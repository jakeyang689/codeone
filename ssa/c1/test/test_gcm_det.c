// Test GCM determinism
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern int std_crypto_aes_gcm_encrypt(
    uint8_t *pt, int pt_len, uint8_t *aad, int aad_len,
    uint8_t *key, uint8_t *iv, uint8_t *ct, uint8_t *tag);

extern int std_crypto_aes256_gcm_encrypt(
    uint8_t *pt, int pt_len, uint8_t *aad, int aad_len,
    uint8_t *key, uint8_t *iv, uint8_t *ct, uint8_t *tag);

extern int std_crypto_aes256_gcm_decrypt(
    uint8_t *ct, int ct_len, uint8_t *aad, int aad_len,
    uint8_t *key, uint8_t *iv, uint8_t *tag, uint8_t *pt);

void print_hex(const char *label, uint8_t *data, int len) {
    printf("%s: ", label);
    for (int i = 0; i < len; i++) printf("%02x", data[i]);
    printf("\n");
}

int main() {
    uint8_t key128[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    uint8_t key256[32] = {
        0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
        0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
        0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7,
        0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4
    };
    uint8_t iv[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
    uint8_t pt[64];
    uint8_t ct1[64], tag1[16], ct2[64], tag2[16], pt2[64];

    for (int i = 0; i < 64; i++) pt[i] = i;
    int ok = 1;

    // AES-128-GCM
    std_crypto_aes_gcm_encrypt(pt, 64, pt, 0, key128, iv, ct1, tag1);
    std_crypto_aes_gcm_encrypt(pt, 64, pt, 0, key128, iv, ct2, tag2);
    if (memcmp(ct1, ct2, 64) != 0 || memcmp(tag1, tag2, 16) != 0) {
        printf("FAIL: AES-128-GCM non-deterministic!\n");
        print_hex("CT1", ct1, 64); print_hex("CT2", ct2, 64);
        ok = 0;
    } else printf("PASS: AES-128-GCM deterministic\n");

    // AES-256-GCM
    std_crypto_aes256_gcm_encrypt(pt, 64, pt, 0, key256, iv, ct1, tag1);
    std_crypto_aes256_gcm_encrypt(pt, 64, pt, 0, key256, iv, ct2, tag2);
    if (memcmp(ct1, ct2, 64) != 0 || memcmp(tag1, tag2, 16) != 0) {
        printf("FAIL: AES-256-GCM non-deterministic!\n");
        print_hex("CT1", ct1, 64); print_hex("CT2", ct2, 64);
        ok = 0;
    } else printf("PASS: AES-256-GCM deterministic\n");

    // AES-256-GCM decrypt round-trip
    std_crypto_aes256_gcm_encrypt(pt, 64, pt, 0, key256, iv, ct1, tag1);
    int r = std_crypto_aes256_gcm_decrypt(ct1, 64, pt, 0, key256, iv, tag1, pt2);
    if (r != 0 || memcmp(pt, pt2, 64) != 0) {
        printf("FAIL: AES-256-GCM round-trip (ret=%d)\n", r);
        ok = 0;
    } else printf("PASS: AES-256-GCM round-trip\n");

    return !ok;
}
