#ifndef TEST_LIBPORPOISE_DOLPHIN_GX_H
#define TEST_LIBPORPOISE_DOLPHIN_GX_H

#include <dolphin/types.h>
#include <simulator/sim_gx_CommandProcessor.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GXFifoObj GXFifoObj;

#if defined(PORPOISE_STUB_DISABLE_GX_COPY_GUEST_ADDRESS_CONTRACT)
#define PORPOISE_STUB_DISABLE_GX_COPY_DISP_GUEST_ADDRESS_CONTRACT
#define PORPOISE_STUB_DISABLE_GX_COPY_TEX_GUEST_ADDRESS_CONTRACT
#endif

#ifndef PORPOISE_STUB_DISABLE_GX_COPY_DISP_GUEST_ADDRESS_CONTRACT
#define LIBPORPOISE_GX_COPY_DISP_GUEST_ADDRESS_API_VERSION 1
#endif
#ifndef PORPOISE_STUB_DISABLE_GX_COPY_TEX_GUEST_ADDRESS_CONTRACT
#define LIBPORPOISE_GX_COPY_TEX_GUEST_ADDRESS_API_VERSION 1
#endif
#define LIBPORPOISE_DOLPHIN_GX_HOST_ARRAY_H 1

typedef f32 Mtx[3][4];
typedef f32 Mtx23[2][3];
typedef f32 Mtx44[4][4];

typedef u32 GXFogType;
typedef u32 GXIndTexAlphaSel;
typedef u32 GXIndTexBiasSel;
typedef u32 GXIndTexFormat;
typedef u32 GXIndTexMtxID;
typedef u32 GXIndTexStageID;
typedef u32 GXIndTexWrap;
typedef u32 GXProjectionType;
typedef s32 GXPrimitive;
typedef s32 GXAttrType;
typedef s32 GXCompCnt;
typedef s32 GXCompType;
typedef s32 GXVtxFmt;
typedef s32 GXCompare;
typedef s32 GXGamma;
typedef s32 GXPixelFmt;
typedef s32 GXZFmt16;
typedef s32 GXTevMode;
typedef s32 GXTexCoordID;
typedef s32 GXBlendMode;
typedef s32 GXBlendFactor;
typedef s32 GXLogicOp;
typedef u32 GXTevKColorID;
typedef u32 GXTevRegID;
typedef s32 GXTevStageID;
typedef u32 GXLightID;
typedef s32 GXAttr;
typedef s32 GXChannelID;
typedef u32 GXTexWrapMode;
typedef u32 GXTexFilter;
typedef u32 GXAnisotropy;
typedef s32 GXTexMapID;
typedef u32 GXTlutFmt;
typedef u32 GXTlut;

struct __GXData_struct {
    uint32_t dirtyState;
    GXBool flushReady;
};

extern struct __GXData_struct *gx;
#define GX_CHECK_FLUSH(gx_value) ((gx_value)->flushReady)
void __GXSetDirtyState(void);
void __GXSendFlushPrim(void);

typedef enum GXTexFmt {
    GX_TF_I4 = 0x0,
    GX_TF_I8 = 0x1,
    GX_TF_IA4 = 0x2,
    GX_TF_IA8 = 0x3,
    GX_TF_RGB565 = 0x4,
    GX_TF_RGB5A3 = 0x5,
    GX_TF_RGBA8 = 0x6,
    GX_TF_C4 = 0x8,
    GX_TF_C8 = 0x9,
    GX_TF_C14X2 = 0xA,
    GX_TF_CMPR = 0xE,
    GX_TF_Z8 = 0x11,
    GX_TF_Z16 = 0x13,
    GX_TF_Z24X8 = 0x16,
    GX_CTF_R4 = 0x20,
    GX_CTF_RA4 = 0x22,
    GX_CTF_RA8 = 0x23,
    GX_CTF_YUVA8 = 0x26,
#ifdef PORPOISE_STUB_SHIFTED_GX_COPY_FORMATS
    GX_CTF_A8 = 0x26,
    GX_CTF_R8 = 0x27,
    GX_CTF_G8 = 0x28,
    GX_CTF_B8 = 0x29,
    GX_CTF_RG8 = 0x2A,
    GX_CTF_GB8 = 0x2B,
#else
    GX_CTF_A8 = 0x27,
    GX_CTF_R8 = 0x28,
    GX_CTF_G8 = 0x29,
    GX_CTF_B8 = 0x2A,
    GX_CTF_RG8 = 0x2B,
    GX_CTF_GB8 = 0x2C,
#endif
    GX_CTF_Z4 = 0x30,
    GX_CTF_Z8M = 0x39,
    GX_CTF_Z8L = 0x3A,
    GX_CTF_Z16L = 0x3C
} GXTexFmt;

