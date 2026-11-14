/* cmap — render SSA as pseudo-C for human reading.
 *
 * See cmap.md for the mapping rules this implements. Two layers:
 *   Core       — faithful 1:1 mapping, keeps basic blocks and goto.
 *                Handles any legal SSA.
 *   Structured — rebuilt on top of Core: recognises the block patterns
 *                from agents.md and emits while/if. Falls back to Core
 *                per-function when a pattern cannot be recognised.
 */

#include "cmap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ==================================================================
 * Growable string
 * ================================================================== */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Str;

static void str_init(Str *s) {
    s->cap = 64;
    s->data = malloc(s->cap);
    s->data[0] = '\0';
    s->len = 0;
}

static void str_free(Str *s) {
    free(s->data);
    s->data = NULL;
    s->len = s->cap = 0;
}

static void str_reserve(Str *s, size_t extra) {
    if (s->len + extra + 1 > s->cap) {
        while (s->len + extra + 1 > s->cap) s->cap *= 2;
        s->data = realloc(s->data, s->cap);
    }
}

static void str_puts(Str *s, const char *t) {
    size_t n = strlen(t);
    str_reserve(s, n);
    memcpy(s->data + s->len, t, n);
    s->len += n;
    s->data[s->len] = '\0';
}

static void str_printf(Str *s, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }
    str_reserve(s, (size_t)n);
    vsnprintf(s->data + s->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    s->len += (size_t)n;
}

static void str_clear(Str *s) {
    s->len = 0;
    if (s->data) s->data[0] = '\0';
}

/* ==================================================================
 * Binding table — SSA value name → cmap name
 *
 * Records the folding results from cmap.md §5.2. Only values that name
 * a storage location get an entry:
 *     %i = let i32              -> "i"
 *     %p = gep i32, *%arr, %j   -> "arr[j]"
 *     %f = field @S, *%pt, 0    -> "pt->x"
 * A value with no entry is not foldable and renders under its own name.
 * ================================================================== */

typedef struct {
    char *ssa;       /* SSA value name, without '%' */
    char *name;      /* cmap expression, e.g. "arr[j]" */
    bool  is_array;  /* array from 'let' — address-of omits the '&' */
} Binding;

typedef struct {
    Binding *items;
    int      count;
    int      cap;
} BindTable;

static void binds_init(BindTable *t) {
    t->cap = 16;
    t->count = 0;
    t->items = malloc((size_t)t->cap * sizeof(Binding));
}

static void binds_free(BindTable *t) {
    for (int i = 0; i < t->count; i++) {
        free(t->items[i].ssa);
        free(t->items[i].name);
    }
    free(t->items);
}

static void binds_set(BindTable *t, const char *ssa, const char *name, bool is_array) {
    for (int i = 0; i < t->count; i++) {
        if (strcmp(t->items[i].ssa, ssa) == 0) {
            free(t->items[i].name);
            t->items[i].name = strdup(name);
            t->items[i].is_array = is_array;
            return;
        }
    }
    if (t->count == t->cap) {
        t->cap *= 2;
        t->items = realloc(t->items, (size_t)t->cap * sizeof(Binding));
    }
    t->items[t->count].ssa = strdup(ssa);
    t->items[t->count].name = strdup(name);
    t->items[t->count].is_array = is_array;
    t->count++;
}

static const Binding *binds_get(const BindTable *t, const char *ssa) {
    for (int i = 0; i < t->count; i++)
        if (strcmp(t->items[i].ssa, ssa) == 0) return &t->items[i];
    return NULL;
}

/* ==================================================================
 * Name set — SSA values that are read as something being stored
 *
 * Folding a load into a value that later gets stored would turn a snapshot
 * into a re-read: `%vi = load ...; store %vi, ...` must keep `vi` distinct
 * from the storage it came from (cmap.md §5.5).
 * ================================================================== */

typedef struct {
    char **items;
    int    count;
    int    cap;
} NameSet;

static bool nameset_has(const NameSet *s, const char *n) {
    for (int i = 0; i < s->count; i++)
        if (strcmp(s->items[i], n) == 0) return true;
    return false;
}

static void nameset_init(NameSet *s) {
    s->cap = 16;
    s->count = 0;
    s->items = malloc((size_t)s->cap * sizeof(char *));
}

static void nameset_free(NameSet *s) {
    for (int i = 0; i < s->count; i++) free(s->items[i]);
    free(s->items);
    s->items = NULL;
    s->count = s->cap = 0;
}

static void nameset_clear(NameSet *s) {
    for (int i = 0; i < s->count; i++) free(s->items[i]);
    s->count = 0;
}

static void nameset_add(NameSet *s, const char *n) {
    if (nameset_has(s, n)) return;
    if (s->count == s->cap) {
        s->cap *= 2;
        s->items = realloc(s->items, (size_t)s->cap * sizeof(char *));
    }
    s->items[s->count++] = strdup(n);
}

/* ==================================================================
 * Render context
 * ================================================================== */

typedef struct {
    FILE          *out;
    const Program *prog;
    int            indent;
    BindTable      binds;
    NameSet        snapshots;   /* values stored elsewhere — never fold */
    Str            line;        /* current line being built */
} Ctx;

static void ctx_init(Ctx *ctx, const Program *prog, FILE *out) {
    ctx->out = out;
    ctx->prog = prog;
    ctx->indent = 0;
    binds_init(&ctx->binds);
    nameset_init(&ctx->snapshots);
    str_init(&ctx->line);
}

static void ctx_free(Ctx *ctx) {
    binds_free(&ctx->binds);
    nameset_free(&ctx->snapshots);
    str_free(&ctx->line);
}

/* Can the address this pointer holds alias an aggregate (an array element
   or a struct field)? Those are the places where folding a load into a
   snapshot would change meaning; a plain scalar variable cannot. */
static bool target_may_alias(const FuncDecl *fn, const Operand *p) {
    if (p->kind != OPERAND_LOCAL) return true;
    for (int i = 0; i < fn->block_count; i++) {
        const BasicBlock *b = &fn->blocks[i];
        for (int j = 0; j < b->insn_count; j++) {
            const Insn *in = &b->insns[j];
            if (!in->result || strcmp(in->result, p->as.local_name) != 0) continue;
            switch (in->kind) {
            case INSN_LET:   return false;   /* a plain scalar variable */
            case INSN_GEP:
            case INSN_FIELD: return true;    /* array element / struct field */
            default:         return true;    /* unknown — be conservative */
            }
        }
    }
    return true;                             /* a parameter, say */
}

/* Record SSA values that are read as the value being stored into a possibly
   aliasing location. Folding a load into one of those would replace a
   snapshot with a re-read (cmap.md §5.5). */
static void collect_snapshots(NameSet *s, const FuncDecl *fn) {
    nameset_clear(s);
    for (int i = 0; i < fn->block_count; i++) {
        const BasicBlock *b = &fn->blocks[i];
        for (int j = 0; j < b->insn_count; j++) {
            const Insn *in = &b->insns[j];
            const Operand *v = NULL, *p = NULL;
            switch (in->kind) {
            case INSN_STORE:
                v = &in->data.store.value;        p = &in->data.store.ptr;        break;
            case INSN_STORE_ATOMIC:
                v = &in->data.store_atomic.value; p = &in->data.store_atomic.ptr; break;
            case INSN_ATOMICRMW:
                v = &in->data.atomicrmw.value;    p = &in->data.atomicrmw.ptr;    break;
            case INSN_CMPXCHG:
                v = &in->data.cmpxchg.new_val;    p = &in->data.cmpxchg.ptr;      break;
            default: break;
            }
            if (v && p && v->kind == OPERAND_LOCAL && target_may_alias(fn, p))
                nameset_add(s, v->as.local_name);
        }
    }
}

/* Flush the current line buffer to the output, indented. */
static void ctx_emit(Ctx *ctx) {
    for (int i = 0; i < ctx->indent; i++) fputs("  ", ctx->out);
    fputs(ctx->line.data, ctx->out);
    fputc('\n', ctx->out);
    str_clear(&ctx->line);
}

static void line_puts(Ctx *ctx, const char *s) {
    str_puts(&ctx->line, s);
    ctx_emit(ctx);
}

static void line_printf(Ctx *ctx, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n >= 0) {
        str_reserve(&ctx->line, (size_t)n);
        vsnprintf(ctx->line.data + ctx->line.len, (size_t)n + 1, fmt, ap2);
        ctx->line.len += (size_t)n;
    }
    va_end(ap2);
    ctx_emit(ctx);
}

/* ==================================================================
 * Types (cmap.md §2)
 * ================================================================== */

