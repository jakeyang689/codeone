#include "codec.h"
#include "lowering_ir.h"
#include <llvm-c/Core.h>
#include <llvm-c/Types.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/IRReader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ==================================================================
 * Error helpers
 * ================================================================== */

static void set_err(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, size, fmt, ap);
    va_end(ap);
}

/* ==================================================================
 * LLVM codegen state
 * ================================================================== */

/* Global flag: has LLVM target been initialized? */
static bool targets_initialized = false;

static bool ensure_targets_initialized(char *err, size_t err_sz) {
    if (targets_initialized) return true;

    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargets();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllAsmPrinters();
    /* Needed for inline asm: the integrated assembler parses the template. */
    LLVMInitializeAllAsmParsers();

    targets_initialized = true;
    return true;
}

/* ==================================================================
 * Diagnostic capture
 *
 * Inline asm reaches the integrated assembler while the target machine
 * emits the object. AsmParser errors are not reported through the emit
 * return value, so they would otherwise be printed and ignored. LLVM
 * routes them to LLVMContext diagnostics, which lets us count them and
 * turn a bad assembly template into a build failure.
 * ================================================================== */

typedef struct {
    int errors;
} DiagCounter;

static void count_diag(LLVMDiagnosticInfoRef di, void *ctx) {
    DiagCounter *c = (DiagCounter *)ctx;
    LLVMDiagnosticSeverity sev = LLVMGetDiagInfoSeverity(di);

    /* Notes are attached to a preceding error; do not print them twice. */
    if (sev != LLVMDSError && sev != LLVMDSWarning)
        return;

    char *msg = LLVMGetDiagInfoDescription(di);
    fprintf(stderr, "%s: %s\n",
            sev == LLVMDSError ? "error" : "warning",
            msg ? msg : "unknown diagnostic");
    if (msg) LLVMDisposeMessage(msg);

    if (sev == LLVMDSError) c->errors++;
}

/* ==================================================================
 * opt level conversion
 * ================================================================== */

static LLVMCodeGenOptLevel to_llvm_opt(CodecOptLevel level) {
    switch (level) {
        case CODEC_O0: return LLVMCodeGenLevelNone;
        case CODEC_O1: return LLVMCodeGenLevelLess;
        case CODEC_O2: return LLVMCodeGenLevelDefault;
        case CODEC_O3: return LLVMCodeGenLevelAggressive;
    }
    return LLVMCodeGenLevelDefault;
}

/* ==================================================================
 * Public API
 * ================================================================== */

bool codec_compile_module(LLVMModuleRef mod, const char *target_triple,
                           const char *target_features,
                           CodecOptLevel opt_level,
                           const char *output_path,
                           char *err, size_t err_sz) {
    if (!mod || !target_triple || !output_path) {
        set_err(err, err_sz, "invalid arguments (null pointer)");
        return false;
    }

    if (!ensure_targets_initialized(err, err_sz))
        return false;

    /* --- Get target machine --- */
    char *err_triple = NULL;
    LLVMTargetRef target = NULL;
    if (LLVMGetTargetFromTriple(target_triple, &target, &err_triple)) {
        set_err(err, err_sz, "unsupported target: %s",
                err_triple ? err_triple : target_triple);
        if (err_triple) LLVMDisposeMessage(err_triple);
        return false;
    }

    LLVMSetTarget(mod, target_triple);

    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, target_triple, "", target_features ? target_features : "",
        to_llvm_opt(opt_level),
        LLVMRelocDefault,
        LLVMCodeModelDefault);

    if (!tm) {
        set_err(err, err_sz, "failed to create target machine");
        return false;
    }

    /* Verify the module (diagnostic only — do not block emission, as some
       pre-existing .bx files trigger LLVM dominance warnings at -O2) */
    char *verify_err = NULL;
    if (LLVMVerifyModule(mod, LLVMReturnStatusAction, &verify_err)) {
        fprintf(stderr, "warning: module verification: %s\n",
                verify_err ? verify_err : "unknown");
        if (verify_err) LLVMDisposeMessage(verify_err);
    }

    /* --- Emit object file --- */
    LLVMContextRef ctx = LLVMGetModuleContext(mod);
    DiagCounter diags = {0};
    LLVMDiagnosticHandler prev_handler = LLVMContextGetDiagnosticHandler(ctx);
    void *prev_ctx = LLVMContextGetDiagnosticContext(ctx);
    LLVMContextSetDiagnosticHandler(ctx, count_diag, &diags);

    char *emit_err = NULL;
    LLVMBool result = LLVMTargetMachineEmitToFile(
        tm, mod, (char *)output_path, LLVMObjectFile, &emit_err);

    LLVMContextSetDiagnosticHandler(ctx, prev_handler, prev_ctx);

    bool success = true;
    if (result) {
        set_err(err, err_sz, "emit error: %s", emit_err ? emit_err : "unknown");
        success = false;
    } else if (diags.errors > 0) {
        set_err(err, err_sz, "assembly failed: %d error(s)", diags.errors);
        success = false;
    }

    if (!success)
        remove(output_path);

    if (emit_err) LLVMDisposeMessage(emit_err);
    LLVMDisposeTargetMachine(tm);
    return success;
}

bool codec_compile_ir(const char *ir, const char *target_triple,
                       const char *target_features,
                       CodecOptLevel opt_level,
                       const char *output_path,
                       char *err, size_t err_sz) {
    if (!ir || !target_triple || !output_path) {
        set_err(err, err_sz, "invalid arguments (null pointer)");
        return false;
    }

    if (!ensure_targets_initialized(err, err_sz))
        return false;

    LLVMContextRef ctx = LLVMContextCreate();
    if (!ctx) {
        set_err(err, err_sz, "failed to create LLVM context");
        return false;
    }

    LLVMMemoryBufferRef buf = LLVMCreateMemoryBufferWithMemoryRangeCopy(
        ir, strlen(ir), "codec_input");
    if (!buf) {
        set_err(err, err_sz, "failed to create memory buffer");
        LLVMContextDispose(ctx);
        return false;
    }

    LLVMModuleRef mod = NULL;
    char *parse_err = NULL;
    LLVMBool parse_result = LLVMParseIRInContext2(ctx, buf, &mod, &parse_err);
    LLVMDisposeMemoryBuffer(buf);
    if (parse_result) {
        set_err(err, err_sz, "IR parse error: %s", parse_err ? parse_err : "unknown");
        if (parse_err) LLVMDisposeMessage(parse_err);
        LLVMContextDispose(ctx);
        return false;
    }

    bool ok = codec_compile_module(mod, target_triple, target_features,
                                   opt_level, output_path, err, err_sz);
    LLVMDisposeModule(mod);
    LLVMContextDispose(ctx);
    return ok;
}

bool codec_compile_program(const Program *prog, const char *target_triple,
                            const char *target_features,
                            CodecOptLevel opt_level,
                            const char *output_path,
                            char *err, size_t err_sz) {
    if (!prog || !target_triple || !output_path) {
        set_err(err, err_sz, "invalid arguments (null pointer)");
        return false;
    }

    if (!ensure_targets_initialized(err, err_sz))
        return false;

    /* Step 1: Lower directly to LLVM Module (no IR string round-trip) */
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef mod = lower_to_llvm_module(prog, ctx, target_triple, err, err_sz);
    if (!mod) {
        LLVMContextDispose(ctx);
        return false;
    }

    /* Step 2: Compile module to .o */
    bool ok = codec_compile_module(mod, target_triple, target_features,
                                   opt_level, output_path, err, err_sz);
    LLVMDisposeModule(mod);
    LLVMContextDispose(ctx);
    return ok;
}
