#ifndef CRYPTO_HW_H
#define CRYPTO_HW_H

#include <stdint.h>

/* Hardware-accelerated primitives called directly from .bx code.
   Every entry point assumes the caller has checked the corresponding bit in
   c1_cpu_features(); nothing here re-checks at runtime. */

void c1_aes128_encrypt_block(const void *in, const void *key, void *out);
void c1_aes128_decrypt_block(const void *in, const void *key, void *out);
void c1_aes256_encrypt_block(const void *in, const void *key, void *out);
void c1_aes256_decrypt_block(const void *in, const void *key, void *out);

void c1_aes128_cbc_encrypt(const void *in, int32_t len, const void *key,
                           const void *iv, void *out);
void c1_aes128_cbc_decrypt(const void *in, int32_t len, const void *key,
                           const void *iv, void *out);
void c1_aes256_cbc_encrypt(const void *in, int32_t len, const void *key,
                           const void *iv, void *out);
void c1_aes256_cbc_decrypt(const void *in, int32_t len, const void *key,
                           const void *iv, void *out);

/* Returns 0 on success, -1 if the tag mismatches. */
int32_t c1_aes128_gcm_encrypt(const void *pt, int32_t pt_len,
                              const void *aad, int32_t aad_len,
                              const void *key, const void *iv,
                              void *ct, void *tag);
int32_t c1_aes128_gcm_decrypt(const void *ct, int32_t ct_len,
                              const void *aad, int32_t aad_len,
                              const void *key, const void *iv,
                              const void *tag, void *pt);
int32_t c1_aes256_gcm_encrypt(const void *pt, int32_t pt_len,
                              const void *aad, int32_t aad_len,
                              const void *key, const void *iv,
                              void *ct, void *tag);
int32_t c1_aes256_gcm_decrypt(const void *ct, int32_t ct_len,
                              const void *aad, int32_t aad_len,
                              const void *key, const void *iv,
                              const void *tag, void *pt);

/* Updates eight uint32 state words in place from one 64-byte block.
   Pointers follow the .bx transform signature (a..h). */
void c1_sha256_transform(void *a, void *b, void *c, void *d,
                         void *e, void *f, void *g, void *h,
                         const void *block);

#endif /* CRYPTO_HW_H */
