#!/bin/bash
# SSA test runner.
#
#   ./run_all.sh              # unit + fast + crypto (default)
#   ./run_all.sh fast         # instruction semantics and pure-SSA tests
#   ./run_all.sh crypto       # algorithm vectors, hardware and software paths
#   ./run_all.sh net          # TLS against a local loopback server
#   ./run_all.sh online       # HTTPS to www.qq.com (needs internet)
#   ./run_all.sh all          # everything
#
# Exits non-zero if anything fails.

set -u
cd "$(dirname "$0")"

C1=${C1:-../c1}
BOX=../../boxes
PLAT=../../platform
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

PASSED=0
FAILED=0
FAIL_NAMES=""

ok()   { PASSED=$((PASSED + 1)); }
bad()  { FAILED=$((FAILED + 1)); FAIL_NAMES="$FAIL_NAMES $1"; echo "FAIL: $1"; }

# compile <name> <extra c1 args...>
# OPT_LEVEL overrides the optimization level for one call.
compile() {
    local name=$1; shift
    if ! $C1 "$@" -o "$OUT/$name" "${OPT_LEVEL:--O2}" >"$OUT/$name.log" 2>&1; then
        echo "COMPILE FAIL: $name"
        sed -n '1,6p' "$OUT/$name.log"
        return 1
    fi
    return 0
}

# run_one <name> <extra c1 args...>
run_one() {
    local name=$1; shift
    compile "$name" "$@" || { bad "$name"; return; }
    if ! "$OUT/$name" >"$OUT/$name.out" 2>&1; then
        echo "RUN FAIL: $name"
        sed -n '1,6p' "$OUT/$name.out"
        bad "$name"
        return
    fi
    ok
}

# run_hw_sw <name> <extra c1 args...>
# Runs the same binary twice, once with hardware crypto and once forced to the
# software path, and requires identical output.
run_hw_sw() {
    local name=$1; shift
    compile "$name" "$@" || { bad "$name"; return; }
    if ! "$OUT/$name" >"$OUT/$name.hw" 2>&1; then
        echo "RUN FAIL (hw): $name"; sed -n '1,6p' "$OUT/$name.hw"; bad "$name"; return
    fi
    if ! C1_NO_HW=1 "$OUT/$name" >"$OUT/$name.sw" 2>&1; then
        echo "RUN FAIL (sw): $name"; sed -n '1,6p' "$OUT/$name.sw"; bad "$name"; return
    fi
    if ! cmp -s "$OUT/$name.hw" "$OUT/$name.sw"; then
        echo "HW/SW MISMATCH: $name"
        diff -u "$OUT/$name.hw" "$OUT/$name.sw" | head -10
        bad "$name"
        return
    fi
    ok
}

group_unit() {
    echo "== unit tests =="
    if (cd ../../test && make test >"$OUT/unit.log" 2>&1); then ok; else
        bad "ssa/test"; tail -20 "$OUT/unit.log"
    fi
    if (cd ../../test/codec_test && make test >"$OUT/codec.log" 2>&1); then ok; else
        bad "codec_test"; tail -20 "$OUT/codec.log"
    fi
}

