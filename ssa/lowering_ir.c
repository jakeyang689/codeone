#include "lowering_ir.h"
#include "hir/tensor.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <llvm-c/Core.h>
#include <llvm-c/Linker.h>
#include <llvm-c/Types.h>

/* Module name for the generated LLVM IR */
#define LLVM_MODULE_NAME "codeone_ssa_module"

/* ==================================================================
 * Value name -> LLVMValueRef mapping
 * ================================================================== */

typedef struct {
    char *name;
    LLVMValueRef value;
} ValueEntry;

typedef struct {
    ValueEntry *entries;
    int count;
    int capacity;
} ValueMap;

static void vm_init(ValueMap *vm) {
    vm->entries = NULL; vm->count = 0; vm->capacity = 0;
}

static void vm_add(ValueMap *vm, const char *name, LLVMValueRef val) {
    if (vm->count >= vm->capacity) {
        vm->capacity = vm->capacity ? vm->capacity * 2 : 64;
        vm->entries = (ValueEntry *)realloc(
            vm->entries, (size_t)vm->capacity * sizeof(ValueEntry));
    }
    vm->entries[vm->count].name = name ? strdup(name) : NULL;
    vm->entries[vm->count].value = val;
    vm->count++;
}

static LLVMValueRef vm_get(const ValueMap *vm, const char *name) {
    for (int i = 0; i < vm->count; i++)
        if (vm->entries[i].name && strcmp(vm->entries[i].name, name) == 0)
            return vm->entries[i].value;
    return NULL;
}

static void vm_free(ValueMap *vm) {
    for (int i = 0; i < vm->count; i++) free(vm->entries[i].name);
    free(vm->entries);
}

/* ==================================================================
 * Struct type map — maps struct name -> LLVM named struct type.
 * Populated at the start of lower_to_llvm_module and used by lower_type.
 * ================================================================== */

static LLVMTypeRef *g_struct_types = NULL;
static const char **g_struct_type_names = NULL;
static int g_struct_type_count = 0;

static LLVMTypeRef lower_get_struct_type(const char *name) {
    for (int i = 0; i < g_struct_type_count; i++) {
        if (strcmp(g_struct_type_names[i], name) == 0)
            return g_struct_types[i];
    }
    return NULL;
}

static void lower_struct_type_map_clear(void) {
    free(g_struct_types); g_struct_types = NULL;
    free(g_struct_type_names); g_struct_type_names = NULL;
    g_struct_type_count = 0;
}

/* ==================================================================
 * Ordering helper
 * ================================================================== */

static LLVMAtomicOrdering lower_ordering(MemoryOrdering o) {
    switch (o) {
        case ORDERING_SEQ_CST:  return LLVMAtomicOrderingSequentiallyConsistent;
        case ORDERING_ACQUIRE:  return LLVMAtomicOrderingAcquire;
        case ORDERING_RELEASE:  return LLVMAtomicOrderingRelease;
        case ORDERING_ACQ_REL:  return LLVMAtomicOrderingAcquireRelease;
        case ORDERING_RELAXED:  return LLVMAtomicOrderingMonotonic;
        default:                return LLVMAtomicOrderingSequentiallyConsistent;
    }
}

/* ==================================================================
 * Helpers
 * ================================================================== */

static void set_err(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, size, fmt, ap);
    va_end(ap);
}

/* ==================================================================
 * Type lowering
 * ================================================================== */

static LLVMTypeRef lower_type(LLVMContextRef ctx, const AstNode *t,
                               char *err, size_t err_sz) {
    if (!t) { set_err(err, err_sz, "null type"); return NULL; }
    switch (t->kind) {
        case AST_TYPE_I1:   return LLVMInt1TypeInContext(ctx);
        case AST_TYPE_I8:   return LLVMInt8TypeInContext(ctx);
        case AST_TYPE_I16:  return LLVMInt16TypeInContext(ctx);
        case AST_TYPE_I32:  return LLVMInt32TypeInContext(ctx);
        case AST_TYPE_I64:  return LLVMInt64TypeInContext(ctx);
        case AST_TYPE_F32:  return LLVMFloatTypeInContext(ctx);
        case AST_TYPE_F64:  return LLVMDoubleTypeInContext(ctx);
        case AST_TYPE_VOID: return LLVMVoidTypeInContext(ctx);
        case AST_TYPE_PTR: {
            LLVMTypeRef inner = lower_type(ctx, t->ptr_to, err, err_sz);
            return inner ? LLVMPointerType(inner, 0) : NULL;
        }
        case AST_TYPE_ARRAY: {
            LLVMTypeRef elem = lower_type(ctx, t->array.elem, err, err_sz);
            return elem ? LLVMArrayType(elem, (unsigned)t->array.size) : NULL;
        }
        case AST_TYPE_STRUCT: {
            LLVMTypeRef st = lower_get_struct_type(t->struct_name);
            if (!st) {
                set_err(err, err_sz, "unknown struct type @%s", t->struct_name);
                return NULL;
            }
            return st;
        }
        default:
            set_err(err, err_sz, "unknown type kind %d", t->kind);
            return NULL;
    }
}

/* ==================================================================
 * Operand lowering
 * ================================================================== */

/* Integer literals are two's-complement bit patterns of the target width.
   LLVMConstInt rejects values that do not fit the TypeRef, so mask first:
   0x80000000 in i32 and -1 in i32 both become valid bit patterns. */
static LLVMValueRef const_int(LLVMTypeRef type, int64_t value) {
    unsigned bits = LLVMGetIntTypeWidth(type);
    unsigned long long v = (unsigned long long)value;
    if (bits < 64) v &= (1ULL << bits) - 1;
    return LLVMConstInt(type, v, false);
}