static const char *basic_type_name(AstNodeKind k) {
    switch (k) {
    case AST_TYPE_I1:   return "bool";
    case AST_TYPE_I8:   return "char";
    case AST_TYPE_I16:  return "int16";
    case AST_TYPE_I32:  return "int32";
    case AST_TYPE_I64:  return "int64";
    case AST_TYPE_F32:  return "float";
    case AST_TYPE_F64:  return "double";
    case AST_TYPE_VOID: return "void";
    default:            return "?";
    }
}

/* Type expression: i32 -> "int32", *i32 -> "int32*", @S -> "struct S" */
static void type_expr(const AstNode *t, Str *s) {
    if (!t) { str_puts(s, "?"); return; }
    switch (t->kind) {
    case AST_TYPE_PTR:
        type_expr(t->ptr_to, s);
        str_puts(s, "*");
        break;
    case AST_TYPE_ARRAY:
        /* A bare array type (e.g. a parameter) decays to an element pointer. */
        type_expr(t->array.elem, s);
        str_puts(s, "*");
        break;
    case AST_TYPE_STRUCT:
        str_printf(s, "struct %s", t->struct_name);
        break;
    default:
        str_puts(s, basic_type_name(t->kind));
        break;
    }
}

/* SSA-side spelling of a type, for provenance comments: "[i32, 6]". */
static void ssa_type_str(const AstNode *t, Str *s) {
    if (!t) { str_puts(s, "?"); return; }
    switch (t->kind) {
    case AST_TYPE_PTR:
        str_puts(s, "*");
        ssa_type_str(t->ptr_to, s);
        break;
    case AST_TYPE_ARRAY:
        str_puts(s, "[");
        ssa_type_str(t->array.elem, s);
        str_printf(s, ", %lld]", (long long)t->array.size);
        break;
    case AST_TYPE_STRUCT:
        str_printf(s, "@%s", t->struct_name);
        break;
    default:
        switch (t->kind) {
        case AST_TYPE_I1:   str_puts(s, "i1");   break;
        case AST_TYPE_I8:   str_puts(s, "i8");   break;
        case AST_TYPE_I16:  str_puts(s, "i16");  break;
        case AST_TYPE_I32:  str_puts(s, "i32");  break;
        case AST_TYPE_I64:  str_puts(s, "i64");  break;
        case AST_TYPE_F32:  str_puts(s, "f32");  break;
        case AST_TYPE_F64:  str_puts(s, "f64");  break;
        case AST_TYPE_VOID: str_puts(s, "void"); break;
        default:            str_puts(s, "?");    break;
        }
        break;
    }
}

/* Declaration: split the type tree into a prefix and an array suffix.
 *     [i32, 6]     -> "int32 x[6]"
 *     *i32         -> "int32* p"
 *     [[i32,4],3]  -> "int32 m[3][4]"
 */
static void decl(const AstNode *t, const char *name, Str *s) {
    Str suffix, prefix;
    str_init(&suffix);
    str_init(&prefix);

    const AstNode *cur = t;
    while (cur && cur->kind == AST_TYPE_ARRAY) {
        str_printf(&suffix, "[%lld]", (long long)cur->array.size);
        cur = cur->array.elem;
    }
    type_expr(cur, &prefix);

    str_printf(s, "%s %s%s", prefix.data, name, suffix.data);

    str_free(&suffix);
    str_free(&prefix);
}

/* ==================================================================
 * Operand rendering (cmap.md §2.2)
 * ================================================================== */

/* Global names arrive in two shapes: the lexer strips '@', but the parser
   builds anonymous string constants with it still attached ("@_s%d_%d" in
   ast.c). cmap never writes '@', so normalise on the way out. */
static const char *global_display_name(const char *name) {
    return name[0] == '@' ? name + 1 : name;
}

static void operand(Ctx *ctx, const Operand *op, Str *s) {
    switch (op->kind) {
    case OPERAND_LOCAL: {
        const Binding *b = binds_get(&ctx->binds, op->as.local_name);
        str_puts(s, b ? b->name : op->as.local_name);
        break;
    }
    case OPERAND_GLOBAL:
        str_puts(s, global_display_name(op->as.global_name));
        break;
    case OPERAND_INT:
        str_printf(s, "%lld", (long long)op->as.int_val);
        break;
    case OPERAND_FLOAT:
        str_printf(s, "%g", op->as.float_val);
        break;
    case OPERAND_BOOL:
        str_puts(s, op->as.bool_val ? "true" : "false");
        break;
    case OPERAND_NULL:
        str_puts(s, "null");
        break;
    }
}

/* Address-of form for call arguments: arrays pass their name directly. */
static void operand_addr(Ctx *ctx, const Operand *op, Str *s) {
    if (op->kind == OPERAND_LOCAL) {
        const Binding *b = binds_get(&ctx->binds, op->as.local_name);
        if (b) {
            if (!b->is_array) str_puts(s, "&");
            str_puts(s, b->name);
            return;
        }
    }
    operand(ctx, op, s);
}

/* Look up a struct field's real name; NULL when unknown. */
static const char *field_name(const Program *prog, const char *struct_name, int idx) {
    if (!struct_name) return NULL;
    for (int i = 0; i < prog->struct_count; i++) {
        if (strcmp(prog->structs[i].name, struct_name) == 0) {
            if (idx >= 0 && idx < prog->structs[i].field_count)
                return prog->structs[i].fields[idx].name;
            return NULL;
        }
    }
    return NULL;
}

/* Look up a struct field's type; NULL when unknown. */
static const AstNode *field_type_of(const Program *prog, const char *struct_name, int idx) {
    if (!struct_name) return NULL;
    for (int i = 0; i < prog->struct_count; i++) {
        if (strcmp(prog->structs[i].name, struct_name) == 0) {
            if (idx >= 0 && idx < prog->structs[i].field_count)
                return prog->structs[i].fields[idx].type;
            return NULL;
        }
    }
    return NULL;
}

/* ==================================================================
 * Instruction helpers
 * ================================================================== */

static const char *binary_op_str(InsnKind k) {
    switch (k) {
    case INSN_ADD:  return "+";
    case INSN_SUB:  return "-";
    case INSN_MUL:  return "*";
    case INSN_DIV:  return "/";
    case INSN_REM:  return "%";
    case INSN_UDIV: return "/";
    case INSN_UREM: return "%";
    case INSN_AND:  return "&";
    case INSN_OR:   return "|";
    case INSN_XOR:  return "^";
    case INSN_SHL:  return "<<";
    case INSN_ASHR: return ">>";
    case INSN_LSHR: return ">>>";   /* cmap marker — C has no logical shift */
    case INSN_EQ:   return "==";
    case INSN_NE:   return "!=";
    case INSN_SLT:  return "<";
    case INSN_SLE:  return "<=";
    case INSN_SGT:  return ">";
    case INSN_SGE:  return ">=";
    default:        return "?";
    }
}

/* Comparisons yield i1 even though their `type` field holds the operand type. */
static bool is_comparison(InsnKind k) {
    return k == INSN_EQ || k == INSN_NE || k == INSN_SLT ||
           k == INSN_SLE || k == INSN_SGT || k == INSN_SGE;
}

static const char *ordering_str(MemoryOrdering o) {
    switch (o) {
    case ORDERING_SEQ_CST: return "seq_cst";
    case ORDERING_ACQUIRE: return "acquire";
    case ORDERING_RELEASE: return "release";
    case ORDERING_ACQ_REL: return "acq_rel";
    case ORDERING_RELAXED: return "relaxed";
    }
    return "?";
}

static const char *atomic_op_str(int op) {
    switch (op) {
    case ATOMICOP_XCHG: return "xchg";
    case ATOMICOP_ADD:  return "add";
    case ATOMICOP_SUB:  return "sub";
    case ATOMICOP_AND:  return "and";
    case ATOMICOP_OR:   return "or";
    case ATOMICOP_XOR:  return "xor";
    }
    return "?";
}

/* Constant i1 node, for comparison and cmpxchg results. */
static AstNode g_bool_type = { .kind = AST_TYPE_I1 };

/* The instruction's result type — NULL when it needs function context. */
static const AstNode *insn_result_type(const Insn *in) {
    switch (in->kind) {
    case INSN_LET:
    case INSN_ADD: case INSN_SUB: case INSN_MUL: case INSN_DIV: case INSN_REM:
    case INSN_UDIV: case INSN_UREM: case INSN_MULHI:
    case INSN_AND: case INSN_OR:  case INSN_XOR:
    case INSN_SHL: case INSN_LSHR: case INSN_ASHR:
    case INSN_LOAD: case INSN_LOAD_ATOMIC: case INSN_ATOMICRMW:
        return in->type;
    case INSN_EQ: case INSN_NE: case INSN_SLT: case INSN_SLE:
    case INSN_SGT: case INSN_SGE: case INSN_CMPXCHG:
        return &g_bool_type;
    case INSN_ZEXT: case INSN_SEXT: case INSN_TRUNC:
        return in->data.convert.to_type;
    default:
        return NULL;   /* gep / field / phi / call need more context */
    }
}

