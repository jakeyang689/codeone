#ifndef C1_HIR_BACKEND_H
#define C1_HIR_BACKEND_H

#include <stdbool.h>
#include <stddef.h>

#include <llvm-c/Types.h>

#include "ast.h"

/* C1 MLIR backend: SSA AST -> c1 dialect -> LLVM dialect -> LLVM IR.
 * Used by the c1 CLI for --backend=mlir and --emit-mlir. */

#ifdef __cplusplus
extern "C" {
#endif

/* Build an LLVM module for `prog` in the caller's LLVM context. The caller
 * owns the returned module and must dispose of it. Returns NULL and writes a
 * message to `err` on failure. */
LLVMModuleRef c1_hir_build_llvm_module(const Program *prog, LLVMContextRef ctx,
                                       const char *target_triple,
                                       char *err, size_t errsz);

/* Print the c1 dialect module for `prog` to `path` without compiling. */
bool c1_hir_emit_text(const Program *prog, const char *path,
                      char *err, size_t errsz);

#ifdef __cplusplus
}
#endif

#endif  // C1_HIR_BACKEND_H
