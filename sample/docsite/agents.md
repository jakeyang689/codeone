# docsite — codeone application

This project is written entirely in codeone SSA IR (`.bx` files). Generate
application code as `.bx` files only; do not add C, Python, or other language
sources.

## Toolchain

codeone is installed at `$CODEONE_HOME`, which defaults to
`~/.codeone/toolchain`. The Makefile derives every tool from it, so a
different location only needs `make CODEONE_HOME=/path/to/toolchain`.

| Path | Purpose |
|---|---|
| `$CODEONE_HOME/bin/c1` | compiler and linker driver |
| `$CODEONE_HOME/lib/libplatform.a` | platform library (sockets, DNS, CSPRNG, hardware crypto) |
| `$CODEONE_HOME/boxes/std/` | standard library boxes (`.bx`) |
| `$CODEONE_HOME/docs/ssa.md` | SSA IR language specification |
| `$CODEONE_HOME/docs/box.md` | box (library) system and module list |
| `$CODEONE_HOME/docs/agents.md` | SSA coding rules and patterns (toolchain copy) |
| `$CODEONE_HOME/docs/cmap.md` | human-readable pseudo-C view of SSA |

`c1` may not be on `PATH`; use `$CODEONE_HOME/bin/c1` (or `make`, which
already points at it).

## Build

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

Useful flags: `-O2` (always for release builds), `-c` (object only),
`--static` (static library), `-I <dir>` (box search path),
`--cpu` / `--os` (cross target), `--cmap` (emit pseudo-C).

## Configuration

Deployment settings are read from the environment at startup, never compiled
into the image:

| Variable | Default | Meaning |
|---|---|---|
| `PORT` | `8443` | HTTPS listen port (`make run PORT=9443` forwards it) |
| `SITE_CERT` | `certs/server.crt` | PEM certificate chain |
| `SITE_KEY` | `certs/server.key` | PEM private key |

The document root is `public/` relative to the working directory and the log
file is `logs/site.log`; start the server from the project root.

## Language rules

- `.bx` is SSA IR: each `%value` is assigned exactly once and every type is
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

## Project layout

- `main.bx` — entry point.
- Extra modules are sibling `.bx` files compiled together:

      $CODEONE_HOME/bin/c1 main.bx util.bx ... -O2 -o app

- `Makefile` — `make` builds `app`, `make run` executes it, `make clean`
  removes build artifacts.

## Verification

Compile and run after every change. Report the actual compiler or runtime
output when something fails; do not assume success without running it.
