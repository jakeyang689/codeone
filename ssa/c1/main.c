#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../lex.h"
#include "../ast.h"
#include "../lowering_ir.h"
#include "../codec.h"
#include "../cmap.h"
#include "../hir/backend.h"
#include <llvm-c/Core.h>

/* ==================================================================
 * Host detection
 * ================================================================== */

static const char *detect_host_arch(void) {
    FILE *fp = popen("uname -m 2>/dev/null", "r");
    static char buf[32];
    if (fp && fgets(buf, sizeof(buf), fp)) {
        pclose(fp);
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
        return buf;
    }
    if (fp) pclose(fp);
    return "aarch64";
}

static const char *detect_host_os(void) {
    FILE *fp = popen("uname -s 2>/dev/null", "r");
    static char buf[32];
    if (fp && fgets(buf, sizeof(buf), fp)) {
        pclose(fp);
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
        return buf;
    }
    if (fp) pclose(fp);
    return "Darwin";
}

/* ==================================================================
 * Triple construction
 * ================================================================== */

static int build_triple(const char *arch, const char *os,
                         char *triple, size_t triple_size) {
    const char *arch_norm = arch;
    const char *os_part = os;

    if (strcmp(arch, "x86_64") == 0 || strcmp(arch, "amd64") == 0)
        arch_norm = "x86_64";
    else if (strcmp(arch, "arm64") == 0 || strcmp(arch, "aarch64") == 0)
        arch_norm = "aarch64";

    if (strcasecmp(os, "Darwin") == 0 || strcasecmp(os, "apple-darwin") == 0 ||
        strcasecmp(os, "macOS") == 0 || strcasecmp(os, "macos") == 0)
#ifndef MACOS_DEPLOYMENT_TARGET
#define MACOS_DEPLOYMENT_TARGET "15.0"
#endif
        os_part = "apple-macosx" MACOS_DEPLOYMENT_TARGET;
    else if (strcasecmp(os, "Linux") == 0 || strcasecmp(os, "linux") == 0 ||
             strcasecmp(os, "unknown-linux-gnu") == 0)
        os_part = "unknown-linux-gnu";

    int n = snprintf(triple, triple_size, "%s-%s", arch_norm, os_part);
    return (size_t)n < triple_size ? 0 : -1;
}

/* ==================================================================
 * Link step: invoke system compiler-as-linker or lld
 * ================================================================== */

/* Build path to lld binary next to the c1 executable. */
static void lld_path(const char *exe_dir, const char *lld_name,
                      char *out, size_t out_sz) {
    if (exe_dir)
        snprintf(out, out_sz, "%s/%s", exe_dir, lld_name);
    else
        snprintf(out, out_sz, "%s", lld_name);
}

/* Get directory containing the c1 executable, or NULL if not determinable */
static const char *exe_dir_from(const char *argv0) {
    static char buf[1024];
    if (strchr(argv0, '/')) {
        snprintf(buf, sizeof(buf), "%s", argv0);
        char *p = strrchr(buf, '/');
        if (p) *p = '\0';
        return buf;
    }
    return NULL;
}

