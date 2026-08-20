#ifndef TEST_LIBPORPOISE_GX_OBJECTS_STUB_H
#define TEST_LIBPORPOISE_GX_OBJECTS_STUB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PorpoiseStubGxObjectsSnapshot {
    unsigned int set_array_count;
    uint32_t array_attribute;
    const void *array_base;
    uint32_t array_size;
    uint32_t array_stride;

    unsigned int get_texture_size_count;
    uint32_t texture_size_result;
    uint32_t texture_size_width;
    uint32_t texture_size_height;
    uint32_t texture_size_format;
    uint32_t texture_size_mipmap;
    uint32_t texture_size_max_lod;

    unsigned int init_texture_count;
    unsigned int init_ci_texture_count;
    unsigned int init_lod_count;
    unsigned int init_user_data_count;
    unsigned int load_texture_count;
    const void *texture_object;
    const void *texture_image;
    const void *texture_user_data;
    uint32_t texture_width;
    uint32_t texture_height;
    uint32_t texture_format;
    uint32_t texture_wrap_s;
    uint32_t texture_wrap_t;
    uint32_t texture_mipmap;
    uint32_t texture_tlut_name;
    uint32_t texture_min_filter;
    uint32_t texture_mag_filter;
    float texture_min_lod;
    float texture_max_lod;
    float texture_lod_bias;
    uint32_t texture_bias_clamp;
    uint32_t texture_edge_lod;
    uint32_t texture_max_aniso;
    uint32_t texture_map_id;

    unsigned int init_tlut_count;
    unsigned int load_tlut_count;
    const void *tlut_object;
    const void *tlut_table;
    uint32_t tlut_format;
    uint32_t tlut_entries;
    uint32_t tlut_name;

    unsigned int ambient_color_count;
    unsigned int material_color_count;
    uint32_t color_channel;
    uint8_t color[4];
} PorpoiseStubGxObjectsSnapshot;

void PorpoiseStubGxObjectsReset(void);
void PorpoiseStubGxObjectsSetTextureSize(uint32_t size);
const PorpoiseStubGxObjectsSnapshot *PorpoiseStubGxObjectsGet(void);

#ifdef __cplusplus
}
#endif

#endif
