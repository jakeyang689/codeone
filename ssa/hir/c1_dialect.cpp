#include "c1_dialect.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"

using namespace mlir;

namespace c1 {

/* ==================================================================
 * Types
 * ================================================================== */

PtrType PtrType::get(MLIRContext *ctx, mlir::Type pointee) {
  return Base::get(ctx, pointee);
}

ArrayType ArrayType::get(MLIRContext *ctx, mlir::Type elem, int64_t size) {
  return Base::get(ctx, elem, size);
}

StructType StructType::get(MLIRContext *ctx, llvm::StringRef structName) {
  return Base::get(ctx, structName);
}

/* ==================================================================
 * Dialect
 * ================================================================== */

C1Dialect::C1Dialect(MLIRContext *ctx)
    : Dialect(getDialectNamespace(), ctx, TypeID::get<C1Dialect>()) {
  addTypes<PtrType, ArrayType, StructType>();
  addOperations<FuncOp, ExternOp, GlobalOp, StructDefOp, ConstOp, NullOp,
                AddressOfOp, AllocaOp, LoadOp, StoreOp, FieldOp, MulhiOp,
                ZExtOp, SExtOp, TruncOp, AddOp, SubOp, MulOp, SDivOp, UDivOp,
                SRemOp, URemOp, AndOp, OrOp, XOrOp, ShlOp, LShrOp, AShrOp,
                ICmpOp, GepOp, CallOp, AtomicLoadOp, AtomicStoreOp,
                AtomicRMwOp, CmpXchgOp, InlineAsmOp, TensorOp, ReturnOp, BrOp,
                CondBrOp>();
}

Type C1Dialect::parseType(DialectAsmParser &parser) const {
  llvm::StringRef kw;
  if (parser.parseKeyword(&kw)) return nullptr;
  MLIRContext *ctx = parser.getContext();
  if (kw == "ptr") {
    mlir::Type pointee;
    if (parser.parseLess() || parser.parseType(pointee) ||
        parser.parseGreater())
      return nullptr;
    return PtrType::get(ctx, pointee);
  }
  if (kw == "array") {
    mlir::Type elem;
    int64_t size = 0;
    if (parser.parseLess() || parser.parseType(elem) || parser.parseComma() ||
        parser.parseInteger(size) || parser.parseGreater())
      return nullptr;
    return ArrayType::get(ctx, elem, size);
  }
  if (kw == "struct") {
    StringAttr name;
    if (parser.parseLess() || parser.parseSymbolName(name) ||
        parser.parseGreater())
      return nullptr;
    return StructType::get(ctx, name.getValue());
  }
  parser.emitError(parser.getCurrentLocation())
      << "unknown c1 type '" << kw << "'";
  return nullptr;
}

void C1Dialect::printType(Type type, DialectAsmPrinter &p) const {
  if (auto ptr = dyn_cast<PtrType>(type)) {
    p << "ptr<" << ptr.getPointee() << ">";
    return;
  }
  if (auto arr = dyn_cast<ArrayType>(type)) {
    p << "array<" << arr.getElementType() << ", " << arr.getSize() << ">";
    return;
  }
  if (auto st = dyn_cast<StructType>(type)) {
    p << "struct<@" << st.getStructName() << ">";
    return;
  }
  p << "<unknown c1 type>";
}

/* ==================================================================
 * Printing helpers
 * ================================================================== */

void printBinaryOp(OpAsmPrinter &p, Operation *op) {
  p << " " << op->getOperand(0) << ", " << op->getOperand(1) << " : "
    << op->getResult(0).getType();
}

void printFuncType(OpAsmPrinter &p, FunctionType type, bool isVariadic) {
  llvm::interleaveComma(type.getInputs(), p,
                        [&](mlir::Type t) { p << t; });
  if (isVariadic) {
    if (!type.getInputs().empty()) p << ", ";
    p << "...";
  }
  p << ") -> ";
  if (type.getResults().empty()) {
    p << "()";
  } else {
    llvm::interleaveComma(type.getResults(), p,
                          [&](mlir::Type t) { p << t; });
  }
}

/* ==================================================================
 * Module-level ops
 * ================================================================== */

void FuncOp::build(OpBuilder &b, OperationState &state, llvm::StringRef name,
                   FunctionType type, llvm::ArrayRef<llvm::StringRef> argNames) {
  state.addAttribute("sym_name", b.getStringAttr(name));
  state.addAttribute("function_type", TypeAttr::get(type));
  llvm::SmallVector<Attribute> names;
  for (llvm::StringRef arg : argNames)
    names.push_back(b.getStringAttr(arg));
  state.addAttribute("arg_names", b.getArrayAttr(names));
  state.addRegion();
}

llvm::StringRef FuncOp::getName() {
  auto attr = (*this)->getAttrOfType<StringAttr>("sym_name");
  return attr ? attr.getValue() : llvm::StringRef();
}

FunctionType FuncOp::getFunctionType() {
  return cast<FunctionType>(
      (*this)->getAttrOfType<TypeAttr>("function_type").getValue());
}

ArrayAttr FuncOp::getArgNamesAttr() {
  return (*this)->getAttrOfType<ArrayAttr>("arg_names");
}

LogicalResult FuncOp::verify() {
  if (getBody().empty())
    return emitOpError("function must have a body");
  unsigned index = 0;
  for (Block &block : getBody()) {
    if (block.empty())
      return emitOpError("block #") << index << " must not be empty";
    if (!block.back().hasTrait<OpTrait::IsTerminator>())
      return emitOpError("in @") << getName() << ": block #" << index
                            << " must end with a terminator";
    index++;
  }
  return success();
}

void FuncOp::print(OpAsmPrinter &p) {
  p << " @" << getName() << "(";
  printFuncType(p, getFunctionType(), false);
  p << " ";
  p.printRegion(getBody(), /*printEntryBlockArgs=*/false,
                /*printBlockTerminators=*/true);
}

void ExternOp::build(OpBuilder &b, OperationState &state, llvm::StringRef name,
                     FunctionType type, bool isVariadic) {
  state.addAttribute("sym_name", b.getStringAttr(name));
  state.addAttribute("function_type", TypeAttr::get(type));
  if (isVariadic) state.addAttribute("variadic", b.getUnitAttr());
}

bool ExternOp::isVariadic() { return (*this)->hasAttr("variadic"); }

llvm::StringRef ExternOp::getName() {
  auto attr = (*this)->getAttrOfType<StringAttr>("sym_name");
  return attr ? attr.getValue() : llvm::StringRef();
}

FunctionType ExternOp::getFunctionType() {
  return cast<FunctionType>(
      (*this)->getAttrOfType<TypeAttr>("function_type").getValue());
}

void ExternOp::print(OpAsmPrinter &p) {
  p << " @" << getName() << "(";
  printFuncType(p, getFunctionType(), isVariadic());
}

void GlobalOp::build(OpBuilder &b, OperationState &state, llvm::StringRef name,
                     mlir::Type type, bool isConst, bool isInternal,
                     Attribute initializer) {
  state.addAttribute("sym_name", b.getStringAttr(name));
  state.addAttribute("global_type", TypeAttr::get(type));
  if (isConst) state.addAttribute("constant", b.getUnitAttr());
  if (isInternal) state.addAttribute("internal", b.getUnitAttr());
  if (initializer) state.addAttribute("initial_value", initializer);
}

llvm::StringRef GlobalOp::getName() {
  auto attr = (*this)->getAttrOfType<StringAttr>("sym_name");
  return attr ? attr.getValue() : llvm::StringRef();
}

mlir::Type GlobalOp::getGlobalType() {
  return (*this)->getAttrOfType<TypeAttr>("global_type").getValue();
}

Attribute GlobalOp::getInitializer() {
  return (*this)->getAttr("initial_value");
}

bool GlobalOp::isConstant() {
  return (*this)->hasAttr("constant");
}

bool GlobalOp::isInternal() { return (*this)->hasAttr("internal"); }

void GlobalOp::print(OpAsmPrinter &p) {
  p << " @" << getName() << " : " << getGlobalType();
  if (isConstant()) p << " constant";
  if (isInternal()) p << " internal";
  if (Attribute init = getInitializer()) {
    p << " = ";
    p.printAttribute(init);
  }
}

/* ==================================================================
 * Struct definitions
 * ================================================================== */

void StructDefOp::build(OpBuilder &b, OperationState &state, llvm::StringRef name,
                        llvm::ArrayRef<llvm::StringRef> fieldNames,
                        llvm::ArrayRef<mlir::Type> fieldTypes) {
  state.addAttribute("sym_name", b.getStringAttr(name));
  llvm::SmallVector<Attribute> names, types;
  for (llvm::StringRef field : fieldNames)
    names.push_back(b.getStringAttr(field));
  for (mlir::Type type : fieldTypes) types.push_back(TypeAttr::get(type));
  state.addAttribute("field_names", b.getArrayAttr(names));
  state.addAttribute("field_types", b.getArrayAttr(types));
}

llvm::StringRef StructDefOp::getName() {
  auto attr = (*this)->getAttrOfType<StringAttr>("sym_name");
  return attr ? attr.getValue() : llvm::StringRef();
}

ArrayAttr StructDefOp::getFieldNames() {
  return (*this)->getAttrOfType<ArrayAttr>("field_names");
}

ArrayAttr StructDefOp::getFieldTypes() {
  return (*this)->getAttrOfType<ArrayAttr>("field_types");
}

void StructDefOp::print(OpAsmPrinter &p) {
  p << " @" << getName() << " {";
  ArrayAttr names = getFieldNames();
  ArrayAttr types = getFieldTypes();
  for (size_t i = 0; i < types.size(); i++) {
    if (i) p << ", ";
    p << cast<StringAttr>(names[i]).getValue() << ": "
      << cast<TypeAttr>(types[i]).getValue();
  }
  p << "}";
}

/* ==================================================================
 * Value-producing ops
 * ================================================================== */

void ConstOp::build(OpBuilder &b, OperationState &state, Attribute value) {
  state.addTypes(cast<TypedAttr>(value).getType());
  state.addAttribute("value", value);
}

Attribute ConstOp::getValue() { return (*this)->getAttr("value"); }

void ConstOp::print(OpAsmPrinter &p) {
  p << " ";
  p.printAttributeWithoutType(getValue());
  p << " : " << getType();
}

void NullOp::build(OpBuilder &b, OperationState &state, mlir::Type resultType) {
  state.addTypes(resultType);
}

void NullOp::print(OpAsmPrinter &p) { p << " : " << getType(); }

void AddressOfOp::build(OpBuilder &b, OperationState &state,
                        llvm::StringRef globalName, mlir::Type resultType) {
  state.addTypes(resultType);
  state.addAttribute("global_name", FlatSymbolRefAttr::get(b.getContext(),
                                                           globalName));
}

llvm::StringRef AddressOfOp::getGlobalName() {
  auto attr = (*this)->getAttrOfType<FlatSymbolRefAttr>("global_name");
  return attr ? attr.getValue() : llvm::StringRef();
}

void AddressOfOp::print(OpAsmPrinter &p) {
  p << " @" << getGlobalName() << " : " << getType();
}

void AllocaOp::build(OpBuilder &b, OperationState &state, mlir::Type resultType,
                     int64_t alignment) {
  state.addTypes(resultType);
  if (alignment > 0) state.addAttribute("alignment", b.getI64IntegerAttr(alignment));
}

int64_t AllocaOp::getAlignment() {
  auto attr = (*this)->getAttrOfType<IntegerAttr>("alignment");
  return attr ? attr.getInt() : 0;
}

void AllocaOp::print(OpAsmPrinter &p) {
  p << " : " << getType();
  if (int64_t alignment = getAlignment()) p << ", align " << alignment;
}

void LoadOp::build(OpBuilder &b, OperationState &state, Value ptr,
                   mlir::Type resultType) {
  state.addOperands(ptr);
  state.addTypes(resultType);
}

void LoadOp::print(OpAsmPrinter &p) {
  p << " " << getOperation()->getOperand(0) << " : " << getType();
}

void StoreOp::build(OpBuilder &b, OperationState &state, Value value, Value ptr) {
  state.addOperands({value, ptr});
}

void StoreOp::print(OpAsmPrinter &p) {
  p << " " << getOperation()->getOperand(0) << ", "
    << getOperation()->getOperand(1) << " : "
    << getOperation()->getOperand(0).getType();
}

void FieldOp::build(OpBuilder &b, OperationState &state, llvm::StringRef structName,
                    Value base, int64_t fieldIndex, mlir::Type resultType) {
  state.addOperands(base);
  state.addTypes(resultType);
  state.addAttribute("struct_name", FlatSymbolRefAttr::get(b.getContext(), structName));
  state.addAttribute("index", b.getI64IntegerAttr(fieldIndex));
}

llvm::StringRef FieldOp::getStructName() {
  auto attr = (*this)->getAttrOfType<FlatSymbolRefAttr>("struct_name");
  return attr ? attr.getValue() : llvm::StringRef();
}

int64_t FieldOp::getFieldIndex() {
  return (*this)->getAttrOfType<IntegerAttr>("index").getInt();
}

void FieldOp::print(OpAsmPrinter &p) {
  p << " @" << getStructName() << ", " << getOperation()->getOperand(0) << ", "
    << getFieldIndex() << " : " << getType();
}

void ICmpOp::build(OpBuilder &b, OperationState &state, llvm::StringRef pred,
                   Value lhs, Value rhs) {
  state.addOperands({lhs, rhs});
  state.addTypes(b.getI1Type());
  state.addAttribute("predicate", b.getStringAttr(pred));
}

llvm::StringRef ICmpOp::getPredicate() {
  auto attr = (*this)->getAttrOfType<StringAttr>("predicate");
  return attr ? attr.getValue() : llvm::StringRef();
}

void ICmpOp::print(OpAsmPrinter &p) {
  p << " " << getPredicate() << " " << getOperand(0) << ", " << getOperand(1)
    << " : " << getOperand(0).getType();
}

LogicalResult ICmpOp::verify() {
  if (getOperand(0).getType() != getOperand(1).getType())
    return emitOpError("both operands must have the same type");
  llvm::StringRef pred = getPredicate();
  if (pred != "eq" && pred != "ne" && pred != "slt" && pred != "sle" &&
      pred != "sgt" && pred != "sge" && pred != "ult" && pred != "ule" &&
      pred != "ugt" && pred != "uge")
    return emitOpError("unknown predicate '") << pred << "'";
  return success();
}

void GepOp::build(OpBuilder &b, OperationState &state, Value base, Value index,
                  mlir::Type elemType, mlir::Type resultType) {
  state.addOperands({base, index});
  state.addAttribute("elem_type", TypeAttr::get(elemType));
  state.addTypes(resultType);
}

mlir::Type GepOp::getElemType() {
  return (*this)->getAttrOfType<TypeAttr>("elem_type").getValue();
}

void GepOp::print(OpAsmPrinter &p) {
  p << " " << getOperand(0) << ", " << getOperand(1) << " : " << getElemType()
    << ", " << getType();
}

void CallOp::build(OpBuilder &b, OperationState &state, llvm::StringRef callee,
                   FunctionType type, ValueRange args) {
  state.addOperands(args);
  state.addTypes(type.getResults());
  state.addAttribute("callee", FlatSymbolRefAttr::get(b.getContext(), callee));
  state.addAttribute("function_type", TypeAttr::get(type));
}

llvm::StringRef CallOp::getCallee() {
  auto attr = (*this)->getAttrOfType<FlatSymbolRefAttr>("callee");
  return attr ? attr.getValue() : llvm::StringRef();
}

FunctionType CallOp::getCalleeType() {
  return cast<FunctionType>(
      (*this)->getAttrOfType<TypeAttr>("function_type").getValue());
}

void CallOp::print(OpAsmPrinter &p) {
  p << " @" << getCallee() << "(";
  llvm::interleaveComma(getOperands(), p, [&](Value v) { p << v; });
  p << ") : (";
  llvm::interleaveComma(getOperands(), p,
                        [&](Value v) { p << v.getType(); });
  p << ") -> ";
  if (getNumResults() == 0) {
    p << "()";
  } else {
    llvm::interleaveComma(getResultTypes(), p,
                          [&](mlir::Type t) { p << t; });
  }
}

/* ==================================================================
 * Atomics
 * ================================================================== */

static mlir::Type pointeeOf(mlir::Value ptr) {
  return cast<PtrType>(ptr.getType()).getPointee();
}

void AtomicLoadOp::build(OpBuilder &b, OperationState &state, Value ptr,
                         llvm::StringRef ordering) {
  state.addOperands(ptr);
  state.addTypes(pointeeOf(ptr));
  state.addAttribute("ordering", b.getStringAttr(ordering));
}

llvm::StringRef AtomicLoadOp::getOrdering() {
  return (*this)->getAttrOfType<StringAttr>("ordering").getValue();
}

void AtomicLoadOp::print(OpAsmPrinter &p) {
  p << " " << getOperation()->getOperand(0) << ", " << getOrdering() << " : "
    << getType();
}

void AtomicStoreOp::build(OpBuilder &b, OperationState &state, Value value,
                          Value ptr, llvm::StringRef ordering) {
  state.addOperands({value, ptr});
  state.addAttribute("ordering", b.getStringAttr(ordering));
}

llvm::StringRef AtomicStoreOp::getOrdering() {
  return (*this)->getAttrOfType<StringAttr>("ordering").getValue();
}

void AtomicStoreOp::print(OpAsmPrinter &p) {
  p << " " << getOperand(0) << ", " << getOperand(1) << ", " << getOrdering()
    << " : " << getOperand(0).getType();
}

void AtomicRMwOp::build(OpBuilder &b, OperationState &state,
                        llvm::StringRef binop, Value ptr, Value value,
                        llvm::StringRef ordering) {
  state.addOperands({ptr, value});
  state.addTypes(pointeeOf(ptr));
  state.addAttribute("binop", b.getStringAttr(binop));
  state.addAttribute("ordering", b.getStringAttr(ordering));
}

llvm::StringRef AtomicRMwOp::getBinOp() {
  return (*this)->getAttrOfType<StringAttr>("binop").getValue();
}

llvm::StringRef AtomicRMwOp::getOrdering() {
  return (*this)->getAttrOfType<StringAttr>("ordering").getValue();
}

void AtomicRMwOp::print(OpAsmPrinter &p) {
  p << " " << getBinOp() << ", " << getOperand(0) << ", " << getOperand(1)
    << ", " << getOrdering() << " : " << getType();
}

void CmpXchgOp::build(OpBuilder &b, OperationState &state, Value ptr,
                      Value cmp, Value newValue, llvm::StringRef successOrdering,
                      llvm::StringRef failureOrdering) {
  state.addOperands({ptr, cmp, newValue});
  state.addTypes(b.getI1Type());
  state.addAttribute("success_ordering", b.getStringAttr(successOrdering));
  state.addAttribute("failure_ordering", b.getStringAttr(failureOrdering));
}

llvm::StringRef CmpXchgOp::getSuccessOrdering() {
  return (*this)->getAttrOfType<StringAttr>("success_ordering").getValue();
}

llvm::StringRef CmpXchgOp::getFailureOrdering() {
  return (*this)->getAttrOfType<StringAttr>("failure_ordering").getValue();
}

void CmpXchgOp::print(OpAsmPrinter &p) {
  p << " " << getOperand(0) << ", " << getOperand(1) << ", " << getOperand(2)
    << ", " << getSuccessOrdering() << ", " << getFailureOrdering() << " : "
    << getOperand(1).getType();
}

/* ==================================================================
 * Inline assembly
 * ================================================================== */

void InlineAsmOp::build(OpBuilder &b, OperationState &state, mlir::Type resultType,
                        ValueRange operands, llvm::StringRef text,
                        llvm::StringRef constraints, bool intelDialect,
                        bool alignStack) {
  state.addOperands(operands);
  if (resultType) state.addTypes(resultType);
  state.addAttribute("text", b.getStringAttr(text));
  state.addAttribute("constraints", b.getStringAttr(constraints));
  state.addAttribute("dialect", b.getStringAttr(intelDialect ? "intel" : "att"));
  if (alignStack) state.addAttribute("align_stack", b.getUnitAttr());
}

llvm::StringRef InlineAsmOp::getText() {
  return (*this)->getAttrOfType<StringAttr>("text").getValue();
}

llvm::StringRef InlineAsmOp::getConstraints() {
  return (*this)->getAttrOfType<StringAttr>("constraints").getValue();
}

llvm::StringRef InlineAsmOp::getDialect() {
  return (*this)->getAttrOfType<StringAttr>("dialect").getValue();
}

bool InlineAsmOp::isAlignStack() { return (*this)->hasAttr("align_stack"); }

void InlineAsmOp::print(OpAsmPrinter &p) {
  p << " " << getDialect();
  if (isAlignStack()) p << " alignstack";
  p << " : ";
  if (getNumResults()) p << getResultTypes().front() << ", ";
  p << "\"" << getText() << "\", \"" << getConstraints() << "\"";
  if (getNumOperands()) {
    p << ", ";
    llvm::interleaveComma(getOperands(), p,
                          [&](Value v) { p << v.getType() << " " << v; });
  }
}

/* ==================================================================
 * Tensor
 * ================================================================== */

void TensorOp::build(OpBuilder &b, OperationState &state, llvm::StringRef tensorOp,
                     mlir::Type elemType, llvm::ArrayRef<int64_t> dims,
                     llvm::StringRef kernel, Value dst, Value lhs, Value rhs) {
  state.addOperands({dst, lhs, rhs});
  state.addAttribute("op", b.getStringAttr(tensorOp));
  state.addAttribute("elem_type", TypeAttr::get(elemType));
  state.addAttribute("dims", b.getDenseI64ArrayAttr(dims));
  state.addAttribute("kernel", b.getStringAttr(kernel));
}

llvm::StringRef TensorOp::getTensorOp() {
  return (*this)->getAttrOfType<StringAttr>("op").getValue();
}

mlir::Type TensorOp::getElemType() {
  return (*this)->getAttrOfType<TypeAttr>("elem_type").getValue();
}

mlir::ArrayRef<int64_t> TensorOp::getDims() {
  return (*this)->getAttrOfType<DenseI64ArrayAttr>("dims").asArrayRef();
}

llvm::StringRef TensorOp::getKernel() {
  return (*this)->getAttrOfType<StringAttr>("kernel").getValue();
}

void TensorOp::print(OpAsmPrinter &p) {
  p << " " << getTensorOp() << ", " << getElemType() << ", [";
  llvm::interleaveComma(getDims(), p);
  p << "], \"" << getKernel() << "\", " << getOperand(0) << ", "
    << getOperand(1) << ", " << getOperand(2);
}

/* ==================================================================
 * Terminators
 * ================================================================== */

void ReturnOp::build(OpBuilder &b, OperationState &state, ValueRange values) {
  state.addOperands(values);
}

void ReturnOp::print(OpAsmPrinter &p) {
  if (getNumOperands() == 0) return;
  p << " " << getOperand(0) << " : " << getOperand(0).getType();
}

void BrOp::build(OpBuilder &b, OperationState &state, Block *dest,
                 ValueRange args) {
  state.addOperands(args);
  state.addSuccessors(dest);
}

void BrOp::print(OpAsmPrinter &p) {
  p << " ";
  p.printSuccessorAndUseList(getSuccessor(), getOperands());
}

LogicalResult BrOp::verify() {
  Block *dest = getSuccessor();
  if (getOperands().size() != dest->getNumArguments())
    return emitOpError("expects ")
           << dest->getNumArguments() << " operands for its successor, got "
           << getOperands().size();
  for (unsigned i = 0; i < getOperands().size(); i++)
    if (getOperand(i).getType() != dest->getArgument(i).getType())
      return emitOpError("operand #")
             << i << " has type " << getOperand(i).getType()
             << ", successor argument has type "
             << dest->getArgument(i).getType();
  return success();
}

void CondBrOp::build(OpBuilder &b, OperationState &state, Value cond,
                     Block *trueDest, ValueRange trueArgs, Block *falseDest,
                     ValueRange falseArgs) {
  state.addOperands(cond);
  state.addOperands(trueArgs);
  state.addOperands(falseArgs);
  state.addAttribute("true_args", b.getI32IntegerAttr((int32_t)trueArgs.size()));
  state.addSuccessors(trueDest);
  state.addSuccessors(falseDest);
}

Value CondBrOp::getCond() { return getOperation()->getOperand(0); }

ValueRange CondBrOp::getTrueArgs() {
  unsigned count = (*this)->getAttrOfType<IntegerAttr>("true_args").getInt();
  return getOperation()->getOperands().slice(1, count);
}

ValueRange CondBrOp::getFalseArgs() {
  unsigned count = (*this)->getAttrOfType<IntegerAttr>("true_args").getInt();
  return getOperation()->getOperands().drop_front(1 + count);
}

void CondBrOp::print(OpAsmPrinter &p) {
  p << " " << getCond() << ", ";
  p.printSuccessorAndUseList(getSuccessor(0), getTrueArgs());
  p << ", ";
  p.printSuccessorAndUseList(getSuccessor(1), getFalseArgs());
}

LogicalResult CondBrOp::verify() {
  unsigned trueCount = (*this)->getAttrOfType<IntegerAttr>("true_args").getInt();
  if (1 + trueCount > getOperation()->getNumOperands())
    return emitOpError("true_args exceeds the operand count");
  auto checkEdge = [&](Block *dest, ValueRange args,
                       llvm::StringRef branch) -> LogicalResult {
    if (args.size() != dest->getNumArguments())
      return emitOpError(branch)
             << " branch expects " << dest->getNumArguments()
             << " operands, got " << args.size();
    for (unsigned i = 0; i < args.size(); i++)
      if (args[i].getType() != dest->getArgument(i).getType())
        return emitOpError(branch)
               << " branch operand #" << i << " has type " << args[i].getType()
               << ", successor argument has type "
               << dest->getArgument(i).getType();
    return success();
  };
  if (failed(checkEdge(getSuccessor(0), getTrueArgs(), "true")))
    return failure();
  return checkEdge(getSuccessor(1), getFalseArgs(), "false");
}

}  // namespace c1
