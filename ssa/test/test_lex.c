#include "test.h"
#include "../lex.h"

/* ---- Special: empty input ---- */

TEST(lex_empty) {
    Lexer lex;
    lexer_init(&lex, "");
    Token tok;
    ASSERT(!lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_EOF);
    lexer_free(&lex);
}

/* ---- AES keywords and v128 ---- */

TEST(lex_kw_unsigned) {
    static const struct { const char *s; TokenKind k; int len; } cases[] = {
        {"mulhi",      TK_MULHI,      5},
        {"udiv",       TK_UDIV,       4},
        {"urem",       TK_UREM,       4},
    };
    for (int i = 0; i < 3; i++) {
        Lexer lex;
        Token tok;
        lexer_init(&lex, cases[i].s);
        ASSERT(lexer_next(&lex, &tok));
        ASSERT_EQ(tok.kind, cases[i].k);
        ASSERT_EQ(tok.length, cases[i].len);
        lexer_free(&lex);
    }
}

TEST(lex_kw_global) {
    Lexer lex;
    lexer_init(&lex, "global");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_GLOBAL);
    ASSERT_EQ(tok.length, 6);
    ASSERT(!lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_EOF);
    lexer_free(&lex);
}

TEST(lex_kw_fun) {
    Lexer lex;
    lexer_init(&lex, "fun");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_FUN);
    ASSERT_EQ(tok.length, 3);
    lexer_free(&lex);
}
TEST(lex_kw_extern) {
    Lexer lex;
    lexer_init(&lex, "extern");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_EXTERN);
    ASSERT_EQ(tok.length, 6);
    lexer_free(&lex);
}

TEST(lex_kw_enum) {
    Lexer lex;
    lexer_init(&lex, "enum");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_ENUM);
    ASSERT_EQ(tok.length, 4);
    lexer_free(&lex);
}

/* ---- Instruction keywords ---- */

