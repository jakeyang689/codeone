#ifndef C1_HIR_C1_DIALECT_H
#define C1_HIR_C1_DIALECT_H

#include "mlir/IR/Builders.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"

namespace mlir {
class OpAsmPrinter;
}  // namespace mlir

namespace c1 {

/* ==================================================================
 * Types: !c1.ptr<T>, !c1.array<T, N>
 * ================================================================== */

class PtrTypeStorage : public mlir::TypeStorage {
 public:
  using KeyTy = mlir::Type;
  explicit PtrTypeStorage(mlir::Type pointee) : pointee(pointee) {}
  bool operator==(const KeyTy &key) const { return key == pointee; }
  static PtrTypeStorage *construct(mlir::TypeStorageAllocator &alloc,
                                   const KeyTy &key) {
    return new (alloc.allocate<PtrTypeStorage>()) PtrTypeStorage(key);
  }
  static llvm::hash_code hashKey(const KeyTy &key) {
    return llvm::hash_combine(key.getAsOpaquePointer());
  }
  mlir::Type pointee;
};

class PtrType
    : public mlir::Type::TypeBase<PtrType, mlir::Type, PtrTypeStorage> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(PtrType)
  static constexpr llvm::StringLiteral name = "c1.ptr";
  using Base::Base;
  static PtrType get(mlir::MLIRContext *ctx, mlir::Type pointee);
  mlir::Type getPointee() const { return getImpl()->pointee; }
};

class ArrayTypeStorage : public mlir::TypeStorage {
 public:
  using KeyTy = std::pair<mlir::Type, int64_t>;
  ArrayTypeStorage(mlir::Type elem, int64_t size) : elem(elem), size(size) {}
  bool operator==(const KeyTy &key) const {
    return key.first == elem && key.second == size;
  }
  static ArrayTypeStorage *construct(mlir::TypeStorageAllocator &alloc,
                                     const KeyTy &key) {
    return new (alloc.allocate<ArrayTypeStorage>())
        ArrayTypeStorage(key.first, key.second);
  }
  static llvm::hash_code hashKey(const KeyTy &key) {
    return llvm::hash_combine(key.first.getAsOpaquePointer(), key.second);
  }
  mlir::Type elem;
  int64_t size;
};

class ArrayType
    : public mlir::Type::TypeBase<ArrayType, mlir::Type, ArrayTypeStorage> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ArrayType)
  static constexpr llvm::StringLiteral name = "c1.array";
  using Base::Base;
  static ArrayType get(mlir::MLIRContext *ctx, mlir::Type elem, int64_t size);
  mlir::Type getElementType() const { return getImpl()->elem; }
  int64_t getSize() const { return getImpl()->size; }
};

class StructTypeStorage : public mlir::TypeStorage {
 public:
  using KeyTy = llvm::StringRef;
  explicit StructTypeStorage(llvm::StringRef name) : name(name) {}
  bool operator==(const KeyTy &key) const { return key == name; }
  static StructTypeStorage *construct(mlir::TypeStorageAllocator &alloc,
                                      const KeyTy &key) {
    return new (alloc.allocate<StructTypeStorage>())
        StructTypeStorage(alloc.copyInto(key));
  }
  static llvm::hash_code hashKey(const KeyTy &key) {
    return llvm::hash_value(key);
  }
  llvm::StringRef name;
};

/* Named struct, identified by symbol: the body lives in a c1.struct_def op. */
class StructType
    : public mlir::Type::TypeBase<StructType, mlir::Type, StructTypeStorage> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(StructType)
  static constexpr llvm::StringLiteral name = "c1.struct";
  using Base::Base;
  static StructType get(mlir::MLIRContext *ctx, llvm::StringRef structName);
  llvm::StringRef getStructName() const { return getImpl()->name; }
};

/* ==================================================================
 * Dialect
 * ================================================================== */

