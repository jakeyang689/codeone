# SSA — Simple Static Single Assignment Representation

## Overview

SSA (Static Single Assignment) is an intermediate representation whose core constraint is: **each variable is assigned only once**. When a variable may have multiple possible values along different control-flow paths, the `phi` instruction merges them. This form makes data-flow analysis simple and direct.

The goal of this design is to provide a **minimally complete** SSA instruction set—a small number of instructions (38), uniform syntax, and clear semantics—suitable for hand-written code, teaching demos, and LLM generation and understanding.

### Design Principles (for LLM generation)

The SSA syntax design revolves around one core goal: **to let LLMs generate and understand SSA code with the highest accuracy**. This requires:

- **Pattern reuse** — every new syntax must reuse an existing pattern. The structure of `field @S, *%p, %i` matches that of `gep type, *%b, %i`; learning one means learning the pair.
- **Strictly fixed** — seeing the instruction keyword determines the structure of the whole line, with no inference from context. `store i32 %v, *%p` is always in this form, with no variants.
- **Explicit, never implicit** — no type inference, no defaults, no ambiguous semantics. All information is on the current line; the LLM does not need to "remember" earlier definitions.
- **Single sigil convention** — `@` for all global symbols (functions, types, labels, globals), `%` for all local values. No new prefix sigils are introduced.

## Syntax Conventions

- `@name` — global symbol (global variable, function name, basic block label). Names may contain `.` for qualification (e.g., the enum member `@Color.Red`)
- `%name` — local SSA value (instruction result, function parameter)
- Literals: `42` (decimal), `0xFF` (hexadecimal), `3.14` (floating point), `true`/`false` (boolean), `null` (null pointer constant), `'A'` (character, equivalent to `65`)
- String literals: `"hello"` is used in `const @name: [i8, N] = "..."`, a null terminator is appended automatically, and the escapes `\n`/`\r`/`\t`/`\0`/`\\`/`\"`/`\xNN` are supported
- Character literals: `'A'` (single character; escapes are the same as for strings: `'\n'`, `'\r'`, `'\t'`, `'\0'`, `'\\'`, `'\''`, `'\x41'`) — converted directly to an integer value during lexical analysis, usable anywhere an integer is required
- `//` — line comment
- Each instruction occupies its own line
- Each basic block starts with `@label:` and ends with a control-flow instruction (`jmp`, `br`, `ret`) — the compiler checks this during parsing; a missing terminator produces a clear error message rather than a segmentation fault

---

## Type System

| Type | Meaning | Example |
|------|------|------|
| `i1` | 1-bit integer (boolean) | `true`/`false`, the result of comparison instructions |
| `i8` | 8-bit integer | byte |
| `i16` | 16-bit integer | short integer |
| `i32` | 32-bit integer | standard integer |
| `i64` | 64-bit integer | long integer |
| `f32` | 32-bit floating point | single-precision float |
| `f64` | 64-bit floating point | double-precision float |
| `*T` | pointer to type `T` | `*i32` means a pointer to i32 |
| `[T, N]` | array of N elements of type T | `[i32, 10]` means 10 i32s |
| `@Name` | user-defined struct type | `@Point` references the declared struct Point |
| `void` | no type (only for function return types) | |

---

## Declaration Order

Declaration order is unconstrained:

- `struct` may be defined after its first use (the compiler first scans all struct names)
- A function may call a function defined **later** in the file (the compiler first creates all function signatures, then generates the function bodies)

Therefore a file can be organized for readability rather than in dependency order.

## Program Structure

An SSA program consists of struct definitions, enum definitions, global data definitions (read-write globals or read-only constants), external function declarations, and function definitions.

### Struct Definitions

```
struct @Name { @field1: type1, @field2: type2, ... }
```

Inside a function, allocate a struct instance on the stack with `let @Name` and access its fields with the `field` instruction. A struct definition must be declared before it is referenced.

**Example:**

```
struct @Point { @x: i32, @y: i32 }
struct @Rect { @origin: @Point, @size: @Point }

fun @main() -> i32 {
  @entry:
    %pt = let @Point
    %xp = field @Point, *%pt, 0
    store i32 42, *%xp
    ret i32 0
}
```

> Structs support nesting (a field of one struct may be of another struct type). Fields are accessed by integer indices starting from 0.

### Global Variable Definitions

