CC       = cc
CXX      = clang++
CFLAGS   = -Wall -Wextra -std=c99 -g -I../.. -I.
CXXFLAGS = -Wall -Wextra -std=c++17 -g -fno-rtti -I../..

ROOT       := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../..)
BUILD_DIR  := $(ROOT)/build/codec_test
LLVM_DIR   = $(ROOT)/llvm
LLVM_BUILD = $(LLVM_DIR)/build-mlir
LLVM_INC  := -I$(LLVM_DIR)/llvm/include -I$(LLVM_BUILD)/include
MLIR_INC  := -I$(LLVM_DIR)/mlir/include -I$(LLVM_BUILD)/tools/mlir/include
LLVM_LIB  := -L$(LLVM_BUILD)/lib
LLVM_LIBS := $(wildcard $(LLVM_BUILD)/lib/libLLVM*.a)
MLIR_LIBS := $(wildcard $(LLVM_BUILD)/lib/libMLIR*.a)

# This LLVM tree is built with zstd, which macOS does not ship.
ZSTD_PREFIX := $(shell brew --prefix zstd 2>/dev/null)
ifneq ($(ZSTD_PREFIX),)
LD_ZSTD = -L$(ZSTD_PREFIX)/lib -lzstd
else
LD_ZSTD = -lzstd
endif

OBJS   = $(addprefix $(BUILD_DIR)/, codec.o lowering_ir.o lex.o ast.o tensor.o test_codec.o)
TARGET = test_codec

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LD_GROUP_BEGIN =
LD_GROUP_END   =
LD_CXX_RUNTIME = -lc++
LD_SYSTEM      =
else
LD_GROUP_BEGIN = -Wl,--start-group
LD_GROUP_END   = -Wl,--end-group
LD_CXX_RUNTIME = -lstdc++
LD_SYSTEM      = -lpthread -ldl -lm -ltinfo
CFLAGS        += -D_GNU_SOURCE
endif

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LLVM_LIB) $(LD_GROUP_BEGIN) \
	  $(LLVM_LIBS) $(MLIR_LIBS) $(MLIR_LIBS) $(LD_GROUP_END) \
	  $(LD_CXX_RUNTIME) $(LD_SYSTEM) -lz $(LD_ZSTD)

test: $(TARGET)
	./$(TARGET)

$(OBJS): | $(BUILD_DIR)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/codec.o: ../../codec.c ../../codec.h
	$(CC) $(CFLAGS) $(LLVM_INC) -c -o $@ $<

$(BUILD_DIR)/lowering_ir.o: ../../lowering_ir.c ../../lowering_ir.h ../../ast.h ../../hir/tensor.h
	$(CC) $(CFLAGS) $(LLVM_INC) -c -o $@ $<

$(BUILD_DIR)/tensor.o: ../../hir/tensor.cpp ../../hir/tensor.h
	$(CXX) $(CXXFLAGS) $(LLVM_INC) $(MLIR_INC) -c -o $@ $<

$(BUILD_DIR)/lex.o: ../../lex.c ../../lex.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/ast.o: ../../ast.c ../../ast.h ../../lex.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_codec.o: test_codec.c test.h ../../codec.h
	$(CC) $(CFLAGS) $(LLVM_INC) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