TEST(lex_kw_let) {
    Lexer lex;
    lexer_init(&lex, "let"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_LET);
    lexer_free(&lex);
}
TEST(lex_kw_add) {
    Lexer lex;
    lexer_init(&lex, "add"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_ADD);
    lexer_free(&lex);
}
TEST(lex_kw_sub) {
    Lexer lex;
    lexer_init(&lex, "sub"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_SUB);
    lexer_free(&lex);
}
TEST(lex_kw_mul) {
    Lexer lex;
    lexer_init(&lex, "mul"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_MUL);
    lexer_free(&lex);
}
TEST(lex_kw_div) {
    Lexer lex;
    lexer_init(&lex, "div"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_DIV);
    lexer_free(&lex);
}
TEST(lex_kw_rem) {
    Lexer lex;
    lexer_init(&lex, "rem"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_REM);
    lexer_free(&lex);
}
TEST(lex_kw_and) {
    Lexer lex;
    lexer_init(&lex, "and"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_AND);
    lexer_free(&lex);
}
TEST(lex_kw_or) {
    Lexer lex;
    lexer_init(&lex, "or"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_OR);
    lexer_free(&lex);
}
TEST(lex_kw_xor) {
    Lexer lex;
    lexer_init(&lex, "xor"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_XOR);
    lexer_free(&lex);
}
TEST(lex_kw_shl) {
    Lexer lex;
    lexer_init(&lex, "shl"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_SHL);
    lexer_free(&lex);
}
TEST(lex_kw_lshr) {
    Lexer lex;
    lexer_init(&lex, "lshr"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_LSHR);
    lexer_free(&lex);
}
TEST(lex_kw_ashr) {
    Lexer lex;
    lexer_init(&lex, "ashr"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_ASHR);
    lexer_free(&lex);
}
TEST(lex_kw_eq) {
    Lexer lex;
    lexer_init(&lex, "eq"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_EQ);
    lexer_free(&lex);
}
TEST(lex_kw_ne) {
    Lexer lex;
    lexer_init(&lex, "ne"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_NE);
    lexer_free(&lex);
}
TEST(lex_kw_slt) {
    Lexer lex;
    lexer_init(&lex, "slt"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_SLT);
    lexer_free(&lex);
}
TEST(lex_kw_sle) {
    Lexer lex;
    lexer_init(&lex, "sle"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_SLE);
    lexer_free(&lex);
}
TEST(lex_kw_sgt) {
    Lexer lex;
    lexer_init(&lex, "sgt"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_SGT);
    lexer_free(&lex);
}
TEST(lex_kw_sge) {
    Lexer lex;
    lexer_init(&lex, "sge"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_SGE);
    lexer_free(&lex);
}
TEST(lex_kw_load) {
    Lexer lex;
    lexer_init(&lex, "load"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_LOAD);
    lexer_free(&lex);
}
TEST(lex_kw_store) {
    Lexer lex;
    lexer_init(&lex, "store"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_STORE);
    lexer_free(&lex);
}
TEST(lex_kw_gep) {
    Lexer lex;
    lexer_init(&lex, "gep"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_GEP);
    lexer_free(&lex);
}
TEST(lex_kw_jmp) {
    Lexer lex;
    lexer_init(&lex, "jmp"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_JMP);
    lexer_free(&lex);
}
TEST(lex_kw_br) {
    Lexer lex;
    lexer_init(&lex, "br"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_BR);
    lexer_free(&lex);
}
TEST(lex_kw_phi) {
    Lexer lex;
    lexer_init(&lex, "phi"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_PHI);
    lexer_free(&lex);
}
TEST(lex_kw_ret) {
    Lexer lex;
    lexer_init(&lex, "ret"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_RET);
    lexer_free(&lex);
}
TEST(lex_kw_call) {
    Lexer lex;
    lexer_init(&lex, "call"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_CALL);
    lexer_free(&lex);
}
TEST(lex_kw_zext) {
    Lexer lex;
    lexer_init(&lex, "zext"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_ZEXT);
    lexer_free(&lex);
}
TEST(lex_kw_sext) {
    Lexer lex;
    lexer_init(&lex, "sext"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_SEXT);
    lexer_free(&lex);
}
TEST(lex_kw_trunc) {
    Lexer lex;
    lexer_init(&lex, "trunc"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_TRUNC);
    lexer_free(&lex);
}
TEST(lex_kw_true) {
    Lexer lex;
    lexer_init(&lex, "true"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_TRUE);
    lexer_free(&lex);
}
TEST(lex_kw_false) {
    Lexer lex;
    lexer_init(&lex, "false"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_FALSE);
    lexer_free(&lex);
}
TEST(lex_kw_null) {
    Lexer lex;
    lexer_init(&lex, "null"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_NULL);
    ASSERT_EQ(tok.length, 4);
    lexer_free(&lex);
}
TEST(lex_kw_void) {
    Lexer lex;
    lexer_init(&lex, "void"); Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_VOID);
    lexer_free(&lex);
}

/* ---- Symbols: @name ---- */

TEST(lex_at_sym_simple) {
    Lexer lex;
    lexer_init(&lex, "@main");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_AT_SYM);
    ASSERT_EQ(tok.length, 4);
    ASSERT_EQ(tok.line, 1);
    ASSERT_EQ(tok.column, 2);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*s", tok.length, tok.start);
    ASSERT_STR_EQ(buf, "main");
    lexer_free(&lex);
}

TEST(lex_at_sym_underscore) {
    Lexer lex;
    lexer_init(&lex, "@loop_body");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_AT_SYM);
    ASSERT_EQ(tok.length, 9);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*s", tok.length, tok.start);
    ASSERT_STR_EQ(buf, "loop_body");
    lexer_free(&lex);
}

TEST(lex_at_sym_qualified) {
    Lexer lex;
    lexer_init(&lex, "@Color.Red");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_AT_SYM);
    ASSERT_EQ(tok.length, 9);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*s", tok.length, tok.start);
    ASSERT_STR_EQ(buf, "Color.Red");
    lexer_free(&lex);
}

TEST(lex_at_sym_trailing_dot) {
    Lexer lex;
    lexer_init(&lex, "@a.");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_AT_SYM);
    ASSERT_EQ(tok.length, 1);
    ASSERT(!lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_ERROR);
    lexer_free(&lex);
}

/* ---- Symbols: %name ---- */

TEST(lex_pct_sym_alpha) {
    Lexer lex;
    lexer_init(&lex, "%result");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_PCT_SYM);
    ASSERT_EQ(tok.length, 6);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*s", tok.length, tok.start);
    ASSERT_STR_EQ(buf, "result");
    lexer_free(&lex);
}

