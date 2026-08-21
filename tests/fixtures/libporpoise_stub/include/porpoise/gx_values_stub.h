#ifndef TEST_LIBPORPOISE_GX_VALUES_STUB_H
#define TEST_LIBPORPOISE_GX_VALUES_STUB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PorpoiseStubGXValueCall {
    PORPOISE_STUB_GX_CALL_DISPLAY_LIST = 0,
    PORPOISE_STUB_GX_SET_PROJECTION,
    PORPOISE_STUB_GX_GET_PROJECTIONV,
    PORPOISE_STUB_GX_LOAD_POS_MTX_IMM,
    PORPOISE_STUB_GX_LOAD_NRM_MTX_IMM,
    PORPOISE_STUB_GX_LOAD_TEX_MTX_IMM,
    PORPOISE_STUB_GX_GET_VIEWPORTV,
    PORPOISE_STUB_GX_SET_IND_TEX_MTX,
    PORPOISE_STUB_GX_SET_TEV_COLOR,
    PORPOISE_STUB_GX_SET_TEV_COLOR_S10,
    PORPOISE_STUB_GX_SET_TEV_KCOLOR,
    PORPOISE_STUB_GX_SET_FOG,
    PORPOISE_STUB_GX_SET_FOG_RANGE_ADJ,
    PORPOISE_STUB_GX_SET_TEV_INDIRECT,
    PORPOISE_STUB_GX_VALUE_CALL_COUNT
} PorpoiseStubGXValueCall;

void PorpoiseStubGXValuesReset(void);
unsigned int PorpoiseStubGXValueCallCount(PorpoiseStubGXValueCall call);
const void *PorpoiseStubGXValueLastPointer(void);
uint32_t PorpoiseStubGXValueLastU32(unsigned int index);
int32_t PorpoiseStubGXValueLastS32(unsigned int index);
float PorpoiseStubGXValueLastFloat(unsigned int index);
int16_t PorpoiseStubGXValueLastS16(unsigned int index);
uint16_t PorpoiseStubGXValueLastU16(unsigned int index);
size_t PorpoiseStubGXValueLastByteCount(void);
uint8_t PorpoiseStubGXValueLastByte(unsigned int index);
void PorpoiseStubGXValueSetProjectionOutput(const float values[7]);
void PorpoiseStubGXValueSetViewportOutput(const float values[6]);

#ifdef __cplusplus
}
#endif

#endif
