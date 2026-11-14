#include "lower.h"

#include <cstdio>
#include <string>
#include <vector>

#include "c1_dialect.h"
#include "tensor.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/Dialect/LLVMIR/Transforms/Passes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Location.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/LLVMIR/ModuleTranslation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/Path.h"
#include "llvm/TargetParser/Triple.h"

using namespace mlir;

namespace c1 {
namespace {

class C1ToLLVM {
 public:
  C1ToLLVM(MLIRContext &ctx, std::string &error)
      : ctx(ctx), b(&ctx), error(error) {}

  ModuleOp lower(ModuleOp module) {
    ModuleOp out = ModuleOp::create(UnknownLoc::get(&ctx));
    moduleBlock = out.getBody();
    b.setInsertionPointToStart(out.getBody());
    collectTensorKernels(module);

    /* Named structs: create every identified type first so fields may refer
     * to structs declared later, then fill in the bodies. */
    for (auto op : module.getOps<StructDefOp>()) {
      structTypes[StructType::get(&ctx, op.getName())] =
          LLVM::LLVMStructType::getIdentified(&ctx, op.getName());
    }
    for (auto op : module.getOps<StructDefOp>()) {
      auto llvmStruct = cast<LLVM::LLVMStructType>(
          structTypes[StructType::get(&ctx, op.getName())]);
      llvm::SmallVector<mlir::Type> fields;
      for (Attribute field : op.getFieldTypes()) {
        mlir::Type lt = convertType(cast<TypeAttr>(field).getValue());
        if (!lt) return {};
        fields.push_back(lt);
      }
      if (failed(llvmStruct.setBody(fields, /*isPacked=*/false))) {
        fail("failed to set the body of struct @" + op.getName().str());
        return {};
      }
    }

    for (auto op : module.getOps<GlobalOp>()) {
      b.setInsertionPointToEnd(out.getBody());
      if (!lowerGlobal(op, out)) return {};
    }
    for (auto op : module.getOps<ExternOp>()) {
      b.setInsertionPointToEnd(out.getBody());
      if (op.isVariadic()) variadicFunctions.insert(op.getName());
      if (!lowerExtern(op)) return {};
    }
    for (auto op : module.getOps<FuncOp>()) {
      b.setInsertionPointToEnd(out.getBody());
      if (!lowerFunc(op)) return {};
    }
    return out;
  }

 private:
  MLIRContext &ctx;
  OpBuilder b;
  std::string &error;
  Block *moduleBlock = nullptr;
  DenseMap<Value, Value> values;
  DenseMap<Block *, Block *> blocks;
  DenseMap<mlir::Type, mlir::Type> structTypes;
  std::vector<C1TensorKernel> tensorKernels;
  llvm::StringSet<> tensorDecls;
  llvm::StringSet<> variadicFunctions;

 public:
  llvm::ArrayRef<C1TensorKernel> getTensorKernels() { return tensorKernels; }

 private:

  void fail(const std::string &msg) {
    if (error.empty()) error = msg;
  }

  static LLVM::AtomicOrdering orderingOf(llvm::StringRef name) {
    return llvm::StringSwitch<LLVM::AtomicOrdering>(name)
        .Case("monotonic", LLVM::AtomicOrdering::monotonic)
        .Case("acquire", LLVM::AtomicOrdering::acquire)
        .Case("release", LLVM::AtomicOrdering::release)
        .Case("acq_rel", LLVM::AtomicOrdering::acq_rel)
        .Case("seq_cst", LLVM::AtomicOrdering::seq_cst)
        .Default(LLVM::AtomicOrdering::seq_cst);
  }

  static unsigned abiAlignment(mlir::Type llvmType) {
    if (auto intType = dyn_cast<IntegerType>(llvmType))
      return std::max<unsigned>(1, intType.getWidth() / 8);
    if (llvmType.isF32()) return 4;
    if (llvmType.isF64()) return 8;
    if (isa<LLVM::LLVMPointerType>(llvmType)) return 8;
    return 8;
  }