static const AstNode *param_type(const FuncDecl *fn, const char *name) {
    if (!fn) return NULL;
    for (int i = 0; i < fn->param_count; i++)
        if (strcmp(fn->params[i].name, name) == 0)
            return fn->params[i].type;
    return NULL;
}

static const AstNode *value_type(const FuncDecl *fn, const char *name) {
    if (!fn) return NULL;
    for (int i = 0; i < fn->block_count; i++) {
        const BasicBlock *b = &fn->blocks[i];
        for (int j = 0; j < b->insn_count; j++) {
            const Insn *in = &b->insns[j];
            if (in->result && strcmp(in->result, name) == 0)
                return insn_result_type(in);
        }
    }
    return NULL;
}

/* Type of a local SSA value: a parameter or an instruction result. */
static const AstNode *local_type(const FuncDecl *fn, const Operand *op) {
    if (op->kind != OPERAND_LOCAL) return NULL;
    const AstNode *t = param_type(fn, op->as.local_name);
    return t ? t : value_type(fn, op->as.local_name);
}

static const AstNode *func_ret_type(const Program *prog, const char *fname) {
    for (int i = 0; i < prog->func_count; i++)
        if (strcmp(prog->functions[i].name, fname) == 0)
            return prog->functions[i].return_type;
    for (int i = 0; i < prog->extern_count; i++)
        if (strcmp(prog->externs[i].name, fname) == 0)
            return prog->externs[i].return_type;
    return NULL;
}

static const GlobalDecl *find_global(const Program *prog, const char *name) {
    for (int i = 0; i < prog->global_count; i++)
        if (strcmp(prog->globals[i].name, name) == 0)
            return &prog->globals[i];
    return NULL;
}

/* Parser-generated anonymous string constants are named "_s<id>_<n>" —
   ast.c builds them with "@_s%d_%d" when a string literal shows up in a
   call argument. */
static bool is_anon_string_name(const char *name) {
    const char *p = name;
    if (*p++ != '_' || *p++ != 's') return false;
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') p++;
    if (*p++ != '_') return false;
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') p++;
    return *p == '\0';
}

/* Render a char-array constant back into a string literal. */
static void render_string_literal(const GlobalDecl *g, Str *s) {
    int n = g->array_count;
    while (n > 0 && g->array_init[n - 1] == 0) n--;   /* drop trailing NULs */
    str_puts(s, "\"");
    for (int i = 0; i < n; i++) {
        long long c = g->array_init[i];
        switch (c) {
        case '\n': str_puts(s, "\\n");  break;
        case '\t': str_puts(s, "\\t");  break;
        case '\r': str_puts(s, "\\r");  break;
        case '\\': str_puts(s, "\\\\"); break;
        case '"':  str_puts(s, "\\\""); break;
        case 0:    str_puts(s, "\\0");  break;
        default:
            if (c >= 32 && c < 127) {
                char ch[2] = { (char)c, '\0' };
                str_puts(s, ch);
            } else {
                str_printf(s, "\\x%02llx", c);
            }
            break;
        }
    }
    str_puts(s, "\"");
}

/* Is this operand an anonymous string constant passed to a call? */
static const GlobalDecl *anon_string_of(const Program *prog, const Operand *op) {
    if (op->kind != OPERAND_GLOBAL) return NULL;
    const char *raw = op->as.global_name;
    if (!is_anon_string_name(global_display_name(raw))) return NULL;

    /* The declaration carries the same spelling as the operand, but be
       lenient about the '@' in case the two ever diverge. */
    const GlobalDecl *g = find_global(prog, raw);
    if (!g) g = find_global(prog, global_display_name(raw));
    if (g && g->array_count > 0 && g->array_init) return g;
    return NULL;
}

/* ==================================================================
 * Memory access helpers — folding rules from cmap.md §5.3
 * ================================================================== */

/* Read through a pointer: foldable pointers read as their storage name. */
static void mem_read(Ctx *ctx, const Operand *ptr, Str *s) {
    if (ptr->kind == OPERAND_LOCAL) {
        const Binding *b = binds_get(&ctx->binds, ptr->as.local_name);
        if (b) { str_puts(s, b->name); return; }
    }
    str_puts(s, "*");
    operand(ctx, ptr, s);
}

/* Write target of a store: foldable pointers name the storage directly. */
static void mem_write(Ctx *ctx, const Operand *ptr, Str *s) {
    if (ptr->kind == OPERAND_LOCAL) {
        const Binding *b = binds_get(&ctx->binds, ptr->as.local_name);
        if (b) { str_puts(s, b->name); return; }
    }
    str_puts(s, "*");
    operand(ctx, ptr, s);
}

/* Base of a field access plus the member operator to use.
 * Folded bases name a struct value (`.`); a bare pointer variable uses `->`. */
static void field_base(Ctx *ctx, const Operand *base, const char **sep, Str *s) {
    if (base->kind == OPERAND_LOCAL) {
        const Binding *b = binds_get(&ctx->binds, base->as.local_name);
        if (b) {
            str_puts(s, b->name);
            *sep = b->is_array ? "->" : ".";
            return;
        }
    }
    operand(ctx, base, s);
    *sep = "->";
}

/* %q = gep T, *%base, %i  ->  "base[i]".  False when base is not foldable. */
static bool gep_fold(Ctx *ctx, const Insn *in, Str *s) {
    const Operand *base = &in->data.gep.base;
    if (base->kind == OPERAND_LOCAL) {
        /* A folded base names storage; an unfolded one is a pointer value
           (a parameter, say). Either can be indexed. */
        const Binding *b = binds_get(&ctx->binds, base->as.local_name);
        str_puts(s, b ? b->name : base->as.local_name);
    } else if (base->kind == OPERAND_GLOBAL) {
        str_puts(s, global_display_name(base->as.global_name));
    } else {
        return false;
    }
    str_puts(s, "[");
    operand(ctx, &in->data.gep.index, s);
    str_puts(s, "]");
    return true;
}

/* %q = field @S, *%base, N  ->  "base->field" (or "base.field"). */
static bool field_fold(Ctx *ctx, const Insn *in, Str *s, const AstNode **out_field_type) {
    const AstNode *st = in->data.field.struct_type;
    if (!st || st->kind != AST_TYPE_STRUCT) return false;

    int idx = (int)in->data.field.field_index.as.int_val;
    const char *fname = field_name(ctx->prog, st->struct_name, idx);
    if (!fname) return false;

    const char *sep = NULL;
    field_base(ctx, &in->data.field.base, &sep, s);
    str_puts(s, sep);
    str_puts(s, fname);

    if (out_field_type) *out_field_type = field_type_of(ctx->prog, st->struct_name, idx);
    return true;
}

/* Wrap a type in one level of pointer, for gep/field result declarations. */
static AstNode ptr_to_node(const AstNode *elem) {
    AstNode n;
    memset(&n, 0, sizeof(n));
    n.kind = AST_TYPE_PTR;
    n.ptr_to = (AstNode *)elem;
    return n;
}

/* Emit `code` with an aligned trailing comment (cmap.md §5.6). */
static void emit_commented(Ctx *ctx, const char *code, const char *comment) {
    if (!comment || !comment[0]) { line_puts(ctx, code); return; }
    int pad = 52 - (int)strlen(code);
    if (pad < 2) pad = 2;
    line_printf(ctx, "%s%*s%s", code, pad, "", comment);
}

/* ==================================================================
 * Core layer — instruction rendering (cmap.md §4)
 * ================================================================== */

