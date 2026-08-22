#include <dolphin/gx.h>
#include <porpoise/gx_values_stub.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    STUB_GX_VALUE_MAX_U32 = 16,
    STUB_GX_VALUE_MAX_FLOAT = 16,
    STUB_GX_VALUE_MAX_S16 = 4,
    STUB_GX_VALUE_MAX_U16 = 10,
    STUB_GX_VALUE_MAX_BYTES = 256
};

static unsigned int call_counts[PORPOISE_STUB_GX_VALUE_CALL_COUNT];
static const void *last_pointer;
static uint32_t last_u32[STUB_GX_VALUE_MAX_U32];
static float last_float[STUB_GX_VALUE_MAX_FLOAT];
static int16_t last_s16[STUB_GX_VALUE_MAX_S16];
static uint16_t last_u16[STUB_GX_VALUE_MAX_U16];
static uint8_t last_bytes[STUB_GX_VALUE_MAX_BYTES];
static size_t last_byte_count;
static float projection_output[7];
static float viewport_output[6];
static float y_scale_factor_output;
static uint32_t disp_copy_y_scale_output;
static struct __GXData_struct gx_data;
struct __GXData_struct *gx = &gx_data;

static void begin_call(PorpoiseStubGXValueCall call)
{
    call_counts[call]++;
    last_pointer = NULL;
    memset(last_u32, 0, sizeof(last_u32));
    memset(last_float, 0, sizeof(last_float));
    memset(last_s16, 0, sizeof(last_s16));
    memset(last_u16, 0, sizeof(last_u16));
    memset(last_bytes, 0, sizeof(last_bytes));
    last_byte_count = 0U;
}

static uint32_t pack_color(GXColor color)
{
    return ((uint32_t)color.r << 24) |
           ((uint32_t)color.g << 16) |
           ((uint32_t)color.b << 8) |
           (uint32_t)color.a;
}

void PorpoiseStubGXValuesReset(void)
{
    memset(call_counts, 0, sizeof(call_counts));
    last_pointer = NULL;
    memset(last_u32, 0, sizeof(last_u32));
    memset(last_float, 0, sizeof(last_float));
    memset(last_s16, 0, sizeof(last_s16));
    memset(last_u16, 0, sizeof(last_u16));
    memset(last_bytes, 0, sizeof(last_bytes));
    last_byte_count = 0U;
    memset(projection_output, 0, sizeof(projection_output));
    memset(viewport_output, 0, sizeof(viewport_output));
    y_scale_factor_output = 0.0f;
    disp_copy_y_scale_output = 0U;
    gx_data.dirtyState = 0U;
    gx_data.flushReady = GX_TRUE;
}

unsigned int PorpoiseStubGXValueCallCount(PorpoiseStubGXValueCall call)
{
    if ((unsigned int)call >= PORPOISE_STUB_GX_VALUE_CALL_COUNT) {
        return 0U;
    }
    return call_counts[call];
}

const void *PorpoiseStubGXValueLastPointer(void)
{
    return last_pointer;
}

uint32_t PorpoiseStubGXValueLastU32(unsigned int index)
{
    return index < STUB_GX_VALUE_MAX_U32 ? last_u32[index] : 0U;
}

int32_t PorpoiseStubGXValueLastS32(unsigned int index)
{
    return index < STUB_GX_VALUE_MAX_U32
               ? (int32_t)last_u32[index]
               : 0;
}

float PorpoiseStubGXValueLastFloat(unsigned int index)
{
    return index < STUB_GX_VALUE_MAX_FLOAT ? last_float[index] : 0.0f;
}

int16_t PorpoiseStubGXValueLastS16(unsigned int index)
{
    return index < STUB_GX_VALUE_MAX_S16 ? last_s16[index] : 0;
}

uint16_t PorpoiseStubGXValueLastU16(unsigned int index)
{
    return index < STUB_GX_VALUE_MAX_U16 ? last_u16[index] : 0U;
}

size_t PorpoiseStubGXValueLastByteCount(void)
{
    return last_byte_count;
}