  mlir::Type convertType(mlir::Type t) {
    if (isa<IntegerType>(t) || isa<FloatType>(t)) return t;
    if (auto ptr = dyn_cast<PtrType>(t))
      return LLVM::LLVMPointerType::get(&ctx);
    if (auto arr = dyn_cast<ArrayType>(t)) {
      mlir::Type elem = convertType(arr.getElementType());
      if (!elem) return {};
      return LLVM::LLVMArrayType::get(elem, arr.getSize());
    }
    if (auto st = dyn_cast<StructType>(t)) {
      auto it = structTypes.find(st);
      if (it == structTypes.end()) {
        fail("unknown struct type @" + st.getStructName().str());
        return {};
      }
      return it->second;
    }
    fail("cannot lower type to LLVM");
    return {};
  }

  LLVM::LLVMFunctionType convertFunctionType(FunctionType type,
                                            bool isVariadic) {
    llvm::SmallVector<mlir::Type> inputs, results;
    for (mlir::Type t : type.getInputs()) {
      mlir::Type lt = convertType(t);
      if (!lt) return {};
      inputs.push_back(lt);
    }
    for (mlir::Type t : type.getResults()) {
      mlir::Type lt = convertType(t);
      if (!lt) return {};
      results.push_back(lt);
    }
    if (results.size() > 1) {
      fail("multiple function results are not supported");
      return {};
    }
    mlir::Type ret =
        results.empty() ? LLVM::LLVMVoidType::get(&ctx) : results.front();
    return LLVM::LLVMFunctionType::get(ret, inputs, isVariadic);
  }

  /* The kernel list and the symbols on the c1.tensor ops come from the same
   * walk, so the calls below and the definitions linked afterwards agree. */
  void collectTensorKernels(ModuleOp module) {
    std::vector<std::string> files;
    std::vector<C1TensorKernel> collected;
    module.walk([&](TensorOp op) {
      C1TensorKernel kernel{};
      kernel.op = llvm::StringSwitch<int>(op.getTensorOp())
                      .Case("add", C1_TENSOR_ADD)
                      .Case("sub", C1_TENSOR_SUB)
                      .Case("mul", C1_TENSOR_MUL)
                      .Case("div", C1_TENSOR_DIV)
                      .Case("matmul", C1_TENSOR_MATMUL)
                      .Default(-1);
      kernel.dtype = C1_DT_F32;
      llvm::ArrayRef<int64_t> dims = op.getDims();
      kernel.ndims = (int)dims.size();
      for (int i = 0; i < kernel.ndims && i < 3; i++) kernel.dims[i] = dims[i];
      snprintf(kernel.symbol, sizeof(kernel.symbol), "%s",
               op.getKernel().str().c_str());
      if (auto fileLoc = dyn_cast<FileLineColLoc>(op.getLoc())) {
        files.push_back(fileLoc.getFilename().getValue().str());
        kernel.line = fileLoc.getLine();
        kernel.column = fileLoc.getColumn();
      }
      collected.push_back(kernel);
    });
    /* Point at the strings only once the vector can no longer reallocate. */
    for (size_t i = 0; i < collected.size(); i++)
      collected[i].file = i < files.size() ? files[i].c_str() : nullptr;
    tensorKernels = std::move(collected);
  }

  bool declareTensorKernel(llvm::StringRef symbol) {
    if (tensorDecls.contains(symbol)) return true;
    mlir::Type ptr = LLVM::LLVMPointerType::get(&ctx);
    LLVM::LLVMFunctionType fnTy = LLVM::LLVMFunctionType::get(
        LLVM::LLVMVoidType::get(&ctx), {ptr, ptr, ptr}, false);
    OpBuilder::InsertionGuard guard(b);
    b.setInsertionPointToEnd(moduleBlock);
    LLVM::LLVMFuncOp::create(b, UnknownLoc::get(&ctx), symbol, fnTy);
    tensorDecls.insert(symbol);
    return true;
  }