class C1Dialect : public mlir::Dialect {
 public:
  explicit C1Dialect(mlir::MLIRContext *ctx);
  static llvm::StringRef getDialectNamespace() { return "c1"; }
  mlir::Type parseType(mlir::DialectAsmParser &parser) const override;
  void printType(mlir::Type type,
                 mlir::DialectAsmPrinter &printer) const override;
};

/* Printing helpers shared by the ops below. */
void printBinaryOp(mlir::OpAsmPrinter &p, mlir::Operation *op);
void printFuncType(mlir::OpAsmPrinter &p, mlir::FunctionType type,
                   bool isVariadic);

/* Boilerplate ODS normally generates for every op: the inherent attribute
 * name list (empty here) and the static create() helper. */
template <typename Concrete>
class OpBoilerplate {
 public:
  static llvm::ArrayRef<llvm::StringRef> getAttributeNames() { return {}; }

  template <typename... Args>
  static Concrete create(mlir::OpBuilder &b, mlir::Location loc,
                         Args... args) {
    mlir::OperationState state(loc, Concrete::getOperationName());
    Concrete::build(b, state, args...);
    return mlir::cast<Concrete>(b.create(state));
  }
};

/* The builtin OneResult trait no longer exposes getResult()/getType(), and
 * hand-written ops have no generated accessors, so this mixin supplies them. */
template <typename Concrete>
class ResultAccessors {
 public:
  mlir::Value getResult() {
    return static_cast<Concrete *>(this)->getOperation()->getResult(0);
  }
  mlir::Type getType() { return getResult().getType(); }
};

/* ==================================================================
 * Module-level ops
 * ================================================================== */

class FuncOp : public mlir::Op<FuncOp, mlir::OpTrait::OneRegion,
                               mlir::OpTrait::ZeroOperands,
                               mlir::OpTrait::ZeroResults,
                               mlir::OpTrait::IsIsolatedFromAbove>,
                    public OpBoilerplate<FuncOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FuncOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.func"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    llvm::StringRef name, mlir::FunctionType type,
                    llvm::ArrayRef<llvm::StringRef> argNames);
  llvm::StringRef getName();
  mlir::FunctionType getFunctionType();
  mlir::ArrayAttr getArgNamesAttr();
  mlir::Region &getBody() { return getOperation()->getRegion(0); }
  void print(mlir::OpAsmPrinter &p);
  mlir::LogicalResult verify();
};

class ExternOp : public mlir::Op<ExternOp, mlir::OpTrait::ZeroOperands,
                                 mlir::OpTrait::ZeroResults,
                                 mlir::OpTrait::ZeroRegions>,
                 public OpBoilerplate<ExternOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ExternOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.extern"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    llvm::StringRef name, mlir::FunctionType type,
                    bool isVariadic);
  llvm::StringRef getName();
  mlir::FunctionType getFunctionType();
  bool isVariadic();
  void print(mlir::OpAsmPrinter &p);
};

class GlobalOp : public mlir::Op<GlobalOp, mlir::OpTrait::ZeroOperands,
                                mlir::OpTrait::ZeroResults,
                                mlir::OpTrait::ZeroRegions>,
                 public OpBoilerplate<GlobalOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(GlobalOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.global"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    llvm::StringRef name, mlir::Type type, bool isConst,
                    bool isInternal, mlir::Attribute initializer);
  llvm::StringRef getName();
  mlir::Type getGlobalType();
  mlir::Attribute getInitializer();
  bool isConstant();
  bool isInternal();
  void print(mlir::OpAsmPrinter &p);
};

class StructDefOp : public mlir::Op<StructDefOp, mlir::OpTrait::ZeroOperands,
                                    mlir::OpTrait::ZeroResults,
                                    mlir::OpTrait::ZeroRegions>,
                    public OpBoilerplate<StructDefOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(StructDefOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.struct_def"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    llvm::StringRef name, llvm::ArrayRef<llvm::StringRef> fieldNames,
                    llvm::ArrayRef<mlir::Type> fieldTypes);
  llvm::StringRef getName();
  mlir::ArrayAttr getFieldNames();
  mlir::ArrayAttr getFieldTypes();
  void print(mlir::OpAsmPrinter &p);
};

