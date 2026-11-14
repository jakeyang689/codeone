/* Hardware-accelerated AES and SHA-256 for the codeone std library.
 *
 * AES uses the ARMv8 Crypto extensions or AES-NI; SHA-256 uses the ARMv8
 * SHA-2 instructions or x86 SHA-NI. Key scheduling and GHASH stay portable C.
 * Each hardware entry point carries a target attribute so the rest of the
 * object keeps the baseline ISA; callers select a path at runtime via
 * c1_cpu_features(). */

#include "crypto_hw.h"
#include "cpu.h"

#include <stddef.h>
#include <string.h>

#if defined(__aarch64__) || defined(__x86_64__)
  #define C1_HW_BACKEND 1
#else
  #define C1_HW_BACKEND 0
#endif

/* ==================================================================
 * Shared software helpers
 * ================================================================== */

static const uint8_t SBOX[256] = {
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};

static uint8_t xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0));
}

static void expand_key(const uint8_t *key, int nk, uint8_t *rk) {
    int nr = nk + 6;
    int words = 4 * (nr + 1);
    uint8_t rcon = 1;
    memcpy(rk, key, (size_t)nk * 4);
    for (int i = nk; i < words; i++) {
        uint8_t t[4];
        memcpy(t, rk + (i - 1) * 4, 4);
        if (i % nk == 0) {
            uint8_t a = t[0];
            t[0] = SBOX[t[1]] ^ rcon;
            t[1] = SBOX[t[2]];
            t[2] = SBOX[t[3]];
            t[3] = SBOX[a];
            rcon = xtime(rcon);
        } else if (nk > 6 && i % nk == 4) {
            for (int j = 0; j < 4; j++) t[j] = SBOX[t[j]];
        }
        for (int j = 0; j < 4; j++)
            rk[i * 4 + j] = rk[(i - nk) * 4 + j] ^ t[j];
    }
}

/* ==================================================================
 * AES core
 * ================================================================== */

#if C1_HW_BACKEND

#if defined(__aarch64__)

  #include <arm_neon.h>

  #if defined(__clang__)
    #define C1_AES_ATTR __attribute__((target("crypto")))
    #define C1_SHA_ATTR __attribute__((target("sha2")))
  #elif defined(__GNUC__) && __GNUC__ >= 10
    #define C1_AES_ATTR __attribute__((target("+crypto")))
    #define C1_SHA_ATTR __attribute__((target("+sha2")))
  #else
    #define C1_AES_ATTR
    #define C1_SHA_ATTR
  #endif

/* AESE is AddRoundKey -> SubBytes -> ShiftRows, so a standard AES round
   takes aese(zero) + aesmc + xor round key. Verified against FIPS-197. */
C1_AES_ATTR
static void aes_encrypt_v(uint8_t out[16], const uint8_t in[16],
                          const uint8_t *rk, int nr) {
    uint8x16_t z = vdupq_n_u8(0);
    uint8x16_t s = veorq_u8(vld1q_u8(in), vld1q_u8(rk));
    for (int r = 1; r < nr; r++)
        s = veorq_u8(vaesmcq_u8(vaeseq_u8(s, z)), vld1q_u8(rk + 16 * r));
    s = veorq_u8(vaeseq_u8(s, z), vld1q_u8(rk + 16 * nr));
    vst1q_u8(out, s);
}

/* FIPS InvCipher applies AddRoundKey before InvMixColumns, hence the xor
   inside the aesimc. */
C1_AES_ATTR
static void aes_decrypt_v(uint8_t out[16], const uint8_t in[16],
                          const uint8_t *rk, int nr) {
    uint8x16_t z = vdupq_n_u8(0);
    uint8x16_t s = veorq_u8(vld1q_u8(in), vld1q_u8(rk + 16 * nr));
    for (int r = nr - 1; r >= 1; r--)
        s = vaesimcq_u8(veorq_u8(vaesdq_u8(s, z), vld1q_u8(rk + 16 * r)));
    s = veorq_u8(vaesdq_u8(s, z), vld1q_u8(rk));
    vst1q_u8(out, s);
}

#define SHA2_ROUNDS(g) do {                       \
        uint32x4_t wk_ = vld1q_u32(w + 4 * (g));  \
        uint32x4_t na_ = vsha256hq_u32(abcd, efgh, wk_);  \
        uint32x4_t ne_ = vsha256h2q_u32(efgh, abcd, wk_); \
        abcd = na_; efgh = ne_;                   \
    } while (0)