  bool lowerGlobal(GlobalOp op, ModuleOp out) {
    Location loc = op.getLoc();
    mlir::Type ty = convertType(op.getGlobalType());
    if (!ty) return false;

    LLVM::Linkage linkage =
        op.isInternal() ? LLVM::Linkage::Internal : LLVM::Linkage::External;
    mlir::Attribute init = op.getInitializer();

    if (init && !isa<ArrayAttr>(init)) {
      LLVM::GlobalOp::create(b, loc, ty, op.isConstant(), linkage, op.getName(),
                             init);
      return true;
    }

    LLVM::GlobalOp global = LLVM::GlobalOp::create(
        b, loc, ty, op.isConstant(), linkage, op.getName(), Attribute());
    Region &region = global.getInitializerRegion();
    region.push_back(new Block());
    OpBuilder::InsertionGuard guard(b);
    b.setInsertionPointToStart(&region.front());

    if (!init) {
      LLVM::ReturnOp::create(b, loc, ValueRange{LLVM::ZeroOp::create(b, loc, ty)});
      return true;
    }

    auto arrInit = cast<ArrayAttr>(init);
    Value current = LLVM::UndefOp::create(b, loc, ty);
    for (size_t i = 0; i < arrInit.size(); i++) {
      Value elem =
          LLVM::ConstantOp::create(b, loc, cast<TypedAttr>(arrInit[i]));
      current = LLVM::InsertValueOp::create(
          b, loc, current, elem, DenseI64ArrayAttr::get(&ctx, {int64_t(i)}));
    }
    LLVM::ReturnOp::create(b, loc, ValueRange{current});
    return true;
  }

  bool lowerExtern(ExternOp op) {
    LLVM::LLVMFunctionType ty =
        convertFunctionType(op.getFunctionType(), op.isVariadic());
    if (!ty) return false;
    LLVM::LLVMFuncOp::create(b, op.getLoc(), op.getName(), ty);
    return true;
  }

  bool lowerFunc(FuncOp op) {
    Location loc = op.getLoc();
    LLVM::LLVMFunctionType fnTy =
        convertFunctionType(op.getFunctionType(), false);
    if (!fnTy) return false;

    LLVM::LLVMFuncOp func =
        LLVM::LLVMFuncOp::create(b, loc, op.getName(), fnTy);

    Region &srcBody = op.getBody();
    Block *entry = func.addEntryBlock(b);
    blocks[&srcBody.front()] = entry;
    for (auto it = std::next(srcBody.begin()); it != srcBody.end(); ++it) {
      Block *block = new Block();
      func.getBody().push_back(block);
      for (unsigned i = 0; i < it->getNumArguments(); i++) {
        mlir::Type t = convertType(it->getArgument(i).getType());
        if (!t) return false;
        block->addArgument(t, loc);
      }
      blocks[&*it] = block;
    }

    for (unsigned i = 0; i < srcBody.front().getNumArguments(); i++)
      values[srcBody.front().getArgument(i)] = entry->getArgument(i);
    for (auto it = std::next(srcBody.begin()); it != srcBody.end(); ++it)
      for (unsigned i = 0; i < it->getNumArguments(); i++)
        values[it->getArgument(i)] = blocks[&*it]->getArgument(i);

    for (Block &srcBlock : srcBody) {
      Block *dstBlock = blocks[&srcBlock];
      b.setInsertionPointToEnd(dstBlock);
      for (Operation &op : srcBlock) {
        if (!lowerOp(&op)) return false;
      }
    }
    return true;
  }

  mlir::Value mapOperand(mlir::Value v) {
    auto it = values.find(v);
    if (it == values.end()) {
      fail("internal error: value was not lowered");
      return {};
    }
    return it->second;
  }

  bool mapOperands(ValueRange range, llvm::SmallVectorImpl<Value> &out) {
    for (Value v : range) {
      Value mapped = mapOperand(v);
      if (!mapped) return false;
      out.push_back(mapped);
    }
    return true;
  }

