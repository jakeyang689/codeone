# The HIR — the `c1` MLIR layer

The HIR (high-level IR) is a hand-written MLIR dialect that represents every
SSA instruction one-to-one. `c1 --backend=mlir` builds it from the AST, lowers
it to the LLVM dialect and translates that to LLVM IR; the default backend
still goes straight from the AST to LLVM IR and remains the reference.

The dialect exists to keep the language's LLM-friendliness contract in a
second, testable intermediate: one op per instruction, fixed operand layout,
types always explicit, no hidden defaults. MLIR is linked into `c1` in-process
— there is no helper binary and no MLIR text round-trip in the pipeline.

## Pipeline

```
.bx ──lexer/parser──> AST ──┬─ lowering_ir.c ──────────────────> LLVM IR   (default)
                            │
                            └─ buildModule ─> c1 dialect ─> lowerToLLVM ─> LLVM IR
                                                            (--backend=mlir)
                                                  │
                          c1_emit_tensor_kernels (linalg) ──link──┘
```

`--emit-mlir` prints the c1 dialect module to `<input>.mlir` and stops.

## Layout

| File | Role |
|---|---|
| `ssa/hir/c1_dialect.h` / `.cpp` | Dialect registration, types, all ops, printers, verifiers. There is no textual parser: ops are only ever built programmatically. |
| `ssa/hir/build.cpp` | AST → `c1` dialect module (`buildModule`). |
| `ssa/hir/lower.cpp` | `c1` dialect → LLVM dialect → `llvm::Module` (`lowerToLLVM`), plus the tensor kernel collection and debug-info attachment. |
| `ssa/hir/backend.cpp` / `.h` | The C API the CLI calls: `c1_hir_build_llvm_module` and `c1_hir_emit_text`. |
| `ssa/hir/tensor.cpp` / `.h` | The linalg kernel emitter shared with the default backend (`c1_emit_tensor_kernels`). |

## Types

| SSA type | HIR type | LLVM lowering |
|---|---|---|
| `i1`…`i64` | builtin `iN` | `iN` |
| `f32`, `f64` | builtin `f32`, `f64` | same |
| `*T` | `!c1.ptr<T>` | `!llvm.ptr` (opaque) |
| `[T, N]` | `!c1.array<T, N>` | `!llvm.array<N x T>` |
| `@Name` | `!c1.struct<@Name>` | `!llvm.struct<"Name", (...)>` |

Pointers carry their pointee type so `load`/`store`/`gep`/`field` can be
checked, but they still lower to opaque pointers, matching the rest of the
compiler. Named structs are identified by symbol, not by body, so recursive
and mutually recursive structs work; bodies live in `c1.struct_def` at module
scope. The enum name is not a type — each member is an internal `c1.global`
constant named `@Enum.Member`.

## Operations

All ops are in dialect `c1` and take explicit operand types. Printed form is
shown for reference; it is what `--emit-mlir` produces.

### Module scope

| Op | Example |
|---|---|
| `c1.struct_def` | `c1.struct_def @S { x: i32, y: !c1.ptr<!c1.struct<@S>> }` |
| `c1.global` | `c1.global @g : i32 internal = 7` (flags: `constant`, `internal`) |
| `c1.func` | `c1.func @f(i32, !c1.ptr<i8>) -> i64 { … }` |
| `c1.extern` | `c1.extern @printf(!c1.ptr<i8>, ...) -> i32` |

### Values and memory

| Op | Example | Notes |
|---|---|---|
| `c1.const` | `c1.const 42 : i32` | carries an MLIR `Attribute` (integer or float literal) |
| `c1.null` | `c1.null : !c1.ptr<i8>` | pointer constant |
| `c1.address_of` | `c1.address_of @g : !c1.ptr<i32>` | address of a global |
| `c1.alloca` | `c1.alloca : !c1.ptr<i32>, align 4` | `let`; **not** zero-initialized |
| `c1.load` | `c1.load %p : i32` | |
| `c1.store` | `c1.store %v, %p : i32` | |
| `c1.gep` | `c1.gep %base, %i : i32, !c1.ptr<i32>` | element type and result type explicit |
| `c1.field` | `c1.field @S, %p, 1 : !c1.ptr<i64>` | struct name and index are attributes |

### Arithmetic, bitwise, comparison, casts

`c1.add` `c1.sub` `c1.mul` `c1.sdiv` `c1.udiv` `c1.srem` `c1.urem` `c1.mulhi`
`c1.and` `c1.or` `c1.xor` `c1.shl` `c1.lshr` `c1.ashr`, all printed as
`c1.add %lhs, %rhs : T`. The opcode alone fixes the semantics; the lowering
picks `llvm.f*` instead of `llvm.*` when `T` is `f32`/`f64`
(add/sub/mul/div/rem).

