# Box — SSA 标准库

## 什么是 Box

Box 是 SSA 生态中的代码复用单元。每个 box 是一个 `.bx` 文件，内部包含一组相关的函数定义。Box 之间通过 `call @func_name()` 组合使用。

Box 的设计原则：

> **命名约定：函数名以路径为前缀。** 例如 `std/core/math.bx` 中的函数名格式为 `@std_core_math_<func>`。这个前缀避免了与 libc 函数（如 `exit`、`printf`）或其他 box 中的同名函数冲突。extern 声明保持原名不变。
>
> `std/net/` 是例外，沿用更早的 `@net_<file>_<func>` 前缀（如 `@net_socket_tcp`），不改成 `@std_net_*`，以免大规模重命名现有代码。

| 原则 | 说明 |
|------|------|
| **单一职责** | 每个 box 只做一类事（数学、内存、IO...） |
| **无依赖** | Box 之间没有内部依赖，需要用哪个就 include 哪个 |
| **纯 SSA** | 只需要 libc 的部分用 `extern` 声明，编译时自动链接 |
| **命名约定** | 函数以路径前缀 `@<dir>_<file>_<func>` 命名（如 `@std_core_math_min`），避免与 libc 符号冲突 |

使用 box 只需将多个 `.bx` 文件传给 `c1` 即可互相调用：

```bash
c1 math.bx print.bx main.bx -O2
```

`c1` 在编译前将所有文件拼接为一个源文件，所以不同文件的函数可以直接 `call`。无需 `import`、`use` 或任何额外声明。

## 现有标准库一览

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
│                         #   _append, _layout, _paint, _hit,
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
│   ├── rsa.bx            # @std_crypto_rsa_bn 大数运算 (_add/_sub/_mul/_mod/_mod_pow/_gcd 等)
│   ├── ecc.bx            # @std_crypto_ecc_generate_keypair, _sign, _verify, _ecdh
│   ├── base64.bx         # @std_crypto_b64_encode, _b64_decode
│   ├── aes.bx            # @std_crypto_aes_encrypt/decrypt (128/256), _aes128_cbc/256_cbc, _aes_gcm_encrypt/decrypt
│   ├── hmac.bx           # @std_crypto_hmac_sha256, _hmac_md5/_sha1/_sha384/_sha512
│   ├── hkdf.bx           # @std_crypto_hkdf_extract/expand_sha256/_sha384,
│   │                     #   _tls13_* key schedule (expand-label, derive-secret, traffic keys)
│   ├── rsa_padding.bx    # @std_crypto_pkcs1_v15_*, _rsa_encrypt/decrypt_pkcs1_v15
│   ├── rsa_pss.bx        # @std_crypto_rsa_sign/_verify_pss_sha256 (随机 32 字节 salt)
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
        ├── base.bx        # 结构体与编解码辅助、@derive12、@send_hs/read_hs、
        │                  #   @net_tls_send/recv/close（按 role 选密钥）、@ch12
        ├── handshake.bx   # @hs12_parse_ch/_build_sh/_build_cert,
        │                  #   @hs12_finished_verify, @hs12_master_secret_ems
        ├── client12.bx    # @tls12_hs (完整握手), @tls12_step_* (各步骤函数)
        ├── client13.bx    # @net_tls13_connect (RFC 8446, AES-128-GCM + P-256)
        ├── client_api.bx  # @net_tls12_connect, @net_tls_init, @net_tls_set_host,
        │                  #   @net_tls_set_trust
        ├── server12.bx    # @tls12_server_hs (TLS 1.2 RSA 密钥交换服务端)
        └── server_api.bx  # @net_tls_server_accept (PEM/DER 证书 + RSA 私钥)


## 平台库

`platform/` 层的 C 代码提供跨平台非 ISO-C 系统 API 的包装：

