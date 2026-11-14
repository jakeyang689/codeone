#ifndef C1_HIR_BUILD_H
#define C1_HIR_BUILD_H

#include <string>

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OwningOpRef.h"

#include "../ast.h"

namespace c1 {

/* Lower the SSA AST into a module of the c1 dialect. Returns a null module and
 * fills `error` when the program uses something the dialect does not cover
 * yet. */
mlir::OwningOpRef<mlir::ModuleOp> buildModule(const Program *prog,
                                              mlir::MLIRContext &ctx,
                                              std::string &error);

}  // namespace c1

#endif  // C1_HIR_BUILD_H
