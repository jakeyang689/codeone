# osone - bare-metal aarch64 kernel
#
# Every tool is looked up relative to CODEONE_HOME, which defaults to the
# install.sh location; QEMU is resolved from PATH. Override any of them on the
# command line, e.g.
#
#   make CODEONE_HOME=/opt/codeone QEMU=/usr/local/bin/qemu-system-aarch64

CODEONE_HOME ?= $(HOME)/.codeone/toolchain
TOOLCHAIN ?= $(CODEONE_HOME)
C1 ?= $(CODEONE_HOME)/bin/c1
LLD ?= $(CODEONE_HOME)/bin/ld.lld

# QEMU is resolved to an absolute path because `run` and `debug` invoke it
# through sudo, whose secure_path does not include user install directories.
# PATH wins; then the usual Homebrew and local-install locations.
QEMU_CANDIDATES := $(shell command -v qemu-system-aarch64 2>/dev/null) \
  /opt/homebrew/bin/qemu-system-aarch64 \
  /usr/local/bin/qemu-system-aarch64 \
  $(HOME)/opt/qemu/bin/qemu-system-aarch64
QEMU ?= $(firstword $(foreach q,$(QEMU_CANDIDATES),$(wildcard $(q))))

.DEFAULT_GOAL := all

APP   := osone
SRCS  := $(shell find src -name '*.bx' | sort)
OBJS  := $(SRCS:.bx=.o)
CFLAGS := --cpu aarch64 --os none -O2 -c

# Toolchain TLS/crypto boxes used by urlone.  They are compiled separately
# because they live outside src/ and must not be copied into the project.
TLS_BOX_SRCS := \
  $(TOOLCHAIN)/boxes/std/net/tls/base.bx \
  $(TOOLCHAIN)/boxes/std/net/tls/record.bx \
  $(TOOLCHAIN)/boxes/std/net/tls/handshake.bx \
  $(TOOLCHAIN)/boxes/std/net/tls/client13.bx \
  $(TOOLCHAIN)/boxes/std/net/tls/client12.bx \
  $(TOOLCHAIN)/boxes/std/net/tls/client_api.bx \
  $(TOOLCHAIN)/boxes/std/crypto/asn1.bx \
  $(TOOLCHAIN)/boxes/std/crypto/aes.bx \
  $(TOOLCHAIN)/boxes/std/crypto/hkdf.bx \
  $(TOOLCHAIN)/boxes/std/crypto/hmac.bx \
  $(TOOLCHAIN)/boxes/std/crypto/md5.bx \
  $(TOOLCHAIN)/boxes/std/crypto/p256.bx \
  $(TOOLCHAIN)/boxes/std/crypto/rsa.bx \
  $(TOOLCHAIN)/boxes/std/crypto/rsa_padding.bx \
  $(TOOLCHAIN)/boxes/std/crypto/rsa_pss.bx \
  $(TOOLCHAIN)/boxes/std/crypto/sha.bx \
  $(TOOLCHAIN)/boxes/std/crypto/tls_prf.bx \
  $(TOOLCHAIN)/boxes/std/crypto/x509.bx
TLS_BOX_OBJS := $(addprefix build/tls/,$(notdir $(TLS_BOX_SRCS:.bx=.o)))

define tls_box_rule
build/tls/$(notdir $(basename $(1))).o: $(1)
	@mkdir -p build/tls
	cd build/tls && $(C1) $(CFLAGS) $(1)
endef
$(foreach b,$(TLS_BOX_SRCS),$(eval $(call tls_box_rule,$(b))))

# By default use macOS vmnet-shared: vmnet provides DHCP/NAT and does not rely on
# Wi-Fi AP bridging for unknown MAC addresses. vmnet needs root, so only QEMU is elevated.
# DHCP acquires an address at boot; on failure fall back to 192.168.1.222/24 with gateway 192.168.1.1.
# To join the host LAN instead, use: make run NETDEV=vmnet-bridged,id=n0,ifname=en0
NETDEV  ?= vmnet-shared,id=n0
PEERDEV := socket,id=n0,listen=127.0.0.1:12400
NICDEV  := -device virtio-net-device,netdev=n0,mac=52:54:00:12:34:56
# Host environment -> guest bootargs. The kernel has no getenv(), so the agent
# reads its endpoint from the fw_cfg item opt/osone/bootargs
# (src/system/agent/config.bx). Values must not contain spaces, commas or
# quotes. AGENT_* wins over the OPENAI_* names.
AGENT_URL     := $(or $(AGENT_URL),$(OPENAI_BASE_URL))
AGENT_MODEL   := $(or $(AGENT_MODEL),$(OPENAI_MODEL))
AGENT_API_KEY := $(or $(AGENT_API_KEY),$(OPENAI_API_KEY))
AGENT_BOOTARGS := $(strip \
  $(if $(AGENT_URL),agent_url=$(AGENT_URL)) \
  $(if $(AGENT_HOST),agent_host=$(AGENT_HOST)) \
  $(if $(AGENT_PORT),agent_port=$(AGENT_PORT)) \
  $(if $(AGENT_PATH),agent_path=$(AGENT_PATH)) \
  $(if $(AGENT_SCHEME),agent_scheme=$(AGENT_SCHEME)) \
  $(if $(AGENT_MODEL),agent_model=$(AGENT_MODEL)) \
  $(if $(AGENT_API_KEY),agent_api_key=$(AGENT_API_KEY)) \
  $(if $(AGENT_MAX_STEPS),agent_max_steps=$(AGENT_MAX_STEPS)))
