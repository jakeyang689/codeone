# Builds libplatform.a — platform abstraction layer for SSA extern calls

CC       = cc
CFLAGS   = -Wall -Wextra -std=c99 -g -fPIC -pthread -fdebug-prefix-map=$(ROOT)=.
AR       = ar
ARFLAGS  = rcs

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_S),Linux)
# -std=c99 hides the POSIX declarations (getaddrinfo, clock_gettime, ...)
# that macOS exposes unconditionally.
CFLAGS += -D_GNU_SOURCE
ifeq ($(UNAME_M),aarch64)
CRYPTO_FLAGS = -march=armv8-a+crypto
endif
endif

ROOT      := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
BUILD_DIR := $(ROOT)/build/platform

# raylib lives in thirdparty/ (archive + extracted tree, see
# tools/setup-thirdparty.sh and thirdparty/packages.conf). Without it the UI
# wrappers are left out and the rest of the library still builds.
RAYLIB_DIR  ?= $(ROOT)/thirdparty/raylib
RAYLIB_TGZ  ?= $(ROOT)/thirdparty/raylib-5.5.tar.gz
RAYLIB_INC  := $(RAYLIB_DIR)/src
RAYLIB_HDR  := $(RAYLIB_INC)/raylib.h

SRCS     = socket.c ioselect.c random.c dns.c cpu.c crypto_hw.c thread.c sync.c time.c fs.c stdio.c secure.c

# The archive alone is enough: the ui.o rule unpacks it on demand, so a plain
# `make` builds the UI wrappers without running the setup script first.
ifneq ($(strip $(wildcard $(RAYLIB_HDR)))$(wildcard $(RAYLIB_TGZ)),)
SRCS    += ui.c
CFLAGS  += -I$(RAYLIB_INC)
endif
OBJS     = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))
TARGET   = libplatform.a

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(OBJS): | $(BUILD_DIR)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/socket.o: socket.c socket.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/ioselect.o: ioselect.c ioselect.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/random.o: random.c random.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/dns.o: dns.c dns.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/cpu.o: cpu.c cpu.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/crypto_hw.o: crypto_hw.c crypto_hw.h cpu.h
	$(CC) $(CFLAGS) -O3 $(CRYPTO_FLAGS) -c -o $@ $<

$(BUILD_DIR)/thread.o: thread.c thread.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sync.o: sync.c sync.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/time.o: time.c time.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/fs.o: fs.c fs.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/stdio.o: stdio.c stdio.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/secure.o: secure.c secure.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(RAYLIB_HDR):
	mkdir -p $(RAYLIB_DIR)
	tar -xzf $(RAYLIB_TGZ) -C $(RAYLIB_DIR) --strip-components=1

$(BUILD_DIR)/ui.o: ui.c ui.h $(RAYLIB_HDR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
