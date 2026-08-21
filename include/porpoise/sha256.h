#ifndef PORPOISE_SHA256_H
#define PORPOISE_SHA256_H

#include "porpoise/common.h"

#define PORPOISE_SHA256_DIGEST_SIZE 32U
#define PORPOISE_SHA256_HEX_SIZE 65U

typedef struct PorpoiseSha256Context {
    uint32_t state[8];
    uint64_t byte_count;
    uint8_t block[64];
    size_t block_size;
} PorpoiseSha256Context;

void porpoise_sha256_init(PorpoiseSha256Context *context);
void porpoise_sha256_update(
    PorpoiseSha256Context *context,
    const void *data,
    size_t size);
void porpoise_sha256_final(
    PorpoiseSha256Context *context,
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE]);

void porpoise_sha256(
    const void *data,
    size_t size,
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE]);

void porpoise_sha256_hex(
    const uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE],
    char hex[PORPOISE_SHA256_HEX_SIZE]);

#endif
