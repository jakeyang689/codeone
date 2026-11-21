#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <ctype.h>

/* ==================================================================
 * Parser helpers
 * ================================================================== */

static void parser_set_error(Parser *parser, const char *msg) {
    parser->has_error = true;
    strncpy(parser->error_msg, msg, sizeof(parser->error_msg) - 1);
    parser->error_msg[sizeof(parser->error_msg) - 1] = '\0';
}

static void advance(Parser *parser) {
    if (!parser->has_error) {
        Token tok;
        if (lexer_next(parser->lexer, &tok)) {
            parser->current = tok;
        } else {
            /* EOF or error */
            parser->current = tok;
        }
    }
}

static bool check(Parser *parser, TokenKind kind) {
    return parser->current.kind == kind;
}

static bool match(Parser *parser, TokenKind kind) {
    if (check(parser, kind)) {
        advance(parser);
        return true;
    }
    return false;
}

static void parser_error(Parser *parser, const char *fmt, ...);

static bool expect(Parser *parser, TokenKind kind) {
    if (check(parser, kind)) {
        advance(parser);
        return true;
    }
    parser_error(parser, "expected %s, got %s",
                 token_kind_name(kind),
                 token_kind_name(parser->current.kind));
    return false;
}

static void parser_error_v(Parser *parser, SourceLoc loc,
                           const char *fmt, va_list args) {
    /* Keep the first error: later ones are usually fallout from it. */
    if (parser->has_error) return;
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    snprintf(parser->error_msg, sizeof(parser->error_msg),
             "line %d:%d: %s", loc.line, loc.column, buf);
    parser->has_error = true;
}

static void parser_error(Parser *parser, const char *fmt, ...) {
    SourceLoc loc = { parser->current.line, parser->current.column };
    va_list args;
    va_start(args, fmt);
    parser_error_v(parser, loc, fmt, args);
    va_end(args);
}

/* Like parser_error, but reports a specific source location. */
static void parser_error_at(Parser *parser, SourceLoc loc,
                            const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    parser_error_v(parser, loc, fmt, args);
    va_end(args);
}

/* ==================================================================
 * Type construction and destruction
 * ================================================================== */

static AstNode *make_type(AstNodeKind kind) {
    AstNode *t = (AstNode *)calloc(1, sizeof(AstNode));
    t->kind = kind;
    return t;
}

static AstNode *make_basic_type(AstNodeKind basic) {
    AstNode *t = make_type(AST_TYPE_VOID);
    t->kind = AST_TYPE_PTR; /* temporary, overwritten below */
    t->basic = basic;
    t->kind = basic; /* basic types ARE AstNodeKind values */
    return t;
}

static AstNode *make_ptr_type(AstNode *inner) {
    AstNode *t = make_type(AST_TYPE_PTR);
    t->ptr_to = inner;
    return t;
}

static AstNode *make_array_type(AstNode *elem, int64_t size) {
    AstNode *t = make_type(AST_TYPE_ARRAY);
    t->array.elem = elem;
    t->array.size = size;
    return t;
}

static AstNode *make_struct_type(const char *name) {
    AstNode *t = make_type(AST_TYPE_STRUCT);
    t->struct_name = strdup(name);
    return t;
}

void node_free(AstNode *type) {
    if (!type) return;
    if (type->kind == AST_TYPE_PTR) {
        node_free(type->ptr_to);
    } else if (type->kind == AST_TYPE_ARRAY) {
        node_free(type->array.elem);
    } else if (type->kind == AST_TYPE_STRUCT) {
        free(type->struct_name);
    }
    free(type);
}

/* ==================================================================
 * LexTypeKind → AstNodeKind conversion
 * ================================================================== */

static AstNodeKind lex_to_ast_type(LexTypeKind ltk) {
    /* The basic types are intentionally 1:1 in value */
    return (AstNodeKind)ltk;
}

/* ==================================================================
 * Type parsing (recursive)
 * ================================================================== */

static AstNode *parse_type(Parser *parser);

static AstNode *parse_type(Parser *parser) {
    if (check(parser, TK_STAR)) {
        advance(parser);
        AstNode *inner = parse_type(parser);
        if (!inner) return NULL;
        return make_ptr_type(inner);
    }
    if (check(parser, TK_LBRACK)) {
        advance(parser);
        AstNode *elem = parse_type(parser);
        if (!elem) return NULL;
        if (!expect(parser, TK_COMMA)) { node_free(elem); return NULL; }
        if (!check(parser, TK_INT)) {
            parser_error(parser, "expected array size integer");
            node_free(elem);
            return NULL;
        }
        int64_t size = parser->current.int_val;
        advance(parser);
        if (!expect(parser, TK_RBRACK)) { node_free(elem); return NULL; }
        return make_array_type(elem, size);
    }
    if (check(parser, TK_TYPE)) {
        AstNodeKind basic = lex_to_ast_type(parser->current.lex_type);
        advance(parser);
        return make_basic_type(basic);
    }
    if (check(parser, TK_VOID)) {
        advance(parser);
        return make_basic_type(AST_TYPE_VOID);
    }
    /* Check for @StructName type reference */
    if (check(parser, TK_AT_SYM)) {
        /* Advance first to preserve start/length before any potential error */
        const char *sname_start = parser->current.start;
        int sname_len = parser->current.length;
        advance(parser);
        for (int i = 0; i < parser->struct_name_count; i++) {
            if ((int)strlen(parser->struct_names[i]) == sname_len &&
                memcmp(parser->struct_names[i], sname_start, (size_t)sname_len) == 0) {
                char *name = strndup(sname_start, sname_len);
                return make_struct_type(name);
            }
        }
        /* Not a known struct name — rewind? No, we already advanced.
           The error message will say what we got. */
        parser_error(parser, "undefined struct type @%.*s", sname_len, sname_start);
        return NULL;
    }
    parser_error(parser, "expected type, got %s", token_kind_name(parser->current.kind));
    return NULL;
}

/* ==================================================================
 * Operand parsing
 * ================================================================== */

/* Forward declaration for string_to_bytes used in parse_operand */
static int string_to_bytes(const char *start, int raw_len, int64_t *out, int out_cap);

static Operand parse_operand(Parser *parser) {
    Operand op;
    memset(&op, 0, sizeof(op));

    switch (parser->current.kind) {
        case TK_PCT_SYM: {
            op.kind = OPERAND_LOCAL;
            op.as.local_name = strndup(parser->current.start, parser->current.length);
            advance(parser);
            break;
        }
        case TK_AT_SYM: {
            op.kind = OPERAND_GLOBAL;
            op.as.global_name = strndup(parser->current.start, parser->current.length);
            advance(parser);
            break;
        }
        case TK_INT:
            op.kind = OPERAND_INT;
            op.as.int_val = parser->current.int_val;
            advance(parser);
            break;
        case TK_FLOAT:
            op.kind = OPERAND_FLOAT;
            op.as.float_val = parser->current.float_val;
            advance(parser);
            break;
        case TK_TRUE:
            op.kind = OPERAND_BOOL;
            op.as.bool_val = true;
            advance(parser);
            break;
        case TK_FALSE:
            op.kind = OPERAND_BOOL;
            op.as.bool_val = false;
            advance(parser);
            break;
        case TK_NULL:
            op.kind = OPERAND_NULL;
            advance(parser);
            break;
        case TK_STR: {
            /* String literal in call argument: auto-create anonymous const */
            int raw_len = parser->current.length;
            const char *raw = parser->current.start;
            int64_t *items = (int64_t *)calloc((size_t)raw_len + 2, sizeof(int64_t));
            int count = string_to_bytes(raw, raw_len, items, raw_len + 1);
            /* An empty literal still needs its terminating NUL so that the
               array has a well-formed [i8, 1] initializer. */
            if (count == 0 || items[count - 1] != 0)
                items[count++] = 0;
            advance(parser);

            /* Build [i8, count] type */
            AstNode *arr_type = (AstNode *)calloc(1, sizeof(AstNode));
            arr_type->kind = AST_TYPE_ARRAY;
            arr_type->array.elem = (AstNode *)calloc(1, sizeof(AstNode));
            arr_type->array.elem->kind = AST_TYPE_I8;
            arr_type->array.size = count;

            /* Generate unique name: _s<fileid>_<N>. Stored without the '@'
               sigil like every other global, so the emitted symbol is a plain
               identifier. */
            static int global_anon_id = 0;
            char name_buf[48];
            snprintf(name_buf, sizeof(name_buf), "_s%d_%d", global_anon_id++, parser->anon_counter++);
            char *decl_name = strdup(name_buf);
            op.as.global_name = strdup(name_buf);

            /* Add to program globals as a const */
            if (parser->prog) {
                Program *prog = parser->prog;
                if (prog->global_count >= prog->global_capacity) {
                    prog->global_capacity = prog->global_capacity ? prog->global_capacity * 2 : 4;
                    prog->globals = (GlobalDecl *)realloc(
                        prog->globals, (size_t)prog->global_capacity * sizeof(GlobalDecl));
                }
                GlobalDecl decl;
                memset(&decl, 0, sizeof(decl));
                decl.name = decl_name;
                decl.type = arr_type;
                decl.is_const = true;
                /* The counter restarts in every translation unit, so the name
                   repeats across objects; internal linkage keeps them apart. */
                decl.is_internal = true;
                decl.has_init = true;
                decl.array_init = items;
                decl.array_count = count;
                decl.loc.line = parser->current.line;
                decl.loc.column = parser->current.column;
                prog->globals[prog->global_count++] = decl;
            } else {
                free(decl_name);
                node_free(arr_type);
                free(items);
            }

            op.kind = OPERAND_GLOBAL;
            break;
        }
        default:
            parser_error(parser,
                         "expected operand (%%name, @name, or literal), got '%.*s'",
                         parser->current.length, parser->current.start);
            break;
    }
    return op;
}

/* ==================================================================
 * Dynamic-array helpers for BasicBlock
 * ================================================================== */

static void block_add_insn(BasicBlock *block, Insn insn) {
    if (block->insn_count >= block->insn_capacity) {
        block->insn_capacity = block->insn_capacity ? block->insn_capacity * 2 : 8;
        block->insns = (Insn *)realloc(block->insns,
                                        (size_t)block->insn_capacity * sizeof(Insn));
    }
    block->insns[block->insn_count++] = insn;
}

/* ==================================================================
 * Instruction parsing
 * ================================================================== */