C1_SHA_ATTR
static void sha256_rounds(uint32_t *st, const uint32_t *w) {
    uint32x4_t abcd = vld1q_u32(st);
    uint32x4_t efgh = vld1q_u32(st + 4);
    SHA2_ROUNDS(0);
    SHA2_ROUNDS(1);
    SHA2_ROUNDS(2);
    SHA2_ROUNDS(3);
    SHA2_ROUNDS(4);
    SHA2_ROUNDS(5);
    SHA2_ROUNDS(6);
    SHA2_ROUNDS(7);
    SHA2_ROUNDS(8);
    SHA2_ROUNDS(9);
    SHA2_ROUNDS(10);
    SHA2_ROUNDS(11);
    SHA2_ROUNDS(12);
    SHA2_ROUNDS(13);
    SHA2_ROUNDS(14);
    SHA2_ROUNDS(15);
    vst1q_u32(st, abcd);
    vst1q_u32(st + 4, efgh);
}
#undef SHA2_ROUNDS

#else /* __x86_64__ */

  #include <immintrin.h>
  #define C1_AES_ATTR __attribute__((target("aes,pclmul")))
  #define C1_SHA_ATTR __attribute__((target("sha")))

C1_AES_ATTR
static void aes_encrypt_v(uint8_t out[16], const uint8_t in[16],
                          const uint8_t *rk, int nr) {
    __m128i s = _mm_xor_si128(_mm_loadu_si128((const __m128i *)in),
                              _mm_loadu_si128((const __m128i *)rk));
    for (int r = 1; r < nr; r++)
        s = _mm_aesenc_si128(s, _mm_loadu_si128((const __m128i *)(rk + 16 * r)));
    s = _mm_aesenclast_si128(s, _mm_loadu_si128((const __m128i *)(rk + 16 * nr)));
    _mm_storeu_si128((__m128i *)out, s);
}

C1_AES_ATTR
static void aes_decrypt_v(uint8_t out[16], const uint8_t in[16],
                          const uint8_t *rk, int nr) {
    __m128i s = _mm_xor_si128(_mm_loadu_si128((const __m128i *)in),
                              _mm_loadu_si128((const __m128i *)(rk + 16 * nr)));
    for (int r = nr - 1; r >= 1; r--)
        s = _mm_aesdec_si128(s, _mm_aesimc_si128(
            _mm_loadu_si128((const __m128i *)(rk + 16 * r))));
    s = _mm_aesdeclast_si128(s, _mm_loadu_si128((const __m128i *)rk));
    _mm_storeu_si128((__m128i *)out, s);
}

#define SHA2_ROUNDS(g) do {                                         \
        __m128i wk_ = _mm_loadu_si128((const __m128i *)(w + 4 * (g))); \
        __m128i r1_ = _mm_sha256rnds2_epu32(abef, cdgh, wk_);        \
        __m128i hi_ = _mm_shuffle_epi32(wk_, 0x0E);                  \
        __m128i r2_ = _mm_sha256rnds2_epu32(cdgh, r1_, hi_);         \
        abef = r1_; cdgh = r2_;                                     \
    } while (0)

C1_SHA_ATTR
static void sha256_rounds(uint32_t *st, const uint32_t *w) {
    __m128i abcd = _mm_loadu_si128((const __m128i *)st);
    __m128i efgh = _mm_loadu_si128((const __m128i *)(st + 4));
    /* SHA-NI works on ABEF / CDGH. */
    __m128i abef = _mm_unpacklo_epi64(abcd, efgh);
    __m128i cdgh = _mm_unpackhi_epi64(abcd, efgh);
    SHA2_ROUNDS(0);
    SHA2_ROUNDS(1);
    SHA2_ROUNDS(2);
    SHA2_ROUNDS(3);
    SHA2_ROUNDS(4);
    SHA2_ROUNDS(5);
    SHA2_ROUNDS(6);
    SHA2_ROUNDS(7);
    SHA2_ROUNDS(8);
    SHA2_ROUNDS(9);
    SHA2_ROUNDS(10);
    SHA2_ROUNDS(11);
    SHA2_ROUNDS(12);
    SHA2_ROUNDS(13);
    SHA2_ROUNDS(14);
    SHA2_ROUNDS(15);
    _mm_storeu_si128((__m128i *)st, _mm_unpacklo_epi64(abef, cdgh));
    _mm_storeu_si128((__m128i *)(st + 4), _mm_unpackhi_epi64(abef, cdgh));
}
#undef SHA2_ROUNDS

