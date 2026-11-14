# cmap — A Readable Pseudo-C Representation of SSA

## 1. Overview

cmap is a **pseudo-C** that presents `.bx` SSA code in a form humans can read directly.

It is not a programming language: no compiler, no syntax checking, no requirement to be valid. Its entire purpose is **to let readers see at a glance what the SSA is doing**.

### 1.1 Why It Is Needed

SSA sacrifices a great deal of readability for machine processing:

| Phenomenon | Example |
|---|---|
| **Pointer dance** | Reading a local variable requires `%iv = load i32, *%i`, writing requires `store i32 0, *%i`, and allocating requires `%i = let i32` |
| **Exposed address computation** | Struct fields and array elements must first compute a pointer with `gep`/`field`, then `load`/`store` |
| **Flattened control flow** | No `if`/`while`, only basic blocks + `jmp`/`br` |
| **Value explosion** | The same loop variable is written as `%iv`, `%iv2`, `%iv3`, `%iv4` |

The result: a 20-line selection sort in `ssa/c1/test/sort.bx` expands to 67 lines scattered across 7 basic blocks.

cmap restores it.

### 1.2 Design Goals

- **Faithful**: every SSA instruction, every type, every control-flow edge has a correspondence in cmap; there is no construct that "cannot be expressed"
- **Readable**: it reads like ordinary C; the reader need not maintain basic blocks and pointers in their head
- **Mechanical**: the mapping rules are deterministic; performing the mapping requires no understanding of program semantics

### 1.3 What It Does Not Guarantee

- **Not compilable**. `int32`, `φ(...)`, and `>>>` are not valid C. cmap targets human eyes, not compilers.
- **One-way lowering**. Only SSA → cmap is defined. Folding loses information; there is no guarantee the original SSA can be recovered from cmap.
- **No cross-function analysis**. No inlining, no constant propagation, no dead-code elimination. One function at a time.
- **Targets unoptimized SSA**. That is, handwritten / LLM-generated code in the `agents.md` style. LLVM-optimized SSA contains constructs goto cannot express and is out of scope.

### 1.4 Two-Layer Structure

cmap has two layers, each with its own role:

| Layer | Nature | Purpose |
|---|---|---|
| **Core** | Mechanical 1:1 mapping, preserving every basic block label and `goto` | Fallback. **Any** valid SSA can be expressed in Core |
| **Structured** | Rebuilds `while` / `if` on top of Core | Readability. Falls back to Core when no structure is recognized |

Mapping flow: first convert to Core, then attempt structuring. **Structuring failure is not an error**—just deliver the Core fallback; no information is lost.

---

## 2. Types and Literals

### 2.1 Types

Each SSA type maps to exactly one cmap type, with no exceptions and no inference:

| SSA type | cmap type | Notes |
|---|---|---|
| `i1` | `bool` | Boolean, the result of comparison instructions |
| `i8` | `char` | Byte |
| `i16` | `int16` | |
| `i32` | `int32` | |
| `i64` | `int64` | |
| `f32` | `float` | |
| `f64` | `double` | |
| `*T` | `T*` | Pointer |
| `[T, N]` | `T[N]` | Array |
| `@Name` | `struct Name` | Struct |
| `void` | `void` | Only for function return types |

Notes:

- `i8` is written as `char` rather than `int8`. In practice SSA's `i8` is byte buffers and string elements, and `char` makes code such as `char buf[256]` and `const char msg[] = "..."` directly readable. **`char` in cmap is always interpreted as signed** (consistent with SSA comparison instructions such as `slt`/`sle`).
- Wide integers are written as `int32`/`int64` rather than C's `int`/`long`, because SSA widths are exact while `int`'s width varies by platform and would introduce ambiguity.
- `[T, N]` is written after the variable name (`int32 arr[6]` rather than `int32[6] arr`), consistent with C declaration syntax.

### 2.2 Literals

| SSA literal | cmap | Notes |
|---|---|---|
| `42` | `42` | Decimal integer |
| `0xFF` | `0xFF` | Hexadecimal; radix and spelling preserved as-is |
| `3.14` | `3.14` | Floating point |
| `true` / `false` | `true` / `false` | `i1` constants |
| `null` | `null` | Null pointer constant |
| `'A'` | `'A'` | Character literal |
| `"Hello\n"` | `"Hello\n"` | String literal |

Two rules:

- **Numeric and string literals are copied verbatim**, with format, radix, and escape sequences left untouched.
- **Character literals keep the `'A'` form**. SSA's lexer converts `'A'` directly to `65`; the two are semantically equivalent, but `'A'` is far more readable, so cmap preserves the written form. Conversely, if the source SSA writes `65`, cmap writes `65` as well—**cmap performs no reverse inference**; it faithfully presents the spelling it sees.

---

## 3. Top-Level Structure

### 3.1 Struct Definitions

