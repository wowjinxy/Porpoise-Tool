#include "porpoise_libporpoise_builtins_private.h"
#include "porpoise_libporpoise_private.h"

#include "porpoise_libporpoise_gx_headers.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PORPOISE_GUEST_GX_LIGHT_OBJECT_SIZE 0x40U
#define PORPOISE_GX_COPY_ALIGNMENT 32U
#define PORPOISE_GX_COPY_ADDRESS_PROBE_SIZE 1U
#define PORPOISE_GX_MAX_COPY_DIMENSION 1024U

typedef struct PorpoiseGxTextureBlockLayout {
    uint32_t format;
    uint8_t width;
    uint8_t height;
    uint8_t bytes;
} PorpoiseGxTextureBlockLayout;

static const PorpoiseGxTextureBlockLayout porpoise_gx_texture_layouts[] = {
    {UINT32_C(0x00), 8U, 8U, 32U},
    {UINT32_C(0x01), 8U, 4U, 32U},
    {UINT32_C(0x02), 8U, 4U, 32U},
    {UINT32_C(0x03), 4U, 4U, 32U},
    {UINT32_C(0x04), 4U, 4U, 32U},
    {UINT32_C(0x05), 4U, 4U, 32U},
    {UINT32_C(0x06), 4U, 4U, 64U},
    {UINT32_C(0x08), 8U, 8U, 32U},
    {UINT32_C(0x09), 8U, 4U, 32U},
    {UINT32_C(0x0A), 4U, 4U, 32U},
    {UINT32_C(0x0E), 8U, 8U, 32U},
    {UINT32_C(0x11), 8U, 4U, 32U},
    {UINT32_C(0x13), 4U, 4U, 32U},
    {UINT32_C(0x16), 4U, 4U, 64U},
    {UINT32_C(0x20), 8U, 8U, 32U},
    {UINT32_C(0x22), 8U, 4U, 32U},
    {UINT32_C(0x23), 4U, 4U, 32U},
    {UINT32_C(0x26), 4U, 4U, 64U},
    {UINT32_C(0x27), 8U, 4U, 32U},
    {UINT32_C(0x28), 8U, 4U, 32U},
    {UINT32_C(0x29), 8U, 4U, 32U},
    {UINT32_C(0x2A), 8U, 4U, 32U},
    {UINT32_C(0x2B), 4U, 4U, 32U},
    {UINT32_C(0x2C), 4U, 4U, 32U},
    {UINT32_C(0x30), 8U, 8U, 32U},
    {UINT32_C(0x39), 8U, 4U, 32U},
    {UINT32_C(0x3A), 8U, 4U, 32U},
    {UINT32_C(0x3C), 4U, 4U, 32U}
};

static uint32_t porpoise_gx_read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24U) |
           ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) |
           (uint32_t)bytes[3];
}

static float porpoise_gx_read_be_float(const uint8_t *bytes)
{
    uint32_t bits = porpoise_gx_read_be32(bytes);
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int porpoise_gx_read_bool(
    PorpoisePpcState *state,
    unsigned int register_index,
    const char *message,
    GXBool *value_out)
{
    uint8_t value = (uint8_t)state->gpr[register_index];

    if (value_out == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "GX boolean validation has no result output");
        return 0;
    }
    if (value > 1U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            state->gpr[register_index],
            message);
        return 0;
    }
    *value_out = value != 0U ? GX_TRUE : GX_FALSE;
    return 1;
}

static int porpoise_gx_texture_copy_size(
    PorpoisePpcState *state,
    const PorpoiseLibporpoiseGxCopyDestination *destination,
    size_t *size_out)
{
    size_t index;

    if (destination == NULL || size_out == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "GX texture-copy size calculation has no destination");
        return 0;
    }
    for (index = 0U;
         index < sizeof(porpoise_gx_texture_layouts) /
                     sizeof(porpoise_gx_texture_layouts[0]);
         index++) {
        const PorpoiseGxTextureBlockLayout *layout =
            &porpoise_gx_texture_layouts[index];
        if (layout->format == destination->format) {
            uint64_t columns =
                ((uint64_t)destination->width + layout->width - 1U) /
                layout->width;
            uint64_t rows =
                ((uint64_t)destination->height + layout->height - 1U) /
                layout->height;
            uint64_t size = columns * rows * layout->bytes;

            if (size == 0U || size > UINT32_MAX || size > SIZE_MAX) {
                porpoise_state_set_fault(
                    state,
                    PORPOISE_FAULT_ADDRESS_OVERFLOW,
                    state->gpr[3],
                    "GX texture-copy destination size overflows");
                return 0;
            }
            *size_out = (size_t)size;
            return 1;
        }
    }
    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_INVALID_ARGUMENT,
        destination->format,
        "GX texture-copy destination format is unsupported");
    return 0;
}