static void render_insn_core(Ctx *ctx, const FuncDecl *fn, const Insn *in) {
    Str code;
    str_init(&code);

    switch (in->kind) {
    /* ---- let ---- */
    case INSN_LET: {
        decl(in->type, in->result, &code);
        str_puts(&code, ";");
        binds_set(&ctx->binds, in->result, in->result,
                  in->type && in->type->kind == AST_TYPE_ARRAY);

        Str ty, cmt;
        str_init(&ty);
        str_init(&cmt);
        ssa_type_str(in->type, &ty);
        str_printf(&cmt, "// %%%s = let %s", in->result, ty.data);
        emit_commented(ctx, code.data, cmt.data);
        str_free(&ty);
        str_free(&cmt);
        break;
    }

    /* ---- arithmetic / bitwise / comparison (same layout) ---- */
    case INSN_ADD: case INSN_SUB: case INSN_MUL: case INSN_DIV: case INSN_REM:
    case INSN_UDIV: case INSN_UREM:
    case INSN_AND: case INSN_OR:  case INSN_XOR:
    case INSN_SHL: case INSN_LSHR: case INSN_ASHR:
    case INSN_EQ:  case INSN_NE:  case INSN_SLT: case INSN_SLE:
    case INSN_SGT: case INSN_SGE: {
        Str rhs;
        str_init(&rhs);
        operand(ctx, &in->data.binary.lhs, &rhs);
        str_printf(&rhs, " %s ", binary_op_str(in->kind));
        operand(ctx, &in->data.binary.rhs, &rhs);

        if (is_comparison(in->kind)) {
            decl(&g_bool_type, in->result, &code);
            str_printf(&code, " = (%s);", rhs.data);
        } else {
            decl(in->type, in->result, &code);
            str_printf(&code, " = %s;", rhs.data);
        }
        line_puts(ctx, code.data);
        str_free(&rhs);
        break;
    }

    case INSN_MULHI: {
        Str rhs;
        str_init(&rhs);
        operand(ctx, &in->data.binary.lhs, &rhs);
        str_puts(&rhs, ", ");
        operand(ctx, &in->data.binary.rhs, &rhs);
        decl(in->type, in->result, &code);
        str_printf(&code, " = mulhi(%s);", rhs.data);
        line_puts(ctx, code.data);
        str_free(&rhs);
        break;
    }

    /* ---- memory ---- */
    case INSN_LOAD: {
        /* A load through a foldable pointer reads as the named storage
           itself (cmap.md §5.3 rule A) — no statement, only a binding. */
        const Operand *ptr = &in->data.load.ptr;
        const Binding *b = (ptr->kind == OPERAND_LOCAL)
                         ? binds_get(&ctx->binds, ptr->as.local_name) : NULL;
        if (b && !nameset_has(&ctx->snapshots, in->result)) {
            binds_set(&ctx->binds, in->result, b->name, b->is_array);
            break;
        }
        decl(in->type, in->result, &code);
        str_puts(&code, " = ");
        mem_read(ctx, ptr, &code);
        str_puts(&code, ";");
        line_puts(ctx, code.data);
        break;
    }

    case INSN_STORE: {
        mem_write(ctx, &in->data.store.ptr, &code);
        str_puts(&code, " = ");
        operand(ctx, &in->data.store.value, &code);
        str_puts(&code, ";");
        line_puts(ctx, code.data);
        break;
    }

    case INSN_GEP: {
        Str name;
        str_init(&name);
        if (gep_fold(ctx, in, &name)) {
            binds_set(&ctx->binds, in->result, name.data, false);
        } else {
            /* Not foldable — keep an explicit pointer (cmap.md §4.6). */
            AstNode pn = ptr_to_node(in->type);
            Str d, base;
            str_init(&d);
            str_init(&base);
            decl(&pn, in->result, &d);
            operand(ctx, &in->data.gep.base, &base);
            str_printf(&code, "%s = &%s[", d.data, base.data);
            operand(ctx, &in->data.gep.index, &code);
            str_puts(&code, "];");
            line_puts(ctx, code.data);
            str_free(&d);
            str_free(&base);
        }
        str_free(&name);
        break;
    }

    case INSN_FIELD: {
        Str name;
        str_init(&name);
        const AstNode *ftype = NULL;
        if (field_fold(ctx, in, &name, &ftype)) {
            binds_set(&ctx->binds, in->result, name.data, false);
        } else {
            AstNode pn = ptr_to_node(ftype);
            Str d, base;
            str_init(&d);
            str_init(&base);
            decl(&pn, in->result, &d);
            operand(ctx, &in->data.field.base, &base);
            str_printf(&code, "%s = &%s->field%d;", d.data, base.data,
                       (int)in->data.field.field_index.as.int_val);
            line_puts(ctx, code.data);
            str_free(&d);
            str_free(&base);
        }
        str_free(&name);
        break;
    }

    /* ---- atomics ---- */
    case INSN_LOAD_ATOMIC: {
        decl(in->type, in->result, &code);
        str_puts(&code, " = atomic_load(");
        operand_addr(ctx, &in->data.load_atomic.ptr, &code);
        str_printf(&code, ", %s);", ordering_str(in->data.load_atomic.ordering));
        line_puts(ctx, code.data);
        break;
    }

    case INSN_STORE_ATOMIC: {
        str_puts(&code, "atomic_store(");
        operand_addr(ctx, &in->data.store_atomic.ptr, &code);
        str_puts(&code, ", ");
        operand(ctx, &in->data.store_atomic.value, &code);
        str_printf(&code, ", %s);", ordering_str(in->data.store_atomic.ordering));
        line_puts(ctx, code.data);
        break;
    }

    case INSN_ATOMICRMW: {
        decl(in->type, in->result, &code);
        str_printf(&code, " = atomic_%s(", atomic_op_str(in->data.atomicrmw.atomic_op));
        operand_addr(ctx, &in->data.atomicrmw.ptr, &code);
        str_puts(&code, ", ");
        operand(ctx, &in->data.atomicrmw.value, &code);
        str_printf(&code, ", %s);", ordering_str(in->data.atomicrmw.ordering));
        line_puts(ctx, code.data);
        break;
    }

    case INSN_CMPXCHG: {
        decl(&g_bool_type, in->result, &code);
        str_puts(&code, " = atomic_cas(");
        operand_addr(ctx, &in->data.cmpxchg.ptr, &code);
        str_puts(&code, ", ");
        operand(ctx, &in->data.cmpxchg.cmp, &code);
        str_puts(&code, ", ");
        operand(ctx, &in->data.cmpxchg.new_val, &code);
        str_printf(&code, ", %s, %s);",
                   ordering_str(in->data.cmpxchg.succ_ordering),
                   ordering_str(in->data.cmpxchg.fail_ordering));
        line_puts(ctx, code.data);
        break;
    }

    /* ---- control flow ---- */
    case INSN_JMP:
        line_printf(ctx, "goto %s;", in->data.jmp.target);
        break;

    case INSN_BR: {
        Str c;
        str_init(&c);
        operand(ctx, &in->data.br.cond, &c);
        line_printf(ctx, "if (%s) goto %s; else goto %s;",
                    c.data, in->data.br.true_target, in->data.br.false_target);
        str_free(&c);
        break;
    }

    case INSN_PHI: {
        const AstNode *t = in->type;
        if (!t && in->data.phi.count > 0)
            t = local_type(fn, &in->data.phi.cases[0].value);
        decl(t, in->result, &code);
        str_puts(&code, " = φ(");
        for (int i = 0; i < in->data.phi.count; i++) {
            if (i) str_puts(&code, ", ");
            str_printf(&code, "%s → ", in->data.phi.cases[i].label);
            operand(ctx, &in->data.phi.cases[i].value, &code);
        }
        str_puts(&code, ");");
        line_puts(ctx, code.data);
        break;
    }

    case INSN_RET: {
        if (in->data.ret.is_void) {
            line_puts(ctx, "return;");
        } else {
            Str v;
            str_init(&v);
            operand(ctx, &in->data.ret.value, &v);
            line_printf(ctx, "return %s;", v.data);
            str_free(&v);
        }
        break;
    }

    /* ---- call ---- */
    case INSN_CALL: {
        Str args;
        str_init(&args);
        for (int i = 0; i < in->data.call.argc; i++) {
            if (i) str_puts(&args, ", ");
            const GlobalDecl *sg = anon_string_of(ctx->prog, &in->data.call.args[i]);
            if (sg) render_string_literal(sg, &args);
            else operand_addr(ctx, &in->data.call.args[i], &args);
        }
        if (in->result) {
            const AstNode *rt = func_ret_type(ctx->prog, in->data.call.func);
            decl(rt, in->result, &code);
            str_printf(&code, " = %s(%s);", in->data.call.func, args.data);
        } else {
            str_printf(&code, "%s(%s);", in->data.call.func, args.data);
        }
        line_puts(ctx, code.data);
        str_free(&args);
        break;
    }

    /* ---- inline assembly ---- */
    case INSN_ASM: {
        Str operands;
        str_init(&operands);
        for (int i = 0; i < in->data.asm_insn.operand_count; i++) {
            if (i) str_puts(&operands, ", ");
            operand_addr(ctx, &in->data.asm_insn.operands[i], &operands);
        }
        if (in->result) {
            decl(in->type, in->result, &code);
            str_printf(&code, " = __asm(\"%s\")", in->data.asm_insn.text);
        } else {
            str_printf(&code, "__asm(\"%s\")", in->data.asm_insn.text);
        }
        if (in->data.asm_insn.operand_count > 0)
            str_printf(&code, ";  /* operands: %s */", operands.data);
        else
            str_puts(&code, ";");
        str_printf(&code, "  /* %s%s, constraints \"%s\" */",
                   in->data.asm_insn.align_stack ? "alignstack " : "",
                   in->data.asm_insn.intel_dialect ? "intel" : "att",
                   in->data.asm_insn.constraints);
        line_puts(ctx, code.data);
        str_free(&operands);
        break;
    }

    /* ---- tensor ---- */
    case INSN_TENSOR: {
        Str et;
        str_init(&et);
        type_expr(in->data.tensor.elem_type, &et);
        str_printf(&code, "tensor_%s<%s, ", tensor_op_name(in->data.tensor.op), et.data);
        for (int i = 0; i < in->data.tensor.ndim; i++) {
            if (i) str_puts(&code, ", ");
            operand(ctx, &in->data.tensor.dims[i], &code);
        }
        str_puts(&code, ">(");
        operand(ctx, &in->data.tensor.dst, &code);
        str_puts(&code, ", ");
        operand(ctx, &in->data.tensor.lhs, &code);
        str_puts(&code, ", ");
        operand(ctx, &in->data.tensor.rhs, &code);
        str_puts(&code, ");");
        line_puts(ctx, code.data);
        str_free(&et);
        break;
    }

    /* ---- conversions ---- */
    case INSN_ZEXT:
    case INSN_SEXT:
    case INSN_TRUNC: {
        const char *op = in->kind == INSN_ZEXT ? "zext"
                       : in->kind == INSN_SEXT ? "sext" : "trunc";
        Str to;
        str_init(&to);
        type_expr(in->data.convert.to_type, &to);
        decl(in->data.convert.to_type, in->result, &code);
        str_printf(&code, " = %s<%s>(", op, to.data);
        operand(ctx, &in->data.convert.value, &code);
        str_puts(&code, ");");
        line_puts(ctx, code.data);
        str_free(&to);
        break;
    }
    }

    str_free(&code);
}