uint8_t PorpoiseStubGXValueLastByte(unsigned int index)
{
    return index < last_byte_count ? last_bytes[index] : 0U;
}

void PorpoiseStubGXValueSetProjectionOutput(const float values[7])
{
    memcpy(projection_output, values, sizeof(projection_output));
}

void PorpoiseStubGXValueSetViewportOutput(const float values[6])
{
    memcpy(viewport_output, values, sizeof(viewport_output));
}

void PorpoiseStubGXValueSetYScaleFactorOutput(float value)
{
    y_scale_factor_output = value;
}

void PorpoiseStubGXValueSetDispCopyYScaleOutput(uint32_t value)
{
    disp_copy_y_scale_output = value;
}

void PorpoiseStubGXValueSetBeginPreamble(
    uint32_t dirty_state,
    int flush_ready)
{
    gx_data.dirtyState = dirty_state;
    gx_data.flushReady = flush_ready ? GX_TRUE : GX_FALSE;
}

void __GXSetDirtyState(void)
{
    call_counts[PORPOISE_STUB_GX_SET_DIRTY_STATE]++;
    gx_data.dirtyState = 0U;
}

void __GXSendFlushPrim(void)
{
    call_counts[PORPOISE_STUB_GX_SEND_FLUSH_PRIM]++;
    gx_data.flushReady = GX_TRUE;
}

void GXCallDisplayList(const void *list, u32 nbytes)
{
    size_t copy_size = (size_t)nbytes;

    begin_call(PORPOISE_STUB_GX_CALL_DISPLAY_LIST);
    last_pointer = list;
    last_u32[0] = nbytes;
    if (copy_size > sizeof(last_bytes)) {
        copy_size = sizeof(last_bytes);
    }
    if (list != NULL && copy_size != 0U) {
        memcpy(last_bytes, list, copy_size);
        last_byte_count = copy_size;
    }
}

void GXSetProjection(const Mtx44 matrix, GXProjectionType type)
{
    begin_call(PORPOISE_STUB_GX_SET_PROJECTION);
    last_u32[0] = (uint32_t)type;
    memcpy(last_float, matrix, sizeof(Mtx44));
}

void GXGetProjectionv(f32 *projection)
{
    begin_call(PORPOISE_STUB_GX_GET_PROJECTIONV);
    memcpy(projection, projection_output, sizeof(projection_output));
}

void GXLoadPosMtxImm(const Mtx matrix, u32 id)
{
    begin_call(PORPOISE_STUB_GX_LOAD_POS_MTX_IMM);
    last_u32[0] = id;
    memcpy(last_float, matrix, sizeof(Mtx));
}

void GXLoadNrmMtxImm(const Mtx matrix, u32 id)
{
    begin_call(PORPOISE_STUB_GX_LOAD_NRM_MTX_IMM);
    last_u32[0] = id;
    memcpy(last_float, matrix, sizeof(Mtx));
}

void GXLoadTexMtxImm(const Mtx matrix, u32 id, GXTexMtxType type)
{
    size_t count = type == GX_MTX2x4 ? 8U : 12U;

    begin_call(PORPOISE_STUB_GX_LOAD_TEX_MTX_IMM);
    last_u32[0] = id;
    last_u32[1] = (uint32_t)type;
    memcpy(last_float, matrix, count * sizeof(float));
}

void GXGetViewportv(f32 *viewport)
{
    begin_call(PORPOISE_STUB_GX_GET_VIEWPORTV);
    memcpy(viewport, viewport_output, sizeof(viewport_output));
}

void GXSetIndTexMtx(
    GXIndTexMtxID id,
    const Mtx23 matrix,
    s8 scale)
{
    begin_call(PORPOISE_STUB_GX_SET_IND_TEX_MTX);
    last_u32[0] = (uint32_t)id;
    last_u32[1] = (uint32_t)(int32_t)scale;
    memcpy(last_float, matrix, sizeof(Mtx23));
}

