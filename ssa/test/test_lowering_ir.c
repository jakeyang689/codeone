#include "test.h"
#include "../lowering_ir.h"
#include "../lex.h"
#include "../ast.h"

/* Helper: parse SSA source and lower to LLVM IR string. */
static char *lower_str_target(const char *source, const char *triple,
                               char *err, size_t err_sz) {
    Lexer lex;
    lexer_init(&lex, source);
    Parser parser;
    ast_init(&parser, &lex);
    Program *prog = ast_parse(&parser);
    if (ast_has_error(&parser)) {
        snprintf(err, err_sz, "parse error: %s", ast_get_error(&parser));
        ast_free(&parser);
        lexer_free(&lex);
        return NULL;
    }
    ast_free(&parser);
    lexer_free(&lex);

    char *ir = lower_to_llvm_ir(prog, triple, err, err_sz);
    program_free(prog);
    return ir;
}

static char *lower_str(const char *source, char *err, size_t err_sz) {
    return lower_str_target(source, NULL, err, err_sz);
}

/* Helper: check if IR contains a substring. */
static bool ir_contains(const char *ir, const char *sub) {
    return strstr(ir, sub) != NULL;
}

/* ==================================================================
 * Tests
 * ================================================================== */

TEST(lower_empty) {
    char err[256] = "";
    char *ir = lower_str("", err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    /* An empty module should at least define the module */
    ASSERT(ir_contains(ir, "codeone_ssa_module"));
    lower_free_ir(ir);
}

TEST(lower_global_int) {
    char err[256] = "";
    char *ir = lower_str("global @x: i32 = 42", err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "@x"));
    ASSERT(ir_contains(ir, "42"));
    lower_free_ir(ir);
}

TEST(lower_global_no_init) {
    char err[256] = "";
    /* Note: parser needs fun or global. A bare global decl without function is fine. */
    char *ir = lower_str("global @buf: [i8, 256]", err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "@buf"));
    ASSERT(ir_contains(ir, "[256 x i8]"));
    lower_free_ir(ir);
}

TEST(lower_global_str) {
    char err[256] = "";
    char *ir = lower_str("const @msg: [i8, 6] = \"hello\"", err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "@msg"));
    ASSERT(ir_contains(ir, "hello"));  /* string content in IR */
    lower_free_ir(ir);
}

TEST(lower_enum_members) {
    char err[256] = "";
    char *ir = lower_str(
        "enum @Color: i32 { @Red = 1, @Green = 2 }\n"
        "fun @pick() -> i32 {\n"
        "  @entry:\n"
        "    %x = add i32 @Color.Red, @Color.Green\n"
        "    ret i32 %x\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    /* Members are internal read-only constants ... */
    ASSERT(ir_contains(ir, "internal constant i32 1"));
    ASSERT(ir_contains(ir, "@Color.Red"));
    ASSERT(ir_contains(ir, "internal constant i32 2"));
    /* ... and are folded into arithmetic as plain integers. */
    ASSERT(ir_contains(ir, "ret i32 3"));
    lower_free_ir(ir);
}

TEST(lower_enum_type_mismatch) {
    char err[256] = "";
    char *ir = lower_str(
        "enum @Color: i32 { @Red = 1 }\n"
        "fun @pick() -> i64 {\n"
        "  @entry:\n"
        "    %x = add i64 @Color.Red, 1\n"
        "    ret i64 %x\n"
        "}",
        err, sizeof(err));
    ASSERT_NULL(ir);
    ASSERT(strstr(err, "different type") != NULL);
}

TEST(lower_func_void) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @foo() -> void {\n"
        "  @entry:\n"
        "    ret void\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "@foo"));
    ASSERT(ir_contains(ir, "ret void"));
    lower_free_ir(ir);
}

TEST(lower_func_ret_i32) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @main() -> i32 {\n"
        "  @entry:\n"
        "    ret i32 42\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "i32"));
    ASSERT(ir_contains(ir, "42"));
    lower_free_ir(ir);
}

TEST(lower_func_params) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @add(a: i32, b: i64) -> i64 {\n"
        "  @entry:\n"
        "    ret i64 0\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "i32"));
    ASSERT(ir_contains(ir, "i64"));
    ASSERT(ir_contains(ir, "%a"));
    ASSERT(ir_contains(ir, "%b"));
    lower_free_ir(ir);
}

TEST(lower_insn_let) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    %p = let i32\n"
        "    ret i32 0\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "alloca"));
    lower_free_ir(ir);
}