#endif

#endif /* C1_HW_BACKEND */

/* GCM numbers a block from its first (most significant) byte, while the
   integer arithmetic below is LSB-first, so operands pass through a 128-bit
   bit reversal. The carry-less multiply itself is three 64x64 products
   (Karatsuba) plus a two-step reduction modulo x^128 + x^7 + x^2 + x + 1. */

typedef unsigned __int128 c1_u128;

static uint64_t bitrev64(uint64_t v) {
#if defined(__clang__)
    return __builtin_bitreverse64(v);   /* single rbit on AArch64 */
#else
    v = ((v >> 1) & 0x5555555555555555ULL) | ((v & 0x5555555555555555ULL) << 1);
    v = ((v >> 2) & 0x3333333333333333ULL) | ((v & 0x3333333333333333ULL) << 2);
    v = ((v >> 4) & 0x0f0f0f0f0f0f0f0fULL) | ((v & 0x0f0f0f0f0f0f0f0fULL) << 4);
    return __builtin_bswap64(v);
#endif
}

static c1_u128 bitrev128(c1_u128 v) {
    uint64_t lo = (uint64_t)v, hi = (uint64_t)(v >> 64);
    return ((c1_u128)bitrev64(lo) << 64) | bitrev64(hi);
}

static c1_u128 be_load128(const uint8_t *b) {
    uint64_t hi, lo;
    memcpy(&lo, b + 8, 8);
    memcpy(&hi, b, 8);
    return ((c1_u128)__builtin_bswap64(hi) << 64) | __builtin_bswap64(lo);
}

static void be_store128(uint8_t *b, c1_u128 v) {
    uint64_t hi = (uint64_t)(v >> 64), lo = (uint64_t)v;
    hi = __builtin_bswap64(hi);
    lo = __builtin_bswap64(lo);
    memcpy(b, &hi, 8);
    memcpy(b + 8, &lo, 8);
}

#if C1_HW_BACKEND

C1_AES_ATTR
static c1_u128 clmul64(uint64_t a, uint64_t b) {
    c1_u128 z;
#if defined(__aarch64__)
    uint8x16_t r = vreinterpretq_u8_p128(vmull_p64((poly64_t)a, (poly64_t)b));
    memcpy(&z, &r, 16);
#else
    __m128i x = _mm_set_epi64x(0, (long long)a);
    __m128i y = _mm_set_epi64x(0, (long long)b);
    __m128i r = _mm_clmulepi64_si128(x, y, 0x00);
    _mm_storeu_si128((__m128i *)&z, r);
#endif
    return z;
}

/* a*b in GF(2^128), both already bit-reversed. */
C1_AES_ATTR
static c1_u128 gf_mul_rev(c1_u128 a, c1_u128 b) {
    uint64_t a0 = (uint64_t)a, a1 = (uint64_t)(a >> 64);
    uint64_t b0 = (uint64_t)b, b1 = (uint64_t)(b >> 64);
    c1_u128 z0 = clmul64(a0, b0);
    c1_u128 z1 = clmul64(a1, b1);
    c1_u128 z2 = clmul64(a0 ^ a1, b0 ^ b1);
    c1_u128 cross = z0 ^ z1 ^ z2;
    c1_u128 lo = z0 ^ (cross << 64);
    c1_u128 hi = z1 ^ (cross >> 64);
    c1_u128 t = hi ^ (hi << 1) ^ (hi << 2) ^ (hi << 7);
    c1_u128 ov = (hi >> 127) ^ (hi >> 126) ^ (hi >> 121);
    t ^= ov ^ (ov << 1) ^ (ov << 2) ^ (ov << 7);
    return lo ^ t;
}

/* Load a block already bit-reversed (the representation gf_mul_rev wants). */
C1_AES_ATTR
static c1_u128 load_rev(const uint8_t *b) {
    return bitrev128(be_load128(b));
}

/* Y = (Y ^ block) * H, chained over the input. Four independent multiplies
   are issued per iteration (Y*H^4 ^ X0*H^4 ^ X1*H^3 ^ X2*H^2 ^ X3*H) so the
   carry-less multiply latency overlaps instead of serialising. */
