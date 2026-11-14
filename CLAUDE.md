# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Goal

This project aims to build a **natural language compiler** — generate SSA directly from natural language via LLM, rather than from a high-level programming language. The compiler then lowers SSA to LLVM IR. This eliminates the programming language middle layer, allowing developers to create quality software through natural language alone.

## Design Principles (for LLM-friendliness)

SSA syntax and every extension must follow these rules, in priority order:

1. **Pattern reuse over new syntax** — Every new instruction must reuse an existing syntactic pattern (e.g. `field @S, *%p, %i` reuses `gep type, *%base, %index`). No ad-hoc syntax. If a pattern doesn't exist, first establish it with at least two use cases.

2. **Strictly fixed grammar per instruction** — Each instruction keyword uniquely determines the complete syntactic structure of that line. LLM should not need to infer context from surrounding lines to predict the next token after the opcode.

3. **Zero implicit rules** — No context-dependent behaviors, no hidden defaults, no "fields may be omitted if..." Nothing that requires the LLM to "know" something beyond what's on the current line.

4. **Single consistent naming convention** — `@` for all global symbols (functions, types, structs, labels, globals), `%` for all local values. Never introduce a new sigil or prefix.

5. **Types always explicit** — No type inference. Every instruction carries its type(s) explicitly. LLMs are better at reproducing what they see than at inferring what's implicit.

These rules serve the primary goal: maximize LLM accuracy in generating and understanding SSA code.

## Architecture

Two-layer system:

```
┌────────────────────────────────────────────────────┐
│  SSA Compiler (ssa/)                                │
│  c1 CLI: .bx → .o via LLVM backend                  │
│  Pipeline: Lexer → Parser → AST → Lowering →       │
│            LLVM IR → Codec → .o → cc/lld → exe    │
│  Cross-platform: --cpu/--os for any target triple   │
│  Link flags: -l <lib>, -L <dir>, --ld <linker>     │
├────────────────────────────────────────────────────┤
│  Platform Library (ssa/platform/)                   │
│  C wrappers for non-ISO-C system APIs               │
│  Platform-specific: #ifdef __APPLE__ / __linux__   │
│  Linked via -l platform                             │
└────────────────────────────────────────────────────┘
                   │ native executable
```

### ISO C vs POSIX vs Platform Layer

| Category | Examples | Where to declare |
|----------|----------|-----------------|
| **ISO C** | `fopen`, `fread`, `malloc`, `strlen`, `putchar`, `memcpy` | Direct `extern` in `boxes/std/` — same on all platforms |
| **POSIX** (non-ISO) | `socket`, `bind`, `listen`, `accept`, `connect` | C wrapper in `platform/`, extern in `boxes/std/net/` — macOS/Linux unified, Windows needs separate path |

### SSA (ssa.md & box.md)

- `@name` — global symbols (functions, block labels, globals)
- `%name` — local SSA values (each defined once)
- Types: `i1/i8/i16/i32/i64/f32/f64`, `*T` (pointer), `[T, N]` (array), `@Name` (struct)
- 39 instructions: arithmetic (incl. unsigned udiv/urem/mulhi), bitops (incl. lshr/ashr), compare, memory (load/store/gep/field/let), control flow, call, type conversions, atomic (load atomic/store atomic/atomicrmw/cmpxchg), `tensor` (f32 elementwise/matmul over static shapes, lowered via MLIR — see below), and `asm` for target inline assembly (att/intel dialects, optional `alignstack`, `${name}` named operands, struct result for multiple outputs; no asm goto). Hardware crypto is not exposed as SSA instructions; the std library calls `ssa/platform/crypto_hw.c` directly
- `const @name: type = value` for read-only globals; `[T, N] = [ val, ... ]` array initializer; string literals `"..."` with `\n`/`\r`/`\t`/`\0`/`\\`/`\"`/`\xNN` escapes (auto-null-terminated); `const @x = "..."` infers `[i8, N]`
- character literals: `'A'` equivalent to `65`, supports same escapes as strings (`'\n'`, `'\x41'` etc.) — lowered to integer constants
- enums: `enum @Color: i32 { @Red = 0, @Green = 1 }` — each member becomes an internal const global named `@Color.Red`, usable anywhere an integer operand is expected; values must be integer literals (no auto-increment), and the enum name is not a type
- `null` keyword as pointer constant (e.g. `eq *i8 %ptr, null`)
- `extern @name(...) -> type` declares libc functions (linked automatically)
- String literals in `call` arguments (e.g. `call @fwrite("Hello\n", 1, 6, %fp)`) are supported — the parser auto-generates an anonymous `const @_sN` global + `gep`. Non-call contexts still require explicit `const` + `gep` (e.g. `const @msg: [i8, 7] = "Hello\n"` + `%p = gep i8, *@msg, 0`).
- Declaration order is free: structs may be used before their declaration (parser pre-scans struct names), and functions may call functions defined later in the file (lowering creates all signatures first). Multiple `.bx` files can be compiled together: `c1 a.bx b.bx -O2`
- Standard library at `ssa/boxes/std/` — functions prefixed with `@<dir>_<file>_<func>` pattern (e.g. `@std_core_math_min`)
- Boxes root: `ssa/boxes/std/<category>/<module>.bx` (e.g. `ssa/boxes/std/text/fmt.bx`, `ssa/boxes/std/net/socket.bx`)
- Full spec: `ssa.md`. Box system: `box.md`. Human-readable pseudo-C view of SSA: `cmap.md`

