CC       = cc
CXX      = clang++
CFLAGS   = -Wall -Wextra -std=c99 -g -I.. -fdebug-prefix-map=$(ROOT)=. \
           -DCODEONE_VERSION=\"$(shell cat $(ROOT)/VERSION)\" \
           -DMACOS_DEPLOYMENT_TARGET=\"$(shell xcrun --show-sdk-version 2>/dev/null || echo 14.0)\" \
           -DC1_CC=\"$(CC)\"
CXXFLAGS = -Wall -Wextra -std=c++17 -g -fno-rtti -I.. -fdebug-prefix-map=$(ROOT)=.

# Auto-detect project root from Makefile location
_ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
ROOT     := $(_ROOT)
BUILD_DIR := $(ROOT)/build/c1
LLVM_DIR   = $(ROOT)/llvm
# c1 links against the MLIR-enabled LLVM tree: MLIR and plain LLVM must come
# from the same build, since the two trees differ in assertion settings.
LLVM_BUILD = $(LLVM_DIR)/build-mlir
LLVM_INC  := -I$(LLVM_DIR)/llvm/include -I$(LLVM_BUILD)/include
MLIR_INC  := -I$(LLVM_DIR)/mlir/include -I$(LLVM_BUILD)/tools/mlir/include
LLVM_LIB  := -L$(LLVM_BUILD)/lib
MLIR_LIBS := $(wildcard $(LLVM_BUILD)/lib/libMLIR*.a)

SRCS   = main.c ../lex.c ../ast.c ../lowering_ir.c ../codec.c ../cmap.c \
         ../hir/tensor.cpp ../hir/c1_dialect.cpp ../hir/build.cpp \
         ../hir/lower.cpp ../hir/backend.cpp
OBJS   = $(addprefix $(BUILD_DIR)/, main.o lex.o ast.o lowering_ir.o codec.o \
         cmap.o tensor.o c1_dialect.o build.o lower.o backend.o)
TARGET = c1

# LLVM's static libraries carry their own registrations, so every archive
# object must be kept. Darwin spells this -all_load, ELF needs the
# --whole-archive pair.
# LLVMSupport in this tree is built with zstd, which macOS does not ship.
ZSTD_PREFIX := $(shell brew --prefix zstd 2>/dev/null)
ifneq ($(ZSTD_PREFIX),)
LD_ZSTD = -L$(ZSTD_PREFIX)/lib -lzstd
else
LD_ZSTD = -lzstd
endif

UNAME_S := $(shell uname -s)
# LLVM archives must be kept whole (their static registrations have no external
# references), but MLIR archives must not be: whole-loading them pulls in the
# LSP and JIT server code, which needs LLVM libraries this tree does not build.
LLVM_LIB_NAMES := AArch64CodeGen AArch64Desc AArch64Info AArch64Utils \
  AArch64AsmParser X86CodeGen X86Desc X86Info X86AsmParser \
  ipo Vectorize InstCombine AggressiveInstCombine ScalarOpts \
  TransformUtils Analysis ProfileData Passes CFGuard \
  DebugInfoDWARF DebugInfoDWARFLowLevel DebugInfoCodeView DebugInfoPDB \
  DebugInfoMSF DebugInfoGSYM DebugInfoBTF Symbolize \
  Object ObjectYAML BitReader BitWriter MCParser MC MCDisassembler \
  Remarks BitstreamReader CodeGen CodeGenTypes SelectionDAG GlobalISel \
  AsmPrinter IRPrinter Target TargetParser BinaryFormat Core Support Demangle \
  TableGen IRReader Option AsmParser TextAPI SandboxIR CGData \
  Coroutines Linker Instrumentation FrontendOpenMP FrontendHLSL \
  FrontendOffloading FrontendAtomic FrontendDirective ObjCARCOpts HipStdPar
comma := ,
ifeq ($(UNAME_S),Darwin)
LD_LLVM_LIBS   = $(foreach lib,$(LLVM_LIB_NAMES),-Wl$(comma)-force_load$(comma)$(LLVM_BUILD)/lib/libLLVM$(lib).a)
LD_GROUP_BEGIN =
LD_GROUP_END   =
LD_CXX_RUNTIME = -lc++
LD_SYSTEM      =
else
LD_LLVM_LIBS   = -Wl,--whole-archive $(LLVM_LIB_NAMES:%=$(LLVM_BUILD)/lib/libLLVM%.a) \
                 -Wl,--no-whole-archive
# MLIR archives reference each other and LLVM, and ELF links are single-pass.
LD_GROUP_BEGIN = -Wl,--start-group
LD_GROUP_END   = -Wl,--end-group
LD_CXX_RUNTIME = -lstdc++
LD_SYSTEM      = -lpthread -ldl -lm -ltinfo
# -std=c99 hides popen() and other POSIX declarations that macOS exposes.
CFLAGS += -D_GNU_SOURCE
endif

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LLVM_LIB) $(LD_LLVM_LIBS) \
	  $(LD_GROUP_BEGIN) $(MLIR_LIBS) $(MLIR_LIBS) $(LD_GROUP_END) \
	  $(LD_CXX_RUNTIME) $(LD_SYSTEM) -lz $(LD_ZSTD)

$(OBJS): | $(BUILD_DIR)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/main.o: main.c ../codec.h ../lowering_ir.h ../ast.h ../lex.h ../hir/backend.h $(ROOT)/VERSION
	$(CC) $(CFLAGS) $(LLVM_INC) -c -o $@ $<

$(BUILD_DIR)/lex.o: ../lex.c ../lex.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/ast.o: ../ast.c ../ast.h ../lex.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/lowering_ir.o: ../lowering_ir.c ../lowering_ir.h ../ast.h
	$(CC) $(CFLAGS) $(LLVM_INC) -c -o $@ $<

$(BUILD_DIR)/codec.o: ../codec.c ../codec.h ../lowering_ir.h
	$(CC) $(CFLAGS) $(LLVM_INC) -c -o $@ $<

# cmap.c is pure AST -> text and needs no LLVM headers.
$(BUILD_DIR)/cmap.o: ../cmap.c ../cmap.h ../ast.h ../lex.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/tensor.o: ../hir/tensor.cpp ../hir/tensor.h
	$(CXX) $(CXXFLAGS) $(LLVM_INC) $(MLIR_INC) -c -o $@ $<

$(BUILD_DIR)/c1_dialect.o: ../hir/c1_dialect.cpp ../hir/c1_dialect.h
	$(CXX) $(CXXFLAGS) $(LLVM_INC) $(MLIR_INC) -c -o $@ $<

$(BUILD_DIR)/build.o: ../hir/build.cpp ../hir/build.h ../hir/c1_dialect.h ../ast.h
	$(CXX) $(CXXFLAGS) $(LLVM_INC) $(MLIR_INC) -c -o $@ $<

$(BUILD_DIR)/lower.o: ../hir/lower.cpp ../hir/lower.h ../hir/c1_dialect.h
	$(CXX) $(CXXFLAGS) $(LLVM_INC) $(MLIR_INC) -c -o $@ $<

$(BUILD_DIR)/backend.o: ../hir/backend.cpp ../hir/backend.h ../hir/build.h ../hir/lower.h ../ast.h
	$(CXX) $(CXXFLAGS) $(LLVM_INC) $(MLIR_INC) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
