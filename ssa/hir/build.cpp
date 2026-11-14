#include "build.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

#include "c1_dialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Verifier.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"

#include "../ast.h"

using namespace mlir;

namespace c1 {
namespace {

class AstToC1 {
 public:
  AstToC1(const Program *prog, MLIRContext &ctx, std::string &error)
      : ctx(ctx), b(&ctx), prog(prog), error(error) {}

  OwningOpRef<ModuleOp> build() {
    OwningOpRef<ModuleOp> module = ModuleOp::create(UnknownLoc::get(&ctx));
    moduleBlock = module->getBody();
    b.setInsertionPointToStart(moduleBlock);

    if (!declareStructs()) return {};
    if (!declareGlobals()) return {};
    if (!buildExterns()) return {};
    if (!buildFunctions()) return {};
    return module;
  }

 private:
  struct GlobalInfo {
    mlir::Type type;
    mlir::Attribute initializer;
  };

  MLIRContext &ctx;
  OpBuilder b;
  Block *moduleBlock = nullptr;
  const Program *prog;
  std::string &error;
  llvm::StringMap<GlobalInfo> globals;
  llvm::StringMap<FunctionType> functions;
  int tensorCount = 0;

  void fail(const std::string &msg) {
    if (error.empty()) error = msg;
  }

  Location locOf(const Insn *insn) {
    if (!insn || insn->loc.line <= 0) return UnknownLoc::get(&ctx);
    return FileLineColLoc::get(&ctx, prog->filename ? prog->filename : "<input>",
                               insn->loc.line, insn->loc.column);
  }

  /* ---------------------------------------------------------------- types */

  mlir::Type convertType(const AstNode *t) {
    if (!t) {
      fail("missing type");
      return {};
    }
    switch (t->kind) {
      case AST_TYPE_I1: return b.getI1Type();
      case AST_TYPE_I8: return b.getIntegerType(8);
      case AST_TYPE_I16: return b.getIntegerType(16);
      case AST_TYPE_I32: return b.getIntegerType(32);
      case AST_TYPE_I64: return b.getIntegerType(64);
      case AST_TYPE_F32: return b.getF32Type();
      case AST_TYPE_F64: return b.getF64Type();
      case AST_TYPE_PTR: {
        mlir::Type inner = convertType(t->ptr_to);
        if (!inner) return {};
        return PtrType::get(&ctx, inner);
      }
      case AST_TYPE_ARRAY: {
        mlir::Type elem = convertType(t->array.elem);
        if (!elem) return {};
        return ArrayType::get(&ctx, elem, t->array.size);
      }
      case AST_TYPE_STRUCT:
        return StructType::get(&ctx, t->struct_name);
      case AST_TYPE_VOID:
        fail("void is only valid as a function return type");
        return {};
      default:
        fail("unknown type kind");
        return {};
    }
  }

  FunctionType convertFunctionType(const AstNode *retType, const FuncParam *params,
                                   int paramCount, bool isVariadic) {
    llvm::SmallVector<mlir::Type> inputs, results;
    for (int i = 0; i < paramCount; i++) {
      mlir::Type t = convertType(params[i].type);
      if (!t) return {};
      inputs.push_back(t);
    }
    if (retType && retType->kind != AST_TYPE_VOID) {
      mlir::Type r = convertType(retType);
      if (!r) return {};
      results.push_back(r);
    }
    return FunctionType::get(&ctx, inputs, results);
  }

  /* -------------------------------------------------------------- structs */

  bool declareStructs() {
    for (int i = 0; i < prog->struct_count; i++) {
      const StructDecl *sd = &prog->structs[i];
      llvm::SmallVector<llvm::StringRef> fieldNames;
      llvm::SmallVector<mlir::Type> fieldTypes;
      for (int j = 0; j < sd->field_count; j++) {
        mlir::Type t = convertType(sd->fields[j].type);
        if (!t) return false;
        fieldNames.push_back(sd->fields[j].name);
        fieldTypes.push_back(t);
      }
      StructDefOp::create(b, UnknownLoc::get(&ctx), sd->name, fieldNames,
                          fieldTypes);
    }
    return true;
  }

  /* -------------------------------------------------------------- globals */