/* ==================================================================
 * Value-producing ops
 * ================================================================== */

class ConstOp : public mlir::Op<ConstOp, mlir::OpTrait::OneResult,
                                mlir::OpTrait::ZeroOperands,
                                mlir::OpTrait::ZeroRegions>,
                public ResultAccessors<ConstOp>,
                public OpBoilerplate<ConstOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ConstOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.const"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    mlir::Attribute value);
  mlir::Attribute getValue();
  void print(mlir::OpAsmPrinter &p);
};

class NullOp : public mlir::Op<NullOp, mlir::OpTrait::OneResult,
                               mlir::OpTrait::ZeroOperands,
                               mlir::OpTrait::ZeroRegions>,
                              public ResultAccessors<NullOp>,
               public OpBoilerplate<NullOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(NullOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.null"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    mlir::Type resultType);
  void print(mlir::OpAsmPrinter &p);
};

class AddressOfOp : public mlir::Op<AddressOfOp, mlir::OpTrait::OneResult,
                                   mlir::OpTrait::ZeroOperands,
                                   mlir::OpTrait::ZeroRegions>,
                                  public ResultAccessors<AddressOfOp>,
                   public OpBoilerplate<AddressOfOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(AddressOfOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.address_of"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    llvm::StringRef globalName, mlir::Type resultType);
  llvm::StringRef getGlobalName();
  void print(mlir::OpAsmPrinter &p);
};

/* All binary arithmetic/bitwise ops share one shape: two operands of the same
 * type, one result of that type. Concrete ops only supply the mnemonic. */
template <typename Concrete>
class BinaryOpBase
    : public mlir::Op<Concrete, mlir::OpTrait::OneResult,
                      mlir::OpTrait::NOperands<2>::Impl,
                      mlir::OpTrait::ZeroRegions>,
                      public ResultAccessors<Concrete>,
                      public OpBoilerplate<Concrete> {
 public:
  using Base = mlir::Op<Concrete, mlir::OpTrait::OneResult,
                        mlir::OpTrait::NOperands<2>::Impl,
                        mlir::OpTrait::ZeroRegions>;
  using Base::Base;
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    mlir::Value lhs, mlir::Value rhs) {
    state.addOperands({lhs, rhs});
    state.addTypes(lhs.getType());
  }
  mlir::Value getLhs() { return this->getOperand(0); }
  mlir::Value getRhs() { return this->getOperand(1); }
  void print(mlir::OpAsmPrinter &p) { printBinaryOp(p, this->getOperation()); }
  mlir::LogicalResult verify() {
    if (this->getOperand(0).getType() != this->getOperand(1).getType())
      return this->emitOpError("both operands must have the same type");
    if (this->getResult().getType() != this->getOperand(0).getType())
      return this->emitOpError("result type must match the operand type");
    return mlir::success();
  }
};

#define C1_BINARY_OP(NAME, MNEMONIC)                                    \
  class NAME : public BinaryOpBase<NAME> {                              \
   public:                                                              \
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(NAME)                  \
    using BinaryOpBase<NAME>::BinaryOpBase;                             \
    static llvm::StringRef getOperationName() { return MNEMONIC; }      \
  }

C1_BINARY_OP(AddOp, "c1.add");
C1_BINARY_OP(SubOp, "c1.sub");
C1_BINARY_OP(MulOp, "c1.mul");
C1_BINARY_OP(SDivOp, "c1.sdiv");
C1_BINARY_OP(UDivOp, "c1.udiv");
C1_BINARY_OP(SRemOp, "c1.srem");
C1_BINARY_OP(URemOp, "c1.urem");
C1_BINARY_OP(AndOp, "c1.and");
C1_BINARY_OP(OrOp, "c1.or");
C1_BINARY_OP(XOrOp, "c1.xor");
C1_BINARY_OP(ShlOp, "c1.shl");
C1_BINARY_OP(LShrOp, "c1.lshr");
C1_BINARY_OP(AShrOp, "c1.ashr");
C1_BINARY_OP(MulhiOp, "c1.mulhi");