`c1.icmp` carries a predicate attribute:
`c1.icmp slt %a, %b : i32` (the printed type is the operand type; the result
is `i1`), with `eq ne slt sle sgt sge ult ule ugt uge`.
SSA has no float comparison instruction, so there is no `c1.fcmp`.

`c1.zext` / `c1.sext` / `c1.trunc` are printed `c1.zext %v : i8 to i64`.

`c1.call` prints the callee and the full function type:
`c1.call @f(%a, %b) : (i32, i32) -> i32`.

### Atomics and inline assembly

| Op | Example |
|---|---|
| `c1.atomic_load` | `c1.atomic_load %p, seq_cst : i32` |
| `c1.atomic_store` | `c1.atomic_store %v, %p, release : i32` |
| `c1.atomicrmw` | `c1.atomicrmw add, %p, %v, seq_cst : i32` |
| `c1.cmpxchg` | `c1.cmpxchg %p, %expected, %new, acquire, seq_cst : i32` |

Orderings: `monotonic acquire release acq_rel seq_cst`. RMW operations:
`xchg add sub and or xor`. `c1.cmpxchg` produces `i1` — the success flag.

`c1.inline_asm`:

```
%r = c1.inline_asm att : i64, "add $0, $1, $2", "=r,r,r", i64 %a, i64 %b
     c1.inline_asm att alignstack : "dmb ish", "~{memory}"
```

Attributes: `dialect` (`att`/`intel`), `text`, `constraints`, optional
`align_stack`. Zero or one result type. A struct result means several outputs,
one per `=` output constraint, in order.

### Tensor

```
c1.tensor matmul, f32, [2, 2, 2], "__c1_tensor_main_2", %dst, %lhs, %rhs
```

Attributes: `op` (`add sub mul div matmul`), `elem_type` (f32 only),
`dims` (positive integer constants), `kernel` (the generated symbol). Operands
are destination, lhs, rhs. `matmul` declares `[M, K, N]` but its buffers are
2-D. There is no result: the destination is explicit.

### Terminators

| Op | Example |
|---|---|
| `c1.return` | `c1.return %v : i32` |
| `c1.br` | `c1.br ^bb1(%a, %b)` |
| `c1.cond_br` | `c1.cond_br %c, ^bb1(%a), ^bb2(%b, %c)` |

## Building the HIR from the AST

`buildModule` emits, in order: `c1.struct_def` for every named struct,
`c1.global` for every global/const/enum member, `c1.extern` for every external
declaration, then every `c1.func`.

- **Structs.** Declarations come first (name only) so fields may refer to
  structs defined later; the body is filled afterwards by the lowering.
- **Externs.** MLIR symbols are unique per module, so a declaration for a
  function defined in another input file — or a repeated declaration of the
  same name — does not produce a second `c1.extern`.
- **Phi.** A `phi` becomes an argument on its block, in source order. Every
  terminator forwards exactly one value per destination argument. `phi` in the
  entry block is rejected. Non-terminators are emitted before terminators so a
  branch can forward any value regardless of block order.
- **Strings.** The parser has already turned string literals into anonymous
  `const @_sN` globals plus `gep`, so the HIR only sees ordinary globals and
  symbol operands.
- **Calls.** Arguments are resolved against the callee's `FunctionType`;
  extra arguments of a variadic callee default to `i32`.
- **Inline asm.** The number of `=` output constraints is checked against the
  result type; a struct result must have exactly that many fields.
- **Tensor.** Each instruction gets a symbol
  `__c1_tensor_<func>_<n>` with a per-program counter.
- **Locations.** Every op takes a `FileLineColLoc` built from its `SourceLoc`,
  and functions take one from `FuncDecl::loc`. That is what makes DWARF
  emission work later; see below.

Values are looked up by name in a `StringMap`, so the builder fails with a
message rather than producing malformed IR when an operand is unknown or has
the wrong type.

## Lowering to LLVM

`lowerToLLVM` is a manual walker, not a conversion-pattern pass: it creates
each `llvm.func`, maps every source block to a destination block, binds each
`c1` value to its `llvm` replacement and rebuilds the body in place. The
mapping is mechanical:

