# osone — a bare-metal kernel written in codeone SSA

osone is a small operating-system kernel for `aarch64` written entirely in
codeone SSA (`.bx`). It boots under QEMU's `virt` machine, brings up an
identity-mapped MMU with W^X, and then runs a set of self-tests before handing
control to an interactive shell.

It demonstrates that `c1` can target a freestanding environment: no libc, no
CRT, no host OS — just `c1 --cpu aarch64 --os none` plus `ld.lld`.

## What runs

- `src/kernel/mmu/` — page tables (identity, W^X) and a bump allocator.
- `src/kernel/fs/` — VFS with an in-memory `ramfs` backend.
- `src/kernel/net/` — virtio NIC driver, Ethernet/ARP/IP/ICMP/UDP/TCP, DHCP, DNS.
- `src/kernel/gpu/`, `src/kernel/input/`, `src/kernel/rng/` — virtio display,
  keyboard/tablet and entropy drivers.
- `src/kernel/proc/` — round-robin scheduler with a 100 Hz GICv2 timer tick,
  EL1 exception entry, `spawn`/`wait`/`ps`.
- `src/kernel/rtc/`, `src/kernel/power/` — PL031 wall clock and PSCI
  system reset/off through the QEMU `hvc` conduit.
- `src/system/shell/` — line-editing shell with file and network commands.
- `src/system/gui/` — bitmap font, framebuffer drawing primitives (including
  alpha blending), a retained widget layer (window, text, button) with
  per-widget opacity, hover/press states and a desktop loop.
- `src/system/net/` — URL parsing and a curl-like HTTP/HTTPS client that runs
  the toolchain TLS boxes (`urlone`).
- `src/system/agent/` — a CLI agent that talks to an OpenAI-compatible
  endpoint, calls kernel tools (filesystem, network, power) under a policy
  level, and keeps an audit log (`agent`).

## Prerequisites

| Requirement | Notes |
|---|---|
| codeone toolchain | `install.sh` location: `$CODEONE_HOME`, default `~/.codeone/toolchain` |
| `c1` and `ld.lld` | Shipped with the toolchain; the Makefile derives both from `CODEONE_HOME` |
| QEMU (`qemu-system-aarch64`) | Needed for the run targets only, not for building |
| `make` | macOS or Linux |

## Build

```bash
cd sample/osone
make
```

This compiles every `.bx` under `src/` with
`c1 --cpu aarch64 --os none -O2 -c`, compiles the TLS/crypto boxes from
`$TOOLCHAIN/boxes/std/` into `build/tls/`, and links everything with
`linker.ld` into `osone.elf` (about 310 KB). `make clean` removes the objects,
`osone.elf`, `qemu.log` and `build/`.

## Run

```bash
make run-peer   # no root: built-in L2 peer, serial on stdio
make run        # sudo: vmnet-shared networking (DHCP/NAT)
make run-gui    # cocoa window with virtio-gpu, keyboard and tablet
make debug      # like `make run`, plus QEMU's interrupt log in qemu.log
```

**QEMU lookup.** The Makefile resolves `qemu-system-aarch64` to an absolute
path, because `run` and `debug` execute QEMU through `sudo`, whose `secure_path`
does not include user install directories. It searches, in order:

1. `PATH`
2. `/opt/homebrew/bin`
3. `/usr/local/bin`
4. `$HOME/opt/qemu/bin`

If QEMU is installed somewhere else, pass its real path; a path that does not
exist is rejected with `error: not an executable: ...`:

```bash
make run QEMU=/some/where/bin/qemu-system-aarch64
```

`brew install qemu` covers the first three on macOS; a local build configured
with `./configure --prefix=$HOME/opt/qemu` covers the fourth.

`make run` and `make debug` ask for your password (they need `sudo` for
`vmnet-shared`); `make run-peer` and `make run-gui` do not.

`make run-peer` builds and starts `tests/net_peer.bx`, a hosted codeone
program that speaks QEMU's socket framing on `127.0.0.1:12400` and plays the
gateway `192.168.1.1`: it answers ARP, echoes ICMP, echoes UDP port 7 and
serves a TCP echo on port 7000 that closes with FIN. That backend is what
makes `nettest` pass end to end; under `make run` the vmnet gateway answers
ARP and ICMP but has no UDP/TCP echo services.

`make run-gui` opens a window that stays blank until you type `gui` at the
`osone>` prompt; the desktop is drawn on demand. `make run`, `run-peer` and
`debug` start QEMU without a display device, so `gui` there reports
`no virtio-gpu device`.

On a fresh boot you should see:

```
osone: boot
osone kernel
mmu: enabled (identity, W^X)
mkdir /etc -> inode 1
wrote 12 bytes
read back: hello osone
fs: ok
proc: all done
all tests passed
osone shell: type 'help' for commands
osone>
```

Without a DHCP server (`run-peer`) the stack falls back to
`192.168.1.222/24`, gateway `192.168.1.1`, which is enough for local tests.
To join the host LAN instead, override the backend:

```bash
make run NETDEV=vmnet-bridged,id=n0,ifname=en0
```

## Shell commands

`ls [path]`, `cd [path]`, `pwd`, `cat <file>`, `mkdir <dir>`, `touch <file>`,
`cp <src> <dst>`, `mv <src> <dst>`, `rm <file>`, `rmdir <dir>`, `echo <text>`,
`ps`, `mem`, `uptime`, `uname`, `date`, `time`, `clear`, `history`, `ifconfig`,
`arp`, `dhcp`, `ping <host>`, `nslookup <host>`, `nettest`, `urlone <url>`,
`agent [cmd]`, `gui`, `reboot`, `poweroff`, `help`.

The line editor completes command names on Tab: a unique prefix is filled in
and followed by a space, an ambiguous prefix lists the matching commands.
Completion applies to the first word with the cursor at the end of the line.

The VFS has no truncate, so `cp` and `mv` refuse an existing destination.
`reboot` and `poweroff` use PSCI and only work under QEMU (`make run*`).

## Agent

`agent` drives an OpenAI-compatible HTTP endpoint from the shell. There is no
`getenv()` in the kernel, so the Makefile forwards host variables into the
guest through QEMU's fw_cfg device (`opt/osone/bootargs`):

```bash
export AGENT_URL=https://api.example.com/v1   # or OPENAI_BASE_URL
export AGENT_MODEL=...                        # or OPENAI_MODEL
export AGENT_API_KEY=...                      # or OPENAI_API_KEY
make run
```

`AGENT_URL` is a base URL: a bare host or a path ending in `/v1` expands to
`/v1/chat/completions`. `AGENT_HOST`, `AGENT_PORT`, `AGENT_PATH`,
`AGENT_SCHEME` (http/https) and `AGENT_MAX_STEPS` override single parts.
Values must not contain spaces, commas or quotes; the key is visible in the
host process list while QEMU runs. Nothing is written to disk; the key stays in
guest RAM, `agent config` only reports whether it is set, and `agent reset`
clears the runtime copy. `/etc/agent.conf` (`key=value` lines) is an optional
alternative.

| Command | Meaning |
|---|---|
| `agent` | interactive session; type a line to ask, `:` commands for the rest |
| `agent ask <text>` | one request; the model may call tools up to `max_steps` times |
| `agent tool <name> [k=v;...]` | run one tool directly |
| `agent tools` | list tools with their policy levels |
| `agent config [k=v ...]` | show or change endpoint, model, key, `max_steps` |
| `agent policy safe\|write\|unsafe` | tool policy 0 read-only, 1 write, 2 unsafe |
| `agent log [clear]` | audit ring, last 16 actions |
| `agent reset` | clear session and configuration |

Bare `agent` enters a chat loop at the `agent>` prompt. Every line that does
not start with `:` is sent to the model, so the conversation continues without
retyping `agent ask`. The in-session commands are `:help`, `:tools`,
`:config [k=v ...]`, `:policy safe|write|unsafe`, `:log [clear]`, `:reset` and
`:exit` (or `:quit`); they act on the same state as the one-shot forms. The
line editor is shared with the shell, so arrows and history still work and
Tab completion is off.

The model answers in a fixed line protocol — `ACTION: tool` with `TOOL:` and
`ARG: key=value;...`, or `ACTION: final` with `TEXT:`. A tool result is
appended to the transcript and sent back, so one `agent ask` can chain
several tools; the loop stops at `max_steps` (default 4). HTTP, JSON escaping
and the response scanner are plain SSA, and HTTPS reuses the in-tree TLS
client (TLS 1.3, then ECDHE TLS 1.2, then RSA TLS 1.2). Responses with
`Content-Length` or chunked encoding are supported; `\uXXXX` escapes become
`?` on the ASCII console.

## Overridable variables

```bash
make CODEONE_HOME=/opt/codeone       # toolchain root
make TOOLCHAIN=/path/to/toolchain    # where the std boxes are read from
make C1=/path/to/c1 LLD=/path/to/ld.lld
make QEMU=/path/to/qemu-system-aarch64
make NETDEV=vmnet-bridged,id=n0,ifname=en0
```

## Layout

| Path | Contents |
|---|---|
| `src/kernel/` | Kernel: boot, console, MMU, filesystem, network, drivers, processes |
| `src/system/` | Non-kernel code: shell, GUI, user-space networking, agent |
| `tests/` | Host-side tools; `net_peer.bx` is the L2 peer used by `make run-peer` |
| `linker.ld` | Memory map: image at `0x40000000`, page tables, vectors, stack |
| `Makefile` | Build, link and QEMU run targets |
| `agents.md` | Coding conventions for AI agents working on this project |

The image targets QEMU `virt` only; it does not boot on real hardware, and it
depends on the semihosting-free UART at `0x09000000` provided by that machine.