  bool declareGlobals() {
    for (int i = 0; i < prog->global_count; i++) {
      const GlobalDecl *gd = &prog->globals[i];
      mlir::Type ty = convertType(gd->type);
      if (!ty) return false;

      mlir::Attribute init;
      if (gd->array_init && gd->array_count > 0) {
        auto arrTy = dyn_cast<ArrayType>(ty);
        if (!arrTy) {
          fail(std::string("global @") + gd->name +
               " has an array initializer but is not an array");
          return false;
        }
        if (gd->array_count != arrTy.getSize()) {
          fail(std::string("global @") + gd->name + " initializer has " +
               std::to_string(gd->array_count) + " elements, expected " +
               std::to_string(arrTy.getSize()));
          return false;
        }
        llvm::SmallVector<Attribute> elems;
        for (int j = 0; j < gd->array_count; j++)
          elems.push_back(b.getIntegerAttr(arrTy.getElementType(),
                                           gd->array_init[j]));
        init = b.getArrayAttr(elems);
      } else if (gd->has_init) {
        switch (gd->init_value.kind) {
          case OPERAND_INT:
            init = b.getIntegerAttr(ty, gd->init_value.as.int_val);
            break;
          case OPERAND_FLOAT:
            init = b.getFloatAttr(ty, gd->init_value.as.float_val);
            break;
          case OPERAND_BOOL:
            init = b.getIntegerAttr(b.getI1Type(),
                                    gd->init_value.as.bool_val ? 1 : 0);
            break;
          default:
            fail(std::string("unsupported initializer for global @") + gd->name);
            return false;
        }
      }

      GlobalOp::create(b, UnknownLoc::get(&ctx), gd->name, ty, gd->is_const,
                       gd->is_internal, init);
      globals[gd->name] = GlobalInfo{ty, init};
    }
    return true;
  }

  /* -------------------------------------------------------------- externs */

  bool buildExterns() {
    llvm::StringSet<> emitted;
    for (int i = 0; i < prog->extern_count; i++) {
      const ExternDecl *ext = &prog->externs[i];
      FunctionType ty = convertFunctionType(ext->return_type, ext->params,
                                            ext->param_count, ext->is_variadic);
      if (!ty) return false;
      functions[ext->name] = ty;
      /* MLIR symbols are unique per module: a declaration for a function
       * defined in another input file, or a repeated declaration, must not
       * produce a second c1.extern. */
      if (emitted.contains(ext->name)) continue;
      bool defined = false;
      for (int j = 0; j < prog->func_count; j++)
        if (llvm::StringRef(prog->functions[j].name) == ext->name) {
          defined = true;
          break;
        }
      if (defined) continue;
      emitted.insert(ext->name);
      ExternOp::create(b, UnknownLoc::get(&ctx), ext->name, ty,
                       ext->is_variadic);
    }
    return true;
  }

  /* ------------------------------------------------------------ functions */

  bool buildFunctions() {
    for (int i = 0; i < prog->func_count; i++) {
      const FuncDecl *fn = &prog->functions[i];
      FunctionType ty = convertFunctionType(fn->return_type, fn->params,
                                            fn->param_count, false);
      if (!ty) return false;
      functions[fn->name] = ty;
    }

    for (int i = 0; i < prog->func_count; i++) {
      if (!buildFunction(&prog->functions[i])) return false;
    }
    return true;
  }

  struct FnCtx {
    llvm::StringMap<Block *> blocks;
    llvm::StringMap<Value> values;
    llvm::StringMap<llvm::SmallVector<const Insn *>> phis;
    mlir::Type retType;
    llvm::StringRef name;
  };

  static bool isTerminator(InsnKind kind) {
    return kind == INSN_JMP || kind == INSN_BR || kind == INSN_RET;
  }

