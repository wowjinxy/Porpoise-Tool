#include <dolphin/gx.h>
#include <porpoise/gx_objects_stub.h>

#include <string.h>

static PorpoiseStubGxObjectsSnapshot snapshot;

void PorpoiseStubGxObjectsReset(void)
{
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.texture_size_result = 32U;
}

void PorpoiseStubGxObjectsSetTextureSize(uint32_t size)
{
    snapshot.texture_size_result = size;
}

const PorpoiseStubGxObjectsSnapshot *PorpoiseStubGxObjectsGet(void)
{
    return &snapshot;
}

void GXSetArrayCanonicalSized(
    GXAttr attr,
    void *base,
    u32 size,
    u8 stride)
{
    snapshot.set_array_count++;
    snapshot.array_attribute = (uint32_t)attr;
    snapshot.array_base = base;
    snapshot.array_size = (uint32_t)size;
    snapshot.array_stride = (uint32_t)stride;
}

u32 GXGetTexBufferSize(
    u16 width,
    u16 height,
    u32 format,
    GXBool mipmap,
    u8 max_lod)
{
    snapshot.get_texture_size_count++;
    snapshot.texture_size_width = (uint32_t)width;
    snapshot.texture_size_height = (uint32_t)height;
    snapshot.texture_size_format = (uint32_t)format;
    snapshot.texture_size_mipmap = (uint32_t)mipmap;
    snapshot.texture_size_max_lod = (uint32_t)max_lod;
    return (u32)snapshot.texture_size_result;
}

static void record_texture_init(
    GXTexObj *object,
    void *image,
    u16 width,
    u16 height,
    uint32_t format,
    GXTexWrapMode wrap_s,
    GXTexWrapMode wrap_t,
    GXBool mipmap)
{
    snapshot.texture_object = object;
    snapshot.texture_image = image;
    snapshot.texture_width = (uint32_t)width;
    snapshot.texture_height = (uint32_t)height;
    snapshot.texture_format = format;
    snapshot.texture_wrap_s = (uint32_t)wrap_s;
    snapshot.texture_wrap_t = (uint32_t)wrap_t;
    snapshot.texture_mipmap = (uint32_t)mipmap;
}

void GXInitTexObj(
    GXTexObj *object,
    void *image,
    u16 width,
    u16 height,
    GXTexFmt format,
    GXTexWrapMode wrap_s,
    GXTexWrapMode wrap_t,
    GXBool mipmap)
{
    snapshot.init_texture_count++;
    record_texture_init(
        object,
        image,
        width,
        height,
        (uint32_t)format,
        wrap_s,
        wrap_t,
        mipmap);
}

void GXInitTexObjCI(
    GXTexObj *object,
    void *image,
    u16 width,
    u16 height,
    GXCITexFmt format,
    GXTexWrapMode wrap_s,
    GXTexWrapMode wrap_t,
    GXBool mipmap,
    u32 tlut_name)
{
    snapshot.init_ci_texture_count++;
    record_texture_init(
        object,
        image,
        width,
        height,
        (uint32_t)format,
        wrap_s,
        wrap_t,
        mipmap);
    snapshot.texture_tlut_name = (uint32_t)tlut_name;
}

void GXInitTexObjLOD(
    GXTexObj *object,
    GXTexFilter min_filter,
    GXTexFilter mag_filter,
    f32 min_lod,
    f32 max_lod,
    f32 lod_bias,
    GXBool bias_clamp,
    GXBool edge_lod,
    GXAnisotropy max_aniso)
{
    snapshot.init_lod_count++;
    snapshot.texture_object = object;
    snapshot.texture_min_filter = (uint32_t)min_filter;
    snapshot.texture_mag_filter = (uint32_t)mag_filter;
    snapshot.texture_min_lod = min_lod;
    snapshot.texture_max_lod = max_lod;
    snapshot.texture_lod_bias = lod_bias;
    snapshot.texture_bias_clamp = (uint32_t)bias_clamp;
    snapshot.texture_edge_lod = (uint32_t)edge_lod;
    snapshot.texture_max_aniso = (uint32_t)max_aniso;
}

void GXInitTexObjUserData(GXTexObj *object, void *user_data)
{
    snapshot.init_user_data_count++;
    snapshot.texture_object = object;
    snapshot.texture_user_data = user_data;
}

void GXLoadTexObj(GXTexObj *object, GXTexMapID map_id)
{
    snapshot.load_texture_count++;
    snapshot.texture_object = object;
    snapshot.texture_map_id = (uint32_t)map_id;
}

void GXInitTlutObj(
    GXTlutObj *object,
    void *table,
    GXTlutFmt format,
    u16 entries)
{
    snapshot.init_tlut_count++;
    snapshot.tlut_object = object;
    snapshot.tlut_table = table;
    snapshot.tlut_format = (uint32_t)format;
    snapshot.tlut_entries = (uint32_t)entries;
}

void GXLoadTlut(GXTlutObj *object, GXTlut name)
{
    snapshot.load_tlut_count++;
    snapshot.tlut_object = object;
    snapshot.tlut_name = (uint32_t)name;
}

static void record_color(GXChannelID channel, GXColor color)
{
    snapshot.color_channel = (uint32_t)channel;
    snapshot.color[0] = color.r;
    snapshot.color[1] = color.g;
    snapshot.color[2] = color.b;
    snapshot.color[3] = color.a;
}

void GXSetChanAmbColor(GXChannelID channel, GXColor color)
{
    snapshot.ambient_color_count++;
    record_color(channel, color);
}

void GXSetChanMatColor(GXChannelID channel, GXColor color)
{
    snapshot.material_color_count++;
    record_color(channel, color);
}
