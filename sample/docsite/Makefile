# docsite — codeone application

# The toolchain lives outside the project and its location varies per machine,
# so it is never written into the sources. Override CODEONE_HOME (or C1,
# BOXES and PLATFORM individually) on the command line.
CODEONE_HOME ?= $(HOME)/.codeone/toolchain
C1       ?= $(CODEONE_HOME)/bin/c1
BOXES    ?= $(CODEONE_HOME)/boxes/std
PLATFORM ?= -L $(CODEONE_HOME)/lib -l platform
OPT      ?= -O2
PORT     ?= 8443

APP  := app
SRCS := main.bx util.bx tls_server.bx http_server.bx logger.bx
LIBS := $(BOXES)/net/socket.bx \
        $(BOXES)/net/ioselect.bx \
        $(BOXES)/thread/thread.bx \
        $(BOXES)/thread/sync.bx \
        $(BOXES)/net/tls/server_api.bx \
        $(BOXES)/net/tls/server12.bx \
        $(BOXES)/net/tls/handshake.bx \
        $(BOXES)/net/tls/base.bx \
        $(BOXES)/net/tls/record.bx \
        $(BOXES)/io/file.bx \
        $(BOXES)/crypto/rsa.bx \
        $(BOXES)/crypto/sha.bx \
        $(BOXES)/crypto/hmac.bx \
        $(BOXES)/crypto/md5.bx \
        $(BOXES)/crypto/tls_prf.bx \
        $(BOXES)/crypto/aes.bx

.PHONY: all run clean

all: $(APP)

$(APP): $(SRCS)
	$(C1) $(SRCS) $(LIBS) $(PLATFORM) $(OPT) -o $(APP)

run: $(APP)
	PORT=$(PORT) ./$(APP)

clean:
	rm -f $(APP) *.o