  bool buildFunction(const FuncDecl *fn) {
    b.setInsertionPointToEnd(moduleBlock);
    FunctionType fnType = functions[fn->name];
    llvm::SmallVector<llvm::StringRef> argNames;
    for (int i = 0; i < fn->param_count; i++) argNames.push_back(fn->params[i].name);

    /* A real location here is what gives the function a DWARF subprogram. */
    Location fnLoc = fn->loc.line > 0
                         ? Location(FileLineColLoc::get(
                               &ctx, prog->filename ? prog->filename : "<input>",
                               fn->loc.line, fn->loc.column))
                         : Location(UnknownLoc::get(&ctx));
    FuncOp func = FuncOp::create(b, fnLoc, fn->name, fnType, argNames);
    mlir::Region &body = func.getBody();

    FnCtx fnc;
    fnc.name = func.getName();
    fnc.retType = fnType.getResults().empty()
                      ? mlir::Type()
                      : fnType.getResults().front();

    for (int i = 0; i < fn->block_count; i++) {
      BasicBlock *src = &fn->blocks[i];
      Block *block = new Block();
      body.push_back(block);
      if (i == 0) {
        for (mlir::Type t : fnType.getInputs())
          block->addArgument(t, UnknownLoc::get(&ctx));
        for (int j = 0; j < fn->param_count; j++)
          fnc.values[fn->params[j].name] = block->getArgument(j);
      }
      fnc.blocks[src->label] = block;
    }

    /* Phi nodes become block arguments, in source order. */
    for (int i = 0; i < fn->block_count; i++) {
      BasicBlock *src = &fn->blocks[i];
      Block *block = fnc.blocks[src->label];
      for (int j = 0; j < src->insn_count; j++) {
        const Insn *insn = &src->insns[j];
        if (insn->kind != INSN_PHI || !insn->result) continue;
        if (i == 0) {
          fail("in @" + fnc.name.str() +
               ": phi is not supported in the entry block");
          return false;
        }
        mlir::Type ty = insn->type ? convertType(insn->type) : b.getI32Type();
        if (!ty) return false;
        block->addArgument(ty, locOf(insn));
        fnc.values[insn->result] =
            block->getArgument(block->getNumArguments() - 1);
        fnc.phis[src->label].push_back(insn);
      }
    }

    /* Non-terminators run first so a branch terminator can look up any value
     * it forwards to a phi argument, regardless of block order. */
    for (int i = 0; i < fn->block_count; i++) {
      BasicBlock *src = &fn->blocks[i];
      b.setInsertionPointToEnd(fnc.blocks[src->label]);
      for (int j = 0; j < src->insn_count; j++) {
        const Insn *insn = &src->insns[j];
        if (isTerminator(insn->kind) || insn->kind == INSN_PHI) continue;
        if (!emitInsn(fnc, insn, src->label)) return false;
      }
    }

    for (int i = 0; i < fn->block_count; i++) {
      BasicBlock *src = &fn->blocks[i];
      b.setInsertionPointToEnd(fnc.blocks[src->label]);
      for (int j = 0; j < src->insn_count; j++) {
        const Insn *insn = &src->insns[j];
        if (!isTerminator(insn->kind)) continue;
        if (!emitInsn(fnc, insn, src->label)) return false;
      }
    }
    return true;
  }

  bool appendPhiArgs(FnCtx &fnc, const char *target, llvm::StringRef from,
                     llvm::SmallVectorImpl<Value> &args) {
    auto it = fnc.phis.find(target);
    if (it == fnc.phis.end()) return true;
    for (const Insn *phi : it->second) {
      mlir::Type ty = phi->type ? convertType(phi->type) : b.getI32Type();
      if (!ty) return false;
      const PhiCase *incoming = nullptr;
      for (int i = 0; i < phi->data.phi.count; i++) {
        const PhiCase *pc = &phi->data.phi.cases[i];
        if (pc->label && from == pc->label) {
          incoming = pc;
          break;
        }
      }
      if (!incoming) {
        fail("in @" + fnc.name.str() + ": phi has no incoming value from @" +
             from.str() + " for @" + target);
        return false;
      }
      Value v = resolveOperand(incoming->value, ty, fnc.values, fnc.name, phi);
      if (!v) return false;
      args.push_back(v);
    }
    return true;
  }

  void bindResult(const Insn *insn, mlir::Value value,
                  llvm::StringMap<Value> &values) {
    if (insn->result && value) values[insn->result] = value;
  }

