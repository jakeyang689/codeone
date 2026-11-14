/* OpenSSL AES/RSA Performance Benchmark */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/bn.h>
#include <openssl/rand.h>

static double nsec_diff(struct timespec end, struct timespec start) {
    double sec = (double)(end.tv_sec - start.tv_sec);
    double ns = (double)(end.tv_nsec - start.tv_nsec);
    return sec * 1e9 + ns;
}

static void bench_aes128_gcm_encrypt(void) {
    unsigned char key[16], iv[12], aad[13], pt[65536], ct[65536], tag[16];
    int outlen, len;
    EVP_CIPHER_CTX *ctx;
    int i;
    struct timespec ts, te;

    RAND_bytes(key, 16);
    RAND_bytes(iv, 12);
    RAND_bytes(aad, 13);
    RAND_bytes(pt, 65536);

    ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL);
    EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv);
    EVP_EncryptUpdate(ctx, NULL, &len, aad, 13);
    EVP_EncryptUpdate(ctx, ct, &len, pt, 65536);
    EVP_EncryptFinal_ex(ctx, ct + len, &len);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);
    EVP_CIPHER_CTX_free(ctx);

    const int N = 100;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    for (i = 0; i < N; i++) {
        ctx = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL);
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL);
        EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv);
        EVP_EncryptUpdate(ctx, NULL, &len, aad, 13);
        EVP_EncryptUpdate(ctx, ct, &len, pt, 65536);
        EVP_EncryptFinal_ex(ctx, ct + len, &len);
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);
        EVP_CIPHER_CTX_free(ctx);
    }
    clock_gettime(CLOCK_MONOTONIC, &te);

    double elapsed = nsec_diff(te, ts);
    printf("OpenSSL AES-128-GCM Encrypt: %d x 64KB = %.0f ns, %.1f MB/s\n",
           N, elapsed, (N * 64.0) / (elapsed / 1e9));
}

static void bench_aes128_gcm_decrypt(void) {
    unsigned char key[16], iv[12], aad[13], pt[65536], ct[65536], tag[16], out[65536];
    int outlen, len;
    EVP_CIPHER_CTX *ctx;
    int i;
    struct timespec ts, te;

    RAND_bytes(key, 16);
    RAND_bytes(iv, 12);
    RAND_bytes(aad, 13);
    RAND_bytes(pt, 65536);

    ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL);
    EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv);
    EVP_EncryptUpdate(ctx, NULL, &len, aad, 13);
    EVP_EncryptUpdate(ctx, ct, &len, pt, 65536);
    EVP_EncryptFinal_ex(ctx, ct + len, &len);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);
    EVP_CIPHER_CTX_free(ctx);

    const int N = 100;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    for (i = 0; i < N; i++) {
        ctx = EVP_CIPHER_CTX_new();
        EVP_DecryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL);
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL);
        EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv);
        EVP_DecryptUpdate(ctx, NULL, &len, aad, 13);
        EVP_DecryptUpdate(ctx, out, &len, ct, 65536);
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag);
        EVP_DecryptFinal_ex(ctx, out + len, &len);
        EVP_CIPHER_CTX_free(ctx);
    }
    clock_gettime(CLOCK_MONOTONIC, &te);

    double elapsed = nsec_diff(te, ts);
    printf("OpenSSL AES-128-GCM Decrypt: %d x 64KB = %.0f ns, %.1f MB/s\n",
           N, elapsed, (N * 64.0) / (elapsed / 1e9));
}

