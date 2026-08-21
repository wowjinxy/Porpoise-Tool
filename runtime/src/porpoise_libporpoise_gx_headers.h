#ifndef PORPOISE_LIBPORPOISE_GX_HEADERS_H
#define PORPOISE_LIBPORPOISE_GX_HEADERS_H

/*
 * Keep the generated runtime on libPorpoise's public, split GX headers.
 * The current umbrella <dolphin/gx.h> also includes GXMisc.h, which declares
 * three SDK-internal functions as static without defining them. GCC 16 reports
 * those declarations at end of translation unit under -Wunused-function, so
 * an include-site diagnostic pragma cannot suppress the resulting warning.
 *
 * The fallback preserves compatibility with the deliberately small contract
 * stubs (and older libPorpoise layouts) that expose only the umbrella header.
 */
#if !defined(PORPOISE_LIBPORPOISE_FORCE_UMBRELLA_GX_HEADERS) && \
    defined(__has_include)
#if __has_include(<dolphin/gx/GXFrameBuffer.h>)
#define PORPOISE_LIBPORPOISE_HAS_SPLIT_GX_HEADERS 1
#endif
#endif

#if defined(PORPOISE_LIBPORPOISE_HAS_SPLIT_GX_HEADERS)
#include <dolphin/gx/GXBump.h>
#include <dolphin/gx/GXDispList.h>
#include <dolphin/gx/GXFifo.h>
#include <dolphin/gx/GXFrameBuffer.h>
#include <dolphin/gx/GXHostArray.h>
#include <dolphin/gx/GXLighting.h>
#include <dolphin/gx/GXPixel.h>
#include <dolphin/gx/GXTev.h>
#include <dolphin/gx/GXTexture.h>
#include <dolphin/gx/GXTransform.h>

/* GXManage.h is not usable as a standalone public header in the current
 * libPorpoise contract because its unrelated verification declarations name
 * an unavailable GXVerifyLevel type. GXMisc.h also declares SDK-internal
 * static functions without definitions. Mirror only the stable public SDK
 * declarations used by the runtime and generated ABI bridges; the split and
 * umbrella headers publish these exact signatures. */
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*GXDrawDoneCallback)(void);
GXFifoObj *GXInit(void *base, u32 size);
void GXAbortFrame(void);
void GXPixModeSync(void);
void GXPokeBlendMode(
    GXBlendMode type,
    GXBlendFactor source_factor,
    GXBlendFactor destination_factor,
    GXLogicOp operation);
GXDrawDoneCallback GXSetDrawDoneCallback(GXDrawDoneCallback callback);
void GXSetDrawDone(void);
void GXWaitDrawDone(void);
#ifdef __cplusplus
}
#endif
#else
#include <dolphin/gx.h>
#endif

#endif