  mlir::Value resolveOperand(const Operand &op, mlir::Type expected,
                             llvm::StringMap<Value> &values,
                             llvm::StringRef funcName, const Insn *insn) {
    switch (op.kind) {
      case OPERAND_LOCAL: {
        auto it = values.find(op.as.local_name);
        if (it == values.end()) {
          fail("in @" + funcName.str() + ": undefined local %" +
               op.as.local_name);
          return {};
        }
        return it->second;
      }
      case OPERAND_GLOBAL: {
        auto it = globals.find(op.as.global_name);
        if (it == globals.end()) {
          fail("in @" + funcName.str() + ": undefined global @" +
               op.as.global_name);
          return {};
        }
        const GlobalInfo &gi = it->second;
        if (expected && !isa<PtrType>(expected) &&
            (isa<IntegerType>(gi.type) || isa<FloatType>(gi.type))) {
          if (gi.type != expected) {
            fail("in @" + funcName.str() + ": global @" + op.as.global_name +
                 " has type " + typeToString(gi.type) + ", expected " +
                 typeToString(expected));
            return {};
          }
          if (!gi.initializer) {
            fail("in @" + funcName.str() + ": global @" + op.as.global_name +
                 " has no initializer");
            return {};
          }
          return ConstOp::create(b, locOf(insn), gi.initializer).getResult();
        }
        return AddressOfOp::create(b, locOf(insn), op.as.global_name,
                                   PtrType::get(&ctx, gi.type))
            .getResult();
      }
      case OPERAND_INT: {
        if (!isa<IntegerType>(expected)) {
          fail("in @" + funcName.str() + ": integer literal used where " +
               typeToString(expected) + " is expected");
          return {};
        }
        return ConstOp::create(b, locOf(insn),
                               b.getIntegerAttr(expected, op.as.int_val))
            .getResult();
      }
      case OPERAND_FLOAT: {
        if (!isa<FloatType>(expected)) {
          fail("in @" + funcName.str() + ": float literal used where " +
               typeToString(expected) + " is expected");
          return {};
        }
        return ConstOp::create(b, locOf(insn),
                               b.getFloatAttr(expected, op.as.float_val))
            .getResult();
      }
      case OPERAND_BOOL:
        return ConstOp::create(
                   b, locOf(insn),
                   b.getIntegerAttr(b.getI1Type(), op.as.bool_val ? 1 : 0))
            .getResult();
      case OPERAND_NULL: {
        if (!expected || !isa<PtrType>(expected)) {
          fail("in @" + funcName.str() +
               ": null used where a pointer is expected");
          return {};
        }
        return NullOp::create(b, locOf(insn), expected).getResult();
      }
    }
    fail("in @" + funcName.str() + ": unknown operand kind");
    return {};
  }

  static std::string typeToString(mlir::Type t) {
    std::string s;
    llvm::raw_string_ostream os(s);
    t.print(os);
    return s;
  }

  static llvm::StringRef orderingName(MemoryOrdering ordering) {
    switch (ordering) {
      case ORDERING_SEQ_CST: return "seq_cst";
      case ORDERING_ACQUIRE: return "acquire";
      case ORDERING_RELEASE: return "release";
      case ORDERING_ACQ_REL: return "acq_rel";
      case ORDERING_RELAXED: return "monotonic";
    }
    return "seq_cst";
  }

  static llvm::StringRef atomicOpName(int atomicOp) {
    switch (atomicOp) {
      case ATOMICOP_XCHG: return "xchg";
      case ATOMICOP_ADD: return "add";
      case ATOMICOP_SUB: return "sub";
      case ATOMICOP_AND: return "and";
      case ATOMICOP_OR: return "or";
      case ATOMICOP_XOR: return "xor";
    }
    return "";
  }

  static llvm::StringRef tensorOpName(int tensorOp) {
    switch (tensorOp) {
      case TENSOR_OP_ADD: return "add";
      case TENSOR_OP_SUB: return "sub";
      case TENSOR_OP_MUL: return "mul";
      case TENSOR_OP_DIV: return "div";
      case TENSOR_OP_MATMUL: return "matmul";
    }
    return "";
  }

  static llvm::StringRef predicateFor(InsnKind kind) {
    switch (kind) {
      case INSN_EQ: return "eq";
      case INSN_NE: return "ne";
      case INSN_SLT: return "slt";
      case INSN_SLE: return "sle";
      case INSN_SGT: return "sgt";
      case INSN_SGE: return "sge";
      default: return "eq";
    }
  }

