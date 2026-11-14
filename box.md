# Box — SSA Standard Library

## What Is a Box

A box is the unit of code reuse in the SSA ecosystem. Each box is a `.bx` file containing a set of related function definitions. Boxes compose via `call @func_name()`.

Design principles for boxes:

> **Naming convention: function names are prefixed by their path.** For example, functions in `std/core/math.bx` are named `@std_core_math_<func>`. This prefix avoids collisions with libc functions (such as `exit`, `printf`) or same-named functions in other boxes. extern declarations keep their original names.
>
> `std/net/` is an exception: it keeps the earlier `@net_<file>_<func>` prefix (e.g. `@net_socket_tcp`) rather than being renamed to `@std_net_*`, to avoid a large-scale rename of existing code.

| Principle | Description |
|------|------|
| **Single responsibility** | Each box does one kind of thing (math, memory, IO...) |
| **No dependencies** | Boxes have no internal dependencies; include only the ones you need |
| **Pure SSA** | Parts that only need libc are declared with `extern` and linked automatically at compile time |
| **Naming convention** | Functions are named with a path prefix `@<dir>_<file>_<func>` (e.g. `@std_core_math_min`) to avoid collisions with libc symbols |

To use a box, just pass multiple `.bx` files to `c1` and they can call each other:

```bash
c1 math.bx print.bx main.bx -O2
```

Before compiling, `c1` concatenates all files into a single source file, so functions in different files can be called directly with `call`. No `import`, `use`, or any extra declaration is needed.

## Existing Standard Library Overview