static Insn parse_let_insn(Parser *parser, const char *result) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = INSN_LET;
    insn.result = result ? strdup(result) : NULL;
    insn.type = parse_type(parser);
    if (match(parser, TK_COMMA)) {
        if (!check(parser, TK_INT)) {
            parser_error(parser, "expected alignment integer");
        } else {
            insn.data.let.align = (int)parser->current.int_val;
            advance(parser);
        }
    }
    return insn;
}



static Insn parse_binary_insn(Parser *parser, InsnKind kind, const char *result) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = kind;
    insn.result = result ? strdup(result) : NULL;
    insn.type = parse_type(parser);
    insn.data.binary.lhs = parse_operand(parser);
    expect(parser, TK_COMMA);
    insn.data.binary.rhs = parse_operand(parser);
    return insn;
}

static Insn parse_load_insn(Parser *parser, const char *result) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = INSN_LOAD;
    insn.result = result ? strdup(result) : NULL;
    insn.type = parse_type(parser);
    expect(parser, TK_COMMA);
    expect(parser, TK_STAR);
    insn.data.load.ptr = parse_operand(parser);
    return insn;
}

static Insn parse_store_insn(Parser *parser) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = INSN_STORE;
    insn.type = parse_type(parser);
    insn.data.store.value = parse_operand(parser);
    expect(parser, TK_COMMA);
    expect(parser, TK_STAR);
    insn.data.store.ptr = parse_operand(parser);
    return insn;
}

static MemoryOrdering parse_ordering(Parser *parser) {
    switch (parser->current.kind) {
        case TK_SEQ_CST:  advance(parser); return ORDERING_SEQ_CST;
        case TK_ACQUIRE:  advance(parser); return ORDERING_ACQUIRE;
        case TK_RELEASE:  advance(parser); return ORDERING_RELEASE;
        case TK_ACQ_REL:  advance(parser); return ORDERING_ACQ_REL;
        case TK_RELAXED:  advance(parser); return ORDERING_RELAXED;
        default:
            parser_error(parser, "expected memory ordering (seq_cst/acquire/release/acq_rel/relaxed)");
            return ORDERING_SEQ_CST;
    }
}

static Insn parse_load_atomic_insn(Parser *parser, const char *result) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = INSN_LOAD_ATOMIC;
    insn.result = result ? strdup(result) : NULL;
    insn.type = parse_type(parser);
    expect(parser, TK_COMMA);
    expect(parser, TK_STAR);
    insn.data.load_atomic.ptr = parse_operand(parser);
    expect(parser, TK_COMMA);
    insn.data.load_atomic.ordering = parse_ordering(parser);
    return insn;
}

static Insn parse_store_atomic_insn(Parser *parser) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = INSN_STORE_ATOMIC;
    insn.type = parse_type(parser);
    insn.data.store_atomic.value = parse_operand(parser);
    expect(parser, TK_COMMA);
    expect(parser, TK_STAR);
    insn.data.store_atomic.ptr = parse_operand(parser);
    expect(parser, TK_COMMA);
    insn.data.store_atomic.ordering = parse_ordering(parser);
    return insn;
}

static int parse_atomic_op(Parser *parser) {
    switch (parser->current.kind) {
        case TK_XCHG:  advance(parser); return ATOMICOP_XCHG;
        case TK_ADD:   advance(parser); return ATOMICOP_ADD;
        case TK_SUB:   advance(parser); return ATOMICOP_SUB;
        case TK_AND:   advance(parser); return ATOMICOP_AND;
        case TK_OR:    advance(parser); return ATOMICOP_OR;
        case TK_XOR:   advance(parser); return ATOMICOP_XOR;
        default:
            parser_error(parser, "expected atomic operation (xchg/add/sub/and/or/xor)");
            return ATOMICOP_XCHG;
    }
}

static Insn parse_atomicrmw_insn(Parser *parser, const char *result) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = INSN_ATOMICRMW;
    insn.result = result ? strdup(result) : NULL;
    insn.data.atomicrmw.atomic_op = parse_atomic_op(parser);
    expect(parser, TK_COMMA);
    insn.type = parse_type(parser);
    expect(parser, TK_COMMA);
    expect(parser, TK_STAR);
    insn.data.atomicrmw.ptr = parse_operand(parser);
    expect(parser, TK_COMMA);
    insn.data.atomicrmw.value = parse_operand(parser);
    expect(parser, TK_COMMA);
    insn.data.atomicrmw.ordering = parse_ordering(parser);
    return insn;
}

static Insn parse_cmpxchg_insn(Parser *parser, const char *result) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = INSN_CMPXCHG;
    insn.result = result ? strdup(result) : NULL;
    insn.type = parse_type(parser);
    expect(parser, TK_COMMA);
    expect(parser, TK_STAR);
    insn.data.cmpxchg.ptr = parse_operand(parser);
    expect(parser, TK_COMMA);
    insn.data.cmpxchg.cmp = parse_operand(parser);
    expect(parser, TK_COMMA);
    insn.data.cmpxchg.new_val = parse_operand(parser);
    expect(parser, TK_COMMA);
    insn.data.cmpxchg.succ_ordering = parse_ordering(parser);
    expect(parser, TK_COMMA);
    insn.data.cmpxchg.fail_ordering = parse_ordering(parser);
    return insn;
}

static Insn parse_gep_insn(Parser *parser, const char *result) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = INSN_GEP;
    insn.result = result ? strdup(result) : NULL;
    insn.type = parse_type(parser);
    expect(parser, TK_COMMA);
    expect(parser, TK_STAR);
    insn.data.gep.base = parse_operand(parser);
    expect(parser, TK_COMMA);
    insn.data.gep.index = parse_operand(parser);
    return insn;
}

static Insn parse_field_insn(Parser *parser, const char *result) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = INSN_FIELD;
    insn.result = result ? strdup(result) : NULL;
    insn.data.field.struct_type = parse_type(parser);
    if (!insn.data.field.struct_type) return insn;
    expect(parser, TK_COMMA);
    expect(parser, TK_STAR);
    insn.data.field.base = parse_operand(parser);
    expect(parser, TK_COMMA);
    insn.data.field.field_index = parse_operand(parser);
    return insn;
}

static Insn parse_jmp_insn(Parser *parser) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = INSN_JMP;
    if (!check(parser, TK_AT_SYM)) {
        parser_error(parser, "expected label after jmp");
        return insn;
    }
    insn.data.jmp.target = strndup(parser->current.start, parser->current.length);
    advance(parser);
    return insn;
}

static Insn parse_br_insn(Parser *parser) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = INSN_BR;
    insn.data.br.cond = parse_operand(parser);
    expect(parser, TK_COMMA);
    if (!check(parser, TK_AT_SYM)) {
        parser_error(parser, "expected label after br condition");
        return insn;
    }
    insn.data.br.true_target = strndup(parser->current.start, parser->current.length);
    advance(parser);
    expect(parser, TK_COMMA);
    if (!check(parser, TK_AT_SYM)) {
        parser_error(parser, "expected false label in br");
        return insn;
    }
    insn.data.br.false_target = strndup(parser->current.start, parser->current.length);
    advance(parser);
    return insn;
}

static Insn parse_phi_insn(Parser *parser, const char *result) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = INSN_PHI;
    insn.result = result ? strdup(result) : NULL;

    /* Optional explicit type: `phi i32 [ v, @l ], ...`. Without it the
       lowering assumes i32, which is only correct for 32-bit values. */
    if (check(parser, TK_TYPE) || check(parser, TK_STAR) || check(parser, TK_AT_SYM))
        insn.type = parse_type(parser);

    int count = 0, cap = 0;
    PhiCase *cases = NULL;

    do {
        if (!expect(parser, TK_LBRACK)) goto phi_cleanup;
        Operand val = parse_operand(parser);
        if (!expect(parser, TK_COMMA)) goto phi_cleanup;
        if (!check(parser, TK_AT_SYM)) {
            parser_error(parser, "expected label in phi case");
            goto phi_cleanup;
        }
        char *lbl = strndup(parser->current.start, parser->current.length);
        advance(parser);
        if (!expect(parser, TK_RBRACK)) { free(lbl); goto phi_cleanup; }

        if (count >= cap) {
            cap = cap ? cap * 2 : 4;
            cases = (PhiCase *)realloc(cases, (size_t)cap * sizeof(PhiCase));
        }
        cases[count].value = val;
        cases[count].label = lbl;
        count++;
    } while (match(parser, TK_COMMA));

    insn.data.phi.cases = cases;
    insn.data.phi.count = count;
    return insn;

phi_cleanup:
    for (int i = 0; i < count; i++) {
        operand_free(&cases[i].value);
        free(cases[i].label);
    }
    free(cases);
    insn.data.phi.cases = NULL;
    insn.data.phi.count = 0;
    return insn;
}

static Insn parse_ret_insn(Parser *parser) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = INSN_RET;

    if (match(parser, TK_VOID)) {
        insn.data.ret.is_void = true;
        return insn;
    }
    insn.type = parse_type(parser);
    insn.data.ret.value = parse_operand(parser);
    insn.data.ret.is_void = false;
    return insn;
}

static Insn parse_call_insn(Parser *parser, const char *result) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = INSN_CALL;
    insn.result = result ? strdup(result) : NULL;

    if (!check(parser, TK_AT_SYM)) {
        parser_error(parser, "expected function name after call");
        return insn;
    }
    insn.data.call.func = strndup(parser->current.start, parser->current.length);
    advance(parser);
    expect(parser, TK_LPAREN);

    int argc = 0, cap = 0;
    Operand *args = NULL;
    /* Bail out on the first error: a failed expect() records the problem but
       does not consume the offending token, so an unchecked loop would spin
       on it forever instead of reporting the syntax error. */
    while (!parser->has_error &&
           !check(parser, TK_RPAREN) && !check(parser, TK_EOF)) {
        if (argc > 0) {
            expect(parser, TK_COMMA);
            if (parser->has_error) break;
        }
        if (argc >= cap) {
            cap = cap ? cap * 2 : 4;
            args = (Operand *)realloc(args, (size_t)cap * sizeof(Operand));
        }
        args[argc++] = parse_operand(parser);
    }
    expect(parser, TK_RPAREN);
    insn.data.call.args = args;
    insn.data.call.argc = argc;
    return insn;
}

/* Decode a TK_STR token into bytes. Raw newlines are rejected so that a
   multi-line asm template cannot desynchronize line numbers; use \n instead.
   Returns a NUL-terminated buffer and stores the byte count in *out_len. */