#undef C1_BINARY_OP

class AllocaOp : public mlir::Op<AllocaOp, mlir::OpTrait::OneResult,
                                 mlir::OpTrait::ZeroOperands,
                                 mlir::OpTrait::ZeroRegions>,
                 public ResultAccessors<AllocaOp>,
                 public OpBoilerplate<AllocaOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(AllocaOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.alloca"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    mlir::Type resultType, int64_t alignment);
  int64_t getAlignment();
  void print(mlir::OpAsmPrinter &p);
};

class LoadOp : public mlir::Op<LoadOp, mlir::OpTrait::OneResult,
                               mlir::OpTrait::OneOperand,
                               mlir::OpTrait::ZeroRegions>,
               public ResultAccessors<LoadOp>,
               public OpBoilerplate<LoadOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LoadOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.load"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    mlir::Value ptr, mlir::Type resultType);
  void print(mlir::OpAsmPrinter &p);
};

class StoreOp : public mlir::Op<StoreOp, mlir::OpTrait::ZeroResults,
                               mlir::OpTrait::NOperands<2>::Impl,
                               mlir::OpTrait::ZeroRegions>,
                public OpBoilerplate<StoreOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(StoreOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.store"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    mlir::Value value, mlir::Value ptr);
  void print(mlir::OpAsmPrinter &p);
};

/* field @S, %p, i -- reuses the gep pattern, but the struct and the field
 * index are attributes because both are compile-time constants. */
class FieldOp : public mlir::Op<FieldOp, mlir::OpTrait::OneResult,
                               mlir::OpTrait::OneOperand,
                               mlir::OpTrait::ZeroRegions>,
                public ResultAccessors<FieldOp>,
                public OpBoilerplate<FieldOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FieldOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.field"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    llvm::StringRef structName, mlir::Value base,
                    int64_t fieldIndex, mlir::Type resultType);
  llvm::StringRef getStructName();
  int64_t getFieldIndex();
  void print(mlir::OpAsmPrinter &p);
};

/* Zero/sign extension and truncation share one shape: one operand, one
 * result, both types explicit. */
template <typename Concrete>
class CastOpBase
    : public mlir::Op<Concrete, mlir::OpTrait::OneResult,
                      mlir::OpTrait::OneOperand,
                      mlir::OpTrait::ZeroRegions>,
      public ResultAccessors<Concrete>,
      public OpBoilerplate<Concrete> {
 public:
  using Base = mlir::Op<Concrete, mlir::OpTrait::OneResult,
                        mlir::OpTrait::OneOperand,
                        mlir::OpTrait::ZeroRegions>;
  using Base::Base;
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    mlir::Value value, mlir::Type toType) {
    state.addOperands(value);
    state.addTypes(toType);
  }
  mlir::Value getValue() { return this->getOperation()->getOperand(0); }
  void print(mlir::OpAsmPrinter &p) {
    p << " " << getValue() << " : " << getValue().getType() << " to "
      << this->getResult().getType();
  }
};

#define C1_CAST_OP(NAME, MNEMONIC)                                     \
  class NAME : public CastOpBase<NAME> {                               \
   public:                                                             \
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(NAME)                 \
    using CastOpBase<NAME>::CastOpBase;                                \
    static llvm::StringRef getOperationName() { return MNEMONIC; }     \
  }

C1_CAST_OP(ZExtOp, "c1.zext");
C1_CAST_OP(SExtOp, "c1.sext");
C1_CAST_OP(TruncOp, "c1.trunc");

#undef C1_CAST_OP

/* Atomics. The pointer operand carries the value type in its pointee, so no
 * separate type attribute is needed. Orderings use the LLVM IR spelling:
 * monotonic, acquire, release, acq_rel, seq_cst. */
