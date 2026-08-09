#ifndef TEST_LIBPORPOISE_OS_HOST_ADDRESS_H
#define TEST_LIBPORPOISE_OS_HOST_ADDRESS_H

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OS_HOST_ADDRESS_TOKEN_TAG 0xB0000000U
#define OS_HOST_ADDRESS_TOKEN_MASK 0xF0000000U
#define OS_HOST_ADDRESS_TOKEN_SLOT_BITS 14U
#define OS_HOST_ADDRESS_TOKEN_SLOT_COUNT (1U << OS_HOST_ADDRESS_TOKEN_SLOT_BITS)

BOOL __OSHostIsAddressToken(u32 address);
BOOL __OSHostIsFileBackedImageAddress(const void *pointer);
u32 __OSHostEncodeAddress(const void *pointer);
u32 __OSHostEncodePointerWord(const void *pointer);
void *__OSHostDecodeAddress(u32 address);
void __OSHostReleaseAddress(u32 token);

#ifdef __cplusplus
}
#endif

#endif