static LLVMValueRef lower_op(LLVMContextRef ctx, LLVMModuleRef mod,
                              const ValueMap *vm, const Operand *op,
                              LLVMTypeRef expected, char *err, size_t err_sz) {
    switch (op->kind) {
        case OPERAND_LOCAL: {
            LLVMValueRef v = vm_get(vm, op->as.local_name);
            if (!v)
                set_err(err, err_sz, "undefined local %%%s", op->as.local_name);
            return v;
        }
        case OPERAND_GLOBAL: {
            LLVMValueRef g = LLVMGetNamedGlobal(mod, op->as.global_name);
            if (!g) {
                set_err(err, err_sz, "undefined global @%s (check spelling, "
                        "or ensure the defining .bx file is included in compilation)",
                        op->as.global_name);
                return NULL;
            }
            /* When the caller expects a non-pointer value (not the address of the
               global), and the global holds a scalar constant (not array/struct),
               return the compile-time constant value directly instead of the
               global variable pointer.  This fixes passing const i32 values
               to call arguments and arithmetic operands. */
            if (expected && LLVMGetTypeKind(expected) != LLVMPointerTypeKind) {
                LLVMTypeRef gv_type = LLVMGlobalGetValueType(g);
                if (gv_type) {
                    LLVMTypeKind gvk = LLVMGetTypeKind(gv_type);
                    if (gvk != LLVMPointerTypeKind &&
                        gvk != LLVMArrayTypeKind &&
                        gvk != LLVMStructTypeKind) {
                        LLVMValueRef init = LLVMGetInitializer(g);
                        if (init) {
                            /* A mismatched scalar type (e.g. an i32 enum
                               member used in an i64 instruction) would build
                               invalid IR, so reject it here. */
                            if (LLVMTypeOf(init) != expected) {
                                set_err(err, err_sz,
                                        "global @%s has a different type than "
                                        "this instruction expects",
                                        op->as.global_name);
                                return NULL;
                            }
                            return init;
                        }
                    }
                }
            }
            return g;
        }
        case OPERAND_INT:
            return const_int(expected, op->as.int_val);
        case OPERAND_FLOAT:
            return LLVMConstReal(expected, op->as.float_val);
        case OPERAND_BOOL:
            return LLVMConstInt(LLVMInt1TypeInContext(ctx),
                                op->as.bool_val ? 1 : 0, false);
        case OPERAND_NULL:
            return LLVMConstPointerNull(expected);
        default:
            set_err(err, err_sz, "unknown operand kind");
            return NULL;
    }
}

/* ==================================================================
 * Tensor kernels
 *
 * Each `tensor` instruction lowers to a call into a kernel that the MLIR
 * bridge compiles in a separate module. The bridge needs the full list up
 * front, so the program is scanned before lowering starts.
 * ================================================================== */

typedef struct {
    const Insn *insn;
    int index;
} TensorInsnEntry;

static C1TensorKernel *g_tensor_kernels = NULL;
static TensorInsnEntry *g_tensor_entries = NULL;
static int g_tensor_count = 0;

static void tensor_state_free(void) {
    free(g_tensor_kernels);
    free(g_tensor_entries);
    g_tensor_kernels = NULL;
    g_tensor_entries = NULL;
    g_tensor_count = 0;
}

static int tensor_kernel_index(const Insn *insn) {
    for (int i = 0; i < g_tensor_count; i++)
        if (g_tensor_entries[i].insn == insn)
            return g_tensor_entries[i].index;
    return -1;
}

static bool tensor_dtype_of(const AstNode *t, int *out) {
    if (!t) return false;
    switch (t->kind) {
        case AST_TYPE_F32: *out = C1_DT_F32; return true;
        case AST_TYPE_F64: *out = C1_DT_F64; return true;
        case AST_TYPE_I8:  *out = C1_DT_I8;  return true;
        case AST_TYPE_I16: *out = C1_DT_I16; return true;
        case AST_TYPE_I32: *out = C1_DT_I32; return true;
        case AST_TYPE_I64: *out = C1_DT_I64; return true;
        default: return false;
    }
}

/* M0 scope: f32 only. The bridge has the same restriction. */
static bool tensor_dtype_supported(int dtype) {
    return dtype == C1_DT_F32;
}

static bool tensor_collect(const Program *prog, char *err, size_t err_sz) {
    int count = 0;
    for (int i = 0; i < prog->func_count; i++)
        for (int j = 0; j < prog->functions[i].block_count; j++)
            for (int k = 0; k < prog->functions[i].blocks[j].insn_count; k++)
                if (prog->functions[i].blocks[j].insns[k].kind == INSN_TENSOR)
                    count++;

    if (count == 0) return true;

    g_tensor_kernels = (C1TensorKernel *)calloc((size_t)count, sizeof(C1TensorKernel));
    g_tensor_entries = (TensorInsnEntry *)calloc((size_t)count, sizeof(TensorInsnEntry));
    if (!g_tensor_kernels || !g_tensor_entries) {
        tensor_state_free();
        set_err(err, err_sz, "out of memory for tensor kernels");
        return false;
    }

    int n = 0;
    for (int i = 0; i < prog->func_count; i++) {
        const FuncDecl *func = &prog->functions[i];
        for (int j = 0; j < func->block_count; j++) {
            const BasicBlock *bb = &func->blocks[j];
            for (int k = 0; k < bb->insn_count; k++) {
                const Insn *in = &bb->insns[k];
                if (in->kind != INSN_TENSOR) continue;

                int ndim = (in->data.tensor.op == TENSOR_OP_MATMUL) ? 3 : 2;
                if (in->data.tensor.ndim != ndim) {
                    set_err(err, err_sz,
                            "%s:%d:%d: tensor %s expects %d shape dimensions",
                            prog->filename ? prog->filename : "<input>",
                            in->loc.line, in->loc.column,
                            tensor_op_name(in->data.tensor.op), ndim);
                    tensor_state_free();
                    return false;
                }

                C1TensorKernel *t = &g_tensor_kernels[n];
                t->op = in->data.tensor.op;
                t->ndims = ndim;

                if (!tensor_dtype_of(in->data.tensor.elem_type, &t->dtype) ||
                    !tensor_dtype_supported(t->dtype)) {
                    set_err(err, err_sz,
                            "%s:%d:%d: tensor currently supports only f32",
                            prog->filename ? prog->filename : "<input>",
                            in->loc.line, in->loc.column);
                    tensor_state_free();
                    return false;
                }

                for (int d = 0; d < ndim; d++) {
                    const Operand *dim = &in->data.tensor.dims[d];
                    if (dim->kind != OPERAND_INT || dim->as.int_val <= 0) {
                        set_err(err, err_sz,
                                "%s:%d:%d: tensor shape must be positive integer literals",
                                prog->filename ? prog->filename : "<input>",
                                in->loc.line, in->loc.column);
                        tensor_state_free();
                        return false;
                    }
                    t->dims[d] = dim->as.int_val;
                }

                snprintf(t->symbol, sizeof(t->symbol),
                         "__c1_tensor_%s_%d", func->name, n);
                t->file = prog->filename;
                t->line = in->loc.line;
                t->column = in->loc.column;

                g_tensor_entries[n].insn = in;
                g_tensor_entries[n].index = n;
                n++;
            }
        }
    }

    g_tensor_count = count;
    return true;
}