TEST(lower_insn_add) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(x: i32) -> i32 {\n"
        "  @entry:\n"
        "    %1 = add i32 %x, 4\n"
        "    ret i32 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "add"));
    lower_free_ir(ir);
}

TEST(lower_insn_sub) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(x: i32) -> i32 {\n"
        "  @entry:\n"
        "    %1 = sub i32 %x, 3\n"
        "    ret i32 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "sub"));
    lower_free_ir(ir);
}

TEST(lower_insn_mul) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(x: i64) -> i64 {\n"
        "  @entry:\n"
        "    %1 = mul i64 %x, 3\n"
        "    ret i64 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "mul"));
    ASSERT(ir_contains(ir, "i64"));
    lower_free_ir(ir);
}

TEST(lower_insn_div) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(x: i32) -> i32 {\n"
        "  @entry:\n"
        "    %1 = div i32 %x, 3\n"
        "    ret i32 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "sdiv"));
    lower_free_ir(ir);
}

TEST(lower_insn_rem) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(x: i32) -> i32 {\n"
        "  @entry:\n"
        "    %1 = rem i32 %x, 3\n"
        "    ret i32 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "srem"));
    lower_free_ir(ir);
}

TEST(lower_insn_and) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(x: i32) -> i32 {\n"
        "  @entry:\n"
        "    %1 = and i32 %x, 1\n"
        "    ret i32 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "and"));
    lower_free_ir(ir);
}

TEST(lower_insn_or) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(x: i32) -> i32 {\n"
        "  @entry:\n"
        "    %1 = or i32 %x, 2\n"
        "    ret i32 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "or"));
    lower_free_ir(ir);
}

TEST(lower_insn_xor) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(x: i32) -> i32 {\n"
        "  @entry:\n"
        "    %1 = xor i32 %x, 3\n"
        "    ret i32 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "xor"));
    lower_free_ir(ir);
}

TEST(lower_insn_shl) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(x: i32) -> i32 {\n"
        "  @entry:\n"
        "    %1 = shl i32 %x, 4\n"
        "    ret i32 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "shl"));
    lower_free_ir(ir);
}


TEST(lower_insn_lshr) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(x: i32) -> i32 {\n"
        "  @entry:\n"
        "    %1 = lshr i32 %x, 2\n"
        "    ret i32 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "lshr"));
    lower_free_ir(ir);
}

TEST(lower_insn_ashr) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(x: i32) -> i32 {\n"
        "  @entry:\n"
        "    %1 = ashr i32 %x, 2\n"
        "    ret i32 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "ashr"));
    lower_free_ir(ir);
}

TEST(lower_insn_comparisons) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(x: i32) -> i1 {\n"
        "  @entry:\n"
        "    %1 = slt i32 %x, 5\n"
        "    ret i1 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "icmp slt"));
    lower_free_ir(ir);
}

TEST(lower_insn_eq) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(x: i32) -> i1 {\n"
        "  @entry:\n"
        "    %1 = eq i32 %x, 2\n"
        "    ret i1 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "icmp eq"));
    lower_free_ir(ir);
}

TEST(lower_insn_null) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(p: *i8) -> i1 {\n"
        "  @entry:\n"
        "    %1 = eq *i8 %p, null\n"
        "    ret i1 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "icmp eq"));
    ASSERT(ir_contains(ir, "null"));
    lower_free_ir(ir);
}

TEST(lower_insn_br) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(x: i32) -> i32 {\n"
        "  @entry:\n"
        "    %c = slt i32 %x, 0\n"
        "    br %c, @neg, @pos\n"
        "  @neg:\n"
        "    ret i32 -1\n"
        "  @pos:\n"
        "    ret i32 1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "br"));
    ASSERT(ir_contains(ir, "icmp slt"));
    lower_free_ir(ir);
}

TEST(lower_insn_jmp) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    jmp @end\n"
        "  @end:\n"
        "    ret i32 0\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "br label %end"));
    lower_free_ir(ir);
}

