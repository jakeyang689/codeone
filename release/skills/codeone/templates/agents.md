# {{PROJECT_NAME}} — codeone {{KIND}}

This project is written entirely in codeone SSA (`.bx` files). Generate
code as `.bx` files only; do not add C, Python, or other language sources.

## Toolchain

codeone is installed at:

    {{CODEONE_HOME}}

| Path | Purpose |
|---|---|
| `{{CODEONE_HOME}}/bin/c1` | compiler and linker driver |
| `{{CODEONE_HOME}}/lib/libplatform.a` | platform library (sockets, DNS, CSPRNG, hardware crypto) |
| `{{CODEONE_HOME}}/boxes/std/` | standard library boxes (`.bx`) |
| `{{CODEONE_HOME}}/docs/ssa.md` | SSA language specification |
| `{{CODEONE_HOME}}/docs/box.md` | box (library) system and module list |
| `{{CODEONE_HOME}}/docs/agents.md` | SSA coding rules and patterns (toolchain copy) |
| `{{CODEONE_HOME}}/docs/cmap.md` | human-readable pseudo-C view of SSA |

`c1` may not be on `PATH`; always invoke it by absolute path.

## Build

    {{CODEONE_HOME}}/bin/c1 {{ENTRY_FILE}} {{BUILD_FLAGS}} -o {{ARTIFACT}}

Link standard-library modules explicitly:

    {{CODEONE_HOME}}/bin/c1 {{ENTRY_FILE}} \
      {{CODEONE_HOME}}/boxes/std/io/print.bx \
      {{CODEONE_HOME}}/boxes/std/text/str.bx \
      {{BUILD_FLAGS}} -o {{ARTIFACT}}

Link the platform library when using sockets, TLS, randomness, or
hardware-accelerated crypto:

    {{CODEONE_HOME}}/bin/c1 {{ENTRY_FILE}} <boxes...> \
      -L {{CODEONE_HOME}}/lib -l platform {{BUILD_FLAGS}} -o {{ARTIFACT}}

{{PLATFORM_EXTRA}}

Useful flags: `-O2` (always for release builds), `-c` (object only),
`--static` (static library), `-I <dir>` (box search path),
`--cpu` / `--os` (cross target), `--cmap` (emit pseudo-C).

## Language rules

- `.bx` is SSA: each `%value` is assigned exactly once and every type is
  written explicitly.
- `@name` — global symbols: functions, structs, globals, block labels.
- `%name` — local values.
{{ENTRY_RULE}}
- Declare libc functions with `extern @name(args) -> type`; they link
  automatically.
- No implicit conversions: use `zext` / `sext` / `trunc`.
- Every basic block ends with `jmp`, `br`, or `ret`.
- Read `{{CODEONE_HOME}}/docs/ssa.md` and `{{CODEONE_HOME}}/docs/agents.md`
  before introducing unfamiliar constructs. Inspect generated IR with
  `c1 --cmap file.bx`.

## Project layout and modules

Do not put everything in `{{ENTRY_FILE}}`. Split it into modules.

{{ENTRY_DOC}}
- Every other module is its own `<module>.bx` file in the project root, named
  after its responsibility (for example `store.bx`, `http_client.bx`).
- All `.bx` files in the project root are compiled together; the `Makefile`
  picks them up with `$(wildcard *.bx)`.
- Do not create nested source directories; keep modules flat in the project
  root.
- Split when a file grows past a few hundred lines or when a group of
  functions has a distinct purpose.

### Calling functions across modules

Every file has its own declarations. To call a function defined in another
module, declare its prototype with `extern` in the calling file:

    // {{ENTRY_FILE}}
    extern @store_open(path: *i8) -> i32

    fun @{{ENTRY_FN}}() -> i32 {
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

`{{ENTRY_FILE}}`:

    extern @greet_hello() -> i32

    fun @{{ENTRY_FN}}() -> i32 {
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

    {{CODEONE_HOME}}/bin/c1 {{ENTRY_FILE}} greet.bx {{BUILD_FLAGS}} -o {{ARTIFACT}}

`Makefile` — `make` builds `{{ARTIFACT}}` from every `*.bx`; {{RUN_DOC}},
and `make clean` removes build artifacts.

## Verification

{{VERIFY_DOC}}
