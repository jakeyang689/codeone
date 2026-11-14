CC       = cc
CXX      = clang++
CFLAGS   = -Wall -Wextra -std=c99 -g -I.. -fdebug-prefix-map=$(ROOT)=. \
           -DCODEONE_VERSION=\"$(shell cat $(ROOT)/VERSION)\" \
           -DMACOS_DEPLOYMENT_TARGET=\"$(shell xcrun --show-sdk-version 2>/dev/null || echo 14.0)\" \
           -DC1_CC=\"$(CC)\"

# Auto-detect project root from Makefile location
_ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
ROOT     := $(_ROOT)
BUILD_DIR := $(ROOT)/build/c1
LLVM_DIR   = $(ROOT)/llvm
LLVM_INC  := -I$(LLVM_DIR)/llvm/include -I$(LLVM_DIR)/build/include
LLVM_LIB  := -L$(LLVM_DIR)/build/lib

SRCS   = main.c ../lex.c ../ast.c ../lowering_ir.c ../codec.c ../cmap.c
OBJS   = $(addprefix $(BUILD_DIR)/, main.o lex.o ast.o lowering_ir.o codec.o cmap.o)
TARGET = c1

# LLVM's static libraries carry their own registrations, so every archive
# object must be kept. Darwin spells this -all_load, ELF needs the
# --whole-archive pair.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LD_WHOLE_BEGIN = -Wl,-all_load
LD_WHOLE_END   =
LD_CXX_RUNTIME = -lc++
LD_SYSTEM      =
else
LD_WHOLE_BEGIN = -Wl,--whole-archive
LD_WHOLE_END   = -Wl,--no-whole-archive
LD_CXX_RUNTIME = -lstdc++
LD_SYSTEM      = -lpthread -ldl -lm -ltinfo
# -std=c99 hides popen() and other POSIX declarations that macOS exposes.
CFLAGS += -D_GNU_SOURCE
endif

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LLVM_LIB) $(LD_WHOLE_BEGIN) \
  -lLLVMAArch64CodeGen -lLLVMAArch64Desc -lLLVMAArch64Info \
  -lLLVMAArch64Utils -lLLVMAArch64AsmParser \
  -lLLVMX86CodeGen -lLLVMX86Desc -lLLVMX86Info \
  -lLLVMX86AsmParser \
  -lLLVMipo -lLLVMVectorize -lLLVMInstCombine \
  -lLLVMAggressiveInstCombine -lLLVMScalarOpts -lLLVMTransformUtils \
  -lLLVMAnalysis -lLLVMProfileData -lLLVMPasses -lLLVMCFGuard \
  -lLLVMDebugInfoDWARF -lLLVMDebugInfoDWARFLowLevel \
  -lLLVMDebugInfoCodeView -lLLVMDebugInfoPDB -lLLVMDebugInfoMSF \
  -lLLVMDebugInfoGSYM -lLLVMDebugInfoBTF -lLLVMSymbolize \
  -lLLVMObject -lLLVMObjectYAML -lLLVMBitReader -lLLVMBitWriter \
  -lLLVMMCParser -lLLVMMC -lLLVMMCDisassembler -lLLVMRemarks -lLLVMBitstreamReader \
  -lLLVMCodeGen -lLLVMCodeGenTypes -lLLVMSelectionDAG \
  -lLLVMGlobalISel -lLLVMAsmPrinter -lLLVMIRPrinter \
  -lLLVMTarget -lLLVMTargetParser \
  -lLLVMBinaryFormat -lLLVMCore -lLLVMSupport -lLLVMDemangle \
  -lLLVMTableGen -lLLVMIRReader -lLLVMOption -lLLVMAsmParser \
  -lLLVMTextAPI -lLLVMSandboxIR -lLLVMCGData \
  -lLLVMCoroutines -lLLVMLinker -lLLVMInstrumentation \
  -lLLVMFrontendOpenMP -lLLVMFrontendHLSL -lLLVMFrontendOffloading \
  -lLLVMFrontendAtomic -lLLVMFrontendDirective \
  -lLLVMObjCARCOpts -lLLVMHipStdPar \
  $(LD_WHOLE_END) $(LD_CXX_RUNTIME) $(LD_SYSTEM) -lz

$(OBJS): | $(BUILD_DIR)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/main.o: main.c ../codec.h ../lowering_ir.h ../ast.h ../lex.h $(ROOT)/VERSION
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

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
