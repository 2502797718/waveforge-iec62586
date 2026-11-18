#include <stdio.h>
#include <string.h>

#include "wg_internal.h"

/* SHA-256（FIPS 180-4），仅用于 manifest 的输出校验。 */

typedef struct {
    uint32_t au32_state[8];
    uint64_t u64_total;
    unsigned char au8_block[64];
    size_t z_used;
} prv_sha256_ctx_t;

static const uint32_t s_au32_sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t prv_rotr(uint32_t u32_val, uint32_t u32_shift)
{
    return (u32_val >> u32_shift) | (u32_val << (32u - u32_shift));
}

static void prv_sha256_block(prv_sha256_ctx_t *pst_ctx, const unsigned char *p_block)
{
    uint32_t au32_w[64];
    uint32_t u32_a;
    uint32_t u32_b;
    uint32_t u32_c;
    uint32_t u32_d;
    uint32_t u32_e;
    uint32_t u32_f;
    uint32_t u32_g;
    uint32_t u32_h;
    uint32_t u32_idx;

    for (u32_idx = 0u; u32_idx < 16u; u32_idx++) {
        au32_w[u32_idx] = ((uint32_t)p_block[u32_idx * 4u] << 24)
                        | ((uint32_t)p_block[u32_idx * 4u + 1u] << 16)
                        | ((uint32_t)p_block[u32_idx * 4u + 2u] << 8)
                        | (uint32_t)p_block[u32_idx * 4u + 3u];
    }
    for (u32_idx = 16u; u32_idx < 64u; u32_idx++) {
        uint32_t u32_s0 = prv_rotr(au32_w[u32_idx - 15u], 7u)
                        ^ prv_rotr(au32_w[u32_idx - 15u], 18u)
                        ^ (au32_w[u32_idx - 15u] >> 3);
        uint32_t u32_s1 = prv_rotr(au32_w[u32_idx - 2u], 17u)
                        ^ prv_rotr(au32_w[u32_idx - 2u], 19u)
                        ^ (au32_w[u32_idx - 2u] >> 10);
        au32_w[u32_idx] = au32_w[u32_idx - 16u] + u32_s0
                        + au32_w[u32_idx - 7u] + u32_s1;
    }

    u32_a = pst_ctx->au32_state[0];
    u32_b = pst_ctx->au32_state[1];
    u32_c = pst_ctx->au32_state[2];
    u32_d = pst_ctx->au32_state[3];
    u32_e = pst_ctx->au32_state[4];
    u32_f = pst_ctx->au32_state[5];
    u32_g = pst_ctx->au32_state[6];
    u32_h = pst_ctx->au32_state[7];

    for (u32_idx = 0u; u32_idx < 64u; u32_idx++) {
        uint32_t u32_s1 = prv_rotr(u32_e, 6u) ^ prv_rotr(u32_e, 11u) ^ prv_rotr(u32_e, 25u);
        uint32_t u32_ch = (u32_e & u32_f) ^ ((~u32_e) & u32_g);
        uint32_t u32_t1 = u32_h + u32_s1 + u32_ch + s_au32_sha256_k[u32_idx] + au32_w[u32_idx];
        uint32_t u32_s0 = prv_rotr(u32_a, 2u) ^ prv_rotr(u32_a, 13u) ^ prv_rotr(u32_a, 22u);
        uint32_t u32_maj = (u32_a & u32_b) ^ (u32_a & u32_c) ^ (u32_b & u32_c);
        uint32_t u32_t2 = u32_s0 + u32_maj;

        u32_h = u32_g;
        u32_g = u32_f;
        u32_f = u32_e;
        u32_e = u32_d + u32_t1;
        u32_d = u32_c;
        u32_c = u32_b;
        u32_b = u32_a;
        u32_a = u32_t1 + u32_t2;
    }

    pst_ctx->au32_state[0] += u32_a;
    pst_ctx->au32_state[1] += u32_b;
    pst_ctx->au32_state[2] += u32_c;
    pst_ctx->au32_state[3] += u32_d;
    pst_ctx->au32_state[4] += u32_e;
    pst_ctx->au32_state[5] += u32_f;
    pst_ctx->au32_state[6] += u32_g;
    pst_ctx->au32_state[7] += u32_h;
}