TEST(lex_pct_sym_digit) {
    Lexer lex;
    lexer_init(&lex, "%1");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_PCT_SYM);
    ASSERT_EQ(tok.length, 1);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*s", tok.length, tok.start);
    ASSERT_STR_EQ(buf, "1");
    lexer_free(&lex);
}

TEST(lex_pct_sym_alphanum) {
    Lexer lex;
    lexer_init(&lex, "%a123");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_PCT_SYM);
    ASSERT_EQ(tok.length, 4);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*s", tok.length, tok.start);
    ASSERT_STR_EQ(buf, "a123");
    lexer_free(&lex);
}

/* ---- Type keywords ---- */

TEST(lex_type_i1) {
    Lexer lex;
    lexer_init(&lex, "i1"); Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_TYPE);
    ASSERT_EQ(tok.lex_type, LEX_TYPE_I1);
    lexer_free(&lex);
}
TEST(lex_type_i8) {
    Lexer lex;
    lexer_init(&lex, "i8"); Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_TYPE);
    ASSERT_EQ(tok.lex_type, LEX_TYPE_I8);
    lexer_free(&lex);
}
TEST(lex_type_i32) {
    Lexer lex;
    lexer_init(&lex, "i32"); Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_TYPE);
    ASSERT_EQ(tok.lex_type, LEX_TYPE_I32);
    lexer_free(&lex);
}
TEST(lex_type_i64) {
    Lexer lex;
    lexer_init(&lex, "i64"); Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_TYPE);
    ASSERT_EQ(tok.lex_type, LEX_TYPE_I64);
    lexer_free(&lex);
}
TEST(lex_type_f32) {
    Lexer lex;
    lexer_init(&lex, "f32"); Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_TYPE);
    ASSERT_EQ(tok.lex_type, LEX_TYPE_F32);
    lexer_free(&lex);
}
TEST(lex_type_f64) {
    Lexer lex;
    lexer_init(&lex, "f64"); Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_TYPE);
    ASSERT_EQ(tok.lex_type, LEX_TYPE_F64);
    lexer_free(&lex);
}

/* ---- Literals: integers ---- */

TEST(lex_int_zero) {
    Lexer lex;
    lexer_init(&lex, "0"); Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_INT);
    ASSERT_EQ(tok.int_val, 0);
    lexer_free(&lex);
}
TEST(lex_int_positive) {
    Lexer lex;
    lexer_init(&lex, "42"); Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_INT);
    ASSERT_EQ(tok.int_val, 42);
    lexer_free(&lex);
}
TEST(lex_int_negative) {
    Lexer lex;
    lexer_init(&lex, "-1"); Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_INT);
    ASSERT_EQ(tok.int_val, -1);
    lexer_free(&lex);
}
TEST(lex_int_large) {
    Lexer lex;
    lexer_init(&lex, "65535"); Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_INT);
    ASSERT_EQ(tok.int_val, 65535);
    lexer_free(&lex);
}

/* ---- Literals: floats ---- */

TEST(lex_float_simple) {
    Lexer lex;
    lexer_init(&lex, "3.14"); Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_FLOAT);
    ASSERT(tok.float_val > 3.13 && tok.float_val < 3.15);
    lexer_free(&lex);
}
TEST(lex_float_negative) {
    Lexer lex;
    lexer_init(&lex, "-0.5"); Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_FLOAT);
    ASSERT_EQ(tok.float_val, -0.5);
    lexer_free(&lex);
}
TEST(lex_float_zero) {
    Lexer lex;
    lexer_init(&lex, "0.0"); Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_FLOAT);
    ASSERT_EQ(tok.float_val, 0.0);
    lexer_free(&lex);
}

/* ---- String literals ---- */

TEST(lex_str_simple) {
    Lexer lex;
    lexer_init(&lex, "\"hello\"");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_STR);
    ASSERT_EQ(tok.length, 5);
    ASSERT_EQ(memcmp(tok.start, "hello", 5), 0);
    lexer_free(&lex);
}

TEST(lex_str_escape) {
    Lexer lex;
    lexer_init(&lex, "\"a\\nb\"");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_STR);
    ASSERT_EQ(tok.length, 4);
    lexer_free(&lex);
}

TEST(lex_str_unterminated) {
    Lexer lex;
    lexer_init(&lex, "\"oops");
    Token tok;
    ASSERT(!lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_ERROR);
    lexer_free(&lex);
}

/* ---- Character literals ---- */

