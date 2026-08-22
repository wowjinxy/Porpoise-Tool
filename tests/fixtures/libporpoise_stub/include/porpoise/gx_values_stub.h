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
    PORPOISE_STUB_GX_SET_DIRTY_STATE,
    PORPOISE_STUB_GX_SEND_FLUSH_PRIM,
    PORPOISE_STUB_GX_CLEAR_VTX_DESC,
    PORPOISE_STUB_GX_SET_VTX_DESC,
    PORPOISE_STUB_GX_SET_VTX_ATTR_FMT,
    PORPOISE_STUB_GX_INVALIDATE_VTX_CACHE,
    PORPOISE_STUB_GX_SET_NUM_TEX_GENS,
    PORPOISE_STUB_GX_SET_NUM_CHANS,
    PORPOISE_STUB_GX_INVALIDATE_TEX_ALL,
    PORPOISE_STUB_GX_SET_TEV_OP,
    PORPOISE_STUB_GX_SET_TEV_ORDER,
    PORPOISE_STUB_GX_SET_NUM_TEV_STAGES,
    PORPOISE_STUB_GX_SET_COLOR_UPDATE,
    PORPOISE_STUB_GX_SET_Z_MODE,
    PORPOISE_STUB_GX_SET_PIXEL_FMT,
    PORPOISE_STUB_GX_SET_VIEWPORT,
    PORPOISE_STUB_GX_SET_SCISSOR,
    PORPOISE_STUB_GX_SET_DISP_COPY_SRC,
    PORPOISE_STUB_GX_GET_Y_SCALE_FACTOR,
    PORPOISE_STUB_GX_SET_DISP_COPY_Y_SCALE,
    PORPOISE_STUB_GX_SET_DISP_COPY_GAMMA,
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
void PorpoiseStubGXValueSetYScaleFactorOutput(float value);
void PorpoiseStubGXValueSetDispCopyYScaleOutput(uint32_t value);
void PorpoiseStubGXValueSetBeginPreamble(
    uint32_t dirty_state,
    int flush_ready);

#ifdef __cplusplus
}
#endif

#endif