| 文件 | 函数 | 平台 |
|------|------|------|
| `socket.c` | `c1_socket_create`, `c1_socket_bind`, `c1_socket_accept`, `c1_socket_connect`, `c1_socket_send` 等 | macOS: BSD sockets; Linux: POSIX |
| `ioselect.c` | `c1_ioselect_init`, `c1_ioselect_add`, `c1_ioselect_poll` | macOS: kqueue; Linux: epoll |
| `random.c` | `c1_random_buf(buf, len)` | macOS: `arc4random_buf`; Linux: `getrandom` |
| `cpu.c` | `c1_cpu_features()` — AES/SHA256/PMULL/SHA512 bitmask; `C1_NO_HW=1` forces zero | macOS: `sysctl`; Linux: `getauxval`; x86: `cpuid` |
| `crypto_hw.c` | `c1_aes*_encrypt/decrypt_block`, `c1_aes*_cbc_*`, `c1_aes*_gcm_*`, `c1_sha256_transform` | ARMv8 Crypto / AES-NI, 4-way PMULL/PCLMULQDQ GHASH; built with -O3 |
| `thread.c` | `c1_thread_create`, `c1_thread_join`, `c1_thread_detach` | macOS / Linux: POSIX `pthread` |
| `sync.c` | `c1_mutex_*`, `c1_cond_*`, `c1_thread_yield/sleep_ms/self`, `c1_cpu_count` | macOS / Linux: POSIX `pthread` |
| `ui.c` | `c1_ui_init/close/begin_frame/end_frame/clear`, `c1_ui_rect/_rect_rounded/_text/_text_width`, `c1_ui_load_font`, `c1_ui_mouse_*`, `c1_ui_screenshot` | macOS / Linux: raylib（第三方源码包，`tools/setup-thirdparty.sh` 按 `thirdparty/packages.conf` 解压编译） |

## 常用模式

### 泛型数据结构（以 stack 为例）

SSA 没有泛型，但可以用 `*i8` + `elem_size` 实现**类型无关**的数据结构：

```ssa
// 初始化一个可存 16 个 i32 的栈：256 字节，elem_size = 4
%stack = let [i8, 256]
call @std_data_stack_init(%stack, 64, 4)

// push：caller 自行准备 elem_size 字节的数据
store i32 123, *%tmp_4bytes
call @std_data_stack_push(%stack, %tmp_4bytes)

// pop：caller 提供目标缓冲区
call @std_data_stack_pop(%stack, %out_4bytes)
%val = load i32, *%out_4bytes
```

### 调用 libc

用 `extern` 声明即可，无需手动链接：

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

编译时 `c1` 会自动调用系统 `cc` 完成链接。

### 组合 box 编写应用

```bash
# 多个 .bx 文件直接一起编译，函数自动可见
c1 std/core/math.bx my_print.bx main.bx -O2
```

`main.bx` 中直接调用 `math.bx` 的函数：

```ssa
fun @main() -> i32 {
  @e:
    %m = call @std_core_math_min(3, 7)
    ret i32 0
}
```

如果要复用的 std box 不在当前目录，使用绝对或相对路径即可：

```bash
c1 ../std/core/math.bx main.bx -o my_prog
```

### 多线程

语言没有函数指针，所以所有线程共用一个由应用定义的入口函数 `@thread_run`，
`task_id` 用来区分不同的线程体，`arg` 是 `std_thread_spawn` 传入的同一个指针：

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

编译时需要同时链接线程 box 和平台库（Linux 上旧版 glibc 还需 `-l pthread`）：

```bash
c1 app.bx boxes/std/thread/thread.bx boxes/std/thread/sync.bx \
  -L ssa/platform -l platform -l pthread
```

`@std_thread_mutex_trylock(m)` 不阻塞：返回 0 表示拿到锁，1 表示已被其他线程持有。
`@std_thread_cond_timedwait(c, m, ms)` 最多等待 `ms` 毫秒：返回 0 表示被唤醒，
1 表示超时；无论哪种返回，mutex 都重新处于持有状态。

### TLS 1.2 服务端

`net/tls/server_api.bx` 提供最小服务端入口。它在已经 `accept` 的连接上完成
TLS 1.2 握手（`TLS_RSA_WITH_AES_128_GCM_SHA256`），随后用统一的
`net_tls_send/recv/close` 收发应用数据：