C1_AES_ATTR
static void ghash_update(uint8_t *Y, const uint8_t *H,
                         const uint8_t *data, int32_t len) {
    c1_u128 y = load_rev(Y);
    c1_u128 h = load_rev(H);
    c1_u128 h2 = gf_mul_rev(h, h);
    c1_u128 h3 = gf_mul_rev(h2, h);
    c1_u128 h4 = gf_mul_rev(h3, h);
    int32_t i = 0;

    while (i + 64 <= len) {
        c1_u128 x0 = load_rev(data + i);
        c1_u128 x1 = load_rev(data + i + 16);
        c1_u128 x2 = load_rev(data + i + 32);
        c1_u128 x3 = load_rev(data + i + 48);
        c1_u128 t0 = gf_mul_rev(y ^ x0, h4);
        c1_u128 t1 = gf_mul_rev(x1, h3);
        c1_u128 t2 = gf_mul_rev(x2, h2);
        c1_u128 t3 = gf_mul_rev(x3, h);
        y = t0 ^ t1 ^ t2 ^ t3;
        i += 64;
    }
    while (i + 16 <= len) {
        y = gf_mul_rev(y ^ load_rev(data + i), h);
        i += 16;
    }
    if (i < len) {
        uint8_t blk[16] = {0};
        for (int j = 0; i + j < len; j++) blk[j] = data[i + j];
        y = gf_mul_rev(y ^ load_rev(blk), h);
    }
    be_store128(Y, bitrev128(y));
}

#else /* software fallback */

static void gf_mul_sw(uint8_t *x, const uint8_t *y) {
    uint8_t z[16] = {0};
    uint8_t v[16];
    memcpy(v, y, 16);
    for (int i = 0; i < 128; i++) {
        if ((x[i >> 3] >> (7 - (i & 7))) & 1) {
            for (int j = 0; j < 16; j++) z[j] ^= v[j];
        }
        int lsb = v[15] & 1;
        for (int j = 15; j > 0; j--)
            v[j] = (uint8_t)((v[j] >> 1) | (v[j - 1] << 7));
        v[0] >>= 1;
        if (lsb) v[0] ^= 0xe1;
    }
    memcpy(x, z, 16);
}

C1_AES_ATTR
static void ghash_update(uint8_t *Y, const uint8_t *H,
                         const uint8_t *data, int32_t len) {
    int32_t i = 0;
    while (i + 16 <= len) {
        for (int j = 0; j < 16; j++) Y[j] ^= data[i + j];
        gf_mul_sw(Y, H);
        i += 16;
    }
    if (i < len) {
        uint8_t blk[16] = {0};
        for (int j = 0; i + j < len; j++) blk[j] = data[i + j];
        for (int j = 0; j < 16; j++) Y[j] ^= blk[j];
        gf_mul_sw(Y, H);
    }
}

#endif /* C1_HW_BACKEND */


#ifndef C1_AES_ATTR
  #define C1_AES_ATTR
#endif
#ifndef C1_SHA_ATTR
  #define C1_SHA_ATTR
#endif

/* Direct calls so the AES core inlines into the CBC/GCM loops instead of
   going through an out-of-line wrapper. */
#if C1_HW_BACKEND
  #define C1_ENC_BLOCK(out, in, rk, nr) aes_encrypt_v((out), (in), (rk), (nr))
  #define C1_DEC_BLOCK(out, in, rk, nr) aes_decrypt_v((out), (in), (rk), (nr))
#else
  #define C1_ENC_BLOCK(out, in, rk, nr) do { (void)(out); (void)(in); (void)(rk); (void)(nr); } while (0)
  #define C1_DEC_BLOCK(out, in, rk, nr) do { (void)(out); (void)(in); (void)(rk); (void)(nr); } while (0)
#endif

/* ==================================================================
 * SHA-256 wrapper: message schedule + hardware rounds
 * ================================================================== */

static const uint32_t SHA256_K[64] = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

static uint32_t ror32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

C1_SHA_ATTR
void c1_sha256_transform(void *ap, void *bp, void *cp, void *dp,
                         void *ep, void *fp, void *gp, void *hp,
                         const void *blockp) {
#if C1_HW_BACKEND
    void *ptrs[8] = { ap, bp, cp, dp, ep, fp, gp, hp };
    const uint8_t *block = (const uint8_t *)blockp;
    uint32_t st[8], saved[8], w[64];

    for (int i = 0; i < 8; i++) st[i] = saved[i] = *(uint32_t *)ptrs[i];

    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[4 * i] << 24) |
               ((uint32_t)block[4 * i + 1] << 16) |
               ((uint32_t)block[4 * i + 2] << 8) |
               (uint32_t)block[4 * i + 3];
    }