/* ==================================================================
 * Instruction lowering
 * ================================================================== */

static LLVMIntPredicate cmp_kind_to_llvm(InsnKind k) {
    switch (k) {
        case INSN_EQ:  return LLVMIntEQ;
        case INSN_NE:  return LLVMIntNE;
        case INSN_SLT: return LLVMIntSLT;
        case INSN_SLE: return LLVMIntSLE;
        case INSN_SGT: return LLVMIntSGT;
        case INSN_SGE: return LLVMIntSGE;
        default: return LLVMIntEQ;
    }
}

/* ==================================================================
 * AES instructions
 * SSA semantics follow the standard AES round (SubBytes, ShiftRows,
 * MixColumns, AddRoundKey), i.e. x86 AESENC. AArch64 lacks a single
 * instruction for that: AESE does AddRoundKey first, so the round is
 * emitted as aese(state, 0) + aesmc + xor with the round key.
 * ================================================================== */




/* ==================================================================
 * SHA-256 compression
 * SSA semantics: state vectors are [A,B,C,D] and [E,F,G,H]; the third
 * operand holds four W+K words. AArch64 has one instruction for the
 * 4-round step. x86 SHA-NI works on an ABEF / CDGH layout and in 2-round
 * chunks, so the operands are transposed, two SHA256RNDS2 are issued, and
 * the result is transposed back.
 * ================================================================== */




/* Functions containing crypto instructions get a per-function
   target-features attribute so the rest of the module stays on the baseline
   ISA. Without it the backend refuses to select the intrinsics, and with a
   global -mattr the whole binary would require the extension at runtime. */

/* ==================================================================
 * gfmul — GF(2^128) multiply with the GCM polynomial
 *   P(x) = x^128 + x^7 + x^2 + x + 1
 *
 * GHASH's bit order is MSB-first, which is the bit-reverse of the integer
 * order used by carry-less multiply, so operands and result pass through
 * llvm.bitreverse.i128. The 128x128 product is split Karatsuba-style into
 * three 64x64 PMULL/PCLMULQDQ operations, then reduced twice: once for the
 * high half and once for the 7 carry bits that fold-over produces.
 * Verified against a bit-by-bit reference over random vectors.
 * ================================================================== */