class AtomicLoadOp : public mlir::Op<AtomicLoadOp, mlir::OpTrait::OneResult,
                                     mlir::OpTrait::OneOperand,
                                     mlir::OpTrait::ZeroRegions>,
                     public ResultAccessors<AtomicLoadOp>,
                     public OpBoilerplate<AtomicLoadOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(AtomicLoadOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.atomic_load"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    mlir::Value ptr, llvm::StringRef ordering);
  llvm::StringRef getOrdering();
  void print(mlir::OpAsmPrinter &p);
};

class AtomicStoreOp : public mlir::Op<AtomicStoreOp,
                                      mlir::OpTrait::ZeroResults,
                                      mlir::OpTrait::NOperands<2>::Impl,
                                      mlir::OpTrait::ZeroRegions>,
                      public OpBoilerplate<AtomicStoreOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(AtomicStoreOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.atomic_store"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    mlir::Value value, mlir::Value ptr,
                    llvm::StringRef ordering);
  llvm::StringRef getOrdering();
  void print(mlir::OpAsmPrinter &p);
};

class AtomicRMwOp : public mlir::Op<AtomicRMwOp, mlir::OpTrait::OneResult,
                                    mlir::OpTrait::NOperands<2>::Impl,
                                    mlir::OpTrait::ZeroRegions>,
                    public ResultAccessors<AtomicRMwOp>,
                    public OpBoilerplate<AtomicRMwOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(AtomicRMwOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.atomicrmw"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    llvm::StringRef binop, mlir::Value ptr, mlir::Value value,
                    llvm::StringRef ordering);
  llvm::StringRef getBinOp();
  llvm::StringRef getOrdering();
  void print(mlir::OpAsmPrinter &p);
};

/* Result is the i1 success flag; the value type comes from the pointee. */
class CmpXchgOp : public mlir::Op<CmpXchgOp, mlir::OpTrait::OneResult,
                                  mlir::OpTrait::NOperands<3>::Impl,
                                  mlir::OpTrait::ZeroRegions>,
                   public ResultAccessors<CmpXchgOp>,
                   public OpBoilerplate<CmpXchgOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(CmpXchgOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.cmpxchg"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    mlir::Value ptr, mlir::Value cmp, mlir::Value newValue,
                    llvm::StringRef successOrdering,
                    llvm::StringRef failureOrdering);
  llvm::StringRef getSuccessOrdering();
  llvm::StringRef getFailureOrdering();
  void print(mlir::OpAsmPrinter &p);
};

/* Inline assembly with zero or one result. A struct result means several
 * outputs, one per '=' constraint in order. */
class InlineAsmOp : public mlir::Op<InlineAsmOp,
                                    mlir::OpTrait::VariadicResults,
                                    mlir::OpTrait::VariadicOperands,
                                    mlir::OpTrait::ZeroRegions>,
                    public OpBoilerplate<InlineAsmOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(InlineAsmOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.inline_asm"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    mlir::Type resultType, mlir::ValueRange operands,
                    llvm::StringRef text, llvm::StringRef constraints,
                    bool intelDialect, bool alignStack);
  llvm::StringRef getText();
  llvm::StringRef getConstraints();
  llvm::StringRef getDialect();
  bool isAlignStack();
  void print(mlir::OpAsmPrinter &p);
};

/* tensor <op>, <elemtype>, [d0, ...], *%dst, *%lhs, *%rhs -- the destination
 * is explicit and there is no result. The kernel symbol names the generated
 * linalg kernel this line calls. */
class TensorOp : public mlir::Op<TensorOp, mlir::OpTrait::ZeroResults,
                                 mlir::OpTrait::NOperands<3>::Impl,
                                 mlir::OpTrait::ZeroRegions>,
                 public OpBoilerplate<TensorOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(TensorOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.tensor"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    llvm::StringRef tensorOp, mlir::Type elemType,
                    llvm::ArrayRef<int64_t> dims, llvm::StringRef kernel,
                    mlir::Value dst, mlir::Value lhs, mlir::Value rhs);
  llvm::StringRef getTensorOp();
  mlir::Type getElemType();
  mlir::ArrayRef<int64_t> getDims();
  llvm::StringRef getKernel();
  void print(mlir::OpAsmPrinter &p);
};