```ssa
extern @net_tls_server_accept(ctx: *i8, fd: i32, cert: *i8, cert_len: i32,
                              key: *i8, key_len: i32, limbs: i32) -> i32
extern @net_tls_recv(ctx: *i8, buf: *i8, cap: i32, out_len: *i32) -> i32
extern @net_tls_send(ctx: *i8, data: *i8, len: i32) -> i32

fun @main() -> i32 {
  @entry:
    %ctx = let [i8, 1024]
    // ... 建立监听，accept 得到 %cfd，读取证书/私钥 ...
    %r = call @net_tls_server_accept(%ctx, %cfd, %certp, %cert_len,
                                     %keyp, %key_len, 64)
    // 此后 %ctx 的 role 为服务端，send/recv 自动选用服务端密钥
    ret i32 %r
}
```

- `cert` / `key` 可以是 PEM 或 DER；PEM 取文件中的第一个块。私钥支持
  PKCS#1 `RSAPrivateKey` 和 PKCS#8 `PrivateKeyInfo`，RSA 位宽需与 `limbs` 匹配
  （2048 位用 64）。
- 支持 extended master secret 与 secure renegotiation；不支持客户端证书、
  会话恢复和重协商。
- `net_tls_send/recv` 按 context 的 role 选择密钥、IV 和序列号，客户端与服务端
  共用同一套收发函数。

编译服务端需要下列文件（客户端栈不需要 x509/p256/hkdf）：

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

### TLS 客户端信任链

客户端默认不校验证书链。配置信任锚后，TLS 1.3 和 TLS 1.2 客户端都会执行：
主机名匹配（SAN dNSName / CN）→ 逐级用 issuer 公钥验签 → 每张非根证书的
有效期检查。TLS 1.3 的中间证书来自 Certificate 消息，TLS 1.2 则来自
Certificate 列表，两者都跳过第一张（叶子证书）。

```ssa
extern @net_tls_init(ctx: *i8) -> void
extern @net_tls_set_trust(ctx: *i8, pool: *i8, offsets: *i32, lens: *i32,
                          count: i32) -> void
extern @std_crypto_pem_read_certs(pem: *i8, pem_len: i32, pool: *i8, pool_cap: i32,
                                  offsets: *i32, lens: *i32, max_count: i32) -> i32

fun @main() -> i32 {
  @entry:
    %ctx = let @TLSContext           // 栈上内存不会自动清零
    call @net_tls_init(%cp)          // 必须先清空 trust 配置
    call @net_tls_set_host(%cp, "example.com", 11)
    // PEM 里可以有多个 CERTIFICATE 块
    %n = call @std_crypto_pem_read_certs(%pemp, %pem_len, %poolp, 16384,
                                         %offsp, %lensp, 8)
    call @net_tls_set_trust(%cp, %poolp, %offsp, %lensp, %n)
    %r = call @net_tls13_connect(%cp, %fd, 64)   // 校验失败返回非 0
    ret i32 %r
}
```

- HTTP 客户端用 `net_http_client_open_trusted(host, host_len, port, use_tls, c,
  pool, offsets, lens, count)` 启用同一套校验；`net_http_client_open` 保持宽松默认。
- `count == 0` 时保持旧的宽松行为：TLS 1.3 只探测自签名证书，TLS 1.2 完全不校验。
  因此不调用 `net_tls_init` 的栈上 context 会读到随机值，必须显式清零
  （`@memset` 或 `net_tls_init`）。
- 信任锚本身被无条件信任：只检查「当前证书由该锚签发」，不再校验锚的自签名和有效期。
- `x509_verify_chain` 返回 `0` 成功、`-1` 超过 8 层链深、`-2` 证书过期/未生效、
  `-3` 找不到签发者。
- 只支持 RSA PKCS#1 v1.5、RSASSA-PSS 和 ECDSA P-256 签名。

### JSON

