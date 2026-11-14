#ifndef LOWERING_IR_H
#define LOWERING_IR_H

#include "ast.h"
#include <llvm-c/Types.h>

/* ==================================================================
 * LLVM IR lowering
 * ================================================================== */

/* Lowers a complete SSA Program into an LLVM module.
 * `target_triple` selects architecture-specific lowering (AES intrinsics);
 * it may be NULL, in which case such instructions fail with an error.
 * The caller provides an LLVMContextRef. The returned LLVMModuleRef must
 * be disposed with LLVMDisposeModule when no longer needed.
 * On error, returns NULL and sets *error_msg. */
LLVMModuleRef lower_to_llvm_module(const Program *prog, LLVMContextRef ctx,
                                    const char *target_triple,
                                    char *error_msg, size_t error_size);

/* Lowers a complete SSA Program into LLVM IR text.
 * Returns a newly allocated string containing the LLVM IR assembly.
 * On error, returns NULL and sets *error_msg. */
char *lower_to_llvm_ir(const Program *prog, const char *target_triple,
                       char *error_msg, size_t error_size);

/* Same as above but writes the IR to a file. Returns false on error. */
bool lower_to_llvm_ir_file(const Program *prog, const char *target_triple,
                           const char *output_path,
                           char *error_msg, size_t error_size);

/* Free the IR string returned by lower_to_llvm_ir. */
void lower_free_ir(char *ir);

#endif /* LOWERING_IR_H */
