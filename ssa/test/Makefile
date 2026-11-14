CC       = cc
CXX      = clang++
CFLAGS   = -Wall -Wextra -std=c99 -g -I..

ROOT       := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
BUILD_DIR  := $(ROOT)/build/test
LLVM_DIR   = $(ROOT)/llvm
LLVM_INC  := -I$(LLVM_DIR)/llvm/include -I$(LLVM_DIR)/build/include
LLVM_LIB  := -L$(LLVM_DIR)/build/lib
LLVM_LIBS := -lLLVMCore -lLLVMSupport -lLLVMBinaryFormat -lLLVMDemangle \
             -lLLVMRemarks -lLLVMTargetParser -lLLVMBitstreamReader -lLLVMTableGen

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
LD_CXX_RUNTIME = -lc++
LD_SYSTEM      =
LD_GROUP_BEGIN =
LD_GROUP_END   =
else
LD_CXX_RUNTIME = -lstdc++
LD_SYSTEM      = -lpthread -ldl -lm -ltinfo -lz
CFLAGS        += -D_GNU_SOURCE
# The LLVM archives reference each other both ways (TargetParser's Triple.cpp
# calls into Support), and ELF links are single-pass, so resolve as a group.
LD_GROUP_BEGIN = -Wl,--start-group
LD_GROUP_END   = -Wl,--end-group
endif
LLVM_LINK := $(LLVM_LIB) $(LD_GROUP_BEGIN) $(LLVM_LIBS) $(LD_GROUP_END) \
             $(LD_CXX_RUNTIME) $(LD_SYSTEM)

SRCS   = ../lex.c ../ast.c ../lowering_ir.c test_lex.c test_ast.c test_lowering_ir.c test_main.c
OBJS   = $(addprefix $(BUILD_DIR)/, lex.o ast.o lowering_ir.o test_lex.o test_ast.o test_lowering_ir.o test_main.o)
TARGET = test_ssa

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LLVM_LINK)

test: $(TARGET)
	./$(TARGET)

$(OBJS): | $(BUILD_DIR)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/lex.o: ../lex.c ../lex.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/ast.o: ../ast.c ../ast.h ../lex.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/lowering_ir.o: ../lowering_ir.c ../lowering_ir.h ../ast.h
	$(CC) $(CFLAGS) $(LLVM_INC) -c -o $@ $<

$(BUILD_DIR)/test_lex.o: test_lex.c test.h ../lex.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_ast.o: test_ast.c test.h ../ast.h ../lex.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/test_lowering_ir.o: test_lowering_ir.c test.h ../lowering_ir.h ../lex.h ../ast.h
	$(CC) $(CFLAGS) $(LLVM_INC) -c -o $@ $<

$(BUILD_DIR)/test_main.o: test_main.c test.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