TEST(lex_char_simple) {
    Lexer lex;
    lexer_init(&lex, "'A'");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_INT);
    ASSERT_EQ(tok.int_val, 65);
    lexer_free(&lex);
}

TEST(lex_char_escape_n) {
    Lexer lex;
    lexer_init(&lex, "'\\n'");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_INT);
    ASSERT_EQ(tok.int_val, 10);
    lexer_free(&lex);
}

TEST(lex_char_hex) {
    Lexer lex;
    lexer_init(&lex, "'\\x41'");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_INT);
    ASSERT_EQ(tok.int_val, 65);
    lexer_free(&lex);
}

/* ---- Hex literals ---- */

TEST(lex_hex_upper) {
    Lexer lex;
    lexer_init(&lex, "0xFF");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_INT);
    ASSERT_EQ(tok.int_val, 255);
    lexer_free(&lex);
}

TEST(lex_hex_lower) {
    Lexer lex;
    lexer_init(&lex, "0x1a");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_INT);
    ASSERT_EQ(tok.int_val, 26);
    lexer_free(&lex);
}

TEST(lex_hex_zero) {
    Lexer lex;
    lexer_init(&lex, "0x0");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_INT);
    ASSERT_EQ(tok.int_val, 0);
    lexer_free(&lex);
}

/* ---- Punctuation ---- */

#define TEST_PUNCT(name, src, expected_kind, expected_len) \
    TEST(lex_punct_##name) { \
        Lexer lex; \
        lexer_init(&lex, src); \
        Token tok; \
        ASSERT(lexer_next(&lex, &tok)); \
        ASSERT_EQ(tok.kind, expected_kind); \
        ASSERT_EQ(tok.length, expected_len); \
        lexer_free(&lex); \
    }

TEST_PUNCT(lbrace,     "{",  TK_LBRACE, 1)
TEST_PUNCT(rbrace,     "}",  TK_RBRACE, 1)
TEST_PUNCT(lparen,     "(",  TK_LPAREN, 1)
TEST_PUNCT(rparen,     ")",  TK_RPAREN, 1)
TEST_PUNCT(lbrack,     "[",  TK_LBRACK, 1)
TEST_PUNCT(rbrack,     "]",  TK_RBRACK, 1)
TEST_PUNCT(comma,      ",",  TK_COMMA,  1)
TEST_PUNCT(colon,      ":",  TK_COLON,  1)
TEST_PUNCT(assign,     "=",  TK_ASSIGN, 1)
TEST_PUNCT(arrow,      "->", TK_ARROW,  2)
TEST_PUNCT(star,       "*",  TK_STAR,   1)
TEST_PUNCT(ellipsis,   "...", TK_ELLIPSIS, 3)

/* ---- Whitespace and comments ---- */

TEST(lex_whitespace_spaces) {
    Lexer lex;
    lexer_init(&lex, "  add  sub  ");
    Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_ADD);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_SUB);
    ASSERT(!lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_EOF);
    lexer_free(&lex);
}

TEST(lex_whitespace_newlines) {
    Lexer lex;
    lexer_init(&lex, "add\nsub\nmul");
    Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_ADD);
    ASSERT_EQ(tok.line, 1);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_SUB);
    ASSERT_EQ(tok.line, 2);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_MUL);
    ASSERT_EQ(tok.line, 3);
    lexer_free(&lex);
}

TEST(lex_comment_single) {
    Lexer lex;
    lexer_init(&lex, "add // this is a comment\nsub");
    Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_ADD);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_SUB);
    ASSERT(!lexer_next(&lex, &tok));
    lexer_free(&lex);
}

TEST(lex_comment_at_eof) {
    Lexer lex;
    lexer_init(&lex, "// just a comment");
    Token tok;
    ASSERT(!lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_EOF);
    lexer_free(&lex);
}

/* ---- Mixed program fragment ---- */

TEST(lex_global_decl) {
    Lexer lex;
    lexer_init(&lex, "global @count: i32 = 0");
    Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_GLOBAL);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_AT_SYM);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_COLON);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_TYPE);
    ASSERT_EQ(tok.lex_type, LEX_TYPE_I32);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_ASSIGN);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_INT);
    ASSERT_EQ(tok.int_val, 0);
    ASSERT(!lexer_next(&lex, &tok));
    lexer_free(&lex);
}