### Component layers

| Layer | Location | Role |
|---|---|---|
| **Lexer** | `ssa/lex.c/h` | `.bx` → tokens. 30+ keywords, `@`/`%` symbols, int/float, `//` comments |
| **AST + Parser** | `ssa/ast.c/h` | Tokens → AST. Recursive-descent. Nested pointer/array/struct types. `SourceLoc` debug info |
| **Lowering** | `ssa/lowering_ir.c/h` | AST → LLVM. `lower_to_llvm_module` (direct) and `lower_to_llvm_ir` (text). 3-pass: phis → non-terminators → terminators. Struct type map for named structs |
| **Codec** | `ssa/codec.c/h` | LLVM → `.o`. Three paths: from IR text, from module, or from AST (skips IR serialization) |
| **c1 CLI** | `ssa/c1/main.c` | `.bx` → executable or `.a` static lib. Flags: `--cpu`, `--os`, `--mattr`, `-O`, `-o`, `-c`, `--static`, `-l`, `-L`, `--ld`, `--link-flag`, `-I`, `--cmap` |
| **Tensor backend** | `ssa/hir/tensor.cpp` | `tensor` instructions become calls to generated kernels. `lowering_ir.c` collects them and calls `c1_emit_tensor_kernels`, a C entry in this C++ file that builds linalg kernels, lowers them to LLVM IR and hands back an `LLVMModuleRef` in the caller's context. MLIR is linked into c1 (in-process), so there is no helper binary |
| **Std lib** | `ssa/boxes/std/` | core/math, core/convert, core/random, mem/memops, mem/alloc, io/print, io/file (open/read/write/append, read_all/write_all/read_line, seek/tell), **io/stdio** (stdin/stdout/stderr), **io/path** (basename/dirname/extension/join), **io/fs** (exists/stat/mkdir/remove/rename/dir iteration), **process/process** (getenv/exit/system), text/str (incl. starts_with/trim/split_next/replace), text/fmt, **text/hex**, **text/utf8**, data/stack, data/bitset, **data/vector** (growable elements), **data/hashmap** (byte keys/values), **data/hashset**, **json/json** (parse, tree builders, serializer, free), **ui/widget** (widget tree, column/row layout, rect/text/button nodes, pre-order paint, hit test, button press state), **time/time** (wall/monotonic ms, ISO 8601 format/parse), **thread/thread** (spawn/join/detach), **thread/sync** (mutex incl. trylock, condvar incl. timedwait, yield/sleep/self/cpu_count), crypto/crc32, crypto/fnv1a, crypto/md5, **crypto/sha256**, **crypto/aes**, **crypto/rsa**, **crypto/rsa_pss**, **crypto/base64**, **crypto/pem** (read/write), **crypto/oaep**, **crypto/util** (constant-time compare, secure zero) |
| **Net lib** | `ssa/boxes/std/net/` | `socket.bx`, `url.bx`, `ioselect.bx`, `http/request.bx` + `http/response.bx` (HTTP/1.0/1.1 request building/parsing, chunked decoding), `http/websocket/` (RFC 6455 client: handshake, frame codec, masked connection), `tls/` (TLS 1.2 client and server, TLS 1.3 client; HTTP falls back to 1.2; optional trust-store chain validation) |
| **Platform** | `ssa/platform/` | C wrappers for non-ISO-C system APIs. `socket.c`, `ioselect.c`, `cpu.c` (CPU crypto feature detection), `crypto_hw.c` (hardware AES-CBC/GCM with 4-way PMULL/PCLMULQDQ GHASH, and SHA-256 compression, built with -O3), `thread.c`/`sync.c` (POSIX pthreads: thread create/join/detach, mutex, condvar, scheduling helpers), `time.c` (wall-clock and monotonic milliseconds via POSIX `clock_gettime`), `fs.c` (stat, mkdir, remove, rename, directory iteration), `stdio.c` (stdin/stdout/stderr FILE* handles), `secure.c` (zeroing that resists dead-store elimination), `ui.c` (raylib window, frame loop and flat draw calls; built only when `thirdparty/raylib` has been unpacked). Compiled to `libplatform.a` with `-pthread` |
| **Test programs** | `ssa/c1/test/` | `run_all.sh` drives four groups (unit/fast/crypto/net/online). `isa/` covers instruction semantics; crypto/TLS/HTTP tests: hello.bx, file_test.bx, extern_demo.bx, hashmap.bx, sort.bx, atomic_test.bx, json_test.bx, base64_test.bx, crypto_util_test.bx, pem_read_test.bx, rsa_pss_sign_test.bx, oaep_test.bx, hex_test.bx, text_test.bx, utf8_test.bx, io_test.bx, stdio_test.bx, process_test.bx, argv_test.bx, path_test.bx, fs_test.bx, vector_test.bx, hashmap_test.bx, hashset_test.bx, time_test.bx, thread_test.bx, std_test.bx, url_test.bx, http_test.bx, ws_frame_test.bx, ws_echo_test.bx, https_example_test.bx, tls_client_test.bx, tls_local_test.bx, rsa_test.bx, rsa_enc_test.bx, rsa_rt_test.bx, rsa_pss_test.bx, x509_algs_test.bx, tls13_kdf_test.bx, tls13_client_test.bx, tls13_record_test.bx, tls13_record_vector_test.bx, tls_server_test.bx, tls_trust_test.bx, x509_chain_test.bx, https13_test.bx, genkey_test.bx, genkey_2048.bx, test_sha256.bx, test_hmac.bx, test_hkdf.bx, test_gcm.bx, str_call_test.bx, tensor_test.bx (add/mul/matmul bit-exact checks), ui_demo.bx (window + PNG screenshot), ui_layout_test.bx (layout/hit test with stubbed drawing) |