  static bool isBinary(InsnKind kind) {
    switch (kind) {
      case INSN_ADD: case INSN_SUB: case INSN_MUL: case INSN_DIV:
      case INSN_REM: case INSN_AND: case INSN_OR: case INSN_XOR:
      case INSN_SHL: case INSN_LSHR: case INSN_ASHR:
      case INSN_UDIV: case INSN_UREM: case INSN_MULHI:
        return true;
      default:
        return false;
    }
  }

  mlir::Value createBinary(InsnKind kind, mlir::Value l, mlir::Value r,
                           Location loc) {
    switch (kind) {
      case INSN_ADD: return AddOp::create(b, loc, l, r).getResult();
      case INSN_SUB: return SubOp::create(b, loc, l, r).getResult();
      case INSN_MUL: return MulOp::create(b, loc, l, r).getResult();
      case INSN_DIV: return SDivOp::create(b, loc, l, r).getResult();
      case INSN_REM: return SRemOp::create(b, loc, l, r).getResult();
      case INSN_UDIV: return UDivOp::create(b, loc, l, r).getResult();
      case INSN_UREM: return URemOp::create(b, loc, l, r).getResult();
      case INSN_MULHI: return MulhiOp::create(b, loc, l, r).getResult();
      case INSN_AND: return AndOp::create(b, loc, l, r).getResult();
      case INSN_OR: return OrOp::create(b, loc, l, r).getResult();
      case INSN_XOR: return XOrOp::create(b, loc, l, r).getResult();
      case INSN_SHL: return ShlOp::create(b, loc, l, r).getResult();
      case INSN_LSHR: return LShrOp::create(b, loc, l, r).getResult();
      case INSN_ASHR: return AShrOp::create(b, loc, l, r).getResult();
      default: return {};
    }
  }

  bool findFieldType(llvm::StringRef structName, int64_t index,
                     mlir::Type &out) {
    for (int i = 0; i < prog->struct_count; i++) {
      const StructDecl *sd = &prog->structs[i];
      if (structName != sd->name) continue;
      if (index < 0 || index >= sd->field_count) {
        fail("field index " + std::to_string(index) + " is out of range for @" +
             structName.str());
        return false;
      }
      out = convertType(sd->fields[index].type);
      return out != mlir::Type();
    }
    fail("unknown struct @" + structName.str());
    return false;
  }

