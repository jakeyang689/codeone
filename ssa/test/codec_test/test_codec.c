#include "test.h"
#include "../../codec.h"
#include "../../lowering_ir.h"
#include "../../lex.h"
#include "../../ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int test_pass_count = 0;
int test_fail_count = 0;

/* Helper: parse SSA source and return the Program. */
static Program *parse_ssa(const char *source) {
    Lexer lex;
    lexer_init(&lex, source);
    Parser parser;
    ast_init(&parser, &lex);
    Program *prog = ast_parse(&parser);
    if (ast_has_error(&parser)) {
        fprintf(stderr, "PARSE ERROR: %s\n", ast_get_error(&parser));
        ast_free(&parser);
        lexer_free(&lex);
        return NULL;
    }
    ast_free(&parser);
    lexer_free(&lex);
    return prog;
}

/* Helper: check if a file exists and has non-zero size. */
static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && st.st_size > 0;
}

/* Detect host target triple */
static const char *host_triple(void) {
    /* On macOS with Apple Silicon, use aarch64; on Intel Mac, use x86_64.
       We'll try the common macOS triples. */
    FILE *fp = popen("uname -m 2>/dev/null", "r");
    if (!fp) return "aarch64-apple-darwin";
    char buf[64];
    if (fgets(buf, sizeof(buf), fp)) {
        pclose(fp);
        if (strstr(buf, "x86_64") || strstr(buf, "amd64"))
            return "x86_64-apple-darwin";
    } else {
        pclose(fp);
    }
    return "aarch64-apple-darwin";
}

/* ==================================================================
 * Tests
 * ================================================================== */

TEST(codec_empty_function) {
    const char *output = "/tmp/codec_test_empty.o";
    remove(output);

    Program *p = parse_ssa(
        "fun @foo() -> void {\n"
        "  @entry:\n"
        "    ret void\n"
        "}"
    );
    ASSERT_NOT_NULL(p);

    char err[256] = "";
    bool ok = codec_compile_program(p, host_triple(), NULL, CODEC_O0, output, err, sizeof(err));
    ASSERT(ok);

    ASSERT(file_exists(output));
    /* Check the file is an object (starts with magic bytes) */
    FILE *fp = fopen(output, "rb");
    ASSERT_NOT_NULL(fp);
    unsigned char magic[4] = {0};
    fread(magic, 1, 4, fp);
    fclose(fp);
    /* Mach-O 64-bit magic: 0xFEEDFACF (stored as little-endian: cf fa ed fe) */
    unsigned int macho = (unsigned)magic[0] | ((unsigned)magic[1] << 8)
                       | ((unsigned)magic[2] << 16) | ((unsigned)magic[3] << 24);
    ASSERT(macho == 0xFEEDFACF || macho == 0xFEEDFACE);

    remove(output);
    program_free(p);
}

TEST(codec_ret_i32) {
    const char *output = "/tmp/codec_test_i32.o";
    remove(output);

    Program *p = parse_ssa(
        "fun @main() -> i32 {\n"
        "  @entry:\n"
        "    ret i32 42\n"
        "}"
    );
    ASSERT_NOT_NULL(p);

    char err[256] = "";
    bool ok = codec_compile_program(p, host_triple(), NULL, CODEC_O0, output, err, sizeof(err));
    ASSERT(ok);
    ASSERT(file_exists(output));
    remove(output);
    program_free(p);
}

TEST(codec_add) {
    const char *output = "/tmp/codec_test_add.o";
    remove(output);

    Program *p = parse_ssa(
        "fun @add(a: i32, b: i32) -> i32 {\n"
        "  @entry:\n"
        "    %r = add i32 %a, %b\n"
        "    ret i32 %r\n"
        "}"
    );
    ASSERT_NOT_NULL(p);

    char err[256] = "";
    bool ok = codec_compile_program(p, host_triple(), NULL, CODEC_O0, output, err, sizeof(err));
    ASSERT(ok);
    ASSERT(file_exists(output));
    remove(output);
    program_free(p);
}