```
struct @Name { @field1: type1, @field2: type2, ... }
```

```
struct @Point { @x: i32, @y: i32 }
struct @Rect { @origin: @Point, @size: @Point }
```

```c
struct Point { int32 x; int32 y; };
struct Rect { struct Point origin; struct Point size; };
```

Field names are the names with the `@` prefix removed. **Field names are real names looked up from the struct definition**, not indices—this lets the later `field` folding (see 5.4) write `pt->x` instead of `pt->field0`.

The field type of a nested struct, `@Point`, is written as `struct Point` in cmap.

### 3.2 Global Data

| SSA | cmap |
|---|---|
| `global @g: i32 = 5` | `int32 g = 5;` |
| `global @buf: [i8, 64]` | `char buf[64];` |
| `const @PI: f64 = 3.14` | `const double PI = 3.14;` |
| `const @K: [i32, 4] = [1, 2, 3, 4]` | `const int32 K[4] = {1, 2, 3, 4};` |
| `const @msg: [i8, 15] = "Hello, World!\n"` | `const char msg[15] = "Hello, World!\n";` |
| `internal const @T: [i8, 16] = "0123456789ABCDEF"` | `static const char T[16] = "0123456789ABCDEF";` |
| `internal global @counter: i32 = 0` | `static int32 counter = 0;` |

- `global` → an ordinary global variable; the initializer is omitted when uninitialized (`char buf[64];`)
- `const` → `const`
- `internal` → `static` (file scope)
- The array literal `[ a, b, c ]` → C's `{a, b, c}`
- String literals are preserved as-is, including escape sequences

### 3.3 External Function Declarations

| SSA | cmap |
|---|---|
| `extern @putchar(c: i32) -> i32` | `extern int32 putchar(int32 c);` |
| `extern @exit(code: i32) -> void` | `extern void exit(int32 code);` |
| `extern @printf(fmt: *i8, ...) -> i32` | `extern int32 printf(char* fmt, ...);` |

Parameter names are preserved (`c`, `code`, `fmt`), so that `%c` in the `fun` body can be matched.

### 3.4 Function Definitions

```
fun @name(p1: t1, p2: t2) -> ret_type { ... }
```

```c
ret_type name(t1 p1, t2 p2) {
  ...
}
```

Mapping rules for function parameters:

- `%p1` (referencing a parameter inside the function body) → `p1`, with no declaration needed—the parameter is already in the signature
- Parameter names take no prefix

Example:

```
fun @selection_sort(arr: *i32, n: i32) -> void {
  @entry:
    %iv = load i32, *%n
    ...
}
```

```c
void selection_sort(int32* arr, int32 n) {
  int32 iv = n;              // %iv = load i32, *%n
  ...
}
```

### 3.5 Name Scope

The `@` and `%` prefixes are always removed:

| SSA | cmap |
|---|---|
| `@selection_sort` | `selection_sort` |
| `@outer_loop` (block label) | `outer_loop` |
| `%min_idx` (local value) | `min_idx` |

**Block names and local names are resolved per function scope.** `@done` and `@fail` in different functions are unrelated—in cmap they appear inside their own function bodies and do not conflict.

---

## 4. Core Layer: Instruction Mapping

### 4.1 The Core Layer Form

The Core layer preserves SSA's basic block structure:

- Each basic block → a C label
- Instructions within a block → sequential statements
- `jmp` / `br` → `goto`

The function body is **flat** (no nested braces), variables are declared in function scope, and labels are labels within the function:

```c
void selection_sort(int32* arr, int32 n) {
  int32 i;                                   // %i = let i32
  int32 j;                                   // %j = let i32
  i = 0;                                     // store i32 0, *%i
  goto outer_loop;                           // jmp @outer_loop

outer_loop:
  bool c1 = (i < n);                         // %c1 = slt i32 %iv, %n
  if (c1) goto inner_init; else goto done;   // br %c1, @inner_init, @done

done:
  return;                                    // ret void
}
```

Specifications:

- **Instructions that produce a result are written as C "declaration + initialization"** (`bool c1 = ...`), because an SSA value has a unique definition point
- Labels occupy their own line, flush left (consistent with C convention), and statements within a block are indented two spaces
- A declaration directly following `label:` requires C23 in real C; pseudo-C allows it—this is pseudo-C's privilege

### 4.2 Stack Allocation

| SSA | cmap |
|---|---|
| `%p = let T` | `T p;`, and register that `%p` denotes the variable `p` |
| `%p = let T, align` | Same as above (`align` omitted in cmap) |

`let` is the only mapping that "declares without initializing"—because SSA's `let` itself does not initialize stack memory either (see section 10 of `agents.md`).

```
%i = let i32
%buf = let [i8, 256]
```

```c
int32 i;
char buf[256];
```

`align` is dropped in cmap: it is an alignment hint for LLVM and meaningless to readers.

### 4.3 Arithmetic

