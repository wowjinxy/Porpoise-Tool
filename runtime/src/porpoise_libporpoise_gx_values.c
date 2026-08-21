#include "porpoise_libporpoise_builtins_private.h"
#include "porpoise_libporpoise_private.h"

#include "porpoise_libporpoise_gx_headers.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    PORPOISE_GX_COLOR_SIZE = 4,
    PORPOISE_GX_COLOR_S10_SIZE = 8,
    PORPOISE_GX_FOG_ADJ_TABLE_SIZE = 20,
    PORPOISE_GX_INDIRECT_MATRIX_SIZE = 24,
    PORPOISE_GX_MATRIX_2X4_SIZE = 32,
    PORPOISE_GX_MATRIX_3X4_SIZE = 48,
    PORPOISE_GX_MATRIX_4X4_SIZE = 64,
    PORPOISE_GX_PROJECTION_VECTOR_SIZE = 28,
    PORPOISE_GX_VIEWPORT_VECTOR_SIZE = 24,
    PORPOISE_GX_TEV_INDIRECT_STACK_SIZE = 8
};

static uint16_t porpoise_gx_load_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) |
                      (uint16_t)bytes[1]);
}

static uint32_t porpoise_gx_load_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static void porpoise_gx_store_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static float porpoise_gx_decode_f32(const uint8_t *bytes)
{
    uint32_t bits = porpoise_gx_load_be32(bytes);
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void porpoise_gx_encode_f32(uint8_t *bytes, float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    porpoise_gx_store_be32(bytes, bits);
}

static int porpoise_gx_read_f32_values(
    PorpoisePpcState *state,
    uint32_t guest_address,
    size_t count,
    float *values,
    const char *null_description)
{
    uint8_t raw[PORPOISE_GX_MATRIX_4X4_SIZE];
    size_t index;
    size_t size;

    if (count == 0U ||
        count > sizeof(raw) / sizeof(uint32_t) ||
        values == NULL) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_STATE,
            state != NULL ? state->pc : 0U,
            "invalid private GX floating-point span request");
        return 0;
    }
    size = count * sizeof(uint32_t);
    if (!porpoise_libporpoise_gx_read_span(
            state,
            guest_address,
            size,
            sizeof(uint32_t),
            raw,
            null_description)) {
        return 0;
    }
    for (index = 0U; index < count; index++) {
        values[index] = porpoise_gx_decode_f32(
            &raw[index * sizeof(uint32_t)]);
    }
    return 1;
}

static void porpoise_gx_write_f32_values(
    void *native_destination,
    const float *values,
    size_t count)
{
    uint8_t raw[PORPOISE_GX_MATRIX_4X4_SIZE] = {0};
    size_t index;

    for (index = 0U; index < count; index++) {
        porpoise_gx_encode_f32(
            &raw[index * sizeof(uint32_t)], values[index]);
    }
    memcpy(native_destination, raw, count * sizeof(uint32_t));
}

static int porpoise_gx_read_color(
    PorpoisePpcState *state,
    uint32_t guest_address,
    GXColor *color,
    const char *null_description)
{
    uint8_t raw[PORPOISE_GX_COLOR_SIZE];

    if (!porpoise_libporpoise_gx_read_span(
            state,
            guest_address,
            sizeof(raw),
            1U,
            raw,
            null_description)) {
        return 0;
    }
    color->r = raw[0];
    color->g = raw[1];
    color->b = raw[2];
    color->a = raw[3];
    return 1;
}

void porpoise_libporpoise_gx_call_display_list_adapter(
    PorpoisePpcState *state)
{
    uint32_t guest_list;
    uint32_t nbytes;
    void *native_list;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state)) {
        return;
    }
    guest_list = state->gpr[3];
    nbytes = state->gpr[4];
    if (nbytes == 0U || (nbytes & UINT32_C(31)) != 0U) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            nbytes,
            "GXCallDisplayList byte count must be a nonzero multiple of 32");
        return;
    }
    native_list = NULL;
    if (!porpoise_libporpoise_gx_decode_span(
            state,
            guest_list,
            (size_t)nbytes,
            32U,
            &native_list,
            "GXCallDisplayList guest display list is NULL")) {
        return;
    }
    GXCallDisplayList(native_list, (u32)nbytes);
}

void porpoise_libporpoise_gx_set_projection_adapter(
    PorpoisePpcState *state)
{
    Mtx44 matrix = {{0.0f}};

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state) ||
        !porpoise_gx_read_f32_values(
            state,
            state->gpr[3],
            PORPOISE_GX_MATRIX_4X4_SIZE / sizeof(float),
            &matrix[0][0],
            "GXSetProjection guest matrix is NULL")) {
        return;
    }
    GXSetProjection(
        (const f32 (*)[4])matrix,
        (GXProjectionType)state->gpr[4]);
}