static char *parse_asm_string(Parser *parser, const char *what, int *out_len) {
    if (!check(parser, TK_STR)) {
        parser_error(parser, "expected %s string in asm, got %s",
                     what, token_kind_name(parser->current.kind));
        return NULL;
    }
    int raw_len = parser->current.length;
    const char *raw = parser->current.start;
    for (int i = 0; i < raw_len; i++) {
        if (raw[i] == '\n') {
            parser_error(parser, "asm %s must use \\n instead of a raw newline", what);
            return NULL;
        }
    }
    int64_t *items = (int64_t *)calloc((size_t)raw_len + 1, sizeof(int64_t));
    if (!items) {
        parser_error(parser, "out of memory");
        return NULL;
    }
    int n = string_to_bytes(raw, raw_len, items, raw_len + 1);
    char *out = (char *)malloc((size_t)n + 1);
    if (!out) {
        free(items);
        parser_error(parser, "out of memory");
        return NULL;
    }
    for (int i = 0; i < n; i++)
        out[i] = (char)(unsigned char)items[i];
    out[n] = '\0';
    free(items);
    advance(parser);
    *out_len = n;
    return out;
}

/* Split a constraint list by its leading character. LLVM requires outputs
   first, then inputs, then clobbers; only the first two consume operands.
   Label constraints (!...) belong to asm goto and are reported separately. */
static bool count_constraints(const char *s, int *outputs, int *inputs) {
    int out = 0, in = 0;
    bool has_label = false;
    const char *p = s;
    while (*p) {
        if (*p == '=') out++;
        else if (*p == '!') has_label = true;
        else if (*p != '~') in++;
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    *outputs = out;
    *inputs = in;
    return has_label;
}

/* Rewrite ${name} / ${name:mod} to LLVM's positional ${N} / ${N:mod}, where
   name is an input operand's %local name and N is its operand number (outputs
   first). Positional ${N}, modifier-only ${:uid} and $$ copy through as-is. */
static char *resolve_named_operands(const Insn *insn, int first_input,
                                    int *out_len, char *err, size_t err_sz) {
    const char *s = insn->data.asm_insn.text;
    int n = insn->data.asm_insn.text_len;
    int cap = n * 4 + 16;
    char *out = (char *)malloc((size_t)cap);
    if (!out) {
        snprintf(err, err_sz, "out of memory");
        return NULL;
    }
    int o = 0;
    for (int i = 0; i < n; ) {
        /* $$ is LLVM's escaped literal dollar; never a named reference. */
        if (s[i] == '$' && i + 1 < n && s[i + 1] == '$') {
            out[o++] = '$';
            out[o++] = '$';
            i += 2;
            continue;
        }
        if (s[i] != '$' || i + 1 >= n || s[i + 1] != '{') {
            out[o++] = s[i++];
            continue;
        }
        int close = -1;
        for (int j = i + 2; j < n; j++) {
            if (s[j] == '}') { close = j; break; }
        }
        if (close < 0) {
            out[o++] = s[i++];
            continue;
        }
        int ns = i + 2, ne = ns;
        while (ne < close && s[ne] != ':') ne++;
        int name_len = ne - ns;
        bool numeric = name_len == 0 || (s[ns] >= '0' && s[ns] <= '9');
        if (numeric) {
            for (int k = i; k <= close; k++) out[o++] = s[k];
            i = close + 1;
            continue;
        }
        int matches = 0, found = -1;
        for (int k = 0; k < insn->data.asm_insn.operand_count; k++) {
            const Operand *op = &insn->data.asm_insn.operands[k];
            if (op->kind != OPERAND_LOCAL) continue;
            const char *on = op->as.local_name;
            if ((int)strlen(on) == name_len &&
                memcmp(on, s + ns, (size_t)name_len) == 0) {
                matches++;
                found = k;
            }
        }
        if (matches == 0) {
            snprintf(err, err_sz, "unknown asm operand name '%.*s' in template",
                     name_len, s + ns);
            free(out);
            return NULL;
        }
        if (matches > 1) {
            snprintf(err, err_sz, "asm operand name '%.*s' is ambiguous "
                     "(used by more than one operand)", name_len, s + ns);
            free(out);
            return NULL;
        }
        o += snprintf(out + o, (size_t)(cap - o), "${%d", first_input + found);
        for (int k = ne; k < close; k++) out[o++] = s[k];
        out[o++] = '}';
        i = close + 1;
    }
    out[o] = '\0';
    *out_len = o;
    return out;
}

/* asm [alignstack] <att|intel> <ret-type>, "<template>", "<constraints>"
   [, <type> <operand>]* */
static Insn parse_asm_insn(Parser *parser, const char *result) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = INSN_ASM;
    insn.result = result ? strdup(result) : NULL;

    /* Validation runs after the operands are consumed, so report against the
       start of the instruction instead of whatever token we stop on. */
    SourceLoc start = { parser->current.line, parser->current.column };

    if (check(parser, TK_ALIGNSTACK)) {
        insn.data.asm_insn.align_stack = true;
        advance(parser);
    }

    if (check(parser, TK_ATT)) {
        insn.data.asm_insn.intel_dialect = false;
        advance(parser);
    } else if (check(parser, TK_INTEL)) {
        insn.data.asm_insn.intel_dialect = true;
        advance(parser);
    } else {
        parser_error_at(parser, start, "expected 'att' or 'intel' after asm, got %s",
                        token_kind_name(parser->current.kind));
        return insn;
    }

    insn.type = parse_type(parser);
    if (!insn.type || parser->has_error) return insn;
    if (!expect(parser, TK_COMMA)) return insn;

    insn.data.asm_insn.text =
        parse_asm_string(parser, "template", &insn.data.asm_insn.text_len);
    if (!insn.data.asm_insn.text || parser->has_error) return insn;
    if (!expect(parser, TK_COMMA)) return insn;

    insn.data.asm_insn.constraints =
        parse_asm_string(parser, "constraint", &insn.data.asm_insn.constraints_len);
    if (!insn.data.asm_insn.constraints || parser->has_error) return insn;

    int count = 0, cap = 0;
    Operand *operands = NULL;
    AstNode **types = NULL;
    while (!parser->has_error && check(parser, TK_COMMA)) {
        advance(parser);
        AstNode *t = parse_type(parser);
        if (!t) break;
        Operand op = parse_operand(parser);
        if (parser->has_error) {
            node_free(t);
            operand_free(&op);
            break;
        }
        if (count >= cap) {
            cap = cap ? cap * 2 : 4;
            operands = (Operand *)realloc(operands, (size_t)cap * sizeof(Operand));
            types = (AstNode **)realloc(types, (size_t)cap * sizeof(AstNode *));
        }
        operands[count] = op;
        types[count] = t;
        count++;
    }
    insn.data.asm_insn.operands = operands;
    insn.data.asm_insn.operand_types = types;
    insn.data.asm_insn.operand_count = count;
    if (parser->has_error) return insn;

    bool is_void = insn.type->kind == AST_TYPE_VOID;
    bool is_struct = insn.type->kind == AST_TYPE_STRUCT;
    int outputs = 0, inputs = 0;
    bool has_label =
        count_constraints(insn.data.asm_insn.constraints, &outputs, &inputs);

    if (has_label) {
        parser_error_at(parser, start,
                        "asm goto label constraints (!...) are not supported");
    } else if (is_void && result) {
        parser_error_at(parser, start, "asm with type void cannot define %%%s", result);
    } else if (!is_void && !result) {
        parser_error_at(parser, start, "asm with a result type needs a %%result");
    } else if (result && is_struct && outputs < 1) {
        parser_error_at(parser, start,
                        "asm with a struct result needs at least one output "
                        "(=) constraint");
    } else if (result && !is_struct && outputs != 1) {
        parser_error_at(parser, start,
                        "asm must have exactly one output (=) constraint, found %d",
                        outputs);
    } else if (!result && outputs != 0) {
        parser_error_at(parser, start,
                        "asm has %d output constraints but no %%result", outputs);
    } else if (inputs != count) {
        parser_error_at(parser, start,
                        "asm has %d input constraint(s) but %d operand(s); "
                        "clobbers (~{...}) take no operand",
                        inputs, count);
    }
    if (parser->has_error) return insn;

    char name_err[160];
    int resolved_len = 0;
    char *resolved = resolve_named_operands(&insn, outputs, &resolved_len,
                                            name_err, sizeof(name_err));
    if (!resolved) {
        parser_error_at(parser, start, "%s", name_err);
        return insn;
    }
    free(insn.data.asm_insn.text);
    insn.data.asm_insn.text = resolved;
    insn.data.asm_insn.text_len = resolved_len;
    return insn;
}

static Insn parse_convert_insn(Parser *parser, InsnKind kind, const char *result) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = kind;
    insn.result = result ? strdup(result) : NULL;
    insn.data.convert.from_type = parse_type(parser);
    insn.data.convert.value = parse_operand(parser);
    if (!expect(parser, TK_TO)) {
        node_free(insn.data.convert.from_type);
        operand_free(&insn.data.convert.value);
        memset(&insn, 0, sizeof(insn));
        return insn;
    }
    insn.data.convert.to_type = parse_type(parser);
    return insn;
}

/* tensor <op>, <elemtype>, [d0, d1(, d2)], *%dst, *%a, *%b
 *
 * The operator token fixes the shape arity: elementwise ops take [rows, cols],
 * matmul takes [M, K, N]. No result value: the destination buffer is explicit.
 */
