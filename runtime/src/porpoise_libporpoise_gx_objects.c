#include "porpoise_libporpoise_builtins_private.h"
#include "porpoise_libporpoise_private.h"

#include "porpoise_libporpoise_gx_headers.h"

#include <stdint.h>

#if !defined(LIBPORPOISE_DOLPHIN_GX_HOST_ARRAY_H)
#error "Porpoise GX object adapters require libPorpoise GXHostArray.h"
#endif

enum {
    PORPOISE_GUEST_GX_TEX_OBJ_SIZE = 0x20,
    PORPOISE_GUEST_GX_TLUT_OBJ_SIZE = 0x0C,
    PORPOISE_GUEST_GX_COLOR_SIZE = 4
};

typedef struct PorpoiseGuestGxTexObj {
    uint32_t mode0;
    uint32_t mode1;
    uint32_t image0;
    uint32_t image3;
    uint32_t user_data;
    uint32_t format;
    uint32_t tlut_name;
    uint16_t load_count;
    uint8_t load_format;
    uint8_t flags;
} PorpoiseGuestGxTexObj;

typedef union PorpoiseNativeGxTexObjStorage {
    GXTexObj object;
    uint32_t alignment;
} PorpoiseNativeGxTexObjStorage;

typedef union PorpoiseNativeGxTlutObjStorage {
    GXTlutObj object;
    uint32_t alignment;
} PorpoiseNativeGxTlutObjStorage;

static uint16_t porpoise_gx_read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) |
                      (uint16_t)bytes[1]);
}

static uint32_t porpoise_gx_read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static int porpoise_gx_invalid_argument(
    PorpoisePpcState *state,
    uint32_t value,
    const char *message)
{
    porpoise_state_set_fault(
        state,
        PORPOISE_FAULT_INVALID_ARGUMENT,
        value,
        message);
    return 0;
}

static int porpoise_gx_texture_format_is_valid(uint32_t format)
{
    switch (format) {
        case GX_TF_I4:
        case GX_TF_I8:
        case GX_TF_IA4:
        case GX_TF_IA8:
        case GX_TF_RGB565:
        case GX_TF_RGB5A3:
        case GX_TF_RGBA8:
        case GX_TF_C4:
        case GX_TF_C8:
        case GX_TF_C14X2:
        case GX_TF_CMPR:
        case GX_TF_Z8:
        case GX_TF_Z16:
        case GX_TF_Z24X8:
            return 1;
        default:
            return 0;
    }
}

static int porpoise_gx_texture_format_is_ci(uint32_t format)
{
    return format == GX_TF_C4 || format == GX_TF_C8 ||
           format == GX_TF_C14X2;
}

static int porpoise_gx_decode_min_filter(
    PorpoisePpcState *state,
    uint32_t mode0,
    GXTexFilter *filter_out)
{
    static const GXTexFilter filters[8] = {
        GX_NEAR,
        GX_NEAR_MIP_NEAR,
        GX_NEAR_MIP_LIN,
        GX_NEAR,
        GX_LINEAR,
        GX_LIN_MIP_NEAR,
        GX_LIN_MIP_LIN,
        GX_NEAR
    };
    uint32_t encoded = (mode0 >> 5) & UINT32_C(7);

    if (filter_out == NULL) {
        return porpoise_gx_invalid_argument(
            state,
            state->pc,
            "GX texture filter decode has no output");
    }
    if (encoded == 3U || encoded == 7U) {
        return porpoise_gx_invalid_argument(
            state,
            encoded,
            "guest GXTexObj has an invalid minimum-filter encoding");
    }
    *filter_out = filters[encoded];
    return 1;
}

static void porpoise_gx_decode_tex_obj(
    const uint8_t bytes[PORPOISE_GUEST_GX_TEX_OBJ_SIZE],
    PorpoiseGuestGxTexObj *object)
{
    object->mode0 = porpoise_gx_read_be32(&bytes[0x00]);
    object->mode1 = porpoise_gx_read_be32(&bytes[0x04]);
    object->image0 = porpoise_gx_read_be32(&bytes[0x08]);
    object->image3 = porpoise_gx_read_be32(&bytes[0x0C]);
    object->user_data = porpoise_gx_read_be32(&bytes[0x10]);
    object->format = porpoise_gx_read_be32(&bytes[0x14]);
    object->tlut_name = porpoise_gx_read_be32(&bytes[0x18]);
    object->load_count = porpoise_gx_read_be16(&bytes[0x1C]);
    object->load_format = bytes[0x1E];
    object->flags = bytes[0x1F];
}