/* ==================================================================
 * Core layer — blocks and functions
 * ================================================================== */

/* Function signature, shared by the Core and Structured renderers. */
static void render_signature(const FuncDecl *fn, Str *s) {
    Str rt;
    str_init(&rt);
    type_expr(fn->return_type, &rt);
    str_printf(s, "%s %s(", rt.data, fn->name);
    if (fn->param_count == 0) {
        str_puts(s, "void");
    } else {
        for (int i = 0; i < fn->param_count; i++) {
            if (i) str_puts(s, ", ");
            decl(fn->params[i].type, fn->params[i].name, s);
        }
    }
    str_puts(s, ")");
    str_free(&rt);
}

static void render_func_core(Ctx *ctx, const FuncDecl *fn) {
    Str sig;
    str_init(&sig);
    render_signature(fn, &sig);
    str_puts(&sig, " {");
    line_puts(ctx, sig.data);
    str_free(&sig);

    ctx->indent++;

    for (int i = 0; i < fn->block_count; i++) {
        const BasicBlock *blk = &fn->blocks[i];
        if (i > 0) line_puts(ctx, "");

        /* Block labels are flush left (cmap.md §4.1). */
        int saved = ctx->indent;
        ctx->indent = 0;
        line_printf(ctx, "%s:", blk->label);
        ctx->indent = saved;

        for (int j = 0; j < blk->insn_count; j++)
            render_insn_core(ctx, fn, &blk->insns[j]);
    }

    ctx->indent--;
    line_puts(ctx, "}");
}

/* ==================================================================
 * Top-level declarations (cmap.md §3)
 * ================================================================== */

static void render_struct(Ctx *ctx, const StructDecl *sd) {
    line_printf(ctx, "struct %s {", sd->name);
    ctx->indent++;
    for (int i = 0; i < sd->field_count; i++) {
        Str s;
        str_init(&s);
        decl(sd->fields[i].type, sd->fields[i].name, &s);
        str_puts(&s, ";");
        line_puts(ctx, s.data);
        str_free(&s);
    }
    ctx->indent--;
    line_puts(ctx, "};");
}

static bool is_char_array_type(const AstNode *t) {
    return t && t->kind == AST_TYPE_ARRAY &&
           t->array.elem && t->array.elem->kind == AST_TYPE_I8;
}

/* A char array is a string only when it ends in NUL and holds none inside.
   A lookup table (an AES S-box, say) is byte data, not text. */
static bool looks_like_string(const GlobalDecl *g) {
    int n = g->array_count;
    if (n == 0 || g->array_init[n - 1] != 0) return false;
    for (int i = 0; i < n - 1; i++)
        if (g->array_init[i] == 0) return false;
    return true;
}

/* Array initialiser, wrapped at 16 values per line when it grows. */
static void render_array_init(Ctx *ctx, const GlobalDecl *g) {
    int n = g->array_count;

    if (n <= 8) {
        Str s;
        str_init(&s);
        str_puts(&s, "{ ");
        for (int i = 0; i < n; i++) {
            if (i) str_puts(&s, ", ");
            str_printf(&s, "%lld", (long long)g->array_init[i]);
        }
        str_puts(&s, " };");
        line_puts(ctx, s.data);
        str_free(&s);
        return;
    }

    for (int i = 0; i < n; i += 16) {
        Str row;
        str_init(&row);
        str_puts(&row, i == 0 ? "{ " : "  ");
        for (int j = i; j < i + 16 && j < n; j++) {
            if (j > i) str_puts(&row, ", ");
            str_printf(&row, "%lld", (long long)g->array_init[j]);
        }
        str_puts(&row, (i + 16 >= n) ? " };" : ",");
        line_puts(ctx, row.data);
        str_free(&row);
    }
}

static void render_global(Ctx *ctx, const GlobalDecl *g) {
    Str s;
    str_init(&s);
    if (g->is_internal) str_puts(&s, "static ");
    if (g->is_const)    str_puts(&s, "const ");

    decl(g->type, global_display_name(g->name), &s);

    bool is_array_init = g->has_init && g->array_init && g->array_count > 0;

    if (is_array_init && is_char_array_type(g->type) && looks_like_string(g)) {
        str_puts(&s, " = ");
        render_string_literal(g, &s);
        str_puts(&s, ";");
        line_puts(ctx, s.data);
        str_free(&s);
        return;
    }

    if (is_array_init) {
        str_puts(&s, " =");
        line_puts(ctx, s.data);
        str_free(&s);
        ctx->indent++;
        render_array_init(ctx, g);
        ctx->indent--;
        return;
    }

    if (g->has_init) {
        str_puts(&s, " = ");
        operand(ctx, &g->init_value, &s);
    }
    str_puts(&s, ";");
    line_puts(ctx, s.data);
    str_free(&s);
}

/* ==================================================================
 * Structured layer — CFG (cmap.md §6)
 * ================================================================== */

typedef struct {
    const FuncDecl *fn;
    int    n;
    int   *succ1;     /* first successor / br true target; -1 = none */
    int   *succ2;     /* br false target; -1 = none */
    bool  *pred;      /* pred[i*n + j]: j is a predecessor of i */
    bool  *dom;       /* dom[i*n + j]:  j dominates i */
    bool  *loop_hdr;  /* block has a back edge pointing at it */
} Cfg;

static int block_index(const FuncDecl *fn, const char *label) {
    for (int i = 0; i < fn->block_count; i++)
        if (strcmp(fn->blocks[i].label, label) == 0) return i;
    return -1;
}

static const Insn *block_terminator(const BasicBlock *b) {
    if (b->insn_count == 0) return NULL;
    const Insn *in = &b->insns[b->insn_count - 1];
    switch (in->kind) {
    case INSN_JMP: case INSN_BR: case INSN_RET: return in;
    default: return NULL;
    }
}

static void cfg_free(Cfg *c) {
    free(c->succ1);
    free(c->succ2);
    free(c->pred);
    free(c->dom);
    free(c->loop_hdr);
    memset(c, 0, sizeof(*c));
}

