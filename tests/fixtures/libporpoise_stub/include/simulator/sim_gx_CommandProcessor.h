#ifndef TEST_LIBPORPOISE_SIM_GX_COMMAND_PROCESSOR_H
#define TEST_LIBPORPOISE_SIM_GX_COMMAND_PROCESSOR_H

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION 1

typedef enum GXBool {
    GX_FALSE = 0,
    GX_TRUE = 1
} GXBool;

void SIM_GX_CommandProcessor_SendU8(u8 data);
void SIM_GX_CommandProcessor_SendU16(u16 data);
void SIM_GX_CommandProcessor_SendS16(s16 data);
void SIM_GX_CommandProcessor_SendU32(u32 data);
void SIM_GX_CommandProcessor_SendF32(f32 data);
GXBool SIM_GX_CommandProcessor_SendCanonicalBytes(
    const u8 *data,
    u32 size);

#ifdef __cplusplus
}
#endif

#endif