static Insn parse_tensor_insn(Parser *parser) {
    Insn insn;
    memset(&insn, 0, sizeof(insn));
    insn.kind = INSN_TENSOR;

    switch (parser->current.kind) {
        case TK_ADD:    insn.data.tensor.op = TENSOR_OP_ADD; break;
        case TK_SUB:    insn.data.tensor.op = TENSOR_OP_SUB; break;
        case TK_MUL:    insn.data.tensor.op = TENSOR_OP_MUL; break;
        case TK_DIV:    insn.data.tensor.op = TENSOR_OP_DIV; break;
        case TK_MATMUL: insn.data.tensor.op = TENSOR_OP_MATMUL; break;
        default:
            parser_error(parser,
                         "expected tensor op (add, sub, mul, div, matmul), got %s",
                         token_kind_name(parser->current.kind));
            return insn;
    }
    advance(parser);

    if (!expect(parser, TK_COMMA)) return insn;
    insn.data.tensor.elem_type = parse_type(parser);
    if (!insn.data.tensor.elem_type) return insn;

    if (!expect(parser, TK_COMMA)) return insn;
    if (!expect(parser, TK_LBRACK)) return insn;

    int ndim = (insn.data.tensor.op == TENSOR_OP_MATMUL) ? 3 : 2;
    for (int i = 0; i < ndim; i++) {
        if (i > 0 && !expect(parser, TK_COMMA)) return insn;
        insn.data.tensor.dims[i] = parse_operand(parser);
        if (parser->has_error) return insn;
    }
    insn.data.tensor.ndim = ndim;
    if (!expect(parser, TK_RBRACK)) return insn;

    if (!expect(parser, TK_COMMA)) return insn;
    if (!expect(parser, TK_STAR)) return insn;
    insn.data.tensor.dst = parse_operand(parser);
    if (parser->has_error) return insn;

    if (!expect(parser, TK_COMMA)) return insn;
    if (!expect(parser, TK_STAR)) return insn;
    insn.data.tensor.lhs = parse_operand(parser);
    if (parser->has_error) return insn;

    if (!expect(parser, TK_COMMA)) return insn;
    if (!expect(parser, TK_STAR)) return insn;
    insn.data.tensor.rhs = parse_operand(parser);
    return insn;
}

/* ==================================================================
 * Main instruction dispatcher
 * ================================================================== */

static void parse_insn(Parser *parser, BasicBlock *block) {
    /* Detect:  %result = INSTRUCTION ... */
    if (check(parser, TK_PCT_SYM)) {
        SourceLoc loc;
        loc.line = parser->current.line;
        loc.column = parser->current.column;
        char *result = strndup(parser->current.start, parser->current.length);
        advance(parser);
        if (!expect(parser, TK_ASSIGN)) { free(result); return; }

        Insn insn;
        memset(&insn, 0, sizeof(insn));
        insn.loc = loc;

        switch (parser->current.kind) {
            case TK_LET: advance(parser); insn = parse_let_insn(parser, result); break;
            case TK_ADD:   advance(parser); insn = parse_binary_insn(parser, INSN_ADD,   result); break;
            case TK_SUB:   advance(parser); insn = parse_binary_insn(parser, INSN_SUB,   result); break;
            case TK_MUL:   advance(parser); insn = parse_binary_insn(parser, INSN_MUL,   result); break;
            case TK_DIV:   advance(parser); insn = parse_binary_insn(parser, INSN_DIV,   result); break;
            case TK_REM:   advance(parser); insn = parse_binary_insn(parser, INSN_REM,   result); break;
            case TK_AND:   advance(parser); insn = parse_binary_insn(parser, INSN_AND,   result); break;
            case TK_OR:    advance(parser); insn = parse_binary_insn(parser, INSN_OR,    result); break;
            case TK_XOR:   advance(parser); insn = parse_binary_insn(parser, INSN_XOR,   result); break;
            case TK_SHL:   advance(parser); insn = parse_binary_insn(parser, INSN_SHL,   result); break;
            case TK_LSHR:  advance(parser); insn = parse_binary_insn(parser, INSN_LSHR,  result); break;
            case TK_ASHR:  advance(parser); insn = parse_binary_insn(parser, INSN_ASHR,  result); break;
            case TK_EQ:    advance(parser); insn = parse_binary_insn(parser, INSN_EQ,    result); break;
            case TK_NE:    advance(parser); insn = parse_binary_insn(parser, INSN_NE,    result); break;
            case TK_SLT:   advance(parser); insn = parse_binary_insn(parser, INSN_SLT,   result); break;
            case TK_SLE:   advance(parser); insn = parse_binary_insn(parser, INSN_SLE,   result); break;
            case TK_SGT:   advance(parser); insn = parse_binary_insn(parser, INSN_SGT,   result); break;
            case TK_SGE:   advance(parser); insn = parse_binary_insn(parser, INSN_SGE,   result); break;
            case TK_LOAD: {
                advance(parser);
                /* Check for 'load atomic' */
                if (check(parser, TK_ATOMIC)) {
                    advance(parser);
                    insn = parse_load_atomic_insn(parser, result);
                } else {
                    insn = parse_load_insn(parser, result);
                }
                break;
            }
            case TK_GEP:   advance(parser); insn = parse_gep_insn(parser, result); break;
            case TK_FIELD: advance(parser); insn = parse_field_insn(parser, result); break;
            case TK_ATOMICRMW: advance(parser); insn = parse_atomicrmw_insn(parser, result); break;
            case TK_CMPXCHG: advance(parser); insn = parse_cmpxchg_insn(parser, result); break;
            case TK_PHI:   advance(parser); insn = parse_phi_insn(parser, result); break;
            case TK_CALL:  advance(parser); insn = parse_call_insn(parser, result); break;
            case TK_ASM:   advance(parser); insn = parse_asm_insn(parser, result); break;
            case TK_ZEXT:  advance(parser); insn = parse_convert_insn(parser, INSN_ZEXT,  result); break;
            case TK_SEXT:  advance(parser); insn = parse_convert_insn(parser, INSN_SEXT,  result); break;
            case TK_TRUNC: advance(parser); insn = parse_convert_insn(parser, INSN_TRUNC, result); break;
            case TK_MULHI:      advance(parser); insn = parse_binary_insn(parser, INSN_MULHI, result); break;
            case TK_UDIV:       advance(parser); insn = parse_binary_insn(parser, INSN_UDIV, result); break;
            case TK_UREM:       advance(parser); insn = parse_binary_insn(parser, INSN_UREM, result); break;
            default:
                parser_error(parser, "unexpected instruction '%s' after '%%s ='",
                             token_kind_name(parser->current.kind));
                free(result);
                return;
        }
        /* The parse_* helpers build a fresh Insn with no location; keep the
           position captured at the result token. */
        insn.loc = loc;
        block_add_insn(block, insn);
        return;
    }

    /* No result: store | jmp | br | ret | call */
    switch (parser->current.kind) {
        case TK_STORE: {
            SourceLoc loc = {parser->current.line, parser->current.column};
            advance(parser);
            Insn insn;
            if (check(parser, TK_ATOMIC)) {
                advance(parser);
                insn = parse_store_atomic_insn(parser);
            } else {
                insn = parse_store_insn(parser);
            }
            insn.loc = loc;
            block_add_insn(block, insn);
            break;
        }
        case TK_JMP: {
            SourceLoc loc = {parser->current.line, parser->current.column};
            advance(parser);
            Insn insn = parse_jmp_insn(parser);
            insn.loc = loc;
            block_add_insn(block, insn);
            break;
        }
        case TK_BR: {
            SourceLoc loc = {parser->current.line, parser->current.column};
            advance(parser);
            Insn insn = parse_br_insn(parser);
            insn.loc = loc;
            block_add_insn(block, insn);
            break;
        }
        case TK_RET: {
            SourceLoc loc = {parser->current.line, parser->current.column};
            advance(parser);
            Insn insn = parse_ret_insn(parser);
            insn.loc = loc;
            block_add_insn(block, insn);
            break;
        }
        case TK_CALL: {
            SourceLoc loc = {parser->current.line, parser->current.column};
            advance(parser);
            Insn insn = parse_call_insn(parser, NULL);
            insn.loc = loc;
            block_add_insn(block, insn);
            break;
        }
        case TK_ASM: {
            SourceLoc loc = {parser->current.line, parser->current.column};
            advance(parser);
            Insn insn = parse_asm_insn(parser, NULL);
            insn.loc = loc;
            block_add_insn(block, insn);
            break;
        }
        case TK_TENSOR: {
            SourceLoc loc = {parser->current.line, parser->current.column};
            advance(parser);
            Insn insn = parse_tensor_insn(parser);
            insn.loc = loc;
            block_add_insn(block, insn);
            break;
        }
        default:
            parser_error(parser, "expected instruction, got %s",
                         token_kind_name(parser->current.kind));
            break;
    }
}

/* ==================================================================
 * Basic block parsing
 * ================================================================== */

static BasicBlock parse_basic_block(Parser *parser) {
    BasicBlock block;
    memset(&block, 0, sizeof(block));

    if (!check(parser, TK_AT_SYM)) {
        parser_error(parser, "expected basic block label");
        return block;
    }
    block.loc.line = parser->current.line;
    block.loc.column = parser->current.column;
    block.label = strndup(parser->current.start, parser->current.length);
    advance(parser);
    expect(parser, TK_COLON);

    /* Parse instructions until next label, '}', or EOF/error */
    while (!check(parser, TK_AT_SYM) &&
           !check(parser, TK_RBRACE) &&
           !check(parser, TK_EOF) &&
           !parser->has_error) {
        parse_insn(parser, &block);
    }
    return block;
}

/* ==================================================================
 * Parameter list parsing
 * ================================================================== */

static void parse_params(Parser *parser, FuncParam **out_params, int *out_count) {
    int count = 0, cap = 0;
    FuncParam *params = NULL;

    if (check(parser, TK_RPAREN)) {
        *out_params = NULL;
        *out_count = 0;
        return;
    }

    do {
        /* Parameter name: currently any identifier-like token is TK_ERROR.
         * Workaround: for now we use TK_AT_SYM as param name in test,
         * or we can look at this differently.
         *
         * In SSA:  fun @name(a: i32, b: i32)
         * Here 'a' and 'b' are bare identifiers. But our lexer doesn't
         * produce TK_IDENT — unknown words become TK_ERROR.
         * FIX: accept a token by storing its text.
         *
         * We'll accept TK_AT_SYM as a param name (user writes @a: i32)
         * AND we need a fallback: if we see a word the lexer doesn't
         * recognize, it's TK_ERROR — but we should accept it as ident.
         *
         * For now, accept TK_AT_SYM as param name (matches fun @name
         * style params).  If the lexer returns TK_ERROR for an unknown
         * word, we try to recover by using its text anyway.
         */
        const char *pname_start;
        int pname_len;
        TokenKind k = parser->current.kind;

        if (k == TK_ELLIPSIS) {
            break;
        }

        if (k == TK_AT_SYM || k == TK_IDENT) {
            pname_start = parser->current.start;
            pname_len = parser->current.length;
            advance(parser);
        } else {
            parser_error(parser, "expected parameter name");
            goto cleanup;
        }

        if (!expect(parser, TK_COLON)) goto cleanup;
        AstNode *ptype = parse_type(parser);
        if (!ptype) goto cleanup;

        if (count >= cap) {
            cap = cap ? cap * 2 : 4;
            params = (FuncParam *)realloc(params, (size_t)cap * sizeof(FuncParam));
        }
        params[count].name = strndup(pname_start, pname_len);
        params[count].type = ptype;
        count++;
    } while (match(parser, TK_COMMA));

    *out_params = params;
    *out_count = count;
    return;

cleanup:
    for (int i = 0; i < count; i++) {
        free(params[i].name);
        node_free(params[i].type);
    }
    free(params);
    *out_params = NULL;
    *out_count = 0;
}

