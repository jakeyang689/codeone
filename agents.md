# Agents — SSA Coding Rules for AI Agent Generation

This document constrains the style and patterns AI agents use when generating `.bx` SSA code. It is used together with `ssa.md` (language specification) and `box.md` (standard library specification).

## 1. Variable Naming

| Pattern | Naming | Example |
|------|------|------|
| Loop index | `%i`, `%j`, `%k` | |
| Length/count | `%len`, `%count`, `%n` | |
| Pointer | `%p`, `%<name>_ptr` | `%bp`, `%keys_ptr` |
| Value reused from a load | Source name + suffix | `%i` → `%iv`, `%j` → `%jv` |
| Comparison result | Meaningful abbreviation | `%eq`, `%lt`, `%neg`, `%free`, `%done` |
| Temporary result | Do not number with `%1`, `%2` | Use `%tmp` or a context name |
| Pointer computed by GEP | `%p` + array name | `%pi`, `%pj`, `%pk` |
| Function parameter | Use the parameter name directly | `%key`, `%val`, `%arr` |
| Test result | `%r0`, `%r1` ... (by test number) | |
| Character comparison | `%c0v`, `%c5v`, `%dc0v` | Character index + `v` suffix |

## 2. Basic Block Naming

| Pattern | Label | Use case |
|------|------|----------|
| Entry | `@entry` | First basic block of each function |
| Loop triple | `@<prefix>_cond` / `@<prefix>_body` / `@<prefix>_next` | `@init_cond`, `@inner_body`, `@inext` |
| Conditional branch | Descriptive name | `@update_min`, `@swap_step`, `@fix`, `@gfound` |
| Merge exit | `@done` | End point of a loop/function |
| Test sink | `@pass` / `@fail` | Test pass/fail exit |
| Linear assertion chain | Test number | `@t2`, `@c7`, `@v_h`, `@e1` |

Block names must be unique within a function, and should be meaningful in the context of the function name (they need not be globally unique).

## 3. Phi and Memory Patterns

The `phi` instruction lowers directly to an LLVM phi; it merges values from different predecessor paths at a basic block entry:

```
%val = phi i32 [%a, @then], [%b, @else]
```

Write the type explicitly. When omitted, lowering treats it as `i32`, so 64-bit values and pointers must include their type.

`phi` must appear at the start of a basic block, before all non-phi instructions. Every predecessor basic block must have a corresponding `[value, label]` pair.

When `phi` is inconvenient (e.g. a loop needs to persist state across iterations), use `let` + `store` + `load` instead:

```
// phi version (suitable for simple merges):
@entry:
  jmp @loop
@loop:
  %i = phi [0, @entry], [%next, @body]
  ...
@body:
  ...
  %next = add i32 %i, 1
  jmp @loop

// memory version (for persistence/complex control flow):
@entry:
  %iptr = let i32
  store i32 0, *%iptr
  jmp @loop
@loop:
  %i = load i32, *%iptr
  ...
@body:
  ...
  %next = add i32 %i, 1
  store i32 %next, *%iptr
  jmp @loop
```

Rules:
- `phi` is preferred over `let/store/load` — the generated IR is leaner and the LLVM optimizer handles it better
- Use `let/store/load` for loop variable persistence — loops need a mutable stack variable across iterations
- Allocate `let` before use; do not `let` a variable with the same name twice in the same scope
- Compute pointers with `gep`; do not hard-code address offsets
- Avoid `let` across basic blocks — allocate locally in the block where it is needed

## 4. Loop Structure

Every loop uses the canonical **three basic blocks**:

```
@entry:           // 1. Initialize
  %i = let i32
  store i32 0, *%i
  jmp @cond

@cond:            // 2. Condition check
  %iv = load i32, *%i
  %c = slt i32 %iv, %n
  br %c, @body, @done

@body:            // 3. Loop body
  ...
  %iv2 = load i32, *%i
  %next = add i32 %iv2, 1
  store i32 %next, *%i
  jmp @cond
```

The loop variable update always goes at the end of the body, ending with `jmp @cond`.

## 5. Function Structure

- `@entry` holds all `let` allocations and initial stores, then `jmp`s to the first logical block
- Every basic block ends with a **control-flow instruction** (`jmp`, `br`, `ret`) — the compiler checks this while parsing; a missing terminator produces a clear error instead of a silent SIGSEGV
- A control-flow instruction cannot appear in the middle of a block
- Do not reference a nonexistent `@label`

## 6. Test Function Pattern

Use the `c1 test` subcommand:

```
fun @test_xxx() -> i32 { ... }    // returns 0 for pass, non-zero for failure
```

Inline assertion pattern:
```
  %eq = eq i32 %actual, %expected
  br %eq, @pass, @fail
@pass:
  ret i32 0
@fail:
  ret i32 1
```

Rules:
- Test function names start with `@test_`
- Fixed signature `() -> i32`
- Do not share mutable state
- Each test is independent and repeatable

## 7. Box Authoring Conventions

Function naming `@<dir>_<file>_<name>`:

| File path | Function name example |
|----------|-----------|
| `boxes/std/core/math.bx` | `@std_core_math_min` |
| `boxes/std/io/print.bx`  | `@std_io_print_i32` |
| `boxes/std/net/socket.bx` | `@net_socket_create` (`net/` keeps the `@net_` prefix) |

- Write comments explaining parameter meaning and preconditions
- Avoid global mutable state (use `global` sparingly; `const` is fine)

## 7a. Testing Requirements

When adding a function to a `.bx` file under `boxes/std/`, you must add a unit test for it in `c1/test/`:

| Scenario | Test approach | Example |
|------|----------|------|
| New `.bx` file | Create `c1/test/<name>_test.bx` | `boxes/std/crypto/sha256.bx` → `c1/test/sha256_test.bx` |
| New function in an existing `.bx` file | Append to an existing test file | Add `@malloc_aligned` to `boxes/std/mem/alloc.bx` → append in `c1/test/std_test.bx` |

Rules:
- Tests cover **at least one known result** (assert against a hardcoded expected value)
- The test file must have a `@main` entry function, returning `0` for pass and `1` for failure
- Compile with: `./c1 test/<name>.bx ../boxes/std/.../<file>.bx -o test/<name>`

## 8. Anti-Patterns to Avoid
- Do not write control-flow instructions in non-tail position
- Do not rely on implicit type inference — always write types explicitly
- Do not reference a nonexistent `@label`
- Do not reallocate an existing stack variable
- Do not define functions with the same name in different files
- Do not rename function parameter names with a `%` prefix — use the parameter name directly
- **Do not output strings character by character with `putchar`** — use `call @fwrite("...", 1, N, %fp)` or a predefined `print_str` that writes in one call
- **Variadic extern functions must declare `...`** — for example `extern @printf(fmt: *i8, ...) -> i32`. Omitting `...` marks the LLVM function type as non-variadic, and at `-O2` the optimizer generates incorrect argument-passing code
- **Declare a group of related integer constants with `enum`** — e.g. `enum @Color: i32 { @Red = 0, @Green = 1 }`; reference members by their fully qualified name `@Color.Red`, and values must be written explicitly (no implicit auto-increment)
- A `const` without `internal` is **globally visible** — same-named `const`s in different `.bx` files will collide at link time. `internal const` is **file-scoped**; same names do not collide but also cannot be referenced across files. For constants that must be shared across multiple files, use a `const` without `internal` and define it in only one file, or define a copy in each file

## 9. Struct Constraints

Data that can be expressed as a struct **must** use `struct` + `field` access; do not use a flat `[i8, N]` array with hard-coded offsets.

```
// Wrong: flat array + hard-coded offsets
%ctx = let [i8, 288]
%fd_p = gep i8, *%ctx, 0       // fd at offset 0
%fd = load i32, *%fd_p
%ver_p = gep i8, *%ctx, 8      // version at offset 8
store i32 771, *%ver_p

// Correct: use a struct
struct @MyContext {
  @fd: i32,
  @pad: [i8, 4],
  @version: i32,
}
%ctx = let @MyContext
%fd_p = field @MyContext, *%ctx, 0
%fd = load i32, *%fd_p
%ver_p = field @MyContext, *%ctx, 2
store i32 771, *%ver_p
```

Rules:
- When several related fields form one logical entity, define a `struct` and access it with `field`
- In code expressed with a `struct`, `field` accesses a named field index (0, 1, 2...), not a memory byte offset — LLVM handles padding and alignment automatically
- Do not use `gep` with hard-coded constants on `[i8, N]` to access different fields

## 10. Stack Initialization Constraints

`let` allocates stack space but does **not** zero-initialize it — the memory contents after allocation are indeterminate (leftover stack data):

```
// Wrong: assuming buf contents are determinate or all zero
%buf = let [i8, 64]
// At this point buf[0..63] holds unpredictable stack garbage

// Correct: initialize explicitly
%buf = let [i8, 64]
%bp = gep i8, *%buf, 0
%n64 = let i64
store i64 64, *%n64
%n = load i64, *%n64
call @memset(%bp, 0, %n)
```

Rules:
- All stack memory allocated by `let` has **undefined** contents
- When a defined initial value is needed (including all zeros), it must be initialized explicitly via `store` or `call @memset`

## 11. Inline Assembly Constraints

`asm` is only for **hardware instructions** that the standard library does not yet expose (e.g. barriers, special register access). It is not a channel for bypassing SSA semantics:

- Prefer existing instructions or `boxes/std/` functions; write `asm` only when there is no equivalent capability
- Keep assembly code in a **single wrapper module**, exposed through ordinary `fun`s; callers never see `asm`
- Templates are **target-specific**: aarch64 and x86_64 must use separate modules/functions; the same template cannot be compiled across targets
- When there are memory side effects, you must write `~{memory}` explicitly; `asm` does not declare it implicitly
- Separate template lines with `\n`; bare newlines are forbidden. Operand numbering starts with outputs (`$0` is the first output)
- With many inputs, prefer `${name}` to reference `%name`; it is less error-prone than counting `$N`. `${name:mod}` may carry a modifier
- Use a struct result for multiple outputs, with the number of output constraints matching the number of fields; the result is an aggregate value, so it must be `store`d and then read field by field
- When a template writes multiple outputs before reading all inputs, the outputs must use `=&`; otherwise the allocator may let an output share a register with an input

## 12. Tensor Constraints

`tensor` is for whole-buffer arithmetic, not a replacement for scalar loops:

- Buffers are row-major and contiguous; `dst`, `lhs`, `rhs` are ordinary pointers obtained with `let` + `gep`
- Shapes are **static positive integer literals** — no `%values` and no runtime shapes
- The rank is fixed by the op: `[R, C]` for `add`/`sub`/`mul`/`div`, `[M, K, N]` for `matmul`
- Element type is currently `f32` only; other dtypes are rejected at compile time
- `tensor` has no result value; results are written through `dst`
- `matmul` **overwrites** `dst` (it zero-fills the accumulator first), while elementwise ops also fully overwrite `dst`
- Kernels are generated in-process by MLIR, which is linked into `c1`; nothing extra to install or locate at compile time