static void prv_sha256_init(prv_sha256_ctx_t *pst_ctx)
{
    pst_ctx->au32_state[0] = 0x6a09e667u;
    pst_ctx->au32_state[1] = 0xbb67ae85u;
    pst_ctx->au32_state[2] = 0x3c6ef372u;
    pst_ctx->au32_state[3] = 0xa54ff53au;
    pst_ctx->au32_state[4] = 0x510e527fu;
    pst_ctx->au32_state[5] = 0x9b05688cu;
    pst_ctx->au32_state[6] = 0x1f83d9abu;
    pst_ctx->au32_state[7] = 0x5be0cd19u;
    pst_ctx->u64_total = 0u;
    pst_ctx->z_used = 0u;
}

static void prv_sha256_update(prv_sha256_ctx_t *pst_ctx, const void *p_data, size_t z_len)
{
    const unsigned char *p_cur = (const unsigned char *)p_data;

    pst_ctx->u64_total += (uint64_t)z_len;
    while (z_len > 0u) {
        size_t z_copy = 64u - pst_ctx->z_used;

        if (z_copy > z_len) {
            z_copy = z_len;
        }
        (void)memcpy(pst_ctx->au8_block + pst_ctx->z_used, p_cur, z_copy);
        pst_ctx->z_used += z_copy;
        p_cur += z_copy;
        z_len -= z_copy;
        if (pst_ctx->z_used == 64u) {
            prv_sha256_block(pst_ctx, pst_ctx->au8_block);
            pst_ctx->z_used = 0u;
        }
    }
}

static void prv_sha256_final(prv_sha256_ctx_t *pst_ctx, unsigned char *p_digest)
{
    uint64_t u64_bits = pst_ctx->u64_total * 8u;
    unsigned char a_pad[72];
    size_t z_pad_len;
    size_t z_idx;

    z_pad_len = (pst_ctx->z_used < 56u) ? (56u - pst_ctx->z_used)
                                       : (120u - pst_ctx->z_used);
    (void)memset(a_pad, 0, sizeof(a_pad));
    a_pad[0] = 0x80u;
    for (z_idx = 0u; z_idx < 8u; z_idx++) {
        a_pad[z_pad_len + z_idx] = (unsigned char)(u64_bits >> (56u - 8u * (uint32_t)z_idx));
    }
    prv_sha256_update(pst_ctx, a_pad, z_pad_len + 8u);

    for (z_idx = 0u; z_idx < 8u; z_idx++) {
        uint32_t u32_val = pst_ctx->au32_state[z_idx];

        p_digest[z_idx * 4u] = (unsigned char)(u32_val >> 24);
        p_digest[z_idx * 4u + 1u] = (unsigned char)(u32_val >> 16);
        p_digest[z_idx * 4u + 2u] = (unsigned char)(u32_val >> 8);
        p_digest[z_idx * 4u + 3u] = (unsigned char)u32_val;
    }
}

int32_t prv_sha256_file(const char *pc_path, char ac_hex[65])
{
    static const char s_ac_hex[] = "0123456789abcdef";
    unsigned char a_buf[8192];
    unsigned char a_digest[32];
    prv_sha256_ctx_t st_ctx;
    FILE *p_file;
    size_t z_read;
    size_t z_idx;

    p_file = fopen(pc_path, "rb");
    if (p_file == NULL) {
        return PRV_ERR_IO;
    }

    prv_sha256_init(&st_ctx);
    while ((z_read = fread(a_buf, 1u, sizeof(a_buf), p_file)) > 0u) {
        prv_sha256_update(&st_ctx, a_buf, z_read);
    }
    if (ferror(p_file) != 0) {
        (void)fclose(p_file);
        return PRV_ERR_IO;
    }
    (void)fclose(p_file);

    prv_sha256_final(&st_ctx, a_digest);
    for (z_idx = 0u; z_idx < 32u; z_idx++) {
        ac_hex[z_idx * 2u] = s_ac_hex[a_digest[z_idx] >> 4];
        ac_hex[z_idx * 2u + 1u] = s_ac_hex[a_digest[z_idx] & 0x0Fu];
    }
    ac_hex[64] = '\0';
    return PRV_OK;
}
