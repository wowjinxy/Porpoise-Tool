#include "porpoise/sha256.h"

#include <string.h>

static const uint32_t sha256_constants[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491),
    UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
    UINT32_C(0x3956c25b), UINT32_C(0x59f111f1),
    UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
    UINT32_C(0xd807aa98), UINT32_C(0x12835b01),
    UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe),
    UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
    UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786),
    UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
    UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa),
    UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d),
    UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
    UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147),
    UINT32_C(0x06ca6351), UINT32_C(0x14292967),
    UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138),
    UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb),
    UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
    UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b),
    UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
    UINT32_C(0xd192e819), UINT32_C(0xd6990624),
    UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08),
    UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
    UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a),
    UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
    UINT32_C(0x748f82ee), UINT32_C(0x78a5636f),
    UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb),
    UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)
};

static uint32_t rotate_right(uint32_t value, unsigned int amount) {
    return (value >> amount) | (value << (32U - amount));
}

static uint32_t load_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) |
           ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) |
           (uint32_t)bytes[3];
}

static void store_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
}

static void sha256_transform(
    PorpoiseSha256Context *context,
    const uint8_t block[64]) {
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    size_t index;

    for (index = 0U; index < 16U; index++) {
        words[index] = load_be32(block + index * 4U);
    }
    for (index = 16U; index < 64U; index++) {
        uint32_t s0 = rotate_right(words[index - 15U], 7U) ^
                      rotate_right(words[index - 15U], 18U) ^
                      (words[index - 15U] >> 3U);
        uint32_t s1 = rotate_right(words[index - 2U], 17U) ^
                      rotate_right(words[index - 2U], 19U) ^
                      (words[index - 2U] >> 10U);
        words[index] = words[index - 16U] + s0 +
                       words[index - 7U] + s1;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (index = 0U; index < 64U; index++) {
        uint32_t upper_sigma1 = rotate_right(e, 6U) ^
                                rotate_right(e, 11U) ^
                                rotate_right(e, 25U);
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t temporary1 = h + upper_sigma1 + choice +
                              sha256_constants[index] + words[index];
        uint32_t upper_sigma0 = rotate_right(a, 2U) ^
                                rotate_right(a, 13U) ^
                                rotate_right(a, 22U);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary2 = upper_sigma0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

void porpoise_sha256_init(PorpoiseSha256Context *context) {
    if (context == NULL) return;
    context->state[0] = UINT32_C(0x6a09e667);
    context->state[1] = UINT32_C(0xbb67ae85);
    context->state[2] = UINT32_C(0x3c6ef372);
    context->state[3] = UINT32_C(0xa54ff53a);
    context->state[4] = UINT32_C(0x510e527f);
    context->state[5] = UINT32_C(0x9b05688c);
    context->state[6] = UINT32_C(0x1f83d9ab);
    context->state[7] = UINT32_C(0x5be0cd19);
    context->byte_count = UINT64_C(0);
    context->block_size = 0U;
    memset(context->block, 0, sizeof(context->block));
}

void porpoise_sha256_update(
    PorpoiseSha256Context *context,
    const void *data,
    size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;

    if (context == NULL || size == 0U) return;
    if (bytes == NULL) return;

    context->byte_count += (uint64_t)size;
    while (size != 0U) {
        size_t available = sizeof(context->block) - context->block_size;
        size_t copied = size < available ? size : available;
        memcpy(context->block + context->block_size, bytes, copied);
        context->block_size += copied;
        bytes += copied;
        size -= copied;
        if (context->block_size == sizeof(context->block)) {
            sha256_transform(context, context->block);
            context->block_size = 0U;
        }
    }
}

void porpoise_sha256_final(
    PorpoiseSha256Context *context,
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE]) {
    uint64_t bit_count;
    size_t index;

    if (context == NULL || digest == NULL) return;
    bit_count = context->byte_count * UINT64_C(8);

    context->block[context->block_size++] = UINT8_C(0x80);
    if (context->block_size > 56U) {
        memset(context->block + context->block_size, 0,
               sizeof(context->block) - context->block_size);
        sha256_transform(context, context->block);
        context->block_size = 0U;
    }
    memset(context->block + context->block_size, 0,
           56U - context->block_size);
    for (index = 0U; index < 8U; index++) {
        context->block[63U - index] = (uint8_t)(bit_count >> (index * 8U));
    }
    sha256_transform(context, context->block);

    for (index = 0U; index < 8U; index++) {
        store_be32(digest + index * 4U, context->state[index]);
    }
    context->block_size = 0U;
    memset(context->block, 0, sizeof(context->block));
}

void porpoise_sha256(
    const void *data,
    size_t size,
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE]) {
    PorpoiseSha256Context context;
    if (digest == NULL) return;
    porpoise_sha256_init(&context);
    porpoise_sha256_update(&context, data, size);
    porpoise_sha256_final(&context, digest);
}

void porpoise_sha256_hex(
    const uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE],
    char hex[PORPOISE_SHA256_HEX_SIZE]) {
    static const char digits[] = "0123456789abcdef";
    size_t index;

    if (hex == NULL) return;
    if (digest == NULL) {
        hex[0] = '\0';
        return;
    }
    for (index = 0U; index < PORPOISE_SHA256_DIGEST_SIZE; index++) {
        hex[index * 2U] = digits[digest[index] >> 4U];
        hex[index * 2U + 1U] = digits[digest[index] & UINT8_C(0x0f)];
    }
    hex[PORPOISE_SHA256_HEX_SIZE - 1U] = '\0';
}