`std/json/json.bx` 提供解析、树构建、访问和序列化，只依赖 libc（`malloc`/`free`）。
解析结果是 `@JsonValue` 节点树，数组/对象的孩子用兄弟链表连接；字符串和键都会复制
到堆上，解析后原输入缓冲可以释放。

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

构建并输出：

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

接口分组：

| 用途 | 函数 |
|------|------|
| 解析 | `@std_json_parse(text, len)`、`@std_json_parse_ex(text, len, err_pos)` |
| 访问 | `@std_json_type`、`_count`、`_at`、`_get`、`_bool`、`_int`、`_float`、`_str` |
| 构建标量 | `_new_null`、`_new_bool`、`_new_int`、`_new_float`、`_new_string` |
| 构建容器 | `_new_array`、`_new_object`、`_add`、`_set` |
| 序列化 | `@std_json_write(v, out, cap)` |
| 释放 | `@std_json_free(v)` |

- `@std_json_parse` 失败返回 `null`；`@std_json_parse_ex(text, len, err_pos)` 还会把
  出错字节偏移写入 `err_pos`（成功为 0）。
- 数字保留整数/浮点两种形式：无小数点和指数的按 `i64` 保存（超出范围时 `strtoll`
  会饱和），否则按 `f64` 保存。
- `@std_json_write` 输出紧凑 JSON；缓冲区不足返回 -1，成功返回写入字节数并补 NUL。
- 支持 `\uXXXX` 与代理对解码、UTF-8 原样透传；序列化时转义引号、反斜杠和控制字符。
- 编译时加上 `boxes/std/json/json.bx` 即可，无需平台库。

### 时间

`std/time/time.bx` 提供毫秒时间戳和 ISO 8601（UTC）格式化/解析。时钟由平台层
`ssa/platform/time.c` 通过 POSIX `clock_gettime` 提供，编译时需要链接平台库。

```ssa
extern @std_time_now_ms() -> i64
extern @std_time_monotonic_ms() -> i64
extern @std_time_format_iso8601(ms: i64, out: *i8, cap: i32) -> i32
extern @std_time_parse_iso8601(s: *i8, len: i32, out_ms: *i64) -> i32

%now = call @std_time_now_ms()
%start = call @std_time_monotonic_ms()
// ... 被测代码 ...
%end = call @std_time_monotonic_ms()
%elapsed = sub i64 %end, %start

%buf = let [i8, 32]
%bp = gep i8, *%buf, 0
%n = call @std_time_format_iso8601(1709210096789, %bp, 32)   // 2024-02-29T12:34:56.789Z
%ms = let i64
%msp = gep i64, *%ms, 0
%pr = call @std_time_parse_iso8601(%bp, %n, %msp)
```

- `now_ms` 是 Unix 毫秒（UTC）；`monotonic_ms` 适合测耗时，不受系统时钟调整影响。
- `format_iso8601` 固定输出 `YYYY-MM-DDTHH:MM:SS.mmmZ`（24 字符），年份支持
  0000-9999；缓冲区至少 25 字节（含 NUL），成功返回 24，失败返回 -1。
- `parse_iso8601` 接受 `T` 或空格分隔，可选 1-3 位小数秒（多余位数忽略），时区可
  为 `Z`、`z`、`±HH:MM` 或省略（按 UTC）；成功返回 0，失败返回 -1。
- 休眠请使用线程库已有的 `@std_thread_sleep_ms`。

编译示例：

```bash
c1 app.bx boxes/std/time/time.bx -L ssa/platform -l platform -O2
```

### 路径与文件系统

`std/io/path.bx` 是纯 SSA 的路径处理，不依赖平台库；`std/io/fs.bx` 封装
`ssa/platform/fs.c`（POSIX `stat`/`opendir`），编译时需要链接平台库。

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
  %r = call @std_io_fs_readdir(%dir, %np, 256)   // 1=有项 0=结束 -1=错误
  ...