static int porpoise_gx_native_texture_copy_format(
    PorpoisePpcState *state,
    uint32_t guest_format,
    GXTexFmt *native_format_out)
{
    GXTexFmt native_format;

    if (native_format_out == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state->pc,
            "GX texture-copy format conversion has no result output");
        return 0;
    }

    switch (guest_format) {
        case UINT32_C(0x00):
            native_format = (GXTexFmt)GX_TF_I4;
            break;
        case UINT32_C(0x01):
            native_format = (GXTexFmt)GX_TF_I8;
            break;
        case UINT32_C(0x02):
            native_format = (GXTexFmt)GX_TF_IA4;
            break;
        case UINT32_C(0x03):
            native_format = (GXTexFmt)GX_TF_IA8;
            break;
        case UINT32_C(0x04):
            native_format = (GXTexFmt)GX_TF_RGB565;
            break;
        case UINT32_C(0x05):
            native_format = (GXTexFmt)GX_TF_RGB5A3;
            break;
        case UINT32_C(0x06):
            native_format = (GXTexFmt)GX_TF_RGBA8;
            break;
        case UINT32_C(0x08):
            native_format = (GXTexFmt)GX_TF_C4;
            break;
        case UINT32_C(0x09):
            native_format = (GXTexFmt)GX_TF_C8;
            break;
        case UINT32_C(0x0A):
            native_format = (GXTexFmt)GX_TF_C14X2;
            break;
        case UINT32_C(0x0E):
            native_format = (GXTexFmt)GX_TF_CMPR;
            break;
        case UINT32_C(0x11):
            native_format = (GXTexFmt)GX_TF_Z8;
            break;
        case UINT32_C(0x13):
            native_format = (GXTexFmt)GX_TF_Z16;
            break;
        case UINT32_C(0x16):
            native_format = (GXTexFmt)GX_TF_Z24X8;
            break;
        case UINT32_C(0x20):
            native_format = (GXTexFmt)GX_CTF_R4;
            break;
        case UINT32_C(0x22):
            native_format = (GXTexFmt)GX_CTF_RA4;
            break;
        case UINT32_C(0x23):
            native_format = (GXTexFmt)GX_CTF_RA8;
            break;
        case UINT32_C(0x26):
            if ((uint32_t)GX_CTF_YUVA8 == (uint32_t)GX_CTF_A8) {
                porpoise_state_set_fault(
                    state,
                    PORPOISE_FAULT_UNSUPPORTED_OPERATION,
                    guest_format,
                    "native GX header aliases YUVA8 and A8 copy formats");
                return 0;
            }
            native_format = (GXTexFmt)GX_CTF_YUVA8;
            break;
        case UINT32_C(0x27):
            native_format = (GXTexFmt)GX_CTF_A8;
            break;
        case UINT32_C(0x28):
            native_format = (GXTexFmt)GX_CTF_R8;
            break;
        case UINT32_C(0x29):
            native_format = (GXTexFmt)GX_CTF_G8;
            break;
        case UINT32_C(0x2A):
            native_format = (GXTexFmt)GX_CTF_B8;
            break;
        case UINT32_C(0x2B):
            native_format = (GXTexFmt)GX_CTF_RG8;
            break;
        case UINT32_C(0x2C):
            native_format = (GXTexFmt)GX_CTF_GB8;
            break;
        case UINT32_C(0x30):
            native_format = (GXTexFmt)GX_CTF_Z4;
            break;
        case UINT32_C(0x39):
            native_format = (GXTexFmt)GX_CTF_Z8M;
            break;
        case UINT32_C(0x3A):
            native_format = (GXTexFmt)GX_CTF_Z8L;
            break;
        case UINT32_C(0x3C):
            native_format = (GXTexFmt)GX_CTF_Z16L;
            break;
        default:
            porpoise_state_set_fault(
                state,
                PORPOISE_FAULT_INVALID_ARGUMENT,
                guest_format,
                "GX texture-copy destination format is unsupported");
            return 0;
    }

    *native_format_out = native_format;
    return 1;
}