#if defined(__clang__)
  #pragma clang loop unroll(full)
#endif
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
#if defined(__clang__)
  #pragma clang loop unroll(full)
#endif
    for (int i = 0; i < 64; i++) w[i] += SHA256_K[i];

    sha256_rounds(st, w);
    for (int i = 0; i < 8; i++) *(uint32_t *)ptrs[i] = st[i] + saved[i];
#else
    (void)ap; (void)bp; (void)cp; (void)dp;
    (void)ep; (void)fp; (void)gp; (void)hp; (void)blockp;
#endif
}

/* ==================================================================
 * Public AES API
 * ================================================================== */

static void hw_encrypt_block(const uint8_t *in, const uint8_t *rk,
                             int nr, uint8_t *out) {
#if C1_HW_BACKEND
    aes_encrypt_v(out, in, rk, nr);
#else
    (void)in; (void)rk; (void)nr; (void)out;
#endif
}

static void hw_decrypt_block(const uint8_t *in, const uint8_t *rk,
                             int nr, uint8_t *out) {
#if C1_HW_BACKEND
    aes_decrypt_v(out, in, rk, nr);
#else
    (void)in; (void)rk; (void)nr; (void)out;
#endif
}

C1_AES_ATTR
static void cbc_encrypt(const void *in, int32_t len, const void *key,
                        const void *iv, void *out, int nk) {
    uint8_t rk[240];
    int nr = nk + 6;
    const uint8_t *p = (const uint8_t *)in;
    uint8_t *c = (uint8_t *)out;
    uint8_t prev[16], blk[16];
    expand_key((const uint8_t *)key, nk, rk);
    memcpy(prev, iv, 16);
    for (int32_t i = 0; i + 16 <= len; i += 16) {
        for (int j = 0; j < 16; j++) blk[j] = p[i + j] ^ prev[j];
        C1_ENC_BLOCK(prev, blk, rk, nr);
        memcpy(c + i, prev, 16);
    }
}

C1_AES_ATTR
static void cbc_decrypt(const void *in, int32_t len, const void *key,
                        const void *iv, void *out, int nk) {
    uint8_t rk[240];
    int nr = nk + 6;
    const uint8_t *c = (const uint8_t *)in;
    uint8_t *p = (uint8_t *)out;
    uint8_t prev[16], cur[16], dec[16];
    expand_key((const uint8_t *)key, nk, rk);
    memcpy(prev, iv, 16);
    for (int32_t i = 0; i + 16 <= len; i += 16) {
        memcpy(cur, c + i, 16);
        C1_DEC_BLOCK(dec, cur, rk, nr);
        for (int j = 0; j < 16; j++) p[i + j] = dec[j] ^ prev[j];
        memcpy(prev, cur, 16);
    }
}

/* GCM with a 96-bit IV. `encrypting` selects which side GHASH covers. */
C1_AES_ATTR
static void gcm_core(const void *in, int32_t len, const void *aad,
                     int32_t aad_len, const void *key, const void *iv,
                     void *out, uint8_t tag[16], int nk, int encrypting) {
    uint8_t rk[240];
    int nr = nk + 6;
    uint8_t H[16], J0[16], cb[16], ks[16], Y[16];
    const uint8_t *p = (const uint8_t *)in;
    uint8_t *o = (uint8_t *)out;

    expand_key((const uint8_t *)key, nk, rk);
    memset(H, 0, 16);
    C1_ENC_BLOCK(H, H, rk, nr);

    memcpy(J0, iv, 12);
    J0[12] = 0; J0[13] = 0; J0[14] = 0; J0[15] = 1;
    memcpy(cb, J0, 16);

    for (int32_t i = 0; i < len; i += 16) {
        for (int j = 15; j >= 12; j--) {
            if (++cb[j] != 0) break;
        }
        C1_ENC_BLOCK(ks, cb, rk, nr);
        int32_t n = len - i;
        if (n > 16) n = 16;
        for (int j = 0; j < n; j++) o[i + j] = p[i + j] ^ ks[j];
    }

    memset(Y, 0, 16);
    ghash_update(Y, H, (const uint8_t *)aad, aad_len);
    ghash_update(Y, H, encrypting ? (const uint8_t *)out : (const uint8_t *)in, len);

    uint8_t lenblk[16] = {0};
    uint64_t aad_bits = (uint64_t)(uint32_t)aad_len * 8;
    uint64_t len_bits = (uint64_t)(uint32_t)len * 8;
    for (int j = 0; j < 8; j++) {
        lenblk[7 - j] = (uint8_t)(aad_bits >> (8 * j));
        lenblk[15 - j] = (uint8_t)(len_bits >> (8 * j));
    }
    ghash_update(Y, H, lenblk, 16);

    uint8_t ek[16];
    C1_ENC_BLOCK(ek, J0, rk, nr);
    for (int j = 0; j < 16; j++) tag[j] = Y[j] ^ ek[j];
}