class ICmpOp : public mlir::Op<ICmpOp, mlir::OpTrait::OneResult,
                               mlir::OpTrait::NOperands<2>::Impl,
                               mlir::OpTrait::ZeroRegions>,
                              public ResultAccessors<ICmpOp>,
               public OpBoilerplate<ICmpOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ICmpOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.icmp"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    llvm::StringRef predicate, mlir::Value lhs,
                    mlir::Value rhs);
  mlir::StringRef getPredicate();
  void print(mlir::OpAsmPrinter &p);
  mlir::LogicalResult verify();
};

class GepOp : public mlir::Op<GepOp, mlir::OpTrait::OneResult,
                              mlir::OpTrait::NOperands<2>::Impl,
                              mlir::OpTrait::ZeroRegions>,
                             public ResultAccessors<GepOp>,
              public OpBoilerplate<GepOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(GepOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.gep"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    mlir::Value base, mlir::Value index,
                    mlir::Type elemType, mlir::Type resultType);
  mlir::Type getElemType();
  void print(mlir::OpAsmPrinter &p);
};

class CallOp : public mlir::Op<CallOp, mlir::OpTrait::VariadicOperands,
                              mlir::OpTrait::VariadicResults,
                              mlir::OpTrait::ZeroRegions>,
                             public ResultAccessors<CallOp>,
               public OpBoilerplate<CallOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(CallOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.call"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    llvm::StringRef callee, mlir::FunctionType type,
                    mlir::ValueRange args);
  llvm::StringRef getCallee();
  mlir::FunctionType getCalleeType();
  void print(mlir::OpAsmPrinter &p);
};

/* ==================================================================
 * Terminators
 * ================================================================== */

class ReturnOp : public mlir::Op<ReturnOp, mlir::OpTrait::VariadicOperands,
                                 mlir::OpTrait::ZeroResults,
                                 mlir::OpTrait::ZeroRegions,
                                 mlir::OpTrait::IsTerminator>,
                 public OpBoilerplate<ReturnOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ReturnOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.return"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    mlir::ValueRange values);
  void print(mlir::OpAsmPrinter &p);
};

class BrOp : public mlir::Op<BrOp, mlir::OpTrait::VariadicOperands,
                             mlir::OpTrait::ZeroResults,
                             mlir::OpTrait::OneSuccessor,
                             mlir::OpTrait::IsTerminator>,
                 public OpBoilerplate<BrOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(BrOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.br"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    mlir::Block *dest, mlir::ValueRange args);
  void print(mlir::OpAsmPrinter &p);
  mlir::LogicalResult verify();
};

/* Operand layout: cond, true-branch args, false-branch args. The counts are
 * recoverable from the "true_args" attribute. */
class CondBrOp : public mlir::Op<CondBrOp, mlir::OpTrait::VariadicOperands,
                                 mlir::OpTrait::ZeroResults,
                                 mlir::OpTrait::NSuccessors<2>::Impl,
                                 mlir::OpTrait::IsTerminator>,
                 public OpBoilerplate<CondBrOp> {
 public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(CondBrOp)
  using Op::Op;
  static llvm::StringRef getOperationName() { return "c1.cond_br"; }
  static void build(mlir::OpBuilder &b, mlir::OperationState &state,
                    mlir::Value cond, mlir::Block *trueDest,
                    mlir::ValueRange trueArgs, mlir::Block *falseDest,
                    mlir::ValueRange falseArgs);
  mlir::Value getCond();
  mlir::ValueRange getTrueArgs();
  mlir::ValueRange getFalseArgs();
  void print(mlir::OpAsmPrinter &p);
  mlir::LogicalResult verify();
};

}  // namespace c1

#endif  // C1_HIR_C1_DIALECT_H
