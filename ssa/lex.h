#ifndef LEX_H
#define LEX_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ---------- Token kinds ---------- */

typedef enum {
    /* Special */
    TK_EOF,
    TK_ERROR,

    /* Named symbols */
    TK_AT_SYM,    /* @name */
    TK_PCT_SYM,   /* %name */
    TK_IDENT,     /* bare identifier (parameter name) */

    /* Literals */
    TK_INT,
    TK_FLOAT,
    TK_STR,

    /* Basic type keywords */
    TK_TYPE,      /* i1, i8, i16, i32, i64, f32, f64 */

    /* Structural keywords */
    TK_GLOBAL,
    TK_CONST,
    TK_INTERNAL,
    TK_FUN,
    TK_EXTERN,
    TK_STRUCT,
    TK_ENUM,

    /* Instruction keywords */
    TK_LET,
    TK_ADD, TK_SUB, TK_MUL, TK_DIV, TK_REM,
    TK_AND, TK_OR, TK_XOR, TK_XCHG, TK_SHL, TK_LSHR, TK_ASHR,
    TK_EQ, TK_NE, TK_SLT, TK_SLE, TK_SGT, TK_SGE,
    TK_LOAD, TK_STORE, TK_GEP, TK_FIELD,
    TK_JMP, TK_BR, TK_PHI, TK_RET,
    TK_CALL,
    TK_ASM, TK_ATT, TK_INTEL, TK_ALIGNSTACK,
    TK_ZEXT, TK_SEXT, TK_TRUNC,
    TK_MULHI, TK_UDIV, TK_UREM,
    TK_ATOMIC, TK_CMPXCHG, TK_ATOMICRMW,
    TK_TRUE, TK_FALSE, TK_NULL,
    TK_VOID,
    TK_TO,
    TK_SEQ_CST, TK_ACQUIRE, TK_RELEASE, TK_ACQ_REL, TK_RELAXED,

    /* Punctuation */
    TK_LBRACE, TK_RBRACE,
    TK_LPAREN, TK_RPAREN,
    TK_LBRACK, TK_RBRACK,
    TK_COMMA, TK_COLON, TK_ASSIGN, TK_ARROW, TK_STAR, TK_ELLIPSIS,

    TK_COUNT
} TokenKind;

/* ---------- Basic type kinds (for TK_TYPE tokens) ---------- */

typedef enum {
    LEX_TYPE_I1 = 0,
    LEX_TYPE_I8,
    LEX_TYPE_I16,
    LEX_TYPE_I32,
    LEX_TYPE_I64,
    LEX_TYPE_F32,
    LEX_TYPE_F64,
    LEX_TYPE_VOID,
} LexTypeKind;

/* ---------- Token ---------- */

typedef struct {
    TokenKind kind;
    const char *start;   /* pointer into source buffer */
    int length;          /* byte length of token text */
    int line;
    int column;
    union {
        int64_t int_val;
        double float_val;
        LexTypeKind lex_type;
    };
} Token;

/* ---------- Lexer ---------- */

typedef struct {
    char *source;         /* owned source buffer (may be NULL) */
    size_t source_len;
    const char *pos;      /* current read position */
    const char *filename; /* source file path, not owned (NULL for string init) */
    int line;
    int column;
    bool own_source;      /* free source on cleanup? */
    char error_msg[256];
    bool has_error;
} Lexer;

/* ---------- API ---------- */

/* Initialize lexer from a null-terminated string (copy is NOT made). */
void lexer_init(Lexer *lex, const char *source);

/* Initialize lexer by reading a file. Returns false on error. */
bool lexer_init_file(Lexer *lex, const char *path);

/* Free resources. */
void lexer_free(Lexer *lex);

/* Read the next token. Returns true if a valid token was read.
   When EOF is reached, token->kind is set to TK_EOF and false is returned.
   On error, token->kind is set to TK_ERROR and false is returned. */
bool lexer_next(Lexer *lex, Token *token);

/* Return the last error message. */
const char *lexer_get_error(const Lexer *lex);

/* Return a human-readable name for a token kind. */
const char *token_kind_name(TokenKind kind);

/* Return a human-readable name for a basic type kind. */
const char *lex_type_name(LexTypeKind kind);

#endif /* LEX_H */