void GXSetTevColor(GXTevRegID id, GXColor color)
{
    begin_call(PORPOISE_STUB_GX_SET_TEV_COLOR);
    last_u32[0] = (uint32_t)id;
    last_u32[1] = pack_color(color);
}

void GXSetTevColorS10(GXTevRegID id, GXColorS10 color)
{
    begin_call(PORPOISE_STUB_GX_SET_TEV_COLOR_S10);
    last_u32[0] = (uint32_t)id;
    last_s16[0] = color.r;
    last_s16[1] = color.g;
    last_s16[2] = color.b;
    last_s16[3] = color.a;
}

void GXSetTevKColor(GXTevKColorID id, GXColor color)
{
    begin_call(PORPOISE_STUB_GX_SET_TEV_KCOLOR);
    last_u32[0] = (uint32_t)id;
    last_u32[1] = pack_color(color);
}

void GXSetFog(
    GXFogType type,
    f32 start_z,
    f32 end_z,
    f32 near_z,
    f32 far_z,
    GXColor color)
{
    begin_call(PORPOISE_STUB_GX_SET_FOG);
    last_u32[0] = (uint32_t)type;
    last_u32[1] = pack_color(color);
    last_float[0] = start_z;
    last_float[1] = end_z;
    last_float[2] = near_z;
    last_float[3] = far_z;
}

void GXSetFogRangeAdj(
    GXBool enable,
    u16 center,
    GXFogAdjTable *table)
{
    begin_call(PORPOISE_STUB_GX_SET_FOG_RANGE_ADJ);
    last_u32[0] = (uint32_t)enable;
    last_u32[1] = (uint32_t)center;
    last_u32[2] = table != NULL ? 1U : 0U;
    if (table != NULL) {
        memcpy(last_u16, table->fogVals, sizeof(last_u16));
    }
}

void GXSetTevIndirect(
    GXTevStageID stage,
    GXIndTexStageID indirect_stage,
    GXIndTexFormat format,
    GXIndTexBiasSel bias,
    GXIndTexMtxID matrix,
    GXIndTexWrap wrap_s,
    GXIndTexWrap wrap_t,
    GXBool add_previous,
    GXBool indirect_lod,
    GXIndTexAlphaSel alpha)
{
    begin_call(PORPOISE_STUB_GX_SET_TEV_INDIRECT);
    last_u32[0] = (uint32_t)stage;
    last_u32[1] = (uint32_t)indirect_stage;
    last_u32[2] = (uint32_t)format;
    last_u32[3] = (uint32_t)bias;
    last_u32[4] = (uint32_t)matrix;
    last_u32[5] = (uint32_t)wrap_s;
    last_u32[6] = (uint32_t)wrap_t;
    last_u32[7] = (uint32_t)add_previous;
    last_u32[8] = (uint32_t)indirect_lod;
    last_u32[9] = (uint32_t)alpha;
}

void GXClearVtxDesc(void)
{
    begin_call(PORPOISE_STUB_GX_CLEAR_VTX_DESC);
}

void GXSetVtxDesc(GXAttr attr, GXAttrType type)
{
    begin_call(PORPOISE_STUB_GX_SET_VTX_DESC);
    last_u32[0] = (uint32_t)attr;
    last_u32[1] = (uint32_t)type;
}

void GXSetVtxAttrFmt(
    GXVtxFmt format,
    GXAttr attr,
    GXCompCnt count,
    GXCompType type,
    u8 fraction)
{
    begin_call(PORPOISE_STUB_GX_SET_VTX_ATTR_FMT);
    last_u32[0] = (uint32_t)format;
    last_u32[1] = (uint32_t)attr;
    last_u32[2] = (uint32_t)count;
    last_u32[3] = (uint32_t)type;
    last_u32[4] = (uint32_t)fraction;
}

void GXInvalidateVtxCache(void)
{
    begin_call(PORPOISE_STUB_GX_INVALIDATE_VTX_CACHE);
}

void GXSetNumTexGens(u8 count)
{
    begin_call(PORPOISE_STUB_GX_SET_NUM_TEX_GENS);
    last_u32[0] = (uint32_t)count;
}