void porpoise_libporpoise_gx_set_array_adapter(PorpoisePpcState *state)
{
    uint32_t attribute;
    uint32_t guest_base;
    uint32_t stride;
    uint32_t mapped_size;
    void *native_base;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state)) {
        return;
    }

    attribute = state->gpr[3];
    guest_base = state->gpr[4];
    stride = state->gpr[5];
    if (!((attribute >= (uint32_t)GX_VA_POS &&
           attribute <= (uint32_t)GX_LIGHT_ARRAY) ||
          attribute == (uint32_t)GX_VA_NBT)) {
        (void)porpoise_gx_invalid_argument(
            state,
            attribute,
            "GXSetArray received an invalid vertex attribute");
        return;
    }
    if (stride > UINT32_C(0xFF)) {
        (void)porpoise_gx_invalid_argument(
            state,
            stride,
            "GXSetArray stride does not fit the console u8 ABI");
        return;
    }

    native_base = NULL;
    mapped_size = 0U;
    if (!porpoise_libporpoise_gx_decode_mapped_tail(
            state,
            guest_base,
            &native_base,
            &mapped_size)) {
        return;
    }

    GXSetArrayCanonicalSized(
        (GXAttr)attribute,
        native_base,
        (u32)mapped_size,
        (u8)stride);
}

