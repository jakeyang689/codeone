#ifndef C1_HIR_TENSOR_H
#define C1_HIR_TENSOR_H

#include <stddef.h>
#include <stdint.h>
#include <llvm-c/Core.h>
#include <llvm-c/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tensor operation kinds, matching TensorOp in ast.h. */
typedef enum {
    C1_TENSOR_ADD = 0,
    C1_TENSOR_SUB = 1,
    C1_TENSOR_MUL = 2,
    C1_TENSOR_DIV = 3,
    C1_TENSOR_MATMUL = 4,
} C1TensorOp;

/* Element data types. */
typedef enum {
    C1_DT_F32 = 1,
    C1_DT_F64 = 2,
    C1_DT_I8 = 3,
    C1_DT_I16 = 4,
    C1_DT_I32 = 5,
    C1_DT_I64 = 6,
} C1TensorDType;

typedef struct {
    int op;             /* C1TensorOp */
    int dtype;          /* C1TensorDType */
    int ndims;          /* 2 for elementwise, 3 for matmul */
    int64_t dims[3];    /* [rows, cols] or [M, K, N], all positive */
    char symbol[128];   /* LLVM function name; caller picks it */
    const char *file;   /* source path for diagnostics, may be NULL */
    int line;           /* source line of the tensor instruction, 0 if unknown */
    int column;
} C1TensorKernel;

/* Emits one LLVM module holding a function per kernel:
 *
 *   void <symbol>(ptr dst, ptr a, ptr b)
 *
 * The arguments are row-major buffers of `dtype` elements. `ctx` is the
 * caller's context, so the returned module links straight into it.
 * Returns NULL and fills err on failure. The caller owns the module. */
LLVMModuleRef c1_emit_tensor_kernels(const C1TensorKernel *kernels, int count,
                                     LLVMContextRef ctx,
                                     const char *target_triple,
                                     char *err, size_t errsz);

#ifdef __cplusplus
}
#endif

#endif /* C1_HIR_TENSOR_H */