void porpoise_libporpoise_gx_init_adapter(PorpoisePpcState *state)
{
    uint32_t guest_base;
    uint32_t size;
    uint32_t guest_token;
    void *native_base;
    GXFifoObj *native_fifo;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    guest_base = state->gpr[3];
    size = state->gpr[4];
    native_base = NULL;
    if (!porpoise_libporpoise_gx_init_begin(
            state, guest_base, size, &native_base)) {
        return;
    }

    native_fifo = GXInit(native_base, (u32)size);
    guest_token = 0U;
    if (!porpoise_libporpoise_gx_init_commit(
            state, native_fifo, &guest_token)) {
        return;
    }
    state->gpr[3] = guest_token;
}

void porpoise_libporpoise_gx_draw_done_adapter(PorpoisePpcState *state)
{
    if (state == NULL || porpoise_state_should_stop(state) ||
        !porpoise_libporpoise_gx_complete_draw(state)) {
        return;
    }

    if (!porpoise_state_should_stop(state)) {
        (void)porpoise_poll_host_events(state, state->pc);
    }
}

void porpoise_libporpoise_gx_set_draw_done_callback_adapter(
    PorpoisePpcState *state)
{
    uint32_t previous_callback;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    previous_callback = 0U;
    if (!porpoise_libporpoise_gx_set_draw_done_callback(
            state, state->gpr[3], &previous_callback)) {
        return;
    }
    state->gpr[3] = previous_callback;
}

void porpoise_libporpoise_gx_set_copy_filter_adapter(
    PorpoisePpcState *state)
{
    uint8_t sample_pattern[12][2];
    uint8_t vertical_filter[7];
    GXBool use_aa;
    GXBool use_vertical_filter;
    uint32_t guest_sample_pattern;
    uint32_t guest_vertical_filter;
    const u8 (*native_sample_pattern)[2];
    const u8 *native_vertical_filter;

    if (state == NULL || porpoise_state_should_stop(state) ||
        !porpoise_libporpoise_gx_require_active(state)) {
        return;
    }
    if (!porpoise_gx_read_bool(
            state, 3U, "GXSetCopyFilter has an invalid AA flag", &use_aa) ||
        !porpoise_gx_read_bool(
            state,
            5U,
            "GXSetCopyFilter has an invalid vertical-filter flag",
            &use_vertical_filter)) {
        return;
    }

    guest_sample_pattern = state->gpr[4];
    guest_vertical_filter = state->gpr[6];
    native_sample_pattern = NULL;
    native_vertical_filter = NULL;
    if (use_aa != GX_FALSE) {
        if (!porpoise_libporpoise_gx_read_span(
                state,
                guest_sample_pattern,
                sizeof(sample_pattern),
                1U,
                sample_pattern,
                "GXSetCopyFilter AA sample pattern is NULL")) {
            return;
        }
        native_sample_pattern =
            (const u8 (*)[2])(const void *)sample_pattern;
    }
    if (use_vertical_filter != GX_FALSE) {
        if (!porpoise_libporpoise_gx_read_span(
                state,
                guest_vertical_filter,
                sizeof(vertical_filter),
                1U,
                vertical_filter,
                "GXSetCopyFilter vertical filter is NULL")) {
            return;
        }
        native_vertical_filter = vertical_filter;
    }
    GXSetCopyFilter(
        use_aa,
        native_sample_pattern,
        use_vertical_filter,
        native_vertical_filter);
}

void porpoise_libporpoise_gx_set_copy_clear_adapter(
    PorpoisePpcState *state)
{
    uint8_t guest_color[4];
    GXColor native_color;
    uint32_t clear_z;

    if (state == NULL || porpoise_state_should_stop(state) ||
        !porpoise_libporpoise_gx_require_active(state)) {
        return;
    }
    clear_z = state->gpr[4];
    if (clear_z > UINT32_C(0x00FFFFFF)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            clear_z,
            "GXSetCopyClear depth exceeds the 24-bit GX range");
        return;
    }
    if (!porpoise_libporpoise_gx_read_span(
            state,
            state->gpr[3],
            sizeof(guest_color),
            4U,
            guest_color,
            "GXSetCopyClear hidden GXColor pointer is NULL")) {
        return;
    }
    native_color.r = guest_color[0];
    native_color.g = guest_color[1];
    native_color.b = guest_color[2];
    native_color.a = guest_color[3];
    GXSetCopyClear(native_color, (u32)clear_z);
}