TEST(lex_func_decl) {
    Lexer lex;
    lexer_init(&lex, "fun @add(a: i32, b: i32) -> i32 { @entry: ret i32 %a }");
    Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_FUN);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_AT_SYM);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_LPAREN);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_IDENT); /* 'a' */
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_COLON);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_TYPE); /* i32 */
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_COMMA);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_IDENT); /* 'b' */
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_COLON);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_TYPE); /* i32 */
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_RPAREN);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_ARROW);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_TYPE); /* i32 */
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_LBRACE);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_AT_SYM); /* entry */
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_COLON);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_RET);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_TYPE); /* i32 */
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_PCT_SYM); /* %a */
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_RBRACE);
    ASSERT(!lexer_next(&lex, &tok));
    lexer_free(&lex);
}

/* ---- Token kind names ---- */

TEST(lex_token_kind_name) {
    ASSERT_STR_EQ(token_kind_name(TK_ADD), "add");
    ASSERT_STR_EQ(token_kind_name(TK_EOF), "EOF");
    ASSERT_STR_EQ(token_kind_name(TK_ARROW), "->");
    ASSERT_STR_EQ(token_kind_name(TK_LBRACE), "{");
    ASSERT_STR_EQ(token_kind_name(TK_AT_SYM), "@name");
}

/* ---- Type names ---- */

TEST(lex_type_name) {
    ASSERT_STR_EQ(lex_type_name(LEX_TYPE_I32), "i32");
    ASSERT_STR_EQ(lex_type_name(LEX_TYPE_F64), "f64");
    ASSERT_STR_EQ(lex_type_name(LEX_TYPE_VOID), "void");
}

/* ---- File init ---- */

TEST(lex_init_file) {
    /* Write a temp .ssa file */
    const char *path = "/tmp/_test_lex.ssa";
    FILE *fp = fopen(path, "w");
    ASSERT_NOT_NULL(fp);
    fprintf(fp, "global @x: i32 = 42\n");
    fclose(fp);

    Lexer lex;
    ASSERT(lexer_init_file(&lex, path));
    Token tok;
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_GLOBAL);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_AT_SYM);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_COLON);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_TYPE);
    ASSERT_EQ(tok.lex_type, LEX_TYPE_I32);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_ASSIGN);
    ASSERT(lexer_next(&lex, &tok)); ASSERT_EQ(tok.kind, TK_INT);
    ASSERT_EQ(tok.int_val, 42);
    ASSERT(!lexer_next(&lex, &tok));
    lexer_free(&lex);

    remove(path);
}

TEST(lex_init_file_not_found) {
    Lexer lex;
    ASSERT(!lexer_init_file(&lex, "/tmp/_nonexistent_ssa_file_.ssa"));
    ASSERT(lex.has_error);
    lexer_free(&lex);
}

/* ---- Error handling ---- */

TEST(lex_error_bad_at) {
    Lexer lex;
    lexer_init(&lex, "@");
    Token tok;
    ASSERT(!lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_ERROR);
    lexer_free(&lex);
}

TEST(lex_error_bad_pct) {
    Lexer lex;
    lexer_init(&lex, "%");
    Token tok;
    ASSERT(!lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_ERROR);
    lexer_free(&lex);
}

TEST(lex_error_unexpected_char) {
    Lexer lex;
    lexer_init(&lex, "#");
    Token tok;
    ASSERT(!lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_ERROR);
    lexer_free(&lex);
}

TEST(lex_ident_param) {
    Lexer lex;
    lexer_init(&lex, "foobar");
    Token tok;
    ASSERT(lexer_next(&lex, &tok));
    ASSERT_EQ(tok.kind, TK_IDENT);
    ASSERT_EQ(tok.length, 6);
    lexer_free(&lex);
}

/* ---- Run all lexer tests ---- */