static int link_object(const char **obj_paths, int obj_count,
                        const char *exe_path,
                        const char *arch, const char *triple,
                        const char *linker,
                        const char **link_flags, int link_flag_count,
                        const char *exe_dir) {
    char cmd[4096];

    /* Build space-separated list of object files */
    char objs[4096] = "";
    int opos = 0;
    for (int i = 0; i < obj_count; i++)
        opos += snprintf(objs + opos, sizeof(objs) - (size_t)opos, "%s%s",
                         i > 0 ? " " : "", obj_paths[i]);

    if (!linker) {
        linker = C1_CC;
    }

    if (strcmp(linker, "lld") == 0) {
        char lld_buf[1024];
        if (strstr(triple, "macosx")) {
            const char *ver = strstr(triple, "macosx") + 6;
            lld_path(exe_dir, "ld64.lld", lld_buf, sizeof(lld_buf));
            snprintf(cmd, sizeof(cmd),
                "%s -o %s %s -arch %s -platform_version macos %s %s -lSystem "
                "-syslibroot \"$(xcrun --show-sdk-path 2>/dev/null || "
                "echo /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk)\"",
                lld_buf, exe_path, objs,
                (strcmp(arch, "aarch64") == 0) ? "arm64" : arch,
                ver, ver);
        } else if (strstr(triple, "linux-gnu")) {
            lld_path(exe_dir, "ld.lld", lld_buf, sizeof(lld_buf));
            snprintf(cmd, sizeof(cmd), "%s -o %s %s -lc", lld_buf, exe_path, objs);
        } else if (strstr(triple, "windows")) {
            lld_path(exe_dir, "lld-link", lld_buf, sizeof(lld_buf));
            snprintf(cmd, sizeof(cmd), "%s /out:%s %s", lld_buf, exe_path, objs);
        } else {
            fprintf(stderr, "error: --ld lld not supported for target: %s\n", triple);
            return 1;
        }
    } else {
        snprintf(cmd, sizeof(cmd), "%s -o %s %s", linker, exe_path, objs);
        if (strstr(triple, "macosx")) {
            const char *ver = strstr(triple, "macosx") + 6;
            const char *host_arch = detect_host_arch();
            size_t len = strlen(cmd);
            snprintf(cmd + len, sizeof(cmd) - len, " -mmacosx-version-min=%s", ver);
            if (strcmp(arch, host_arch) != 0) {
                len = strlen(cmd);
                snprintf(cmd + len, sizeof(cmd) - len, " -arch %s", arch);
            }
        }
        for (int fi = 0; fi < link_flag_count; fi++) {
            size_t flen = strlen(cmd);
            snprintf(cmd + flen, sizeof(cmd) - flen, " %s", link_flags[fi]);
        }
    }

    fprintf(stdout, "Linking:   %s\n", cmd);
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "link error: linker returned %d\n", ret);
        if (linker && strcmp(linker, "lld") == 0)
            fprintf(stderr, "  Check that lld is in the c1 executable directory\n");
    }
    return ret;
}

/* ==================================================================
 * Helpers
 * ================================================================== */

static const char *file_basename(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/') base = p + 1;
    return base;
}

/* ==================================================================
 * Test framework helpers (c1 test)
 * ================================================================== */

/* Scan source for functions named @test_*.
 * Returns count and fills **names (caller must free). */
static int scan_test_functions(const char *source, const char ***names_out) {
    Lexer lex;
    lexer_init(&lex, source);
    Parser parser;
    ast_init(&parser, &lex);
    Program *prog = ast_parse(&parser);
    if (!prog || ast_has_error(&parser)) {
        if (prog) program_free(prog);
        ast_free(&parser);
        lexer_free(&lex);
        *names_out = NULL;
        return 0;
    }

    int count = 0;
    for (int i = 0; i < prog->func_count; i++)
        if (strncmp(prog->functions[i].name, "test_", 5) == 0)
            count++;

    const char **names = calloc((size_t)count, sizeof(char *));
    int idx = 0;
    for (int i = 0; i < prog->func_count; i++)
        if (strncmp(prog->functions[i].name, "test_", 5) == 0)
            names[idx++] = strdup(prog->functions[i].name);

    program_free(prog);
    ast_free(&parser);
    lexer_free(&lex);
    *names_out = names;
    return count;
}

/* Generate SSA harness that calls each test function.
 * Returns newly allocated string. */