```
std/
├── core/
│   ├── math.bx           # @std_core_math_min, _max, _abs, _clamp, _is_power_of_two
│   ├── convert.bx        # @std_core_convert_itoa
│   └── random.bx         # @std_core_random_bytes (cryptographically secure)
│
├── mem/
│   ├── memops.bx         # @std_mem_memops_copy, _zero, _set, _cmp
│   └── alloc.bx          # @std_mem_alloc_malloc, _free, _calloc
│
├── io/
│   ├── print.bx          # @std_io_print_ch, _nl, _i32
│   ├── file.bx           # @std_io_file_open_read/_write/_append, _read, _write,
│   │                     #   _read_all, _write_all, _read_line, _seek, _tell, _close
│   ├── stdio.bx          # @std_io_stdio_stdin/stdout/stderr, _write/_write_err,
│   │                     #   _read, _read_line, _flush
│   ├── path.bx           # @std_io_path_basename, _dirname, _extension, _join, _is_absolute
│   └── fs.bx             # @std_io_fs_exists, _is_file, _is_dir, _size, _mtime,
│                         #   _mkdir, _remove, _rename, _opendir/_readdir/_closedir
│
├── process/
│   └── process.bx        # @std_process_getenv, _exit, _system
│
├── text/
│   ├── str.bx            # @std_text_str_len, _eq, _copy, _ncpy, _ends_with,
│   │                     #   _starts_with, _trim, _split_next, _replace,
│   │                     #   _append, _append_cstr, _append_int, _find_ci, _find_crlf, _parse_uint, _lower
│   ├── fmt.bx            # @std_text_fmt_hex
│   ├── hex.bx            # @std_text_hex_encode, _decode
│   └── utf8.bx           # @std_text_utf8_validate, _decode, _encode, _count
│
├── data/
│   ├── stack.bx          # @std_data_stack_init, _push, _pop, _count
│   ├── bitset.bx         # @std_data_bitset_init, _set_bit, _clear_bit, _test
│   ├── vector.bx         # @std_data_vector_init, _reserve, _push, _get, _set,
│   │                     #   _ptr, _data, _count, _capacity, _elem_size, _clear, _free
│   ├── hashmap.bx        # @std_data_hashmap_init, _put, _get, _get_len, _has,
│   │                     #   _remove, _count, _capacity, _slot_key, _slot_value, _free
│   └── hashset.bx        # @std_data_hashset_init, _add, _has, _remove, _count, _free
│
├── json/
│   └── json.bx           # @std_json_parse/_parse_ex, _write, _type/_count/_at/_get,
│                         #   _bool/_int/_float/_str,
│                         #   _new_null/_new_bool/_new_int/_new_float/_new_string,
│                         #   _new_array/_new_object, _add/_set, _free
│
├── ui/
│   └── widget.bx         # @std_ui_box (column/row + pad/gap), _rect, _text, _button, _fill,
│                         #   _checkbox, _slider, _progress, _input,
│                         #   _append, _layout, _paint, _hit,
│                         #   _pointer/_keyboard/_input_type/_input_backspace,
│                         #   _button_update/_button_at  (needs platform/ui.c + raylib)
│
├── time/
│   └── time.bx           # @std_time_now_ms, _monotonic_ms,
│                         #   _format_iso8601, _parse_iso8601
│
├── crypto/
│   ├── crc32.bx          # @std_crypto_crc32_compute
│   ├── fnv1a.bx          # @std_crypto_fnv1a_hash
│   ├── md5.bx            # @std_crypto_md5_compute, _transform
│   ├── sha.bx            # @std_crypto_sha1/256/384/512_compute, _transform
│   ├── rsa.bx            # @std_crypto_rsa_bn big-number arithmetic (_add/_sub/_mul/_mod/_mod_pow/_gcd etc.)
│   ├── ecc.bx            # @std_crypto_ecc_generate_keypair, _sign, _verify, _ecdh
│   ├── base64.bx         # @std_crypto_b64_encode, _b64_decode
│   ├── aes.bx            # @std_crypto_aes_encrypt/decrypt (128/256), _aes128_cbc/256_cbc, _aes_gcm_encrypt/decrypt
│   ├── hmac.bx           # @std_crypto_hmac_sha256, _hmac_md5/_sha1/_sha384/_sha512
│   ├── hkdf.bx           # @std_crypto_hkdf_extract/expand_sha256/_sha384,
│   │                     #   _tls13_* key schedule (expand-label, derive-secret, traffic keys)
│   ├── rsa_padding.bx    # @std_crypto_pkcs1_v15_*, _rsa_encrypt/decrypt_pkcs1_v15
│   ├── rsa_pss.bx        # @std_crypto_rsa_sign/_verify_pss_sha256 (random 32-byte salt)
│   ├── oaep.bx           # @std_crypto_rsa_encrypt/decrypt_oaep_sha256
│   ├── p256.bx           # @std_crypto_p256_generate_keypair, _sign, _verify, _ecdh
│   ├── asn1.bx           # @_asn1_read_tag, _read_length, _find_in_seq, _read_integer
│   ├── pem.bx            # @std_crypto_pem_write_rsa_pub, _write_rsa_priv,
│   │                     #   _read_cert, _read_certs, _read_rsa_private, _find
│   ├── tls_prf.bx        # @std_crypto_tls12_prf_sha256, _master_secret, _key_block, _finished
│   ├── util.bx           # @std_crypto_util_ct_eq, _zero (constant-time compare,
│   │                     #   compiler-proof zeroing via platform/secure.c)
│   └── x509.bx           # @x509_parse_rsa_pubkey, _parse_p256_pubkey, _find_spki,
│                         #   _verify_self_signed (RSA/PSS/ECDSA), _check_hostname (SAN)
│
├── thread/
│   ├── thread.bx         # @std_thread_spawn, _join, _detach
│   └── sync.bx           # @std_thread_mutex_new/lock/trylock/unlock/free,
│                         #   _cond_new/wait/timedwait/signal/broadcast/free,
│                         #   _yield, _sleep_ms, _self, _cpu_count
│
└── net/
    ├── socket.bx          # @net_socket_tcp, _bind, _listen, _accept, _connect, _send, _recv, _close
    ├── url.bx             # @net_url_encode, _decode, _parse (scheme/host/port/path/query)
    ├── ioselect.bx        # @net_ioselect_init, _add, _select, _poll
    ├── http/
    │   ├── request.bx     # @net_http_build_get, _build_post, _build_request, _build_request_conn,
    │   │                  #   _parse_request, _header_add
    │   ├── response.bx    # @net_http_parse_response, _header_find/_get/value, _decode_chunked
    │   ├── client.bx      # @net_http_request, _client_open/send/recv/read_head/close
    │   └── websocket/     # RFC 6455 client
    │       ├── frame.bx   # @net_ws_build_frame, _parse_frame, _mask, _unmask_frame,
    │       │              #   _close_code, _put_close_code
    │       ├── handshake.bx # @net_ws_accept, _make_key, _build_handshake, _check_handshake,
    │       │                #   _build_accept_response
    │       └── conn.bx    # @net_ws_open/send/send_text/send_binary/send_close/
    │                      #   recv/close_code_of/is_closed/destroy
    └── tls/
        ├── record.bx      # @tls_write_record/read_plain, @tls12/13_record_encrypt/decrypt
        ├── base.bx        # struct and codec helpers, @derive12, @send_hs/read_hs,
        │                  #   @net_tls_send/recv/close (select keys by role), @ch12
        ├── handshake.bx   # @hs12_parse_ch/_build_sh/_build_cert,
        │                  #   @hs12_finished_verify, @hs12_master_secret_ems
        ├── client12.bx    # @tls12_hs (full handshake), @tls12_step_* (per-step functions)
        ├── client13.bx    # @net_tls13_connect (RFC 8446, AES-128-GCM + P-256)
        ├── client_api.bx  # @net_tls12_connect, @net_tls_init, @net_tls_set_host,
        │                  #   @net_tls_set_trust
        ├── server12.bx    # @tls12_server_hs (TLS 1.2 RSA key exchange server)
        └── server_api.bx  # @net_tls_server_accept (PEM/DER certificate + RSA private key)
```


## Platform Library

The C code in the `platform/` layer provides platform wrappers for non-ISO-C system APIs:

| File | Functions | Platform |
|------|------|------|
| `socket.c` | `c1_socket_create`, `c1_socket_bind`, `c1_socket_accept`, `c1_socket_connect`, `c1_socket_send` etc. | macOS: BSD sockets; Linux: POSIX |
| `ioselect.c` | `c1_ioselect_init`, `c1_ioselect_add`, `c1_ioselect_poll` | macOS: kqueue; Linux: epoll |
| `random.c` | `c1_random_buf(buf, len)` | macOS: `arc4random_buf`; Linux: `getrandom` |
| `cpu.c` | `c1_cpu_features()` — AES/SHA256/PMULL/SHA512 bitmask; `C1_NO_HW=1` forces zero | macOS: `sysctl`; Linux: `getauxval`; x86: `cpuid` |
| `crypto_hw.c` | `c1_aes*_encrypt/decrypt_block`, `c1_aes*_cbc_*`, `c1_aes*_gcm_*`, `c1_sha256_transform` | ARMv8 Crypto / AES-NI, 4-way PMULL/PCLMULQDQ GHASH; built with -O3 |
| `thread.c` | `c1_thread_create`, `c1_thread_join`, `c1_thread_detach` | macOS / Linux: POSIX `pthread` |
| `sync.c` | `c1_mutex_*`, `c1_cond_*`, `c1_thread_yield/sleep_ms/self`, `c1_cpu_count` | macOS / Linux: POSIX `pthread` |
| `ui.c` | `c1_ui_init/close/begin_frame/end_frame/clear`, `c1_ui_rect/_rect_rounded/_text/_text_width`, `c1_ui_load_font`, `c1_ui_mouse_*`, `c1_ui_screenshot` | macOS / Linux: raylib (third-party source package; `tools/setup-thirdparty.sh` unpacks and builds it per `thirdparty/packages.conf`) |

## Common Patterns

### Generic Data Structures (using stack as an example)

SSA has no generics, but `*i8` + `elem_size` can implement **type-agnostic** data structures:

```ssa
// Initialize a stack that can hold 16 i32s: 256 bytes, elem_size = 4
%stack = let [i8, 256]
call @std_data_stack_init(%stack, 64, 4)

// push: the caller prepares data of elem_size bytes
store i32 123, *%tmp_4bytes
call @std_data_stack_push(%stack, %tmp_4bytes)

// pop: the caller provides the destination buffer
call @std_data_stack_pop(%stack, %out_4bytes)
%val = load i32, *%out_4bytes
```

### Calling libc

Just declare with `extern`; no manual linking is needed:

```ssa
extern @puts(s: *i8) -> i32
extern @malloc(n: i64) -> *i8

fun @main() -> i32 {
  @e:
    call @puts("hello")
    %p = call @malloc(100)
    ret i32 0
}
```

At compile time `c1` automatically invokes the system `cc` to perform linking.

### Writing an Application by Composing Boxes

```bash
# Compile multiple .bx files together directly; functions are automatically visible
c1 std/core/math.bx my_print.bx main.bx -O2
```

Functions from `math.bx` can be called directly in `main.bx`:

```ssa
fun @main() -> i32 {
  @e:
    %m = call @std_core_math_min(3, 7)
    ret i32 0
}
```

If the std box you want to reuse is not in the current directory, just use an absolute or relative path:

```bash
c1 ../std/core/math.bx main.bx -o my_prog
```

### Multithreading

The language has no function pointers, so all threads share a single application-defined entry function `@thread_run`; `task_id` distinguishes different thread bodies, and `arg` is the same pointer passed to `std_thread_spawn`:

```ssa
extern @std_thread_spawn(task_id: i32, arg: *i8) -> *i8
extern @std_thread_join(t: *i8) -> i32

struct @Shared {
  @mutex: *i8,
  @value: i32
}

fun @thread_run(task_id: i32, arg: *i8) -> i32 {
  @entry:
    %mp = field @Shared, *%arg, 0
    %m = load *i8, *%mp
    call @std_thread_mutex_lock(%m)
    %vp = field @Shared, *%arg, 1
    %v = load i32, *%vp
    %v1 = add i32 %v, 1
    store i32 %v1, *%vp
    call @std_thread_mutex_unlock(%m)
    ret i32 0
}

fun @main() -> i32 {
  @entry:
    %sh = let @Shared
    %mp = field @Shared, *%sh, 0
    %m = call @std_thread_mutex_new()
    store *i8 %m, *%mp
    %vp = field @Shared, *%sh, 1
    store i32 0, *%vp

    %t = call @std_thread_spawn(1, %sh)
    %r = call @std_thread_join(%t)
    ret i32 0
}
```

When compiling, link the thread box and the platform library together (on Linux, older glibc also needs `-l pthread`):

```bash
c1 app.bx boxes/std/thread/thread.bx boxes/std/thread/sync.bx \
  -L ssa/platform -l platform -l pthread
```

`@std_thread_mutex_trylock(m)` does not block: it returns 0 if the lock was acquired, 1 if it is held by another thread.
`@std_thread_cond_timedwait(c, m, ms)` waits at most `ms` milliseconds: it returns 0 if woken, 1 on timeout; in either case the mutex is held again on return.

### TLS 1.2 Server

`net/tls/server_api.bx` provides a minimal server entry point. It completes a TLS 1.2 handshake (`TLS_RSA_WITH_AES_128_GCM_SHA256`) on an already-`accept`ed connection, then sends and receives application data with the unified `net_tls_send/recv/close`:

```ssa
extern @net_tls_server_accept(ctx: *i8, fd: i32, cert: *i8, cert_len: i32,
                              key: *i8, key_len: i32, limbs: i32) -> i32
extern @net_tls_recv(ctx: *i8, buf: *i8, cap: i32, out_len: *i32) -> i32
extern @net_tls_send(ctx: *i8, data: *i8, len: i32) -> i32

fun @main() -> i32 {
  @entry:
    %ctx = let [i8, 1024]
    // ... set up the listener, accept to get %cfd, read the certificate/private key ...
    %r = call @net_tls_server_accept(%ctx, %cfd, %certp, %cert_len,
                                     %keyp, %key_len, 64)
    // From now on %ctx has the server role; send/recv automatically pick the server keys
    ret i32 %r
}
```

- `cert` / `key` may be PEM or DER; for PEM the first block in the file is taken. The private key supports
  PKCS#1 `RSAPrivateKey` and PKCS#8 `PrivateKeyInfo`, and the RSA bit width must match `limbs`
  (64 for 2048 bits).
- Supports extended master secret and secure renegotiation; does not support client certificates,
  session resumption, or renegotiation.
- `net_tls_send/recv` select the key, IV, and sequence number according to the context role, so client and server
  share the same send/receive functions.

Compiling the server requires the following files (the client stack does not need x509/p256/hkdf):

```bash
c1 server.bx \
  boxes/std/net/tls/server_api.bx boxes/std/net/tls/server12.bx \
  boxes/std/net/tls/handshake.bx boxes/std/net/tls/base.bx \
  boxes/std/net/tls/record.bx boxes/std/net/socket.bx \
  boxes/std/io/file.bx boxes/std/crypto/rsa.bx boxes/std/crypto/sha.bx \
  boxes/std/crypto/hmac.bx boxes/std/crypto/md5.bx \
  boxes/std/crypto/tls_prf.bx boxes/std/crypto/aes.bx \
  -L ssa/platform -l platform -O2
```

### TLS Client Trust Chain

Clients do not validate the certificate chain by default. Once trust anchors are configured, both the TLS 1.3 and TLS 1.2 clients perform:
hostname matching (SAN dNSName / CN) → verifying signatures level by level with the issuer's public key → validity period
checking for every non-root certificate. TLS 1.3 intermediates come from the Certificate message, TLS 1.2 from the
Certificate list; both skip the first (leaf) certificate.

```ssa
extern @net_tls_init(ctx: *i8) -> void
extern @net_tls_set_trust(ctx: *i8, pool: *i8, offsets: *i32, lens: *i32,
                          count: i32) -> void
extern @std_crypto_pem_read_certs(pem: *i8, pem_len: i32, pool: *i8, pool_cap: i32,
                                  offsets: *i32, lens: *i32, max_count: i32) -> i32

fun @main() -> i32 {
  @entry:
    %ctx = let @TLSContext           // stack memory is not automatically zeroed
    call @net_tls_init(%cp)          // the trust configuration must be cleared first
    call @net_tls_set_host(%cp, "example.com", 11)
    // a PEM may contain multiple CERTIFICATE blocks
    %n = call @std_crypto_pem_read_certs(%pemp, %pem_len, %poolp, 16384,
                                         %offsp, %lensp, 8)
    call @net_tls_set_trust(%cp, %poolp, %offsp, %lensp, %n)
    %r = call @net_tls13_connect(%cp, %fd, 64)   // returns non-zero if validation fails
    ret i32 %r
}
```

- HTTP clients enable the same validation with `net_http_client_open_trusted(host, host_len, port, use_tls, c,
  pool, offsets, lens, count)`; `net_http_client_open` keeps the permissive default.
- When `count == 0` the old permissive behavior is kept: TLS 1.3 only probes for a self-signed certificate, and TLS 1.2 does no validation at all.
  Therefore a stack context that never called `net_tls_init` will read random values and must be explicitly zeroed
  (`@memset` or `net_tls_init`).
- Trust anchors themselves are unconditionally trusted: only "the current certificate is issued by this anchor" is checked; the anchor's self-signature and validity period are not verified.
- `x509_verify_chain` returns `0` on success, `-1` when the chain exceeds 8 levels, `-2` when a certificate is expired/not yet valid,
  and `-3` when no issuer can be found.
- Only RSA PKCS#1 v1.5, RSASSA-PSS, and ECDSA P-256 signatures are supported.

### JSON

`std/json/json.bx` provides parsing, tree construction, access, and serialization, depending only on libc (`malloc`/`free`).
The parse result is a tree of `@JsonValue` nodes; the children of arrays/objects are linked by sibling lists; strings and keys are copied
to the heap, so the original input buffer can be freed after parsing.

```ssa
extern @strlen(s: *i8) -> i64
extern @std_json_parse(text: *i8, len: i32) -> *i8
extern @std_json_get(v: *i8, key: *i8) -> *i8
extern @std_json_int(v: *i8) -> i64
extern @std_json_free(v: *i8) -> void

const @DOC = "{\"name\":\"codeone\",\"count\":3}"

fun @main() -> i32 {
  @entry:
    %p = gep i8, *@DOC, 0
    %l64 = call @strlen(%p)
    %len = trunc i64 %l64 to i32
    %root = call @std_json_parse(%p, %len)
    %c = call @std_json_get(%root, "count")
    %n = call @std_json_int(%c)
    call @std_json_free(%root)
    ret i32 0
}
```