void run_lex_tests(void) {
    printf("\n=== Lexer Tests ===\n\n");

    /* Special */
    RUN_TEST(lex_empty);

    /* Structural keywords */
    RUN_TEST(lex_kw_unsigned);
    RUN_TEST(lex_kw_global);
    RUN_TEST(lex_kw_fun);
    RUN_TEST(lex_kw_extern);

    /* Instruction keywords */
    RUN_TEST(lex_kw_let);
    RUN_TEST(lex_kw_add);
    RUN_TEST(lex_kw_sub);
    RUN_TEST(lex_kw_mul);
    RUN_TEST(lex_kw_div);
    RUN_TEST(lex_kw_rem);
    RUN_TEST(lex_kw_and);
    RUN_TEST(lex_kw_or);
    RUN_TEST(lex_kw_xor);
    RUN_TEST(lex_kw_shl);
    RUN_TEST(lex_kw_lshr);
    RUN_TEST(lex_kw_ashr);
    RUN_TEST(lex_kw_eq);
    RUN_TEST(lex_kw_ne);
    RUN_TEST(lex_kw_slt);
    RUN_TEST(lex_kw_sle);
    RUN_TEST(lex_kw_sgt);
    RUN_TEST(lex_kw_sge);
    RUN_TEST(lex_kw_load);
    RUN_TEST(lex_kw_store);
    RUN_TEST(lex_kw_gep);
    RUN_TEST(lex_kw_jmp);
    RUN_TEST(lex_kw_br);
    RUN_TEST(lex_kw_phi);
    RUN_TEST(lex_kw_ret);
    RUN_TEST(lex_kw_call);
    RUN_TEST(lex_kw_zext);
    RUN_TEST(lex_kw_sext);
    RUN_TEST(lex_kw_trunc);
    RUN_TEST(lex_kw_true);
    RUN_TEST(lex_kw_false);
    RUN_TEST(lex_kw_null);
    RUN_TEST(lex_kw_void);

    /* @ symbols */
    RUN_TEST(lex_at_sym_simple);
    RUN_TEST(lex_at_sym_underscore);
    RUN_TEST(lex_at_sym_qualified);
    RUN_TEST(lex_at_sym_trailing_dot);

    /* % symbols */
    RUN_TEST(lex_pct_sym_alpha);
    RUN_TEST(lex_pct_sym_digit);
    RUN_TEST(lex_pct_sym_alphanum);

    /* Types */
    RUN_TEST(lex_type_i1);
    RUN_TEST(lex_type_i8);
    RUN_TEST(lex_type_i32);
    RUN_TEST(lex_type_i64);
    RUN_TEST(lex_type_f32);
    RUN_TEST(lex_type_f64);

    /* Integers */
    RUN_TEST(lex_int_zero);
    RUN_TEST(lex_int_positive);
    RUN_TEST(lex_int_negative);
    RUN_TEST(lex_int_large);

    /* Floats */
    RUN_TEST(lex_float_simple);
    RUN_TEST(lex_float_negative);
    RUN_TEST(lex_float_zero);

    /* Strings */
    RUN_TEST(lex_str_simple);
    RUN_TEST(lex_str_escape);
    RUN_TEST(lex_str_unterminated);

    /* Char literals */
    RUN_TEST(lex_char_simple);
    RUN_TEST(lex_char_escape_n);
    RUN_TEST(lex_char_hex);

    /* Hex literals */
    RUN_TEST(lex_hex_upper);
    RUN_TEST(lex_hex_lower);
    RUN_TEST(lex_hex_zero);

    /* Punctuation */
    RUN_TEST(lex_punct_lbrace);
    RUN_TEST(lex_punct_rbrace);
    RUN_TEST(lex_punct_lparen);
    RUN_TEST(lex_punct_rparen);
    RUN_TEST(lex_punct_lbrack);
    RUN_TEST(lex_punct_rbrack);
    RUN_TEST(lex_punct_comma);
    RUN_TEST(lex_punct_colon);
    RUN_TEST(lex_punct_assign);
    RUN_TEST(lex_punct_arrow);
    RUN_TEST(lex_punct_star);
    RUN_TEST(lex_punct_ellipsis);

    /* Whitespace / comments */
    RUN_TEST(lex_whitespace_spaces);
    RUN_TEST(lex_whitespace_newlines);
    RUN_TEST(lex_comment_single);
    RUN_TEST(lex_comment_at_eof);

    /* Program fragments */
    RUN_TEST(lex_global_decl);
    RUN_TEST(lex_func_decl);

    /* Names */
    RUN_TEST(lex_token_kind_name);
    RUN_TEST(lex_type_name);

    /* File I/O */
    RUN_TEST(lex_init_file);
    RUN_TEST(lex_init_file_not_found);

    /* Identifiers */
    RUN_TEST(lex_ident_param);

    /* Errors */
    RUN_TEST(lex_error_bad_at);
    RUN_TEST(lex_error_bad_pct);
    RUN_TEST(lex_error_unexpected_char);
}
