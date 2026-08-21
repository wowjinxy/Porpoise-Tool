#ifndef TEST_LIBPORPOISE_SDK_IMPORT_CONTRACT_H
#define TEST_LIBPORPOISE_SDK_IMPORT_CONTRACT_H

/*
 * Stable compile/link contract for ordinary typed SDK imports emitted by
 * Porpoise-Tool's ABI-manifest path.  These declarations intentionally use
 * the manifest's scalar C ABI rather than guessing private SDK structures.
 * Stateful or pointer-bearing calls remain in their focused fixture modules.
 */
#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

u32 AIGetStreamSampleRate(void);
u8 AIGetStreamVolLeft(void);
u8 AIGetStreamVolRight(void);
void AIInitDMA(u32, u32);
void AISetDSPSampleRate(u32);
void AISetStreamPlayState(u32);
void AISetStreamVolLeft(u8);
void AISetStreamVolRight(u8);
void AIStartDMA(void);
void AIStopDMA(void);
void ARStartDMA(u32, u32, u32, u32);
s32 CARDCheck(s32);
s32 CARDGetResultCode(s32);
void CARDInit(void);
s32 CARDUnmount(s32);
void DBClose(void);
void DBInit(void);
void DBInitInterrupts(void);
void DBOpen(void);
s32 DBQueryData(void);
u32 DSPCheckMailFromDSP(void);
u32 DSPCheckMailToDSP(void);
void DSPInit(void);
u32 DSPReadMailFromDSP(void);
void DSPSendMailToDSP(u32);
s32 DVDSetAutoInvalidation(s32);
void EXI2_EnableInterrupts(void);
s32 EXI2_Poll(void);
void EXI2_Reserve(void);
void EXI2_Unreserve(void);
s32 EXIDeselect(s32);
s32 EXIDetach(s32);
s32 EXIProbe(s32);
s32 EXISelect(s32, u32, u32);
s32 EXISync(s32);
s32 EXIUnlock(s32);
void GXBegin(s32, s32, u16);
void GXClearVtxDesc(void);
void GXEnableTexOffsets(s32, u8, u8);
void GXInvalidateTexAll(void);
void GXInvalidateVtxCache(void);
void GXPixModeSync(void);
void GXPokeBlendMode(s32, s32, s32, s32);
void GXSetAlphaCompare(s32, u8, s32, s32, u8);
void GXSetAlphaUpdate(u8);
void GXSetBlendMode(s32, s32, s32, s32);
void GXSetChanCtrl(s32, u8, s32, s32, u32, s32, s32);
void GXSetClipMode(s32);
void GXSetCoPlanar(u8);
void GXSetColorUpdate(u8);
void GXSetCopyClamp(s32);
void GXSetCullMode(s32);
void GXSetCurrentMtx(u32);
void GXSetDispCopyFrame2Field(s32);
void GXSetDispCopyGamma(s32);
void GXSetDispCopySrc(u16, u16, u16, u16);
u32 GXSetDispCopyYScale(f32);
void GXSetDither(u8);
void GXSetDrawDone(void);
void GXSetDstAlpha(u8, u8);
void GXSetFieldMask(u8, u8);
void GXSetFieldMode(u8, u8);
void GXSetIndTexCoordScale(s32, s32, s32);
void GXSetLineWidth(u8, s32);
void GXSetNumChans(u8);
void GXSetNumIndStages(u8);
void GXSetNumTevStages(u8);
void GXSetNumTexGens(u8);
void GXSetPixelFmt(s32, s32);
void GXSetPointSize(u8, s32);
void GXSetScissor(u32, u32, u32, u32);
void GXSetScissorBoxOffset(s32, s32);
void GXSetTevAlphaIn(s32, s32, s32, s32, s32);
void GXSetTevColorIn(s32, s32, s32, s32, s32);
void GXSetTevDirect(s32);
void GXSetTevKAlphaSel(s32, s32);
void GXSetTevKColorSel(s32, s32);
void GXSetTevOp(s32, s32);
void GXSetTevOrder(s32, s32, s32, s32);
void GXSetTevSwapMode(s32, s32, s32);
void GXSetTevSwapModeTable(s32, s32, s32, s32, s32);
void GXSetTexCoordGen2(s32, s32, s32, u32, u8, u32);
void GXSetTexCopySrc(u16, u16, u16, u16);
void GXSetViewport(f32, f32, f32, f32, f32, f32);
void GXSetViewportJitter(f32, f32, f32, f32, f32, f32, u32);
void GXSetVtxAttrFmt(s32, s32, s32, s32, u8);
void GXSetVtxDesc(s32, s32);
void GXSetZCompLoc(u8);
void GXSetZMode(u8, s32, u8);
void GXSetZTexture(s32, s32, u32);
void GXWaitDrawDone(void);
s32 InitializeUART(s32);
u32 OSGetConsoleSimulatedMemSize(void);
u32 OSGetPhysicalMemSize(void);
u32 OSGetResetCode(void);
s32 OSGetResetSwitchState(void);
u32 OSGetSoundMode(void);
u32 OSGetTick(void);
u16 OSGetWirelessID(s32);
void OSResetSystem(s32, u32, s32);
s32 OSSetCurrentHeap(s32);
void OSSetSoundMode(u32);
void OSSetWirelessID(s32, u16);
void PADControlMotor(s32, u32);
s32 PADInit(void);
s32 PADRecalibrate(u32);
s32 PADReset(u32);
void PPCHalt(void);
u32 PPCMfhid0(void);
u32 PPCMfhid2(void);
u32 PPCMfl2cr(void);
u32 PPCMfmsr(void);
void PPCMtdec(u32);
void PPCMthid0(u32);
void PPCMthid2(u32);
void PPCMtl2cr(u32);
void PPCMtmsr(u32);
u32 SIDisablePolling(u32);
u32 SIEnablePolling(u32);
u32 SIGetType(s32);
void SIRefreshSamplingRate(void);
void SISetCommand(s32, u32);
void SISetSamplingRate(u32);
void VIFlush(void);
u32 VIGetCurrentLine(void);
u32 VIGetNextField(void);
u32 VIGetTvFormat(void);
void VIInit(void);
void VISetBlack(s32);
void VIWaitForRetrace(void);
void GXAbortFrame(void);
void GXSetIndTexOrder(s32, s32, s32);
void GXSetTevColorOp(s32, s32, s32, s32, u8, s32);
void GXSetTevAlphaOp(s32, s32, s32, s32, u8, s32);
void GXDrawSphere(u8, u8);
void GXDrawSphere1(u8);

#ifdef __cplusplus
}
#endif

#endif