### LLVM

At `llvm/`, two static-lib trees: `llvm/build/lib/*.a` (plain LLVM) and `llvm/build-mlir/lib/*.a` (LLVM + MLIR). LLVM 23 C API, opaque pointers. `c1` links only against `build-mlir`, since the two trees differ in assertion settings and are not link-compatible; its LLVM archives are `-force_load`ed (ELF: `--whole-archive`) while MLIR archives are loaded lazily.

Makefiles auto-detect the project root via `$(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)`. LLVM path is relative: `$(ROOT)/llvm`. Building currently requires macOS with Xcode CLT (for `xcrun --show-sdk-version`); on Linux you'd need to set a `MACOS_DEPLOYMENT_TARGET` default or adjust the Makefile.

Object files go to `build/<component>/` at the project root (`build/c1`, `build/test`, `build/codec_test`, `build/platform`). Binaries (`c1`, `test_ssa`, `test_codec`, `libplatform.a`) stay in their source directories; `build/` is gitignored and removed by `./build.sh --clean`.

### MLIR (tensor backend)

`tensor` lowering needs MLIR, which is built separately in `llvm/build-mlir` (an LLVM build with `-DLLVM_ENABLE_PROJECTS="lld;mlir"`). `./build.sh --rebuild-llvm` sets this tree up; for a one-off build:

```bash
cmake -S llvm/llvm -B llvm/build-mlir -G Ninja \
  -DLLVM_ENABLE_PROJECTS="lld;mlir" -DLLVM_TARGETS_TO_BUILD="AArch64;X86" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
ninja -C llvm/build-mlir
```

`ssa/hir/tensor.cpp` is compiled as C++ and linked into `c1` together with the MLIR archives from `llvm/build-mlir`; MLIR is required to build `c1` at all. It also needs zstd (`brew install zstd` on macOS, `libzstd-dev` on Debian), because this LLVM tree is built with zstd support.

Design notes: static memrefs (`memref<4x4xf32>`) cross the ABI as a plain pointer (`convert-func-to-llvm` with `useBarePtrCallConv`), so the generated kernel signature is `void <symbol>(ptr dst, ptr lhs, ptr rhs)` and the call site needs no descriptor. `matmul` zero-fills `dst` with `linalg.fill` before `linalg.matmul`; elementwise ops use `linalg.generic` with identity maps. A `matmul` declares `[M, K, N]` but its three buffers are 2-D (`M×K`, `K×N`, `M×N`). The pipeline is `linalg-to-loops` → `scf-to-cf` → `convert-to-llvm` → `reconcile-unrealized-casts`, then `mlirTranslateModuleToLLVMIR`; the result links straight into the module being lowered.

A custom MLIR dialect that covers every SSA instruction lives in `ssa/hir/` (`c1_dialect.*`, `build.cpp`, `lower.cpp`). `c1 --backend=mlir` selects it; the default stays the direct LLVM lowering. Only the float/pointer conversion group (`fptosi`, `sitofp`, `ptrtoint`, `bitcast`, ...) is still missing there. The dialect, its ops and the lowering are described in `hir.md`.

### Third-party (thirdparty/)

`thirdparty/packages.conf` lists each dependency as `name|version|url|sha256|build`. `tools/setup-thirdparty.sh [name...]` downloads the archives (verifying SHA-256), extracts them to `thirdparty/<name>/` and runs the build command. Only `packages.conf` is tracked; archives (~40 MB for raylib 5.5) and extracted trees are gitignored. `CODEONE_MIRROR` prepends a mirror prefix to GitHub URLs, `CODEONE_PACKAGES` overrides the manifest path. raylib produces `thirdparty/raylib/build/raylib/libraylib.a`. raylib supplies the window, input and 2D drawing for the UI layer; `ssa/platform/ui.c` wraps it in flat integer-pixel calls and `ssa/boxes/std/ui/widget.bx` implements the widget tree, column/row layout, painting and hit testing in SSA. Link apps with `-L thirdparty/raylib/build/raylib -l raylib` plus the platform frameworks (macOS: `--link-flag "-framework Cocoa"`, IOKit, OpenGL, CoreVideo).

### Version

Version is in `VERSION` at project root (`0.0.1`). Built into `c1 --version` via `-DCODEONE_VERSION` flag in the Makefile.

## SSA Coding Rules

Detailed SSA code style and pattern rules are in [`agents.md`](agents.md). Key rules for `.bx` files:

- `@` for globals, `%` for locals — no exceptions
- `phi` is supported and preferred over `let/store/load` for simple value merging
- `let` does NOT zero-initialize stack memory — always use `store` or `@memset`
- No `phi` for cross-iteration state in loops: use `let` + `store`/`load` instead
- String literals in `call` args allowed (auto `const` + `gep`), but `putchar`+ASCII for string output is not recommended

## Performance Notes

- **`-O2` is critical** for cryptographic workloads. At `-O0` (~500k lines SSA), even a single 2048-bit `mod_pow` runs >10s. At `-O2` (~7M instructions retired), it completes in ~0.35s.
- The schoolbook multiplication in `bn_mul` is O(n²) — 64×64 = 4096 iterations for 2048-bit. `std_crypto_rsa_bn_mod_pow_mont` (Montgomery, used by `rsa_encrypt`/`rsa_decrypt`) avoids the per-multiply division: a 2048-bit mod_pow drops from ~5.2s to ~37ms. Karatsuba would still help key generation.
- `x509` verification still calls the plain `bn_mod_pow`; switching it to Montgomery broke the self-signature test and needs investigation.