group_fast() {
    echo "== fast tests =="
    # Instruction semantics must hold at both ends: -O0 exercises the raw
    # lowering, -O2 the optimizer. Some bugs only show up at one of them.
    for t in isa/arith isa/compare isa/unsigned isa/convert isa/memory isa/control isa/call; do
        run_one "$(basename "$t")" "$t.bx"
        OPT_LEVEL=-O0 run_one "$(basename "$t")_O0" "$t.bx"
    done
    # Inline asm is target-specific: run the host variant, compile-check the
    # other target, and require an unassemblable template to fail the build.
    case "$(uname -m)" in
        arm64|aarch64) asm_host=isa/asm_arm64; asm_other=isa/asm_x86_64; asm_other_cpu=x86_64 ;;
        *)             asm_host=isa/asm_x86_64; asm_other=isa/asm_arm64; asm_other_cpu=aarch64 ;;
    esac
    run_one "$(basename "$asm_host")" "$asm_host.bx"
    OPT_LEVEL=-O0 run_one "$(basename "$asm_host")_O0" "$asm_host.bx"
    compile asm_cross "$asm_other.bx" -c --cpu "$asm_other_cpu" --os Linux \
        && ok || bad asm_cross
    if $C1 isa/asm_invalid.bx -o "$OUT/asm_invalid" "${OPT_LEVEL:--O2}" \
            >"$OUT/asm_invalid.log" 2>&1; then
        echo "EXPECTED FAILURE: asm_invalid compiled"
        bad asm_invalid
    else
        ok
    fi
    run_one url_test        url_test.bx "$BOX/std/net/url.bx"
    run_one url_parse       url_parse_test.bx "$BOX/std/net/url.bx"
    run_one http_test       http_test.bx "$BOX/std/net/http/request.bx" "$BOX/std/net/http/response.bx" "$BOX/std/text/str.bx"
    run_one http_edge       http_edge_test.bx "$BOX/std/net/http/request.bx" "$BOX/std/net/http/response.bx" "$BOX/std/text/str.bx"
    run_one ws_frame        ws_frame_test.bx \
        "$BOX/std/net/http/websocket/frame.bx" \
        "$BOX/std/net/http/websocket/handshake.bx" \
        "$BOX/std/net/http/request.bx" "$BOX/std/net/http/response.bx" \
        "$BOX/std/text/str.bx" "$BOX/std/core/random.bx" \
        "$BOX/std/crypto/sha.bx" "$BOX/std/crypto/base64.bx" -L "$PLAT" -l platform
    run_one std_bounds      std_bounds_test.bx \
        "$BOX/std/mem/memops.bx" "$BOX/std/crypto/rsa.bx" "$BOX/std/crypto/asn1.bx" \
        -L "$PLAT" -l platform
    run_one ecc_scalar      ecc_scalar_test.bx \
        "$BOX/std/crypto/ecc.bx" "$BOX/std/crypto/rsa.bx" -L "$PLAT" -l platform
    run_hw_sw rsa_pss       rsa_pss_test.bx \
        "$BOX/std/crypto/rsa_pss.bx" "$BOX/std/crypto/rsa.bx" "$BOX/std/crypto/sha.bx" \
        -L "$PLAT" -l platform
    run_hw_sw tls13_kdf     tls13_kdf_test.bx \
        "$BOX/std/crypto/hkdf.bx" "$BOX/std/crypto/hmac.bx" \
        "$BOX/std/crypto/sha.bx" "$BOX/std/crypto/md5.bx" -L "$PLAT" -l platform
    run_one x509_algs       x509_algs_test.bx \
        "$BOX/std/crypto/x509.bx" "$BOX/std/crypto/asn1.bx" "$BOX/std/crypto/rsa.bx" \
        "$BOX/std/crypto/rsa_pss.bx" "$BOX/std/crypto/rsa_padding.bx" \
        "$BOX/std/crypto/sha.bx" "$BOX/std/crypto/p256.bx" "$BOX/std/io/file.bx" \
        -L "$PLAT" -l platform
    run_one zext_i64        test_zext_i64.bx
    run_one scalar          scalar_test.bx
    run_one str_call        str_call_test.bx
    run_one enum            enum_test.bx enum_dup.bx
    run_one sort            sort.bx
    run_one hashmap         hashmap.bx
    # The layout engine is checked without a window: the platform drawing
    # externs are stubbed inside the test.
    run_one ui_layout       ui_layout_test.bx "$BOX/std/ui/widget.bx"
    # Tensor kernels are generated in-process by MLIR, which every c1 has.
    run_one tensor          tensor_test.bx
    run_one vector          vector_test.bx "$BOX/std/data/vector.bx"
    run_one hashmap_std     hashmap_test.bx "$BOX/std/data/hashmap.bx"
    run_one hashset         hashset_test.bx "$BOX/std/data/hashmap.bx" "$BOX/std/data/hashset.bx"
    run_one json            json_test.bx "$BOX/std/json/json.bx"
    run_one base64          base64_test.bx "$BOX/std/crypto/base64.bx"
    run_one crypto_util     crypto_util_test.bx "$BOX/std/crypto/util.bx" -L "$PLAT" -l platform
    run_one pem_read        pem_read_test.bx \
        "$BOX/std/crypto/pem.bx" "$BOX/std/crypto/base64.bx" "$BOX/std/crypto/rsa.bx" \
        "$BOX/std/crypto/x509.bx" "$BOX/std/crypto/asn1.bx" "$BOX/std/crypto/rsa_padding.bx" \
        "$BOX/std/crypto/rsa_pss.bx" "$BOX/std/crypto/p256.bx" "$BOX/std/crypto/sha.bx" \
        "$BOX/std/io/file.bx" -L "$PLAT" -l platform
    run_one rsa_pss_sign    rsa_pss_sign_test.bx \
        "$BOX/std/crypto/rsa_pss.bx" "$BOX/std/crypto/pem.bx" \
        "$BOX/std/crypto/base64.bx" "$BOX/std/crypto/rsa.bx" \
        "$BOX/std/crypto/sha.bx" "$BOX/std/io/file.bx" -L "$PLAT" -l platform
    run_one oaep            oaep_test.bx \
        "$BOX/std/crypto/oaep.bx" "$BOX/std/crypto/rsa.bx" "$BOX/std/crypto/sha.bx" \
        "$BOX/std/crypto/pem.bx" "$BOX/std/crypto/base64.bx" "$BOX/std/io/file.bx" \
        -L "$PLAT" -l platform
    run_one hex             hex_test.bx "$BOX/std/text/hex.bx"
    run_one text            text_test.bx "$BOX/std/text/str.bx"
    run_one utf8            utf8_test.bx "$BOX/std/text/utf8.bx"
    run_one io              io_test.bx "$BOX/std/io/file.bx"
    run_one stdio           stdio_test.bx "$BOX/std/io/stdio.bx" -L "$PLAT" -l platform
    run_one process         process_test.bx "$BOX/std/process/process.bx"
    run_one argv            argv_test.bx
    run_one path            path_test.bx "$BOX/std/io/path.bx"
    run_one fs              fs_test.bx "$BOX/std/io/fs.bx" "$BOX/std/io/file.bx" -L "$PLAT" -l platform
    run_one time            time_test.bx "$BOX/std/time/time.bx" -L "$PLAT" -l platform
    run_one atomic          atomic_test.bx
    run_one thread          thread_test.bx \
        "$BOX/std/thread/thread.bx" "$BOX/std/thread/sync.bx" \
        -L "$PLAT" -l platform -l pthread
    run_one std_test        std_test.bx
}