void porpoise_libporpoise_gx_set_disp_copy_dst_adapter(
    PorpoisePpcState *state)
{
    uint16_t width;
    uint16_t height;

    if (state == NULL || porpoise_state_should_stop(state) ||
        !porpoise_libporpoise_gx_require_active(state)) {
        return;
    }
    width = (uint16_t)state->gpr[3];
    height = (uint16_t)state->gpr[4];
    if (width == 0U || height == 0U ||
        width > PORPOISE_GX_MAX_COPY_DIMENSION ||
        height > PORPOISE_GX_MAX_COPY_DIMENSION ||
        (width & UINT16_C(15)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            width == 0U || width > PORPOISE_GX_MAX_COPY_DIMENSION ||
                    (width & UINT16_C(15)) != 0U
                ? (uint32_t)width
                : (uint32_t)height,
            "GXSetDispCopyDst requires nonzero dimensions at most 1024 and a 16-pixel-aligned width");
        return;
    }

    /* Commit the private mirror before entering the void native call. The
     * record helper can still fault (for example after context divergence),
     * and native GX must not be mutated in that case. */
    if (!porpoise_libporpoise_gx_record_disp_copy_destination(
            state, width, height)) {
        return;
    }
    GXSetDispCopyDst((u16)width, (u16)height);
}

void porpoise_libporpoise_gx_set_tex_copy_dst_adapter(
    PorpoisePpcState *state)
{
    PorpoiseLibporpoiseGxCopyDestination destination;
    GXBool mipmap;
    GXTexFmt native_format;
    size_t ignored_size;

    if (state == NULL || porpoise_state_should_stop(state) ||
        !porpoise_libporpoise_gx_require_active(state)) {
        return;
    }
    destination.width = (uint16_t)state->gpr[3];
    destination.height = (uint16_t)state->gpr[4];
    destination.format = state->gpr[5];
    destination.mipmap = 0U;
    if (destination.width == 0U || destination.height == 0U ||
        destination.width > PORPOISE_GX_MAX_COPY_DIMENSION ||
        destination.height > PORPOISE_GX_MAX_COPY_DIMENSION) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            destination.width == 0U ||
                    destination.width > PORPOISE_GX_MAX_COPY_DIMENSION
                ? (uint32_t)destination.width
                : (uint32_t)destination.height,
            "GXSetTexCopyDst requires nonzero dimensions at most 1024");
        return;
    }
    if (!porpoise_gx_read_bool(
            state,
            6U,
            "GXSetTexCopyDst has an invalid mipmap flag",
            &mipmap)) {
        return;
    }
    destination.mipmap = (uint8_t)mipmap;
    ignored_size = 0U;
    if (!porpoise_gx_texture_copy_size(
            state, &destination, &ignored_size) ||
        !porpoise_gx_native_texture_copy_format(
            state, destination.format, &native_format)) {
        return;
    }

    if (!porpoise_libporpoise_gx_record_tex_copy_destination(
            state,
            destination.width,
            destination.height,
            destination.format,
            destination.mipmap)) {
        return;
    }
    GXSetTexCopyDst(
        (u16)destination.width,
        (u16)destination.height,
        native_format,
        mipmap);
}

void porpoise_libporpoise_gx_copy_disp_adapter(PorpoisePpcState *state)
{
    GXBool clear;
    void *native_destination;

    if (state == NULL || porpoise_state_should_stop(state) ||
        !porpoise_gx_read_bool(
            state, 4U, "GXCopyDisp has an invalid clear flag", &clear)) {
        return;
    }

    /* GXCopyDisp's complete span is derived from native GX source and Y-scale
     * state, not GXSetDispCopyDst's retained height. Tool can safely validate
     * only the exact aligned origin. The guest-address endpoint owns native-
     * state span derivation and must reject an incomplete mapping. */
    native_destination = NULL;
    if (!porpoise_libporpoise_gx_decode_span(
            state,
            state->gpr[3],
            PORPOISE_GX_COPY_ADDRESS_PROBE_SIZE,
            PORPOISE_GX_COPY_ALIGNMENT,
            &native_destination,
            "GXCopyDisp destination is NULL")) {
        return;
    }
#if defined(LIBPORPOISE_GX_COPY_DISP_GUEST_ADDRESS_API_VERSION) && \
    LIBPORPOISE_GX_COPY_DISP_GUEST_ADDRESS_API_VERSION >= 1
    (void)native_destination;
    if (GXHostCopyDispGuestAddress((u32)state->gpr[3], clear) == GX_FALSE) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_HOST_IO,
            state->gpr[3],
            "libPorpoise rejected the GXCopyDisp guest-address request");
    }