## TLS 1.3

`net/tls/client13.bx` implements a TLS 1.3 client with
`TLS_AES_128_GCM_SHA256`, `secp256r1`, and `rsa_pss_rsae_sha256` /
`ecdsa_secp256r1_sha256`. `net_http_client_open` tries 1.3 first and falls
back to 1.2 on a fresh connection.

CertificateVerify and the SAN dNSName are always checked. Without a trust
store the client keeps the permissive self-signed probe; with one configured
through `net_tls_set_trust(ctx, pool, offsets, lens, count)` it validates the
full chain (leaf → intermediates from the Certificate message → trust anchors)
with `x509_verify_chain`, including validity periods. Stack contexts are not
zero-initialized, so callers must call `net_tls_init` (or `net_tls_set_trust`)
before connecting. `net_http_client_open_trusted` takes the same trust parameters as
`net_tls_set_trust`; `net_http_client_open` keeps the permissive default.
SHA-384 signature variants and ChaCha20-Poly1305 / X25519 / Ed25519 are not
supported.

## TLS 1.2 client

`net/tls/client12.bx` runs the TLS 1.2 handshake side of the shared stack.
It honours the same trust store: when `count > 0`, `_t12_check_chain` matches
the hostname and validates the leaf plus the intermediate certificates from
the Certificate list. With no trust store it keeps the previous unverified
behaviour.

## TLS 1.2 server