Uniform form `%r = op T %a, %b` → `T r = a op b;`

| SSA | cmap |
|---|---|
| `%r = add i32 %a, %b` | `int32 r = a + b;` |
| `%r = sub i32 %a, %b` | `int32 r = a - b;` |
| `%r = mul i32 %a, %b` | `int32 r = a * b;` |
| `%r = div i32 %a, %b` | `int32 r = a / b;` |
| `%r = rem i32 %a, %b` | `int32 r = a % b;` |

Floating point works the same way (`f64` → `double`, same operators).

`div`/`rem` are **signed** operations, and C's `/` and `%` on signed types likewise truncate toward zero with the remainder following the dividend—the semantics match.

### 4.4 Bitwise Operations

| SSA | cmap |
|---|---|
| `%r = and i32 %a, %b` | `int32 r = a & b;` |
| `%r = or i32 %a, %b` | `int32 r = a \| b;` |
| `%r = xor i32 %a, %b` | `int32 r = a ^ b;` |
| `%r = shl i32 %a, %b` | `int32 r = a << b;` |
| `%r = ashr i32 %a, %b` | `int32 r = a >> b;` |
| `%r = lshr i32 %a, %b` | `int32 r = a >>> b;` |

**`>>>` is a notation introduced by cmap**, denoting logical right shift (zero-fill the high bits). C's `>>` on a signed number is an arithmetic right shift and cannot express logical right shift; writing `(unsigned)a >> b` would introduce an unsigned type that does not exist in SSA at all. Using `>>>` makes it clear at a glance.

The three shifts remain distinguishable in cmap: `<<`, `>>` (arithmetic), `>>>` (logical).

### 4.5 Comparisons

Uniform form `%r = op T %a, %b` → `bool r = (a op b);` (**result parenthesized**)

| SSA | cmap |
|---|---|
| `%r = eq i32 %a, %b` | `bool r = (a == b);` |
| `%r = ne i32 %a, %b` | `bool r = (a != b);` |
| `%r = slt i32 %a, %b` | `bool r = (a < b);` |
| `%r = sle i32 %a, %b` | `bool r = (a <= b);` |
| `%r = sgt i32 %a, %b` | `bool r = (a > b);` |
| `%r = sge i32 %a, %b` | `bool r = (a >= b);` |

Floating point comparison uses the same operators—`double`'s `<` is a floating-point comparison.

**Null pointer comparison**: `null` is written as `null` in cmap.

| SSA | cmap |
|---|---|
| `%r = eq *i8 %p, null` | `bool r = (p == null);` |
| `%r = ne *i8 %p, null` | `bool r = (p != null);` |

The result is parenthesized because comparison values are always embedded into `if (...)` or `while (...)`.

### 4.6 Memory Access

The mapping of `load`/`store`/`gep`/`field` **depends on the source of the pointer**; the main rules are in Chapter 5. When a pointer cannot be folded (see 5.5), use the fallback forms here:

| SSA | cmap (fallback) |
|---|---|
| `%v = load T, *%p` | `T v = *p;` |
| `store T %v, *%p` | `*p = v;` |
| `%q = gep T, *%base, %i` | `T* q = &base[i];` |
| `%q = field @S, *%base, %N` | `field_type* q = &base->fieldN;` |

The fallback form of `field` must look up the struct definition to get the field name. If the field type is `@Point`, write `struct Point* q = &base->origin;`.

### 4.7 Atomic Operations

| SSA | cmap |
|---|---|
| `%v = load atomic T, *%p, ord` | `T v = atomic_load(p, ord);` |
| `store atomic T %v, *%p, ord` | `atomic_store(p, v, ord);` |
| `%old = atomicrmw xchg, T, *%p, %v, ord` | `T old = atomic_xchg(p, v, ord);` |
| `%old = atomicrmw add, T, *%p, %v, ord` | `T old = atomic_add(p, v, ord);` |
| `%old = atomicrmw sub, T, *%p, %v, ord` | `T old = atomic_sub(p, v, ord);` |
| `%old = atomicrmw and, T, *%p, %v, ord` | `T old = atomic_and(p, v, ord);` |
| `%old = atomicrmw or, T, *%p, %v, ord` | `T old = atomic_or(p, v, ord);` |
| `%old = atomicrmw xor, T, *%p, %v, ord` | `T old = atomic_xor(p, v, ord);` |
| `%ok = cmpxchg T, *%p, %c, %n, so, fo` | `bool ok = atomic_cas(p, c, n, so, fo);` |

The memory order `ord` is preserved as-is (`seq_cst` / `acquire` / `release` / `acq_rel` / `relaxed`)—it is the core of concurrency semantics and cannot be omitted. Both the success and failure memory orders of `cmpxchg` are preserved.

Collapsing `atomicrmw add` into `atomic_add` is more compact while preserving the original opcode root for cross-reference.

### 4.8 Control Flow