# NOTE: the key is visible in the host process list while QEMU runs. The comma
# inside the -fw_cfg argument must not be parsed as a $(if) separator.
comma := ,
AGENT_FWCFG := $(if $(AGENT_BOOTARGS),-fw_cfg 'name=opt/osone/bootargs$(comma)string=$(AGENT_BOOTARGS)')

QEMU_BASE := $(QEMU) -M virt,gic-version=2 -cpu cortex-a53 -m 512M -kernel $(APP).elf \
  -device virtio-rng-device $(AGENT_FWCFG)
QEMU_CMD  := $(QEMU_BASE) -nographic

# cocoa is the only available display backend on macOS; do not add display devices to run/run-peer.
QEMU_GUI := $(QEMU_BASE) -display cocoa,show-cursor=on -serial mon:stdio \
  -device virtio-gpu-device,xres=1280,yres=800 \
  -device virtio-keyboard-device -device virtio-tablet-device

.PHONY: all run run-peer run-gui debug clean check-qemu

all: $(APP).elf

# In -c mode c1 writes <stem>.o into the current directory (ignoring -o), so enter the source directory first.
src/%.o: src/%.bx
	cd $(dir $@) && $(C1) $(CFLAGS) $(notdir $<)

$(APP).elf: $(OBJS) $(TLS_BOX_OBJS) linker.ld
	$(LLD) -m aarch64elf -T linker.ld -o $@ $(OBJS) $(TLS_BOX_OBJS)

check-qemu:
	@if [ -z "$(QEMU)" ]; then \
	  echo "error: qemu-system-aarch64 not found"; \
	  echo "       searched PATH, /opt/homebrew/bin, /usr/local/bin and"; \
	  echo "       $(HOME)/opt/qemu/bin"; \
	  echo "       install QEMU (brew install qemu) or pass the real path:"; \
	  echo "       make $(MAKECMDGOALS) QEMU=/path/that/exists/qemu-system-aarch64"; \
	  exit 1; \
	fi
	@if [ ! -x "$(QEMU)" ]; then \
	  echo "error: not an executable: $(QEMU)"; \
	  echo "       pass the real path to qemu-system-aarch64"; \
	  exit 1; \
	fi

run: $(APP).elf check-qemu
	sudo $(QEMU_CMD) -netdev $(NETDEV) $(NICDEV)

# Host-side L2 peer self-test (no root required). The peer is a hosted codeone
# program: it answers ARP, echoes ICMP, UDP port 7 and TCP port 7000, which is
# what `nettest` exercises.
PEER_BIN := build/net_peer
PEER_BOX := $(TOOLCHAIN)/boxes/std/net/socket.bx

$(PEER_BIN): tests/net_peer.bx $(PEER_BOX)
	@mkdir -p build
	$(C1) tests/net_peer.bx $(PEER_BOX) -L $(TOOLCHAIN)/lib -l platform -O2 -o $@

run-peer: $(APP).elf $(PEER_BIN) check-qemu
	@$(PEER_BIN) & peer=$$!; \
	trap 'kill $$peer 2>/dev/null' EXIT INT TERM; \
	$(QEMU_CMD) -netdev $(PEERDEV) $(NICDEV)

# Graphics mode: macOS cocoa window with virtio-gpu/keyboard/tablet, no networking.
run-gui: $(APP).elf check-qemu
	$(QEMU_GUI)

debug: $(APP).elf check-qemu
	sudo $(QEMU_CMD) -netdev $(NETDEV) $(NICDEV) -d int -D qemu.log

clean:
	rm -f $(APP).elf *.o qemu.log
	find src -name '*.o' -delete
	rm -rf build