void porpoise_libporpoise_gx_load_tex_obj_adapter(PorpoisePpcState *state)
{
    uint8_t bytes[PORPOISE_GUEST_GX_TEX_OBJ_SIZE];
    PorpoiseGuestGxTexObj guest_object;
    PorpoiseNativeGxTexObjStorage native_storage;
    GXTexFilter min_filter;
    GXTexFilter mag_filter;
    uint32_t guest_object_address;
    uint32_t map_id;
    uint32_t width;
    uint32_t height;
    uint32_t wrap_s;
    uint32_t wrap_t;
    uint32_t mipmap;
    uint32_t max_lod_integer;
    uint32_t image_address;
    uint32_t image_size;
    uint32_t max_aniso;
    int is_ci;
    int8_t lod_bias_encoded;
    void *native_image;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state)) {
        return;
    }

    guest_object_address = state->gpr[3];
    map_id = state->gpr[4];
    if (map_id >= (uint32_t)GX_MAX_TEXMAP) {
        (void)porpoise_gx_invalid_argument(
            state,
            map_id,
            "GXLoadTexObj received an invalid texture map ID");
        return;
    }
    if (!porpoise_libporpoise_gx_read_span(
            state,
            guest_object_address,
            sizeof(bytes),
            4U,
            bytes,
            "GXLoadTexObj guest GXTexObj pointer is NULL")) {
        return;
    }
    porpoise_gx_decode_tex_obj(bytes, &guest_object);

    if (!porpoise_gx_texture_format_is_valid(guest_object.format)) {
        (void)porpoise_gx_invalid_argument(
            state,
            guest_object.format,
            "guest GXTexObj has an invalid texture format");
        return;
    }
    if (((guest_object.image0 >> 20) & UINT32_C(0xF)) !=
        (guest_object.format & UINT32_C(0xF))) {
        (void)porpoise_gx_invalid_argument(
            state,
            guest_object.format,
            "guest GXTexObj format metadata disagrees with its image register");
        return;
    }
    wrap_s = guest_object.mode0 & UINT32_C(3);
    wrap_t = (guest_object.mode0 >> 2) & UINT32_C(3);
    if (wrap_s > (uint32_t)GX_MIRROR || wrap_t > (uint32_t)GX_MIRROR) {
        (void)porpoise_gx_invalid_argument(
            state,
            guest_object.mode0,
            "guest GXTexObj has an invalid wrap mode");
        return;
    }
    if ((guest_object.flags & UINT8_C(0xFC)) != 0U) {
        (void)porpoise_gx_invalid_argument(
            state,
            guest_object.flags,
            "guest GXTexObj has unsupported flag bits");
        return;
    }

    is_ci = porpoise_gx_texture_format_is_ci(guest_object.format);
    if (is_ci != ((guest_object.flags & UINT8_C(2)) == 0U)) {
        (void)porpoise_gx_invalid_argument(
            state,
            guest_object.flags,
            "guest GXTexObj CI metadata disagrees with its format");
        return;
    }
    if (is_ci && guest_object.tlut_name >= (uint32_t)GX_MAX_TLUT_ALL) {
        (void)porpoise_gx_invalid_argument(
            state,
            guest_object.tlut_name,
            "guest GXTexObj has an invalid TLUT name");
        return;
    }
    if (guest_object.user_data != 0U) {
        (void)porpoise_gx_invalid_argument(
            state,
            guest_object.user_data,
            "nonzero guest GXTexObj user data cannot cross the native callback boundary");
        return;
    }
    if (!porpoise_gx_decode_min_filter(
            state,
            guest_object.mode0,
            &min_filter)) {
        return;
    }
    max_aniso = (guest_object.mode0 >> 19) & UINT32_C(3);
    if (max_aniso >= (uint32_t)GX_MAX_ANISOTROPY) {
        (void)porpoise_gx_invalid_argument(
            state,
            max_aniso,
            "guest GXTexObj has an invalid anisotropy value");
        return;
    }

    width = (guest_object.image0 & UINT32_C(0x3FF)) + 1U;
    height = ((guest_object.image0 >> 10) & UINT32_C(0x3FF)) + 1U;
    mipmap = (uint32_t)(guest_object.flags & UINT8_C(1));
    max_lod_integer = ((guest_object.mode1 >> 8) & UINT32_C(0xFF)) / 16U;
    mag_filter = ((guest_object.mode0 >> 4) & UINT32_C(1)) != 0U
                     ? GX_LINEAR
                     : GX_NEAR;
    image_address =
        (guest_object.image3 & UINT32_C(0x001FFFFF)) << 5;
    image_size = (uint32_t)GXGetTexBufferSize(
        (u16)width,
        (u16)height,
        guest_object.format,
        (GXBool)mipmap,
        (u8)max_lod_integer);
    if (image_size == 0U) {
        (void)porpoise_gx_invalid_argument(
            state,
            image_address,
            "guest GXTexObj has an empty or unrepresentable image span");
        return;
    }

    native_image = NULL;
    if (!porpoise_libporpoise_gx_decode_span(
            state,
            image_address,
            (size_t)image_size,
            32U,
            &native_image,
            "guest GXTexObj image pointer is NULL")) {
        return;
    }
    if (is_ci) {
        GXInitTexObjCI(
            &native_storage.object,
            native_image,
            (u16)width,
            (u16)height,
            (GXCITexFmt)guest_object.format,
            (GXTexWrapMode)wrap_s,
            (GXTexWrapMode)wrap_t,
            (GXBool)mipmap,
            (u32)guest_object.tlut_name);
    } else {
        GXInitTexObj(
            &native_storage.object,
            native_image,
            (u16)width,
            (u16)height,
            (GXTexFmt)guest_object.format,
            (GXTexWrapMode)wrap_s,
            (GXTexWrapMode)wrap_t,
            (GXBool)mipmap);
    }

    lod_bias_encoded = (int8_t)((guest_object.mode0 >> 9) & UINT32_C(0xFF));
    GXInitTexObjLOD(
        &native_storage.object,
        min_filter,
        mag_filter,
        (f32)(guest_object.mode1 & UINT32_C(0xFF)) / 16.0f,
        (f32)((guest_object.mode1 >> 8) & UINT32_C(0xFF)) / 16.0f,
        (f32)lod_bias_encoded / 32.0f,
        (GXBool)((guest_object.mode0 >> 21) & UINT32_C(1)),
        (GXBool)(((guest_object.mode0 >> 8) & UINT32_C(1)) == 0U),
        (GXAnisotropy)max_aniso);
    GXLoadTexObj(&native_storage.object, (GXTexMapID)map_id);
}

