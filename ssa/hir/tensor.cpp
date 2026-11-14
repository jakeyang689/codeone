#include "tensor.h"

#include <cstring>
#include <string>
#include <vector>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ComplexToLLVM/ComplexToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/ConvertToLLVM/ToLLVMPass.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVM.h"
#include "mlir/Conversion/IndexToLLVM/IndexToLLVM.h"
#include "mlir/Conversion/MathToLLVM/MathToLLVM.h"
#include "mlir/Conversion/MemRefToLLVM/MemRefToLLVM.h"
#include "mlir/Conversion/Passes.h"
#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Conversion/UBToLLVM/UBToLLVM.h"
#include "mlir/Conversion/VectorToLLVM/ConvertVectorToLLVM.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Index/IR/IndexDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/Linalg/Passes.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"

namespace {

void setErr(char *err, size_t errsz, const std::string &text) {
  if (!err || errsz == 0) return;
  size_t n = text.size() < errsz - 1 ? text.size() : errsz - 1;
  std::memcpy(err, text.data(), n);
  err[n] = '\0';
}

mlir::Type elementType(mlir::MLIRContext *ctx, int dtype) {
  switch (dtype) {
    case C1_DT_F32: return mlir::Float32Type::get(ctx);
    case C1_DT_F64: return mlir::Float64Type::get(ctx);
    case C1_DT_I8: return mlir::IntegerType::get(ctx, 8);
    case C1_DT_I16: return mlir::IntegerType::get(ctx, 16);
    case C1_DT_I32: return mlir::IntegerType::get(ctx, 32);
    case C1_DT_I64: return mlir::IntegerType::get(ctx, 64);
    default: return mlir::Type();
  }
}

mlir::Value makeZero(mlir::OpBuilder &b, mlir::Location loc, mlir::Type elem) {
  return mlir::arith::ConstantOp::create(b, loc, b.getFloatAttr(elem, 0.0));
}

void buildElementwise(mlir::OpBuilder &b, mlir::Location loc,
                      const C1TensorKernel &k, mlir::Value dst, mlir::Value lhs,
                      mlir::Value rhs) {
  mlir::MLIRContext *ctx = b.getContext();
  auto identity = mlir::AffineMap::getMultiDimIdentityMap(k.ndims, ctx);
  llvm::SmallVector<mlir::AffineMap, 3> maps(3, identity);
  llvm::SmallVector<mlir::utils::IteratorType, 2> iterators(
      k.ndims, mlir::utils::IteratorType::parallel);

  mlir::linalg::GenericOp::create(b, 
      loc, mlir::TypeRange{}, mlir::ValueRange{lhs, rhs}, mlir::ValueRange{dst},
      maps, iterators,
      [&](mlir::OpBuilder &bb, mlir::Location l, mlir::ValueRange args) {
        mlir::Value result;
        switch (k.op) {
          case C1_TENSOR_ADD: result = mlir::arith::AddFOp::create(bb, l, args[0], args[1]); break;
          case C1_TENSOR_SUB: result = mlir::arith::SubFOp::create(bb, l, args[0], args[1]); break;
          case C1_TENSOR_MUL: result = mlir::arith::MulFOp::create(bb, l, args[0], args[1]); break;
          case C1_TENSOR_DIV: result = mlir::arith::DivFOp::create(bb, l, args[0], args[1]); break;
          default: break;
        }
        mlir::linalg::YieldOp::create(bb, l, result);
      });
}

bool buildKernel(mlir::ModuleOp module, const C1TensorKernel &k,
                 std::string &errText) {
  mlir::MLIRContext *ctx = module.getContext();
  mlir::OpBuilder builder(ctx);
  // Point diagnostics at the tensor instruction in the .bx file.
  mlir::Location loc = mlir::UnknownLoc::get(ctx);
  if (k.line > 0)
    loc = mlir::FileLineColLoc::get(ctx, k.file ? k.file : "<input>", k.line,
                                    k.column > 0 ? k.column : 1);

  mlir::Type elem = elementType(ctx, k.dtype);
  if (!elem || !mlir::isa<mlir::FloatType>(elem)) {
    errText = "tensor: unsupported element type";
    return false;
  }
  if (k.ndims < 2 || k.ndims > 3) {
    errText = "tensor: unsupported rank";
    return false;
  }

  // matmul is declared as [M, K, N] but its three buffers are 2-D: M*K, K*N,
  // M*N elements. Elementwise ops share one 2-D shape across all buffers.
  llvm::SmallVector<int64_t, 3> dstShape, lhsShape, rhsShape;
  if (k.op == C1_TENSOR_MATMUL) {
    dstShape = {k.dims[0], k.dims[2]};
    lhsShape = {k.dims[0], k.dims[1]};
    rhsShape = {k.dims[1], k.dims[2]};
  } else {
    dstShape = lhsShape = rhsShape = llvm::SmallVector<int64_t, 3>(k.dims, k.dims + k.ndims);
  }
  auto dstTy = mlir::MemRefType::get(dstShape, elem);
  auto lhsTy = mlir::MemRefType::get(lhsShape, elem);
  auto rhsTy = mlir::MemRefType::get(rhsShape, elem);
  auto fnTy = mlir::FunctionType::get(ctx, {dstTy, lhsTy, rhsTy}, {});
  auto fn = mlir::func::FuncOp::create(builder, loc, k.symbol, fnTy);
  // Private so kernels cannot collide across object files and LLVM may inline them.
  fn.setPrivate();
  module.push_back(fn);

  mlir::Block *entry = fn.addEntryBlock();
  mlir::OpBuilder body(entry, entry->begin());
  mlir::Value dst = entry->getArgument(0);
  mlir::Value lhs = entry->getArgument(1);
  mlir::Value rhs = entry->getArgument(2);

  if (k.op == C1_TENSOR_MATMUL) {
    mlir::linalg::FillOp::create(body, loc, mlir::ValueRange{makeZero(body, loc, elem)},
                                      mlir::ValueRange{dst});
    mlir::linalg::MatmulOp::create(body, loc, mlir::ValueRange{lhs, rhs},
                                        mlir::ValueRange{dst});
  } else {
    buildElementwise(body, loc, k, dst, lhs, rhs);
  }
  mlir::func::ReturnOp::create(body, loc);
  return true;
}

void loadDialects(mlir::MLIRContext &ctx) {
  ctx.getOrLoadDialect<mlir::func::FuncDialect>();
  ctx.getOrLoadDialect<mlir::linalg::LinalgDialect>();
  ctx.getOrLoadDialect<mlir::arith::ArithDialect>();
  ctx.getOrLoadDialect<mlir::scf::SCFDialect>();
  ctx.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
  ctx.getOrLoadDialect<mlir::memref::MemRefDialect>();
  ctx.getOrLoadDialect<mlir::index::IndexDialect>();
  ctx.getOrLoadDialect<mlir::ub::UBDialect>();
  ctx.getOrLoadDialect<mlir::LLVM::LLVMDialect>();
}

bool lowerToLLVM(mlir::ModuleOp module, const std::string &triple) {
  mlir::MLIRContext *ctx = module.getContext();
  if (!triple.empty())
    module->setAttr(mlir::LLVM::LLVMDialect::getTargetTripleAttrName(),
                    mlir::StringAttr::get(ctx, triple));

  mlir::PassManager pm(ctx);
  pm.addNestedPass<mlir::func::FuncOp>(mlir::createConvertLinalgToLoopsPass());
  pm.addPass(mlir::createSCFToControlFlowPass());
  // Static memrefs must cross the ABI as bare pointers, matching the
  // `void(ptr, ptr, ptr)` call that lowering emits.
  mlir::ConvertFuncToLLVMPassOptions funcOpts;
  funcOpts.useBarePtrCallConv = true;
  pm.addPass(mlir::createConvertFuncToLLVMPass(funcOpts));
  pm.addPass(mlir::createConvertToLLVMPass());
  pm.addPass(mlir::createReconcileUnrealizedCastsPass());
  return mlir::succeeded(pm.run(module));
}

} // namespace

