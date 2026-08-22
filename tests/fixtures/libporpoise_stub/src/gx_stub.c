#include <dolphin/gx.h>
#include <porpoise/stub.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PORPOISE_STUB_GUEST_RAM_END UINT32_C(0x81800000)

struct GXFifoObj {
    uint8_t opaque[128];
};

static struct GXFifoObj unencodable_fifo;
static unsigned int gx_init_call_count;
static void *gx_init_last_base;
static u32 gx_init_last_size;
static int gx_init_result_kind = PORPOISE_STUB_GX_INIT_TOKEN_RESULT;
static GXDrawDoneCallback draw_done_callback;
static unsigned int draw_done_setter_call_count;
static unsigned int draw_done_call_count;
static unsigned int copy_filter_call_count;
static GXBool copy_filter_use_aa;
static GXBool copy_filter_use_vertical;
static u8 copy_filter_sample[24];
static u8 copy_filter_vertical[7];
static unsigned int copy_clear_call_count;
static GXColor copy_clear_color;
static u32 copy_clear_depth;
static unsigned int set_disp_copy_dst_call_count;
static u16 disp_copy_width;
static u16 disp_copy_height;
static unsigned int set_tex_copy_dst_call_count;
static u16 tex_copy_width;
static u16 tex_copy_height;
static GXTexFmt tex_copy_format;
static GXBool tex_copy_mipmap;
static unsigned int copy_disp_call_count;
static unsigned int copy_tex_call_count;
static unsigned int copy_disp_guest_address_call_count;
static unsigned int copy_tex_guest_address_call_count;
static u32 copy_disp_guest_address;
static u32 copy_tex_guest_address;
static GXBool copy_disp_guest_address_clear;
static GXBool copy_tex_guest_address_clear;
static GXBool copy_disp_guest_address_result = GX_TRUE;
static GXBool copy_tex_guest_address_result = GX_TRUE;
static u32 copy_disp_guest_address_required_span;
static unsigned int copy_disp_accepted_call_count;
static u32 copy_disp_accepted_guest_address;
static GXBool copy_disp_accepted_clear;
static unsigned int load_light_call_count;
static GXLightObj load_light;
static GXLightID load_light_id;

static void foreign_draw_done_callback(void)
{
}

GXFifoObj *GXInit(void *base, u32 size)
{
    gx_init_call_count++;
    gx_init_last_base = base;
    gx_init_last_size = size;
    draw_done_callback = NULL;

    switch (gx_init_result_kind) {
        case PORPOISE_STUB_GX_INIT_NULL_RESULT:
            return NULL;
        case PORPOISE_STUB_GX_INIT_UNENCODABLE_RESULT:
            return &unencodable_fifo;
        case PORPOISE_STUB_GX_INIT_MAPPED_RESULT:
            return (GXFifoObj *)base;
        case PORPOISE_STUB_GX_INIT_TOKEN_RESULT:
        default:
            return (GXFifoObj *)PorpoiseStubNativePointer();
    }
}

void PorpoiseStubGXInitReset(void)
{
    gx_init_call_count = 0U;
    gx_init_last_base = NULL;
    gx_init_last_size = 0U;
    gx_init_result_kind = PORPOISE_STUB_GX_INIT_TOKEN_RESULT;
}

void PorpoiseStubGXInitSetResult(int result_kind)
{
    gx_init_result_kind = result_kind;
}

unsigned int PorpoiseStubGXInitCallCount(void)
{
    return gx_init_call_count;
}

const void *PorpoiseStubGXInitLastBase(void)
{
    return gx_init_last_base;
}

uint32_t PorpoiseStubGXInitLastSize(void)
{
    return (uint32_t)gx_init_last_size;
}

GXDrawDoneCallback GXSetDrawDoneCallback(GXDrawDoneCallback callback)
{
    GXDrawDoneCallback previous = draw_done_callback;
    draw_done_setter_call_count++;
    draw_done_callback = callback;
    return previous;
}

void GXDrawDone(void)
{
    draw_done_call_count++;
    if (draw_done_callback != NULL) draw_done_callback();
}

void GXSetCopyFilter(
    GXBool use_aa,
    const u8 sample_pattern[12][2],
    GXBool use_vertical_filter,
    const u8 vertical_filter[7])
{
    copy_filter_call_count++;
    copy_filter_use_aa = use_aa;
    copy_filter_use_vertical = use_vertical_filter;
    memset(copy_filter_sample, 0, sizeof(copy_filter_sample));
    memset(copy_filter_vertical, 0, sizeof(copy_filter_vertical));
    if (sample_pattern != NULL) {
        memcpy(copy_filter_sample, sample_pattern, sizeof(copy_filter_sample));
    }
    if (vertical_filter != NULL) {
        memcpy(copy_filter_vertical, vertical_filter, sizeof(copy_filter_vertical));
    }
}