/* Helper: process escape sequences in a string literal and write bytes into out.
   Returns the number of bytes produced. */
static int string_to_bytes(const char *start, int raw_len, int64_t *out, int out_cap) {
    int out_idx = 0;
    int i = 0;
    while (i < raw_len && out_idx < out_cap) {
        if (start[i] == '\\' && i + 1 < raw_len) {
            i++;
            switch (start[i]) {
                case 'n':  out[out_idx++] = '\n'; i++; break;
                case 'r':  out[out_idx++] = '\r'; i++; break;
                case 't':  out[out_idx++] = '\t'; i++; break;
                case '0':  out[out_idx++] = '\0'; i++; break;
                case '\\': out[out_idx++] = '\\'; i++; break;
                case '"':  out[out_idx++] = '"';  i++; break;
                case 'x': {
                    i++;
                    unsigned char c = 0;
                    int digits = 0;
                    while (i < raw_len && digits < 2 && isxdigit((unsigned char)start[i])) {
                        char ch = start[i];
                        if (ch >= '0' && ch <= '9')      c = (unsigned char)((c << 4) | (ch - '0'));
                        else if (ch >= 'a' && ch <= 'f') c = (unsigned char)((c << 4) | (ch - 'a' + 10));
                        else if (ch >= 'A' && ch <= 'F') c = (unsigned char)((c << 4) | (ch - 'A' + 10));
                        digits++;
                        i++;
                    }
                    out[out_idx++] = (int64_t)(unsigned char)c;
                    break;
                }
                default:   out[out_idx++] = (int64_t)(unsigned char)start[i]; i++; break;
            }
        } else {
            out[out_idx++] = (int64_t)(unsigned char)start[i];
            i++;
        }
    }
    return out_idx;
}

/* ==================================================================
 * Top-level declaration parsing
 * ================================================================== */

static GlobalDecl parse_global(Parser *parser, bool is_const, bool is_internal) {
    GlobalDecl decl;
    memset(&decl, 0, sizeof(decl));
    decl.has_init = false;
    decl.is_const = is_const;
    decl.is_internal = is_internal;

    decl.loc.line = parser->current.line;
    decl.loc.column = parser->current.column;
    if (!is_const && !expect(parser, TK_GLOBAL)) return decl;
    if (is_const && !expect(parser, TK_CONST)) return decl;
    if (!check(parser, TK_AT_SYM)) {
        parser_error(parser, "expected global name after 'global'");
        return decl;
    }
    decl.name = strndup(parser->current.start, parser->current.length);
    advance(parser);
    if (check(parser, TK_COLON)) {
        advance(parser);
        decl.type = parse_type(parser);
        if (!decl.type) return decl;
    } else if (!(is_const && check(parser, TK_ASSIGN))) {
        parser_error(parser, "expected ':' and a type after global name");
        return decl;
    }

    if (match(parser, TK_ASSIGN)) {
        /* Check for array literal: [ val, val, ... ] */
        if (check(parser, TK_LBRACK)) {
            advance(parser);
            int count = 0, cap = 0;
            int64_t *items = NULL;
            while (!check(parser, TK_RBRACK) && !check(parser, TK_EOF) && !parser->has_error) {
                if (count > 0) expect(parser, TK_COMMA);
                if (!check(parser, TK_INT)) {
                    parser_error(parser, "expected integer in array initializer");
                    free(items);
                    return decl;
                }
                if (count >= cap) {
                    cap = cap ? cap * 2 : 16;
                    items = (int64_t *)realloc(items, (size_t)cap * sizeof(int64_t));
                }
                items[count++] = parser->current.int_val;
                advance(parser);
            }
            expect(parser, TK_RBRACK);
            decl.array_init = items;
            decl.array_count = count;
            decl.has_init = true;
        } else if (check(parser, TK_STR)) {
            /* String literal: "..." — expand to byte array */
            int raw_len = parser->current.length;
            const char *raw = parser->current.start;
            int64_t *items = (int64_t *)calloc((size_t)raw_len + 2, sizeof(int64_t));
            int count = string_to_bytes(raw, raw_len, items, raw_len + 1);
            /* An empty literal still needs its terminating NUL so that the
               array has a well-formed [i8, 1] initializer. */
            if (count == 0 || items[count - 1] != 0)
                items[count++] = 0;
            decl.array_init = items;
            decl.array_count = count;
            decl.has_init = true;
            if (!decl.type)
                decl.type = make_array_type(make_basic_type(AST_TYPE_I8), count);
            advance(parser);
        } else {
            decl.init_value = parse_operand(parser);
            decl.has_init = true;
        }
    }
    return decl;
}

/* ==================================================================
 * Function-level validation
 * ================================================================== */

/* A %value is defined exactly once per function; parameters are part of the
 * same namespace. Report duplicates by sorting the definitions by name. */
typedef struct {
    const char *name;
    SourceLoc loc;
} LocalDef;

static int local_def_cmp(const void *a, const void *b) {
    const LocalDef *x = (const LocalDef *)a;
    const LocalDef *y = (const LocalDef *)b;
    int c = strcmp(x->name, y->name);
    if (c != 0) return c;
    if (x->loc.line != y->loc.line) return x->loc.line - y->loc.line;
    return x->loc.column - y->loc.column;
}

static void validate_local_names(Parser *parser, const FuncDecl *decl) {
    int total = decl->param_count;
    for (int i = 0; i < decl->block_count; i++)
        for (int j = 0; j < decl->blocks[i].insn_count; j++)
            if (decl->blocks[i].insns[j].result) total++;
    if (total == 0) return;

    LocalDef *defs = (LocalDef *)malloc((size_t)total * sizeof(LocalDef));
    if (!defs) {
        parser_set_error(parser, "out of memory while validating local names");
        return;
    }

    int n = 0;
    for (int i = 0; i < decl->param_count; i++) {
        defs[n].name = decl->params[i].name;
        defs[n].loc = decl->loc;
        n++;
    }
    for (int i = 0; i < decl->block_count; i++) {
        const BasicBlock *bb = &decl->blocks[i];
        for (int j = 0; j < bb->insn_count; j++) {
            if (!bb->insns[j].result) continue;
            defs[n].name = bb->insns[j].result;
            defs[n].loc = bb->insns[j].loc;
            n++;
        }
    }

    qsort(defs, (size_t)n, sizeof(LocalDef), local_def_cmp);
    for (int i = 1; i < n; i++) {
        if (strcmp(defs[i - 1].name, defs[i].name) == 0) {
            parser_error_at(parser, defs[i].loc,
                            "local value %%%s is defined more than once "
                            "(first at line %d)",
                            defs[i].name, defs[i - 1].loc.line);
            break;
        }
    }
    free(defs);
}

/* Block labels are function-scoped and must be unique. */
static void validate_block_labels(Parser *parser, const FuncDecl *decl) {
    for (int i = 0; i < decl->block_count; i++) {
        for (int j = 0; j < i; j++) {
            if (strcmp(decl->blocks[i].label, decl->blocks[j].label) == 0) {
                parser_error_at(parser, decl->blocks[i].loc,
                                "block label @%s is defined more than once "
                                "(first at line %d)",
                                decl->blocks[i].label,
                                decl->blocks[j].loc.line);
                return;
            }
        }
    }
}

static FuncDecl parse_function(Parser *parser) {
    FuncDecl decl;
    memset(&decl, 0, sizeof(decl));

    decl.loc.line = parser->current.line;
    decl.loc.column = parser->current.column;
    if (!expect(parser, TK_FUN)) return decl;
    if (!check(parser, TK_AT_SYM)) {
        parser_error(parser, "expected function name after 'fun'");
        return decl;
    }
    decl.name = strndup(parser->current.start, parser->current.length);
    advance(parser);
    expect(parser, TK_LPAREN);
    parse_params(parser, &decl.params, &decl.param_count);
    expect(parser, TK_RPAREN);
    expect(parser, TK_ARROW);
    decl.return_type = parse_type(parser);
    if (!decl.return_type) return decl;

    expect(parser, TK_LBRACE);

    /* Parse basic blocks until '}' */
    while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF) && !parser->has_error) {
        if (!check(parser, TK_AT_SYM)) {
            parser_error(parser, "expected basic block label inside function body");
            break;
        }
        if (decl.block_count >= decl.block_capacity) {
            decl.block_capacity = decl.block_capacity ? decl.block_capacity * 2 : 8;
            decl.blocks = (BasicBlock *)realloc(
                decl.blocks, (size_t)decl.block_capacity * sizeof(BasicBlock));
        }
        decl.blocks[decl.block_count++] = parse_basic_block(parser);
    }

    /* Validate basic blocks: each must end with a terminator */
    if (!parser->has_error) {
        for (int i = 0; i < decl.block_count; i++) {
            const BasicBlock *bb = &decl.blocks[i];
            if (bb->insn_count == 0) {
                parser_error(parser, "basic block %s has no instructions",
                             bb->label);
                break;
            }
            InsnKind last_kind = bb->insns[bb->insn_count - 1].kind;
            if (last_kind != INSN_JMP && last_kind != INSN_BR && last_kind != INSN_RET) {
                parser_error(parser, "basic block %s does not end with a "
                             "terminator (jmp/br/ret)", bb->label);
                break;
            }
        }
    }

    /* Names are function-scoped: block labels and %values must be unique. */
    if (!parser->has_error) validate_block_labels(parser, &decl);
    if (!parser->has_error) validate_local_names(parser, &decl);

    expect(parser, TK_RBRACE);
    return decl;
}

static ExternDecl parse_extern(Parser *parser) {
    ExternDecl decl;
    memset(&decl, 0, sizeof(decl));

    decl.loc.line = parser->current.line;
    decl.loc.column = parser->current.column;
    if (!expect(parser, TK_EXTERN)) return decl;

    if (!check(parser, TK_AT_SYM)) {
        parser_error(parser, "expected function name after 'extern'");
        return decl;
    }
    decl.name = strndup(parser->current.start, parser->current.length);
    advance(parser);

    expect(parser, TK_LPAREN);
    parse_params(parser, &decl.params, &decl.param_count);
    if (check(parser, TK_ELLIPSIS)) {
        decl.is_variadic = true;
        advance(parser);
    }
    expect(parser, TK_RPAREN);
    expect(parser, TK_ARROW);
    decl.return_type = parse_type(parser);
    if (!decl.return_type) return decl;

    return decl;
}