TEST(codec_all_insns) {
    const char *output = "/tmp/codec_test_all.o";
    remove(output);

    Program *p = parse_ssa(
        "fun @calc(a: i32, b: i32) -> i32 {\n"
        "  @entry:\n"
        "    %1 = add i32 %a, %b\n"
        "    %2 = sub i32 %1, %b\n"
        "    %3 = mul i32 %2, 2\n"
        "    %4 = div i32 %3, 3\n"
        "    %5 = and i32 %4, 255\n"
        "    %6 = or i32 %5, 1\n"
        "    %7 = xor i32 %6, %a\n"
        "    ret i32 %7\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    char err[256] = "";
    bool ok = codec_compile_program(p, host_triple(), NULL, CODEC_O2, output, err, sizeof(err));
    ASSERT(ok);
    ASSERT(file_exists(output));
    remove(output);
    program_free(p);
}

TEST(codec_branch) {
    const char *output = "/tmp/codec_test_branch.o";
    remove(output);

    Program *p = parse_ssa(
        "fun @abs(x: i32) -> i32 {\n"
        "  @entry:\n"
        "    %c = slt i32 %x, 0\n"
        "    br %c, @neg, @pos\n"
        "  @neg:\n"
        "    %neg = sub i32 0, %x\n"
        "    ret i32 %neg\n"
        "  @pos:\n"
        "    ret i32 %x\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    char err[256] = "";
    bool ok = codec_compile_program(p, host_triple(), NULL, CODEC_O0, output, err, sizeof(err));
    ASSERT(ok);
    ASSERT(file_exists(output));
    remove(output);
    program_free(p);
}

TEST(codec_global_var) {
    const char *output = "/tmp/codec_test_global.o";
    remove(output);

    Program *p = parse_ssa(
        "global @x: i32 = 42\n"
        "fun @get_x() -> i32 {\n"
        "  @entry:\n"
        "    ret i32 0\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    char err[256] = "";
    bool ok = codec_compile_program(p, host_triple(), NULL, CODEC_O0, output, err, sizeof(err));
    ASSERT(ok);
    ASSERT(file_exists(output));
    remove(output);
    program_free(p);
}

TEST(codec_phi_loop) {
    const char *output = "/tmp/codec_test_phi.o";
    remove(output);

    Program *p = parse_ssa(
        "fun @sum(n: i32) -> i32 {\n"
        "  @entry:\n"
        "    jmp @loop\n"
        "  @loop:\n"
        "    %i = phi [0, @entry], [%next, @body]\n"
        "    %s = phi [0, @entry], [%sum, @body]\n"
        "    %cmp = slt i32 %i, %n\n"
        "    br %cmp, @body, @done\n"
        "  @body:\n"
        "    %next = add i32 %i, 1\n"
        "    %sum = add i32 %s, %next\n"
        "    jmp @loop\n"
        "  @done:\n"
        "    ret i32 %s\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    char err[256] = "";
    bool ok = codec_compile_program(p, host_triple(), NULL, CODEC_O0, output, err, sizeof(err));
    ASSERT(ok);
    ASSERT(file_exists(output));
    remove(output);
    program_free(p);
}

TEST(codec_ir_direct) {
    const char *output = "/tmp/codec_test_ir.o";
    remove(output);

    char err[256] = "";
    bool ok = codec_compile_ir(
        "define i32 @foo() {\n"
        "  ret i32 7\n"
        "}\n",
        host_triple(), NULL, CODEC_O0, output, err, sizeof(err));
    ASSERT(ok);
    ASSERT(file_exists(output));
    remove(output);
}

/* ==================================================================
 * Error handling tests
 * ================================================================== */

TEST(codec_error_null_ir) {
    char err[256] = "";
    bool ok = codec_compile_ir(NULL, host_triple(), NULL, CODEC_O0, "/tmp/x.o", err, sizeof(err));
    ASSERT(!ok);
    ASSERT(strlen(err) > 0);
}

TEST(codec_error_bad_triple) {
    char err[256] = "";
    bool ok = codec_compile_ir("define void @f() { ret void }",
                                "nonexistent-cpu-triple", NULL,
                                CODEC_O0, "/tmp/x.o", err, sizeof(err));
    ASSERT(!ok);
    ASSERT(strlen(err) > 0);
}

TEST(codec_error_bad_ir) {
    char err[256] = "";
    /* Use IR that is parseable as LLVM IR but semantically wrong */
    bool ok = codec_compile_ir("define void @f() { ret i32 0 }\n",
                                host_triple(), NULL, CODEC_O0, "/tmp/x.o", err, sizeof(err));
    /* This should fail because the return type doesn't match */
    ASSERT(!ok);
    ASSERT(strlen(err) > 0);
}

TEST(codec_error_ir_syntax) {
    char err[256] = "";
    bool ok = codec_compile_ir("this is not valid llvm ir at all !!!",
                                host_triple(), NULL, CODEC_O0, "/tmp/x.o", err, sizeof(err));
    ASSERT(!ok);
}

TEST(codec_error_null_program) {
    char err[256] = "";
    bool ok = codec_compile_program(NULL, host_triple(), NULL, CODEC_O0, "/tmp/x.o", err, sizeof(err));
    ASSERT(!ok);
    ASSERT(strlen(err) > 0);
}

/* ==================================================================
 * Run all codec tests
 * ================================================================== */

void run_codec_tests(void) {
    printf("\n=== Codec Tests ===\n\n");

    RUN_TEST(codec_empty_function);
    RUN_TEST(codec_ret_i32);
    RUN_TEST(codec_add);
    RUN_TEST(codec_all_insns);
    RUN_TEST(codec_branch);
    RUN_TEST(codec_global_var);
    RUN_TEST(codec_phi_loop);
    RUN_TEST(codec_ir_direct);

    RUN_TEST(codec_error_null_ir);
    RUN_TEST(codec_error_bad_triple);
    RUN_TEST(codec_error_bad_ir);
    RUN_TEST(codec_error_null_program);
}

int main(void) {
    run_codec_tests();
    if (test_fail_count > 0) {
        printf("\n✗ Codec tests: %d passed, %d failed\n",
               test_pass_count, test_fail_count);
        return 1;
    }
    printf("\n✓ All %d codec tests passed.\n", test_pass_count);
    return 0;
}