  bool lowerOp(Operation *op) {
    Location loc = op->getLoc();
    StringRef name = op->getName().getStringRef();

    if (auto cst = dyn_cast<ConstOp>(op)) {
      Value v = LLVM::ConstantOp::create(b, loc,
                                        cast<TypedAttr>(cst.getValue()));
      bind(op, v);
      return true;
    }
    if (auto addr = dyn_cast<AddressOfOp>(op)) {
      Value v = LLVM::AddressOfOp::create(
          b, loc, LLVM::LLVMPointerType::get(&ctx), addr.getGlobalName());
      bind(op, v);
      return true;
    }
    if (auto alloca = dyn_cast<AllocaOp>(op)) {
      mlir::Type elem = convertType(cast<PtrType>(alloca.getType()).getPointee());
      if (!elem) return false;
      Value one = LLVM::ConstantOp::create(b, loc, b.getI64IntegerAttr(1));
      Value v = LLVM::AllocaOp::create(b, loc, LLVM::LLVMPointerType::get(&ctx),
                                       elem, one,
                                       (unsigned)alloca.getAlignment());
      bind(op, v);
      return true;
    }
    if (auto load = dyn_cast<LoadOp>(op)) {
      mlir::Type ty = convertType(load.getType());
      if (!ty) return false;
      Value addr = mapOperand(load.getOperation()->getOperand(0));
      if (!addr) return false;
      bind(op, LLVM::LoadOp::create(b, loc, ty, addr));
      return true;
    }
    if (auto store = dyn_cast<StoreOp>(op)) {
      Value value = mapOperand(store.getOperation()->getOperand(0));
      Value addr = mapOperand(store.getOperation()->getOperand(1));
      if (!value || !addr) return false;
      LLVM::StoreOp::create(b, loc, value, addr);
      return true;
    }
    if (auto field = dyn_cast<FieldOp>(op)) {
      StructType st = StructType::get(&ctx, field.getStructName());
      auto it = structTypes.find(st);
      if (it == structTypes.end()) {
        fail("unknown struct type @" + field.getStructName().str());
        return false;
      }
      Value base = mapOperand(field.getOperation()->getOperand(0));
      if (!base) return false;
      LLVM::GEPArg indices[2] = {LLVM::GEPArg(0),
                                 LLVM::GEPArg((int32_t)field.getFieldIndex())};
      Value v = LLVM::GEPOp::create(b, loc, LLVM::LLVMPointerType::get(&ctx),
                                    it->second, base, indices);
      bind(op, v);
      return true;
    }
    if (auto nul = dyn_cast<NullOp>(op)) {
      mlir::Type ty = convertType(nul.getResult().getType());
      if (!ty) return false;
      bind(op, LLVM::ZeroOp::create(b, loc, ty));
      return true;
    }
    if (auto gep = dyn_cast<GepOp>(op)) {
      mlir::Type elem = convertType(gep.getElemType());
      mlir::Type ptrTy = convertType(gep.getResult().getType());
      if (!elem || !ptrTy) return false;
      Value base = mapOperand(gep.getOperand(0));
      Value index = mapOperand(gep.getOperand(1));
      if (!base || !index) return false;
      Value v = LLVM::GEPOp::create(b, loc, ptrTy, elem, base,
                                    ValueRange{index});
      bind(op, v);
      return true;
    }
    if (auto cmp = dyn_cast<ICmpOp>(op)) {
      Value lhs = mapOperand(cmp.getOperand(0));
      Value rhs = mapOperand(cmp.getOperand(1));
      if (!lhs || !rhs) return false;
      LLVM::ICmpPredicate pred =
          llvm::StringSwitch<LLVM::ICmpPredicate>(cmp.getPredicate())
              .Case("eq", LLVM::ICmpPredicate::eq)
              .Case("ne", LLVM::ICmpPredicate::ne)
              .Case("slt", LLVM::ICmpPredicate::slt)
              .Case("sle", LLVM::ICmpPredicate::sle)
              .Case("sgt", LLVM::ICmpPredicate::sgt)
              .Case("sge", LLVM::ICmpPredicate::sge)
              .Case("ult", LLVM::ICmpPredicate::ult)
              .Case("ule", LLVM::ICmpPredicate::ule)
              .Case("ugt", LLVM::ICmpPredicate::ugt)
              .Case("uge", LLVM::ICmpPredicate::uge)
              .Default(LLVM::ICmpPredicate::eq);
      bind(op, LLVM::ICmpOp::create(b, loc, pred, lhs, rhs));
      return true;
    }
    if (auto cast = dyn_cast<ZExtOp>(op)) {
      Value arg = mapOperand(cast.getValue());
      mlir::Type to = convertType(cast.getResult().getType());
      if (!arg || !to) return false;
      bind(op, LLVM::ZExtOp::create(b, loc, to, arg));
      return true;
    }
    if (auto cast = dyn_cast<SExtOp>(op)) {
      Value arg = mapOperand(cast.getValue());
      mlir::Type to = convertType(cast.getResult().getType());
      if (!arg || !to) return false;
      bind(op, LLVM::SExtOp::create(b, loc, to, arg));
      return true;
    }
    if (auto cast = dyn_cast<TruncOp>(op)) {
      Value arg = mapOperand(cast.getValue());
      mlir::Type to = convertType(cast.getResult().getType());
      if (!arg || !to) return false;
      bind(op, LLVM::TruncOp::create(b, loc, to, arg));
      return true;
    }
    if (auto load = dyn_cast<AtomicLoadOp>(op)) {
      mlir::Type ty = convertType(load.getType());
      Value addr = mapOperand(load.getOperation()->getOperand(0));
      if (!ty || !addr) return false;
      bind(op, LLVM::LoadOp::create(b, loc, ty, addr, abiAlignment(ty),
                                    /*isVolatile=*/false,
                                    /*isNonTemporal=*/false,
                                    /*isInvariant=*/false,
                                    /*isInvariantGroup=*/false,
                                    orderingOf(load.getOrdering())));
      return true;
    }
    if (auto store = dyn_cast<AtomicStoreOp>(op)) {
      Value value = mapOperand(store.getOperation()->getOperand(0));
      Value addr = mapOperand(store.getOperation()->getOperand(1));
      if (!value || !addr) return false;
      LLVM::StoreOp::create(b, loc, value, addr, abiAlignment(value.getType()),
                            /*isVolatile=*/false, /*isNonTemporal=*/false,
                            /*isInvariantGroup=*/false,
                            orderingOf(store.getOrdering()));
      return true;
    }
    if (auto rmw = dyn_cast<AtomicRMwOp>(op)) {
      mlir::Type ty = convertType(rmw.getType());
      Value addr = mapOperand(rmw.getOperation()->getOperand(0));
      Value value = mapOperand(rmw.getOperation()->getOperand(1));
      if (!ty || !addr || !value) return false;
      llvm::StringRef name = rmw.getBinOp();
      LLVM::AtomicBinOp binOp;
      if (name == "xchg") binOp = LLVM::AtomicBinOp::xchg;
      else if (name == "add") binOp = LLVM::AtomicBinOp::add;
      else if (name == "sub") binOp = LLVM::AtomicBinOp::sub;
      else if (name == "and") binOp = LLVM::AtomicBinOp::_and;
      else if (name == "or") binOp = LLVM::AtomicBinOp::_or;
      else if (name == "xor") binOp = LLVM::AtomicBinOp::_xor;
      else {
        fail("invalid atomicrmw op '" + name.str() + "'");
        return false;
      }
      bind(op, LLVM::AtomicRMWOp::create(b, loc, binOp, addr, value,
                                         orderingOf(rmw.getOrdering()),
                                         /*syncscope=*/"", abiAlignment(ty)));
      return true;
    }
    if (auto cas = dyn_cast<CmpXchgOp>(op)) {
      Value addr = mapOperand(cas.getOperation()->getOperand(0));
      Value cmp = mapOperand(cas.getOperation()->getOperand(1));
      Value newValue = mapOperand(cas.getOperation()->getOperand(2));
      if (!addr || !cmp || !newValue) return false;
      Value result = LLVM::AtomicCmpXchgOp::create(
          b, loc, addr, cmp, newValue, orderingOf(cas.getSuccessOrdering()),
          orderingOf(cas.getFailureOrdering()), /*syncscope=*/"",
          abiAlignment(cmp.getType()));
      bind(op, LLVM::ExtractValueOp::create(b, loc, result,
                                            ArrayRef<int64_t>{1}));
      return true;
    }
    if (auto asmOp = dyn_cast<InlineAsmOp>(op)) {
      llvm::SmallVector<Value> operands;
      for (Value operand : asmOp.getOperands()) {
        Value mapped = mapOperand(operand);
        if (!mapped) return false;
        operands.push_back(mapped);
      }
      mlir::Type resultType;
      if (asmOp.getNumResults())
        resultType = convertType(asmOp.getResultTypes().front());
      if (asmOp.getNumResults() && !resultType) return false;

      LLVM::AsmDialectAttr dialect = LLVM::AsmDialectAttr::get(
          &ctx, asmOp.getDialect() == "intel" ? LLVM::AsmDialect::AD_Intel
                                              : LLVM::AsmDialect::AD_ATT);
      auto newAsm = LLVM::InlineAsmOp::create(
          b, loc, resultType, operands, asmOp.getText(), asmOp.getConstraints(),
          /*has_side_effects=*/true, asmOp.isAlignStack(),
          LLVM::tailcallkind::TailCallKind::None, dialect,
          /*operand_attrs=*/ArrayAttr());
      if (newAsm.getNumResults() == 1) bind(op, newAsm->getResult(0));
      return true;
    }
    if (auto tensor = dyn_cast<TensorOp>(op)) {
      llvm::SmallVector<Value> operands;
      for (Value operand : tensor.getOperands()) {
        Value mapped = mapOperand(operand);
        if (!mapped) return false;
        operands.push_back(mapped);
      }
      if (!declareTensorKernel(tensor.getKernel())) return false;
      LLVM::CallOp::create(
          b, loc, TypeRange{}, FlatSymbolRefAttr::get(&ctx, tensor.getKernel()),
          operands);
      return true;
    }
    if (auto call = dyn_cast<CallOp>(op)) {
      LLVM::LLVMFunctionType fnTy = convertFunctionType(
          call.getCalleeType(), variadicFunctions.contains(call.getCallee()));
      if (!fnTy) return false;
      llvm::SmallVector<Value> args;
      for (Value arg : call.getOperands()) {
        Value mapped = mapOperand(arg);
        if (!mapped) return false;
        args.push_back(mapped);
      }
      // Building from the callee type sets var_callee_type for variadic calls;
      // an ignored result simply stays unused.
      auto newCall = LLVM::CallOp::create(
          b, loc, fnTy, FlatSymbolRefAttr::get(&ctx, call.getCallee()), args);
      if (newCall.getNumResults() == 1 && call.getNumResults() == 1)
        bind(op, newCall.getResult());
      return true;
    }
    if (auto ret = dyn_cast<ReturnOp>(op)) {
      llvm::SmallVector<Value> operands;
      for (Value v : ret.getOperands()) {
        Value mapped = mapOperand(v);
        if (!mapped) return false;
        operands.push_back(mapped);
      }
      LLVM::ReturnOp::create(b, loc, operands);
      return true;
    }
    if (auto br = dyn_cast<BrOp>(op)) {
      auto it = blocks.find(br.getSuccessor());
      if (it == blocks.end()) {
        fail("internal error: branch target was not lowered");
        return false;
      }
      llvm::SmallVector<Value> args;
      if (!mapOperands(br.getOperands(), args)) return false;
      LLVM::BrOp::create(b, loc, args, it->second);
      return true;
    }
    if (auto condBr = dyn_cast<CondBrOp>(op)) {
      Value cond = mapOperand(condBr.getCond());
      if (!cond) return false;
      auto trueIt = blocks.find(condBr.getSuccessor(0));
      auto falseIt = blocks.find(condBr.getSuccessor(1));
      if (trueIt == blocks.end() || falseIt == blocks.end()) {
        fail("internal error: branch target was not lowered");
        return false;
      }
      llvm::SmallVector<Value> trueArgs, falseArgs;
      if (!mapOperands(condBr.getTrueArgs(), trueArgs)) return false;
      if (!mapOperands(condBr.getFalseArgs(), falseArgs)) return false;
      LLVM::CondBrOp::create(b, loc, cond, trueIt->second, trueArgs,
                             falseIt->second, falseArgs);
      return true;
    }

    Value lhs = op->getNumOperands() == 2 ? mapOperand(op->getOperand(0))
                                          : Value();
    Value rhs = op->getNumOperands() == 2 ? mapOperand(op->getOperand(1))
                                          : Value();
    if (name.starts_with("c1.") && op->getNumOperands() == 2 && !lhs) {
      return false;
    }
    if (name.starts_with("c1.") && op->getNumOperands() == 2 && !rhs) {
      return false;
    }

    bool isFloat = lhs && isa<FloatType>(lhs.getType());
    Operation *v = nullptr;
    if (name == "c1.add") {
      v = isFloat ? LLVM::FAddOp::create(b, loc, lhs, rhs)
                  : LLVM::AddOp::create(b, loc, lhs, rhs);
    } else if (name == "c1.sub") {
      v = isFloat ? LLVM::FSubOp::create(b, loc, lhs, rhs)
                  : LLVM::SubOp::create(b, loc, lhs, rhs);
    } else if (name == "c1.mul") {
      v = isFloat ? LLVM::FMulOp::create(b, loc, lhs, rhs)
                  : LLVM::MulOp::create(b, loc, lhs, rhs);
    } else if (name == "c1.sdiv") {
      v = isFloat ? LLVM::FDivOp::create(b, loc, lhs, rhs)
                  : LLVM::SDivOp::create(b, loc, lhs, rhs);
    } else if (name == "c1.srem") {
      v = isFloat ? LLVM::FRemOp::create(b, loc, lhs, rhs)
                  : LLVM::SRemOp::create(b, loc, lhs, rhs);
    } else if (name == "c1.udiv") {
      v = LLVM::UDivOp::create(b, loc, lhs, rhs);
    } else if (name == "c1.urem") {
      v = LLVM::URemOp::create(b, loc, lhs, rhs);
    } else if (name == "c1.mulhi") {
      auto intTy = dyn_cast<IntegerType>(lhs.getType());
      if (!intTy) {
        fail("c1.mulhi requires an integer operand");
        return false;
      }
      mlir::Type wide = IntegerType::get(&ctx, intTy.getWidth() * 2);
      Value lw = LLVM::ZExtOp::create(b, loc, wide, lhs);
      Value rw = LLVM::ZExtOp::create(b, loc, wide, rhs);
      Value prod = LLVM::MulOp::create(b, loc, lw, rw);
      Value shift = LLVM::ConstantOp::create(
          b, loc, IntegerAttr::get(wide, intTy.getWidth()));
      v = LLVM::TruncOp::create(
          b, loc, lhs.getType(),
          Value(LLVM::LShrOp::create(b, loc, prod, shift)));
    } else if (name == "c1.and") {
      v = LLVM::AndOp::create(b, loc, lhs, rhs);
    } else if (name == "c1.or") {
      v = LLVM::OrOp::create(b, loc, lhs, rhs);
    } else if (name == "c1.xor") {
      v = LLVM::XOrOp::create(b, loc, lhs, rhs);
    } else if (name == "c1.shl") {
      v = LLVM::ShlOp::create(b, loc, lhs, rhs);
    } else if (name == "c1.lshr") {
      v = LLVM::LShrOp::create(b, loc, lhs, rhs);
    } else if (name == "c1.ashr") {
      v = LLVM::AShrOp::create(b, loc, lhs, rhs);
    } else {
      fail("cannot lower operation '" + name.str() + "' to LLVM");
      return false;
    }
    bind(op, v ? v->getResult(0) : Value());
    return true;
  }