group_crypto() {
    echo "== crypto tests (hardware vs software) =="
    run_hw_sw aes128         test_aes.bx     "$BOX/std/crypto/aes.bx" -L "$PLAT" -l platform
    run_hw_sw aes256         test_aes256.bx  "$BOX/std/crypto/aes.bx" -L "$PLAT" -l platform
    run_hw_sw aes_cbc        test_aes_cbc.bx "$BOX/std/crypto/aes.bx" -L "$PLAT" -l platform
    run_hw_sw gcm            test_gcm.bx     "$BOX/std/crypto/aes.bx" -L "$PLAT" -l platform
    run_hw_sw sha256         test_sha256.bx  "$BOX/std/crypto/sha.bx" -L "$PLAT" -l platform
    run_hw_sw sha256_msg     test_sha256_msg.bx "$BOX/std/crypto/sha.bx" -L "$PLAT" -l platform
    run_hw_sw sha512         test_sha512.bx  "$BOX/std/crypto/sha.bx" -L "$PLAT" -l platform
    run_hw_sw md5            run_md5_test.bx md5_test.bx "$BOX/std/crypto/md5.bx"
    run_hw_sw hmac           test_hmac.bx    "$BOX/std/crypto/hmac.bx" "$BOX/std/crypto/sha.bx" "$BOX/std/crypto/md5.bx" -L "$PLAT" -l platform
    run_hw_sw sha_all        test_sha_all.bx  "$BOX/std/crypto/sha.bx" -L "$PLAT" -l platform
    run_hw_sw hkdf           test_hkdf.bx    "$BOX/std/crypto/hkdf.bx" "$BOX/std/crypto/hmac.bx" "$BOX/std/crypto/sha.bx" "$BOX/std/crypto/md5.bx" -L "$PLAT" -l platform
    run_one x509_chain x509_chain_test.bx \
        "$BOX/std/crypto/x509.bx" "$BOX/std/crypto/asn1.bx" \
        "$BOX/std/crypto/rsa.bx" "$BOX/std/crypto/rsa_pss.bx" \
        "$BOX/std/crypto/rsa_padding.bx" "$BOX/std/crypto/sha.bx" \
        "$BOX/std/crypto/p256.bx" "$BOX/std/crypto/pem.bx" \
        "$BOX/std/crypto/base64.bx" "$BOX/std/io/file.bx" \
        "$BOX/std/time/time.bx" -L "$PLAT" -l platform
}

