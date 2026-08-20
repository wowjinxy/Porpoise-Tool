#include <porpoise/sdk_import_contract.h>

/* Deterministic no-op implementations for the fixture's scalar SDK surface. */
#define STUB_VOID_0(name) \
    void name(void) {}
#define STUB_VOID_1(name, t0) \
    void name(t0 a0) { (void)a0; }
#define STUB_VOID_2(name, t0, t1) \
    void name(t0 a0, t1 a1) { (void)a0; (void)a1; }
#define STUB_VOID_3(name, t0, t1, t2) \
    void name(t0 a0, t1 a1, t2 a2) \
    { (void)a0; (void)a1; (void)a2; }
#define STUB_VOID_4(name, t0, t1, t2, t3) \
    void name(t0 a0, t1 a1, t2 a2, t3 a3) \
    { (void)a0; (void)a1; (void)a2; (void)a3; }
#define STUB_VOID_5(name, t0, t1, t2, t3, t4) \
    void name(t0 a0, t1 a1, t2 a2, t3 a3, t4 a4) \
    { (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; }
#define STUB_VOID_6(name, t0, t1, t2, t3, t4, t5) \
    void name(t0 a0, t1 a1, t2 a2, t3 a3, t4 a4, t5 a5) \
    { (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; }
#define STUB_VOID_7(name, t0, t1, t2, t3, t4, t5, t6) \
    void name(t0 a0, t1 a1, t2 a2, t3 a3, t4 a4, t5 a5, t6 a6) \
    { \
        (void)a0; (void)a1; (void)a2; (void)a3; \
        (void)a4; (void)a5; (void)a6; \
    }

#define STUB_RETURN_0(type, name, value) \
    type name(void) { return (type)(value); }
#define STUB_RETURN_1(type, name, value, t0) \
    type name(t0 a0) { (void)a0; return (type)(value); }
#define STUB_RETURN_3(type, name, value, t0, t1, t2) \
    type name(t0 a0, t1 a1, t2 a2) \
    { (void)a0; (void)a1; (void)a2; return (type)(value); }

#define STUB_U8_0(name) STUB_RETURN_0(u8, name, 0u)
#define STUB_U16_1(name, t0) STUB_RETURN_1(u16, name, 0u, t0)
#define STUB_U32_0(name) STUB_RETURN_0(u32, name, 0u)
#define STUB_U32_1(name, t0) STUB_RETURN_1(u32, name, 0u, t0)
#define STUB_S32_0(name) STUB_RETURN_0(s32, name, 0)
#define STUB_S32_1(name, t0) STUB_RETURN_1(s32, name, 0, t0)
#define STUB_S32_3(name, t0, t1, t2) \
    STUB_RETURN_3(s32, name, 0, t0, t1, t2)

STUB_U32_0(AIGetStreamSampleRate)
STUB_U8_0(AIGetStreamVolLeft)
STUB_U8_0(AIGetStreamVolRight)
STUB_VOID_2(AIInitDMA, u32, u32)
STUB_VOID_1(AISetDSPSampleRate, u32)
STUB_VOID_1(AISetStreamPlayState, u32)
STUB_VOID_1(AISetStreamVolLeft, u8)
STUB_VOID_1(AISetStreamVolRight, u8)
STUB_VOID_0(AIStartDMA)
STUB_VOID_0(AIStopDMA)
STUB_VOID_4(ARStartDMA, u32, u32, u32, u32)
STUB_S32_1(CARDCheck, s32)
STUB_S32_1(CARDGetResultCode, s32)
STUB_VOID_0(CARDInit)
STUB_S32_1(CARDUnmount, s32)
STUB_VOID_0(DBClose)
STUB_VOID_0(DBInit)
STUB_VOID_0(DBInitInterrupts)
STUB_VOID_0(DBOpen)
STUB_S32_0(DBQueryData)
STUB_U32_0(DSPCheckMailFromDSP)
STUB_U32_0(DSPCheckMailToDSP)
STUB_VOID_0(DSPInit)
STUB_U32_0(DSPReadMailFromDSP)
STUB_VOID_1(DSPSendMailToDSP, u32)
STUB_S32_1(DVDSetAutoInvalidation, s32)
STUB_VOID_0(EXI2_EnableInterrupts)
STUB_S32_0(EXI2_Poll)
STUB_VOID_0(EXI2_Reserve)
STUB_VOID_0(EXI2_Unreserve)
STUB_S32_1(EXIDeselect, s32)
STUB_S32_1(EXIDetach, s32)
STUB_S32_1(EXIProbe, s32)
STUB_S32_3(EXISelect, s32, u32, u32)
STUB_S32_1(EXISync, s32)
STUB_S32_1(EXIUnlock, s32)
STUB_VOID_3(GXBegin, s32, s32, u16)
STUB_VOID_0(GXClearVtxDesc)
STUB_VOID_3(GXEnableTexOffsets, s32, u8, u8)
STUB_VOID_0(GXInvalidateTexAll)
STUB_VOID_0(GXInvalidateVtxCache)
STUB_VOID_0(GXPixModeSync)
STUB_VOID_4(GXPokeBlendMode, s32, s32, s32, s32)
STUB_VOID_5(GXSetAlphaCompare, s32, u8, s32, s32, u8)
STUB_VOID_1(GXSetAlphaUpdate, u8)
STUB_VOID_4(GXSetBlendMode, s32, s32, s32, s32)
STUB_VOID_7(GXSetChanCtrl, s32, u8, s32, s32, u32, s32, s32)
STUB_VOID_1(GXSetClipMode, s32)
STUB_VOID_1(GXSetCoPlanar, u8)
STUB_VOID_1(GXSetColorUpdate, u8)
STUB_VOID_1(GXSetCopyClamp, s32)
STUB_VOID_1(GXSetCullMode, s32)
STUB_VOID_1(GXSetCurrentMtx, u32)
STUB_VOID_1(GXSetDispCopyFrame2Field, s32)
STUB_VOID_1(GXSetDispCopyGamma, s32)
STUB_VOID_4(GXSetDispCopySrc, u16, u16, u16, u16)
STUB_U32_1(GXSetDispCopyYScale, f32)
STUB_VOID_1(GXSetDither, u8)
STUB_VOID_0(GXSetDrawDone)
STUB_VOID_2(GXSetDstAlpha, u8, u8)
STUB_VOID_2(GXSetFieldMask, u8, u8)
STUB_VOID_2(GXSetFieldMode, u8, u8)
STUB_VOID_3(GXSetIndTexCoordScale, s32, s32, s32)
STUB_VOID_2(GXSetLineWidth, u8, s32)
STUB_VOID_1(GXSetNumChans, u8)
STUB_VOID_1(GXSetNumIndStages, u8)
STUB_VOID_1(GXSetNumTevStages, u8)
STUB_VOID_1(GXSetNumTexGens, u8)
STUB_VOID_2(GXSetPixelFmt, s32, s32)
STUB_VOID_2(GXSetPointSize, u8, s32)
STUB_VOID_4(GXSetScissor, u32, u32, u32, u32)
STUB_VOID_2(GXSetScissorBoxOffset, s32, s32)
STUB_VOID_5(GXSetTevAlphaIn, s32, s32, s32, s32, s32)
STUB_VOID_5(GXSetTevColorIn, s32, s32, s32, s32, s32)
STUB_VOID_1(GXSetTevDirect, s32)
STUB_VOID_2(GXSetTevKAlphaSel, s32, s32)
STUB_VOID_2(GXSetTevKColorSel, s32, s32)
STUB_VOID_2(GXSetTevOp, s32, s32)
STUB_VOID_4(GXSetTevOrder, s32, s32, s32, s32)
STUB_VOID_3(GXSetTevSwapMode, s32, s32, s32)
STUB_VOID_5(GXSetTevSwapModeTable, s32, s32, s32, s32, s32)
STUB_VOID_6(GXSetTexCoordGen2, s32, s32, s32, u32, u8, u32)
STUB_VOID_4(GXSetTexCopySrc, u16, u16, u16, u16)
STUB_VOID_6(GXSetViewport, f32, f32, f32, f32, f32, f32)
STUB_VOID_7(GXSetViewportJitter, f32, f32, f32, f32, f32, f32, u32)
STUB_VOID_5(GXSetVtxAttrFmt, s32, s32, s32, s32, u8)
STUB_VOID_2(GXSetVtxDesc, s32, s32)
STUB_VOID_1(GXSetZCompLoc, u8)
STUB_VOID_3(GXSetZMode, u8, s32, u8)
STUB_VOID_3(GXSetZTexture, s32, s32, u32)
STUB_VOID_0(GXWaitDrawDone)
STUB_S32_1(InitializeUART, s32)
STUB_U32_0(OSGetConsoleSimulatedMemSize)
STUB_U32_0(OSGetPhysicalMemSize)
STUB_U32_0(OSGetResetCode)
STUB_S32_0(OSGetResetSwitchState)
STUB_U32_0(OSGetSoundMode)
STUB_U32_0(OSGetTick)
STUB_U16_1(OSGetWirelessID, s32)
STUB_VOID_3(OSResetSystem, s32, u32, s32)
STUB_S32_1(OSSetCurrentHeap, s32)
STUB_VOID_1(OSSetSoundMode, u32)
STUB_VOID_2(OSSetWirelessID, s32, u16)
STUB_VOID_2(PADControlMotor, s32, u32)
STUB_S32_0(PADInit)
STUB_S32_1(PADRecalibrate, u32)
STUB_S32_1(PADReset, u32)
STUB_VOID_0(PPCHalt)
STUB_U32_0(PPCMfhid0)
STUB_U32_0(PPCMfhid2)
STUB_U32_0(PPCMfl2cr)
STUB_U32_0(PPCMfmsr)
STUB_VOID_1(PPCMtdec, u32)
STUB_VOID_1(PPCMthid0, u32)
STUB_VOID_1(PPCMthid2, u32)
STUB_VOID_1(PPCMtl2cr, u32)
STUB_VOID_1(PPCMtmsr, u32)
STUB_U32_1(SIDisablePolling, u32)
STUB_U32_1(SIEnablePolling, u32)
STUB_U32_1(SIGetType, s32)
STUB_VOID_0(SIRefreshSamplingRate)
STUB_VOID_2(SISetCommand, s32, u32)
STUB_VOID_1(SISetSamplingRate, u32)
STUB_VOID_0(VIFlush)
STUB_U32_0(VIGetCurrentLine)
STUB_U32_0(VIGetNextField)
STUB_U32_0(VIGetTvFormat)
STUB_VOID_0(VIInit)
STUB_VOID_1(VISetBlack, s32)
STUB_VOID_0(VIWaitForRetrace)
STUB_VOID_0(GXAbortFrame)
STUB_VOID_3(GXSetIndTexOrder, s32, s32, s32)
STUB_VOID_6(GXSetTevColorOp, s32, s32, s32, s32, u8, s32)
STUB_VOID_6(GXSetTevAlphaOp, s32, s32, s32, s32, u8, s32)
STUB_VOID_2(GXDrawSphere, u8, u8)
STUB_VOID_1(GXDrawSphere1, u8)
