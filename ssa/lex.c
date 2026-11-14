#include "lex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------- Character classification helpers ---------- */

static bool is_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static bool is_ident_cont(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

/* ---------- Keyword / type lookup ---------- */

typedef struct {
    const char *word;
    int len;
    TokenKind kind;
} KeywordEntry;

static const KeywordEntry keywords[] = {
    {"global", 6, TK_GLOBAL},
    {"internal", 8, TK_INTERNAL},
    {"fun",    3, TK_FUN},
    {"extern", 6, TK_EXTERN},
    {"struct", 6, TK_STRUCT},
    {"enum",   4, TK_ENUM},
    {"field",  5, TK_FIELD},
    {"let",    3, TK_LET},
    {"acquire",     7, TK_ACQUIRE},
    {"acq_rel",     7, TK_ACQ_REL},
    {"add",         3, TK_ADD},
    {"sub",    3, TK_SUB},
    {"mul",    3, TK_MUL},
    {"div",    3, TK_DIV},
    {"rem",    3, TK_REM},
    {"and",    3, TK_AND},
    {"atomic", 6, TK_ATOMIC},
    {"acq_rel", 7, TK_ACQ_REL},
    {"acquire", 7, TK_ACQUIRE},
    {"atomicrmw", 9, TK_ATOMICRMW},
    {"or",     2, TK_OR},
    {"xchg",   4, TK_XCHG},
    {"xor",    3, TK_XOR},
    {"shl",    3, TK_SHL},
    {"lshr",   4, TK_LSHR},
    {"ashr",   4, TK_ASHR},
    {"eq",     2, TK_EQ},
    {"ne",     2, TK_NE},
    {"null",   4, TK_NULL},
    {"slt",    3, TK_SLT},
    {"sle",    3, TK_SLE},
    {"sgt",    3, TK_SGT},
    {"sge",    3, TK_SGE},
    {"load",   4, TK_LOAD},
    {"store",  5, TK_STORE},
    {"gep",    3, TK_GEP},
    {"jmp",    3, TK_JMP},
    {"br",     2, TK_BR},
    {"phi",    3, TK_PHI},
    {"ret",    3, TK_RET},
    {"call",   4, TK_CALL},
    {"asm",    3, TK_ASM},
    {"tensor", 6, TK_TENSOR},
    {"matmul", 6, TK_MATMUL},
    {"att",    3, TK_ATT},
    {"intel",  5, TK_INTEL},
    {"alignstack", 10, TK_ALIGNSTACK},
    {"const",  5, TK_CONST},
    {"cmpxchg", 7, TK_CMPXCHG},
    {"relaxed", 7, TK_RELAXED},
    {"release", 7, TK_RELEASE},
    {"seq_cst", 7, TK_SEQ_CST},
    {"zext",   4, TK_ZEXT},
    {"sext",   4, TK_SEXT},
    {"trunc",  5, TK_TRUNC},
    {"mulhi",      5, TK_MULHI},
    {"udiv",       4, TK_UDIV},
    {"urem",       4, TK_UREM},
    {"true",   4, TK_TRUE},
    {"false",  5, TK_FALSE},
    {"void",   4, TK_VOID},
    {"to",     2, TK_TO},
};

#define NUM_KEYWORDS (sizeof(keywords) / sizeof(keywords[0]))

static TokenKind lookup_keyword(const char *start, int len) {
    for (size_t i = 0; i < NUM_KEYWORDS; i++) {
        if (keywords[i].len == len &&
            memcmp(start, keywords[i].word, len) == 0) {
            return keywords[i].kind;
        }
    }
    return TK_ERROR; /* not found */
}

/* ---------- Type keyword lookup ---------- */

typedef struct {
    const char *word;
    int len;
    LexTypeKind kind;
} TypeEntry;

static const TypeEntry type_keywords[] = {
    {"i1",  2, LEX_TYPE_I1},
    {"i8",  2, LEX_TYPE_I8},
    {"i16", 3, LEX_TYPE_I16},
    {"i32", 3, LEX_TYPE_I32},
    {"i64", 3, LEX_TYPE_I64},
    {"f32", 3, LEX_TYPE_F32},
    {"f64", 3, LEX_TYPE_F64},
};

#define NUM_TYPE_KEYWORDS (sizeof(type_keywords) / sizeof(type_keywords[0]))

static LexTypeKind lookup_type(const char *start, int len) {
    for (size_t i = 0; i < NUM_TYPE_KEYWORDS; i++) {
        if (type_keywords[i].len == len &&
            memcmp(start, type_keywords[i].word, len) == 0) {
            return type_keywords[i].kind;
        }
    }
    return (LexTypeKind)-1;
}

/* ---------- Internal helpers ---------- */

static void lexer_set_error(Lexer *lex, const char *msg) {
    lex->has_error = true;
    strncpy(lex->error_msg, msg, sizeof(lex->error_msg) - 1);
    lex->error_msg[sizeof(lex->error_msg) - 1] = '\0';
}

static void skip_whitespace_and_comments(Lexer *lex) {
    while (*(lex->pos)) {
        char c = *(lex->pos);
        if (c == ' ' || c == '\t' || c == '\r') {
            lex->pos++;
            lex->column++;
        } else if (c == '\n') {
            lex->pos++;
            lex->line++;
            lex->column = 1;
        } else if (c == '/' && *(lex->pos + 1) == '/') {
            /* Line comment: skip to end of line */
            lex->pos += 2;
            lex->column += 2;
            while (*(lex->pos) && *(lex->pos) != '\n') {
                lex->pos++;
                lex->column++;
            }
            /* The newline will be consumed on the next iteration */
        } else {
            break;
        }
    }
}

/* ---------- Public API ---------- */

void lexer_init(Lexer *lex, const char *source) {
    lex->source = (char *)source;   /* not owned */
    lex->source_len = strlen(source);
    lex->pos = source;
    lex->filename = NULL;
    lex->line = 1;
    lex->column = 1;
    lex->own_source = false;
    lex->has_error = false;
    lex->error_msg[0] = '\0';
}

bool lexer_init_file(Lexer *lex, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        lex->source = NULL;
        lex->source_len = 0;
        lex->pos = NULL;
        lex->own_source = false;
        lex->has_error = true;
        snprintf(lex->error_msg, sizeof(lex->error_msg),
                 "cannot open file: %s", path);
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    lex->source = (char *)malloc((size_t)size + 1);
    if (!lex->source) {
        fclose(fp);
        lex->has_error = true;
        snprintf(lex->error_msg, sizeof(lex->error_msg), "out of memory");
        return false;
    }

    fread(lex->source, 1, (size_t)size, fp);
    lex->source[size] = '\0';
    fclose(fp);

    lex->source_len = (size_t)size;
    lex->pos = lex->source;
    lex->filename = path;
    lex->line = 1;
    lex->column = 1;
    lex->own_source = true;
    lex->has_error = false;
    lex->error_msg[0] = '\0';
    return true;
}

void lexer_free(Lexer *lex) {
    if (lex->own_source && lex->source) {
        free(lex->source);
    }
    lex->source = NULL;
    lex->pos = NULL;
}

bool lexer_next(Lexer *lex, Token *token) {
    skip_whitespace_and_comments(lex);

    token->start = lex->pos;
    token->line = lex->line;
    token->column = lex->column;

    char c = *(lex->pos);

    if (c == '\0') {
        token->kind = TK_EOF;
        token->length = 0;
        return false;
    }

    /* ---- @name (global symbol / label reference) ---- */
    if (c == '@') {
        lex->pos++;
        lex->column++;
        token->start = lex->pos; /* point past @ */
        token->column = lex->column; /* update column to name position */
        if (!is_ident_start(*(lex->pos))) {
            token->kind = TK_ERROR;
            lexer_set_error(lex, "expected identifier after '@'");
            return false;
        }
        /* Dots join a qualified name (@Color.Red). A trailing dot that is not
           followed by an identifier stays a separate token. */
        while (is_ident_cont(*(lex->pos)) ||
               (*(lex->pos) == '.' && is_ident_start(*(lex->pos + 1)))) {
            lex->pos++;
            lex->column++;
        }
        token->kind = TK_AT_SYM;
        token->length = (int)(lex->pos - token->start);
        return true;
    }

    /* ---- Character literal: 'A', '\n', '\x41' ---- */
    if (c == '\'') {
        lex->pos++; lex->column++;
        int64_t cv = 0;
        if (*(lex->pos) == '\\') {
            lex->pos++; lex->column++;
            switch (*(lex->pos)) {
                case 'n':  cv = '\n'; lex->pos++; lex->column++; break;
                case 'r':  cv = '\r'; lex->pos++; lex->column++; break;
                case 't':  cv = '\t'; lex->pos++; lex->column++; break;
                case '0':  cv = '\0'; lex->pos++; lex->column++; break;
                case '\\': cv = '\\'; lex->pos++; lex->column++; break;
                case '\'': cv = '\''; lex->pos++; lex->column++; break;
                case 'x':
                    lex->pos++; lex->column++;
                    for (int i = 0; i < 2 && isxdigit((unsigned char)*(lex->pos)); i++) {
                        char ch = *(lex->pos);
                        if (ch >= '0' && ch <= '9')      cv = (cv << 4) | (ch - '0');
                        else if (ch >= 'a' && ch <= 'f') cv = (cv << 4) | (ch - 'a' + 10);
                        else if (ch >= 'A' && ch <= 'F') cv = (cv << 4) | (ch - 'A' + 10);
                        lex->pos++; lex->column++;
                    }
                    break;
                default:  cv = (int64_t)(unsigned char)*(lex->pos); lex->pos++; lex->column++; break;
            }
        } else {
            cv = (int64_t)(unsigned char)*(lex->pos);
            lex->pos++; lex->column++;
        }
        if (*(lex->pos) != '\'') {
            token->kind = TK_ERROR;
            lexer_set_error(lex, "unterminated character literal");
            return false;
        }
        lex->pos++; lex->column++;
        token->kind = TK_INT;
        token->int_val = cv;
        return true;
    }

    /* ---- %name (local SSA value) ---- */
    if (c == '%') {
        lex->pos++;
        lex->column++;
        token->start = lex->pos;
        token->column = lex->column; /* update column to name position */
        if (!is_ident_start(*(lex->pos)) && !isdigit((unsigned char)*(lex->pos))) {
            token->kind = TK_ERROR;
            lexer_set_error(lex, "expected identifier after '%'");
            return false;
        }
        /* % can be followed by digits (e.g. %0, %1) */
        while (is_ident_cont(*(lex->pos)) || isdigit((unsigned char)*(lex->pos))) {
            lex->pos++;
            lex->column++;
        }
        token->kind = TK_PCT_SYM;
        token->length = (int)(lex->pos - token->start);
        return true;
    }

    /* ---- Numbers: integer and float ---- */
    if (isdigit((unsigned char)c) ||
        (c == '-' && isdigit((unsigned char)*(lex->pos + 1)))) {
        bool negative = false;
        if (c == '-') {
            negative = true;
            lex->pos++;
            lex->column++;
        }

        /* ---- Hex literal: 0x... ---- */
        if (*(lex->pos) == '0' &&
            (*(lex->pos + 1) == 'x' || *(lex->pos + 1) == 'X')) {
            lex->pos += 2;
            lex->column += 2;
            token->start = lex->pos;
            if (!isxdigit((unsigned char)*(lex->pos))) {
                token->kind = TK_ERROR;
                lexer_set_error(lex, "expected hex digit after '0x'");
                return false;
            }
            while (isxdigit((unsigned char)*(lex->pos))) {
                lex->pos++;
                lex->column++;
            }
            token->length = (int)(lex->pos - token->start);
            size_t flen = (size_t)token->length;
            char *buf = (char *)malloc(flen + 1);
            if (!buf) { token->kind = TK_ERROR; lexer_set_error(lex, "out of memory"); return false; }
            memcpy(buf, token->start, (size_t)token->length);
            buf[flen] = '\0';
            token->int_val = strtoll(buf, NULL, 16);
            free(buf);
            token->kind = TK_INT;
            return true;
        }

        token->start = lex->pos;
        bool is_float = false;
        while (isdigit((unsigned char)*(lex->pos))) {
            lex->pos++;
            lex->column++;
        }
        if (*(lex->pos) == '.' && isdigit((unsigned char)*(lex->pos + 1))) {
            is_float = true;
            lex->pos++; /* skip '.' */
            lex->column++;
            while (isdigit((unsigned char)*(lex->pos))) {
                lex->pos++;
                lex->column++;
            }
        }

        token->length = (int)(lex->pos - token->start);

        if (is_float) {
            /* Need the full string including '-' for strtod */
            char *end = NULL;
            /* Build temp string: '-' + digits.digits */
            size_t full_len = (size_t)(lex->pos - token->start) + (negative ? 1 : 0);
            char *buf = (char *)malloc(full_len + 1);
            if (!buf) {
                token->kind = TK_ERROR;
                lexer_set_error(lex, "out of memory");
                return false;
            }
            char *wp = buf;
            if (negative) *wp++ = '-';
            memcpy(wp, token->start, (size_t)(lex->pos - token->start));
            buf[full_len] = '\0';
            token->float_val = strtod(buf, &end);
            free(buf);
            token->kind = TK_FLOAT;
        } else {
            char *end = NULL;
            /* Build temp string for strtoll */
            size_t full_len = (size_t)(lex->pos - token->start) + (negative ? 1 : 0);
            char *buf = (char *)malloc(full_len + 1);
            if (!buf) {
                token->kind = TK_ERROR;
                lexer_set_error(lex, "out of memory");
                return false;
            }
            char *wp = buf;
            if (negative) *wp++ = '-';
            memcpy(wp, token->start, (size_t)(lex->pos - token->start));
            buf[full_len] = '\0';
            token->int_val = strtoll(buf, &end, 10);
            free(buf);
            token->kind = TK_INT;
        }

        /* Reset start to beginning of number (including sign) for consistency */
        if (negative) {
            token->start--; /* point to '-' */
            token->length++;
        }
        return true;
    }

    /* ---- Identifiers (bare keywords / type names / param names) ---- */
    if (is_ident_start(c)) {
        while (is_ident_cont(*(lex->pos))) {
            lex->pos++;
            lex->column++;
        }
        token->length = (int)(lex->pos - token->start);

        /* Check for type keywords first */
        LexTypeKind ltk = lookup_type(token->start, token->length);
        if (ltk != (LexTypeKind)-1) {
            token->kind = TK_TYPE;
            token->lex_type = ltk;
            return true;
        }

        /* Then check for other keywords */
        TokenKind kw = lookup_keyword(token->start, token->length);
        if (kw != TK_ERROR) {
            token->kind = kw;
            return true;
        }

        /* Otherwise it's a plain identifier (parameter name) */
        token->kind = TK_IDENT;
        return true;
    }

    /* ---- String literal: "..." ---- */
    if (c == '"') {
        lex->pos++;
        lex->column++;
        token->start = lex->pos;
        while (*(lex->pos) && *(lex->pos) != '"') {
            if (*(lex->pos) == '\\' && *(lex->pos + 1))
                lex->pos++; /* skip escape */
            lex->pos++;
            lex->column++;
        }
        token->length = (int)(lex->pos - token->start);
        token->kind = TK_STR;
        if (*(lex->pos) == '"') {
            lex->pos++;
            lex->column++;
        } else {
            token->kind = TK_ERROR;
            lexer_set_error(lex, "unterminated string literal");
            return false;
        }
        return true;
    }

    /* ---- Ellipsis: ... ---- */
    if (c == '.' && *(lex->pos + 1) == '.' && *(lex->pos + 2) == '.') {
        token->kind = TK_ELLIPSIS;
        token->length = 3;
        lex->pos += 3;
        lex->column += 3;
        return true;
    }

    /* ---- Single / multi-character punctuation ---- */
    token->length = 1;
    switch (c) {
        case '{': token->kind = TK_LBRACE; break;
        case '}': token->kind = TK_RBRACE; break;
        case '(': token->kind = TK_LPAREN; break;
        case ')': token->kind = TK_RPAREN; break;
        case '[': token->kind = TK_LBRACK; break;
        case ']': token->kind = TK_RBRACK; break;
        case ',': token->kind = TK_COMMA; break;
        case ':': token->kind = TK_COLON; break;
        case '=': token->kind = TK_ASSIGN; break;
        case '*': token->kind = TK_STAR; break;
        case '-':
            if (*(lex->pos + 1) == '>') {
                lex->pos++;
                lex->column++;
                token->length = 2;
                token->kind = TK_ARROW;
            } else {
                token->kind = TK_ERROR;
                lexer_set_error(lex, "unexpected '-' (did you mean '->'?)");
                return false;
            }
            break;
        default:
            token->kind = TK_ERROR;
            {
                char msg[64];
                snprintf(msg, sizeof(msg),
                         "unexpected character: '%c' (0x%02x)", c, (unsigned char)c);
                lexer_set_error(lex, msg);
            }
            return false;
    }

    lex->pos++;
    lex->column++;
    return true;
}

const char *lexer_get_error(const Lexer *lex) {
    return lex->error_msg;
}

const char *token_kind_name(TokenKind kind) {
    switch (kind) {
        case TK_EOF:    return "EOF";
        case TK_ERROR:  return "ERROR";
        case TK_AT_SYM: return "@name";
        case TK_PCT_SYM: return "%name";
        case TK_IDENT: return "identifier";
        case TK_INT:    return "integer";
        case TK_FLOAT:  return "float";
        case TK_STR:    return "string";
        case TK_TYPE:   return "type";
        case TK_CONST:  return "const";
        case TK_GLOBAL: return "global";
        case TK_FUN:    return "fun";
        case TK_EXTERN: return "extern";
        case TK_STRUCT: return "struct";
        case TK_ENUM:   return "enum";
        case TK_FIELD:  return "field";
        case TK_LET:    return "let";
        case TK_ADD:    return "add";
        case TK_SUB:    return "sub";
        case TK_MUL:    return "mul";
        case TK_DIV:    return "div";
        case TK_REM:    return "rem";
        case TK_AND:    return "and";
        case TK_OR:     return "or";
        case TK_XCHG:   return "xchg";
        case TK_XOR:    return "xor";
        case TK_SHL:    return "shl";
        case TK_LSHR:   return "lshr";
        case TK_ASHR:   return "ashr";
        case TK_EQ:     return "eq";
        case TK_NE:     return "ne";
        case TK_SLT:    return "slt";
        case TK_SLE:    return "sle";
        case TK_SGT:    return "sgt";
        case TK_SGE:    return "sge";
        case TK_LOAD:   return "load";
        case TK_STORE:  return "store";
        case TK_GEP:    return "gep";
        case TK_JMP:    return "jmp";
        case TK_BR:     return "br";
        case TK_PHI:    return "phi";
        case TK_RET:    return "ret";
        case TK_CALL:   return "call";
        case TK_ASM:    return "asm";
        case TK_TENSOR: return "tensor";
        case TK_MATMUL: return "matmul";
        case TK_ATT:    return "att";
        case TK_INTEL:  return "intel";
        case TK_ALIGNSTACK: return "alignstack";
        case TK_ATOMIC: return "atomic";
        case TK_CMPXCHG: return "cmpxchg";
        case TK_SEQ_CST: return "seq_cst";
        case TK_ACQUIRE: return "acquire";
        case TK_RELEASE: return "release";
        case TK_ACQ_REL: return "acq_rel";
        case TK_RELAXED: return "relaxed";
        case TK_ATOMICRMW: return "atomicrmw";
        case TK_ZEXT:   return "zext";
        case TK_SEXT:   return "sext";
        case TK_TRUNC:  return "trunc";
        case TK_MULHI:      return "mulhi";
        case TK_UDIV:       return "udiv";
        case TK_UREM:       return "urem";
        case TK_TRUE:   return "true";
        case TK_FALSE:  return "false";
        case TK_NULL:   return "null";
        case TK_VOID:   return "void";
        case TK_TO:     return "to";
        case TK_LBRACE: return "{";
        case TK_RBRACE: return "}";
        case TK_LPAREN: return "(";
        case TK_RPAREN: return ")";
        case TK_LBRACK: return "[";
        case TK_RBRACK: return "]";
        case TK_COMMA:  return ",";
        case TK_COLON:  return ":";
        case TK_ASSIGN: return "=";
        case TK_ARROW:  return "->";
        case TK_ELLIPSIS: return "...";
        case TK_STAR:   return "*";
        default:        return "?";
    }
}

const char *lex_type_name(LexTypeKind kind) {
    switch (kind) {
        case LEX_TYPE_I1:  return "i1";
        case LEX_TYPE_I8:  return "i8";
        case LEX_TYPE_I16: return "i16";
        case LEX_TYPE_I32: return "i32";
        case LEX_TYPE_I64: return "i64";
        case LEX_TYPE_F32: return "f32";
        case LEX_TYPE_F64: return "f64";
        case LEX_TYPE_VOID: return "void";
        default:           return "?";
    }
}