void porpoise_libporpoise_gx_get_projectionv_adapter(
    PorpoisePpcState *state)
{
    f32 projection[PORPOISE_GX_PROJECTION_VECTOR_SIZE / sizeof(f32)];
    void *native_destination;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state)) {
        return;
    }
    native_destination = NULL;
    if (!porpoise_libporpoise_gx_decode_span(
            state,
            state->gpr[3],
            PORPOISE_GX_PROJECTION_VECTOR_SIZE,
            sizeof(uint32_t),
            &native_destination,
            "GXGetProjectionv guest output is NULL")) {
        return;
    }
    GXGetProjectionv(projection);
    porpoise_gx_write_f32_values(
        native_destination,
        projection,
        sizeof(projection) / sizeof(projection[0]));
}

void porpoise_libporpoise_gx_load_pos_mtx_imm_adapter(
    PorpoisePpcState *state)
{
    Mtx matrix = {{0.0f}};

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state) ||
        !porpoise_gx_read_f32_values(
            state,
            state->gpr[3],
            PORPOISE_GX_MATRIX_3X4_SIZE / sizeof(float),
            &matrix[0][0],
            "GXLoadPosMtxImm guest matrix is NULL")) {
        return;
    }
    GXLoadPosMtxImm((const f32 (*)[4])matrix, (u32)state->gpr[4]);
}

void porpoise_libporpoise_gx_load_nrm_mtx_imm_adapter(
    PorpoisePpcState *state)
{
    Mtx matrix = {{0.0f}};

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state) ||
        !porpoise_gx_read_f32_values(
            state,
            state->gpr[3],
            PORPOISE_GX_MATRIX_3X4_SIZE / sizeof(float),
            &matrix[0][0],
            "GXLoadNrmMtxImm guest matrix is NULL")) {
        return;
    }
    GXLoadNrmMtxImm((const f32 (*)[4])matrix, (u32)state->gpr[4]);
}

void porpoise_libporpoise_gx_load_tex_mtx_imm_adapter(
    PorpoisePpcState *state)
{
    Mtx matrix = {{0.0f}};
    uint32_t type;
    size_t size;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state)) {
        return;
    }
    type = state->gpr[5];
    if (type == (uint32_t)GX_MTX2x4) {
        size = PORPOISE_GX_MATRIX_2X4_SIZE;
    } else if (type == (uint32_t)GX_MTX3x4) {
        size = PORPOISE_GX_MATRIX_3X4_SIZE;
    } else {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_INVALID_ARGUMENT,
            type,
            "GXLoadTexMtxImm matrix type must be GX_MTX2x4 or GX_MTX3x4");
        return;
    }
    memset(matrix, 0, sizeof(matrix));
    if (!porpoise_gx_read_f32_values(
            state,
            state->gpr[3],
            size / sizeof(float),
            &matrix[0][0],
            "GXLoadTexMtxImm guest matrix is NULL")) {
        return;
    }
    GXLoadTexMtxImm(
        (const f32 (*)[4])matrix,
        (u32)state->gpr[4],
        (GXTexMtxType)type);
}

void porpoise_libporpoise_gx_get_viewportv_adapter(
    PorpoisePpcState *state)
{
    f32 viewport[PORPOISE_GX_VIEWPORT_VECTOR_SIZE / sizeof(f32)];
    void *native_destination;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state)) {
        return;
    }
    native_destination = NULL;
    if (!porpoise_libporpoise_gx_decode_span(
            state,
            state->gpr[3],
            PORPOISE_GX_VIEWPORT_VECTOR_SIZE,
            sizeof(uint32_t),
            &native_destination,
            "GXGetViewportv guest output is NULL")) {
        return;
    }
    GXGetViewportv(viewport);
    porpoise_gx_write_f32_values(
        native_destination,
        viewport,
        sizeof(viewport) / sizeof(viewport[0]));
}

void porpoise_libporpoise_gx_set_ind_tex_mtx_adapter(
    PorpoisePpcState *state)
{
    Mtx23 matrix = {{0.0f}};

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state) ||
        !porpoise_gx_read_f32_values(
            state,
            state->gpr[4],
            PORPOISE_GX_INDIRECT_MATRIX_SIZE / sizeof(float),
            &matrix[0][0],
            "GXSetIndTexMtx guest matrix is NULL")) {
        return;
    }
    GXSetIndTexMtx(
        (GXIndTexMtxID)state->gpr[3],
        (const f32 (*)[3])matrix,
        (s8)(uint8_t)state->gpr[5]);
}

void porpoise_libporpoise_gx_set_tev_color_adapter(
    PorpoisePpcState *state)
{
    GXColor color;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state) ||
        !porpoise_gx_read_color(
            state,
            state->gpr[4],
            &color,
            "GXSetTevColor guest color is NULL")) {
        return;
    }
    GXSetTevColor((GXTevRegID)state->gpr[3], color);
}