static bool lower_insn(LLVMContextRef ctx, LLVMModuleRef mod,
                        LLVMBuilderRef builder, ValueMap *vm,
                        const Insn *insn, char *err, size_t err_sz) {
    LLVMValueRef result = NULL;

    switch (insn->kind) {
        case INSN_LET: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            result = LLVMBuildAlloca(builder, t, insn->result ? insn->result : "");
            /* Explicit alignment, needed for buffers that are also accessed
               as v128 (loads/stores of <16 x i8> assume 16-byte alignment). */
            if (insn->data.let.align > 0)
                LLVMSetAlignment(result, (unsigned)insn->data.let.align);
            break;
        }
        case INSN_UDIV:
        case INSN_UREM: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef l = lower_op(ctx, mod, vm, &insn->data.binary.lhs, t, err, err_sz);
            LLVMValueRef r = lower_op(ctx, mod, vm, &insn->data.binary.rhs, t, err, err_sz);
            if (!l || !r) return false;
            result = (insn->kind == INSN_UDIV)
                ? LLVMBuildUDiv(builder, l, r, insn->result ? insn->result : "")
                : LLVMBuildURem(builder, l, r, insn->result ? insn->result : "");
            break;
        }
        case INSN_MULHI: {
            /* High half of the unsigned product. Widening to 2N bits and
               shifting back lets the backend pick umulh (AArch64) or the
               mulx/rdx pair (x86-64). */
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef l = lower_op(ctx, mod, vm, &insn->data.binary.lhs, t, err, err_sz);
            LLVMValueRef r = lower_op(ctx, mod, vm, &insn->data.binary.rhs, t, err, err_sz);
            if (!l || !r) return false;
            unsigned bits = LLVMGetIntTypeWidth(t);
            LLVMTypeRef wide = LLVMIntTypeInContext(ctx, bits * 2);
            LLVMValueRef lw = LLVMBuildZExt(builder, l, wide, "");
            LLVMValueRef rw = LLVMBuildZExt(builder, r, wide, "");
            LLVMValueRef p = LLVMBuildMul(builder, lw, rw, "");
            LLVMValueRef sh = LLVMBuildLShr(builder, p,
                                             LLVMConstInt(wide, bits, false), "");
            result = LLVMBuildTrunc(builder, sh, t, insn->result ? insn->result : "");
            break;
        }
        case INSN_ADD: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef l = lower_op(ctx, mod, vm, &insn->data.binary.lhs, t, err, err_sz);
            LLVMValueRef r = lower_op(ctx, mod, vm, &insn->data.binary.rhs, t, err, err_sz);
            if (!l || !r) return false;
            result = LLVMBuildAdd(builder, l, r, insn->result ? insn->result : "");
            break;
        }
        case INSN_SUB: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef l = lower_op(ctx, mod, vm, &insn->data.binary.lhs, t, err, err_sz);
            LLVMValueRef r = lower_op(ctx, mod, vm, &insn->data.binary.rhs, t, err, err_sz);
            if (!l || !r) return false;
            result = LLVMBuildSub(builder, l, r, insn->result ? insn->result : "");
            break;
        }
        case INSN_MUL: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef l = lower_op(ctx, mod, vm, &insn->data.binary.lhs, t, err, err_sz);
            LLVMValueRef r = lower_op(ctx, mod, vm, &insn->data.binary.rhs, t, err, err_sz);
            if (!l || !r) return false;
            result = LLVMBuildMul(builder, l, r, insn->result ? insn->result : "");
            break;
        }
        case INSN_DIV: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef l = lower_op(ctx, mod, vm, &insn->data.binary.lhs, t, err, err_sz);
            LLVMValueRef r = lower_op(ctx, mod, vm, &insn->data.binary.rhs, t, err, err_sz);
            if (!l || !r) return false;
            result = LLVMBuildSDiv(builder, l, r, insn->result ? insn->result : "");
            break;
        }
        case INSN_REM: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef l = lower_op(ctx, mod, vm, &insn->data.binary.lhs, t, err, err_sz);
            LLVMValueRef r = lower_op(ctx, mod, vm, &insn->data.binary.rhs, t, err, err_sz);
            if (!l || !r) return false;
            result = LLVMBuildSRem(builder, l, r, insn->result ? insn->result : "");
            break;
        }
        case INSN_AND: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef l = lower_op(ctx, mod, vm, &insn->data.binary.lhs, t, err, err_sz);
            LLVMValueRef r = lower_op(ctx, mod, vm, &insn->data.binary.rhs, t, err, err_sz);
            if (!l || !r) return false;
            result = LLVMBuildAnd(builder, l, r, insn->result ? insn->result : "");
            break;
        }
        case INSN_OR: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef l = lower_op(ctx, mod, vm, &insn->data.binary.lhs, t, err, err_sz);
            LLVMValueRef r = lower_op(ctx, mod, vm, &insn->data.binary.rhs, t, err, err_sz);
            if (!l || !r) return false;
            result = LLVMBuildOr(builder, l, r, insn->result ? insn->result : "");
            break;
        }
        case INSN_XOR: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef l = lower_op(ctx, mod, vm, &insn->data.binary.lhs, t, err, err_sz);
            LLVMValueRef r = lower_op(ctx, mod, vm, &insn->data.binary.rhs, t, err, err_sz);
            if (!l || !r) return false;
            result = LLVMBuildXor(builder, l, r, insn->result ? insn->result : "");
            break;
        }
        case INSN_SHL: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef l = lower_op(ctx, mod, vm, &insn->data.binary.lhs, t, err, err_sz);
            LLVMValueRef r = lower_op(ctx, mod, vm, &insn->data.binary.rhs, t, err, err_sz);
            if (!l || !r) return false;
            result = LLVMBuildShl(builder, l, r, insn->result ? insn->result : "");
            break;
        }
        case INSN_LSHR: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef l = lower_op(ctx, mod, vm, &insn->data.binary.lhs, t, err, err_sz);
            LLVMValueRef r = lower_op(ctx, mod, vm, &insn->data.binary.rhs, t, err, err_sz);
            if (!l || !r) return false;
            result = LLVMBuildLShr(builder, l, r, insn->result ? insn->result : "");
            break;
        }
        case INSN_ASHR: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef l = lower_op(ctx, mod, vm, &insn->data.binary.lhs, t, err, err_sz);
            LLVMValueRef r = lower_op(ctx, mod, vm, &insn->data.binary.rhs, t, err, err_sz);
            if (!l || !r) return false;
            result = LLVMBuildAShr(builder, l, r, insn->result ? insn->result : "");
            break;
        }
        case INSN_EQ: case INSN_NE: case INSN_SLT:
        case INSN_SLE: case INSN_SGT: case INSN_SGE: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef l = lower_op(ctx, mod, vm, &insn->data.binary.lhs, t, err, err_sz);
            LLVMValueRef r = lower_op(ctx, mod, vm, &insn->data.binary.rhs, t, err, err_sz);
            if (!l || !r) return false;
            result = LLVMBuildICmp(builder, cmp_kind_to_llvm(insn->kind),
                                    l, r, insn->result ? insn->result : "");
            break;
        }
        case INSN_LOAD: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef ptr = lower_op(ctx, mod, vm, &insn->data.load.ptr,
                                         LLVMPointerType(t, 0), err, err_sz);
            if (!ptr) return false;
            result = LLVMBuildLoad2(builder, t, ptr,
                                     insn->result ? insn->result : "");
            break;
        }
        case INSN_STORE: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef val = lower_op(ctx, mod, vm, &insn->data.store.value,
                                         t, err, err_sz);
            LLVMValueRef ptr = lower_op(ctx, mod, vm, &insn->data.store.ptr,
                                         LLVMPointerType(t, 0), err, err_sz);
            if (!val || !ptr) return false;
            LLVMBuildStore(builder, val, ptr);
            return true;
        }
        case INSN_LOAD_ATOMIC: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef ptr = lower_op(ctx, mod, vm, &insn->data.load_atomic.ptr,
                                         LLVMPointerType(t, 0), err, err_sz);
            if (!ptr) return false;
            result = LLVMBuildLoad2(builder, t, ptr,
                                     insn->result ? insn->result : "");
            LLVMSetOrdering(result, lower_ordering(insn->data.load_atomic.ordering));
            break;
        }
        case INSN_STORE_ATOMIC: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef val = lower_op(ctx, mod, vm, &insn->data.store_atomic.value,
                                         t, err, err_sz);
            LLVMValueRef ptr = lower_op(ctx, mod, vm, &insn->data.store_atomic.ptr,
                                         LLVMPointerType(t, 0), err, err_sz);
            if (!val || !ptr) return false;
            LLVMValueRef si = LLVMBuildStore(builder, val, ptr);
            LLVMSetOrdering(si, lower_ordering(insn->data.store_atomic.ordering));
            return true;
        }
        case INSN_ATOMICRMW: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef ptr = lower_op(ctx, mod, vm, &insn->data.atomicrmw.ptr,
                                         LLVMPointerType(t, 0), err, err_sz);
            LLVMValueRef val = lower_op(ctx, mod, vm, &insn->data.atomicrmw.value,
                                         t, err, err_sz);
            if (!ptr || !val) return false;
            static const LLVMAtomicRMWBinOp op_map[] = {
                [ATOMICOP_XCHG] = LLVMAtomicRMWBinOpXchg,
                [ATOMICOP_ADD]  = LLVMAtomicRMWBinOpAdd,
                [ATOMICOP_SUB]  = LLVMAtomicRMWBinOpSub,
                [ATOMICOP_AND]  = LLVMAtomicRMWBinOpAnd,
                [ATOMICOP_OR]   = LLVMAtomicRMWBinOpOr,
                [ATOMICOP_XOR]  = LLVMAtomicRMWBinOpXor,
            };
            int op = insn->data.atomicrmw.atomic_op;
            if (op < 0 || op > 5) { set_err(err, err_sz, "invalid atomicrmw op"); return false; }
            result = LLVMBuildAtomicRMW(builder, op_map[op], ptr, val,
                                         lower_ordering(insn->data.atomicrmw.ordering),
                                         false);
            break;
        }
        case INSN_CMPXCHG: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef ptr = lower_op(ctx, mod, vm, &insn->data.cmpxchg.ptr,
                                         LLVMPointerType(t, 0), err, err_sz);
            LLVMValueRef cmp = lower_op(ctx, mod, vm, &insn->data.cmpxchg.cmp,
                                         t, err, err_sz);
            LLVMValueRef new_val = lower_op(ctx, mod, vm, &insn->data.cmpxchg.new_val,
                                             t, err, err_sz);
            if (!ptr || !cmp || !new_val) return false;
            LLVMValueRef cas = LLVMBuildAtomicCmpXchg(
                builder, ptr, cmp, new_val,
                lower_ordering(insn->data.cmpxchg.succ_ordering),
                lower_ordering(insn->data.cmpxchg.fail_ordering), false);
            /* Extract i1 success flag: element 1 of {ty, i1} */
            result = LLVMBuildExtractValue(builder, cas, 1,
                                            insn->result ? insn->result : "");
            break;
        }
        case INSN_GEP: {
            LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
            if (!t) return false;
            LLVMValueRef base = lower_op(ctx, mod, vm, &insn->data.gep.base,
                                          LLVMPointerType(t, 0), err, err_sz);
            LLVMValueRef idx = lower_op(ctx, mod, vm, &insn->data.gep.index,
                                         LLVMInt32TypeInContext(ctx), err, err_sz);
            if (!base || !idx) return false;
            LLVMValueRef indices[] = { idx };
            result = LLVMBuildGEP2(builder, t, base, indices, 1,
                                    insn->result ? insn->result : "");
            break;
        }
        case INSN_JMP: {
            /* JMP is handled in pass 2 */
            return true;
        }
        case INSN_BR: {
            /* BR is handled in pass 2 */
            return true;
        }
        case INSN_PHI: {
            /* PHI is handled in pass 2 */
            return true;
        }
        case INSN_RET: {
            /* RET is handled in pass 2 */
            return true;
        }
        case INSN_CALL: {
            LLVMValueRef fn = LLVMGetNamedFunction(mod, insn->data.call.func);
            if (!fn) {
                set_err(err, err_sz, "undefined function @%s", insn->data.call.func);
                return false;
            }
            LLVMTypeRef fn_type = LLVMGlobalGetValueType(fn);

            /* Get the actual parameter types from the function signature
               instead of hardcoding i32. This lets call args be correctly
               typed (i64, *i8, f64, etc.) for extern calls like malloc,
               Skia API, or SDL2. */
            int n_fn_params = LLVMCountParamTypes(fn_type);
            LLVMTypeRef *param_types = NULL;
            if (n_fn_params > 0) {
                param_types = (LLVMTypeRef *)malloc(
                    (size_t)n_fn_params * sizeof(LLVMTypeRef));
                LLVMGetParamTypes(fn_type, param_types);
            }

            LLVMValueRef *args = NULL;
            if (insn->data.call.argc > 0) {
                args = (LLVMValueRef *)malloc(
                    (size_t)insn->data.call.argc * sizeof(LLVMValueRef));
                for (int i = 0; i < insn->data.call.argc; i++) {
                    /* Use the function's actual param type; fall back to i32 if
                       the function has fewer params than the call provides */
                    LLVMTypeRef expected = (i < n_fn_params)
                        ? param_types[i]
                        : LLVMInt32TypeInContext(ctx);
                    args[i] = lower_op(ctx, mod, vm, &insn->data.call.args[i],
                                       expected, err, err_sz);
                    if (!args[i]) { free(args); free(param_types); return false; }
                }
            }

            free(param_types);

            LLVMValueRef no_args[1] = {0};
            result = LLVMBuildCall2(builder, fn_type, fn,
                                     args ? args : no_args,
                                     insn->data.call.argc,
                                     insn->result ? insn->result : "");
            free(args);
            break;
        }
        case INSN_ASM: {
            LLVMTypeRef ret = lower_type(ctx, insn->type, err, err_sz);
            if (!ret) return false;

            /* A struct result means multiple outputs: one struct field per
               constraint, in order. The parser cannot see struct bodies, so
               the field/output match is checked here. */
            if (LLVMGetTypeKind(ret) == LLVMStructTypeKind) {
                unsigned fields = LLVMCountStructElementTypes(ret);
                int outs = 0;
                for (const char *p = insn->data.asm_insn.constraints; *p; ) {
                    if (*p == '=') outs++;
                    while (*p && *p != ',') p++;
                    if (*p == ',') p++;
                }
                if ((unsigned)outs != fields) {
                    set_err(err, err_sz,
                            "asm struct result @%s has %u field(s) but %d "
                            "output constraint(s)",
                            insn->type->struct_name, fields, outs);
                    return false;
                }
            }

            int n = insn->data.asm_insn.operand_count;
            LLVMTypeRef *param_types = NULL;
            if (n > 0) {
                param_types = (LLVMTypeRef *)malloc((size_t)n * sizeof(LLVMTypeRef));
                for (int i = 0; i < n; i++) {
                    param_types[i] = lower_type(ctx, insn->data.asm_insn.operand_types[i],
                                                err, err_sz);
                    if (!param_types[i]) { free(param_types); return false; }
                }
            }
            LLVMTypeRef fn_type = LLVMFunctionType(ret, param_types, (unsigned)n, false);

            LLVMValueRef callee = LLVMGetInlineAsm(
                fn_type,
                insn->data.asm_insn.text, (size_t)insn->data.asm_insn.text_len,
                insn->data.asm_insn.constraints,
                (size_t)insn->data.asm_insn.constraints_len,
                /* HasSideEffects */ 1,
                insn->data.asm_insn.align_stack ? 1 : 0,
                insn->data.asm_insn.intel_dialect ? LLVMInlineAsmDialectIntel
                                                  : LLVMInlineAsmDialectATT,
                /* CanThrow */ 0);

            LLVMValueRef *args = NULL;
            if (n > 0) {
                args = (LLVMValueRef *)malloc((size_t)n * sizeof(LLVMValueRef));
                for (int i = 0; i < n; i++) {
                    args[i] = lower_op(ctx, mod, vm, &insn->data.asm_insn.operands[i],
                                       param_types[i], err, err_sz);
                    if (!args[i]) { free(args); free(param_types); return false; }
                }
            }
            free(param_types);

            LLVMValueRef no_args[1] = {0};
            result = LLVMBuildCall2(builder, fn_type, callee,
                                    args ? args : no_args, (unsigned)n,
                                    insn->result ? insn->result : "");
            free(args);

            /* Lets the integrated assembler report errors against the .bx line. */
            if (insn->loc.line > 0) {
                LLVMMetadataRef loc = LLVMValueAsMetadata(LLVMConstInt(
                    LLVMInt64TypeInContext(ctx), (unsigned long long)insn->loc.line, 0));
                LLVMSetMetadata(result,
                                LLVMGetMDKindIDInContext(ctx, "srcloc", 6),
                                LLVMMetadataAsValue(ctx, LLVMMDNodeInContext2(ctx, &loc, 1)));
            }
            break;
        }
        case INSN_ZEXT: {
            LLVMTypeRef from = lower_type(ctx, insn->data.convert.from_type, err, err_sz);
            LLVMTypeRef to = lower_type(ctx, insn->data.convert.to_type, err, err_sz);
            if (!from || !to) return false;
            LLVMValueRef val = lower_op(ctx, mod, vm, &insn->data.convert.value,
                                         from, err, err_sz);
            if (!val) return false;
            result = LLVMBuildZExt(builder, val, to,
                                    insn->result ? insn->result : "");
            break;
        }
        case INSN_SEXT: {
            LLVMTypeRef from = lower_type(ctx, insn->data.convert.from_type, err, err_sz);
            LLVMTypeRef to = lower_type(ctx, insn->data.convert.to_type, err, err_sz);
            if (!from || !to) return false;
            LLVMValueRef val = lower_op(ctx, mod, vm, &insn->data.convert.value,
                                         from, err, err_sz);
            if (!val) return false;
            result = LLVMBuildSExt(builder, val, to,
                                    insn->result ? insn->result : "");
            break;
        }
        case INSN_TRUNC: {
            LLVMTypeRef from = lower_type(ctx, insn->data.convert.from_type, err, err_sz);
            LLVMTypeRef to = lower_type(ctx, insn->data.convert.to_type, err, err_sz);
            if (!from || !to) return false;
            LLVMValueRef val = lower_op(ctx, mod, vm, &insn->data.convert.value,
                                         from, err, err_sz);
            if (!val) return false;
            result = LLVMBuildTrunc(builder, val, to,
                                     insn->result ? insn->result : "");
            break;
        }
        case INSN_FIELD: {
            LLVMTypeRef struct_t = lower_type(ctx, insn->data.field.struct_type,
                                              err, err_sz);
            if (!struct_t) return false;
            LLVMValueRef ptr = lower_op(ctx, mod, vm, &insn->data.field.base,
                                         LLVMPointerType(struct_t, 0), err, err_sz);
            if (!ptr) return false;
            /* field index must be a compile-time integer constant */
            const Operand *fi = &insn->data.field.field_index;
            if (fi->kind != OPERAND_INT) {
                set_err(err, err_sz, "field index must be an integer constant");
                return false;
            }
            unsigned field_idx = (unsigned)fi->as.int_val;
            result = LLVMBuildStructGEP2(builder, struct_t, ptr, field_idx,
                                          insn->result ? insn->result : "");
            break;
        }
        case INSN_TENSOR: {
            int idx = tensor_kernel_index(insn);
            if (idx < 0) {
                set_err(err, err_sz, "tensor: kernel was not collected");
                return false;
            }
            const char *sym = g_tensor_kernels[idx].symbol;
            LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx, 0);
            LLVMTypeRef params[3] = { ptr, ptr, ptr };
            LLVMTypeRef fn_ty = LLVMFunctionType(
                LLVMVoidTypeInContext(ctx), params, 3, 0);

            /* The kernel module has not been linked yet, so create the
               declaration here; the linker folds in the definition later. */
            LLVMValueRef fn = LLVMGetNamedFunction(mod, sym);
            if (!fn) fn = LLVMAddFunction(mod, sym, fn_ty);

            LLVMValueRef args[3];
            args[0] = lower_op(ctx, mod, vm, &insn->data.tensor.dst, ptr, err, err_sz);
            args[1] = lower_op(ctx, mod, vm, &insn->data.tensor.lhs, ptr, err, err_sz);
            args[2] = lower_op(ctx, mod, vm, &insn->data.tensor.rhs, ptr, err, err_sz);
            if (!args[0] || !args[1] || !args[2]) return false;

            LLVMBuildCall2(builder, fn_ty, fn, args, 3, "");
            break;
        }
        default:
            set_err(err, err_sz, "unhandled instruction kind %d", insn->kind);
            return false;
    }

    if (result && insn->result) {
        vm_add(vm, insn->result, result);
    }
    return true;
}