void GXSetCopyClear(GXColor clear_color, u32 clear_z)
{
    copy_clear_call_count++;
    copy_clear_color = clear_color;
    copy_clear_depth = clear_z;
}

void GXSetDispCopyDst(u16 width, u16 height)
{
    set_disp_copy_dst_call_count++;
    disp_copy_width = width;
    disp_copy_height = height;
}

void GXSetTexCopyDst(
    u16 width,
    u16 height,
    GXTexFmt format,
    GXBool mipmap)
{
    set_tex_copy_dst_call_count++;
    tex_copy_width = width;
    tex_copy_height = height;
    tex_copy_format = format;
    tex_copy_mipmap = mipmap;
}

void GXCopyDisp(void *destination, GXBool clear)
{
    copy_disp_call_count++;
    (void)destination;
    (void)clear;
}

void GXCopyTex(void *destination, GXBool clear)
{
    copy_tex_call_count++;
    (void)destination;
    (void)clear;
}

#ifndef PORPOISE_STUB_DISABLE_GX_COPY_DISP_GUEST_ADDRESS_CONTRACT
GXBool GXHostCopyDispGuestAddress(u32 destination, GXBool clear)
{
    copy_disp_guest_address_call_count++;
    copy_disp_guest_address = destination;
    copy_disp_guest_address_clear = clear;
    if (copy_disp_guest_address_required_span != 0U &&
        (destination >= PORPOISE_STUB_GUEST_RAM_END ||
         copy_disp_guest_address_required_span >
             PORPOISE_STUB_GUEST_RAM_END - destination)) {
        return GX_FALSE;
    }
    if (copy_disp_guest_address_result == GX_FALSE) {
        return GX_FALSE;
    }
    copy_disp_accepted_call_count++;
    copy_disp_accepted_guest_address = destination;
    copy_disp_accepted_clear = clear;
    return GX_TRUE;
}
#endif

#ifndef PORPOISE_STUB_DISABLE_GX_COPY_TEX_GUEST_ADDRESS_CONTRACT
GXBool GXHostCopyTexGuestAddress(u32 destination, GXBool clear)
{
    copy_tex_guest_address_call_count++;
    copy_tex_guest_address = destination;
    copy_tex_guest_address_clear = clear;
    return copy_tex_guest_address_result;
}
#endif

void GXLoadLightObjImm(const GXLightObj *light, GXLightID id)
{
    load_light_call_count++;
    memset(&load_light, 0, sizeof(load_light));
    if (light != NULL) {
        memcpy(&load_light, light, sizeof(load_light));
    }
    load_light_id = id;
}

void PorpoiseStubGXBoundaryReset(void)
{
    draw_done_setter_call_count = 0U;
    draw_done_call_count = 0U;
    copy_filter_call_count = 0U;
    copy_filter_use_aa = GX_FALSE;
    copy_filter_use_vertical = GX_FALSE;
    memset(copy_filter_sample, 0, sizeof(copy_filter_sample));
    memset(copy_filter_vertical, 0, sizeof(copy_filter_vertical));
    copy_clear_call_count = 0U;
    memset(&copy_clear_color, 0, sizeof(copy_clear_color));
    copy_clear_depth = 0U;
    set_disp_copy_dst_call_count = 0U;
    disp_copy_width = 0U;
    disp_copy_height = 0U;
    set_tex_copy_dst_call_count = 0U;
    tex_copy_width = 0U;
    tex_copy_height = 0U;
    tex_copy_format = 0U;
    tex_copy_mipmap = GX_FALSE;
    copy_disp_call_count = 0U;
    copy_tex_call_count = 0U;
    copy_disp_guest_address_call_count = 0U;
    copy_tex_guest_address_call_count = 0U;
    copy_disp_guest_address = 0U;
    copy_tex_guest_address = 0U;
    copy_disp_guest_address_clear = GX_FALSE;
    copy_tex_guest_address_clear = GX_FALSE;
    copy_disp_guest_address_result = GX_TRUE;
    copy_tex_guest_address_result = GX_TRUE;
    copy_disp_guest_address_required_span = 0U;
    copy_disp_accepted_call_count = 0U;
    copy_disp_accepted_guest_address = 0U;
    copy_disp_accepted_clear = GX_FALSE;
    load_light_call_count = 0U;
    memset(&load_light, 0, sizeof(load_light));
    load_light_id = 0U;
}

unsigned int PorpoiseStubGXDrawDoneSetterCallCount(void)
{
    return draw_done_setter_call_count;
}

unsigned int PorpoiseStubGXDrawDoneCallCount(void)
{
    return draw_done_call_count;
}

void PorpoiseStubGXSetForeignDrawDoneCallback(int enabled)
{
    draw_done_callback = enabled ? foreign_draw_done_callback : NULL;
}

void PorpoiseStubGXTriggerDrawDone(void)
{
    if (draw_done_callback != NULL) {
        draw_done_callback();
    }
}