void porpoise_libporpoise_gx_load_tlut_adapter(PorpoisePpcState *state)
{
    uint8_t bytes[PORPOISE_GUEST_GX_TLUT_OBJ_SIZE];
    PorpoiseNativeGxTlutObjStorage native_storage;
    uint32_t guest_object_address;
    uint32_t tlut_name;
    uint32_t tlut_word;
    uint32_t load_tlut0;
    uint32_t format;
    uint32_t entries;
    uint32_t table_address;
    uint32_t table_size;
    void *native_table;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state)) {
        return;
    }

    guest_object_address = state->gpr[3];
    tlut_name = state->gpr[4];
    if (tlut_name >= (uint32_t)GX_MAX_TLUT_ALL) {
        (void)porpoise_gx_invalid_argument(
            state,
            tlut_name,
            "GXLoadTlut received an invalid TLUT name");
        return;
    }
    if (!porpoise_libporpoise_gx_read_span(
            state,
            guest_object_address,
            sizeof(bytes),
            4U,
            bytes,
            "GXLoadTlut guest GXTlutObj pointer is NULL")) {
        return;
    }

    tlut_word = porpoise_gx_read_be32(&bytes[0x00]);
    load_tlut0 = porpoise_gx_read_be32(&bytes[0x04]);
    entries = (uint32_t)porpoise_gx_read_be16(&bytes[0x08]);
    format = (tlut_word >> 10) & UINT32_C(3);
    if (format >= (uint32_t)GX_MAX_TLUTFMT) {
        (void)porpoise_gx_invalid_argument(
            state,
            format,
            "guest GXTlutObj has an invalid palette format");
        return;
    }
    if (entries == 0U || entries > UINT32_C(0x4000)) {
        (void)porpoise_gx_invalid_argument(
            state,
            entries,
            "guest GXTlutObj has an invalid palette entry count");
        return;
    }

    table_address = (load_tlut0 & UINT32_C(0x001FFFFF)) << 5;
    table_size = entries * UINT32_C(2);
    native_table = NULL;
    if (!porpoise_libporpoise_gx_decode_span(
            state,
            table_address,
            (size_t)table_size,
            32U,
            &native_table,
            "guest GXTlutObj table pointer is NULL")) {
        return;
    }

    GXInitTlutObj(
        &native_storage.object,
        native_table,
        (GXTlutFmt)format,
        (u16)entries);
    GXLoadTlut(&native_storage.object, (GXTlut)tlut_name);
}

static void porpoise_gx_set_channel_color(
    PorpoisePpcState *state,
    int ambient)
{
    uint8_t bytes[PORPOISE_GUEST_GX_COLOR_SIZE];
    GXColor color;
    uint32_t channel;
    uint32_t guest_color;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state)) {
        return;
    }

    channel = state->gpr[3];
    guest_color = state->gpr[4];
    /* The exact KAR bodies silently return for signed-negative and >= 6. */
    if (channel > (uint32_t)GX_COLOR1A1) {
        return;
    }
    if (!porpoise_libporpoise_gx_read_span(
            state,
            guest_color,
            sizeof(bytes),
            4U,
            bytes,
            ambient
                ? "GXSetChanAmbColor guest GXColor pointer is NULL"
                : "GXSetChanMatColor guest GXColor pointer is NULL")) {
        return;
    }

    color.r = bytes[0];
    color.g = bytes[1];
    color.b = bytes[2];
    color.a = bytes[3];
    if (ambient) {
        GXSetChanAmbColor((GXChannelID)channel, color);
    } else {
        GXSetChanMatColor((GXChannelID)channel, color);
    }
}

void porpoise_libporpoise_gx_set_chan_amb_color_adapter(
    PorpoisePpcState *state)
{
    porpoise_gx_set_channel_color(state, 1);
}

void porpoise_libporpoise_gx_set_chan_mat_color_adapter(
    PorpoisePpcState *state)
{
    porpoise_gx_set_channel_color(state, 0);
}
