#ifndef CODEC_H
#define CODEC_H

#include "ast.h"
#include <stdbool.h>
#include <stddef.h>
#include <llvm-c/Types.h>

/* ==================================================================
 * Codec — LLVM IR → 目标机器码 (.o)
 *
 * 将 lowering_ir 产出的 LLVM IR 编译为目标平台的机器码，
 * 输出 .o 目标文件。
 * ================================================================== */

/* 优化级别 */
typedef enum {
    CODEC_O0 = 0,  /* 无优化 */
    CODEC_O1 = 1,  /* 低 */
    CODEC_O2 = 2,  /* 中 (默认) */
    CODEC_O3 = 3,  /* 高 */
} CodecOptLevel;

/* ==================================================================
 * API
 * ================================================================== */

/* target_features 是 LLVM 的 -mattr 字符串（如 "+aes,+sha2"），可为 NULL。 */

/* 从 LLVM IR 字符串编译到 .o 文件。 */
bool codec_compile_ir(const char *ir, const char *target_triple,
                       const char *target_features,
                       CodecOptLevel opt_level,
                       const char *output_path,
                       char *error_msg, size_t error_size);

/* 从已构建好的 LLVM Module 编译到 .o 文件。
 * 调用者保留 module 的所有权（不会被释放）。 */
bool codec_compile_module(LLVMModuleRef mod, const char *target_triple,
                           const char *target_features,
                           CodecOptLevel opt_level,
                           const char *output_path,
                           char *error_msg, size_t error_size);

/* 从 Program AST 直接编译到 .o 文件。
 * 内部通过 lower_to_llvm_module 直接生成 LLVM Module，
 * 跳过 IR 字符串序列化/反序列化过程，性能更高。 */
bool codec_compile_program(const Program *prog, const char *target_triple,
                            const char *target_features,
                            CodecOptLevel opt_level,
                            const char *output_path,
                            char *error_msg, size_t error_size);

#endif /* CODEC_H */