#else
    (void)native_destination;
    (void)clear;
    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        state->gpr[3],
        "libPorpoise does not advertise the GXCopyDisp guest-address contract");
#endif
}

void porpoise_libporpoise_gx_copy_tex_adapter(PorpoisePpcState *state)
{
    PorpoiseLibporpoiseGxCopyDestination destination;
    GXBool clear;
    void *native_destination;
    size_t size;

    if (state == NULL || porpoise_state_should_stop(state) ||
        !porpoise_libporpoise_gx_get_tex_copy_destination(
            state, &destination) ||
        !porpoise_gx_read_bool(
            state, 4U, "GXCopyTex has an invalid clear flag", &clear) ||
        !porpoise_gx_texture_copy_size(state, &destination, &size)) {
        return;
    }
    native_destination = NULL;
    if (!porpoise_libporpoise_gx_decode_span(
            state,
            state->gpr[3],
            size,
            PORPOISE_GX_COPY_ALIGNMENT,
            &native_destination,
            "GXCopyTex destination is NULL")) {
        return;
    }
#if defined(LIBPORPOISE_GX_COPY_TEX_GUEST_ADDRESS_API_VERSION) && \
    LIBPORPOISE_GX_COPY_TEX_GUEST_ADDRESS_API_VERSION >= 1
    (void)native_destination;
    if (GXHostCopyTexGuestAddress((u32)state->gpr[3], clear) == GX_FALSE) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_HOST_IO,
            state->gpr[3],
            "libPorpoise rejected the GXCopyTex guest-address request");
    }
#else
    (void)native_destination;
    (void)clear;
    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_UNSUPPORTED_OPERATION,
        state->gpr[3],
        "libPorpoise does not advertise the GXCopyTex guest-address contract");
#endif
}

void porpoise_libporpoise_gx_load_light_obj_imm_adapter(
    PorpoisePpcState *state)
{
    uint8_t guest_light[PORPOISE_GUEST_GX_LIGHT_OBJECT_SIZE];
    GXLightObjPriv native_light;
    uint32_t light_id;
    size_t index;

    if (state == NULL || porpoise_state_should_stop(state) ||
        !porpoise_libporpoise_gx_require_active(state)) {
        return;
    }
    light_id = state->gpr[4];
    if (light_id == 0U || light_id > UINT32_C(0x80) ||
        (light_id & (light_id - 1U)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            light_id,
            "GXLoadLightObjImm requires exactly one GX light ID bit");
        return;
    }
    if (!porpoise_libporpoise_gx_read_span(
            state,
            state->gpr[3],
            sizeof(guest_light),
            4U,
            guest_light,
            "GXLoadLightObjImm light object is NULL")) {
        return;
    }

    memset(&native_light, 0, sizeof(native_light));
    for (index = 0U; index < 3U; index++) {
        native_light.reserved[index] = porpoise_gx_read_be32(
            &guest_light[index * 4U]);
    }
    native_light.color.r = guest_light[0x0CU];
    native_light.color.g = guest_light[0x0DU];
    native_light.color.b = guest_light[0x0EU];
    native_light.color.a = guest_light[0x0FU];
    for (index = 0U; index < 3U; index++) {
        native_light.a[index] = porpoise_gx_read_be_float(
            &guest_light[0x10U + index * 4U]);
        native_light.k[index] = porpoise_gx_read_be_float(
            &guest_light[0x1CU + index * 4U]);
        native_light.lpos[index] = porpoise_gx_read_be_float(
            &guest_light[0x28U + index * 4U]);
        native_light.ldir[index] = porpoise_gx_read_be_float(
            &guest_light[0x34U + index * 4U]);
    }
    GXLoadLightObjImm(
        (GXLightObj *)(void *)&native_light,
        (GXLightID)light_id);
}