LLVMModuleRef c1_emit_tensor_kernels(const C1TensorKernel *kernels, int count,
                                     LLVMContextRef ctxRef,
                                     const char *target_triple,
                                     char *err, size_t errsz) {
  if (!kernels || count <= 0) {
    setErr(err, errsz, "tensor: no kernels to emit");
    return nullptr;
  }
  if (!ctxRef) {
    setErr(err, errsz, "tensor: null LLVM context");
    return nullptr;
  }

  mlir::MLIRContext ctx;
  mlir::DialectRegistry registry;
  mlir::registerLLVMDialectTranslation(registry);
  mlir::registerBuiltinDialectTranslation(registry);
  // convert-to-llvm dispatches through dialect interfaces; register them.
  mlir::arith::registerConvertArithToLLVMInterface(registry);
  mlir::cf::registerConvertControlFlowToLLVMInterface(registry);
  // linalg-to-loops loads these dialects even when no such op is generated;
  // convert-to-llvm then asks every loaded dialect for its interface.
  mlir::registerConvertComplexToLLVMInterface(registry);
  mlir::vector::registerConvertVectorToLLVMInterface(registry);
  mlir::registerConvertFuncToLLVMInterface(registry);
  mlir::index::registerConvertIndexToLLVMInterface(registry);
  mlir::registerConvertMathToLLVMInterface(registry);
  mlir::registerConvertMemRefToLLVMInterface(registry);
  mlir::ub::registerConvertUBToLLVMInterface(registry);
  ctx.appendDialectRegistry(registry);
  loadDialects(ctx);

  bool hadError = false;
  std::string diagText;
  mlir::ScopedDiagnosticHandler diagHandler(&ctx, [&](mlir::Diagnostic &diag) {
    if (diag.getSeverity() == mlir::DiagnosticSeverity::Error) {
      hadError = true;
      llvm::raw_string_ostream os(diagText);
      diag.print(os);
      os.flush();
    }
    return mlir::success();
  });

  mlir::OpBuilder builder(&ctx);
  auto module = mlir::ModuleOp::create(builder.getUnknownLoc());
  for (int i = 0; i < count; i++) {
    std::string errText;
    if (!buildKernel(module, kernels[i], errText)) {
      setErr(err, errsz, errText);
      module.erase();
      return nullptr;
    }
  }
  if (mlir::failed(module.verify()) || hadError) {
    setErr(err, errsz, diagText);
    module.erase();
    return nullptr;
  }
  if (!lowerToLLVM(module, target_triple ? target_triple : "")) {
    setErr(err, errsz, diagText);
    module.erase();
    return nullptr;
  }
  if (hadError) {
    setErr(err, errsz, diagText);
    module.erase();
    return nullptr;
  }

  llvm::LLVMContext *llvmCtx = llvm::unwrap(ctxRef);
  std::unique_ptr<llvm::Module> llvmModule =
      mlir::translateModuleToLLVMIR(module.getOperation(), *llvmCtx);
  module.erase();
  if (!llvmModule) {
    setErr(err, errsz, "tensor: failed to translate to LLVM IR");
    return nullptr;
  }
  return llvm::wrap(llvmModule.release());
}