| SSA | cmap |
|---|---|
| `jmp @L` | `goto L;` |
| `br %c, @T, @F` | `if (c) goto T; else goto F;` |
| `%r = phi [%a, @A], [%b, @B]` | `T r = φ(A → a, B → b);` |
| `ret T %v` | `return v;` |
| `ret void` | `return;` |

**phi is written as an explicit source table in the Core layer**:

```
%x = phi [0, @entry], [%next, @body]
```

```c
int32 x = φ(entry → 0, body → next);
```

`φ(A → a, B → b)` reads: when control flow enters from block `A`, take `a`; when it enters from block `B`, take `b`. C has no counterpart—precisely why phi must be written explicitly and cannot be hidden. With multiple predecessors, list all source pairs in SSA order.

### 4.9 Function Calls

| SSA | cmap |
|---|---|
| `%r = call @f(%a, %b)` | `T r = f(a, b);` |
| `call @f(%a, %b)` | `f(a, b);` |

The return type `T` is taken from `@f`'s signature.

**String literal arguments are restored directly to literals**. In SSA, `call`'s string arguments are implicitly turned by the parser into an anonymous `const` global + `gep` (see `ssa.md`) and are invisible in the source file; writing back the literal in cmap is the most natural reading:

```
call @printf("Hello, %d\n", %n)
```

```c
printf("Hello, %d\n", n);
```

The **definitions of these anonymous `const`s do not appear in cmap**. Their contents are already restored as literals at the call site; listing them again is just noise. Likewise, global symbols in cmap never carry `@`—note that the parser names anonymous constants with `@` (`@_s0_0`) while the lexer names ordinary symbols without it, and both must be normalized to the form without `@`.

Cross-box calls just drop the `@`:

```
%m = call @std_core_math_min(3, 7)
```

```c
int32 m = std_core_math_min(3, 7);
```

### 4.10 Type Conversions

| SSA | cmap |
|---|---|
| `%r = zext i8 %v to i32` | `int32 r = zext<int32>(v);` |
| `%r = sext i32 %v to i64` | `int64 r = sext<int64>(v);` |
| `%r = trunc i32 %v to i8` | `char r = trunc<char>(v);` |

**Do not use C casts `(int32)v`**: C cast semantics depend on the signedness of the source and target types and cannot distinguish "zero extension" from "sign extension", which are different instructions with different semantics in SSA. Writing `zext<T>(v)` / `sext<T>(v)` / `trunc<T>(v)` puts the intent on its face.

### 4.11 Inline Assembly

The assembly template and constraint strings are written out as-is, untranslated and unreordered. The dialect and constraints go in a trailing comment because they determine how each part of the template is read:

| SSA | cmap |
|---|---|
| `%r = asm att i64, "add $0, $1, $1", "=r,r", i64 %a` | `int64 r = __asm("add $0, $1, $1");  /* operands: a */  /* att, constraints "=r,r" */` |
| `asm att void, "dmb ish", "~{memory}"` | `__asm("dmb ish");  /* att, constraints "~{memory}" */` |
| `asm alignstack att void, "nop", ""` | `__asm("nop");  /* alignstack att, constraints "" */` |

Operands are listed in SSA order, and `$N` numbering is preserved—the numbering is determined by the constraint string (outputs first, then inputs), and cmap does not renumber, otherwise readers cannot match the `$N` in the template. `${name}` is already rewritten to `${N}` at parse time, and cmap likewise outputs by index.

---

## 5. Pointer Folding

### 5.1 What Folding Is

Accessing a storage location in SSA takes three steps: `let` to obtain the pointer → `gep`/`field` offset → `load`/`store` access. These three steps are necessary for the machine and pure noise for the reader.

Folding is: **if an SSA value points to a nameable storage location, read it as the name of that storage location.**

### 5.2 Three Kinds of Foldable Pointer Sources

Only three kinds of SSA values can "name" storage:

| Source | Pointer value | Name it denotes | Registered as |
|---|---|---|---|
| `%p = let T` | `%p` | the variable `p` | `%p` ≡ variable `p` |
| `%q = gep T, *%base, %i` | `%q` | the location of `base[i]` | `%q` ≡ `&base[i]` |
| `%q = field @S, *%base, %N` | `%q` | the location of `base->fieldN` | `%q` ≡ `&base->fieldN` |

The `base` of `gep`/`field` can come from different sources; all are foldable:

| Source of `base` | Example | Folds to |
|---|---|---|
| Array from `let` | `%arr = let [i32, 6]` | `arr[i]` |
| **Global array / string constant** | `const @_b64_chars: [i8, 64] = "..."` | `_b64_chars[i]` |
| Already-folded name (chained) | `%sr = field ...` followed by `gep` | see 5.4 |

A global array's `gep` is commonly used for table lookup and for taking the address of a string constant:

```
%c  = gep i8, *@_b64_chars, %idx      // table lookup
%v  = load i8, *%c

%msg = gep i8, *@hello, 0             // address of the string constant (equivalent to the array name itself)
call @print_str(%msg)
```

```c
char c = _b64_chars[idx];

print_str(hello);
```

When `gep T, *@arr, 0` has index 0, the result folds to the array name itself, and there is no need to write the address form `arr[0]`.

The `base` itself may be an already-folded name, forming a chain (see 5.4).

### 5.3 Three Expansion Rules

After registration, all uses of that pointer expand as follows:

**Rule A — As the source of `load`: write the name**

| SSA | cmap |
|---|---|
| `%v = load i32, *%i` (`%i` is the let variable `i`) | `i` |
| `%v = load i32, *%p` (`%p` ≡ `&arr[j]`) | `arr[j]` |
| `%v = load i32, *%f` (`%f` ≡ `&pt->x`) | `pt->x` |

**Rule B — As the target of `store`: the name goes on the left**

| SSA | cmap |
|---|---|
| `store i32 0, *%i` | `i = 0;` |
| `store i32 %v, *%p` (`%p` ≡ `&arr[j]`) | `arr[j] = v;` |
| `store i32 42, *%f` (`%f` ≡ `&pt->x`) | `pt->x = 42;` |

**Rule C — Passing as an address (`call` argument): add `&`**

| SSA | cmap |
|---|---|
| `call @f(%i)` (`%i` is the variable from `let i32`) | `f(&i);` |
| `call @selection_sort(%arr, 6)` (`%arr` is from `let [i32, 6]`) | `selection_sort(arr, 6);` |

Array exception: the variable name from `let [T, N]` is itself an address in C, so no `&` is added.

### 5.4 Chained Folding

The result of `field` can be further subjected to `field`/`gep`, forming a chain. When folding, read the whole chain as a single member-access expression:

```
%t12 = field @TLSContext,   *%ctx, 0     // field 0 of @TLSContext is named tls12
%sr  = field @TLS12Context, *%t12, 14    // the field's type is [i8, 64]
%p   = gep   i8,            *%sr,  0
%v   = load  i8,            *%p
```

Register step by step:

| Value | Registered as |
|---|---|
| `%t12` | `&ctx->tls12` |
| `%sr` | `&ctx->tls12.sr` |
| `%p` | `&ctx->tls12.sr[0]` |

Finally:

```c
char v = ctx->tls12.sr[0];
```

The whole chain folds into a single expression, and all three intermediate pointer values disappear from cmap. `field → gep` (an array field inside a struct) likewise folds to `ctx->buf[i]`.

### 5.5 Cases That Cannot Be Folded

In the following cases the pointer value cannot be named; fall back to the forms in section 4.6 and keep an explicit pointer variable:

- **The pointer is stored into memory** (`store *T %p, *%slot`)—it no longer uniquely corresponds to one storage location
- **The pointer participates in pointer arithmetic or comparison**, and its source is not `let`
- **The pointer crosses a function boundary**—only chains visible within a function are foldable
- `let` appears in a **non-`@entry` block** (observed in practice, e.g. `socket.bx @recv`): the variable is still declared at the first `let`, just not at the top of the function. **Do not assume all declarations are at the beginning**

### 5.6 Annotating Sources During Folding

Folding makes SSA value names disappear (`%iv`, `%iv2`, `%iv3` all become `i`). To still be able to read alongside the original SSA, **annotate the SSA source at variable declarations**:

```c
int32 i;                    // %i = let i32
int32 min_idx;              // %min_idx = let i32
int32 j_start = i + 1;      // %j_start = add i32 %iv2, 1
```

Annotation rules:

- **Variables folded from `let`**: annotate the end of the declaration line with `// %<original name> = let <type>`
- **Computed values** (`add`, `slt`, etc.): the name itself is preserved (`j_start`), so no annotation is needed
- **Aliases folded from `load`/`gep`/`field`** (`%iv`, `%pj`): **not annotated**. The storage they point to is already annotated at the declaration; annotating at every point would make the code unreadable

When every value needs to be cross-referenced one by one, use the side-by-side SSA ↔ cmap format of Chapter 8 rather than stuffing cmap with comments.

### 5.7 Snapshot Values Are Not Folded

Rule A (folding `load` into a storage name) has one exception: **if the load's result is written into another storage location, it cannot be folded**.

```
%vi = load i32, *%pi        // %pi ≡ &arr[i]
%vm = load i32, *%pm        // %pm ≡ &arr[min_idx]
store i32 %vm, *%pi
store i32 %vi, *%pm
```

If `%vi` were folded to `arr[i]`, the last line would become `arr[min_idx] = arr[i]`—but at that point `arr[i]` has already been overwritten by the previous line, **and the semantics would be wrong**. `%vi` is a snapshot and must remain independent:

```c
int32 vi = arr[i];
int32 vm = arr[min_idx];
arr[i] = vm;
arr[min_idx] = vi;
```