```
global @name: type = init_value
global @name: type                      // uninitialized, zero value by default
```

**Parameter description:**

| Parameter | Description |
|------|------|
| `@name` | Global variable name, prefixed with `@` |
| `type` | Variable type |
| `init_value` | Initial value (optional; defaults to a zero value if omitted) |

Constant declarations use `const` instead of `global`, corresponding to LLVM's `constant`; they are immutable and permit more optimization:

```
const @pi: f64 = 3.14
const @hex: [i8, 16] = [ 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 65, 66, 67, 68, 69, 70 ]
```

String literal initialization (automatically appends a `\0` terminator):

```
const @fpath: [i8, 14] = "file_test.txt"
const @mode: [i8, 2] = "w"
```

Array-typed constants are initialized with the bracket literal `[ val, val, ... ]`, each element being an integer constant. A string literal is equivalent to an integer array and supports the standard escape sequences (`\n`, `\t`, `\0`, `\\`, `\"`, `\xNN`).

### Enum Definitions

```
enum @Name: type { @member1 = value1, @member2 = value2, ... }
```

An enum provides a uniform naming prefix for a set of integer constants. `type` must be an integer type (`i1`/`i8`/`i16`/`i32`/`i64`); every member must explicitly write out an integer literal (decimal, hexadecimal, or negative); there is no implicit auto-increment.

Each member is equivalent to an `internal const` constant, with the fully qualified name `@Name.member`:

```
enum @Color: i32 {
  @Red = 0,
  @Green = 1,
  @Blue = 2,
  @White = 0xFF
}

fun @next() -> i32 {
@entry:
  %c = add i32 @Color.Blue, 1     // 3
  ret i32 %c
}
```

- When referencing, write the fully qualified name `@Color.Blue`; there is no shorthand `@Blue`.
- A member's type matches the type declared by the enum and may be used anywhere an integer operand is required (arithmetic, comparison, `store`, `call` arguments, etc.); a type mismatch is an error.
- The enum itself is not a type: `@Color` is only used to qualify member names and cannot appear in a type position—write `i32` etc. directly in type positions.
- Like `struct` definitions, members are **file-local visible** (equivalent to `internal const`): every `.bx` file that uses the enum must repeat the declaration, and duplicate declarations in different files do not conflict at link time. `internal enum` is a syntax error—enum members are always file-scoped.

### Linkage Scope

`const` and `global` are **globally visible** by default (exported to the linker symbol table). Same-named global declarations in different `.bx` files conflict at link time.