typedef GXTexFmt GXCITexFmt;

enum {
    GX_VA_POS = 9,
    GX_VA_NRM = 10,
    GX_VA_CLR0 = 11,
    GX_VA_CLR1 = 12,
    GX_VA_TEX0 = 13,
    GX_VA_TEX1 = 14,
    GX_VA_TEX2 = 15,
    GX_VA_TEX3 = 16,
    GX_VA_TEX4 = 17,
    GX_VA_TEX5 = 18,
    GX_VA_TEX6 = 19,
    GX_VA_TEX7 = 20,
    GX_POS_MTX_ARRAY = 21,
    GX_NRM_MTX_ARRAY = 22,
    GX_TEX_MTX_ARRAY = 23,
    GX_LIGHT_ARRAY = 24,
    GX_VA_NBT = 25,

    GX_COLOR0 = 0,
    GX_COLOR1 = 1,
    GX_ALPHA0 = 2,
    GX_ALPHA1 = 3,
    GX_COLOR0A0 = 4,
    GX_COLOR1A1 = 5,

    GX_CLAMP = 0,
    GX_REPEAT = 1,
    GX_MIRROR = 2,

    GX_NEAR = 0,
    GX_LINEAR = 1,
    GX_NEAR_MIP_NEAR = 2,
    GX_LIN_MIP_NEAR = 3,
    GX_NEAR_MIP_LIN = 4,
    GX_LIN_MIP_LIN = 5,

    GX_ANISO_1 = 0,
    GX_ANISO_2 = 1,
    GX_ANISO_4 = 2,
    GX_MAX_ANISOTROPY = 3,

    GX_TEXMAP0 = 0,
    GX_TEXMAP1 = 1,
    GX_TEXMAP2 = 2,
    GX_TEXMAP3 = 3,
    GX_TEXMAP4 = 4,
    GX_TEXMAP5 = 5,
    GX_TEXMAP6 = 6,
    GX_TEXMAP7 = 7,
    GX_MAX_TEXMAP = 8,

    GX_TL_IA8 = 0,
    GX_TL_RGB565 = 1,
    GX_TL_RGB5A3 = 2,
    GX_MAX_TLUTFMT = 3,
    GX_TLUT0 = 0,
    GX_MAX_TLUT = 16,
    GX_BIGTLUT0 = 16,
    GX_BIGTLUT3 = 19,
    GX_MAX_TLUT_ALL = 20
};

#ifndef PORPOISE_STUB_SPLIT_GX_HEADER_VIEW
typedef void (*GXDrawDoneCallback)(void);
#endif

typedef enum GXTexMtxType {
    GX_MTX3x4 = 0,
    GX_MTX2x4 = 1
} GXTexMtxType;

typedef struct GXColor {
    u8 r;
    u8 g;
    u8 b;
    u8 a;
} GXColor;

typedef struct GXColorS10 {
    s16 r;
    s16 g;
    s16 b;
    s16 a;
} GXColorS10;

typedef struct GXFogAdjTable {
    u16 fogVals[10];
} GXFogAdjTable;

typedef struct GXLightObj {
    u8 bytes[0x40];
} GXLightObj;

typedef struct GXLightObjPriv {
    u32 reserved[3];
    GXColor color;
    f32 a[3];
    f32 k[3];
    f32 lpos[3];
    f32 ldir[3];
} GXLightObjPriv;

typedef struct GXTexObj {
    u8 bytes[0x20];
} GXTexObj;

typedef struct GXTlutObj {
    u8 bytes[0x0C];
} GXTlutObj;

GXFifoObj *GXInit(void *base, u32 size);
#ifndef PORPOISE_STUB_SPLIT_GX_HEADER_VIEW
GXDrawDoneCallback GXSetDrawDoneCallback(GXDrawDoneCallback callback);
void GXDrawDone(void);
#endif
void GXSetCopyFilter(
    GXBool use_aa,
    const u8 sample_pattern[12][2],
    GXBool use_vertical_filter,
    const u8 vertical_filter[7]);
void GXSetCopyClear(GXColor clear_color, u32 clear_z);
void GXSetDispCopyDst(u16 width, u16 height);
void GXSetTexCopyDst(
    u16 width,
    u16 height,
    GXTexFmt format,
    GXBool mipmap);
