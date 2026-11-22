# llmone — a GPT-style language model in SSA
#
# CFG=tiny validates the whole pipeline in seconds; CFG=big compiles the
# ~99.5M-parameter model. Each config is generated into build/<cfg>/ because
# the tensor instruction needs literal shapes.

C1       ?= $(HOME)/.codeone/toolchain/bin/c1
LIBDIR   ?= $(HOME)/.codeone/toolchain/lib
PLATFORM ?= -L $(LIBDIR) -l platform
OPT      ?= -O2

CFG      ?= tiny
STEPS    ?= 600
TOKENS   ?= 200
PROMPT   ?= The

DIR    := build/$(CFG)
BIN    := $(DIR)/llmone
CORPUS := corpus/tinyshakespeare.txt

.PHONY: all gen build train gen-text gradcheck test clean

all: build

gen:
	./tools/gen.sh $(CFG)

build: gen
	$(C1) $(DIR)/*.bx $(OPT) -o $(BIN) $(PLATFORM)

$(CORPUS):
	./tools/fetch-corpus.sh

train: build $(CORPUS)
	cd $(DIR) && ./llmone train ../../$(CORPUS) $(STEPS)

gen-text: build
	cd $(DIR) && ./llmone gen "$(PROMPT)" $(TOKENS)

gradcheck: build $(CORPUS)
	cd $(DIR) && ./llmone gradcheck ../../$(CORPUS)

test: build
	$(C1) test/kernel_test.bx math.bx nn.bx $(OPT) -o build/kernel_test
	./build/kernel_test
	$(C1) test/data_test.bx data.bx $(OPT) -o build/data_test
	./build/data_test
	$(C1) test/matmul_ref_test.bx ../numone/array/linalg.bx $(OPT) -o build/matmul_ref_test
	./build/matmul_ref_test

clean:
	rm -rf build