/* Build successors, predecessors and the dominator sets. */
static bool cfg_build(Cfg *c, const FuncDecl *fn) {
    memset(c, 0, sizeof(*c));
    c->fn = fn;
    c->n = fn->block_count;
    if (c->n == 0) return true;

    int n = c->n;
    c->succ1    = malloc((size_t)n * sizeof(int));
    c->succ2    = malloc((size_t)n * sizeof(int));
    c->pred     = calloc((size_t)n * (size_t)n, sizeof(bool));
    c->dom      = calloc((size_t)n * (size_t)n, sizeof(bool));
    c->loop_hdr = calloc((size_t)n, sizeof(bool));
    if (!c->succ1 || !c->succ2 || !c->pred || !c->dom || !c->loop_hdr) return false;

    for (int i = 0; i < n; i++) {
        c->succ1[i] = c->succ2[i] = -1;
        const Insn *t = block_terminator(&fn->blocks[i]);
        if (!t) continue;                 /* malformed block — leave it isolated */
        if (t->kind == INSN_JMP) {
            c->succ1[i] = block_index(fn, t->data.jmp.target);
        } else if (t->kind == INSN_BR) {
            c->succ1[i] = block_index(fn, t->data.br.true_target);
            c->succ2[i] = block_index(fn, t->data.br.false_target);
        }
    }

    for (int i = 0; i < n; i++) {
        if (c->succ1[i] >= 0) c->pred[c->succ1[i] * n + i] = true;
        if (c->succ2[i] >= 0) c->pred[c->succ2[i] * n + i] = true;
    }

    /* Dominators: dom[0] = {0}; dom[i] = {i} ∪ ⋂ dom[p] over predecessors. */
    for (int j = 0; j < n; j++) c->dom[j] = (j == 0);       /* dom[0][*] */
    for (int i = 1; i < n; i++)
        for (int j = 0; j < n; j++) c->dom[i * n + j] = true;

    bool *acc = malloc((size_t)n * sizeof(bool));
    bool *next = malloc((size_t)n * sizeof(bool));
    if (!acc || !next) { free(acc); free(next); return false; }

    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 1; i < n; i++) {
            for (int j = 0; j < n; j++) acc[j] = true;
            bool has_pred = false;
            for (int p = 0; p < n; p++) {
                if (!c->pred[i * n + p]) continue;
                has_pred = true;
                for (int j = 0; j < n; j++) acc[j] = acc[j] && c->dom[p * n + j];
            }
            if (!has_pred) continue;
            for (int j = 0; j < n; j++) next[j] = (j == i) || acc[j];
            for (int j = 0; j < n; j++) {
                if (next[j] != c->dom[i * n + j]) {
                    c->dom[i * n + j] = next[j];
                    changed = true;
                }
            }
        }
    }
    free(acc);
    free(next);

    /* A -> H is a back edge iff H dominates A. H is then a loop header. */
    for (int i = 0; i < n; i++) {
        int s[2] = { c->succ1[i], c->succ2[i] };
        for (int k = 0; k < 2; k++) {
            int h = s[k];
            if (h >= 0 && c->dom[i * n + h]) c->loop_hdr[h] = true;
        }
    }
    return true;
}

/* Natural loop body of header h: {h} plus everything that reaches a back-edge
   source without going through h. Caller frees. */
static bool *loop_body(const Cfg *c, int h) {
    int n = c->n;
    bool *body = calloc((size_t)n, sizeof(bool));
    int  *stack = malloc((size_t)n * 2 * sizeof(int));
    if (!body || !stack) { free(body); free(stack); return NULL; }

    int sc = 0;
    body[h] = true;
    for (int i = 0; i < n; i++) {
        /* Only back-edge sources count: i -> h requires h to dominate i.
           An ordinary entry edge (e.g. the loop's own init block) must not
           drag outside blocks into the body. */
        if ((c->succ1[i] == h || c->succ2[i] == h) &&
            c->dom[i * n + h] && !body[i]) {
            body[i] = true;
            stack[sc++] = i;
        }
    }
    while (sc > 0) {
        int m = stack[--sc];
        for (int p = 0; p < n; p++) {
            if (c->pred[m * n + p] && !body[p]) {
                body[p] = true;
                stack[sc++] = p;
            }
        }
    }
    free(stack);
    return body;
}

/* A loop with an exit other than the header's own conditional exit needs
   `break`, which the conservative rules do not reconstruct — fall back. */
static bool loop_has_break(const Cfg *c, int h, const bool *body) {
    for (int i = 0; i < c->n; i++) {
        if (!body[i]) continue;
        int s[2] = { c->succ1[i], c->succ2[i] };
        for (int k = 0; k < 2; k++) {
            int x = s[k];
            if (x < 0 || body[x]) continue;
            if (i == h) continue;         /* the header's own exit is fine */
            return true;
        }
    }
    return false;
}

/* Irreducible CFG: a node inside the loop reachable from outside it. */
static bool loop_is_reducible(const Cfg *c, int h, const bool *body) {
    for (int i = 0; i < c->n; i++) {
        if (!body[i] || i == h) continue;
        for (int p = 0; p < c->n; p++)
            if (c->pred[i * c->n + p] && !body[p]) return false;
    }
    return true;
}

/* Header instructions must be foldable loads or pure computation. A phi,
   store, call or atomic in the header would change meaning once hoisted out
   of the loop, so we refuse to restructure and fall back to Core. */
static bool header_is_hoistable(const Cfg *c, int h) {
    const BasicBlock *b = &c->fn->blocks[h];
    for (int i = 0; i < b->insn_count; i++) {
        const Insn *in = &b->insns[i];
        if (in->kind == INSN_JMP || in->kind == INSN_BR || in->kind == INSN_RET) continue;
        switch (in->kind) {
        case INSN_LOAD: case INSN_GEP: case INSN_FIELD:
        case INSN_ADD: case INSN_SUB: case INSN_MUL: case INSN_DIV: case INSN_REM:
        case INSN_AND: case INSN_OR:  case INSN_XOR:
        case INSN_SHL: case INSN_LSHR: case INSN_ASHR:
        case INSN_EQ:  case INSN_NE:  case INSN_SLT: case INSN_SLE:
        case INSN_SGT: case INSN_SGE:
        case INSN_ZEXT: case INSN_SEXT: case INSN_TRUNC:
            break;                        /* safe to hoist or inline */
        default:
            return false;                 /* phi / store / call / atomic */
        }
    }
    return true;
}

/* Sentinel join for `if (c) { return ...; } else { return ...; }` — the two
   arms never meet because both leave the function. */
#define JOIN_RET (-2)

static bool block_is_ret(const Cfg *c, int i) {
    if (i < 0) return false;
    const Insn *t = block_terminator(&c->fn->blocks[i]);
    return t && t->kind == INSN_RET;
}

/* First block reachable from both t and f — the join of an if-diamond.
   Returns -1 when the two arms never meet (irregular control flow),
   JOIN_RET when both arms simply return. */
static int find_join(const Cfg *c, int t, int f) {
    if (t < 0 || f < 0) return -1;
    if (t == f) return t;

    /* An arm that just returns needs no meeting point (cmap.md §6.3).
       Joining at f works either way: when only t returns, f is the
       fall-through; when only f returns, f's `return` is simply emitted
       after the if instead of as an else branch. */
    if (block_is_ret(c, t) && block_is_ret(c, f)) return JOIN_RET;
    if (block_is_ret(c, t) || block_is_ret(c, f)) return f;

    int n = c->n;
    bool *rf   = calloc((size_t)n, sizeof(bool));
    bool *seen = calloc((size_t)n, sizeof(bool));
    int  *q    = malloc((size_t)n * sizeof(int));
    if (!rf || !seen || !q) { free(rf); free(seen); free(q); return -1; }

    /* Neither walk may pass through a loop header. Inside a loop the two
       arms of a branch often meet only after wrapping around the back edge;
       following that would name a block from a later iteration as the join
       and unroll the loop into the output. */
    int qh = 0, qt = 0;
    rf[f] = true;
    q[qt++] = f;
    while (qh < qt) {
        int x = q[qh++];
        if (x != f && c->loop_hdr[x]) continue;
        int s[2] = { c->succ1[x], c->succ2[x] };
        for (int k = 0; k < 2; k++)
            if (s[k] >= 0 && !rf[s[k]]) { rf[s[k]] = true; q[qt++] = s[k]; }
    }

    /* Search from t's successors, not from t itself. Inside a loop t may
       also be reachable from f, which would otherwise make t its own join;
       the join is where the two arms actually meet. */
    int join = -1;
    qh = qt = 0;
    int s0[2] = { c->succ1[t], c->succ2[t] };
    for (int k = 0; k < 2; k++)
        if (s0[k] >= 0 && !seen[s0[k]]) { seen[s0[k]] = true; q[qt++] = s0[k]; }
    while (qh < qt) {
        int x = q[qh++];
        if (rf[x]) { join = x; break; }
        if (c->loop_hdr[x]) continue;
        int s[2] = { c->succ1[x], c->succ2[x] };
        for (int k = 0; k < 2; k++)
            if (s[k] >= 0 && !seen[s[k]]) { seen[s[k]] = true; q[qt++] = s[k]; }
    }

    free(rf);
    free(seen);
    free(q);
    return join;
}

/* Whether the structured layer can handle this function at all. */
static bool cfg_is_structurable(const Cfg *c) {
    /* Loops: the header must be hoistable, single-entry, and free of `break`. */
    for (int h = 0; h < c->n; h++) {
        if (!c->loop_hdr[h]) continue;
        if (!header_is_hoistable(c, h)) return false;
        bool *body = loop_body(c, h);
        if (!body) return false;
        bool bad = loop_has_break(c, h, body) || !loop_is_reducible(c, h, body);
        free(body);
        if (bad) return false;
    }
    /* Every remaining branch must form a diamond — its two arms must meet. */
    for (int i = 0; i < c->n; i++) {
        if (c->loop_hdr[i]) continue;
        const Insn *t = block_terminator(&c->fn->blocks[i]);
        if (!t || t->kind != INSN_BR) continue;
        if (find_join(c, c->succ1[i], c->succ2[i]) == -1) return false;
    }
    return true;
}