The `internal` keyword can be used to restrict the scope to the current file (equivalent to C's `static const`):

```
internal const @CHAR_TABLE: [i8, 16] = "0123456789ABCDEF"
internal global @counter: i32 = 0
```

Symbols marked `internal` are not exported to the linker symbol table, and same-named `internal` declarations in different files do not conflict. Rules of use:

| Declaration form | Linkage | Same name in multiple files | Cross-file reference |
|----------|------|-----------|-----------|
| `const @X: i32 = 42` | globally exported | conflict | accessible across files |
| `internal const @X: i32 = 42` | file scope | no conflict | not accessible across files |

For constants that need to be shared across multiple `.bx` files, either define the `const` without `internal` in only one file, or define a separate copy in each file that uses it.

### Function Definitions

```
fun @name(param1: type1, param2: type2, ...) -> return_type {
  @entry:
    instr1
    instr2
    ...
}
```

**Parameter description:**

| Parameter | Description |
|------|------|
| `@name` | Function name, prefixed with `@` |
| `paramN: typeN` | Parameter name and type; parameters are used directly as `%paramN` inside the function body |
| `return_type` | Return type; `void` means no return value |
| `@label:` | Basic block label |
| `instr` | SSA instruction sequence |

### External Function Declarations

```
extern @name(param1: type1, param2: type2, ...) -> return_type
```

Declares an external function (such as a libc function) for the current program to call. An external function has no body and is resolved by the linker at link time.

**Parameter description:**

| Parameter | Description |
|------|------|
| `@name` | Function name; the linker looks up the symbol by this name |
| `paramN: typeN` | Parameter name and type |
| `return_type` | Return type |

**Example:**

```
extern @putchar(c: i32) -> i32
extern @exit(code: i32) -> void
extern @write(fd: i32, buf: *i8, len: i64) -> i64

fun @main() -> i32 {
  @entry:
    call @putchar(65)       // outputs 'A'
    ret i32 0
}
```

> External function declarations must precede `fun` definitions; the compiler generates them as LLVM function declarations (with no body). The linking process is performed automatically by the `c1` compiler invoking the system `cc`; no link libraries need to be specified manually. Use `--static` to package the compilation result as a `.a` static library for other programs to link against.

---
## Stack Allocation Instructions

### `let` — Stack Allocation

Allocates a block of memory in the function's stack frame and returns a pointer to that memory.

```
%ptr = let type, align
%ptr = let type               // use the default alignment
```

**Parameter description:**

| Parameter | Description |
|------|------|
| `type` | Type to allocate |
| `align` | Optional, alignment in bytes |
| `%ptr` | Result pointer, of type `*type` |

> Stack allocations are reclaimed automatically when the function returns. The `let` instruction must execute in the function's `@entry` basic block (ensuring controllable stack unwinding).

---

## Arithmetic Instructions

The syntax of all arithmetic instructions is uniform:

```
%result = op type %a, %b
```

| Parameter | Description |
|------|------|
| `op` | Instruction name |
| `type` | Operand type (i8/i16/i32/i64/f32/f64) |
| `%a, %b` | The two operands; their type must match `type` |
| `%result` | Computed result, of type `type` |

### `add` — Addition

```
%r = add i32 %a, %b
```

Semantics: `%r = %a + %b`

### `sub` — Subtraction

```
%r = sub i32 %a, %b
```

Semantics: `%r = %a - %b`

### `mul` — Multiplication

```
%r = mul i32 %a, %b
```

Semantics: `%r = %a × %b`

### `div` — Signed Division

```
%r = div i32 %a, %b
```

Semantics: `%r = %a ÷ %b` (rounds toward zero). If `%b = 0`, the behavior is undefined.

### `rem` — Signed Remainder

```
%r = rem i32 %a, %b
```

Semantics: `%r = %a - (%a ÷ %b) × %b`; the sign of the result matches the dividend. If `%b = 0`, the behavior is undefined.

---

## Unsigned Arithmetic Instructions

### `udiv` / `urem` — Unsigned Division and Remainder

```
%r = udiv type %a, %b
%r = urem type %a, %b
```

Same form as `div`/`rem`, but interprets the operands as unsigned integers.

### `mulhi` — Unsigned High Multiply

```
%r = mulhi type %a, %b
```

Returns the high half of the product of two unsigned numbers (the result width is the same as the operand width). On `i64` it corresponds to AArch64's `umulh` or x86-64's `mulx`/`rdx`; used in big-number multiplication to obtain the high bits of a 64×64→128 product.

## Bitwise Instructions

The syntax is the same as for arithmetic instructions.

### `and` — Bitwise AND

```
%r = and i32 %a, %b
```

Semantics: `%r = %a & %b` (bitwise AND)

### `or` — Bitwise OR

```
%r = or i32 %a, %b
```

Semantics: `%r = %a | %b` (bitwise OR)

### `xor` — Bitwise XOR

```
%r = xor i32 %a, %b
```

Semantics: `%r = %a ^ %b` (bitwise XOR)

### `shl` — Shift Left

```
%r = shl i32 %a, %b
```

Semantics: `%r = %a << %b`. Low bits are filled with zeros. If `%b ≥ the type's bit width`, the behavior is undefined.

### `lshr` — Logical Right Shift

```
%r = lshr i32 %a, %b
```

Semantics: same as `shr`, with high bits filled with zeros.

### `ashr` — Arithmetic Right Shift

```
%r = ashr i32 %a, %b
```

Semantics: `%r = %a >> %b` (arithmetic right shift, high bits filled with the sign bit). Shifting a negative number right preserves the sign. If `%b ≥ the type's bit width`, the behavior is undefined.

---

## Comparison Instructions

Comparison instructions return type `i1` (boolean). The syntax is:

```
%result = op type %a, %b
```

| Instruction | Meaning | Condition |
|------|------|------|
| `eq` | equal | `%a == %b` |
| `ne` | not equal | `%a != %b` |
| `slt` | signed less than | `%a < %b` |
| `sle` | signed less than or equal | `%a <= %b` |
| `sgt` | signed greater than | `%a > %b` |
| `sge` | signed greater than or equal | `%a >= %b` |

> All comparison instructions compare with signed semantics. For floating-point numbers, `eq`/`ne`/`slt`/`sle`/`sgt`/`sge` are likewise used. Null checks on pointer types use `eq *T %ptr, null` (`null` is the null pointer constant).

---

## Memory Access Instructions

### `load` — Load from Memory

```
%value = load type, *%ptr
```

**Parameter description:**

| Parameter | Description |
|------|------|
| `type` | Type of the loaded data |
| `%ptr` | Source pointer; its type must be `*type` |
| `%value` | Loaded value, of type `type` |

Semantics: reads a value of type `type` from the address pointed to by `%ptr`.

### `store` — Write to Memory

```
store type %value, *%ptr
```

**Parameter description:**

| Parameter | Description |
|------|------|
| `type` | Type of the data written |
| `%value` | Value to write, of type `type` |
| `%ptr` | Destination pointer; its type must be `*type` |

> `store` produces no SSA value (no `%result =`).

### `gep` — Pointer Element Computation

```
%ptr = gep type, *%base, %index
```

**Parameter description:**

| Parameter | Description |
|------|------|
| `type` | Element type |
| `%base` | Base pointer, of type `*[type, N]` or `*type` |
| `%index` | Element index (i32), starting from 0 |
| `%ptr` | Computed pointer, of type `*type` |

Semantics: equivalent to `&base[index]`. Moves the pointer forward by `%index` elements of `type`. No bounds checking is performed.

### `field` — Struct Field Access

```
%ptr = field @StructType, *%base, %field_index
```

**Parameter description:**

| Parameter | Description |
|------|------|
| `@StructType` | Name of a declared struct type |
| `%base` | Pointer to a struct instance, of type `*@StructType` |
| `%field_index` | Field index (integer constant), starting from 0 |
| `%ptr` | Pointer to that field, whose type is a pointer to the field's type |

Semantics: computes the memory address of the specified struct field. Equivalent to C's `&base->field`.

**Example:**

```
struct @Point { @x: i32, @y: i32 }

%pt = let @Point
%xp = field @Point, *%pt, 0     // address of the x field, type *i32
%yp = field @Point, *%pt, 1     // address of the y field, type *i32
store i32 100, *%xp

// nested struct
struct @Rect { @origin: @Point, @size: @Point }
%r = let @Rect
%op = field @Rect, *%r, 0               // @origin field, type *@Point
%xp2 = field @Point, *%op, 0           // origin.x field, type *i32
```

> The difference between `field` and `gep`: `gep` is for array element indexing, while `field` is for struct field access. Both map to the LLVM GEP instruction underneath.

---

## Atomic Operation Instructions

Atomic operations provide safe memory access in a multithreaded environment.

### `load atomic` — Atomic Load

```
%value = load atomic type, *%ptr, ordering
```

### `store atomic` — Atomic Store

```
store atomic type %value, *%ptr, ordering
```

### `atomicrmw` — Atomic Read-Modify-Write

```
%old = atomicrmw operation, type, *%ptr, %value, ordering
```

Opcodes:

| Opcode | Effect |
|--------|------|
| `xchg` | `old = *ptr; *ptr = value` |
| `add` | `old = *ptr; *ptr = old + value` |
| `sub` | `old = *ptr; *ptr = old - value` |
| `and` | `old = *ptr; *ptr = old & value` |
| `or`  | `old = *ptr; *ptr = old \| value` |
| `xor` | `old = *ptr; *ptr = old ^ value` |

### `cmpxchg` — Compare and Exchange

```
%ok = cmpxchg type, *%ptr, %cmp, %new, succ_ordering, fail_ordering
```

Compares `*ptr` with `%cmp` and writes `%new` if they are equal. Returns `i1` indicating success. `succ_ordering` and `fail_ordering` are the memory orderings on success and on failure, respectively.

### Memory Ordering Parameters

All atomic instructions must specify a memory ordering at the end. Possible values:

| Keyword | LLVM equivalent | Description |
|--------|-----------|------|
| `seq_cst` | SequentiallyConsistent | sequential consistency (recommended default) |
| `acquire` | Acquire | acquire on read |
| `release` | Release | release on write |
| `acq_rel` | AcquireRelease | acquire on read + release on write |
| `relaxed` | Monotonic | atomicity only |

**Example:**

```
%counter = let i32
store i32 0, *%counter

// atomic add 1, returning the old value
%old = atomicrmw add, i32, *%counter, 1, seq_cst

// atomic CAS
%ok = cmpxchg i32, *%counter, %expected, %new_val, acquire, seq_cst
```

---

## Control-Flow Instructions

### `jmp` — Unconditional Jump

```
jmp @target
```

**Parameter description:**

| Parameter | Description |
|------|------|
| `@target` | Target basic block label |

Semantics: control transfers unconditionally to the `@target` basic block.

### `br` — Conditional Branch

```
br %cond, @true_label, @false_label
```

**Parameter description:**

| Parameter | Description |
|------|------|
| `%cond` | Condition value; its type must be `i1` |
| `@true_label` | Target basic block when the condition is true |
| `@false_label` | Target basic block when the condition is false |

Semantics: if `%cond = true`, jump to `@true_label`; otherwise jump to `@false_label`.

### `phi` — Phi Node

```
%result = phi type [%val1, @label1], [%val2, @label2], ...
%result = phi [%val1, @label1], [%val2, @label2], ...          // when the type is omitted, treated as i32
```

**Parameter description:**

| Parameter | Description |
|------|------|
| `[%val, @label]` | A pair of a value and a predecessor basic block label, meaning that `%val` is taken when entering from `@label` |
| `%result` | The phi node result. The type may be written explicitly; when omitted, lowering treats it as `i32`, so 64-bit values and pointers must have their type written out |

Semantics: a phi node selects the corresponding value according to **which predecessor basic block control flow came from**. All `%val` must have the same type.

> The essence of a phi node is merging multiple definitions of a variable—it represents a variable taking different values along different control-flow paths. Every predecessor basic block must have a corresponding `[value, label]` pair.

### `ret` — Return

```
ret type %value
ret void
```

**Parameter description:**

| Parameter | Description |
|------|------|
| `type %value` | The return value's type and value, consistent with the return type in the function signature |
| `void` | no return value |

Semantics: returns from the current function. If the function's return type is `void`, use `ret void`.

---

## Function Call Instructions

### `call` — Call a Function

```
%result = call @func(%arg1, %arg2, ...)
call @func(%arg1, %arg2, ...)       // when there is no return value
```

**Parameter description:**

| Parameter | Description |
|------|------|
| `@func` | Name of the function being called |
| `%argN` | Arguments, matched positionally to the function definition's parameters |
| `%result` | The function's return value (if the return type is not void) |

Semantics: calls the function with the given arguments; the argument types must match the parameter types one-to-one.

**String literal arguments:** string literals may be used directly in `call` argument positions; the parser automatically creates an anonymous `const` global for them and passes the pointer:

```
call @fwrite("Hello\n", 1, 6, %fp)   // automatically creates const @_s0: [i8, 6] = [72,101,108,108,111,10,0]
call @print_str("RSA OK\n")          // automatically creates const @_s1: [i8, 7] = [82,83,65,32,79,75,10,0]
```

This is functionally equivalent to:
```
const @msg: [i8, 7] = "RSA OK\n"
%p = gep i8, *@msg, 0
call @print_str(%p)
```

String literal arguments are not supported in non-`call` contexts (such as global initialization, assignment, etc.)—those scenarios still require an explicit `const` declaration.

---

## Type Conversion Instructions

### `zext` — Zero Extension

```
%result = zext type %value to type
```

Zero-extends a smaller integer type to a larger integer type, filling the high bits with zeros.

### `sext` — Sign Extension

```
%result = sext type %value to type
```

Sign-extends a smaller integer type to a larger integer type, filling the high bits with the sign bit.

### `trunc` — Truncation

```
%result = trunc type %value to type
```

Truncates a larger integer type to a smaller integer type, discarding the high bits.

---

## Inline Assembly Instruction

### `asm` — Embed Target Assembly

```
%result = asm [alignstack] att type, "template", "constraints", type %operand, ...
asm [alignstack] att void, "template", "constraints", type %operand, ...
```

**Parameter description:**

| Parameter | Description |
|------|------|
| `alignstack` | Optional flag requiring the assembly code to maintain stack alignment |
| `att` / `intel` | Assembly dialect, required, with no default |
| `type` | Result type; write `void` when there is no result; write the struct type for multiple outputs |
| `"template"` | Assembly template; `$0`, `$1`… reference operands |
| `"constraints"` | LLVM constraint string, passed through as-is |
| `type %operand` | Input operands, with each type annotated explicitly |

The syntax follows the same pattern as `call`: type first, operands after, comma-separated.

```ssa
%sum = asm att i64, "add $0, $1, $2", "=r,r,r", i64 %a, i64 %b
asm att void, "dmb ish", "~{memory}"
asm alignstack att void, "nop", ""
```

**Operand numbering:** the numbering order is determined by the constraint string—outputs first, then inputs, consistent with LLVM. In the example above, `$0` is the output and `$1`/`$2` are inputs. Clobbers (`~{reg}`, `~{memory}`, `~{cc}`) do not consume operands. To write a literal `$` in the template, write `$$`.

**Named operands:** input operands may be referenced by `${name}` according to their `%name`, rewritten to the corresponding `${N}` during parsing; with a modifier, write `${name:mod}` (e.g. `${a:c}`). The numeric form `${1}` is kept as-is. If the same-named operand appears twice, an ambiguity error is reported.

```ssa
%r = asm att i64, "add $0, ${lhs}, ${rhs:l}", "=r,r,r", i64 %lhs, i64 %rhs
```

**Multiple outputs (struct result):** when the result type is a struct, the number of `=` output constraints must equal the number of struct fields, and the template writes into `$0`, `$1`… in field order. The result is an aggregate value, which must first be `store`d into a slot and then read field by field.

```ssa
struct @Pair64 { @lo: i64, @hi: i64 }

%p = asm att @Pair64, "add $0, $2, #1\nadd $1, $2, #2", "=&r,=&r,r", i64 40
store @Pair64 %p, *%slot
%lo_p = field @Pair64, *%slot, 0
```

When the template writes multiple outputs before all inputs have been read, the outputs must use `=&` (early-clobber). Otherwise the register allocator may make the outputs share the same register with inputs or even with each other.

**Constraint string:**

| Prefix | Meaning |
|------|------|
| `=` | output (exactly one for a single result; equal to the field count for a struct result) |
| `=&` | early-clobber output |
| `=*` | indirect output (memory) |
| no prefix | input |
| `~{...}` | clobber |

**Validation rules (enforced at parse time):**

- With a `%result` and a non-struct type, the constraint string must have exactly one `=` output; for a struct result, the number of outputs must equal the field count (checked at lowering time); for `void`, there must be no outputs.
- The number of input constraints must equal the number of operands.
- The template and constraint string must be written as string literals, with the same escape rules as ordinary strings; **raw newlines are not allowed**—use `\n` for multi-line templates.
- `asm` always carries `sideeffect` and is never removed by optimization; however, it does not implicitly declare memory side effects—write `~{memory}` explicitly when needed.

**Restrictions:** `asm goto` (the `!` label constraint is an error at parse time) and the `unwind` flag are not supported. Templates are parsed by the target platform's integrated assembler; instruction mnemonics and register names are **target-dependent** content—a template written for aarch64 cannot be compiled for x86_64, and vice versa. An assembler error makes the whole compilation fail.

---

## Tensor Instruction

### `tensor` — Tensor Arithmetic

```
tensor op, elemtype, [dims], *%dst, *%lhs, *%rhs
```

| Parameter | Description |
|------|------|
| `op` | `add`, `sub`, `mul`, `div` (elementwise) or `matmul` |
| `elemtype` | Element type of all three buffers; currently only `f32` |
| `[dims]` | Static shape: `[R, C]` for elementwise, `[M, K, N]` for `matmul`; positive integer literals |
| `*%dst` | Output buffer, row-major, `R*C` (or `M*N`) elements |
| `*%lhs`, `*%rhs` | Input buffers, same layout; for `matmul`, `lhs` is `[M, K]` and `rhs` is `[K, N]` |

The instruction has **no result value**; results are written through `%dst`.
The rank is fixed by the op (2 for elementwise, 3 for `matmul`), so no rank
inference is needed.

```ssa
%a = let [f32, 4]
%b = let [f32, 4]
%c = let [f32, 4]
%ap = gep f32, *%a, 0
...
tensor add, f32, [2, 2], *%c, *%a, *%b
tensor matmul, f32, [2, 2, 2], *%c, *%a, *%b
```

**Lowering:** each `tensor` instruction becomes a call to a generated kernel
`void __c1_tensor_<function>_<n>(ptr dst, ptr lhs, ptr rhs)`. `c1` builds the
kernels with in-process MLIR (`linalg.generic` / `linalg.matmul` /
`linalg.fill`, in `ssa/hir/tensor.cpp`), lowers them through `linalg-to-loops`
and `convert-to-llvm`, and links the resulting module into the main one.
Because the shapes are fully static, the memref arguments lower to plain
pointers — the call site needs no descriptor.

**Validation:** shapes must be positive integer literals and the element type
must be `f32`; both are checked before the kernels are built.

---

## Instruction Quick Reference Table

| Category | Instruction | Form | Meaning |
|------|------|------|------|
| Stack allocation | `let` | `%r = let type` | allocate on the stack |
| Arithmetic | `add` | `%r = add type %a, %b` | addition |
| | `sub` | `%r = sub type %a, %b` | subtraction |
| | `mul` | `%r = mul type %a, %b` | multiplication |
| | `div` | `%r = div type %a, %b` | signed division |
| | `rem` | `%r = rem type %a, %b` | signed remainder |
| | `udiv` | `%r = udiv type %a, %b` | unsigned division |
| | `urem` | `%r = urem type %a, %b` | unsigned remainder |
| | `mulhi` | `%r = mulhi type %a, %b` | unsigned high multiply |
| Bitwise | `and` | `%r = and type %a, %b` | bitwise AND |
| | `or` | `%r = or type %a, %b` | bitwise OR |
| | `xor` | `%r = xor type %a, %b` | bitwise XOR |
| | `shl` | `%r = shl type %a, %b` | shift left |
| | `lshr` | `%r = lshr type %a, %b` | logical right shift |
| | `ashr` | `%r = ashr type %a, %b` | arithmetic right shift |
| Comparison | `eq` | `%r = eq type %a, %b` | equal |
| | `ne` | `%r = ne type %a, %b` | not equal |
| | `slt` | `%r = slt type %a, %b` | signed less than |
| | `sle` | `%r = sle type %a, %b` | signed less than or equal |
| | `sgt` | `%r = sgt type %a, %b` | signed greater than |
| | `sge` | `%r = sge type %a, %b` | signed greater than or equal |
| Memory | `load` | `%r = load type, *%p` | read from a pointer |
| | `store` | `store type %v, *%p` | write through a pointer |
| | `gep` | `%r = gep type, *%b, %i` | pointer offset (array) |
| | `field` | `%r = field @S, *%b, %i` | struct field access |
| Atomic | `load atomic` | `%r = load atomic type, *%p, ord` | atomic load |
| | `store atomic` | `store atomic type %v, *%p, ord` | atomic store |
| | `atomicrmw` | `%r = atomicrmw op, type, *%p, %v, ord` | atomic read-modify-write |
| | `cmpxchg` | `%r = cmpxchg type, *%p, %cmp, %new, so, fo` | compare and exchange |
| Control flow | `jmp` | `jmp @label` | unconditional jump |
| | `br` | `br %c, @t, @f` | conditional branch |
| | `phi` | `%r = phi type [v1, @l1], [v2, @l2]` | Phi node |
| | `ret` | `ret type %v` / `ret void` | return |
| Function call | `call` | `%r = call @f(%args...)` | call a function |
| Type conversion | `zext` | `%r = zext T %v to T2` | zero extension |
| | `sext` | `%r = sext T %v to T2` | sign extension |
| | `trunc` | `%r = trunc T %v to T2` | truncation |
| Inline assembly | `asm` | `%r = asm [alignstack] att T, "tpl", "cons", T %a` | embed target assembly |
| Tensor | `tensor` | `tensor op, T, [dims], *%d, *%a, *%b` | tensor arithmetic (MLIR kernels) |

## SSA Rules Summary

1. **Every `%` value is defined exactly once**; it cannot be reassigned
2. **A value must be defined before use** (in a basic block that dominates the use)
3. **Every basic block starts with a label** and ends with a control-flow instruction (`jmp`/`br`/`ret`)
4. **Phi nodes appear at the beginning of a basic block**, merging values coming from different predecessors
5. **Types must match**: all operand types must be consistent with the type declared by the instruction
