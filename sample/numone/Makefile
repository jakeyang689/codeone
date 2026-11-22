# numone — codeone application

C1       ?= /Users/yangweisheng/.codeone/toolchain/bin/c1
BOXES    ?= /Users/yangweisheng/.codeone/toolchain/boxes/std
PLATFORM ?= -L /Users/yangweisheng/.codeone/toolchain/lib -l platform
OPT      ?= -O2

APP      := numone
APP_MLIR := numone-mlir

# Sources are grouped by purpose: array/ (scalar reference), tensor/ (MLIR
# kernels) and test/ (checks comparing the two). Every .bx file is compiled
# together; no Makefile change is needed when adding a module.
SRCS := $(wildcard *.bx) $(wildcard array/*.bx) $(wildcard tensor/*.bx) $(wildcard test/*.bx)

# Standard-library boxes used by the app, e.g.: LIBS += $(BOXES)/io/print.bx
LIBS :=

.PHONY: all run clean

all: $(APP) $(APP_MLIR)

$(APP): $(SRCS)
	$(C1) $(SRCS) $(LIBS) $(PLATFORM) $(OPT) -o $(APP)

# Same program lowered through the custom MLIR dialect instead of the
# direct LLVM path.
$(APP_MLIR): $(SRCS)
	$(C1) $(SRCS) $(LIBS) $(PLATFORM) $(OPT) --backend=mlir -o $(APP_MLIR)

run: all
	./$(APP)
	./$(APP_MLIR)

clean:
	rm -f $(APP) $(APP_MLIR) *.o