```

- 路径函数把结果写入调用方缓冲并返回长度；缓冲区不足返回 -1。输入输出不可重叠。
- `join` 在第二段是绝对路径时直接返回第二段；`basename("")` 返回 `"."`。
- `fs` 的布尔查询返回 1/0；`size`/`mtime` 失败返回 -1；`mkdir`/`remove`/`rename`
  成功返回 0；`readdir` 已跳过 `.` 和 `..`。

编译示例：

```bash
# 只用路径函数
c1 app.bx boxes/std/io/path.bx -O2
# 使用文件系统函数
c1 app.bx boxes/std/io/path.bx boxes/std/io/fs.bx \
  -L ssa/platform -l platform -O2
```

### 文件流与标准流

`std/io/file.bx` 在基础读写之外还提供追加模式、整文件读写、按行读取和定位：

```ssa
extern @std_io_file_open_append(path: *i8) -> *i8
extern @std_io_file_write_all(fp: *i8, buf: *i8, len: i32) -> i32
extern @std_io_file_read_all(fp: *i8, buf: *i8, cap: i32) -> i32
extern @std_io_file_read_line(fp: *i8, buf: *i8, cap: i32) -> i32
extern @std_io_file_seek(fp: *i8, offset: i64, whence: i32) -> i32
extern @std_io_file_tell(fp: *i8) -> i64

%fp = call @std_io_file_open_read(%path)
%n = call @std_io_file_read_all(%fp, %buf, %cap)   // 读到 EOF 或 cap-1，补 NUL
call @std_io_file_close(%fp)
```

- `read_all` 最多读 `cap-1` 字节，缓冲区不足时截断；返回实际字节数。
- `read_line` 保留换行符（与 `fgets` 一致），EOF 返回 0。
- `seek` 的 `whence` 用 `@STD_IO_FILE_SEEK_SET/_CUR/_END`。
- `std/io/stdio.bx` 提供标准流（需要平台库）：

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

标准流句柄就是 `FILE*`，也可以直接传给 `std_io_file_*` 的函数。

### 进程、环境与命令行参数

入口函数可以直接接收 `argc/argv`：

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

`std/process/process.bx`（只依赖 libc）：

```ssa
extern @std_process_getenv(name: *i8) -> *i8   // 未设置返回 null
extern @std_process_exit(code: i32) -> void
extern @std_process_system(cmd: *i8) -> i32    // 0 表示 shell 成功
```

`system` 会经过平台 shell，不要拼接不可信的输入。

编译示例：

```bash
c1 app.bx boxes/std/io/file.bx boxes/std/io/stdio.bx \
  boxes/std/process/process.bx -L ssa/platform -l platform -O2
```

### 动态容器

三个容器都由调用方提供至少 32 字节的头部存储，内部数据在堆上增长。

**vector**：固定大小元素的动态数组。

```ssa
extern @std_data_vector_init(v: *i8, elem_size: i32, initial_cap: i32) -> i32
extern @std_data_vector_push(v: *i8, val: *i8) -> i32
extern @std_data_vector_get(v: *i8, idx: i32, out: *i8) -> i32
extern @std_data_vector_free(v: *i8) -> void

%v = let [i8, 32]
call @std_data_vector_init(%v, 4, 8)     // 4 字节元素，初始 8 个
%val = let i32
%vp = gep i32, *%val, 0
store i32 42, *%vp
call @std_data_vector_push(%v, %vp)
%out = let i32
%op = gep i32, *%out, 0
call @std_data_vector_get(%v, 0, %op)    // out = 42
call @std_data_vector_free(%v)
```

**hashmap**：字节串 key/value，FNV-1a + 开放寻址，负载到 70% 自动扩容。

```ssa
extern @std_data_hashmap_init(m: *i8, initial_cap: i32) -> i32
extern @std_data_hashmap_put(m: *i8, key: *i8, key_len: i32,
                             val: *i8, val_len: i32) -> i32
extern @std_data_hashmap_get(m: *i8, key: *i8, key_len: i32,
                             out: *i8, out_cap: i32) -> i32
extern @std_data_hashmap_free(m: *i8) -> void