static StructDecl parse_struct_decl(Parser *parser) {
    StructDecl decl;
    memset(&decl, 0, sizeof(decl));

    decl.loc.line = parser->current.line;
    decl.loc.column = parser->current.column;
    if (!expect(parser, TK_STRUCT)) return decl;

    if (!check(parser, TK_AT_SYM)) {
        parser_error(parser, "expected struct name after 'struct'");
        return decl;
    }
    decl.name = strndup(parser->current.start, parser->current.length);
    advance(parser);

    if (!expect(parser, TK_LBRACE)) return decl;

    int fcount = 0, fcap = 0;
    FuncParam *fields = NULL;

    while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF) && !parser->has_error) {
        if (fcount > 0) expect(parser, TK_COMMA);

        if (!check(parser, TK_AT_SYM)) {
            parser_error(parser, "expected field name");
            goto fields_cleanup;
        }
        const char *fname = parser->current.start;
        int fname_len = parser->current.length;
        advance(parser);

        if (!expect(parser, TK_COLON)) goto fields_cleanup;
        AstNode *ftype = parse_type(parser);
        if (!ftype) goto fields_cleanup;

        if (fcount >= fcap) {
            fcap = fcap ? fcap * 2 : 8;
            fields = (FuncParam *)realloc(fields, (size_t)fcap * sizeof(FuncParam));
        }
        fields[fcount].name = strndup(fname, fname_len);
        fields[fcount].type = ftype;
        fcount++;
    }
    expect(parser, TK_RBRACE);

    decl.fields = fields;
    decl.field_count = fcount;
    return decl;

fields_cleanup:
    for (int i = 0; i < fcount; i++) {
        free(fields[i].name);
        node_free(fields[i].type);
    }
    free(fields);
    decl.fields = NULL;
    decl.field_count = 0;
    return decl;
}

/* Deep-copy a type tree so each declaration owns its own instance. */
static AstNode *clone_type(const AstNode *t) {
    if (!t) return NULL;
    AstNode *n = (AstNode *)calloc(1, sizeof(AstNode));
    if (!n) return NULL;
    n->kind = t->kind;
    switch (t->kind) {
        case AST_TYPE_PTR:
            n->ptr_to = clone_type(t->ptr_to);
            break;
        case AST_TYPE_ARRAY:
            n->array.elem = clone_type(t->array.elem);
            n->array.size = t->array.size;
            break;
        case AST_TYPE_STRUCT:
            n->struct_name = t->struct_name ? strdup(t->struct_name) : NULL;
            break;
        default:
            n->basic = t->basic;
            break;
    }
    return n;
}

/* Enum declaration:
 *
 *   enum @Color: i32 {
 *     @Red = 0,
 *     @Green = 1,
 *     @Blue = 2
 *   }
 *
 * Each member becomes an internal read-only constant named @Color.Red, so it
 * is used anywhere an integer operand is expected (e.g. `add i32 @Color.Red,
 * 1`). Values are integer literals with no implicit auto-increment. Members
 * are file-local, like struct definitions, so every .bx file that uses the
 * enum declares it again without risking link-time duplicate symbols. */
static bool parse_enum_decl(Parser *parser, Program *prog) {
    SourceLoc loc = { parser->current.line, parser->current.column };
    if (!expect(parser, TK_ENUM)) return false;

    if (!check(parser, TK_AT_SYM)) {
        parser_error(parser, "expected enum name after 'enum'");
        return false;
    }
    char *enum_name = strndup(parser->current.start, parser->current.length);
    advance(parser);

    AstNode *underlying = NULL;
    char **seen = NULL;
    int seen_count = 0, seen_cap = 0;
    bool ok = true;

    if (!expect(parser, TK_COLON)) { ok = false; goto cleanup; }
    underlying = parse_type(parser);
    if (!underlying) { ok = false; goto cleanup; }
    switch (underlying->kind) {
        case AST_TYPE_I1: case AST_TYPE_I8: case AST_TYPE_I16:
        case AST_TYPE_I32: case AST_TYPE_I64:
            break;
        default:
            parser_error_at(parser, loc,
                            "enum @%s needs an integer type (i1/i8/i16/i32/i64)",
                            enum_name);
            ok = false;
            goto cleanup;
    }

    if (!expect(parser, TK_LBRACE)) { ok = false; goto cleanup; }

    while (!check(parser, TK_RBRACE) && !check(parser, TK_EOF) && !parser->has_error) {
        if (seen_count > 0 && !expect(parser, TK_COMMA)) { ok = false; break; }

        SourceLoc mloc = { parser->current.line, parser->current.column };
        if (!check(parser, TK_AT_SYM)) {
            parser_error(parser, "expected enum member name");
            ok = false;
            break;
        }
        const char *mstart = parser->current.start;
        int mlen = parser->current.length;
        advance(parser);

        if (!expect(parser, TK_ASSIGN)) { ok = false; break; }
        if (!check(parser, TK_INT)) {
            parser_error(parser, "expected integer value for enum member");
            ok = false;
            break;
        }
        int64_t value = parser->current.int_val;
        advance(parser);

        size_t qlen = strlen(enum_name) + 1 + (size_t)mlen + 1;
        char *qname = (char *)malloc(qlen);
        if (!qname) {
            parser_error(parser, "out of memory");
            ok = false;
            break;
        }
        snprintf(qname, qlen, "%s.%.*s", enum_name, mlen, mstart);

        bool dup = false;
        for (int i = 0; i < seen_count; i++) {
            if (strcmp(seen[i], qname) == 0) {
                parser_error(parser, "duplicate enum member @%s", qname);
                dup = true;
                break;
            }
        }
        if (dup) { free(qname); ok = false; break; }

        if (seen_count >= seen_cap) {
            seen_cap = seen_cap ? seen_cap * 2 : 8;
            char **grown = (char **)realloc(seen, (size_t)seen_cap * sizeof(char *));
            if (!grown) {
                parser_error(parser, "out of memory");
                free(qname);
                ok = false;
                break;
            }
            seen = grown;
        }
        seen[seen_count++] = qname;

        if (prog->global_count >= prog->global_capacity) {
            prog->global_capacity = prog->global_capacity ? prog->global_capacity * 2 : 4;
            prog->globals = (GlobalDecl *)realloc(
                prog->globals, (size_t)prog->global_capacity * sizeof(GlobalDecl));
        }
        GlobalDecl decl;
        memset(&decl, 0, sizeof(decl));
        decl.loc = mloc;
        decl.name = strdup(qname);
        decl.type = clone_type(underlying);
        decl.init_value.kind = OPERAND_INT;
        decl.init_value.as.int_val = value;
        decl.has_init = true;
        decl.is_const = true;
        decl.is_internal = true;
        prog->globals[prog->global_count++] = decl;
    }

    if (ok && !expect(parser, TK_RBRACE)) ok = false;

cleanup:
    for (int i = 0; i < seen_count; i++) free(seen[i]);
    free(seen);
    node_free(underlying);
    free(enum_name);
    return ok;
}

/* Register a struct name so parse_type can recognise @StructName references. */
static void parser_register_struct(Parser *parser, const char *name) {
    for (int i = 0; i < parser->struct_name_count; i++) {
        if (strcmp(parser->struct_names[i], name) == 0) return;
    }
    if (parser->struct_name_count >= parser->struct_name_capacity) {
        parser->struct_name_capacity = parser->struct_name_capacity ? parser->struct_name_capacity * 2 : 16;
        parser->struct_names = (char **)realloc(
            parser->struct_names, (size_t)parser->struct_name_capacity * sizeof(char *));
    }
    parser->struct_names[parser->struct_name_count] = strdup(name);
    parser->struct_name_count++;
}

/* ==================================================================
 * Program parsing
 * ================================================================== */

/* Collect every `struct @Name` up front so a type may be referenced before
   its declaration. Uses a copy of the lexer and never touches parser state. */
static void prescan_structs(Parser *parser) {
    Lexer lex = *parser->lexer;
    Token tok;
    while (lexer_next(&lex, &tok)) {
        if (tok.kind != TK_STRUCT) continue;
        Token name;
        if (!lexer_next(&lex, &name)) break;
        if (name.kind != TK_AT_SYM) continue;
        char *nm = strndup(name.start, (size_t)name.length);
        if (nm) {
            parser_register_struct(parser, nm);
            free(nm);
        }
    }
}

Program *ast_parse(Parser *parser) {
    Program *prog = (Program *)calloc(1, sizeof(Program));
    if (!prog) return NULL;

    parser->prog = prog;
    parser->anon_counter = 0;
    prog->filename = parser->lexer->filename;

    /* Pre-register struct names so types may be used before declaration. */
    prescan_structs(parser);

    /* Read the first token */
    advance(parser);

    while (!check(parser, TK_EOF) && !parser->has_error) {
        bool has_internal = false;
        if (check(parser, TK_INTERNAL)) {
            has_internal = true;
            advance(parser);
        }
        if (check(parser, TK_GLOBAL) || check(parser, TK_CONST)) {
            bool is_const = check(parser, TK_CONST);
            if (prog->global_count >= prog->global_capacity) {
                prog->global_capacity = prog->global_capacity ? prog->global_capacity * 2 : 4;
                prog->globals = (GlobalDecl *)realloc(
                    prog->globals, (size_t)prog->global_capacity * sizeof(GlobalDecl));
            }
            prog->globals[prog->global_count++] = parse_global(parser, is_const, has_internal);
        } else if (check(parser, TK_FUN)) {
            if (prog->func_count >= prog->func_capacity) {
                prog->func_capacity = prog->func_capacity ? prog->func_capacity * 2 : 4;
                prog->functions = (FuncDecl *)realloc(
                    prog->functions, (size_t)prog->func_capacity * sizeof(FuncDecl));
            }
            prog->functions[prog->func_count++] = parse_function(parser);
        } else if (check(parser, TK_STRUCT)) {
            if (prog->struct_count >= prog->struct_capacity) {
                prog->struct_capacity = prog->struct_capacity ? prog->struct_capacity * 2 : 4;
                prog->structs = (StructDecl *)realloc(
                    prog->structs, (size_t)prog->struct_capacity * sizeof(StructDecl));
            }
            StructDecl sd = parse_struct_decl(parser);
            if (sd.name) {
                parser_register_struct(parser, sd.name);
                prog->structs[prog->struct_count++] = sd;
            }
        } else if (check(parser, TK_ENUM)) {
            if (has_internal) {
                parser_error(parser, "enum members are always file-local; "
                             "remove 'internal' before 'enum'");
                break;
            }
            if (!parse_enum_decl(parser, prog)) break;
        } else if (check(parser, TK_EXTERN)) {
            if (prog->extern_count >= prog->extern_capacity) {
                prog->extern_capacity = prog->extern_capacity ? prog->extern_capacity * 2 : 4;
                prog->externs = (ExternDecl *)realloc(
                    prog->externs, (size_t)prog->extern_capacity * sizeof(ExternDecl));
            }
            prog->externs[prog->extern_count++] = parse_extern(parser);
        } else {
            parser_error(parser, "expected top-level declaration, got %s",
                         token_kind_name(parser->current.kind));
            break;
        }
    }

    if (parser->has_error && prog) {
        program_free(prog);
        return NULL;
    }
    return prog;
}

