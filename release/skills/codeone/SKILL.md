---
name: codeone
description: Create and develop applications written in codeone SSA (.bx) with the locally installed codeone toolchain. Use when the user asks to create a codeone app or project, scaffold an SSA project, or build/run .bx application code.
---

# codeone

codeone applications are written entirely in SSA (`.bx` files) and compiled
with the locally installed `c1` toolchain. There is no source language in
between: generate `.bx` directly.

## Commands

All commands go through the dispatcher next to this file:

    bash <skill-dir>/codeone.sh <command> [args...]

### `codeone help`

Print the usage summary (English). Run `bash <skill-dir>/codeone.sh help`
and show its output. Running the skill with no command also prints help.

### `codeone create <app-name> [--bin|--static]`

Scaffold a new project in the current directory. The user creates the
directory and runs this command inside it. `create` requires the application
name as its only positional argument: it names the built artifact and is
written into `agents.md`, the `Makefile`, and `.gitignore`. The project name in
file headers remains the directory name.

The optional mode flag selects what the project builds:

- `--bin` (default; also the behavior when no flag is given) — an executable
  named `<app-name>`, written from the `main.bx` Hello World template.
- `--static` — a static library named `lib<app-name>.a`, written from the
  `lib.bx` template, which exposes `@hello` instead of `@main`. Link it from a
  consumer with `-L <project-dir> -l <app-name>`.

If the user does not provide an application name, ask for it before running
the command; do not guess one. Choose `--bin` unless the user asks for a
library.

1. The skill only uses the installed toolchain at `$HOME/.codeone/toolchain`.
   If `<toolchain>/bin/c1` is missing, stop and tell the user to install it
   from the codeone repository first:

       ./build.sh && ./install.sh

2. Run the command from inside the project directory. If the current
   directory is not empty, stop. Never overwrite existing files.

3. Run the scaffold command:

       bash <skill-dir>/codeone.sh create <app-name> [--bin|--static]

   It writes `agents.md`, `main.bx` (or `lib.bx` with `--static`), `Makefile`,
   and `.gitignore`, with the absolute toolchain path and artifact name
   substituted in, then builds the template to verify the toolchain works.
   In `--bin` mode it also runs the binary. The artifact is `<app-name>` (bin)
   or `lib<app-name>.a` (static) in the project root.

4. Do not run `git init`.

5. Report the project path, the artifact name, the toolchain path, and how to
   build and run:

       make run      # --bin
       make          # --static, then link with -L <project-dir> -l <app-name>

### Developing in the project

- All application code is `.bx` (SSA). Do not add C, Python, or other
  language sources to the project.
- Keep `main.bx` as the entry point only (or `lib.bx` as the public API for a
  `--static` project). Put application logic in separate `<module>.bx` files in
  the project root; the Makefile compiles every `*.bx`. Prefix module symbols
  with the module name (`@store_open`).
- A `--static` project has no `@main`: expose public functions for consumers.
- Other modules' functions require an `extern` prototype in the calling file:
  `extern @store_open(path: *i8) -> i32`. Globals, constants, and structs are
  file-local; pass data through function parameters/pointers or provide
  accessor functions.
- Read `<toolchain>/docs/ssa.md` for the language and
  `<toolchain>/docs/agents.md` for SSA coding rules before writing unfamiliar
  constructs. `<toolchain>/docs/box.md` lists the standard library modules.
- Compile by absolute path to `c1`; use `-O2` and link the standard-library
  boxes the app needs, for example:

      ~/.codeone/toolchain/bin/c1 main.bx \
        ~/.codeone/toolchain/boxes/std/io/print.bx \
        -O2 -o <app-name>

- Link the platform library for sockets, TLS, randomness, or hardware crypto:

      ~/.codeone/toolchain/bin/c1 main.bx <boxes...> \
        -L ~/.codeone/toolchain/lib -l platform -O2 -o <app-name>

- Compile and run after every change; report the real compiler output when a
  build fails.