static void bench_rsa_2048(void) {
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    unsigned char pt[32], ct[256], out[256];
    size_t ct_len, out_len;
    int i;
    struct timespec ts, te;
    RSA *rsa;
    const BIGNUM *n, *e, *d;
    BN_CTX *bn_ctx;
    BIGNUM *base, *result;

    unsigned char msg[] = "RSA benchmark message!";

    printf("Generating RSA-2048 keypair (one-time)...");
    fflush(stdout);
    pkey = EVP_PKEY_Q_keygen(NULL, NULL, "RSA", (size_t)2048);
    if (!pkey) {
        fprintf(stderr, "RSA keygen failed\n");
        return;
    }
    printf(" done\n");

    rsa = EVP_PKEY_get1_RSA(pkey);
    RSA_get0_key(rsa, &n, &e, &d);

    /* --- PKCS1v15 Encrypt --- */
    ct_len = sizeof(ct);
    const int N_ENC = 50;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    for (i = 0; i < N_ENC; i++) {
        ctx = EVP_PKEY_CTX_new(pkey, NULL);
        EVP_PKEY_encrypt_init(ctx);
        EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING);
        ct_len = sizeof(ct);
        EVP_PKEY_encrypt(ctx, ct, &ct_len, msg, sizeof(msg));
        EVP_PKEY_CTX_free(ctx);
    }
    clock_gettime(CLOCK_MONOTONIC, &te);
    double elapsed = nsec_diff(te, ts);
    printf("OpenSSL RSA-2048 PKCS1v15 Encrypt: %d ops = %.0f ns, %.0f ns/op\n",
           N_ENC, elapsed, elapsed / N_ENC);

    /* Pre-encrypt */
    ctx = EVP_PKEY_CTX_new(pkey, NULL);
    EVP_PKEY_encrypt_init(ctx);
    EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING);
    ct_len = sizeof(ct);
    EVP_PKEY_encrypt(ctx, ct, &ct_len, msg, sizeof(msg));
    EVP_PKEY_CTX_free(ctx);

    /* --- PKCS1v15 Decrypt --- */
    const int N_DEC = 50;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    for (i = 0; i < N_DEC; i++) {
        ctx = EVP_PKEY_CTX_new(pkey, NULL);
        EVP_PKEY_decrypt_init(ctx);
        EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING);
        out_len = sizeof(out);
        EVP_PKEY_decrypt(ctx, out, &out_len, ct, ct_len);
        EVP_PKEY_CTX_free(ctx);
    }
    clock_gettime(CLOCK_MONOTONIC, &te);
    elapsed = nsec_diff(te, ts);
    printf("OpenSSL RSA-2048 PKCS1v15 Decrypt: %d ops = %.0f ns, %.0f ns/op\n",
           N_DEC, elapsed, elapsed / N_DEC);

    /* --- Raw BN_mod_exp (e=65537) --- */
    bn_ctx = BN_CTX_new();
    base = BN_new();
    result = BN_new();
    BN_rand(base, 2048, -1, 0);
    const int N_EXP = 20;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    for (i = 0; i < N_EXP; i++) {
        BN_mod_exp(result, base, e, n, bn_ctx);
    }
    clock_gettime(CLOCK_MONOTONIC, &te);
    elapsed = nsec_diff(te, ts);
    printf("OpenSSL RSA-2048 mod_pow (e=65537): %d ops = %.0f ns, %.0f ns/op\n",
           N_EXP, elapsed, elapsed / N_EXP);

    /* --- Raw BN_mod_exp (full exponent) --- */
    clock_gettime(CLOCK_MONOTONIC, &ts);
    for (i = 0; i < 5; i++) {
        BN_mod_exp(result, base, d, n, bn_ctx);
    }
    clock_gettime(CLOCK_MONOTONIC, &te);
    elapsed = nsec_diff(te, ts);
    printf("OpenSSL RSA-2048 mod_pow (full exp): 5 ops = %.0f ns, %.0f ns/op\n",
           5, elapsed, elapsed / 5);

    BN_free(base);
    BN_free(result);
    BN_CTX_free(bn_ctx);
    RSA_free(rsa);
    EVP_PKEY_free(pkey);
}

int main(void) {
    printf("=== OpenSSL AES-128-GCM Performance ===\n");
    bench_aes128_gcm_encrypt();
    bench_aes128_gcm_decrypt();

    printf("\n=== OpenSSL RSA-2048 Performance ===\n");
    bench_rsa_2048();

    return 0;
}