/* ==================================================================
 * Parser init / free
 * ================================================================== */

void ast_init(Parser *parser, Lexer *lexer) {
    parser->lexer = lexer;
    parser->has_error = false;
    parser->error_msg[0] = '\0';
    memset(&parser->current, 0, sizeof(parser->current));
    parser->struct_names = NULL;
    parser->struct_name_count = 0;
    parser->struct_name_capacity = 0;
    parser->prog = NULL;
    parser->anon_counter = 0;
}

void ast_free(Parser *parser) {
    for (int i = 0; i < parser->struct_name_count; i++)
        free(parser->struct_names[i]);
    free(parser->struct_names);
    parser->struct_names = NULL;
    parser->struct_name_count = 0;
    parser->struct_name_capacity = 0;
}

bool ast_has_error(const Parser *parser) {
    return parser->has_error;
}

const char *ast_get_error(const Parser *parser) {
    return parser->error_msg;
}

/* ==================================================================
 * Ordering / AST utility: type / instruction names
 * ================================================================== */

static const char *ordering_name(MemoryOrdering o) {
    switch (o) {
        case ORDERING_SEQ_CST:  return "seq_cst";
        case ORDERING_ACQUIRE:  return "acquire";
        case ORDERING_RELEASE:  return "release";
        case ORDERING_ACQ_REL:  return "acq_rel";
        case ORDERING_RELAXED:  return "relaxed";
        default:                return "?";
    }
}

const char *ast_node_kind_name(AstNodeKind kind) {
    switch (kind) {
        case AST_TYPE_I1:   return "i1";
        case AST_TYPE_I8:   return "i8";
        case AST_TYPE_I16:  return "i16";
        case AST_TYPE_I32:  return "i32";
        case AST_TYPE_I64:  return "i64";
        case AST_TYPE_F32:  return "f32";
        case AST_TYPE_F64:  return "f64";
        case AST_TYPE_VOID: return "void";
        case AST_TYPE_PTR:  return "ptr";
        case AST_TYPE_ARRAY:return "array";
        case AST_TYPE_STRUCT:return "struct";
        default:            return "?";
    }
}

const char *insn_kind_name(InsnKind kind) {
    switch (kind) {
        case INSN_LET:   return "let";
        case INSN_ADD:   return "add";
        case INSN_SUB:   return "sub";
        case INSN_MUL:   return "mul";
        case INSN_DIV:   return "div";
        case INSN_REM:   return "rem";
        case INSN_AND:   return "and";
        case INSN_OR:    return "or";
        case INSN_XOR:   return "xor";
        case INSN_SHL:   return "shl";
        case INSN_LSHR:  return "lshr";
        case INSN_ASHR:  return "ashr";
        case INSN_EQ:    return "eq";
        case INSN_NE:    return "ne";
        case INSN_SLT:   return "slt";
        case INSN_SLE:   return "sle";
        case INSN_SGT:   return "sgt";
        case INSN_SGE:   return "sge";
        case INSN_LOAD:  return "load";
        case INSN_STORE: return "store";
        case INSN_GEP:   return "gep";
        case INSN_FIELD: return "field";
        case INSN_LOAD_ATOMIC:  return "load atomic";
        case INSN_STORE_ATOMIC: return "store atomic";
        case INSN_ATOMICRMW: return "atomicrmw";
        case INSN_CMPXCHG:  return "cmpxchg";
        case INSN_JMP:   return "jmp";
        case INSN_BR:    return "br";
        case INSN_PHI:   return "phi";
        case INSN_RET:   return "ret";
        case INSN_CALL:  return "call";
        case INSN_ASM:   return "asm";
        case INSN_ZEXT:  return "zext";
        case INSN_SEXT:  return "sext";
        case INSN_TRUNC: return "trunc";
        case INSN_MULHI:      return "mulhi";
        case INSN_UDIV:       return "udiv";
        case INSN_UREM:       return "urem";
        case INSN_TENSOR:     return "tensor";
        default:         return "?";
    }
}

/* ==================================================================
 * AST deep-free
 * ================================================================== */

void operand_free(Operand *op) {
    if (op->kind == OPERAND_LOCAL)  free(op->as.local_name);
    if (op->kind == OPERAND_GLOBAL) free(op->as.global_name);
}

const char *tensor_op_name(int op) {
    switch (op) {
        case TENSOR_OP_ADD:    return "add";
        case TENSOR_OP_SUB:    return "sub";
        case TENSOR_OP_MUL:    return "mul";
        case TENSOR_OP_DIV:    return "div";
        case TENSOR_OP_MATMUL: return "matmul";
        default:               return "?";
    }
}

const char *source_loc_str(const SourceLoc *loc) {
    static char buf[32];
    if (loc && loc->line > 0)
        snprintf(buf, sizeof(buf), "%d:%d", loc->line, loc->column);
    else
        snprintf(buf, sizeof(buf), "?:?");
    return buf;
}

void insn_free(Insn *insn) {
    free(insn->result);
    node_free(insn->type);
    switch (insn->kind) {
        case INSN_LET: break;
        case INSN_ADD:
        case INSN_SUB:
        case INSN_MUL:
        case INSN_DIV:
        case INSN_REM:
        case INSN_AND:
        case INSN_OR:
        case INSN_XOR:
        case INSN_SHL:
        case INSN_LSHR:
        case INSN_ASHR:
        case INSN_EQ:
        case INSN_NE:
        case INSN_SLT:
        case INSN_SLE:
        case INSN_SGT:
        case INSN_SGE:
        case INSN_MULHI:
        case INSN_UDIV:
        case INSN_UREM:
            operand_free(&insn->data.binary.lhs);
            operand_free(&insn->data.binary.rhs);
            break;
        case INSN_LOAD:
            operand_free(&insn->data.load.ptr);
            break;
        case INSN_STORE:
            operand_free(&insn->data.store.value);
            operand_free(&insn->data.store.ptr);
            break;
        case INSN_GEP:
            operand_free(&insn->data.gep.base);
            operand_free(&insn->data.gep.index);
            break;
        case INSN_FIELD:
            node_free(insn->data.field.struct_type);
            operand_free(&insn->data.field.base);
            operand_free(&insn->data.field.field_index);
            break;
        case INSN_LOAD_ATOMIC:
            operand_free(&insn->data.load_atomic.ptr);
            break;
        case INSN_STORE_ATOMIC:
            operand_free(&insn->data.store_atomic.value);
            operand_free(&insn->data.store_atomic.ptr);
            break;
        case INSN_ATOMICRMW:
            operand_free(&insn->data.atomicrmw.ptr);
            operand_free(&insn->data.atomicrmw.value);
            break;
        case INSN_CMPXCHG:
            operand_free(&insn->data.cmpxchg.ptr);
            operand_free(&insn->data.cmpxchg.cmp);
            operand_free(&insn->data.cmpxchg.new_val);
            break;
        case INSN_JMP:
            free(insn->data.jmp.target);
            break;
        case INSN_BR:
            operand_free(&insn->data.br.cond);
            free(insn->data.br.true_target);
            free(insn->data.br.false_target);
            break;
        case INSN_PHI:
            for (int i = 0; i < insn->data.phi.count; i++) {
                operand_free(&insn->data.phi.cases[i].value);
                free(insn->data.phi.cases[i].label);
            }
            free(insn->data.phi.cases);
            break;
        case INSN_RET:
            if (!insn->data.ret.is_void)
                operand_free(&insn->data.ret.value);
            break;
        case INSN_CALL:
            free(insn->data.call.func);
            for (int i = 0; i < insn->data.call.argc; i++)
                operand_free(&insn->data.call.args[i]);
            free(insn->data.call.args);
            break;
        case INSN_ASM:
            free(insn->data.asm_insn.text);
            free(insn->data.asm_insn.constraints);
            for (int i = 0; i < insn->data.asm_insn.operand_count; i++) {
                operand_free(&insn->data.asm_insn.operands[i]);
                node_free(insn->data.asm_insn.operand_types[i]);
            }
            free(insn->data.asm_insn.operands);
            free(insn->data.asm_insn.operand_types);
            break;
        case INSN_ZEXT:
        case INSN_SEXT:
        case INSN_TRUNC:
            node_free(insn->data.convert.from_type);
            operand_free(&insn->data.convert.value);
            node_free(insn->data.convert.to_type);
            break;
        case INSN_TENSOR:
            node_free(insn->data.tensor.elem_type);
            for (int i = 0; i < insn->data.tensor.ndim; i++)
                operand_free(&insn->data.tensor.dims[i]);
            operand_free(&insn->data.tensor.dst);
            operand_free(&insn->data.tensor.lhs);
            operand_free(&insn->data.tensor.rhs);
            break;
    }
}

static void block_free(BasicBlock *block) {
    free(block->label);
    for (int i = 0; i < block->insn_count; i++)
        insn_free(&block->insns[i]);
    free(block->insns);
}

