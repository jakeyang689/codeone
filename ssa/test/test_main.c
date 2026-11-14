#include "test.h"
#include <stdio.h>

int test_pass_count = 0;
int test_fail_count = 0;

/* Lexer tests */
void run_lex_tests(void);

/* AST / Parser tests */
void run_ast_tests(void);

/* Lowering IR tests */
void run_lowering_tests(void);

static int run_suite(const char *name, void (*fn)(void)) {
    test_pass_count = 0;
    test_fail_count = 0;
    fn();
    if (test_fail_count > 0) {
        printf("\n✗ %s: %d passed, %d failed\n", name, test_pass_count, test_fail_count);
        return -1;
    }
    printf("\n✓ All %d %s passed.\n", test_pass_count, name);
    return test_pass_count;
}

int main(void) {
    int total = 0;

    int r = run_suite("lexer tests", run_lex_tests);
    if (r < 0) return 1;
    total += r;

    r = run_suite("AST/parser tests", run_ast_tests);
    if (r < 0) return 1;
    total += r;

    r = run_suite("lowering IR tests", run_lowering_tests);
    if (r < 0) return 1;
    total += r;

    printf("\n=== All %d tests passed! ===\n", total);
    return 0;
}
