#ifndef C1_HIR_LOWER_H
#define C1_HIR_LOWER_H

#include <memory>
#include <string>

#include "mlir/IR/BuiltinOps.h"

namespace llvm {
class LLVMContext;
class Module;
}  // namespace llvm

namespace c1 {

/* Rewrite a c1 dialect module into the LLVM dialect and translate it to
 * LLVM IR. Returns null and fills `error` on failure. */
std::unique_ptr<llvm::Module> lowerToLLVM(mlir::ModuleOp module,
                                          llvm::LLVMContext &ctx,
                                          const char *triple,
                                          std::string &error);

}  // namespace c1

#endif  // C1_HIR_LOWER_H