void c1_aes128_encrypt_block(const void *in, const void *key, void *out) {
    uint8_t rk[176];
    expand_key((const uint8_t *)key, 4, rk);
    hw_encrypt_block((const uint8_t *)in, rk, 10, (uint8_t *)out);
}

void c1_aes128_decrypt_block(const void *in, const void *key, void *out) {
    uint8_t rk[176];
    expand_key((const uint8_t *)key, 4, rk);
    hw_decrypt_block((const uint8_t *)in, rk, 10, (uint8_t *)out);
}

void c1_aes256_encrypt_block(const void *in, const void *key, void *out) {
    uint8_t rk[240];
    expand_key((const uint8_t *)key, 8, rk);
    hw_encrypt_block((const uint8_t *)in, rk, 14, (uint8_t *)out);
}

void c1_aes256_decrypt_block(const void *in, const void *key, void *out) {
    uint8_t rk[240];
    expand_key((const uint8_t *)key, 8, rk);
    hw_decrypt_block((const uint8_t *)in, rk, 14, (uint8_t *)out);
}

void c1_aes128_cbc_encrypt(const void *in, int32_t len, const void *key,
                           const void *iv, void *out) {
    cbc_encrypt(in, len, key, iv, out, 4);
}
void c1_aes128_cbc_decrypt(const void *in, int32_t len, const void *key,
                           const void *iv, void *out) {
    cbc_decrypt(in, len, key, iv, out, 4);
}
void c1_aes256_cbc_encrypt(const void *in, int32_t len, const void *key,
                           const void *iv, void *out) {
    cbc_encrypt(in, len, key, iv, out, 8);
}
void c1_aes256_cbc_decrypt(const void *in, int32_t len, const void *key,
                           const void *iv, void *out) {
    cbc_decrypt(in, len, key, iv, out, 8);
}

int32_t c1_aes128_gcm_encrypt(const void *pt, int32_t pt_len,
                              const void *aad, int32_t aad_len,
                              const void *key, const void *iv,
                              void *ct, void *tag) {
    gcm_core(pt, pt_len, aad, aad_len, key, iv, ct, (uint8_t *)tag, 4, 1);
    return 0;
}

int32_t c1_aes128_gcm_decrypt(const void *ct, int32_t ct_len,
                              const void *aad, int32_t aad_len,
                              const void *key, const void *iv,
                              const void *tag, void *pt) {
    uint8_t want[16];
    gcm_core(ct, ct_len, aad, aad_len, key, iv, pt, want, 4, 0);
    uint8_t diff = 0;
    for (int j = 0; j < 16; j++)
        diff |= (uint8_t)(want[j] ^ ((const uint8_t *)tag)[j]);
    return diff ? -1 : 0;
}

int32_t c1_aes256_gcm_encrypt(const void *pt, int32_t pt_len,
                              const void *aad, int32_t aad_len,
                              const void *key, const void *iv,
                              void *ct, void *tag) {
    gcm_core(pt, pt_len, aad, aad_len, key, iv, ct, (uint8_t *)tag, 8, 1);
    return 0;
}

int32_t c1_aes256_gcm_decrypt(const void *ct, int32_t ct_len,
                              const void *aad, int32_t aad_len,
                              const void *key, const void *iv,
                              const void *tag, void *pt) {
    uint8_t want[16];
    gcm_core(ct, ct_len, aad, aad_len, key, iv, pt, want, 8, 0);
    uint8_t diff = 0;
    for (int j = 0; j < 16; j++)
        diff |= (uint8_t)(want[j] ^ ((const uint8_t *)tag)[j]);
    return diff ? -1 : 0;
}