Decision rules:

| Target of store | Handling |
|---|---|
| Scalar variable from `let` (`store i32 %v, *%min_idx`) | No aliasing involved; `%v` folds normally |
| Array elements, struct fields, pointers of unknown origin (parameters, etc.) | Treated as possible aliasing → the load result is kept as an independent variable |

This rule does not perform precise alias analysis—it conservatively keeps a value whenever aliasing is **possible**. Better one extra temporary variable than semantically wrong code.

---

## 6. Structured Layer: Control-Flow Reconstruction

### 6.1 When to Use

The Structured layer sits on top of Core. Recognized successfully → more readable; recognition fails → fall back to Core.

**Falling back is not an error; it is expected behavior.** Better to deliver a plain goto version than to guess a structure that may be wrong.

### 6.2 while Loops

Recognition pattern: a block `@cond` ends with `br %c, @body, @exit`, and `@body` can reach `@cond` (a back edge exists), and `@exit` is outside the loop body.

**It is not fixed which of `br`'s two targets is written first.** `br %c, @body, @exit` and `br %c, @exit, @body` are equally common—the latter reads as `if (cond) break;`, and `pem.bx` has one. The criterion is **which target can reach the source of the back edge**, not operand order; when the loop body falls on the false branch, invert the condition and output `while (!(c))`.

```
@init:  ...              jmp @cond          // optional
@cond:  ...  br %c, @body, @exit
@body:  ...              jmp @cond          // or back to @cond via other blocks
```

```c
<init statements>
while (c) {
  <body statements>
}
<contents of @exit>
```

Key points:

- The condition expression comes from the loop head's `br`
- **Multiple paths back to `@cond` within the loop body are normal**; C's `while` naturally supports this. In practice `convert.bx`'s `@parse_loop` has 4 back edges and still reconstructs fine
- Code at `@exit` follows the `while`

### 6.3 if / if-else

Recognition pattern: `@cur` ends with `br %c, @T, @F`, and both branches eventually converge to the same block `@J` (`@J` is not `@T` or `@F`).

```
@cur:  ...  br %c, @T, @F
@T:    ...  jmp @J
@F:    ...  jmp @J
@J:    ...
```

```c
if (c) {
  <contents of @T>
} else {
  <contents of @F>
}
<contents of @J>
```

Variants:

| Form | cmap |
|---|---|
| `@F == @J` | `if (c) { <@T> }` |
| Both branches end with `ret` | `if (c) { return ...; } else { return ...; }` |
| Only `@F` ends with `ret` | `if (c) { <@T> }`, with `@F`'s `return` following the `if` |
| Only `@T` ends with `ret` | `if (c) { return ...; }`, with the contents of `@F` following the `if` |
| `@T == @F` | `if (c) goto T;` |
| `@F` points to `@cur` itself | This is a loop; handle per 6.2 |

### 6.4 Chained br Is if-else if

IR has no `switch`; multi-way branches are written as chained `br`:

```
@a:  br %c1, @x, @b
@b:  br %c2, @y, @z
```

```c
if (c1) { goto x; }
else if (c2) { goto y; }
else { goto z; }
```

If each branch can be further structured, expand recursively.

### 6.5 Inline Sugar

The Structured layer allows **single-use** computed values to be inlined at the use site:

```
%j_start = add i32 %iv2, 1
store i32 %j_start, *%j
```

- Core layer: `int32 j_start = i + 1;` newline `j = j_start;`
- Structured layer: `j = i + 1;`

This is the only extra folding in the Structured layer. It brings the code closer to handwritten C, at the cost of losing the name `%j_start`—so **look at the Core layer when strict cross-referencing is needed**.

### 6.6 Fallback Conditions

If any of the following occurs, that function falls back to the Core layer without structural guessing:

1. **The loop has exit edges other than the loop-head condition** (`break` semantics). The conservative rule does not reconstruct `break`.
   Example: `hashmap.bx`'s `@gnext: br %gmore, @gloop, @gmiss`—`@gmiss` means "exit the loop and continue with the code after it".
   Note: a direct `return` inside the loop body does **not** require fallback; C's `return` works directly (example: `hashmap.bx`'s `@iput`).
2. **A block has more than 2 predecessors and its entry needs phi**—multi-way merges cannot be reduced to if/else.
3. **Irreducible CFG** (a loop with multiple entries).
4. **A `jmp` forwarding chain** makes the block structure ambiguous.

Observed: 2/3/4 do not occur in the current codebase, 1 does (`hashmap.bx`), so fallback is a minority case.

---

## 7. phi Handling

### 7.1 Core Layer: Explicit Source Table

```c
int32 x = φ(entry → 0, body → next);
```

No resolution is done—the source information is fully preserved.

### 7.2 Structured Layer: Resolution

**phi at a loop head** → initialize before the loop + write back at the end of the loop:

```c
int32 x = 0;                 // from entry
while (...) {
  ...
  x = next;                  // from the back edge of body
}
```

**phi at an if merge point** → declare early + assign in each branch:

```c
int32 x;                     // declaration hoisted before the if
if (c) { x = a; } else { x = b; }
```

### 7.3 When Resolution Is Impossible

- phi has **more than 2 predecessors** → fall back to Core, using the `φ` notation
- phi's values come from **non-adjacent blocks** → same as above

Observed: all phi in the codebase have 2 predecessors, and loop variables almost always go through `let` + `store`/`load` (contrary to `agents.md`'s claimed "phi first"), so the two rules in 7.2 cover the actual use cases.

---

## 8. Complete Example

`ssa/c1/test/sort.bx`'s `@selection_sort`—a selection sort with a doubly nested loop.

### 8.1 Original SSA (67 lines)

```
fun @selection_sort(arr: *i32, n: i32) -> void {
  @entry:
    %i = let i32
    %j = let i32
    %min_idx = let i32
    store i32 0, *%i
    jmp @outer_loop

  @outer_loop:
    %iv = load i32, *%i
    %c1 = slt i32 %iv, %n
    br %c1, @inner_init, @done

  @inner_init:
    %iv2 = load i32, *%i
    store i32 %iv2, *%min_idx
    %j_start = add i32 %iv2, 1
    store i32 %j_start, *%j
    jmp @inner_cond

  @inner_cond:
    %jv = load i32, *%j
    %c2 = slt i32 %jv, %n
    br %c2, @inner_body, @swap_step

  @inner_body:
    %jv2 = load i32, *%j
    %pj = gep i32, *%arr, %jv2
    %vj = load i32, *%pj

    %mv = load i32, *%min_idx
    %pm = gep i32, *%arr, %mv
    %vm = load i32, *%pm

    %lt = slt i32 %vj, %vm
    br %lt, @update_min, @inner_next

  @update_min:
    %jv3 = load i32, *%j
    store i32 %jv3, *%min_idx
    jmp @inner_next

  @inner_next:
    %jv4 = load i32, *%j
    %j_next = add i32 %jv4, 1
    store i32 %j_next, *%j
    jmp @inner_cond

  @swap_step:
    %iv3 = load i32, *%i
    %pi = gep i32, *%arr, %iv3
    %vi = load i32, *%pi

    %mv2 = load i32, *%min_idx
    %pm2 = gep i32, *%arr, %mv2
    %vm2 = load i32, *%pm2

    store i32 %vm2, *%pi
    store i32 %vi, *%pm2

    %i_next = add i32 %iv3, 1
    store i32 %i_next, *%i
    jmp @outer_loop

  @done:
    ret void
}
```

### 8.2 Core Layer (48 lines)

```c
void selection_sort(int32* arr, int32 n) {
  int32 i;                                          // %i = let i32
  int32 j;                                          // %j = let i32
  int32 min_idx;                                    // %min_idx = let i32
  i = 0;                                            // store i32 0, *%i
  goto outer_loop;                                  // jmp @outer_loop

outer_loop:
  bool c1 = (i < n);                                // %c1 = slt i32 %iv, %n
  if (c1) goto inner_init; else goto done;          // br %c1, @inner_init, @done

inner_init:
  min_idx = i;                                      // store i32 %iv2, *%min_idx
  int32 j_start = i + 1;                            // %j_start = add i32 %iv2, 1
  j = j_start;                                      // store i32 %j_start, *%j
  goto inner_cond;                                  // jmp @inner_cond

inner_cond:
  bool c2 = (j < n);                                // %c2 = slt i32 %jv, %n
  if (c2) goto inner_body; else goto swap_step;

inner_body:
  int32 vj = arr[j];                                // %vj = load i32, *%pj
  int32 vm = arr[min_idx];                          // %vm = load i32, *%pm
  bool lt = (vj < vm);                              // %lt = slt i32 %vj, %vm
  if (lt) goto update_min; else goto inner_next;

update_min:
  min_idx = j;                                      // store i32 %jv3, *%min_idx
  goto inner_next;                                  // jmp @inner_next

inner_next:
  int32 j_next = j + 1;                             // %j_next = add i32 %jv4, 1
  j = j_next;                                       // store i32 %j_next, *%j
  goto inner_cond;                                  // jmp @inner_cond

swap_step:
  int32 vi = arr[i];                                // %vi = load i32, *%pi
  int32 vm2 = arr[min_idx];                         // %vm2 = load i32, *%pm2
  arr[i] = vm2;                                     // store i32 %vm2, *%pi
  arr[min_idx] = vi;                                // store i32 %vi, *%pm2
  int32 i_next = i + 1;                             // %i_next = add i32 %iv3, 1
  i = i_next;                                       // store i32 %i_next, *%i
  goto outer_loop;                                  // jmp @outer_loop

done:
  return;                                           // ret void
}
```

Note how the pile of pointers in the middle disappears:

| SSA value | Registration | Usage |
|---|---|---|
| `%pj` | `&arr[j]` | `%vj = load i32, *%pj` → `arr[j]` |
| `%pm` | `&arr[min_idx]` | `%vm = load i32, *%pm` → `arr[min_idx]` |
| `%pi` | `&arr[i]` | `store i32 %vm2, *%pi` → `arr[i] = vm2;` |
| `%pm2` | `&arr[min_idx]` | `store i32 %vi, *%pm2` → `arr[min_idx] = vi;` |

Meanwhile all seven aliases `%iv`, `%iv2`, `%iv3`, `%jv`, `%jv2`, `%jv3`, `%jv4` fold into `i` and `j`.

### 8.3 Structured Layer (25 lines)

```c
void selection_sort(int32* arr, int32 n) {
  int32 i;                                          // %i = let i32
  int32 j;                                          // %j = let i32
  int32 min_idx;                                    // %min_idx = let i32

  i = 0;                                            // store i32 0, *%i
  while (i < n) {                                   // @outer_loop
    min_idx = i;                                    // @inner_init
    j = i + 1;                                      // @inner_init (inline sugar)

    while (j < n) {                                 // @inner_cond
      if (arr[j] < arr[min_idx]) {                  // @inner_body
        min_idx = j;                                // @update_min
      }
      j = j + 1;                                    // @inner_next (inline sugar)
    }

    // @swap_step: swap arr[i] and arr[min_idx]
    int32 vi = arr[i];
    int32 vm2 = arr[min_idx];
    arr[i] = vm2;
    arr[min_idx] = vi;

    i = i + 1;                                      // @swap_step (inline sugar)
  }
}                                                   // @done: ret void
```

Scale of the three layers: **SSA 67 lines → Core 48 lines → Structured 25 lines**.

Structures recognized by the Structured layer:

| Structure | Corresponding blocks |
|---|---|
| Outer `while (i < n)` | Loop head `@outer_loop`, back edge from `@swap_step` |
| Inner `while (j < n)` | Loop head `@inner_cond`, back edge from `@inner_next` |
| `if (arr[j] < arr[min_idx])` | The `br` in `@inner_body`; both branches converge to `@inner_next` |
| Inner loop's `@exit` = `@swap_step` | Falls inside the outer loop body, so subsequent statements follow the inner `while` |

The inner loop's exit `@swap_step` is not the function exit but **part of the outer loop body**—this is exactly what nested loops look like in the CFG; when structuring, handle it as "continue after the inner `while` ends".

---

## 9. Limitations and Non-Goals

### 9.1 Not Compilable

`int32`, `φ(...)`, `>>>`, `zext<T>(v)`, and a declaration directly following `label:`—none of these are valid C. cmap targets human eyes. Do not try to validate it with a C compiler.

### 9.2 One-Way Lowering, Not Guaranteed Reversible

Folding loses information: once `%iv`/`%iv2`/`%iv3` all become `i`, the number of aliases originally used cannot be recovered. The side-by-side format in Chapter 8 is for manual cross-referencing, not a reversible encoding.

### 9.3 Generic Containers Cannot Be Expressed Nicely

SSA has no generics; containers such as `ssa/boxes/std/data/stack.bx` implement type-agnostic behavior with `*i8` + `elem_size`:

```
%stack = let [i8, 256]
call @std_data_stack_init(%stack, 64, 4)
```

```c
char stack[256];
std_data_stack_init(stack, 64, 4);
```

The element type and length information is **completely lost** in cmap—it just looks like a byte buffer. This information is already missing at the SSA level; cmap cannot bring it back. Readers need to reconstruct it from the calling convention.

### 9.4 No Cross-Function Analysis

No inlining, no constant propagation, no dead-code elimination. Read one function at a time. Cross-box calls are shown only as ordinary function calls (`std_core_math_min(3, 7)`).

### 9.5 Targets Unoptimized SSA Only

That is, handwritten / LLM-generated code in the `agents.md` style: all `let`s concentrated in a few places, every basic block ending with `jmp`/`br`/`ret`, and meaningful block names.

LLVM-optimized SSA contains constructs goto cannot express (such as `select`, multi-way phi, cross-block value reuse) and is out of scope.

### 9.6 Other Degenerate Cases

| Case | Handling |
|---|---|
| `let` appears in a non-`@entry` block | The variable is still declared at the first `let`; do not assume declarations are all at the top of the function |
| `let [i8, 0]` empty array | `char x[0];` (written as-is) |
| `let @Struct` | `struct S x;` (written as-is) |
| Block names repeated across functions (`@done` 31 times, `@fail` 223 times) | Resolved per function scope; unrelated |
| `call` passing a string literal | Restored directly to the literal; the anonymous `const` global is not shown |