Build and output:

```ssa
extern @std_json_new_object() -> *i8
extern @std_json_new_int(i: i64) -> *i8
extern @std_json_set(v: *i8, key: *i8, key_len: i32, child: *i8) -> i32
extern @std_json_write(v: *i8, out: *i8, cap: i32) -> i32

%obj = call @std_json_new_object()
%n = call @std_json_new_int(42)
call @std_json_set(%obj, "n", 1, %n)
%out = let [i8, 128]
%op = gep i8, *%out, 0
%w = call @std_json_write(%obj, %op, 128)
call @std_json_free(%obj)
```

Interface groups:

| Purpose | Functions |
|------|------|
| Parse | `@std_json_parse(text, len)`, `@std_json_parse_ex(text, len, err_pos)` |
| Access | `@std_json_type`, `_count`, `_at`, `_get`, `_bool`, `_int`, `_float`, `_str` |
| Build scalars | `_new_null`, `_new_bool`, `_new_int`, `_new_float`, `_new_string` |
| Build containers | `_new_array`, `_new_object`, `_add`, `_set` |
| Serialize | `@std_json_write(v, out, cap)` |
| Free | `@std_json_free(v)` |

- `@std_json_parse` returns `null` on failure; `@std_json_parse_ex(text, len, err_pos)` also writes
  the byte offset of the error into `err_pos` (0 on success).
- Numbers keep two forms, integer and floating point: those without a decimal point or exponent are stored as `i64` (`strtoll`
  saturates when out of range); otherwise as `f64`.
- `@std_json_write` outputs compact JSON; it returns -1 if the buffer is too small, otherwise the number of bytes written plus a NUL terminator.
- Supports `\uXXXX` and surrogate-pair decoding, and passes UTF-8 through unchanged; serialization escapes quotes, backslashes, and control characters.
- Just add `boxes/std/json/json.bx` at compile time; no platform library is needed.

### Time

`std/time/time.bx` provides millisecond timestamps and ISO 8601 (UTC) formatting/parsing. The clock is provided by the platform layer
`ssa/platform/time.c` via POSIX `clock_gettime`; the platform library must be linked at compile time.

```ssa
extern @std_time_now_ms() -> i64
extern @std_time_monotonic_ms() -> i64
extern @std_time_format_iso8601(ms: i64, out: *i8, cap: i32) -> i32
extern @std_time_parse_iso8601(s: *i8, len: i32, out_ms: *i64) -> i32

%now = call @std_time_now_ms()
%start = call @std_time_monotonic_ms()
// ... code under test ...
%end = call @std_time_monotonic_ms()
%elapsed = sub i64 %end, %start

%buf = let [i8, 32]
%bp = gep i8, *%buf, 0
%n = call @std_time_format_iso8601(1709210096789, %bp, 32)   // 2024-02-29T12:34:56.789Z
%ms = let i64
%msp = gep i64, *%ms, 0
%pr = call @std_time_parse_iso8601(%bp, %n, %msp)
```

- `now_ms` is Unix milliseconds (UTC); `monotonic_ms` is suitable for timing durations and is unaffected by system clock adjustments.
- `format_iso8601` always outputs `YYYY-MM-DDTHH:MM:SS.mmmZ` (24 characters), with years
  0000-9999 supported; the buffer must be at least 25 bytes (including NUL); it returns 24 on success and -1 on failure.
- `parse_iso8601` accepts `T` or a space separator, an optional 1-3 digit fractional second (extra digits are ignored), and a time zone of
  `Z`, `z`, `±HH:MM`, or omitted (treated as UTC); it returns 0 on success and -1 on failure.
- For sleeping, use the thread library's existing `@std_thread_sleep_ms`.

Compile example:

```bash
c1 app.bx boxes/std/time/time.bx -L ssa/platform -l platform -O2
```

### Paths and File System

`std/io/path.bx` is pure-SSA path handling and does not depend on the platform library; `std/io/fs.bx` wraps
`ssa/platform/fs.c` (POSIX `stat`/`opendir`) and requires the platform library at compile time.

```ssa
extern @std_io_path_basename(path: *i8, out: *i8, cap: i32) -> i32
extern @std_io_path_dirname(path: *i8, out: *i8, cap: i32) -> i32
extern @std_io_path_extension(path: *i8, out: *i8, cap: i32) -> i32
extern @std_io_path_join(a: *i8, b: *i8, out: *i8, cap: i32) -> i32
extern @std_io_path_is_absolute(path: *i8) -> i1

extern @std_io_fs_is_dir(path: *i8) -> i32
extern @std_io_fs_size(path: *i8) -> i64
extern @std_io_fs_mkdir(path: *i8) -> i32
extern @std_io_fs_opendir(path: *i8) -> *i8
extern @std_io_fs_readdir(dir: *i8, name: *i8, cap: i32) -> i32
extern @std_io_fs_closedir(dir: *i8) -> i32

%path = let [i8, 256]
%pp = gep i8, *%path, 0
%n = call @std_io_path_join("/tmp", "data", %pp, 256)   // /tmp/data

%dir = call @std_io_fs_opendir(%pp)
%name = let [i8, 256]
%np = gep i8, *%name, 0
@next:
  %r = call @std_io_fs_readdir(%dir, %np, 256)   // 1=entry 0=end -1=error
  ...
```