static char *generate_test_harness(const char **test_names, int test_count) {
    /* Estimate size: ~500 bytes overhead + ~150 per test */
    size_t cap = 4096 + (size_t)test_count * 256;
    char *buf = malloc(cap);
    char *p = buf;
    char *end = buf + cap;
    int n;

    n = snprintf(p, (size_t)(end - p),
        "extern @putchar(c: i32) -> i32\n");
    p += n;

    for (int i = 0; i < test_count; i++) {
        n = snprintf(p, (size_t)(end - p),
            "extern @%s() -> i32\n", test_names[i]);
        p += n;
    }
    p += snprintf(p, (size_t)(end - p), "\n");

    n = snprintf(p, (size_t)(end - p),
        "fun @main() -> i32 {\n"
        "  @entry:\n"
        "    %%fc = let i32\n"
        "    store i32 0, *%%fc\n"
        "    jmp @t0\n\n");
    p += n;

    for (int i = 0; i < test_count; i++) {
        char nxt[32];
        if (i < test_count - 1) snprintf(nxt, sizeof(nxt), "t%d", i + 1);
        else snprintf(nxt, sizeof(nxt), "done");

        n = snprintf(p, (size_t)(end - p),
            "  @t%d:\n"
            "    %%r%d = call @%s()\n"
            "    %%p%d = eq i32 %%r%d, 0\n"
            "    br %%p%d, @p%d, @f%d\n\n"
            "  @p%d:\n"
            "    call @putchar(46)\n"
            "    jmp @%s\n\n"
            "  @f%d:\n"
            "    call @putchar(70)\n"
            "    %%n%d = load i32, *%%fc\n"
            "    %%n%di = add i32 %%n%d, 1\n"
            "    store i32 %%n%di, *%%fc\n"
            "    jmp @%s\n\n",
            i, i, test_names[i], i, i, i, i, i,
            i, nxt,
            i,
            i, i,
            i, i,
            nxt);
        p += n;
        if (p >= end) break;
    }

    n = snprintf(p, (size_t)(end - p),
        "  @done:\n"
        "    call @putchar(10)\n"
        "    %%total = load i32, *%%fc\n"
        "    ret i32 %%total\n"
        "}\n");
    p += n;

    return buf;
}

/* Run a compiled test binary, capture output, display formatted results. */
static int run_tests(const char *exe_path, const char **test_names, int test_count) {
    char output_path[256];
    snprintf(output_path, sizeof(output_path), "___test_output_%d.txt", getpid());

    /* Prefix with ./ if the path is relative (system() uses PATH, not CWD) */
    char exe_cmd[4096];
    if (exe_path[0] != '/' && exe_path[0] != '.')
        snprintf(exe_cmd, sizeof(exe_cmd), "./%s", exe_path);
    else
        snprintf(exe_cmd, sizeof(exe_cmd), "%s", exe_path);

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "\"%s\" > \"%s\" 2>&1", exe_cmd, output_path);
    int ret = system(cmd);

    FILE *fp = fopen(output_path, "r");
    if (!fp) {
        fprintf(stderr, "error: cannot read test output\n");
        remove(output_path);
        return 1;
    }

    printf("\nRunning %d tests:\n", test_count);
    int passed = 0, failed = 0;
    int ch;
    for (int i = 0; i < test_count; i++) {
        /* Skip newlines/r between test chars */
        do { ch = fgetc(fp); } while (ch == '\n' || ch == '\r');
        if (ch == EOF) {
            printf("  @%s ... CRASHED\n", test_names[i]);
            failed++;
            continue;
        }
        if (ch == '.') {
            printf("  @%s ... ok\n", test_names[i]);
            passed++;
        } else {
            printf("  @%s ... FAILED\n", test_names[i]);
            failed++;
        }
    }
    fclose(fp);
    remove(output_path);

    printf("\n  %d passed, %d failed", passed, failed);
    if (test_count > passed + failed)
        printf(", %d incomplete", test_count - passed - failed);
    printf("\n");

    return WEXITSTATUS(ret);
}

/* ==================================================================
 * Usage
 * ================================================================== */

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] <input.bx> [<input2.bx> ...]\n"
        "\n"
        "Options:\n"
        "  --cpu <arch>     Target CPU architecture\n"
        "                   (default: host: %s)\n"
        "  --os <os>        Target operating system\n"
        "                   (default: host: %s)\n"
        "  --mattr <feat>   Target CPU features, e.g. '+aes,+sha2,+pmull'\n"
        "  -o <file>        Output path (default: a.out or <input>.a with --static)\n"
        "  -c               Compile to object file only, do not link\n"
        "  --static         Build a static library (.a) instead of executable\n"
        "  -O <0|1|2|3>     Optimization level (default: 0)\n"
        "  --ld <linker>    Linker command (default: cc; use 'lld' for LLVM lld)\n"
        "  -l <lib>         Link against a library (e.g. -l platform)\n"
        "  -L <dir>         Add library search path\n"
        "  --link-flag <f>  Pass a flag to the linker verbatim\n"
        "                   (e.g. --link-flag \"-framework Cocoa\")\n"
        "  -I <dir>         Add .bx file search path\n"
        "  --cmap           Emit a .cmap pseudo-C view per input (no compile)\n"
        "  --backend <b>    Code generation backend: llvm (default) or mlir\n"
        "  --emit-mlir      Emit a .mlir c1 dialect view per input (no compile)\n"
        "  --version        Show version info\n"
        "  --help           Show this help\n"
        "\n"
        "Input:\n"
        "  Multiple .bx files are compiled together and linked into one program.\n"
        "  Each file is parsed on its own, so calling a function or using a\n"
        "  struct from another file needs an extern / local declaration.\n"
        "\n"
        "Output:\n"
        "  Generates a .o object file, then links into an executable.\n"
        "  The intermediate .o file is removed after linking.\n"
        "\n"
        "Examples:\n"
        "  c1 foo.bx\n"
        "  c1 math.bx main.bx -O2\n"
        "  c1 --cpu x86_64 --os Linux foo.bx\n"
        "  c1 -I ~/.codeone/toolchain/boxes foo.bx\n"
        "  c1 --ld gcc foo.bx\n"
        "  c1 --ld lld foo.bx\n"
        "  c1 -O2 -o my_prog a.bx b.bx c.bx\n"
        "\n",
        prog, detect_host_arch(), detect_host_os());
}