/* ==================================================================
 * Main entry point
 * ================================================================== */

LLVMModuleRef lower_to_llvm_module(const Program *prog, LLVMContextRef ctx,
                                    const char *target_triple,
                                    char *err, size_t err_sz) {
    if (!prog) { set_err(err, err_sz, "null program"); return NULL; }
    if (!ctx) { set_err(err, err_sz, "null context"); return NULL; }

    tensor_state_free();
    if (!tensor_collect(prog, err, err_sz))
        return NULL;

    LLVMModuleRef kernel_mod = NULL;
    if (g_tensor_count > 0) {
        kernel_mod = c1_emit_tensor_kernels(g_tensor_kernels, g_tensor_count,
                                            ctx, target_triple, err, err_sz);
        if (!kernel_mod) {
            tensor_state_free();
            return NULL;
        }
    }

    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext(LLVM_MODULE_NAME, ctx);
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);
    LLVMValueRef *func_refs = NULL;
    bool ok = true;

    #define FAIL_CLEANUP() do { ok = false; goto MODULE_CLEANUP; } while(0)

    /* ---- Pre-create all named struct types (two-pass) ---- */
    if (prog->struct_count > 0) {
        g_struct_type_count = prog->struct_count;
        g_struct_type_names = (const char **)malloc(
            (size_t)g_struct_type_count * sizeof(const char *));
        g_struct_types = (LLVMTypeRef *)malloc(
            (size_t)g_struct_type_count * sizeof(LLVMTypeRef));
        if (!g_struct_type_names || !g_struct_types) {
            set_err(err, err_sz, "out of memory for struct types");
            FAIL_CLEANUP();
        }
        /* Pass 1: create all named opaque struct types */
        for (int i = 0; i < g_struct_type_count; i++) {
            g_struct_type_names[i] = prog->structs[i].name;
            g_struct_types[i] = LLVMStructCreateNamed(ctx, prog->structs[i].name);
        }
        /* Pass 2: set struct bodies (after all types exist, for nested refs) */
        for (int i = 0; i < g_struct_type_count && ok; i++) {
            int fc = prog->structs[i].field_count;
            LLVMTypeRef *field_types = NULL;
            if (fc > 0) {
                field_types = (LLVMTypeRef *)malloc(
                    (size_t)fc * sizeof(LLVMTypeRef));
                for (int j = 0; j < fc; j++) {
                    field_types[j] = lower_type(ctx, prog->structs[i].fields[j].type,
                                                err, err_sz);
                    if (!field_types[j]) {
                        free(field_types);
                        FAIL_CLEANUP();
                    }
                }
            }
            LLVMStructSetBody(g_struct_types[i], field_types, fc, false);
            free(field_types);
        }
    }

    /* ---- Lower globals ---- */
    for (int g = 0; g < prog->global_count; g++) {
        const GlobalDecl *gd = &prog->globals[g];
        LLVMTypeRef t = lower_type(ctx, gd->type, err, err_sz);
        if (!t) FAIL_CLEANUP();
        LLVMValueRef gv = LLVMAddGlobal(mod, t, gd->name);
        if (gd->is_const) {
            LLVMSetGlobalConstant(gv, true);
        }
        if (gd->is_internal) {
            LLVMSetLinkage(gv, LLVMInternalLinkage);
        }
        if (gd->has_init) {
            LLVMValueRef init = NULL;
            if (gd->array_init && gd->array_count > 0) {
                /* Array initializer: [ val, val, ... ] */
                LLVMTypeRef elem_t = LLVMGetElementType(t);
                if (!elem_t) { set_err(err, err_sz, "invalid array element type"); FAIL_CLEANUP(); }
                LLVMValueRef *vals = (LLVMValueRef *)malloc(
                    (size_t)gd->array_count * sizeof(LLVMValueRef));
                for (int j = 0; j < gd->array_count; j++) {
                    vals[j] = const_int(elem_t, gd->array_init[j]);
                }
                init = LLVMConstArray2(elem_t, vals, (unsigned)gd->array_count);
                free(vals);
            } else {
                switch (gd->init_value.kind) {
                    case OPERAND_INT:
                        init = const_int(t, gd->init_value.as.int_val);
                        break;
                    case OPERAND_FLOAT:
                        init = LLVMConstReal(t, gd->init_value.as.float_val);
                        break;
                    case OPERAND_BOOL:
                        init = LLVMConstInt(t, gd->init_value.as.bool_val ? 1 : 0, false);
                        break;
                    default:
                        set_err(err, err_sz, "unsupported global initializer");
                        FAIL_CLEANUP();
                }
            }
            LLVMSetInitializer(gv, init);
        } else {
            LLVMSetInitializer(gv, LLVMConstNull(t));
        }
        if (!ok) FAIL_CLEANUP();
    }

    /* ---- Lower extern declarations ---- */
    for (int i = 0; i < prog->extern_count && ok; i++) {
        const ExternDecl *ext = &prog->externs[i];

        LLVMTypeRef ext_ret = ext->return_type
            ? lower_type(ctx, ext->return_type, err, err_sz)
            : LLVMVoidTypeInContext(ctx);
        if (!ext_ret) FAIL_CLEANUP();

        LLVMTypeRef *ext_params = NULL;
        if (ext->param_count > 0) {
            ext_params = (LLVMTypeRef *)malloc(
                (size_t)ext->param_count * sizeof(LLVMTypeRef));
            for (int j = 0; j < ext->param_count; j++) {
                ext_params[j] = lower_type(ctx, ext->params[j].type, err, err_sz);
                if (!ext_params[j]) { free(ext_params); FAIL_CLEANUP(); }
            }
        }
        LLVMTypeRef ext_func_type = LLVMFunctionType(
            ext_ret, ext_params, ext->param_count, ext->is_variadic);
        LLVMValueRef ext_func = LLVMAddFunction(mod, ext->name, ext_func_type);
        free(ext_params);

        /* Mark as a declaration (no body) — LLVM treats this as an extern */
        /* LLVMAddFunction without creating basic blocks is already a declaration */
        (void)ext_func; /* suppress unused warning */
    }

    /* ---- Create all function signatures first, so a call may refer to a
       function defined later in the file. ---- */
    if (prog->func_count > 0) {
        func_refs = (LLVMValueRef *)calloc((size_t)prog->func_count,
                                            sizeof(LLVMValueRef));
        if (!func_refs) {
            set_err(err, err_sz, "out of memory for function table");
            FAIL_CLEANUP();
        }
    }
    for (int i = 0; i < prog->func_count && ok; i++) {
        const FuncDecl *func = &prog->functions[i];

        LLVMTypeRef ret_type = func->return_type
            ? lower_type(ctx, func->return_type, err, err_sz)
            : LLVMVoidTypeInContext(ctx);
        if (!ret_type) FAIL_CLEANUP();

        LLVMTypeRef *param_types = NULL;
        if (func->param_count > 0) {
            param_types = (LLVMTypeRef *)malloc(
                (size_t)func->param_count * sizeof(LLVMTypeRef));
            for (int j = 0; j < func->param_count; j++) {
                param_types[j] = lower_type(ctx, func->params[j].type, err, err_sz);
                if (!param_types[j]) { free(param_types); FAIL_CLEANUP(); }
            }
        }
        LLVMTypeRef llvm_func_type = LLVMFunctionType(
            ret_type, param_types, func->param_count, false);
        LLVMValueRef fn = LLVMAddFunction(mod, func->name, llvm_func_type);
        free(param_types);

        for (int j = 0; j < func->param_count; j++) {
            LLVMSetValueName2(LLVMGetParam(fn, j),
                               func->params[j].name,
                               strlen(func->params[j].name));
        }
        func_refs[i] = fn;
    }

    /* ---- Lower function bodies ---- */
    for (int i = 0; i < prog->func_count && ok; i++) {
        const FuncDecl *func = &prog->functions[i];
        LLVMValueRef llvm_func = func_refs[i];

        LLVMBasicBlockRef *block_refs = NULL;
        if (func->block_count > 0) {
            block_refs = (LLVMBasicBlockRef *)malloc(
                (size_t)func->block_count * sizeof(LLVMBasicBlockRef));
            for (int j = 0; j < func->block_count; j++) {
                block_refs[j] = LLVMAppendBasicBlockInContext(
                    ctx, llvm_func, func->blocks[j].label);
            }
        }

        ValueMap vm;
        vm_init(&vm);

        for (int j = 0; j < func->param_count; j++) {
            vm_add(&vm, func->params[j].name, LLVMGetParam(llvm_func, j));
        }

        /* Pass 1a: Pre-create phi nodes */
        for (int j = 0; j < func->block_count && ok; j++) {
            LLVMPositionBuilderAtEnd(builder, block_refs[j]);
            const BasicBlock *bb = &func->blocks[j];
            for (int k = 0; k < bb->insn_count && ok; k++) {
                const Insn *insn = &bb->insns[k];
                if (insn->kind != INSN_PHI || !insn->result) continue;
                LLVMTypeRef t = insn->type ? lower_type(ctx, insn->type, err, err_sz)
                                           : LLVMInt32TypeInContext(ctx);
                if (!t) { set_err(err, err_sz, "phi: bad type"); ok = false; break; }
                LLVMValueRef phi = LLVMBuildPhi(builder, t, insn->result);
                vm_add(&vm, insn->result, phi);
            }
        }

        /* Pass 1b: non-terminator, non-phi instructions */
        for (int j = 0; j < func->block_count && ok; j++) {
            LLVMPositionBuilderAtEnd(builder, block_refs[j]);
            const BasicBlock *bb = &func->blocks[j];
            for (int k = 0; k < bb->insn_count && ok; k++) {
                const Insn *insn = &bb->insns[k];
                if (insn->kind == INSN_JMP || insn->kind == INSN_BR ||
                    insn->kind == INSN_RET || insn->kind == INSN_PHI)
                    continue;
                if (!lower_insn(ctx, mod, builder, &vm, insn, err, err_sz))
                    ok = false;
            }
        }

        /* Pass 2: phi incoming values + jmp/br/ret */
        for (int j = 0; j < func->block_count && ok; j++) {
            LLVMPositionBuilderAtEnd(builder, block_refs[j]);
            const BasicBlock *bb = &func->blocks[j];
            for (int k = 0; k < bb->insn_count && ok; k++) {
                const Insn *insn = &bb->insns[k];

                if (insn->kind == INSN_PHI) {
                    if (!insn->result) continue;
                    LLVMValueRef phi = vm_get(&vm, insn->result);
                    if (!phi) { set_err(err, err_sz, "phi %%%s not pre-created", insn->result); ok = false; break; }
                    LLVMTypeRef t = insn->type ? lower_type(ctx, insn->type, err, err_sz)
                                               : LLVMInt32TypeInContext(ctx);
                    if (!t) { ok = false; break; }
                    for (int p = 0; p < insn->data.phi.count && ok; p++) {
                        const PhiCase *pc = &insn->data.phi.cases[p];
                        LLVMValueRef val = lower_op(ctx, mod, &vm, &pc->value, t, err, err_sz);
                        LLVMBasicBlockRef pred = NULL;
                        for (int b = 0; b < func->block_count; b++)
                            if (strcmp(func->blocks[b].label, pc->label) == 0)
                                { pred = block_refs[b]; break; }
                        if (!val || !pred) {
                            set_err(err, err_sz, "phi incoming error: @%s", pc->label);
                            ok = false;
                        } else {
                            LLVMAddIncoming(phi, &val, &pred, 1);
                        }
                    }

                } else if (insn->kind == INSN_JMP) {
                    const char *target = insn->data.jmp.target;
                    LLVMBasicBlockRef tbb = NULL;
                    for (int b = 0; b < func->block_count; b++)
                        if (strcmp(func->blocks[b].label, target) == 0)
                            { tbb = block_refs[b]; break; }
                    if (!tbb) { set_err(err, err_sz, "jmp target @%s not found", target); ok = false; }
                    else LLVMBuildBr(builder, tbb);

                } else if (insn->kind == INSN_BR) {
                    LLVMValueRef cond = lower_op(ctx, mod, &vm,
                        &insn->data.br.cond, LLVMInt1TypeInContext(ctx), err, err_sz);
                    if (!cond) { ok = false; }
                    else {
                        const char *tt = insn->data.br.true_target;
                        const char *ft = insn->data.br.false_target;
                        LLVMBasicBlockRef tbb = NULL, fbb = NULL;
                        for (int b = 0; b < func->block_count; b++) {
                            if (strcmp(func->blocks[b].label, tt) == 0) tbb = block_refs[b];
                            if (strcmp(func->blocks[b].label, ft) == 0) fbb = block_refs[b];
                        }
                        if (!tbb || !fbb) { set_err(err, err_sz, "br target not found"); ok = false; }
                        else LLVMBuildCondBr(builder, cond, tbb, fbb);
                    }

                } else if (insn->kind == INSN_RET) {
                    if (insn->data.ret.is_void) {
                        LLVMBuildRetVoid(builder);
                    } else {
                        LLVMTypeRef t = lower_type(ctx, insn->type, err, err_sz);
                        if (!t) { ok = false; }
                        else {
                            LLVMValueRef val = lower_op(ctx, mod, &vm,
                                &insn->data.ret.value, t, err, err_sz);
                            if (!val) { ok = false; }
                            else LLVMBuildRet(builder, val);
                        }
                    }
                }
            }
        }

        vm_free(&vm);
        free(block_refs);
        if (!ok) FAIL_CLEANUP();
    }