%m = let [i8, 32]
call @std_data_hashmap_init(%m, 16)
call @std_data_hashmap_put(%m, "answer", 6, %vp, 4)   // 1=插入 0=更新 -1=错误
%n = call @std_data_hashmap_get(%m, "answer", 6, %op, 4)   // 返回存储长度
call @std_data_hashmap_free(%m)
```

- `get` / `slot_key` / `slot_value` 最多复制 `out_cap` 字节并返回完整长度，不追加 NUL。
- 遍历：`capacity()` 取槽数，循环 `slot_key(m, slot, ...)`，空槽返回 -1。
- 删除会留下墓碑，`count()` 只统计有效项。
- `hashset` 建立在 hashmap 之上，需要同时链接两个文件：

```bash
c1 app.bx boxes/std/data/hashset.bx boxes/std/data/hashmap.bx -O2
```

### 窗口与布局（raylib）

`ui/widget.bx` 把窗口内容当成一棵 `@UiNode` 树：容器只有 column / row 两种，
叶子是矩形、单行文字或按钮。布局分三步 —— `_measure`（自底向上算尺寸）、
`_arrange`（自顶向下给坐标）、`_paint`（前序绘制）；`_hit` 做命中测试。
节点 kind 由文件内的 `enum @UiKind` 给出：`@UiKind.Column`、`@UiKind.Row`、
`@UiKind.Rect`、`@UiKind.Text`、`@UiKind.Button`。枚举成员是文件局部的，
使用方要自己声明同样的一份（见 `ssa/c1/test/ui_demo.bx`）。

```bash
# 一次性：按 thirdparty/packages.conf 下载/解压 raylib 并编译静态库
./tools/setup-thirdparty.sh raylib

# macOS 链接（Linux 换成 -lGL -lm -lpthread -ldl）
c1 app.bx boxes/std/ui/widget.bx -L ssa/platform -l platform \
  -L thirdparty/raylib/build/raylib -l raylib \
  --link-flag "-framework Cocoa" --link-flag "-framework IOKit" \
  --link-flag "-framework OpenGL" --link-flag "-framework CoreVideo"
```

```ssa
%nodes = let [@UiNode, 8]          // 节点数写成常量数组
%root = gep @UiNode, *%nodes, 0
call @std_ui_box(%root, @UiKind.Column, 24, 16) // pad 24, gap 16
call @std_ui_fill(%root, 0x1E242C, 16)
%t = gep @UiNode, *%nodes, 1
call @std_ui_text(%t, "hello", 20, 0xE6EDF3)
call @std_ui_append(%root, %t)

call @std_ui_layout(%root, 40, 40, 480)
call @std_ui_button_update(%root, %mx, %my, %down) // 每帧：更新按下状态
call @std_ui_paint(%root)
```

按钮自带文本、背景色、文字色和圆角，按下时会自动变暗：

```ssa
%btn = gep @UiNode, *%nodes, 2
call @std_ui_button(%btn, "OK", 16, 0x2563EB, 0xFFFFFF, 8) // text,size,bg,fg,radius
call @std_ui_append(%root, %btn)