void porpoise_libporpoise_gx_set_tev_color_s10_adapter(
    PorpoisePpcState *state)
{
    uint8_t raw[PORPOISE_GX_COLOR_S10_SIZE];
    GXColorS10 color;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state) ||
        !porpoise_libporpoise_gx_read_span(
            state,
            state->gpr[4],
            sizeof(raw),
            sizeof(uint16_t),
            raw,
            "GXSetTevColorS10 guest color is NULL")) {
        return;
    }
    color.r = (s16)porpoise_gx_load_be16(&raw[0]);
    color.g = (s16)porpoise_gx_load_be16(&raw[2]);
    color.b = (s16)porpoise_gx_load_be16(&raw[4]);
    color.a = (s16)porpoise_gx_load_be16(&raw[6]);
    GXSetTevColorS10((GXTevRegID)state->gpr[3], color);
}

void porpoise_libporpoise_gx_set_tev_kcolor_adapter(
    PorpoisePpcState *state)
{
    GXColor color;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state) ||
        !porpoise_gx_read_color(
            state,
            state->gpr[4],
            &color,
            "GXSetTevKColor guest color is NULL")) {
        return;
    }
    GXSetTevKColor((GXTevKColorID)state->gpr[3], color);
}

void porpoise_libporpoise_gx_set_fog_adapter(
    PorpoisePpcState *state)
{
    GXColor color;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state) ||
        !porpoise_gx_read_color(
            state,
            state->gpr[4],
            &color,
            "GXSetFog guest color is NULL")) {
        return;
    }
    GXSetFog(
        (GXFogType)state->gpr[3],
        (f32)porpoise_fpr_get_f64(state, 1U, 0U),
        (f32)porpoise_fpr_get_f64(state, 2U, 0U),
        (f32)porpoise_fpr_get_f64(state, 3U, 0U),
        (f32)porpoise_fpr_get_f64(state, 4U, 0U),
        color);
}

void porpoise_libporpoise_gx_set_fog_range_adj_adapter(
    PorpoisePpcState *state)
{
    uint8_t raw[PORPOISE_GX_FOG_ADJ_TABLE_SIZE];
    GXFogAdjTable table;
    unsigned int index;
    GXBool enable;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state)) {
        return;
    }
    enable = (GXBool)((uint8_t)state->gpr[3] != 0U);
    if (enable != GX_FALSE) {
        if (!porpoise_libporpoise_gx_read_span(
                state,
                state->gpr[5],
                sizeof(raw),
                sizeof(uint16_t),
                raw,
                "GXSetFogRangeAdj enabled guest table is NULL")) {
            return;
        }
        for (index = 0U; index < 10U; index++) {
            table.fogVals[index] = porpoise_gx_load_be16(
                &raw[index * sizeof(uint16_t)]);
        }
        GXSetFogRangeAdj(enable, (u16)state->gpr[4], &table);
        return;
    }
    GXSetFogRangeAdj(GX_FALSE, (u16)state->gpr[4], NULL);
}

void porpoise_libporpoise_gx_set_tev_indirect_adapter(
    PorpoisePpcState *state)
{
    uint8_t stack_arguments[PORPOISE_GX_TEV_INDIRECT_STACK_SIZE];
    uint32_t stack_address;
    uint32_t ind_lod;
    uint32_t alpha_sel;

    if (state == NULL || porpoise_state_should_stop(state)) {
        return;
    }
    if (!porpoise_libporpoise_gx_require_active(state)) {
        return;
    }
    if (state->gpr[1] > UINT32_MAX - UINT32_C(15)) {
        porpoise_state_set_fault(
            state,
            PORPOISE_FAULT_ADDRESS_OVERFLOW,
            state->gpr[1],
            "GXSetTevIndirect guest overflow-argument area wraps");
        return;
    }
    stack_address = state->gpr[1] + UINT32_C(8);
    if (!porpoise_libporpoise_gx_read_span(
            state,
            stack_address,
            sizeof(stack_arguments),
            sizeof(uint32_t),
            stack_arguments,
            "GXSetTevIndirect guest overflow-argument area is NULL")) {
        return;
    }
    ind_lod = porpoise_gx_load_be32(&stack_arguments[0]);
    alpha_sel = porpoise_gx_load_be32(&stack_arguments[4]);
    GXSetTevIndirect(
        (GXTevStageID)state->gpr[3],
        (GXIndTexStageID)state->gpr[4],
        (GXIndTexFormat)state->gpr[5],
        (GXIndTexBiasSel)state->gpr[6],
        (GXIndTexMtxID)state->gpr[7],
        (GXIndTexWrap)state->gpr[8],
        (GXIndTexWrap)state->gpr[9],
        (GXBool)((uint8_t)state->gpr[10] != 0U),
        (GXBool)((uint8_t)ind_lod != 0U),
        (GXIndTexAlphaSel)alpha_sel);
}