MODULE_CLEANUP:
    free(func_refs);
    lower_struct_type_map_clear();
    LLVMDisposeBuilder(builder);
    if (kernel_mod) {
        if (ok) {
            /* LLVMLinkModules2 destroys the source module whether or not it
               succeeds, so it must not be disposed afterwards. */
            if (LLVMLinkModules2(mod, kernel_mod) != 0) {
                set_err(err, err_sz, "failed to link tensor kernels into module");
                ok = false;
            }
        } else {
            LLVMDisposeModule(kernel_mod);
        }
        kernel_mod = NULL;
    }
    tensor_state_free();
    if (!ok) {
        LLVMDisposeModule(mod);
        return NULL;
    }
    return mod;
}

char *lower_to_llvm_ir(const Program *prog, const char *target_triple,
                       char *err, size_t err_sz) {
    if (!prog) { set_err(err, err_sz, "null program"); return NULL; }

    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef mod = lower_to_llvm_module(prog, ctx, target_triple, err, err_sz);
    if (!mod) {
        LLVMContextDispose(ctx);
        return NULL;
    }

    char *ir = LLVMPrintModuleToString(mod);
    if (!ir) set_err(err, err_sz, "failed to print LLVM IR");

    LLVMDisposeModule(mod);
    LLVMContextDispose(ctx);
    return ir;
}

bool lower_to_llvm_ir_file(const Program *prog, const char *target_triple,
                            const char *path, char *err, size_t err_sz) {
    char *ir = lower_to_llvm_ir(prog, target_triple, err, err_sz);
    if (!ir) return false;
    FILE *fp = fopen(path, "w");
    if (!fp) {
        set_err(err, err_sz, "cannot open: %s", path);
        LLVMDisposeMessage(ir);
        return false;
    }
    fprintf(fp, "%s", ir);
    fclose(fp);
    LLVMDisposeMessage(ir);
    return true;
}

void lower_free_ir(char *ir) {
    LLVMDisposeMessage(ir);
}
