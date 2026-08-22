#ifndef TEST_LIBPORPOISE_DOLPHIN_VI_H
#define TEST_LIBPORPOISE_DOLPHIN_VI_H

#include <dolphin/types.h>

#ifndef PORPOISE_STUB_DISABLE_VI_NEXT_FRAMEBUFFER_GUEST_ADDRESS_CONTRACT
#define LIBPORPOISE_VI_SET_NEXT_FRAME_BUFFER_GUEST_ADDRESS_API_VERSION 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VITVMode {
    VI_TVMODE_STUB = 0
} VITVMode;

typedef enum VIXFBMode {
    VI_XFBMODE_STUB = 0
} VIXFBMode;

typedef struct _GXRenderModeObj {
    VITVMode viTVmode;
    u16 fbWidth;
    u16 efbHeight;
    u16 xfbHeight;
    u16 viXOrigin;
    u16 viYOrigin;
    u16 viWidth;
    u16 viHeight;
    VIXFBMode xFBmode;
    u8 field_rendering;
    u8 aa;
    u8 sample_pattern[12][2];
    u8 vfilter[7];
} GXRenderModeObj;

void VIInit(void);
void VIConfigure(const GXRenderModeObj *mode);
void VIWaitForRetrace(void);
void VISetBlack(BOOL black);
void VIFlush(void);
#ifndef PORPOISE_STUB_DISABLE_VI_NEXT_FRAMEBUFFER_GUEST_ADDRESS_CONTRACT
BOOL VIHostSetNextFrameBufferGuestAddress(u32 guest_address);
#endif

#ifdef __cplusplus
}
#endif

#endif