// 点击：鼠标按下且命中的是按钮
%pressed = call @c1_ui_mouse_pressed()
%hit = call @std_ui_button_at(%root, %mx, %my)
%is_press = ne i32 %pressed, 0
%is_btn = ne *@UiNode %hit, null
%clicked = and i1 %is_press, %is_btn
```

注意事项：

- 颜色是 `0xRRGGBB`（不带 alpha），`-1` 表示不填充。
- column 会把自己的子节点拉伸到内宽；需要固定宽度的元素目前请放进 row。
- 按钮的尺寸由文字宽度 + 32px、文字高度 + 16px 得出；放在 column 里会被拉满整行。
- 结构体和跨文件函数都要在调用方文件里重复声明（SSA 目前按文件解析）。
- `ui_demo.bx` 是完整示例：跑 30 帧后写 `ui_demo.png` 退出。

### 文本与编码

`text/str.bx` 新增：

- `@std_text_str_starts_with(s, prefix) -> i1`
- `@std_text_str_trim(s, out, cap) -> i32` — 去掉首尾空白后复制
- `@std_text_str_split_next(s, pos, sep, out, cap) -> i32` — 迭代切分：
  `pos` 初始为 0，返回分段长度，-1 表示结束；空分段保留
- `@std_text_str_replace(s, needle, repl, out, cap) -> i32` — needle 不能为空

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
- `@std_text_hex_encode(data, len, out, cap)` 输出小写十六进制 + NUL
- `@std_text_hex_decode(src, src_len, out, out_cap, out_len)` 接受大小写，长度必须为偶数

`text/utf8.bx`
- `@std_text_utf8_validate(s, len) -> i32`
- `@std_text_utf8_decode(s, len, pos, cp) -> i32`（1 成功、0 结束、-1 非法）
- `@std_text_utf8_encode(cp, out, cap) -> i32`
- `@std_text_utf8_count(s, len) -> i32`
- 拒绝 overlong、代理区与超过 U+10FFFF 的编码

`crypto/base64.bx` 现在同时有编码和解码：

```ssa
extern @std_crypto_b64_encode(out: *i8, data: *i8, in_len: i32) -> void
extern @std_crypto_b64_decode(src: *i8, src_len: i32, out: *i8,
                               out_cap: i32, out_len: *i32) -> i32
```

`b64_decode` 忽略空白（便于直接解码 PEM 内容），要求标准填充；输出不补 NUL。
这些模块都只依赖 libc，编译时直接加入对应 `.bx` 即可：

```bash
c1 app.bx boxes/std/text/str.bx boxes/std/text/hex.bx \
  boxes/std/text/utf8.bx boxes/std/crypto/base64.bx -O2
```

### PEM 读取与 RSA 私钥解析

`crypto/pem.bx` 现在既能写也能读：

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

- `pem_find` 按 `-----BEGIN <label>-----` 匹配块（`label_len == 0` 接受任意块），
  base64 解码到 `out`；空白（换行等）会被忽略。
- `read_cert` 等价于用 `CERTIFICATE` 标签调用 `pem_find`。
- `read_rsa_private` 同时接受 PKCS#1 `RSA PRIVATE KEY` 和 PKCS#8
  `PRIVATE KEY`，输出 `n/e/d` 大整数；`limbs` 需容纳模数（2048 位用 64）。
- `read_certs` 读取文件里所有 `CERTIFICATE` 块，DER 依次写入 `pool`，
  `offsets[i]`/`lens[i]` 记录第 i 张证书的位置，返回张数（0 表示没有证书）。
  这正是 TLS 信任锚的加载方式，见「TLS 客户端信任链」。
- 返回值 0 表示成功，-1 表示找不到、格式错误或输出缓冲区不足。

编译时需同时包含 `pem.bx`、`base64.bx`、`rsa.bx`：

```bash
c1 app.bx boxes/std/crypto/pem.bx boxes/std/crypto/base64.bx \
  boxes/std/crypto/rsa.bx -O2
```

配合 `crypto/rsa_pss.bx` 与 `crypto/oaep.bx` 还可以做签名和 OAEP 加密：

- `@std_crypto_rsa_sign_pss_sha256(hash, d, n, limbs, signature) -> i32`
  — 对 32 字节 SHA-256 摘要签名，salt 为 32 字节随机数；与
  `@std_crypto_rsa_verify_pss_sha256` 配对
- `@std_crypto_rsa_encrypt_oaep_sha256(msg, msg_len, e, e_limbs, n, limbs, ct)`
- `@std_crypto_rsa_decrypt_oaep_sha256(ct, d, n, limbs, out, out_cap) -> i32`
  — 返回消息长度，失败 -1；标签为空，消息上限 `k - 66` 字节

```bash
c1 app.bx boxes/std/crypto/oaep.bx boxes/std/crypto/rsa.bx \
  boxes/std/crypto/sha.bx -O2
```