void program_free(Program *prog) {
    if (!prog) return;
    for (int i = 0; i < prog->struct_count; i++) {
        free(prog->structs[i].name);
        for (int j = 0; j < prog->structs[i].field_count; j++) {
            free(prog->structs[i].fields[j].name);
            node_free(prog->structs[i].fields[j].type);
        }
        free(prog->structs[i].fields);
    }
    free(prog->structs);
    for (int i = 0; i < prog->global_count; i++) {
        free(prog->globals[i].name);
        node_free(prog->globals[i].type);
        if (prog->globals[i].has_init)
            operand_free(&prog->globals[i].init_value);
        free(prog->globals[i].array_init);
    }
    free(prog->globals);
    for (int i = 0; i < prog->extern_count; i++) {
        free(prog->externs[i].name);
        for (int j = 0; j < prog->externs[i].param_count; j++) {
            free(prog->externs[i].params[j].name);
            node_free(prog->externs[i].params[j].type);
        }
        free(prog->externs[i].params);
        node_free(prog->externs[i].return_type);
    }
    free(prog->externs);
    for (int i = 0; i < prog->func_count; i++) {
        free(prog->functions[i].name);
        for (int j = 0; j < prog->functions[i].param_count; j++) {
            free(prog->functions[i].params[j].name);
            node_free(prog->functions[i].params[j].type);
        }
        free(prog->functions[i].params);
        node_free(prog->functions[i].return_type);
        for (int j = 0; j < prog->functions[i].block_count; j++)
            block_free(&prog->functions[i].blocks[j]);
        free(prog->functions[i].blocks);
    }
    free(prog->functions);
    free(prog);
}

/* ==================================================================
 * AST pretty-printer
 * ================================================================== */

static void print_type(const AstNode *type) {
    if (!type) { printf("?"); return; }
    switch (type->kind) {
        case AST_TYPE_PTR:
            printf("*");
            print_type(type->ptr_to);
            break;
        case AST_TYPE_ARRAY:
            printf("[");
            print_type(type->array.elem);
            printf(", %" PRId64 "]", type->array.size);
            break;
        case AST_TYPE_STRUCT:
            printf("@%s", type->struct_name);
            break;
        default:
            printf("%s", ast_node_kind_name(type->kind));
            break;
    }
}

static void print_operand(const Operand *op) {
    switch (op->kind) {
        case OPERAND_LOCAL:  printf("%%%s", op->as.local_name); break;
        case OPERAND_GLOBAL: printf("@%s", op->as.global_name); break;
        case OPERAND_INT:    printf("%" PRId64, op->as.int_val); break;
        case OPERAND_FLOAT:  printf("%g", op->as.float_val); break;
        case OPERAND_BOOL:   printf("%s", op->as.bool_val ? "true" : "false"); break;
        case OPERAND_NULL:   printf("null"); break;
    }
}

static void print_insn(const Insn *insn, int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
    printf("/* %s */ ", source_loc_str(&insn->loc));

    if (insn->result) {
        printf("%%%s = ", insn->result);
    }
    printf("%s ", insn_kind_name(insn->kind));

    switch (insn->kind) {
        case INSN_LET:
            print_type(insn->type);
            if (insn->data.let.align)
                printf(", %d", insn->data.let.align);
            break;
        case INSN_ADD: case INSN_SUB: case INSN_MUL: case INSN_DIV: case INSN_REM:
        case INSN_AND: case INSN_OR: case INSN_XOR: case INSN_SHL:
        case INSN_LSHR: case INSN_ASHR:
        case INSN_EQ: case INSN_NE: case INSN_SLT: case INSN_SLE: case INSN_SGT: case INSN_SGE:
        case INSN_MULHI: case INSN_UDIV: case INSN_UREM:
            print_type(insn->type);
            printf(" ");
            print_operand(&insn->data.binary.lhs);
            printf(", ");
            print_operand(&insn->data.binary.rhs);
            break;
        case INSN_LOAD:
            print_type(insn->type);
            printf(", *");
            print_operand(&insn->data.load.ptr);
            break;
        case INSN_STORE:
            print_type(insn->type);
            printf(" ");
            print_operand(&insn->data.store.value);
            printf(", *");
            print_operand(&insn->data.store.ptr);
            break;
        case INSN_GEP:
            print_type(insn->type);
            printf(", *");
            print_operand(&insn->data.gep.base);
            printf(", ");
            print_operand(&insn->data.gep.index);
            break;
        case INSN_FIELD:
            print_type(insn->data.field.struct_type);
            printf(", *");
            print_operand(&insn->data.field.base);
            printf(", ");
            print_operand(&insn->data.field.field_index);
            break;
        case INSN_TENSOR:
            printf("%s, ", tensor_op_name(insn->data.tensor.op));
            print_type(insn->data.tensor.elem_type);
            printf(", [");
            for (int i = 0; i < insn->data.tensor.ndim; i++) {
                if (i > 0) printf(", ");
                print_operand(&insn->data.tensor.dims[i]);
            }
            printf("], *");
            print_operand(&insn->data.tensor.dst);
            printf(", *");
            print_operand(&insn->data.tensor.lhs);
            printf(", *");
            print_operand(&insn->data.tensor.rhs);
            break;
        case INSN_LOAD_ATOMIC:
            print_type(insn->type);
            printf(", *");
            print_operand(&insn->data.load_atomic.ptr);
            printf(", %s", ordering_name(insn->data.load_atomic.ordering));
            break;
        case INSN_STORE_ATOMIC:
            print_type(insn->type);
            printf(" ");
            print_operand(&insn->data.store_atomic.value);
            printf(", *");
            print_operand(&insn->data.store_atomic.ptr);
            printf(", %s", ordering_name(insn->data.store_atomic.ordering));
            break;
        case INSN_ATOMICRMW: {
            static const char *op_names[] = {"xchg", "add", "sub", "and", "or", "xor"};
            int op = insn->data.atomicrmw.atomic_op;
            if (op >= 0 && op < 6)
                printf("%s ", op_names[op]);
            print_type(insn->type);
            printf(", *");
            print_operand(&insn->data.atomicrmw.ptr);
            printf(", ");
            print_operand(&insn->data.atomicrmw.value);
            printf(", %s", ordering_name(insn->data.atomicrmw.ordering));
            break;
        }
        case INSN_CMPXCHG:
            print_type(insn->type);
            printf(", *");
            print_operand(&insn->data.cmpxchg.ptr);
            printf(", ");
            print_operand(&insn->data.cmpxchg.cmp);
            printf(", ");
            print_operand(&insn->data.cmpxchg.new_val);
            printf(", %s", ordering_name(insn->data.cmpxchg.succ_ordering));
            printf(", %s", ordering_name(insn->data.cmpxchg.fail_ordering));
            break;
        case INSN_JMP:
            printf("@%s", insn->data.jmp.target);
            break;
        case INSN_BR:
            print_operand(&insn->data.br.cond);
            printf(", @%s, @%s", insn->data.br.true_target, insn->data.br.false_target);
            break;
        case INSN_PHI:
            for (int i = 0; i < insn->data.phi.count; i++) {
                if (i > 0) printf(", ");
                printf("[");
                print_operand(&insn->data.phi.cases[i].value);
                printf(", @%s]", insn->data.phi.cases[i].label);
            }
            break;
        case INSN_RET:
            if (insn->data.ret.is_void) {
                printf("void");
            } else {
                print_type(insn->type);
                printf(" ");
                print_operand(&insn->data.ret.value);
            }
            break;
        case INSN_CALL:
            printf("@%s(", insn->data.call.func);
            for (int i = 0; i < insn->data.call.argc; i++) {
                if (i > 0) printf(", ");
                print_operand(&insn->data.call.args[i]);
            }
            printf(")");
            break;
        case INSN_ASM:
            if (insn->data.asm_insn.align_stack) printf("alignstack ");
            print_type(insn->type);
            printf(" %s, \"%s\", \"%s\"",
                   insn->data.asm_insn.intel_dialect ? "intel" : "att",
                   insn->data.asm_insn.text,
                   insn->data.asm_insn.constraints);
            for (int i = 0; i < insn->data.asm_insn.operand_count; i++) {
                printf(", ");
                print_type(insn->data.asm_insn.operand_types[i]);
                printf(" ");
                print_operand(&insn->data.asm_insn.operands[i]);
            }
            break;
        case INSN_ZEXT: case INSN_SEXT: case INSN_TRUNC:
            print_type(insn->data.convert.from_type);
            printf(" ");
            print_operand(&insn->data.convert.value);
            printf(" to ");
            print_type(insn->data.convert.to_type);
            break;
    }
    printf("\n");
}

static void print_loc(const SourceLoc *loc) {
    if (loc && loc->line > 0)
        printf(" /* %d:%d */", loc->line, loc->column);
}

void ast_print(const Program *prog) {
    if (!prog) { printf("(null program)\n"); return; }
    printf("Program");
    if (prog->filename)
        printf(" (%s)", prog->filename);
    printf(" {\n");
    for (int i = 0; i < prog->struct_count; i++) {
        printf("  struct @%s { ", prog->structs[i].name);
        for (int j = 0; j < prog->structs[i].field_count; j++) {
            if (j > 0) printf(", ");
            printf("@%s: ", prog->structs[i].fields[j].name);
            print_type(prog->structs[i].fields[j].type);
        }
        printf(" }\n");
    }
    for (int i = 0; i < prog->global_count; i++) {
        printf("  global @%s: ", prog->globals[i].name);
        print_type(prog->globals[i].type);
        if (prog->globals[i].has_init) {
            printf(" = ");
            print_operand(&prog->globals[i].init_value);
        }
        print_loc(&prog->globals[i].loc);
        printf("\n");
    }
    for (int i = 0; i < prog->extern_count; i++) {
        ExternDecl *e = &prog->externs[i];
        printf("  extern @%s(", e->name);
        for (int j = 0; j < e->param_count; j++) {
            if (j > 0) printf(", ");
            printf("@%s: ", e->params[j].name);
            print_type(e->params[j].type);
        }
        printf(") -> ");
        print_type(e->return_type);
        printf(";\n");
    }
    for (int i = 0; i < prog->func_count; i++) {
        FuncDecl *f = &prog->functions[i];
        printf("  fun @%s(", f->name);
        for (int j = 0; j < f->param_count; j++) {
            if (j > 0) printf(", ");
            printf("@%s: ", f->params[j].name);
            print_type(f->params[j].type);
        }
        printf(") -> ");
        print_type(f->return_type);
        print_loc(&f->loc);
        printf(" {\n");
        for (int j = 0; j < f->block_count; j++) {
            printf("    @%s:", f->blocks[j].label);
            print_loc(&f->blocks[j].loc);
            printf("\n");
            for (int k = 0; k < f->blocks[j].insn_count; k++) {
                print_insn(&f->blocks[j].insns[k], 3);
            }
        }
        printf("  }\n");
    }
    printf("}\n");
}
