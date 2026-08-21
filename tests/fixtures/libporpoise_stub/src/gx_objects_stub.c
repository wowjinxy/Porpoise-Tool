#include <dolphin/gx.h>

#include <string.h>

void GXSetArrayCanonicalSized(
    GXAttr attr,
    void *base,
    u32 size,
    u8 stride)
{
    (void)attr;
    (void)base;
    (void)size;
    (void)stride;
}

u32 GXGetTexBufferSize(
    u16 width,
    u16 height,
    u32 format,
    GXBool mipmap,
    u8 max_lod)
{
    (void)width;
    (void)height;
    (void)format;
    (void)mipmap;
    (void)max_lod;
    return 32U;
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
    (void)image;
    (void)width;
    (void)height;
    (void)format;
    (void)wrap_s;
    (void)wrap_t;
    (void)mipmap;
    if (object != NULL) {
        memset(object, 0, sizeof(*object));
    }
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
    (void)tlut_name;
    GXInitTexObj(
        object,
        image,
        width,
        height,
        format,
        wrap_s,
        wrap_t,
        mipmap);
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
    (void)object;
    (void)min_filter;
    (void)mag_filter;
    (void)min_lod;
    (void)max_lod;
    (void)lod_bias;
    (void)bias_clamp;
    (void)edge_lod;
    (void)max_aniso;
}

void GXLoadTexObj(GXTexObj *object, GXTexMapID map_id)
{
    (void)object;
    (void)map_id;
}

void GXInitTlutObj(
    GXTlutObj *object,
    void *table,
    GXTlutFmt format,
    u16 entries)
{
    (void)table;
    (void)format;
    (void)entries;
    if (object != NULL) {
        memset(object, 0, sizeof(*object));
    }
}

void GXLoadTlut(GXTlutObj *object, GXTlut name)
{
    (void)object;
    (void)name;
}

void GXSetChanAmbColor(GXChannelID channel, GXColor color)
{
    (void)channel;
    (void)color;
}

void GXSetChanMatColor(GXChannelID channel, GXColor color)
{
    (void)channel;
    (void)color;
}