TEST(lower_insn_call) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @bar() -> i32 {\n"
        "  @entry:\n"
        "    ret i32 42\n"
        "}\n"
        "fun @foo() -> i32 {\n"
        "  @entry:\n"
        "    %v = call @bar()\n"
        "    ret i32 %v\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "call i32 @bar()"));
    lower_free_ir(ir);
}

TEST(lower_insn_asm) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @foo() -> i64 {\n"
        "  @entry:\n"
        "    %r = asm att i64, \"add $0, $1, $1\", \"=r,r\", i64 20\n"
        "    ret i64 %r\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "asm sideeffect \"add $0, $1, $1\", \"=r,r\""));
    ASSERT(ir_contains(ir, "i64 20"));
    lower_free_ir(ir);
}

TEST(lower_insn_asm_void_clobber) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @bar() -> void {\n"
        "  @entry:\n"
        "    asm intel void, \"mfence\", \"~{memory}\"\n"
        "    ret void\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "asm sideeffect inteldialect \"mfence\", \"~{memory}\"()"));
    lower_free_ir(ir);
}

TEST(lower_insn_asm_alignstack) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @bar() -> void {\n"
        "  @entry:\n"
        "    asm alignstack att void, \"nop\", \"\"\n"
        "    ret void\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "asm sideeffect alignstack \"nop\", \"\"()"));
    lower_free_ir(ir);
}

TEST(lower_insn_asm_multi_output) {
    char err[256] = "";
    char *ir = lower_str(
        "struct @Pair { @lo: i64, @hi: i64 }\n"
        "fun @foo() -> void {\n"
        "  @entry:\n"
        "    %slot = let @Pair\n"
        "    %s = asm att @Pair, \"mov $0, $1\\nadd $1, $1, #1\", "
        "\"=r,=r,r\", i64 20\n"
        "    store @Pair %s, *%slot\n"
        "    ret void\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "call %Pair asm sideeffect"));
    ASSERT(ir_contains(ir, "\"=r,=r,r\"(i64 20)"));
    lower_free_ir(ir);
}

TEST(lower_error_asm_struct_output_mismatch) {
    char err[256] = "";
    char *ir = lower_str(
        "struct @Pair { @lo: i64, @hi: i64 }\n"
        "fun @foo() -> void {\n"
        "  @entry:\n"
        "    %s = asm att @Pair, \"nop\", \"=r,r\", i64 1\n"
        "    ret void\n"
        "}",
        err, sizeof(err));
    ASSERT_NULL(ir);
    ASSERT(strstr(err, "2 field(s) but 1 output") != NULL);
}

TEST(lower_insn_phi) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(n: i32, m: i32) -> i32 {\n"
        "  @entry:\n"
        "    jmp @loop\n"
        "  @loop:\n"
        "    %x = phi [%n, @entry], [%m, @body]\n"
        "    ret i32 %x\n"
        "  @body:\n"
        "    jmp @loop\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "phi"));
    lower_free_ir(ir);
}

TEST(lower_insn_load_store) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    %p = let i32\n"
        "    store i32 42, *%p\n"
        "    %v = load i32, *%p\n"
        "    ret i32 %v\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "alloca"));
    ASSERT(ir_contains(ir, "store"));
    ASSERT(ir_contains(ir, "load"));
    lower_free_ir(ir);
}

TEST(lower_insn_zext) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(x: i32) -> i64 {\n"
        "  @entry:\n"
        "    %1 = zext i32 %x to i64\n"
        "    ret i64 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "zext"));
    lower_free_ir(ir);
}

TEST(lower_insn_sext) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(x: i32) -> i64 {\n"
        "  @entry:\n"
        "    %1 = sext i32 %x to i64\n"
        "    ret i64 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "sext"));
    lower_free_ir(ir);
}

TEST(lower_insn_trunc) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(x: i64) -> i32 {\n"
        "  @entry:\n"
        "    %1 = trunc i64 %x to i32\n"
        "    ret i32 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "trunc"));
    lower_free_ir(ir);
}












TEST(lower_unsigned_ops) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(a: i64, b: i64) -> i64 {\n"
        "  @entry:\n"
        "    %h = mulhi i64 %a, %b\n"
        "    %q = udiv i64 %h, %b\n"
        "    %r = urem i64 %q, %a\n"
        "    ret i64 %r\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "mul i128"));
    ASSERT(ir_contains(ir, "udiv"));
    ASSERT(ir_contains(ir, "urem"));
    lower_free_ir(ir);
}