group_net() {
    echo "== net tests =="
    if ! compile tls_local tls_local_test.bx \
        "$BOX/std/net/tls/client_api.bx" "$BOX/std/net/tls/client12.bx" "$BOX/std/net/tls/base.bx" \
        "$BOX/std/net/tls/record.bx" "$BOX/std/net/socket.bx" \
        "$BOX/std/crypto/x509.bx" "$BOX/std/crypto/asn1.bx" "$BOX/std/crypto/sha.bx" \
        "$BOX/std/crypto/rsa_pss.bx" \
        "$BOX/std/crypto/hmac.bx" "$BOX/std/crypto/md5.bx" "$BOX/std/crypto/tls_prf.bx" \
        "$BOX/std/crypto/rsa.bx" "$BOX/std/crypto/rsa_padding.bx" "$BOX/std/crypto/aes.bx" \
        "$BOX/std/crypto/hkdf.bx" "$BOX/std/crypto/p256.bx" -L "$PLAT" -l platform; then
        bad "tls_local"
        return
    fi
    (python3 tls_server.py >"$OUT/tls_srv.log" 2>&1 &)
    sleep 1
    if "$OUT/tls_local" >"$OUT/tls_local.out" 2>&1; then ok; else
        sed -n '1,10p' "$OUT/tls_local.out"; bad "tls_local"
    fi
    pkill -f tls_server.py 2>/dev/null

    if ! compile tls_record tls_record_test.bx \
        "$BOX/std/net/tls/client_api.bx" "$BOX/std/net/tls/client12.bx" \
        "$BOX/std/net/tls/base.bx" "$BOX/std/net/tls/record.bx" \
        "$BOX/std/net/socket.bx" "$BOX/std/crypto/x509.bx" "$BOX/std/crypto/asn1.bx" \
        "$BOX/std/crypto/rsa_pss.bx" \
        "$BOX/std/crypto/sha.bx" "$BOX/std/crypto/hmac.bx" "$BOX/std/crypto/md5.bx" \
        "$BOX/std/crypto/tls_prf.bx" "$BOX/std/crypto/rsa.bx" "$BOX/std/crypto/rsa_padding.bx" \
        "$BOX/std/crypto/aes.bx" "$BOX/std/crypto/hkdf.bx" "$BOX/std/crypto/p256.bx" \
        -L "$PLAT" -l platform; then
        bad "tls_record"
        return
    fi
    if "$OUT/tls_record" >"$OUT/tls_record.out" 2>&1; then ok; else
        sed -n '1,10p' "$OUT/tls_record.out"; bad "tls_record"
    fi

    if ! compile tls13_record tls13_record_test.bx \
        "$BOX/std/net/tls/record.bx" "$BOX/std/crypto/aes.bx" \
        "$BOX/std/net/socket.bx" -L "$PLAT" -l platform; then
        bad "tls13_record"
        return
    fi
    if "$OUT/tls13_record" >"$OUT/tls13_record.out" 2>&1; then ok; else
        sed -n '1,10p' "$OUT/tls13_record.out"; bad "tls13_record"
    fi

    if ! compile tls13_record_vector tls13_record_vector_test.bx \
        "$BOX/std/net/tls/record.bx" "$BOX/std/crypto/aes.bx" \
        "$BOX/std/net/socket.bx" -L "$PLAT" -l platform; then
        bad "tls13_record_vector"
        return
    fi
    if "$OUT/tls13_record_vector" >"$OUT/tls13_record_vector.out" 2>&1; then ok; else
        sed -n '1,10p' "$OUT/tls13_record_vector.out"; bad "tls13_record_vector"
    fi

    if ! compile tls13_client tls13_client_test.bx \
        "$BOX/std/net/tls/client13.bx" "$BOX/std/net/tls/client_api.bx" \
        "$BOX/std/net/tls/client12.bx" "$BOX/std/net/tls/base.bx" \
        "$BOX/std/net/tls/record.bx" "$BOX/std/net/socket.bx" \
        "$BOX/std/crypto/x509.bx" "$BOX/std/crypto/asn1.bx" "$BOX/std/crypto/sha.bx" \
        "$BOX/std/crypto/rsa_pss.bx" "$BOX/std/crypto/base64.bx" \
        "$BOX/std/crypto/hmac.bx" "$BOX/std/crypto/md5.bx" "$BOX/std/crypto/tls_prf.bx" \
        "$BOX/std/crypto/rsa.bx" "$BOX/std/crypto/rsa_padding.bx" "$BOX/std/crypto/aes.bx" \
        "$BOX/std/crypto/hkdf.bx" "$BOX/std/crypto/p256.bx" -L "$PLAT" -l platform; then
        bad "tls13_client"
        return
    fi
    (python3 tls13_server.py 8801 1.3 >"$OUT/tls13_srv.log" 2>&1 &)
    sleep 1
    if "$OUT/tls13_client" >"$OUT/tls13_client.out" 2>&1; then ok; else
        sed -n '1,10p' "$OUT/tls13_client.out"; bad "tls13_client"
    fi
    pkill -f tls13_server.py 2>/dev/null

    if ! compile tls_trust tls_trust_test.bx \
        "$BOX/std/net/tls/client13.bx" "$BOX/std/net/tls/client_api.bx" \
        "$BOX/std/net/tls/client12.bx" "$BOX/std/net/tls/base.bx" \
        "$BOX/std/net/tls/record.bx" "$BOX/std/net/socket.bx" \
        "$BOX/std/crypto/x509.bx" "$BOX/std/crypto/asn1.bx" "$BOX/std/crypto/sha.bx" \
        "$BOX/std/crypto/rsa_pss.bx" "$BOX/std/crypto/base64.bx" \
        "$BOX/std/crypto/hmac.bx" "$BOX/std/crypto/md5.bx" "$BOX/std/crypto/tls_prf.bx" \
        "$BOX/std/crypto/rsa.bx" "$BOX/std/crypto/rsa_padding.bx" "$BOX/std/crypto/aes.bx" \
        "$BOX/std/crypto/hkdf.bx" "$BOX/std/crypto/p256.bx" "$BOX/std/crypto/pem.bx" \
        "$BOX/std/io/file.bx" -L "$PLAT" -l platform; then
        bad "tls_trust"
        return
    fi
    (python3 tls13_server.py 8803 1.3 >"$OUT/tls_trust_srv.log" 2>&1 &)
    (python3 tls13_server.py 8802 1.2 >>"$OUT/tls_trust_srv.log" 2>&1 &)
    (python3 tls13_server.py 8804 1.3 certs/chain_bundle.pem certs/chain_leaf.key \
        >>"$OUT/tls_trust_srv.log" 2>&1 &)
    (python3 tls13_server.py 8805 1.3 certs/chain_leaf.pem certs/chain_leaf.key \
        >>"$OUT/tls_trust_srv.log" 2>&1 &)
    sleep 1
    if "$OUT/tls_trust" >"$OUT/tls_trust.out" 2>&1; then ok; else
        sed -n '1,10p' "$OUT/tls_trust.out"; bad "tls_trust"
    fi
    pkill -f tls13_server.py 2>/dev/null

    if ! compile https13 https13_test.bx \
        "$BOX/std/net/http/client.bx" "$BOX/std/net/http/request.bx" \
        "$BOX/std/net/http/response.bx" \
        "$BOX/std/net/tls/client13.bx" "$BOX/std/net/tls/client_api.bx" \
        "$BOX/std/net/tls/client12.bx" "$BOX/std/net/tls/base.bx" \
        "$BOX/std/net/tls/record.bx" "$BOX/std/net/socket.bx" \
        "$BOX/std/text/str.bx" "$BOX/std/core/random.bx" \
        "$BOX/std/crypto/x509.bx" "$BOX/std/crypto/asn1.bx" "$BOX/std/crypto/sha.bx" \
        "$BOX/std/crypto/rsa_pss.bx" "$BOX/std/crypto/hmac.bx" "$BOX/std/crypto/md5.bx" \
        "$BOX/std/crypto/tls_prf.bx" "$BOX/std/crypto/rsa.bx" \
        "$BOX/std/crypto/rsa_padding.bx" "$BOX/std/crypto/aes.bx" \
        "$BOX/std/crypto/hkdf.bx" "$BOX/std/crypto/p256.bx" -L "$PLAT" -l platform; then
        bad "https13"
        return
    fi
    (python3 tls13_server.py 8801 1.3 >"$OUT/https13_srv.log" 2>&1 &)
    (python3 tls13_server.py 8802 1.2 >>"$OUT/https13_srv.log" 2>&1 &)
    sleep 1
    if "$OUT/https13" >"$OUT/https13.out" 2>&1; then ok; else
        sed -n '1,10p' "$OUT/https13.out"; bad "https13"
    fi
    pkill -f tls13_server.py 2>/dev/null

    if ! compile ws_echo ws_echo_test.bx \
        "$BOX/std/net/http/websocket/conn.bx" "$BOX/std/net/http/websocket/handshake.bx" \
        "$BOX/std/net/http/websocket/frame.bx" \
        "$BOX/std/net/http/client.bx" "$BOX/std/net/http/request.bx" "$BOX/std/net/http/response.bx" \
        "$BOX/std/net/socket.bx" "$BOX/std/net/tls/client13.bx" \
        "$BOX/std/net/tls/client_api.bx" "$BOX/std/net/tls/client12.bx" \
        "$BOX/std/net/tls/base.bx" "$BOX/std/net/tls/record.bx" \
        "$BOX/std/text/str.bx" "$BOX/std/core/random.bx" \
        "$BOX/std/crypto/x509.bx" "$BOX/std/crypto/asn1.bx" "$BOX/std/crypto/sha.bx" \
        "$BOX/std/crypto/rsa_pss.bx" \
        "$BOX/std/crypto/base64.bx" "$BOX/std/crypto/hmac.bx" "$BOX/std/crypto/md5.bx" \
        "$BOX/std/crypto/tls_prf.bx" "$BOX/std/crypto/rsa.bx" "$BOX/std/crypto/rsa_padding.bx" \
        "$BOX/std/crypto/aes.bx" "$BOX/std/crypto/hkdf.bx" "$BOX/std/crypto/p256.bx" \
        -L "$PLAT" -l platform; then
        bad "ws_echo"
        return
    fi
    (python3 ws_server.py >"$OUT/ws_srv.log" 2>&1 &)
    sleep 1
    if "$OUT/ws_echo" >"$OUT/ws_echo.out" 2>&1; then ok; else
        sed -n '1,10p' "$OUT/ws_echo.out"; bad "ws_echo"
    fi
    pkill -f ws_server.py 2>/dev/null

    # TLS 1.2 server: the .bx server listens, a Python/OpenSSL client connects.
    if ! compile tls_server tls_server_test.bx \
        "$BOX/std/net/tls/server_api.bx" "$BOX/std/net/tls/server12.bx" \
        "$BOX/std/net/tls/handshake.bx" "$BOX/std/net/tls/base.bx" \
        "$BOX/std/net/tls/record.bx" "$BOX/std/net/socket.bx" \
        "$BOX/std/io/file.bx" "$BOX/std/crypto/rsa.bx" \
        "$BOX/std/crypto/sha.bx" "$BOX/std/crypto/hmac.bx" \
        "$BOX/std/crypto/md5.bx" "$BOX/std/crypto/tls_prf.bx" \
        "$BOX/std/crypto/aes.bx" -L "$PLAT" -l platform; then
        bad "tls_server"
        return
    fi
    "$OUT/tls_server" >"$OUT/tls_server.out" 2>&1 &
    TLS_SRV_PID=$!
    sleep 1
    if python3 tls_server_client.py >"$OUT/tls_server_client.out" 2>&1; then ok; else
        sed -n '1,10p' "$OUT/tls_server_client.out"
        sed -n '1,5p' "$OUT/tls_server.out"
        bad "tls_server"
    fi
    kill "$TLS_SRV_PID" 2>/dev/null
}

