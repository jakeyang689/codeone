#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

extern int test_pass_count;
extern int test_fail_count;

extern int test_pass_count;
extern int test_fail_count;

#define TEST(name) static void test_##name(void)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        test_fail_count++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    long long _a = (long long)(a); \
    long long _b = (long long)(b); \
    if (_a != _b) { \
        fprintf(stderr, "  FAIL: %s == %s  (%lld != %lld) (%s:%d)\n", \
                #a, #b, _a, _b, __FILE__, __LINE__); \
        test_fail_count++; \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    const char *_a = (a); \
    const char *_b = (b); \
    if (strcmp(_a, _b) != 0) { \
        fprintf(stderr, "  FAIL: %s == %s  (\"%s\" != \"%s\") (%s:%d)\n", \
                #a, #b, _a, _b, __FILE__, __LINE__); \
        test_fail_count++; \
        return; \
    } \
} while(0)

#define ASSERT_NULL(a) do { \
    if ((a) != NULL) { \
        fprintf(stderr, "  FAIL: %s is not NULL (%s:%d)\n", #a, __FILE__, __LINE__); \
        test_fail_count++; \
        return; \
    } \
} while(0)

#define ASSERT_NOT_NULL(a) do { \
    if ((a) == NULL) { \
        fprintf(stderr, "  FAIL: %s is NULL (%s:%d)\n", #a, __FILE__, __LINE__); \
        test_fail_count++; \
        return; \
    } \
} while(0)

#define RUN_TEST(name) do { \
    printf("  TEST: %s\n", #name); \
    test_##name(); \
    test_pass_count++; \
} while(0)

#endif /* TEST_H */