void GXSetNumChans(u8 count)
{
    begin_call(PORPOISE_STUB_GX_SET_NUM_CHANS);
    last_u32[0] = (uint32_t)count;
}

void GXInvalidateTexAll(void)
{
    begin_call(PORPOISE_STUB_GX_INVALIDATE_TEX_ALL);
}

void GXSetTevOp(GXTevStageID stage, GXTevMode mode)
{
    begin_call(PORPOISE_STUB_GX_SET_TEV_OP);
    last_u32[0] = (uint32_t)stage;
    last_u32[1] = (uint32_t)mode;
}

void GXSetTevOrder(
    GXTevStageID stage,
    GXTexCoordID coordinate,
    GXTexMapID map,
    GXChannelID color)
{
    begin_call(PORPOISE_STUB_GX_SET_TEV_ORDER);
    last_u32[0] = (uint32_t)stage;
    last_u32[1] = (uint32_t)coordinate;
    last_u32[2] = (uint32_t)map;
    last_u32[3] = (uint32_t)color;
}

void GXSetNumTevStages(u8 count)
{
    begin_call(PORPOISE_STUB_GX_SET_NUM_TEV_STAGES);
    last_u32[0] = (uint32_t)count;
}

void GXSetColorUpdate(u8 enable)
{
    begin_call(PORPOISE_STUB_GX_SET_COLOR_UPDATE);
    last_u32[0] = (uint32_t)enable;
}

void GXSetZMode(u8 compare, GXCompare function, u8 update)
{
    begin_call(PORPOISE_STUB_GX_SET_Z_MODE);
    last_u32[0] = (uint32_t)compare;
    last_u32[1] = (uint32_t)function;
    last_u32[2] = (uint32_t)update;
}

void GXSetPixelFmt(GXPixelFmt pixel_format, GXZFmt16 depth_format)
{
    begin_call(PORPOISE_STUB_GX_SET_PIXEL_FMT);
    last_u32[0] = (uint32_t)pixel_format;
    last_u32[1] = (uint32_t)depth_format;
}

void GXSetViewport(
    f32 left,
    f32 top,
    f32 width,
    f32 height,
    f32 near_z,
    f32 far_z)
{
    begin_call(PORPOISE_STUB_GX_SET_VIEWPORT);
    last_float[0] = left;
    last_float[1] = top;
    last_float[2] = width;
    last_float[3] = height;
    last_float[4] = near_z;
    last_float[5] = far_z;
}

void GXSetScissor(u32 left, u32 top, u32 width, u32 height)
{
    begin_call(PORPOISE_STUB_GX_SET_SCISSOR);
    last_u32[0] = (uint32_t)left;
    last_u32[1] = (uint32_t)top;
    last_u32[2] = (uint32_t)width;
    last_u32[3] = (uint32_t)height;
}

void GXSetDispCopySrc(u16 left, u16 top, u16 width, u16 height)
{
    begin_call(PORPOISE_STUB_GX_SET_DISP_COPY_SRC);
    last_u32[0] = (uint32_t)left;
    last_u32[1] = (uint32_t)top;
    last_u32[2] = (uint32_t)width;
    last_u32[3] = (uint32_t)height;
}

f32 GXGetYScaleFactor(u16 efb_height, u16 xfb_height)
{
    begin_call(PORPOISE_STUB_GX_GET_Y_SCALE_FACTOR);
    last_u32[0] = (uint32_t)efb_height;
    last_u32[1] = (uint32_t)xfb_height;
    return y_scale_factor_output;
}

u32 GXSetDispCopyYScale(f32 scale)
{
    begin_call(PORPOISE_STUB_GX_SET_DISP_COPY_Y_SCALE);
    last_float[0] = scale;
    return (u32)disp_copy_y_scale_output;
}

void GXSetDispCopyGamma(GXGamma gamma)
{
    begin_call(PORPOISE_STUB_GX_SET_DISP_COPY_GAMMA);
    last_u32[0] = (uint32_t)gamma;
}