  void bind(Operation *op, Value v) {
    if (op->getNumResults() == 1 && v) values[op->getResult(0)] = v;
  }
};

}  // namespace

/* Attach a DWARF subprogram to every function. The instruction locations are
 * already FileLineColLoc, so the translation turns them into DILocations; the
 * subprogram is what gives each DILocation its scope. */
static bool attachDebugInfo(ModuleOp module, std::string &error) {
  MLIRContext *ctx = module.getContext();
  FileLineColLoc fileLoc;
  module->walk([&](Operation *op) -> WalkResult {
    if (auto loc = dyn_cast<FileLineColLoc>(op->getLoc())) {
      fileLoc = loc;
      return WalkResult::interrupt();
    }
    return WalkResult::advance();
  });
  if (!fileLoc) return true;

  llvm::StringRef path = fileLoc.getFilename().getValue();
  LLVM::DIFileAttr file = LLVM::DIFileAttr::get(
      ctx, llvm::sys::path::filename(path), llvm::sys::path::parent_path(path));
  LLVM::DICompileUnitAttr compileUnit = LLVM::DICompileUnitAttr::get(
      DistinctAttr::create(UnitAttr::get(ctx)), llvm::dwarf::DW_LANG_C, file,
      StringAttr::get(ctx, "codeone"), /*isOptimized=*/true,
      LLVM::DIEmissionKind::Full);
  module->setLoc(FusedLoc::get(ctx, {module->getLoc()}, compileUnit));

  PassManager pm(ctx);
  pm.addPass(LLVM::createDIScopeForLLVMFuncOpPass());
  if (failed(pm.run(module))) {
    if (error.empty()) error = "failed to attach debug information";
    return false;
  }
  return true;
}

std::unique_ptr<llvm::Module> lowerToLLVM(ModuleOp module,
                                          llvm::LLVMContext &ctx,
                                          const char *triple,
                                          std::string &error) {
  MLIRContext *mlirCtx = module.getContext();
  C1ToLLVM lowerer(*mlirCtx, error);
  ModuleOp llvmModule = lowerer.lower(module);
  if (!llvmModule) return nullptr;

  if (!attachDebugInfo(llvmModule, error)) {
    llvmModule->destroy();
    return nullptr;
  }

  std::unique_ptr<llvm::Module> llvmIR =
      mlir::translateModuleToLLVMIR(llvmModule, ctx);
  if (!llvmIR) {
    if (error.empty()) error = "failed to translate the module to LLVM IR";
    llvmModule->destroy();
    return nullptr;
  }
  if (triple) llvmIR->setTargetTriple(llvm::Triple(triple));
  llvmModule->destroy();

  llvm::ArrayRef<C1TensorKernel> kernels = lowerer.getTensorKernels();
  if (!kernels.empty()) {
    char errbuf[512] = {0};
    LLVMModuleRef kernelModule =
        c1_emit_tensor_kernels(kernels.data(), (int)kernels.size(),
                               llvm::wrap(&ctx), triple, errbuf, sizeof(errbuf));
    if (!kernelModule) {
      if (error.empty())
        error = errbuf[0] ? errbuf : "failed to emit tensor kernels";
      return nullptr;
    }
    std::unique_ptr<llvm::Module> kernelIR(llvm::unwrap(kernelModule));
    if (llvm::Linker::linkModules(*llvmIR, std::move(kernelIR))) {
      if (error.empty()) error = "failed to link tensor kernels";
      return nullptr;
    }
  }
  return llvmIR;
}

}  // namespace c1