void GXCopyDisp(void *destination, GXBool clear);
void GXCopyTex(void *destination, GXBool clear);
#ifndef PORPOISE_STUB_DISABLE_GX_COPY_DISP_GUEST_ADDRESS_CONTRACT
GXBool GXHostCopyDispGuestAddress(u32 destination, GXBool clear);
#endif
#ifndef PORPOISE_STUB_DISABLE_GX_COPY_TEX_GUEST_ADDRESS_CONTRACT
GXBool GXHostCopyTexGuestAddress(u32 destination, GXBool clear);
#endif
void GXLoadLightObjImm(const GXLightObj *light, GXLightID id);
void GXSetArrayCanonicalSized(
    GXAttr attr,
    void *base,
    u32 size,
    u8 stride);
u32 GXGetTexBufferSize(
    u16 width,
    u16 height,
    u32 format,
    GXBool mipmap,
    u8 max_lod);
void GXInitTexObj(
    GXTexObj *object,
    void *image,
    u16 width,
    u16 height,
    GXTexFmt format,
    GXTexWrapMode wrap_s,
    GXTexWrapMode wrap_t,
    GXBool mipmap);
void GXInitTexObjCI(
    GXTexObj *object,
    void *image,
    u16 width,
    u16 height,
    GXCITexFmt format,
    GXTexWrapMode wrap_s,
    GXTexWrapMode wrap_t,
    GXBool mipmap,
    u32 tlut_name);
void GXInitTexObjLOD(
    GXTexObj *object,
    GXTexFilter min_filter,
    GXTexFilter mag_filter,
    f32 min_lod,
    f32 max_lod,
    f32 lod_bias,
    GXBool bias_clamp,
    GXBool edge_lod,
    GXAnisotropy max_aniso);
void GXLoadTexObj(GXTexObj *object, GXTexMapID map_id);
void GXInitTlutObj(
    GXTlutObj *object,
    void *table,
    GXTlutFmt format,
    u16 entries);
void GXLoadTlut(GXTlutObj *object, GXTlut name);
void GXSetChanAmbColor(GXChannelID channel, GXColor color);
void GXSetChanMatColor(GXChannelID channel, GXColor color);
void GXCallDisplayList(const void *list, u32 nbytes);
void GXSetProjection(const Mtx44 matrix, GXProjectionType type);
void GXGetProjectionv(f32 *projection);
void GXLoadPosMtxImm(const Mtx matrix, u32 id);
void GXLoadNrmMtxImm(const Mtx matrix, u32 id);
void GXLoadTexMtxImm(const Mtx matrix, u32 id, GXTexMtxType type);
void GXGetViewportv(f32 *viewport);
void GXSetIndTexMtx(
    GXIndTexMtxID id,
    const Mtx23 matrix,
    s8 scale);
void GXSetTevColor(GXTevRegID id, GXColor color);
void GXSetTevColorS10(GXTevRegID id, GXColorS10 color);
void GXSetTevKColor(GXTevKColorID id, GXColor color);
void GXSetFog(
    GXFogType type,
    f32 start_z,
    f32 end_z,
    f32 near_z,
    f32 far_z,
    GXColor color);
void GXSetFogRangeAdj(
    GXBool enable,
    u16 center,
    GXFogAdjTable *table);
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
    GXIndTexAlphaSel alpha);
void GXClearVtxDesc(void);
void GXSetVtxDesc(GXAttr attr, GXAttrType type);
void GXSetVtxAttrFmt(
    GXVtxFmt format,
    GXAttr attr,
    GXCompCnt count,
    GXCompType type,
    u8 fraction);
void GXInvalidateVtxCache(void);
void GXSetNumTexGens(u8 count);
void GXSetNumChans(u8 count);
void GXInvalidateTexAll(void);
void GXSetTevOp(GXTevStageID stage, GXTevMode mode);
void GXSetTevOrder(
    GXTevStageID stage,
    GXTexCoordID coordinate,
    GXTexMapID map,
    GXChannelID color);
void GXSetNumTevStages(u8 count);
void GXSetColorUpdate(u8 enable);
void GXSetZMode(u8 compare, GXCompare function, u8 update);
void GXSetPixelFmt(GXPixelFmt pixel_format, GXZFmt16 depth_format);
void GXSetViewport(
    f32 left,
    f32 top,
    f32 width,
    f32 height,
    f32 near_z,
    f32 far_z);
void GXSetScissor(u32 left, u32 top, u32 width, u32 height);
void GXSetDispCopySrc(u16 left, u16 top, u16 width, u16 height);
f32 GXGetYScaleFactor(u16 efb_height, u16 xfb_height);
u32 GXSetDispCopyYScale(f32 scale);
void GXSetDispCopyGamma(GXGamma gamma);

#ifdef __cplusplus
}
#endif

#endif