static void render_extern(Ctx *ctx, const ExternDecl *e) {
    Str s, rt;
    str_init(&s);
    str_init(&rt);
    type_expr(e->return_type, &rt);
    str_printf(&s, "extern %s %s(", rt.data, e->name);

    if (e->param_count == 0 && !e->is_variadic) {
        str_puts(&s, "void");
    } else {
        for (int i = 0; i < e->param_count; i++) {
            if (i) str_puts(&s, ", ");
            decl(e->params[i].type, e->params[i].name, &s);
        }
        if (e->is_variadic) {
            if (e->param_count) str_puts(&s, ", ");
            str_puts(&s, "...");
        }
    }
    str_puts(&s, ");");
    line_puts(ctx, s.data);
    str_free(&s);
    str_free(&rt);
}

/* ==================================================================
 * Structured layer — rendering (cmap.md §6)
 * ================================================================== */

/* Drop all bindings — each function has its own scope. */
static void binds_clear(BindTable *t) {
    for (int i = 0; i < t->count; i++) {
        free(t->items[i].ssa);
        free(t->items[i].name);
    }
    t->count = 0;
}

/* Pure computations are safe to inline into a condition. */
static bool is_pure_binary(InsnKind k) {
    return k >= INSN_ADD && k <= INSN_SGE;
}

/* Register a block's foldable pointers and loads without emitting anything,
   so a condition rendered afterwards can fold through them. */
static void preregister_folds(Ctx *ctx, const FuncDecl *fn, int idx, int limit) {
    const BasicBlock *blk = &fn->blocks[idx];
    if (limit > blk->insn_count) limit = blk->insn_count;

    for (int i = 0; i < limit; i++) {
        const Insn *in = &blk->insns[i];
        if (in->kind == INSN_GEP) {
            Str n;
            str_init(&n);
            if (gep_fold(ctx, in, &n))
                binds_set(&ctx->binds, in->result, n.data, false);
            str_free(&n);
        } else if (in->kind == INSN_FIELD) {
            Str n;
            str_init(&n);
            const AstNode *ft = NULL;
            if (field_fold(ctx, in, &n, &ft))
                binds_set(&ctx->binds, in->result, n.data, false);
            str_free(&n);
        } else if (in->kind == INSN_LOAD) {
            const Operand *p = &in->data.load.ptr;
            const Binding *b = (p->kind == OPERAND_LOCAL)
                             ? binds_get(&ctx->binds, p->as.local_name) : NULL;
            if (b && !nameset_has(&ctx->snapshots, in->result))
                binds_set(&ctx->binds, in->result, b->name, b->is_array);
        }
    }
}

/* Render a condition value, inlining pure computations defined in block h.
   `inlined` marks consumed instructions so they are not emitted twice. */
static void render_cond_expr(Ctx *ctx, const Cfg *cfg, int h, const Operand *op,
                             Str *s, bool *inlined, bool wrap) {
    const FuncDecl *fn = cfg->fn;

    if (op->kind == OPERAND_LOCAL) {
        const Binding *b = binds_get(&ctx->binds, op->as.local_name);
        if (b) { str_puts(s, b->name); return; }

        const BasicBlock *blk = &fn->blocks[h];
        for (int j = 0; j < blk->insn_count; j++) {
            const Insn *in = &blk->insns[j];
            if (!in->result || strcmp(in->result, op->as.local_name) != 0) continue;
            if (is_pure_binary(in->kind)) {
                inlined[j] = true;
                if (wrap) str_puts(s, "(");
                render_cond_expr(ctx, cfg, h, &in->data.binary.lhs, s, inlined, true);
                str_printf(s, " %s ", binary_op_str(in->kind));
                render_cond_expr(ctx, cfg, h, &in->data.binary.rhs, s, inlined, true);
                if (wrap) str_puts(s, ")");
                return;
            }
            if (in->kind == INSN_LOAD &&
                !nameset_has(&ctx->snapshots, in->result)) {
                /* Fold it: a load through a foldable pointer reads as the
                   named storage itself (cmap.md §5.3 rule A). */
                inlined[j] = true;
                mem_read(ctx, &in->data.load.ptr, s);
                return;
            }
            break;
        }
    }
    operand(ctx, op, s);
}

/* ---- inline sugar (cmap.md §6.5) -------------------------------------
   A pure computation read by exactly one instruction — the store right
   after it — collapses into that store: `j = i + 1;` rather than
   `int32 j_start = i + 1; j = j_start;`. Only safe when nothing else
   reads the value, hence the use count. */

static int op_refs(const Operand *op, const char *name) {
    return op->kind == OPERAND_LOCAL && strcmp(op->as.local_name, name) == 0;
}

static int insn_refs(const Insn *in, const char *name) {
    int n = 0;
    switch (in->kind) {
    case INSN_ADD: case INSN_SUB: case INSN_MUL: case INSN_DIV: case INSN_REM:
    case INSN_UDIV: case INSN_UREM: case INSN_MULHI:
    case INSN_AND: case INSN_OR:  case INSN_XOR:
    case INSN_SHL: case INSN_LSHR: case INSN_ASHR:
    case INSN_EQ:  case INSN_NE:  case INSN_SLT: case INSN_SLE:
    case INSN_SGT: case INSN_SGE:
        n += op_refs(&in->data.binary.lhs, name);
        n += op_refs(&in->data.binary.rhs, name);
        break;
    case INSN_LOAD:
        n += op_refs(&in->data.load.ptr, name);
        break;
    case INSN_STORE:
        n += op_refs(&in->data.store.value, name);
        n += op_refs(&in->data.store.ptr, name);
        break;
    case INSN_GEP:
        n += op_refs(&in->data.gep.base, name);
        n += op_refs(&in->data.gep.index, name);
        break;
    case INSN_FIELD:
        n += op_refs(&in->data.field.base, name);
        n += op_refs(&in->data.field.field_index, name);
        break;
    case INSN_LOAD_ATOMIC:
        n += op_refs(&in->data.load_atomic.ptr, name);
        break;
    case INSN_STORE_ATOMIC:
        n += op_refs(&in->data.store_atomic.value, name);
        n += op_refs(&in->data.store_atomic.ptr, name);
        break;
    case INSN_ATOMICRMW:
        n += op_refs(&in->data.atomicrmw.ptr, name);
        n += op_refs(&in->data.atomicrmw.value, name);
        break;
    case INSN_CMPXCHG:
        n += op_refs(&in->data.cmpxchg.ptr, name);
        n += op_refs(&in->data.cmpxchg.cmp, name);
        n += op_refs(&in->data.cmpxchg.new_val, name);
        break;
    case INSN_BR:
        n += op_refs(&in->data.br.cond, name);
        break;
    case INSN_PHI:
        for (int i = 0; i < in->data.phi.count; i++)
            n += op_refs(&in->data.phi.cases[i].value, name);
        break;
    case INSN_RET:
        if (!in->data.ret.is_void) n += op_refs(&in->data.ret.value, name);
        break;
    case INSN_CALL:
        for (int i = 0; i < in->data.call.argc; i++)
            n += op_refs(&in->data.call.args[i], name);
        break;
    case INSN_ASM:
        for (int i = 0; i < in->data.asm_insn.operand_count; i++)
            n += op_refs(&in->data.asm_insn.operands[i], name);
        break;
    case INSN_ZEXT: case INSN_SEXT: case INSN_TRUNC:
        n += op_refs(&in->data.convert.value, name);
        break;
    case INSN_TENSOR:
        n += op_refs(&in->data.tensor.dst, name);
        n += op_refs(&in->data.tensor.lhs, name);
        n += op_refs(&in->data.tensor.rhs, name);
        for (int i = 0; i < in->data.tensor.ndim; i++)
            n += op_refs(&in->data.tensor.dims[i], name);
        break;
    default:
        break;
    }
    return n;
}

static int count_uses(const FuncDecl *fn, const char *name) {
    int n = 0;
    for (int i = 0; i < fn->block_count; i++) {
        const BasicBlock *b = &fn->blocks[i];
        for (int j = 0; j < b->insn_count; j++)
            n += insn_refs(&b->insns[j], name);
    }
    return n;
}