group_online() {
    echo "== online tests =="
    run_one https_example https_example_test.bx \
        "$BOX/std/net/tls/client13.bx" \
        "$BOX/std/net/tls/client_api.bx" "$BOX/std/net/tls/client12.bx" "$BOX/std/net/tls/base.bx" \
        "$BOX/std/net/tls/record.bx" "$BOX/std/net/socket.bx" \
        "$BOX/std/net/http/client.bx" "$BOX/std/net/http/request.bx" "$BOX/std/net/http/response.bx" \
        "$BOX/std/text/str.bx" \
        "$BOX/std/crypto/x509.bx" "$BOX/std/crypto/asn1.bx" "$BOX/std/crypto/sha.bx" \
        "$BOX/std/crypto/rsa_pss.bx" \
        "$BOX/std/crypto/hmac.bx" "$BOX/std/crypto/md5.bx" "$BOX/std/crypto/tls_prf.bx" \
        "$BOX/std/crypto/rsa.bx" "$BOX/std/crypto/rsa_padding.bx" "$BOX/std/crypto/aes.bx" \
        "$BOX/std/crypto/hkdf.bx" "$BOX/std/crypto/p256.bx" -L "$PLAT" -l platform
}

WHICH=${1:-default}
case "$WHICH" in
    unit)    group_unit ;;
    fast)    group_fast ;;
    crypto)  group_crypto ;;
    net)     group_net ;;
    online)  group_online ;;
    all)     group_unit; group_fast; group_crypto; group_net; group_online ;;
    default) group_unit; group_fast; group_crypto ;;
    *) echo "usage: $0 [unit|fast|crypto|net|online|all]"; exit 2 ;;
esac

echo
echo "passed: $PASSED  failed: $FAILED"
[ "$FAILED" -eq 0 ] || { echo "failed tests:$FAIL_NAMES"; exit 1; }
exit 0
