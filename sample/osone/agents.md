# osone — codeone application

This project is written entirely in codeone SSA (`.bx` files). Generate
code as `.bx` files only; do not add C, Python, or other language sources.

## Toolchain

codeone is installed at `$CODEONE_HOME`, which defaults to
`~/.codeone/toolchain` and can be overridden in `Makefile` or the environment:

| Path | Purpose |
|---|---|
| `$CODEONE_HOME/bin/c1` | compiler and linker driver |
| `$CODEONE_HOME/bin/ld.lld` | static linker used for the kernel image |
| `$CODEONE_HOME/lib/libplatform.a` | platform library (sockets, DNS, CSPRNG, hardware crypto) |
| `$CODEONE_HOME/boxes/std/` | standard library boxes (`.bx`) |
| `$CODEONE_HOME/docs/ssa.md` | SSA language specification |
| `$CODEONE_HOME/docs/box.md` | box (library) system and module list |
| `$CODEONE_HOME/docs/agents.md` | SSA coding rules and patterns (toolchain copy) |
| `$CODEONE_HOME/docs/cmap.md` | human-readable pseudo-C view of SSA |

`c1` is usually not on `PATH`; invoke it as `$CODEONE_HOME/bin/c1`.

## Build

This is a bare-metal kernel, so it is not compiled with the default libc
target: `make` runs `c1 --cpu aarch64 --os none -O2 -c` per file and links the
objects with `linker.ld`. Use `make`, `make run`, `make run-peer`,
`make run-gui` and `make clean`; see the `Makefile` for the exact flags.

The run targets need `qemu-system-aarch64`. The Makefile resolves it to an
absolute path — searching `PATH`, `/opt/homebrew/bin`, `/usr/local/bin` and
`$HOME/opt/qemu/bin` — so that `sudo` can execute it; when QEMU lives elsewhere,
pass the real path explicitly:

    make run QEMU=/some/where/bin/qemu-system-aarch64

The general `c1` invocation for a hosted application looks like this:

    $CODEONE_HOME/bin/c1 main.bx -O2 -o app

Link standard-library modules explicitly:

    $CODEONE_HOME/bin/c1 main.bx \
      $CODEONE_HOME/boxes/std/io/print.bx \
      $CODEONE_HOME/boxes/std/text/str.bx \
      -O2 -o app

Link the platform library when using sockets, TLS, randomness, or
hardware-accelerated crypto:

    $CODEONE_HOME/bin/c1 main.bx <boxes...> \
      -L $CODEONE_HOME/lib -l platform -O2 -o app

The platform library provides sockets, DNS, the CSPRNG, and hardware crypto.

Useful flags: `-O2` (always for release builds), `-c` (object only),
`--static` (static library), `-I <dir>` (box search path),
`--cpu` / `--os` (cross target), `--cmap` (emit pseudo-C).

## Language rules

- `.bx` is SSA: each `%value` is assigned exactly once and every type is
  written explicitly.
- `@name` — global symbols: functions, structs, globals, block labels.
- `%name` — local values.
- Entry point: `fun @main() -> i32` (the libc `main` is generated).
- Declare libc functions with `extern @name(args) -> type`; they link
  automatically.
- No implicit conversions: use `zext` / `sext` / `trunc`.
- Every basic block ends with `jmp`, `br`, or `ret`.
- Read `$CODEONE_HOME/docs/ssa.md` and `$CODEONE_HOME/docs/agents.md`
  before introducing unfamiliar constructs. Inspect generated IR with
  `c1 --cmap file.bx`.

## Project layout and modules

Do not put everything in `main.bx`. Split it into modules.

Kernel code lives under `src/kernel/`; everything outside the kernel lives
under `src/system/`.