| c1 | LLVM dialect |
|---|---|
| `c1.func` / `c1.extern` | `llvm.func` |
| `c1.const` / `c1.null` / `c1.address_of` | `llvm.mlir.constant` / `llvm.mlir.zero` / `llvm.mlir.addressof` |
| `c1.alloca` / `c1.load` / `c1.store` | `llvm.alloca` / `llvm.load` / `llvm.store` |
| `c1.gep` / `c1.field` | `llvm.getelementptr` |
| arithmetic / bitwise | `llvm.add`, `llvm.fadd`, … chosen by operand type |
| `c1.icmp` / `c1.zext` / `c1.sext` / `c1.trunc` | same-named llvm ops |
| `c1.call` | `llvm.call` |
| `c1.br` / `c1.cond_br` / `c1.return` | `llvm.br` / `llvm.cond_br` / `llvm.return` |
| `c1.atomic_load` / `c1.atomic_store` | `llvm.load` / `llvm.store` with ordering and alignment |
| `c1.atomicrmw` / `c1.cmpxchg` | `llvm.atomicrmw` / `llvm.cmpxchg` (+ `llvm.extractvalue` for the `i1`) |
| `c1.inline_asm` | `llvm.inline_asm` |
| `c1.tensor` | `llvm.call` to a linked linalg kernel |

Details worth knowing:

- **Atomic alignment.** `llvm.load`/`llvm.store` reject ordered accesses
  without an alignment, so the lowering computes the ABI alignment of the
  accessed type and passes it; the same value goes to `llvm.atomicrmw` and
  `llvm.cmpxchg`.
- **Variadic calls.** The lowering collects the variadic `c1.extern` names,
  rebuilds the callee type with `isVarArg` for calls to them, and builds
  `llvm.call` from that type, which sets `var_callee_type`; without it the
  LLVM dialect verifier rejects the call.
- **Inlined assembly.** `has_side_effects` is always set, so `~{memory}` and
  other clobbers survive.
- **Tensor.** Kernels are collected by walking the c1 module for `c1.tensor`
  ops and turned into `C1TensorKernel` records. `declareTensorKernel` emits
  `void <kernel>(ptr, ptr, ptr)` declarations at module scope; after the
  translation to LLVM IR, `c1_emit_tensor_kernels` builds the linalg kernels
  and the result is linked in. Both backends therefore execute the same
  linalg code.
- **Triple.** `setTargetTriple` is applied to the translated `llvm::Module`
  and passed to the kernel emitter.

### Debug information

Every op already has a source location, so no `-g` flag is involved:

1. `attachDebugInfo` finds the first `FileLineColLoc`, derives a `DIFileAttr`
   from the real `.bx` path, and fuses a `DICompileUnitAttr` into the module
   location. Without that the standard pass would use `<unknown>`.
2. MLIR's `DIScopeForLLVMFuncOpPass` gives each `llvm.func` a
   `DISubprogramAttr` (a `DistinctAttr` id, `Definition` flag for defined
   functions, none for declarations).
3. The translation turns every `FileLineColLoc` into a `DILocation` under the
   enclosing subprogram and adds the Debug Info Version module flag.

The result is a `.debug_line` table pointing at the `.bx` file and line
numbers, so a stack trace resolves to SSA source lines. Variable and type
debug info are not emitted.

## Adding an instruction

1. Add the op class in `c1_dialect.h`: traits (`OneResult`, `NOperands<N>`,
   terminators, …), `getOperationName`, `build`, accessors, `print`, optional
   `verify`. Register it in the list at the top of `c1_dialect.cpp`.
2. Emit it from `build.cpp` in the `emitInsn` switch, checking operand types
   before creating the op.
3. Lower it in `lower.cpp`'s `lowerOp`, binding every result.
4. Add an `isa/` test and run it on both backends at `-O0` and `-O2`.

Hand-written ops need `MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID` and a
`getAttributeNames` list (see `OpBoilerplate` in the header). Traits such as
`OneOperand` provide zero-argument `getOperand()`; use
`getOperation()->getOperand(i)` on those ops when the index matters.

## Limits

- The float/pointer conversion group (`fptrunc`, `fpext`, `fptosi`, `sitofp`,
  `uitofp`, `ptrtoint`, `inttoptr`, `bitcast`) is not lowered; the CLI reports
  the instruction kind as unsupported. Nothing in the std library emits them
  yet.
- `tensor` supports `f32` and positive literal shapes only, mirroring the
  reference lowering.
- There is no textual parser for the dialect; `--emit-mlir` output is for
  reading and review, not for feeding back into the compiler.

## Building and testing

MLIR comes from `llvm/build-mlir` (LLVM + MLIR, built with zstd support) and
is required to build `c1` at all:

```bash
cd ssa/c1 && make
```

The dialect path is exercised by pointing the whole test driver at it:

```bash
cd ssa/c1/test
C1='../c1 --backend=mlir' ./run_all.sh fast
C1='../c1 --backend=mlir' ./run_all.sh crypto
C1='../c1 --backend=mlir' ./run_all.sh net
C1='../c1 --backend=mlir' ./run_all.sh online
```

Each group must stay green on both backends; the MLIR backend is compared by
behaviour, not by object bytes.
