#include "porpoise/sha256.h"

#include <stdio.h>
#include <string.h>

static unsigned int failures = 0U;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n",                     \
                    __FILE__, __LINE__, #condition);                            \
            failures++;                                                        \
        }                                                                       \
    } while (0)

static void check_vector(
    const void *bytes,
    size_t size,
    const char *expected) {
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    char hex[PORPOISE_SHA256_HEX_SIZE];
    porpoise_sha256(bytes, size, digest);
    porpoise_sha256_hex(digest, hex);
    CHECK(strcmp(hex, expected) == 0);
}

static void test_standard_vectors(void) {
    static const char long_message[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    check_vector(NULL, 0U,
                 "e3b0c44298fc1c149afbf4c8996fb924"
                 "27ae41e4649b934ca495991b7852b855");
    check_vector("abc", 3U,
                 "ba7816bf8f01cfea414140de5dae2223"
                 "b00361a396177a9cb410ff61f20015ad");
    check_vector(long_message, sizeof(long_message) - 1U,
                 "248d6a61d20638b8e5c026930c3e6039"
                 "a33ce45964ff2167f6ecedd419db06c1");
}

static void test_incremental_updates(void) {
    PorpoiseSha256Context context;
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    char hex[PORPOISE_SHA256_HEX_SIZE];

    porpoise_sha256_init(&context);
    porpoise_sha256_update(&context, "a", 1U);
    porpoise_sha256_update(&context, NULL, 0U);
    porpoise_sha256_update(&context, "b", 1U);
    porpoise_sha256_update(&context, "c", 1U);
    porpoise_sha256_final(&context, digest);
    porpoise_sha256_hex(digest, hex);
    CHECK(strcmp(hex,
                 "ba7816bf8f01cfea414140de5dae2223"
                 "b00361a396177a9cb410ff61f20015ad") == 0);
}

static void test_million_a(void) {
    PorpoiseSha256Context context;
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    char hex[PORPOISE_SHA256_HEX_SIZE];
    char block[1000];
    size_t index;

    memset(block, 'a', sizeof(block));
    porpoise_sha256_init(&context);
    for (index = 0U; index < 1000U; index++)
        porpoise_sha256_update(&context, block, sizeof(block));
    porpoise_sha256_final(&context, digest);
    porpoise_sha256_hex(digest, hex);
    CHECK(strcmp(hex,
                 "cdc76e5c9914fb9281a1c7e284d73e67"
                 "f1809a48a497200e046d39ccc7112cd0") == 0);
}

int main(void) {
    test_standard_vectors();
    test_incremental_updates();
    test_million_a();

    if (failures != 0U) {
        fprintf(stderr, "%u SHA-256 test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