- Path functions write the result into the caller's buffer and return the length; they return -1 if the buffer is too small. Input and output must not overlap.
- `join` returns the second segment directly when it is an absolute path; `basename("")` returns `"."`.
- Boolean queries in `fs` return 1/0; `size`/`mtime` return -1 on failure; `mkdir`/`remove`/`rename`
  return 0 on success; `readdir` already skips `.` and `..`.

Compile example:

```bash
# Path functions only
c1 app.bx boxes/std/io/path.bx -O2
# Using file system functions
c1 app.bx boxes/std/io/path.bx boxes/std/io/fs.bx \
  -L ssa/platform -l platform -O2
```

### File Streams and Standard Streams

In addition to basic reads and writes, `std/io/file.bx` provides append mode, whole-file read/write, line-by-line reading, and seeking:

```ssa
extern @std_io_file_open_append(path: *i8) -> *i8
extern @std_io_file_write_all(fp: *i8, buf: *i8, len: i32) -> i32
extern @std_io_file_read_all(fp: *i8, buf: *i8, cap: i32) -> i32
extern @std_io_file_read_line(fp: *i8, buf: *i8, cap: i32) -> i32
extern @std_io_file_seek(fp: *i8, offset: i64, whence: i32) -> i32
extern @std_io_file_tell(fp: *i8) -> i64

%fp = call @std_io_file_open_read(%path)
%n = call @std_io_file_read_all(%fp, %buf, %cap)   // reads until EOF or cap-1 bytes, NUL-terminated
call @std_io_file_close(%fp)
```

- `read_all` reads at most `cap-1` bytes and truncates if the buffer is too small; it returns the actual number of bytes.
- `read_line` keeps the newline (consistent with `fgets`); it returns 0 at EOF.
- `seek` takes `@STD_IO_FILE_SEEK_SET/_CUR/_END` as `whence`.
- `std/io/stdio.bx` provides the standard streams (requires the platform library):

```ssa
extern @std_io_stdio_stdin() -> *i8
extern @std_io_stdio_stdout() -> *i8
extern @std_io_stdio_stderr() -> *i8
extern @std_io_stdio_write(buf: *i8, len: i32) -> i32
extern @std_io_stdio_write_err(buf: *i8, len: i32) -> i32
extern @std_io_stdio_read(buf: *i8, cap: i32) -> i32
extern @std_io_stdio_read_line(buf: *i8, cap: i32) -> i32
extern @std_io_stdio_flush() -> i32
```

Standard stream handles are simply `FILE*` and can also be passed directly to the `std_io_file_*` functions.

### Processes, Environment, and Command-Line Arguments

The entry function can directly receive `argc/argv`:

```ssa
extern @printf(fmt: *i8, ...) -> i32

fun @main(argc: i32, argv: **i8) -> i32 {
  @entry:
    %p = gep *i8, *%argv, 0
    %name = load *i8, *%p
    call @printf("argc=%d argv0=%s\n", %argc, %name)
    ret i32 0
}
```

`std/process/process.bx` (depends only on libc):

```ssa
extern @std_process_getenv(name: *i8) -> *i8   // returns null if unset
extern @std_process_exit(code: i32) -> void
extern @std_process_system(cmd: *i8) -> i32    // 0 means the shell succeeded
```

`system` goes through the platform shell; do not concatenate untrusted input into it.

Compile example:

```bash
c1 app.bx boxes/std/io/file.bx boxes/std/io/stdio.bx \
  boxes/std/process/process.bx -L ssa/platform -l platform -O2
```

### Dynamic Containers

All three containers take at least 32 bytes of caller-provided header storage; the internal data grows on the heap.

**vector**: a dynamic array of fixed-size elements.

```ssa
extern @std_data_vector_init(v: *i8, elem_size: i32, initial_cap: i32) -> i32
extern @std_data_vector_push(v: *i8, val: *i8) -> i32
extern @std_data_vector_get(v: *i8, idx: i32, out: *i8) -> i32
extern @std_data_vector_free(v: *i8) -> void

%v = let [i8, 32]
call @std_data_vector_init(%v, 4, 8)     // 4-byte elements, initial capacity 8
%val = let i32
%vp = gep i32, *%val, 0
store i32 42, *%vp
call @std_data_vector_push(%v, %vp)
%out = let i32
%op = gep i32, *%out, 0
call @std_data_vector_get(%v, 0, %op)    // out = 42
call @std_data_vector_free(%v)
```

**hashmap**: byte-string keys/values, FNV-1a + open addressing, automatically grows at 70% load factor.

```ssa
extern @std_data_hashmap_init(m: *i8, initial_cap: i32) -> i32
extern @std_data_hashmap_put(m: *i8, key: *i8, key_len: i32,
                             val: *i8, val_len: i32) -> i32
extern @std_data_hashmap_get(m: *i8, key: *i8, key_len: i32,
                             out: *i8, out_cap: i32) -> i32
extern @std_data_hashmap_free(m: *i8) -> void

%m = let [i8, 32]
call @std_data_hashmap_init(%m, 16)
call @std_data_hashmap_put(%m, "answer", 6, %vp, 4)   // 1=inserted 0=updated -1=error
%n = call @std_data_hashmap_get(%m, "answer", 6, %op, 4)   // returns the stored length
call @std_data_hashmap_free(%m)
```

