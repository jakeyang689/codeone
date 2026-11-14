#include "backend.h"

#include <cstdio>
#include <string>

#include "build.h"
#include "c1_dialect.h"
#include "lower.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm-c/Core.h"
#include "llvm/Support/raw_ostream.h"

namespace {

void setError(char *err, size_t errsz, const std::string &msg) {
  if (err && errsz) snprintf(err, errsz, "%s", msg.c_str());
}

}  // namespace

extern "C" LLVMModuleRef c1_hir_build_llvm_module(const Program *prog,
                                                  LLVMContextRef ctx,
                                                  const char *target_triple,
                                                  char *err, size_t errsz) {
  std::string error;
  mlir::DialectRegistry registry;
  mlir::registerBuiltinDialectTranslation(registry);
  mlir::registerLLVMDialectTranslation(registry);
  mlir::MLIRContext mlirCtx(registry);
  mlirCtx.getOrLoadDialect<c1::C1Dialect>();
  mlirCtx.getOrLoadDialect<mlir::LLVM::LLVMDialect>();

  mlir::OwningOpRef<mlir::ModuleOp> module =
      c1::buildModule(prog, mlirCtx, error);
  if (!module) {
    setError(err, errsz, error.empty() ? "failed to build the c1 dialect module"
                                       : error);
    return nullptr;
  }

  llvm::LLVMContext *llvmCtx = llvm::unwrap(ctx);
  std::unique_ptr<llvm::Module> llvmModule =
      c1::lowerToLLVM(*module, *llvmCtx, target_triple, error);
  if (!llvmModule) {
    setError(err, errsz,
             error.empty() ? "failed to lower the module to LLVM IR" : error);
    return nullptr;
  }
  return llvm::wrap(llvmModule.release());
}

extern "C" bool c1_hir_emit_text(const Program *prog, const char *path,
                                 char *err, size_t errsz) {
  std::string error;
  mlir::MLIRContext mlirCtx;
  mlirCtx.getOrLoadDialect<c1::C1Dialect>();

  mlir::OwningOpRef<mlir::ModuleOp> module =
      c1::buildModule(prog, mlirCtx, error);
  if (!module) {
    setError(err, errsz, error.empty() ? "failed to build the c1 dialect module"
                                       : error);
    return false;
  }

  std::error_code ec;
  llvm::raw_fd_ostream out(path, ec);
  if (ec) {
    setError(err, errsz, ec.message());
    return false;
  }
  module->print(out);
  out << "\n";
  return true;
}
