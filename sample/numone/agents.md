# numone — codeone application

This project is written entirely in codeone SSA (`.bx` files). Generate
code as `.bx` files only; do not add C, Python, or other language sources.

## Toolchain

codeone is installed at:

    /Users/yangweisheng/.codeone/toolchain

| Path | Purpose |
|---|---|
| `/Users/yangweisheng/.codeone/toolchain/bin/c1` | compiler and linker driver |
| `/Users/yangweisheng/.codeone/toolchain/lib/libplatform.a` | platform library (sockets, DNS, CSPRNG, hardware crypto) |
| `/Users/yangweisheng/.codeone/toolchain/boxes/std/` | standard library boxes (`.bx`) |
| `/Users/yangweisheng/.codeone/toolchain/docs/ssa.md` | SSA language specification |
| `/Users/yangweisheng/.codeone/toolchain/docs/box.md` | box (library) system and module list |
| `/Users/yangweisheng/.codeone/toolchain/docs/agents.md` | SSA coding rules and patterns (toolchain copy) |
| `/Users/yangweisheng/.codeone/toolchain/docs/cmap.md` | human-readable pseudo-C view of SSA |

`c1` may not be on `PATH`; always invoke it by absolute path.

## Build

    make            # builds numone and numone-mlir (all .bx under the project)
    make run        # builds and runs both backends

Equivalently, by hand:

    /Users/yangweisheng/.codeone/toolchain/bin/c1 main.bx array/*.bx tensor/*.bx test/*.bx -O2 -o numone

Link standard-library modules explicitly:

    /Users/yangweisheng/.codeone/toolchain/bin/c1 main.bx \
      /Users/yangweisheng/.codeone/toolchain/boxes/std/io/print.bx \
      /Users/yangweisheng/.codeone/toolchain/boxes/std/text/str.bx \
      -O2 -o numone

Link the platform library when using sockets, TLS, randomness, or
hardware-accelerated crypto:

    /Users/yangweisheng/.codeone/toolchain/bin/c1 main.bx <boxes...> \
      -L /Users/yangweisheng/.codeone/toolchain/lib -l platform -O2 -o numone

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
- Read `/Users/yangweisheng/.codeone/toolchain/docs/ssa.md` and `/Users/yangweisheng/.codeone/toolchain/docs/agents.md`
  before introducing unfamiliar constructs. Inspect generated IR with
  `c1 --cmap file.bx`.

## Project layout and modules

Do not put everything in `main.bx`. Split it into modules.

- `main.bx` — entry point only: parse arguments, call module functions, and return an exit code. Keep it small.
- Group modules by purpose in subdirectories, with each module as its own
  `<module>.bx` file named after its responsibility (for example
  `array/linalg.bx`, `tensor/matmul.bx`).
- All `.bx` files in the project are compiled together; the `Makefile` picks
  them up with `$(wildcard *.bx) $(wildcard */*.bx)`.
- numone's layout:

  | Directory | Purpose |
  |---|---|
  | `array/` | numpy-like primitives written as scalar SSA loops (fill, copy, scale, sum, dot, transpose, matmul, equality). This is the reference implementation. |
  | `tensor/` | the same operations backed by the `tensor` instruction, lowered to MLIR kernels |
  | `test/` | checks comparing the tensor kernels against the scalar reference bit for bit |

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

    /Users/yangweisheng/.codeone/toolchain/bin/c1 main.bx greet.bx -O2 -o numone

`Makefile` — `make` builds `numone` from every `*.bx`; run it with `make run`,
and `make clean` removes build artifacts.

## Verification

Compile and run after every change. Report the actual compiler or runtime output when something fails; do not assume success without running it.