/* Open a .bx file, searching -I directories if direct path fails. */
static FILE *open_with_include(const char *path, const char **inc_dirs, int inc_count) {
    FILE *fp = fopen(path, "rb");
    if (fp) return fp;
    for (int i = 0; i < inc_count; i++) {
        size_t plen = strlen(inc_dirs[i]) + 1 + strlen(path) + 1;
        char *buf = (char *)malloc(plen);
        if (!buf) continue;
        snprintf(buf, plen, "%s/%s", inc_dirs[i], path);
        fp = fopen(buf, "rb");
        free(buf);
        if (fp) return fp;
    }
    return NULL;
}

/* Render one .bx file to a sibling .cmap file (same directory, .cmap
   extension). No compilation happens. Returns false on error. */
static bool emit_cmap_file(const char *input, const char **inc_dirs, int inc_count) {
    FILE *fp = open_with_include(input, inc_dirs, inc_count);
    if (!fp) {
        fprintf(stderr, "error: cannot open: %s\n", input);
        return false;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *source = (char *)malloc((size_t)sz + 1);
    if (!source) { fclose(fp); return false; }
    fread(source, 1, (size_t)sz, fp);
    fclose(fp);
    source[sz] = '\0';

    Lexer lex;
    lexer_init(&lex, source);
    Parser parser;
    ast_init(&parser, &lex);
    Program *prog = ast_parse(&parser);

    bool ok = false;

    if (ast_has_error(&parser)) {
        fprintf(stderr, "parse error: %s: %s\n", input, ast_get_error(&parser));
    } else {
        char out_path[1024];
        const char *dot = strrchr(input, '.');
        if (dot && strcmp(dot, ".bx") == 0)
            snprintf(out_path, sizeof(out_path), "%.*s.cmap", (int)(dot - input), input);
        else
            snprintf(out_path, sizeof(out_path), "%s.cmap", input);

        FILE *out = fopen(out_path, "w");
        if (!out) {
            fprintf(stderr, "error: cannot write: %s\n", out_path);
        } else {
            char err[256] = "";
            ok = cmap_emit_program(prog, out, err, sizeof(err));
            fclose(out);
            if (ok) fprintf(stdout, "cmap: %s -> %s\n", input, out_path);
            else    fprintf(stderr, "error: %s: %s\n", input, err);
        }
    }

    if (prog) program_free(prog);
    ast_free(&parser);
    lexer_free(&lex);
    free(source);
    return ok;
}

/* Render one .bx file to its c1 dialect module (sibling .mlir file).
   No compilation happens. Returns false on error. */
static bool emit_mlir_file(const char *input, const char **inc_dirs, int inc_count) {
    FILE *fp = open_with_include(input, inc_dirs, inc_count);
    if (!fp) {
        fprintf(stderr, "error: cannot open: %s\n", input);
        return false;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *source = (char *)malloc((size_t)sz + 1);
    if (!source) { fclose(fp); return false; }
    fread(source, 1, (size_t)sz, fp);
    fclose(fp);
    source[sz] = '\0';

    Lexer lex;
    lexer_init(&lex, source);
    lex.filename = input;
    Parser parser;
    ast_init(&parser, &lex);
    Program *prog = ast_parse(&parser);

    bool ok = false;

    if (ast_has_error(&parser)) {
        fprintf(stderr, "parse error: %s: %s\n", input, ast_get_error(&parser));
    } else {
        char out_path[1024];
        const char *dot = strrchr(input, '.');
        if (dot && strcmp(dot, ".bx") == 0)
            snprintf(out_path, sizeof(out_path), "%.*s.mlir", (int)(dot - input), input);
        else
            snprintf(out_path, sizeof(out_path), "%s.mlir", input);

        char err[512] = "";
        ok = c1_hir_emit_text(prog, out_path, err, sizeof(err));
        if (ok) fprintf(stdout, "mlir: %s -> %s\n", input, out_path);
        else    fprintf(stderr, "error: %s: %s\n", input, err);
    }

    if (prog) program_free(prog);
    ast_free(&parser);
    lexer_free(&lex);
    free(source);
    return ok;
}

/* ==================================================================
 * Main
 * ================================================================== */

#define MAX_INPUT_FILES 64

int main(int argc, char **argv) {
    const char *arch = NULL;
    const char *os = NULL;
    const char *inputs[MAX_INPUT_FILES];
    int input_count = 0;
    const char *output = NULL;
    int compile_only = 0;
    int static_lib = 0;
    int show_version = 0;
    int test_mode = 0;
    int cmap_mode = 0;
    int mlir_backend = 0;
    int emit_mlir = 0;
    const char *linker = NULL;
    int link_flag_count = 0;
    const char *link_flags[64];
    int include_dir_count = 0;
    const char *include_dirs[64];
    CodecOptLevel opt_level = CODEC_O0;
    const char *target_features = NULL;
    const char *exe_dir = exe_dir_from(argv[0]);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            show_version = 1;
        } else if (strcmp(argv[i], "--cpu") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "error: --cpu requires an argument\n"); return 1; }
            arch = argv[++i];
        } else if (strcmp(argv[i], "--os") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "error: --os requires an argument\n"); return 1; }
            os = argv[++i];
        } else if (strcmp(argv[i], "--mattr") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "error: --mattr requires an argument\n"); return 1; }
            target_features = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "error: -o requires an argument\n"); return 1; }
            output = argv[++i];
        } else if (strncmp(argv[i], "-O", 2) == 0) {
            const char *val = argv[i] + 2;
            if (val[0] == '\0') {
                if (i + 1 >= argc) { fprintf(stderr, "error: -O requires an argument\n"); return 1; }
                val = argv[++i];
            }
            int level = atoi(val);
            if (level < 0 || level > 3) { fprintf(stderr, "error: -O must be 0-3\n"); return 1; }
            opt_level = (CodecOptLevel)level;
        } else if (strcmp(argv[i], "-c") == 0) {
            compile_only = 1;
        } else if (strcmp(argv[i], "--static") == 0) {
            static_lib = 1;
        } else if (strcmp(argv[i], "--ld") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "error: --ld requires an argument\n"); return 1; }
            linker = argv[++i];
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--lib") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "error: %s requires an argument\n", argv[i]); return 1; }
            if (link_flag_count >= 64) { fprintf(stderr, "error: too many -l flags\n"); return 1; }
            char *flag = malloc(strlen(argv[i+1]) + 4);
            if (!flag) { fprintf(stderr, "error: out of memory\n"); return 1; }
            sprintf(flag, "-l%s", argv[++i]);
            link_flags[link_flag_count++] = flag;
        } else if (strcmp(argv[i], "-L") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "error: -L requires an argument\n"); return 1; }
            if (link_flag_count >= 64) { fprintf(stderr, "error: too many -L flags\n"); return 1; }
            char *flag = malloc(strlen(argv[i+1]) + 4);
            if (!flag) { fprintf(stderr, "error: out of memory\n"); return 1; }
            sprintf(flag, "-L%s", argv[++i]);
            link_flags[link_flag_count++] = flag;
        } else if (strcmp(argv[i], "--link-flag") == 0) {
            /* Passed to the linker verbatim, for flags c1 has no option for
               (-framework Cocoa, -pthread, ...). */
            if (i + 1 >= argc) { fprintf(stderr, "error: --link-flag requires an argument\n"); return 1; }
            if (link_flag_count >= 64) { fprintf(stderr, "error: too many link flags\n"); return 1; }
            /* Copied because link_flags is freed wholesale at the end. */
            link_flags[link_flag_count++] = strdup(argv[++i]);
        } else if (strcmp(argv[i], "-I") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "error: -I requires an argument\n"); return 1; }
            if (include_dir_count >= 64) { fprintf(stderr, "error: too many -I flags\n"); return 1; }
            include_dirs[include_dir_count++] = argv[++i];
        } else if (strcmp(argv[i], "--test") == 0) {
            test_mode = 1;
        } else if (strcmp(argv[i], "--cmap") == 0) {
            cmap_mode = 1;
        } else if (strcmp(argv[i], "--backend") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "error: --backend requires an argument\n"); return 1; }
            i++;
            if (strcmp(argv[i], "mlir") == 0) {
                mlir_backend = 1;
            } else if (strcmp(argv[i], "llvm") == 0) {
                mlir_backend = 0;
            } else {
                fprintf(stderr, "error: unknown backend: %s (use llvm or mlir)\n", argv[i]);
                return 1;
            }
        } else if (strncmp(argv[i], "--backend=", 10) == 0) {
            const char *val = argv[i] + 10;
            if (strcmp(val, "mlir") == 0) {
                mlir_backend = 1;
            } else if (strcmp(val, "llvm") == 0) {
                mlir_backend = 0;
            } else {
                fprintf(stderr, "error: unknown backend: %s (use llvm or mlir)\n", val);
                return 1;
            }
        } else if (strcmp(argv[i], "--emit-mlir") == 0) {
            emit_mlir = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else if (strcmp(argv[i], "test") == 0 && !test_mode) {
            test_mode = 1;
        } else {
            if (input_count >= MAX_INPUT_FILES) {
                fprintf(stderr, "error: too many input files (max %d)\n", MAX_INPUT_FILES);
                return 1;
            }
            inputs[input_count++] = argv[i];
        }
    }

    if (show_version) {
        printf("codeone v" CODEONE_VERSION "\n");
        printf("SSA compiler with LLVM backend\n");
        return 0;
    }

    if (input_count == 0) {
        fprintf(stderr, "error: no input file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    /* --cmap: render each input to its own .cmap file, no compilation. */
    if (cmap_mode) {
        int rc = 0;
        for (int i = 0; i < input_count; i++)
            if (!emit_cmap_file(inputs[i], include_dirs, include_dir_count)) rc = 1;
        return rc;
    }

    /* --emit-mlir: render each input to its c1 dialect module, no compile. */
    if (emit_mlir) {
        int rc = 0;
        for (int i = 0; i < input_count; i++)
            if (!emit_mlir_file(inputs[i], include_dirs, include_dir_count)) rc = 1;
        return rc;
    }

    if (!arch) arch = detect_host_arch();
    if (!os) os = detect_host_os();

    /* --- Derive exe/archive output path from first input --- */
    const char *base = file_basename(inputs[0]);
    const char *ext = NULL;
    for (const char *p = base + strlen(base); p >= base; p--) {
        if (*p == '.') { ext = p; break; }
    }

    static char exe_path[1024];

    if (output) {
        snprintf(exe_path, sizeof(exe_path), "%s", output);
    } else if (static_lib) {
        snprintf(exe_path, sizeof(exe_path), "%.*s.a", (int)(ext - base), base);
    } else if (input_count == 1 && ext &&
               (strcmp(ext, ".bx") == 0 || strcmp(ext, ".ssa") == 0)) {
        snprintf(exe_path, sizeof(exe_path), "%.*s", (int)(ext - base), base);
    } else {
        snprintf(exe_path, sizeof(exe_path), "a.out");
    }

    /* --- Test mode: scan for @test_* functions, generate harness --- */
    const char **test_names = NULL;
    int test_count = 0;
    char test_harness_path[256] = "";

    if (test_mode) {
        /* Phase 1: scan all input files for @test_* functions */
        for (int i = 0; i < input_count; i++) {
            FILE *fp = open_with_include(inputs[i], include_dirs, include_dir_count);
            if (!fp) {
                fprintf(stderr, "error: cannot open: %s\n", inputs[i]);
                return 1;
            }
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            char *source = malloc((size_t)sz + 1);
            fread(source, 1, (size_t)sz, fp);
            fclose(fp);
            source[sz] = '\0';

            const char **file_tests = NULL;
            int file_count = scan_test_functions(source, &file_tests);
            free(source);

            if (file_count > 0) {
                test_names = realloc(test_names, (size_t)(test_count + file_count) * sizeof(char *));
                memcpy((void *)(test_names + test_count), file_tests,
                       (size_t)file_count * sizeof(char *));
                free(file_tests);
                test_count += file_count;
            }
        }

        if (test_count == 0) {
            printf("no tests found\n");
            free(test_names);
            return 0;
        }

        /* Generate harness SSA text */
        char *harness_text = generate_test_harness(test_names, test_count);

        /* Write harness to temp file */
        snprintf(test_harness_path, sizeof(test_harness_path),
                 "___test_harness_%d.bx", getpid());
        FILE *hf = fopen(test_harness_path, "w");
        if (!hf) {
            fprintf(stderr, "error: cannot create test harness: %s\n", test_harness_path);
            free(harness_text);
            free(test_names);
            return 1;
        }
        fputs(harness_text, hf);
        fclose(hf);
        free(harness_text);

        /* Add harness to input list */
        if (input_count >= MAX_INPUT_FILES) {
            fprintf(stderr, "error: too many input files\n");
            free(test_names);
            remove(test_harness_path);
            return 1;
        }
        inputs[input_count++] = test_harness_path;

        /* Default output for test mode */
        if (!output) {
            const char *b = file_basename(inputs[0]);
            const char *dot = NULL;
            for (const char *p = b + strlen(b); p >= b; p--)
                if (*p == '.') { dot = p; break; }
            if (dot)
                snprintf(exe_path, sizeof(exe_path), "%.*s-tests", (int)(dot - b), b);
            else
                snprintf(exe_path, sizeof(exe_path), "%s-tests", b);
        }
    }

    char triple[64];
    if (build_triple(arch, os, triple, sizeof(triple)) != 0) {
        fprintf(stderr, "error: invalid arch/os: %s / %s\n", arch, os);
        if (test_mode) { free(test_names); remove(test_harness_path); }
        return 1;
    }

    fprintf(stdout, "Target:    %s\n", triple);
    for (int i = 0; i < input_count; i++)
        fprintf(stdout, "Input:     %s\n", inputs[i]);
    if (compile_only)
        fprintf(stdout, "Mode:      compile-only (-c)\n");
    else if (static_lib)
        fprintf(stdout, "Mode:      static library\n");
    fprintf(stdout, "Output:    %s\n", exe_path);
    fprintf(stdout, "Optimize:  O%d\n", (int)opt_level);
    fprintf(stdout, "Backend:   %s\n\n", mlir_backend ? "mlir" : "llvm");

    /* --- Compile each .bx to its own .o --- */
    char *obj_paths[64];
    int obj_count = 0;

    for (int i = 0; i < input_count; i++) {
        const char *b = file_basename(inputs[i]);
        const char *dot = NULL;
        for (const char *p = b + strlen(b); p >= b; p--) {
            if (*p == '.') { dot = p; break; }
        }

        char obj_buf[1024];
        if (compile_only && output && input_count == 1)
            snprintf(obj_buf, sizeof(obj_buf), "%s", output);
        else if (dot)
            snprintf(obj_buf, sizeof(obj_buf), "%.*s.o", (int)(dot - b), b);
        else
            snprintf(obj_buf, sizeof(obj_buf), "%s.o", b);

        obj_paths[obj_count] = strdup(obj_buf);
        obj_count++;

        fprintf(stdout, "Compiling: %s -> %s\n", inputs[i], obj_buf);

        /* Read source file */
        FILE *fp = open_with_include(inputs[i], include_dirs, include_dir_count);
        if (!fp) {
            fprintf(stderr, "error: cannot open: %s\n", inputs[i]);
            goto cleanup_objs;
        }
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        char *source = (char *)malloc((size_t)sz + 1);
        if (!source) { fclose(fp); goto cleanup_objs; }
        fread(source, 1, (size_t)sz, fp);
        fclose(fp);
        source[sz] = '\0';

        /* Parse */
        Lexer lex;
        lexer_init(&lex, source);
        lex.filename = inputs[i];   /* for diagnostics; the path outlives compilation */
        Parser parser;
        ast_init(&parser, &lex);
        Program *prog = ast_parse(&parser);

        bool failed = false;

        if (ast_has_error(&parser)) {
            fprintf(stderr, "parse error: %s: %s\n", inputs[i], ast_get_error(&parser));
            failed = true;
        } else {
            /* Compile to .o */
            char err[512] = "";
            if (mlir_backend) {
                LLVMContextRef llvm_ctx = LLVMContextCreate();
                LLVMModuleRef mod = c1_hir_build_llvm_module(
                    prog, llvm_ctx, triple, err, sizeof(err));
                bool ok = false;
                if (!mod) {
                    fprintf(stderr, "error: %s: %s\n", inputs[i], err);
                } else {
                    ok = codec_compile_module(mod, triple, target_features,
                                              opt_level, obj_buf, err,
                                              sizeof(err));
                    if (!ok) fprintf(stderr, "error: %s: %s\n", inputs[i], err);
                    LLVMDisposeModule(mod);
                }
                LLVMContextDispose(llvm_ctx);
                if (!ok) failed = true;
            } else if (!codec_compile_program(prog, triple, target_features, opt_level, obj_buf, err, sizeof(err))) {
                fprintf(stderr, "error: %s: %s\n", inputs[i], err);
                failed = true;
            }
        }

        if (prog) program_free(prog);
        ast_free(&parser);
        lexer_free(&lex);
        free(source);

        if (failed) {
            remove(obj_buf);
            goto cleanup_objs;
        }
    }

    /* --- Archive .o into static library (.a) with --static --- */
    if (static_lib) {
        char ar_cmd[4096];
        int pos = snprintf(ar_cmd, sizeof(ar_cmd), "ar rcs %s", exe_path);
        for (int i = 0; i < obj_count; i++)
            pos += snprintf(ar_cmd + pos, sizeof(ar_cmd) - (size_t)pos, " %s", obj_paths[i]);
        fprintf(stdout, "Archiving: %s\n", ar_cmd);
        int ar_ret = system(ar_cmd);
        for (int i = 0; i < obj_count; i++) { remove(obj_paths[i]); free(obj_paths[i]); }
        if (ar_ret != 0) {
            fprintf(stderr, "error: ar returned %d\n", ar_ret);
            return 1;
        }
        fprintf(stdout, "\nDone: %s\n", exe_path);
        return 0;
    }

    /* --- Link .o into executable (skip with -c) --- */
    if (compile_only) {
        fprintf(stdout, "Compile only:\n");
        for (int i = 0; i < obj_count; i++)
            fprintf(stdout, "  %s\n", obj_paths[i]);
        fprintf(stdout, "\n");
        for (int i = 0; i < obj_count; i++) free(obj_paths[i]);
        return 0;
    }

    if (link_object((const char **)obj_paths, obj_count, exe_path, arch, triple, linker,
                     (const char **)link_flags, link_flag_count,
                     exe_dir) != 0) {
        for (int i = 0; i < obj_count; i++) { remove(obj_paths[i]); free(obj_paths[i]); }
        return 1;
    }

    /* --- Clean up intermediate .o files --- */
    for (int i = 0; i < obj_count; i++) {
        remove(obj_paths[i]);
        free(obj_paths[i]);
    }

    /* --- Test mode: run tests instead of printing Done --- */
    int exit_code = 0;
    if (test_mode) {
        exit_code = run_tests(exe_path, test_names, test_count);
        free(test_names);
        remove(test_harness_path);
        remove(exe_path);
    } else {
        fprintf(stdout, "\nDone: %s\n", exe_path);
    }

    for (int fi = 0; fi < link_flag_count; fi++)
        free((void *)link_flags[fi]);
    return exit_code;

cleanup_objs:
    if (test_mode) {
        free(test_names);
        remove(test_harness_path);
    }
    for (int i = 0; i < obj_count; i++) {
        remove(obj_paths[i]);
        free(obj_paths[i]);
    }
    return 1;
}