  bool emitInsn(FnCtx &fnc, const Insn *insn, llvm::StringRef from) {
    Location loc = locOf(insn);
    llvm::StringMap<Value> &values = fnc.values;
    llvm::StringMap<Block *> &blocks = fnc.blocks;
    llvm::StringRef funcName = fnc.name;

    if (isBinary(insn->kind)) {
      mlir::Type ty = convertType(insn->type);
      if (!ty) return false;
      Value l = resolveOperand(insn->data.binary.lhs, ty, values, funcName, insn);
      if (!l) return false;
      Value r = resolveOperand(insn->data.binary.rhs, ty, values, funcName, insn);
      if (!r) return false;
      Value v = createBinary(insn->kind, l, r, loc);
      if (!v) {
        fail("in @" + funcName.str() + ": unsupported binary instruction");
        return false;
      }
      bindResult(insn, v, values);
      return true;
    }

    switch (insn->kind) {
      case INSN_LET: {
        mlir::Type elem = convertType(insn->type);
        if (!elem) return false;
        mlir::Type ptrTy = PtrType::get(&ctx, elem);
        Value v = AllocaOp::create(b, loc, ptrTy, insn->data.let.align).getResult();
        bindResult(insn, v, values);
        return true;
      }
      case INSN_LOAD: {
        mlir::Type ty = convertType(insn->type);
        if (!ty) return false;
        Value ptr = resolveOperand(insn->data.load.ptr,
                                   PtrType::get(&ctx, ty), values, funcName,
                                   insn);
        if (!ptr) return false;
        Value v = LoadOp::create(b, loc, ptr, ty).getResult();
        bindResult(insn, v, values);
        return true;
      }
      case INSN_STORE: {
        mlir::Type ty = convertType(insn->type);
        if (!ty) return false;
        Value val = resolveOperand(insn->data.store.value, ty, values,
                                   funcName, insn);
        if (!val) return false;
        Value ptr = resolveOperand(insn->data.store.ptr,
                                   PtrType::get(&ctx, ty), values, funcName,
                                   insn);
        if (!ptr) return false;
        StoreOp::create(b, loc, val, ptr);
        return true;
      }
      case INSN_LOAD_ATOMIC: {
        mlir::Type ty = convertType(insn->type);
        if (!ty) return false;
        Value ptr = resolveOperand(insn->data.load_atomic.ptr,
                                   PtrType::get(&ctx, ty), values, funcName,
                                   insn);
        if (!ptr) return false;
        Value v = AtomicLoadOp::create(b, loc, ptr,
                                       orderingName(insn->data.load_atomic.ordering))
                      .getResult();
        bindResult(insn, v, values);
        return true;
      }
      case INSN_STORE_ATOMIC: {
        mlir::Type ty = convertType(insn->type);
        if (!ty) return false;
        Value val = resolveOperand(insn->data.store_atomic.value, ty, values,
                                   funcName, insn);
        if (!val) return false;
        Value ptr = resolveOperand(insn->data.store_atomic.ptr,
                                   PtrType::get(&ctx, ty), values, funcName,
                                   insn);
        if (!ptr) return false;
        AtomicStoreOp::create(
            b, loc, val, ptr, orderingName(insn->data.store_atomic.ordering));
        return true;
      }
      case INSN_ATOMICRMW: {
        mlir::Type ty = convertType(insn->type);
        if (!ty) return false;
        llvm::StringRef binop = atomicOpName(insn->data.atomicrmw.atomic_op);
        if (binop.empty()) {
          fail("in @" + funcName.str() + ": invalid atomicrmw op");
          return false;
        }
        Value ptr = resolveOperand(insn->data.atomicrmw.ptr,
                                   PtrType::get(&ctx, ty), values, funcName,
                                   insn);
        if (!ptr) return false;
        Value val = resolveOperand(insn->data.atomicrmw.value, ty, values,
                                   funcName, insn);
        if (!val) return false;
        Value v = AtomicRMwOp::create(b, loc, binop, ptr, val,
                                      orderingName(insn->data.atomicrmw.ordering))
                      .getResult();
        bindResult(insn, v, values);
        return true;
      }
      case INSN_CMPXCHG: {
        mlir::Type ty = convertType(insn->type);
        if (!ty) return false;
        Value ptr = resolveOperand(insn->data.cmpxchg.ptr,
                                   PtrType::get(&ctx, ty), values, funcName,
                                   insn);
        if (!ptr) return false;
        Value cmp = resolveOperand(insn->data.cmpxchg.cmp, ty, values,
                                   funcName, insn);
        if (!cmp) return false;
        Value newVal = resolveOperand(insn->data.cmpxchg.new_val, ty, values,
                                      funcName, insn);
        if (!newVal) return false;
        Value v = CmpXchgOp::create(
                      b, loc, ptr, cmp, newVal,
                      orderingName(insn->data.cmpxchg.succ_ordering),
                      orderingName(insn->data.cmpxchg.fail_ordering))
                      .getResult();
        bindResult(insn, v, values);
        return true;
      }
      case INSN_ASM: {
        mlir::Type resultType;
        if (insn->type && insn->type->kind != AST_TYPE_VOID) {
          resultType = convertType(insn->type);
          if (!resultType) return false;
        }
        if (insn->result && !resultType) {
          fail("in @" + funcName.str() + ": asm with a result needs a type");
          return false;
        }

        int outputs = 0;
        for (const char *c = insn->data.asm_insn.constraints; *c; ) {
          if (*c == '=') outputs++;
          while (*c && *c != ',') c++;
          if (*c == ',') c++;
        }
        if (auto st = resultType ? dyn_cast<StructType>(resultType)
                                 : StructType()) {
          int fields = -1;
          for (int i = 0; i < prog->struct_count; i++)
            if (st.getStructName() == prog->structs[i].name)
              fields = prog->structs[i].field_count;
          if (fields >= 0 && fields != outputs) {
            fail("in @" + funcName.str() + ": asm struct result @" +
                 st.getStructName().str() + " has " + std::to_string(fields) +
                 " field(s) but " + std::to_string(outputs) +
                 " output constraint(s)");
            return false;
          }
        }

        llvm::SmallVector<Value> operands;
        for (int i = 0; i < insn->data.asm_insn.operand_count; i++) {
          mlir::Type ty = convertType(insn->data.asm_insn.operand_types[i]);
          if (!ty) return false;
          Value operand = resolveOperand(insn->data.asm_insn.operands[i], ty,
                                         values, funcName, insn);
          if (!operand) return false;
          operands.push_back(operand);
        }

        InlineAsmOp asmOp = InlineAsmOp::create(
            b, loc, resultType, operands, insn->data.asm_insn.text,
            insn->data.asm_insn.constraints, insn->data.asm_insn.intel_dialect,
            insn->data.asm_insn.align_stack);
        if (asmOp.getNumResults() == 1) bindResult(insn, asmOp->getResult(0), values);
        return true;
      }
      case INSN_TENSOR: {
        mlir::Type elem = convertType(insn->data.tensor.elem_type);
        if (!elem) return false;
        if (!elem.isF32()) {
          fail("in @" + funcName.str() + ": tensor currently supports only f32");
          return false;
        }
        llvm::SmallVector<int64_t> dims;
        for (int i = 0; i < insn->data.tensor.ndim; i++) {
          const Operand &dim = insn->data.tensor.dims[i];
          if (dim.kind != OPERAND_INT || dim.as.int_val <= 0) {
            fail("in @" + funcName.str() +
                 ": tensor shape must be positive integer literals");
            return false;
          }
          dims.push_back(dim.as.int_val);
        }
        mlir::Type ptrTy = PtrType::get(&ctx, elem);
        Value dst = resolveOperand(insn->data.tensor.dst, ptrTy, values,
                                   funcName, insn);
        Value lhs = resolveOperand(insn->data.tensor.lhs, ptrTy, values,
                                   funcName, insn);
        Value rhs = resolveOperand(insn->data.tensor.rhs, ptrTy, values,
                                   funcName, insn);
        if (!dst || !lhs || !rhs) return false;
        std::string kernel = "__c1_tensor_" + funcName.str() + "_" +
                             std::to_string(tensorCount++);
        TensorOp::create(b, loc, tensorOpName(insn->data.tensor.op), elem, dims,
                         kernel, dst, lhs, rhs);
        return true;
      }
      case INSN_FIELD: {
        const AstNode *stNode = insn->data.field.struct_type;
        if (!stNode || stNode->kind != AST_TYPE_STRUCT) {
          fail("in @" + funcName.str() + ": field needs a struct type");
          return false;
        }
        const Operand &fi = insn->data.field.field_index;
        if (fi.kind != OPERAND_INT) {
          fail("in @" + funcName.str() +
               ": field index must be an integer constant");
          return false;
        }
        mlir::Type fieldTy;
        if (!findFieldType(stNode->struct_name, fi.as.int_val, fieldTy))
          return false;
        StructType structTy = StructType::get(&ctx, stNode->struct_name);
        Value base = resolveOperand(insn->data.field.base,
                                    PtrType::get(&ctx, structTy), values,
                                    funcName, insn);
        if (!base) return false;
        Value v = FieldOp::create(b, loc, stNode->struct_name, base,
                                  fi.as.int_val, PtrType::get(&ctx, fieldTy))
                      .getResult();
        bindResult(insn, v, values);
        return true;
      }
      case INSN_ZEXT: case INSN_SEXT: case INSN_TRUNC: {
        mlir::Type from = convertType(insn->data.convert.from_type);
        mlir::Type to = convertType(insn->data.convert.to_type);
        if (!from || !to) return false;
        Value arg = resolveOperand(insn->data.convert.value, from, values,
                                   funcName, insn);
        if (!arg) return false;
        Value v;
        if (insn->kind == INSN_ZEXT)
          v = ZExtOp::create(b, loc, arg, to).getResult();
        else if (insn->kind == INSN_SEXT)
          v = SExtOp::create(b, loc, arg, to).getResult();
        else
          v = TruncOp::create(b, loc, arg, to).getResult();
        bindResult(insn, v, values);
        return true;
      }
      case INSN_EQ: case INSN_NE: case INSN_SLT: case INSN_SLE:
      case INSN_SGT: case INSN_SGE: {
        mlir::Type ty = convertType(insn->type);
        if (!ty) return false;
        Value l = resolveOperand(insn->data.binary.lhs, ty, values, funcName, insn);
        if (!l) return false;
        Value r = resolveOperand(insn->data.binary.rhs, ty, values, funcName, insn);
        if (!r) return false;
        Value v = ICmpOp::create(b, loc, predicateFor(insn->kind), l, r)
                      .getResult();
        bindResult(insn, v, values);
        return true;
      }
      case INSN_GEP: {
        mlir::Type elem = convertType(insn->type);
        if (!elem) return false;
        mlir::Type ptrTy = PtrType::get(&ctx, elem);
        Value base = resolveOperand(insn->data.gep.base, ptrTy, values,
                                    funcName, insn);
        if (!base) return false;
        Value index = resolveOperand(insn->data.gep.index, b.getI32Type(),
                                     values, funcName, insn);
        if (!index) return false;
        Value v = GepOp::create(b, loc, base, index, elem, ptrTy).getResult();
        bindResult(insn, v, values);
        return true;
      }
      case INSN_CALL: {
        auto it = functions.find(insn->data.call.func);
        if (it == functions.end()) {
          fail("in @" + funcName.str() + ": undefined function @" +
               insn->data.call.func);
          return false;
        }
        FunctionType calleeTy = it->second;
        llvm::SmallVector<Value> args;
        for (int i = 0; i < insn->data.call.argc; i++) {
          mlir::Type expected = i < (int)calleeTy.getInputs().size()
                                    ? calleeTy.getInput(i)
                                    : b.getI32Type();
          Value arg = resolveOperand(insn->data.call.args[i], expected, values,
                                     funcName, insn);
          if (!arg) return false;
          args.push_back(arg);
        }
        if (calleeTy.getResults().empty() && insn->result) {
          fail("in @" + funcName.str() + ": @" + insn->data.call.func +
               " returns nothing but a result is assigned");
          return false;
        }
        CallOp call =
            CallOp::create(b, loc, insn->data.call.func, calleeTy, args);
        if (call->getNumResults() == 1) bindResult(insn, call->getResult(0), values);
        return true;
      }
      case INSN_JMP: {
        auto it = blocks.find(insn->data.jmp.target);
        if (it == blocks.end()) {
          fail("in @" + funcName.str() + ": undefined label @" +
               insn->data.jmp.target);
          return false;
        }
        llvm::SmallVector<Value> args;
        if (!appendPhiArgs(fnc, insn->data.jmp.target, from, args)) return false;
        BrOp::create(b, loc, it->second, args);
        return true;
      }
      case INSN_BR: {
        Value cond = resolveOperand(insn->data.br.cond, b.getI1Type(), values,
                                    funcName, insn);
        if (!cond) return false;
        auto trueIt = blocks.find(insn->data.br.true_target);
        auto falseIt = blocks.find(insn->data.br.false_target);
        if (trueIt == blocks.end() || falseIt == blocks.end()) {
          fail("in @" + funcName.str() + ": undefined branch target");
          return false;
        }
        llvm::SmallVector<Value> trueArgs, falseArgs;
        if (!appendPhiArgs(fnc, insn->data.br.true_target, from, trueArgs))
          return false;
        if (!appendPhiArgs(fnc, insn->data.br.false_target, from, falseArgs))
          return false;
        CondBrOp::create(b, loc, cond, trueIt->second, trueArgs,
                         falseIt->second, falseArgs);
        return true;
      }
      case INSN_RET: {
        if (insn->data.ret.is_void) {
          ReturnOp::create(b, loc, ValueRange{});
          return true;
        }
        Value v = resolveOperand(insn->data.ret.value, fnc.retType, values,
                                 funcName, insn);
        if (!v) return false;
        ReturnOp::create(b, loc, ValueRange{v});
        return true;
      }
      case INSN_PHI:
        fail("in @" + funcName.str() +
             ": phi must be the first instruction of a non-entry block");
        return false;
      default:
        fail("in @" + funcName.str() + ": instruction kind " +
             std::to_string((int)insn->kind) +
             " is not supported by the MLIR backend yet");
        return false;
    }
  }
};

}  // namespace

OwningOpRef<ModuleOp> buildModule(const Program *prog, MLIRContext &ctx,
                                  std::string &error) {
  AstToC1 builder(prog, ctx, error);
  OwningOpRef<ModuleOp> module = builder.build();
  if (!module || !error.empty()) return {};
  if (failed(verify(*module))) {
    if (error.empty()) error = "c1 dialect module failed verification";
    return {};
  }
  return module;
}

}  // namespace c1