/* Can instruction i fold into instruction i+1? */
static bool folds_into_next(const FuncDecl *fn, const BasicBlock *blk, int i) {
    if (i + 1 >= blk->insn_count) return false;
    const Insn *def = &blk->insns[i];
    const Insn *st  = &blk->insns[i + 1];
    if (!def->result || !is_pure_binary(def->kind)) return false;
    if (st->kind != INSN_STORE) return false;
    if (st->data.store.value.kind != OPERAND_LOCAL) return false;
    if (strcmp(st->data.store.value.as.local_name, def->result) != 0) return false;
    return count_uses(fn, def->result) == 1;
}

static void render_folded_store(Ctx *ctx, const BasicBlock *blk, int i) {
    const Insn *def = &blk->insns[i];
    const Insn *st  = &blk->insns[i + 1];
    Str code;
    str_init(&code);
    mem_write(ctx, &st->data.store.ptr, &code);
    str_puts(&code, " = ");
    operand(ctx, &def->data.binary.lhs, &code);
    str_printf(&code, " %s ", binary_op_str(def->kind));
    operand(ctx, &def->data.binary.rhs, &code);
    str_puts(&code, ";");
    line_puts(ctx, code.data);
    str_free(&code);
}

/* Emit a block's instructions, optionally skipping marked ones and the
   terminator. */
static void render_block_insns(Ctx *ctx, const FuncDecl *fn, int idx,
                               const bool *skip, bool skip_terminator) {
    const BasicBlock *blk = &fn->blocks[idx];
    int n = blk->insn_count;
    if (skip_terminator && n > 0) n--;

    for (int i = 0; i < n; i++) {
        if (skip && skip[i]) continue;
        if ((!skip || !skip[i + 1]) && folds_into_next(fn, blk, i)) {
            render_folded_store(ctx, blk, i);
            i++;                     /* the store consumed both instructions */
            continue;
        }
        render_insn_core(ctx, fn, &blk->insns[i]);
    }
}

static void render_region(Ctx *ctx, const Cfg *cfg, int start, int stop);

/* Render a loop headed by block h; returns the block to continue at.
 *
 * Which branch of the header leads into the body depends on how the source
 * wrote it. `br %c, @body, @done` and `br %c, @done, @body` are both common
 * — the latter reads as "if (cond) break". So pick the target that can reach
 * the back edge, and negate the condition when the body is the false branch. */
static int render_loop(Ctx *ctx, const Cfg *cfg, int h) {
    const FuncDecl *fn = cfg->fn;
    const BasicBlock *blk = &fn->blocks[h];
    const Insn *t = block_terminator(blk);
    if (!t || t->kind != INSN_BR) return -1;

    int body_target = -1, exit_target = -1;
    bool negate = false;
    bool *body = loop_body(cfg, h);
    if (body) {
        if (cfg->succ1[h] >= 0 && body[cfg->succ1[h]]) {
            body_target = cfg->succ1[h];
            exit_target = cfg->succ2[h];
        } else if (cfg->succ2[h] >= 0 && body[cfg->succ2[h]]) {
            body_target = cfg->succ2[h];
            exit_target = cfg->succ1[h];
            negate = true;
        }
        free(body);
    }
    if (body_target < 0) return -1;

    /* Register foldable pointers/loads first so the condition can fold
       through them instead of naming intermediate pointers. */
    preregister_folds(ctx, fn, h, blk->insn_count - 1);

    bool *inlined = calloc((size_t)blk->insn_count, sizeof(bool));
    Str cond;
    str_init(&cond);
    render_cond_expr(ctx, cfg, h, &t->data.br.cond, &cond, inlined, false);

    /* Header instructions that were not inlined are hoistable by design. */
    render_block_insns(ctx, fn, h, inlined, true);

    if (negate) line_printf(ctx, "while (!(%s)) {", cond.data);
    else        line_printf(ctx, "while (%s) {", cond.data);
    ctx->indent++;
    render_region(ctx, cfg, body_target, h);
    ctx->indent--;
    line_puts(ctx, "}");

    free(inlined);
    str_free(&cond);
    return exit_target;
}

/* Render an if / if-else whose two arms meet at `join`. */
static void render_if(Ctx *ctx, const Cfg *cfg, int cur, int join) {
    const FuncDecl *fn = cfg->fn;
    const BasicBlock *blk = &fn->blocks[cur];
    const Insn *t = block_terminator(blk);
    if (!t || t->kind != INSN_BR) return;

    int tt = cfg->succ1[cur];
    int ft = cfg->succ2[cur];

    preregister_folds(ctx, fn, cur, blk->insn_count - 1);

    bool *inlined = calloc((size_t)blk->insn_count, sizeof(bool));
    Str cond;
    str_init(&cond);
    render_cond_expr(ctx, cfg, cur, &t->data.br.cond, &cond, inlined, false);

    render_block_insns(ctx, fn, cur, inlined, true);

    line_printf(ctx, "if (%s) {", cond.data);
    ctx->indent++;
    render_region(ctx, cfg, tt, join);
    ctx->indent--;
    if (ft != join) {
        line_puts(ctx, "} else {");
        ctx->indent++;
        render_region(ctx, cfg, ft, join);
        ctx->indent--;
    }
    line_puts(ctx, "}");

    free(inlined);
    str_free(&cond);
}

/* Render blocks from `start` up to (but not including) `stop`.
   stop < 0 means "until control flow ends". */
static int g_region_depth = 0;
/* Blocks rendered for the current function. A backstop so that no CFG shape
   — however unexpected — can make the structured walker spin forever. */
static int g_render_steps = 0;
#define CMAP_STEP_LIMIT 200000

static void render_region(Ctx *ctx, const Cfg *cfg, int start, int stop) {
    /* Guard against runaway recursion on pathological CFGs. */
    if (++g_region_depth > 64) { g_region_depth--; return; }

    int cur = start;

    while (cur >= 0 && cur != stop) {
        if (++g_render_steps > CMAP_STEP_LIMIT) break;

        if (cfg->loop_hdr[cur]) {
            cur = render_loop(ctx, cfg, cur);   /* continue at the loop's exit */
            continue;
        }

        const Insn *t = block_terminator(&cfg->fn->blocks[cur]);
        if (t && t->kind == INSN_BR) {
            int join = find_join(cfg, cfg->succ1[cur], cfg->succ2[cur]);
            if (join != -1 && join != cur) {   /* JOIN_RET is a valid join */
                render_if(ctx, cfg, cur, join);
                cur = join;
                continue;
            }
        }

        /* An unconditional jump that falls through to the next block we are
           about to render needs no goto. */
        bool skip_term = (t && t->kind == INSN_JMP);
        render_block_insns(ctx, cfg->fn, cur, NULL, skip_term);
        cur = (t && t->kind == INSN_RET) ? -1 : cfg->succ1[cur];
    }

    g_region_depth--;
}

static void render_func_structured(Ctx *ctx, const FuncDecl *fn, const Cfg *cfg) {
    Str sig;
    str_init(&sig);
    render_signature(fn, &sig);
    str_puts(&sig, " {");
    line_puts(ctx, sig.data);
    str_free(&sig);

    ctx->indent++;
    render_region(ctx, cfg, 0, -1);
    ctx->indent--;
    line_puts(ctx, "}");
}

/* ==================================================================
 * Entry point
 * ================================================================== */

bool cmap_emit_program(const Program *prog, FILE *out, char *err, size_t err_size) {
    (void)err;
    (void)err_size;

    Ctx ctx;
    ctx_init(&ctx, prog, out);

    for (int i = 0; i < prog->struct_count; i++) {
        render_struct(&ctx, &prog->structs[i]);
        line_puts(&ctx, "");
    }
    for (int i = 0; i < prog->global_count; i++) {
        const GlobalDecl *g = &prog->globals[i];
        /* Anonymous string constants are a parser artifact; their contents
           already show up as literals at the call sites (cmap.md §4.9). */
        if (is_anon_string_name(global_display_name(g->name))) continue;
        render_global(&ctx, g);
    }
    if (prog->global_count) line_puts(&ctx, "");
    for (int i = 0; i < prog->extern_count; i++)
        render_extern(&ctx, &prog->externs[i]);
    if (prog->extern_count) line_puts(&ctx, "");

    for (int i = 0; i < prog->func_count; i++) {
        const FuncDecl *fn = &prog->functions[i];

        binds_clear(&ctx.binds);
        collect_snapshots(&ctx.snapshots, fn);
        g_render_steps = 0;

        /* Try the structured layer; fall back to Core when the CFG does not
           fit the conservative patterns (cmap.md §6.6). */
        bool done = false;
        Cfg cfg;
        if (cfg_build(&cfg, fn)) {
            if (cfg_is_structurable(&cfg)) {
                render_func_structured(&ctx, fn, &cfg);
                done = true;
            }
            cfg_free(&cfg);
        }
        if (!done) {
            binds_clear(&ctx.binds);
            render_func_core(&ctx, fn);
        }

        line_puts(&ctx, "");
    }

    ctx_free(&ctx);
    return true;
}