- `src/kernel/main.bx` — entry point only: parse arguments, call module functions, and return an exit code. Keep it small.
- `src/kernel/boot.bx`, `src/kernel/console.bx` — boot/exception setup and the UART console driver.
- `src/kernel/mmu/` — memory subsystem: `mmu.bx` (page tables) and `kmem.bx` (allocator, memory primitives).
- `src/kernel/fs/` — file system: `vfs.bx` (paths, fd table) and `ramfs.bx` (in-memory backend).
- `src/kernel/net/` — network stack: virtio NIC driver, Ethernet/ARP/IP/ICMP/UDP/TCP, DNS, DHCP.
- `src/kernel/rng/` — virtio-rng driver backing the kernel CSPRNG (`rng_fill` / `c1_random_buf`).
- `src/kernel/gpu/` — virtio-gpu display driver: control queue, scanout setup, framebuffer and flush.
- `src/kernel/input/` — virtio-input drivers: keyboard and tablet event queues.
- `src/kernel/proc/` — process management and timer.
- `src/kernel/rtc/` — PL031 wall-clock driver (`rtc_now`).
- `src/kernel/power/` — PSCI system off/reset through the QEMU `hvc` conduit.
- `src/system/` — non-kernel code: user-space programs, services, and shell utilities. Split by subsystem with the same shallow-tree rules.
  - `src/system/shell/` — command-line shell: `shell.bx` (line editing with Tab command completion, parsing, dispatch, history), `cmd_fs.bx` (ls / cd / pwd / cat / mkdir / touch / cp / rm / rmdir / mv and the working directory), `cmd_net.bx` (ifconfig / arp / dhcp / ping / nslookup / nettest), and `cmd_sys.bx` (ps / mem / uptime / clear / uname / echo / date / time / reboot / poweroff).
  - `src/system/gui/` — `font.bx` holds the bitmap font, `draw.bx` the framebuffer primitives (`gui_fill_rect_alpha` / `gui_text_scale_alpha` blend over the framebuffer; alpha 0..255), `render.bx` the appearance of windows, text and buttons, `widget.bx` the retained widget table with per-widget alpha, hover/press state and hit testing, and `gui.bx` the layout plus the interactive desktop loop (`gui` shell command).
  - `src/system/net/` — URL parsing and the curl-like `urlone` HTTP/HTTPS client: `tlsclient.bx` wraps the toolchain TLS 1.3/RSA-1.2 boxes, `tls12e.bx` is the in-tree TLS 1.2 ECDHE-RSA-AES128-GCM-SHA256 client, and `tls_glue.bx` supplies the platform callbacks.
  - `src/system/agent/` — the `agent` command: `config.bx` reads the fw_cfg item `opt/osone/bootargs` so the endpoint arrives from the host environment, `tools.bx` is the capability layer (`agtools_call`) with the policy check, `cli.bx` holds configuration, the audit ring, command dispatch and the `agent>` chat loop (bare `agent`, `:` commands), and `llm.bx` is the HTTP/JSON client plus the one-action-per-reply tool loop. Keep secrets out of the image: no default endpoint, model or key may be compiled in.
- `tests/` holds host-side tools that are not linked into the kernel image;
  `tests/net_peer.bx` is the L2 peer that `make run-peer` starts (it is a
  hosted program built with the toolchain's socket boxes and `libplatform`).
- Every other module is its own `<module>.bx` file inside the `src/kernel/`
  subdirectory matching its subsystem, named after its responsibility.
- All `.bx` files under `src/` are compiled together; the `Makefile` picks
  them up with `find src -name '*.bx'`.
- Keep the tree shallow: group by subsystem (`src/kernel/mmu`, `src/kernel/fs`), not by file
  type.
- Split when a file grows past a few hundred lines or when a group of
  functions has a distinct purpose.

### Calling functions across modules

Every file has its own declarations. To call a function defined in another
module, declare its prototype with `extern` in the calling file:

    // main.bx
    extern @store_open(path: *i8) -> i32

    fun @main() -> i32 {
      @entry:
        %r = call @store_open("data.db")
        ret i32 0
    }

    // store.bx
    fun @store_open(path: *i8) -> i32 {
      @entry:
        ret i32 0
    }

Globals, constants, and structs are **file-local**. They cannot be referenced
from another module. Share data through function parameters and pointers, or
expose accessor functions from the owning module. If two modules need the
same struct layout, declare the same `struct @Name` in both files.

### Symbol naming across modules

Prefix every symbol with the module name so modules cannot collide:

| Kind | Convention | Example |
|---|---|---|
| function | `@<module>_<verb>` | `@store_open` |
| global / constant | `@<module>_<NAME>` | `@store_MAGIC` |
| struct | `@<module>_<Type>` | `@store_Entry` |
| basic block label | function-local, any name | `@entry`, `@loop` |

### Example

`main.bx`:

    extern @greet_hello() -> i32

    fun @main() -> i32 {
      @entry:
        %n = call @greet_hello()
        ret i32 %n
    }

`greet.bx`:

    extern @printf(fmt: *i8, ...) -> i32

    const @greet_MSG: [i8, 7] = "hello\n"

    fun @greet_hello() -> i32 {
      @entry:
        %p = gep i8, *@greet_MSG, 0
        call @printf(%p)
        ret i32 0
    }

Both files are compiled together:

    $CODEONE_HOME/bin/c1 main.bx greet.bx -O2 -o app

`Makefile` — `make` builds `osone` from every `src/**/*.bx`; run it with
`make run`, and `make clean` removes build artifacts.

## Verification

Compile and run after every change. Report the actual compiler or runtime output when something fails; do not assume success without running it.
