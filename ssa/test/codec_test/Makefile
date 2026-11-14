CC       = cc
CXX      = clang++
CFLAGS   = -Wall -Wextra -std=c99 -g -I../.. -I.

ROOT       := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../..)
BUILD_DIR  := $(ROOT)/build/codec_test
LLVM_DIR   = $(ROOT)/llvm
LLVM_INC  := -I$(LLVM_DIR)/llvm/include -I$(LLVM_DIR)/build/include
LLVM_LIB  := -L$(LLVM_DIR)/build/lib
LLVM_LIBS := \
  -lLLVMAArch64CodeGen -lLLVMAArch64Desc -lLLVMAArch64Info \
  -lLLVMAArch64Utils -lLLVMAArch64AsmParser \
  -lLLVMX86CodeGen -lLLVMX86Desc -lLLVMX86Info -lLLVMX86AsmParser \
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
  -lLLVMCoroutines -lLLVMLinker \
  -lLLVMInstrumentation -lLLVMFrontendOpenMP \
  -lLLVMFrontendHLSL -lLLVMFrontendOffloading \
  -lLLVMFrontendAtomic -lLLVMFrontendDirective \
  -lLLVMObjCARCOpts -lLLVMHipStdPar

OBJS   = $(addprefix $(BUILD_DIR)/, codec.o lowering_ir.o lex.o ast.o test_codec.o)
TARGET = test_codec

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
CFLAGS        += -D_GNU_SOURCE
endif

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -L$(LLVM_DIR)/build/lib $(LD_WHOLE_BEGIN) $(LLVM_LIBS) $(LD_WHOLE_END) \
	  $(LD_CXX_RUNTIME) $(LD_SYSTEM) -lz -o $@ $^

test: $(TARGET)
	./$(TARGET)

$(OBJS): | $(BUILD_DIR)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/codec.o: ../../codec.c ../../codec.h
	$(CC) $(CFLAGS) $(LLVM_INC) -c -o $@ $<

$(BUILD_DIR)/lowering_ir.o: ../../lowering_ir.c ../../lowering_ir.h ../../ast.h
	$(CC) $(CFLAGS) $(LLVM_INC) -c -o $@ $<

$(BUILD_DIR)/lex.o: ../../lex.c ../../lex.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/ast.o: ../../ast.c ../../ast.h ../../lex.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_codec.o: test_codec.c test.h ../../codec.h
	$(CC) $(CFLAGS) $(LLVM_INC) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