- `get` / `slot_key` / `slot_value` copy at most `out_cap` bytes and return the full length, without appending a NUL.
- Iteration: get the slot count with `capacity()`, then loop over `slot_key(m, slot, ...)`; an empty slot returns -1.
- Deletion leaves tombstones; `count()` only counts live entries.
- `hashset` is built on top of hashmap and requires linking both files:

```bash
c1 app.bx boxes/std/data/hashset.bx boxes/std/data/hashmap.bx -O2
```

### Windows and Layout (raylib)

`ui/widget.bx` treats window content as a tree of `@UiNode`: containers are only column / row,
and leaves are rectangles, single-line text, buttons, checkboxes, sliders, progress bars, or single-line input fields. Layout has three steps —
`_measure` (bottom-up size computation), `_arrange` (top-down coordinate assignment), `_paint` (pre-order drawing);
`_hit` performs hit testing. Node kinds are given by the in-file `enum @UiKind`: `@UiKind.Column`,
`@UiKind.Row`, `@UiKind.Rect`, `@UiKind.Text`, `@UiKind.Button`, `@UiKind.Checkbox`,
`@UiKind.Slider`, `@UiKind.Progress`, `@UiKind.Input`. Enum members are local to the file,
so users must declare an identical copy themselves (see `ssa/c1/test/ui_demo.bx`).

```bash
# One-time: download/unpack raylib per thirdparty/packages.conf and build the static library
./tools/setup-thirdparty.sh raylib

# macOS linking (on Linux use -lGL -lm -lpthread -ldl instead)
c1 app.bx boxes/std/ui/widget.bx -L ssa/platform -l platform \
  -L thirdparty/raylib/build/raylib -l raylib \
  --link-flag "-framework Cocoa" --link-flag "-framework IOKit" \
  --link-flag "-framework OpenGL" --link-flag "-framework CoreVideo"
```

```ssa
%nodes = let [@UiNode, 8]          // write the node count as a constant array
%root = gep @UiNode, *%nodes, 0
call @std_ui_box(%root, @UiKind.Column, 24, 16) // pad 24, gap 16
call @std_ui_fill(%root, 0x1E242C, 16)
%t = gep @UiNode, *%nodes, 1
call @std_ui_text(%t, "hello", 20, 0xE6EDF3)
call @std_ui_append(%root, %t)

call @std_ui_layout(%root, 40, 40, 480)
call @std_ui_button_update(%root, %mx, %my, %down) // every frame: update pressed state
call @std_ui_paint(%root)
```

Buttons carry their own text, background color, text color, and corner radius; they darken automatically when pressed:

```ssa
%btn = gep @UiNode, *%nodes, 2
call @std_ui_button(%btn, "OK", 16, 0x2563EB, 0xFFFFFF, 8) // text,size,bg,fg,radius
call @std_ui_append(%root, %btn)

// click: the mouse is pressed and the hit target is the button
%pressed = call @c1_ui_mouse_pressed()
%hit = call @std_ui_button_at(%root, %mx, %my)
%is_press = ne i32 %pressed, 0
%is_btn = ne *@UiNode %hit, null
%clicked = and i1 %is_press, %is_btn
```

The other interactive controls share a single per-frame entry point `_pointer` (press edge + held down). It also refreshes the button's
pressed color, toggles checkboxes, drags sliders, and gives focus to the clicked input field:

```ssa
call @std_ui_pointer(%root, %mx, %my, %down, %pressed)

%cb = gep @UiNode, *%nodes, 3
call @std_ui_checkbox(%cb, "Enable", 16, 0x2563EB, 0xE6EDF3) // text,size,accent,fg

%sl = gep @UiNode, *%nodes, 4
call @std_ui_slider(%sl, 40, 100, 0x2563EB, 0x2D3540)         // value,max,accent,track

%pg = gep @UiNode, *%nodes, 5
call @std_ui_progress(%pg, 70, 100, 0x22C55E, 0x2D3540)       // value,max,accent,track

// the buffer is owned by the caller, capacity includes the trailing NUL; edited by _keyboard once focused
%in = gep @UiNode, *%nodes, 6
call @std_ui_input(%in, %buf, 64, 16, 0x0E1116, 0xE6EDF3)     // text,capacity,size,bg,fg

// every frame: feed this frame's keyboard input to the focused input field; returns 1 when Enter was pressed (focus lost)
%enter = call @std_ui_keyboard(%root)
```

Individual characters can also be fed manually: `call @std_ui_input_type(%root, %ch)`,
`call @std_ui_input_backspace(%root)`. A checkbox's value lives in the node's `@value` field (0/1);
sliders and progress bars use `@value`/`@max`.

Notes:

- Colors are `0xRRGGBB` (no alpha); `-1` means no fill.
- A column stretches its children to the inner width; elements that need a fixed width should currently be placed in a row.
- Button size is text width + 32px and text height + 16px; placed in a column it will be stretched across the full line.
- Structs and cross-file functions must be redeclared in the caller's file (SSA currently parses per file).
- `ui_demo.bx` is a complete example: it runs 30 frames, writes `ui_demo.png`, and exits.