`net/tls/server_api.bx` exposes
`@net_tls_server_accept(ctx, fd, cert, cert_len, key, key_len, limbs)`.
It runs a TLS 1.2 handshake with `TLS_RSA_WITH_AES_128_GCM_SHA256`, echoes
`extended_master_secret` and secure renegotiation when offered, and accepts
PEM or DER input (the key may be PKCS#1 or PKCS#8 RSA). The shared TLS 1.2
message builders/parsers live in `handshake.bx`; `net_tls_send/recv` choose
the key, IV, and sequence from the context role, so both directions share
them. No client certificates, session resumption, or renegotiation.

## Testing

Custom framework (`test.h`): `TEST(name)`, `ASSERT`/`ASSERT_EQ`/`ASSERT_STR_EQ`, `RUN_TEST(name)`.

218 C unit tests + `.bx` integration tests:
- `test/test_lex.c` (93 tests) — keywords, symbols, literals (incl. string/char literals, character escape sequences), comments, errors
- `test/test_ast.c` (78 tests) — each instruction, types, extern, struct, enum, string init, null, complete programs
- `test/test_lowering_ir.c` (43 tests) — lowered instructions, string globals, null, error handling
- `test/codec_test/test_codec.c` (12 tests) — `.o` generation, Mach-O magic, errors
- `c1/test/*.bx` — `.bx` integration tests (hello, file I/O, extern, hashmap, sort, atomic, threads, std, url, RSA, X.509, TLS 1.2/1.3 clients, TLS 1.2 server, HTTP, WebSocket)
- `platform/test/*.bx` — socket echo server/client, ioselect tests

## Commands

```bash
# Tests
cd ssa/test && make test
cd ssa/test/codec_test && make test

# Fetch, unpack and build the third-party packages (raylib for the UI layer)
./tools/setup-thirdparty.sh
# Mirror a slow GitHub connection
CODEONE_MIRROR=https://ghfast.top/ ./tools/setup-thirdparty.sh

# Build toolchain (for current platform)
./build.sh

# Build toolchain for specific target
./build.sh --cpu x86_64 --os Linux

# Clean all build artifacts
./build.sh --clean

# Build toolchain with LLVM lld + cross-target support
./build.sh --rebuild-llvm

# Rebuild c1 compiler only (faster than full build.sh)
cd ssa/c1 && make

# Rebuild platform library only
cd ssa/platform && make

# Build + run C unit tests
cd ssa/test && make test

# Build + run codec tests
cd ssa/test/codec_test && make test

# Install toolchain to ~/.codeone/toolchain (adds to PATH) and the codeone
# skill for local Claude Code / Codex CLI / Gemini CLI installations
./install.sh

# Compile .bx with platform library (socket etc.)
c1 foo.bx boxes/std/net/socket.bx -L ssa/platform -l platform

# Build and run the UI demo (macOS frameworks; writes ui_demo.png)
cd ssa/c1 && ./c1 test/ui_demo.bx ../boxes/std/ui/widget.bx -o test/ui_demo \
  -L ../platform -l platform -L ../../thirdparty/raylib/build/raylib -l raylib \
  --link-flag "-framework Cocoa" --link-flag "-framework IOKit" \
  --link-flag "-framework OpenGL" --link-flag "-framework CoreVideo"
./test/ui_demo

# Compile with include search path for .bx files
c1 -I ~/.codeone/toolchain/boxes foo.bx

# Compile to object file only (no link)
c1 -c foo.bx

# Build static library (.a)
c1 --static foo.bx
c1 --static foo.bx boxes/std/net/socket.bx -L ssa/platform -l platform

# Compile with custom linker
c1 foo.bx --ld lld
c1 foo.bx --ld gcc

# Emit a human-readable pseudo-C view (no compilation, one .cmap per input)
c1 --cmap foo.bx bar.bx

# Compile a WebSocket client over ws://. http/client.bx references net_tls_*,
# so the TLS stack is required even for plain ws:// (and for wss://).
c1 ws.bx boxes/std/net/http/websocket/*.bx boxes/std/net/http/*.bx \
  boxes/std/net/socket.bx boxes/std/text/str.bx boxes/std/core/random.bx \
  boxes/std/crypto/sha.bx boxes/std/crypto/base64.bx \
  boxes/std/net/tls/client_api.bx boxes/std/net/tls/client12.bx \
  boxes/std/net/tls/base.bx boxes/std/net/tls/record.bx \
  boxes/std/crypto/x509.bx boxes/std/crypto/asn1.bx boxes/std/crypto/hmac.bx \
  boxes/std/crypto/md5.bx boxes/std/crypto/tls_prf.bx boxes/std/crypto/rsa.bx \
  boxes/std/crypto/rsa_padding.bx boxes/std/crypto/aes.bx boxes/std/crypto/hkdf.bx \
  boxes/std/crypto/p256.bx -L ssa/platform -l platform

# Run the SSA test suite (unit + fast + crypto; net needs a local server,
# online needs internet access)
cd ssa/c1/test && ./run_all.sh          # or: fast | crypto | net | online | all

# Run a .bx integration test
cd ssa/c1 && ./c1 test/hello.bx -o test/hello && ./test/hello

# Run a platform integration test (socket echo server/client)
cd ssa/platform/test && ../c1 echo_server.bx -o echo_server && ./echo_server &
cd ssa/platform/test && ../c1 echo_client.bx -o echo_client -L .. -l platform && ./echo_client

# Use the built toolchain
toolchain/aarch64-apple-darwin/bin/c1 foo.bx

# Compile + run RSA encryption test (fixed small key)
cd ssa/c1 && ./c1 test/rsa_rt_test.bx boxes/std/crypto/rsa.bx -o test/rsa_rt_test -L platform -l platform -O2 && ./test/rsa_rt_test

# Generate 2048-bit RSA PEM key files (embedded key params)
cd ssa/c1 && ./c1 test/genkey_2048.bx boxes/std/crypto/rsa.bx boxes/std/crypto/base64.bx boxes/std/crypto/pem.bx -o test/genkey_2048 -L platform -l platform -O2 && ./test/genkey_2048
# Output: rsa.pub + rsa.key in current directory

# Generate PEM with known small key (no random prime required)
cd ssa/c1 && ./c1 test/genkey_test.bx boxes/std/crypto/rsa.bx boxes/std/crypto/base64.bx boxes/std/crypto/pem.bx -o test/genkey_test -L platform -l platform -O2 && ./test/genkey_test
# Output: rsa.pub + rsa.key (OpenSSL-compatible PKCS#1 DER)

# Compile with debug IR output (verify LLVM IR)
cd ssa/c1 && ./c1 -c foo.bx && llvm-dis foo.o  # if llvm-dis available

# LLVM incremental build
cd llvm/build && ninja -j10 LLVMAArch64CodeGen LLVMAArch64Desc LLVMAArch64Info LLVMAArch64Utils
```