unsigned int PorpoiseStubGXCopyFilterCallCount(void)
{
    return copy_filter_call_count;
}

uint32_t PorpoiseStubGXCopyFilterUseAA(void)
{
    return (uint32_t)copy_filter_use_aa;
}

uint32_t PorpoiseStubGXCopyFilterUseVertical(void)
{
    return (uint32_t)copy_filter_use_vertical;
}

uint8_t PorpoiseStubGXCopyFilterSample(unsigned int index)
{
    return index < sizeof(copy_filter_sample)
               ? copy_filter_sample[index]
               : UINT8_C(0);
}

uint8_t PorpoiseStubGXCopyFilterVertical(unsigned int index)
{
    return index < sizeof(copy_filter_vertical)
               ? copy_filter_vertical[index]
               : UINT8_C(0);
}

unsigned int PorpoiseStubGXCopyClearCallCount(void)
{
    return copy_clear_call_count;
}

uint32_t PorpoiseStubGXCopyClearColor(void)
{
    return ((uint32_t)copy_clear_color.r << 24U) |
           ((uint32_t)copy_clear_color.g << 16U) |
           ((uint32_t)copy_clear_color.b << 8U) |
           (uint32_t)copy_clear_color.a;
}

uint32_t PorpoiseStubGXCopyClearDepth(void)
{
    return copy_clear_depth;
}

unsigned int PorpoiseStubGXSetDispCopyDstCallCount(void)
{
    return set_disp_copy_dst_call_count;
}

uint32_t PorpoiseStubGXDispCopyWidth(void)
{
    return (uint32_t)disp_copy_width;
}

uint32_t PorpoiseStubGXDispCopyHeight(void)
{
    return (uint32_t)disp_copy_height;
}

unsigned int PorpoiseStubGXSetTexCopyDstCallCount(void)
{
    return set_tex_copy_dst_call_count;
}

uint32_t PorpoiseStubGXTexCopyWidth(void)
{
    return (uint32_t)tex_copy_width;
}

uint32_t PorpoiseStubGXTexCopyHeight(void)
{
    return (uint32_t)tex_copy_height;
}

uint32_t PorpoiseStubGXTexCopyFormat(void)
{
    return (uint32_t)tex_copy_format;
}

uint32_t PorpoiseStubGXTexCopyMipmap(void)
{
    return (uint32_t)tex_copy_mipmap;
}

unsigned int PorpoiseStubGXCopyDispCallCount(void)
{
    return copy_disp_call_count;
}

unsigned int PorpoiseStubGXCopyTexCallCount(void)
{
    return copy_tex_call_count;
}

void PorpoiseStubGXSetGuestAddressCopyResults(
    int display_result,
    int texture_result)
{
    copy_disp_guest_address_result =
        display_result != 0 ? GX_TRUE : GX_FALSE;
    copy_tex_guest_address_result =
        texture_result != 0 ? GX_TRUE : GX_FALSE;
}

void PorpoiseStubGXSetGuestAddressDisplayCopySpan(uint32_t required_bytes)
{
    copy_disp_guest_address_required_span = (u32)required_bytes;
}

unsigned int PorpoiseStubGXCopyDispGuestAddressCallCount(void)
{
    return copy_disp_guest_address_call_count;
}

unsigned int PorpoiseStubGXCopyTexGuestAddressCallCount(void)
{
    return copy_tex_guest_address_call_count;
}

uint32_t PorpoiseStubGXCopyDispGuestAddress(void)
{
    return (uint32_t)copy_disp_guest_address;
}

uint32_t PorpoiseStubGXCopyTexGuestAddress(void)
{
    return (uint32_t)copy_tex_guest_address;
}

uint32_t PorpoiseStubGXCopyDispGuestAddressClearFlag(void)
{
    return (uint32_t)copy_disp_guest_address_clear;
}

uint32_t PorpoiseStubGXCopyTexGuestAddressClearFlag(void)
{
    return (uint32_t)copy_tex_guest_address_clear;
}

unsigned int PorpoiseStubGXCopyDispAcceptedCallCount(void)
{
    return copy_disp_accepted_call_count;
}

uint32_t PorpoiseStubGXCopyDispAcceptedGuestAddress(void)
{
    return (uint32_t)copy_disp_accepted_guest_address;
}

uint32_t PorpoiseStubGXCopyDispAcceptedClearFlag(void)
{
    return (uint32_t)copy_disp_accepted_clear;
}

unsigned int PorpoiseStubGXLoadLightCallCount(void)
{
    return load_light_call_count;
}

uint32_t PorpoiseStubGXLoadLightId(void)
{
    return (uint32_t)load_light_id;
}

uint8_t PorpoiseStubGXLoadLightByte(unsigned int index)
{
    return index < sizeof(load_light.bytes)
               ? load_light.bytes[index]
               : UINT8_C(0);
}