### Text and Encoding

`text/str.bx` adds:

- `@std_text_str_starts_with(s, prefix) -> i1`
- `@std_text_str_trim(s, out, cap) -> i32` — copies after stripping leading and trailing whitespace
- `@std_text_str_split_next(s, pos, sep, out, cap) -> i32` — iterative splitting:
  `pos` starts at 0, returns the segment length, -1 means done; empty segments are preserved
- `@std_text_str_replace(s, needle, repl, out, cap) -> i32` — needle must not be empty

```ssa
%pos = let i32
%pp = gep i32, *%pos, 0
store i32 0, *%pp
@next:
  %n = call @std_text_str_split_next("a,b", %pp, ',', %part, 64)
  %done = eq i32 %n, -1
  br %done, @finish, @next
```

`text/hex.bx`
- `@std_text_hex_encode(data, len, out, cap)` outputs lowercase hex + NUL
- `@std_text_hex_decode(src, src_len, out, out_cap, out_len)` accepts upper or lower case; the length must be even

`text/utf8.bx`
- `@std_text_utf8_validate(s, len) -> i32`
- `@std_text_utf8_decode(s, len, pos, cp) -> i32` (1 success, 0 done, -1 invalid)
- `@std_text_utf8_encode(cp, out, cap) -> i32`
- `@std_text_utf8_count(s, len) -> i32`
- Rejects overlong encodings, the surrogate range, and encodings above U+10FFFF

`crypto/base64.bx` now has both encoding and decoding:

```ssa
extern @std_crypto_b64_encode(out: *i8, data: *i8, in_len: i32) -> void
extern @std_crypto_b64_decode(src: *i8, src_len: i32, out: *i8,
                               out_cap: i32, out_len: *i32) -> i32
```

`b64_decode` ignores whitespace (convenient for decoding PEM content directly) and requires standard padding; the output is not NUL-terminated.
These modules depend only on libc; just add the corresponding `.bx` at compile time:

```bash
c1 app.bx boxes/std/text/str.bx boxes/std/text/hex.bx \
  boxes/std/text/utf8.bx boxes/std/crypto/base64.bx -O2
```

### PEM Reading and RSA Private Key Parsing

`crypto/pem.bx` can now both write and read:

```ssa
extern @std_crypto_pem_find(pem: *i8, pem_len: i32, label: *i8, label_len: i32,
                            out: *i8, out_cap: i32, out_len: *i32) -> i32
extern @std_crypto_pem_read_cert(pem: *i8, pem_len: i32,
                                 out: *i8, out_cap: i32, out_len: *i32) -> i32
extern @std_crypto_pem_read_rsa_private(pem: *i8, pem_len: i32,
                                        n: *i32, e: *i32, d: *i32,
                                        limbs: i32) -> i32
extern @std_crypto_pem_read_certs(pem: *i8, pem_len: i32, pool: *i8, pool_cap: i32,
                                  offsets: *i32, lens: *i32, max_count: i32) -> i32
```

- `pem_find` matches blocks by `-----BEGIN <label>-----` (`label_len == 0` accepts any block) and
  base64-decodes into `out`; whitespace (newlines etc.) is ignored.
- `read_cert` is equivalent to calling `pem_find` with the `CERTIFICATE` label.
- `read_rsa_private` accepts both PKCS#1 `RSA PRIVATE KEY` and PKCS#8
  `PRIVATE KEY`, and outputs the `n/e/d` big integers; `limbs` must hold the modulus (64 for 2048 bits).
- `read_certs` reads all `CERTIFICATE` blocks in the file, writes the DER sequentially into `pool`,
  and records the position of the i-th certificate in `offsets[i]`/`lens[i]`, returning the count (0 means no certificates).
  This is exactly how TLS trust anchors are loaded; see "TLS Client Trust Chain".
- A return value of 0 means success; -1 means not found, malformed, or output buffer too small.

Compiling requires including `pem.bx`, `base64.bx`, and `rsa.bx` together:

```bash
c1 app.bx boxes/std/crypto/pem.bx boxes/std/crypto/base64.bx \
  boxes/std/crypto/rsa.bx -O2
```

Together with `crypto/rsa_pss.bx` and `crypto/oaep.bx`, it can also do signing and OAEP encryption:

- `@std_crypto_rsa_sign_pss_sha256(hash, d, n, limbs, signature) -> i32`
  — signs a 32-byte SHA-256 digest with a random 32-byte salt; pairs with
  `@std_crypto_rsa_verify_pss_sha256`
- `@std_crypto_rsa_encrypt_oaep_sha256(msg, msg_len, e, e_limbs, n, limbs, ct)`
- `@std_crypto_rsa_decrypt_oaep_sha256(ct, d, n, limbs, out, out_cap) -> i32`
  — returns the message length, -1 on failure; the label is empty, and the message limit is `k - 66` bytes

```bash
c1 app.bx boxes/std/crypto/oaep.bx boxes/std/crypto/rsa.bx \
  boxes/std/crypto/sha.bx -O2
```