TEST(lower_forward_call) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @main() -> i32 {\n"
        "  @entry:\n"
        "    %r = call @later(1)\n"
        "    ret i32 %r\n"
        "}\n"
        "fun @later(x: i32) -> i32 {\n"
        "  @entry:\n"
        "    ret i32 %x\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "define i32 @main"));
    ASSERT(ir_contains(ir, "define i32 @later"));
    lower_free_ir(ir);
}

TEST(lower_pointer_type) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @f(p: *i32) -> i32 {\n"
        "  @entry:\n"
        "    %v = load i32, *%p\n"
        "    ret i32 %v\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "ptr"));
    lower_free_ir(ir);
}

TEST(lower_complete_gcd) {
    char err[256] = "";
    char *ir = lower_str(
        "fun @gcd(a: i32, b: i32) -> i32 {\n"
        "  @loop:\n"
        "    ret i32 %a\n"
        "}",
        err, sizeof(err));
    ASSERT_NOT_NULL(ir);
    ASSERT(ir_contains(ir, "@gcd"));
    ASSERT(ir_contains(ir, "i32 %a"));
    lower_free_ir(ir);
}

/* ==================================================================
 * Error handling tests
 * ================================================================== */

TEST(lower_error_null_program) {
    char err[256] = "";
    char *ir = lower_to_llvm_ir(NULL, NULL, err, sizeof(err));
    ASSERT_NULL(ir);
    ASSERT(strlen(err) > 0);
}

TEST(lower_error_undefined_value) {
    char err[256] = "";
    /* Use an undefined %value */
    char *ir = lower_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    %1 = add i32 %undefined, 1\n"
        "    ret i32 %1\n"
        "}",
        err, sizeof(err));
    ASSERT_NULL(ir);
    ASSERT(strlen(err) > 0);
}

/* ==================================================================
 * Run all lowering IR tests
 * ================================================================== */

void run_lowering_tests(void) {
    printf("\n=== Lowering IR Tests ===\n\n");

    RUN_TEST(lower_empty);
    RUN_TEST(lower_global_int);
    RUN_TEST(lower_global_no_init);
    RUN_TEST(lower_global_str);
    RUN_TEST(lower_enum_members);
    RUN_TEST(lower_enum_type_mismatch);
    RUN_TEST(lower_func_void);
    RUN_TEST(lower_func_ret_i32);
    RUN_TEST(lower_func_params);
    RUN_TEST(lower_insn_let);
    RUN_TEST(lower_insn_add);
    RUN_TEST(lower_insn_sub);
    RUN_TEST(lower_insn_mul);
    RUN_TEST(lower_insn_div);
    RUN_TEST(lower_insn_rem);
    RUN_TEST(lower_insn_and);
    RUN_TEST(lower_insn_or);
    RUN_TEST(lower_insn_xor);
    RUN_TEST(lower_insn_shl);
    RUN_TEST(lower_insn_lshr);
    RUN_TEST(lower_insn_ashr);
    RUN_TEST(lower_insn_comparisons);
    RUN_TEST(lower_insn_eq);
    RUN_TEST(lower_insn_null);
    RUN_TEST(lower_insn_br);
    RUN_TEST(lower_insn_jmp);
    RUN_TEST(lower_insn_call);
    RUN_TEST(lower_insn_asm);
    RUN_TEST(lower_insn_asm_void_clobber);
    RUN_TEST(lower_insn_asm_alignstack);
    RUN_TEST(lower_insn_asm_multi_output);
    RUN_TEST(lower_error_asm_struct_output_mismatch);
    RUN_TEST(lower_insn_phi);
    RUN_TEST(lower_insn_load_store);
    RUN_TEST(lower_insn_zext);
    RUN_TEST(lower_insn_sext);
    RUN_TEST(lower_insn_trunc);
    RUN_TEST(lower_unsigned_ops);
    RUN_TEST(lower_forward_call);
    RUN_TEST(lower_pointer_type);
    RUN_TEST(lower_complete_gcd);

    /* Error cases */
    RUN_TEST(lower_error_null_program);
    RUN_TEST(lower_error_undefined_value);
}
