#ifndef TEST_LIBPORPOISE_STUB_H
#define TEST_LIBPORPOISE_STUB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned int PorpoiseStubOSInitCount(void);
void PorpoiseStubAIReset(void);
unsigned int PorpoiseStubAIInitCount(void);
const void *PorpoiseStubAILastStack(void);
void PorpoiseStubRejectNextArenaLo(void);
void PorpoiseStubRejectNextArenaHi(void);
void PorpoiseStubRejectArenaLoOnCall(unsigned int call_number);
void PorpoiseStubRejectArenaHiOnCall(unsigned int call_number);
void PorpoiseStubInterruptReset(void);
int PorpoiseStubInterruptsEnabled(void);
unsigned int PorpoiseStubInterruptDisableCount(void);
unsigned int PorpoiseStubInterruptRestoreCount(void);
unsigned int PorpoiseStubInterruptDisableTransitionCount(void);
unsigned int PorpoiseStubInterruptRestoreTransitionCount(void);
void PorpoiseStubWaitForInterruptWaiter(void);
void PorpoiseStubSetSystemCallVectorMapped(int mapped);
unsigned int PorpoiseStubDVDInitCount(void);
const char *PorpoiseStubDVDRoot(void);
unsigned int PorpoiseStubDVDConvertCallCount(void);
unsigned int PorpoiseStubDVDOpenCallCount(void);
unsigned int PorpoiseStubDVDFastOpenCallCount(void);
unsigned int PorpoiseStubDVDReadCallCount(void);
unsigned int PorpoiseStubDVDCloseCallCount(void);
unsigned int PorpoiseStubDVDCancelCallCount(void);
unsigned int PorpoiseStubDVDActiveFileCount(void);
const void *PorpoiseStubDVDLastOpenFileInfo(void);
const void *PorpoiseStubDVDLastReadFileInfo(void);
uint8_t PorpoiseStubDVDExpectedByte(uint32_t offset);
void PorpoiseStubGXFifoReset(void);
void PorpoiseStubGXFifoSetAccept(int accept);
unsigned int PorpoiseStubGXFifoCallCount(void);
unsigned int PorpoiseStubGXFifoCallSize(unsigned int index);
unsigned int PorpoiseStubGXFifoByteCount(void);
uint8_t PorpoiseStubGXFifoByte(unsigned int index);
unsigned int PorpoiseStubGXNumericWriteCount(void);
enum {
    PORPOISE_STUB_GX_INIT_TOKEN_RESULT = 0,
    PORPOISE_STUB_GX_INIT_NULL_RESULT = 1,
    PORPOISE_STUB_GX_INIT_UNENCODABLE_RESULT = 2,
    PORPOISE_STUB_GX_INIT_MAPPED_RESULT = 3
};
void PorpoiseStubGXInitReset(void);
void PorpoiseStubGXInitSetResult(int result_kind);
unsigned int PorpoiseStubGXInitCallCount(void);
const void *PorpoiseStubGXInitLastBase(void);
uint32_t PorpoiseStubGXInitLastSize(void);
void PorpoiseStubSetDecodeBias(unsigned int bias);
void PorpoiseStubGXBoundaryReset(void);
unsigned int PorpoiseStubGXDrawDoneSetterCallCount(void);
void PorpoiseStubGXSetForeignDrawDoneCallback(int enabled);
void PorpoiseStubGXTriggerDrawDone(void);
unsigned int PorpoiseStubGXCopyFilterCallCount(void);
uint32_t PorpoiseStubGXCopyFilterUseAA(void);
uint32_t PorpoiseStubGXCopyFilterUseVertical(void);
uint8_t PorpoiseStubGXCopyFilterSample(unsigned int index);
uint8_t PorpoiseStubGXCopyFilterVertical(unsigned int index);
unsigned int PorpoiseStubGXCopyClearCallCount(void);
uint32_t PorpoiseStubGXCopyClearColor(void);
uint32_t PorpoiseStubGXCopyClearDepth(void);
unsigned int PorpoiseStubGXSetDispCopyDstCallCount(void);
uint32_t PorpoiseStubGXDispCopyWidth(void);
uint32_t PorpoiseStubGXDispCopyHeight(void);
unsigned int PorpoiseStubGXSetTexCopyDstCallCount(void);
uint32_t PorpoiseStubGXTexCopyWidth(void);
uint32_t PorpoiseStubGXTexCopyHeight(void);
uint32_t PorpoiseStubGXTexCopyFormat(void);
uint32_t PorpoiseStubGXTexCopyMipmap(void);
unsigned int PorpoiseStubGXCopyDispCallCount(void);
unsigned int PorpoiseStubGXCopyTexCallCount(void);
void PorpoiseStubGXSetGuestAddressCopyResults(
    int display_result,
    int texture_result);
void PorpoiseStubGXSetGuestAddressDisplayCopySpan(uint32_t required_bytes);
unsigned int PorpoiseStubGXCopyDispGuestAddressCallCount(void);
unsigned int PorpoiseStubGXCopyTexGuestAddressCallCount(void);
uint32_t PorpoiseStubGXCopyDispGuestAddress(void);
uint32_t PorpoiseStubGXCopyTexGuestAddress(void);
uint32_t PorpoiseStubGXCopyDispGuestAddressClearFlag(void);
uint32_t PorpoiseStubGXCopyTexGuestAddressClearFlag(void);
unsigned int PorpoiseStubGXCopyDispAcceptedCallCount(void);
uint32_t PorpoiseStubGXCopyDispAcceptedGuestAddress(void);
uint32_t PorpoiseStubGXCopyDispAcceptedClearFlag(void);
unsigned int PorpoiseStubGXLoadLightCallCount(void);
uint32_t PorpoiseStubGXLoadLightId(void);
uint8_t PorpoiseStubGXLoadLightByte(unsigned int index);
void PorpoiseStubVIReset(void);
unsigned int PorpoiseStubVIConfigureCount(void);
const void *PorpoiseStubVILastRenderMode(void);
void PorpoiseStubVISetNextFrameBufferResult(int result);
unsigned int PorpoiseStubVISetNextFrameBufferCallCount(void);
uint32_t PorpoiseStubVINextFrameBufferGuestAddress(void);
uint32_t PorpoiseStubVIPendingFrameBufferGuestAddress(void);
void PorpoiseStubARReset(void);
void PorpoiseStubARSetDMAResult(int result);
unsigned int PorpoiseStubARDMACallCount(void);
uint32_t PorpoiseStubARLastDMAType(void);
uint32_t PorpoiseStubARLastDMAMainMemory(void);
uint32_t PorpoiseStubARLastDMAAram(void);
uint32_t PorpoiseStubARLastDMALength(void);
void PorpoiseStubARAllocatorResetState(void);
unsigned int PorpoiseStubARAllocatorInitCount(void);
unsigned int PorpoiseStubARAllocatorAllocCount(void);
unsigned int PorpoiseStubARAllocatorFreeCount(void);
unsigned int PorpoiseStubARAllocatorResetCount(void);
const uint32_t *PorpoiseStubARAllocatorBlockTable(void);
uint32_t PorpoiseStubARAllocatorBlockValue(unsigned int index);
void PorpoiseStubARAllocatorSetSize(uint32_t size);
enum {
    PORPOISE_STUB_DSP_CALLBACK_INIT = 1,
    PORPOISE_STUB_DSP_CALLBACK_RESUME = 2,
    PORPOISE_STUB_DSP_CALLBACK_REQUEST = 4,
    PORPOISE_STUB_DSP_CALLBACK_DONE = 8
};
void PorpoiseStubDSPReset(void);
void PorpoiseStubDSPSetCallbackMask(uint32_t mask);
void PorpoiseStubDSPRejectNext(int reject);
unsigned int PorpoiseStubDSPAddTaskCallCount(void);
unsigned int PorpoiseStubDSPActiveTaskCount(void);
unsigned int PorpoiseStubDSPEventCount(void);
uint32_t PorpoiseStubDSPEventKind(unsigned int index);
uint32_t PorpoiseStubDSPEventState(unsigned int index);
uint32_t PorpoiseStubDSPEventFlags(unsigned int index);
const void *PorpoiseStubDSPLastTask(void);
const void *PorpoiseStubDSPLastIramMemory(void);
const void *PorpoiseStubDSPLastDramMemory(void);
uint32_t PorpoiseStubDSPLastPriority(void);
uint32_t PorpoiseStubDSPLastIramLength(void);
uint32_t PorpoiseStubDSPLastIramAddress(void);
uint32_t PorpoiseStubDSPLastDramLength(void);
uint32_t PorpoiseStubDSPLastDramAddress(void);
uint16_t PorpoiseStubDSPLastInitVector(void);
uint16_t PorpoiseStubDSPLastResumeVector(void);
int64_t PorpoiseStubDSPLastContextTime(void);
int64_t PorpoiseStubDSPLastTaskTime(void);
void PorpoiseStubDispatchReset(void);
int PorpoiseStubDispatchAddAddress(uint32_t address);
int PorpoiseStubDispatchAvailable(uint32_t address);
void *PorpoiseStubNativePointer(void);
void *PorpoiseStubNativePointerAt(unsigned int index);
uint32_t PorpoiseStubTokenAddress(void);
unsigned int PorpoiseStubTokenEncodeCount(void);
unsigned int PorpoiseStubTokenReleaseCount(void);
unsigned int PorpoiseStubTokenActiveCount(void);
void PorpoiseStubSetTokenDecodeBias(unsigned int bias);
unsigned int PorpoiseStubBootstrapCount(void);
unsigned int PorpoiseStubRuntimePrepareCount(void);
int PorpoiseStubTitleSentinelsValid(void);
typedef struct PorpoiseStubTitleRuntimeConfigV1 {
    uint32_t flags;
    const char *dvd_root_directory;
} PorpoiseStubTitleRuntimeConfigV1;
int PorpoiseHostPrepareRuntimeV1(
    uint32_t entry_address,
    PorpoiseStubTitleRuntimeConfigV1 *config_out);
typedef struct PorpoiseStubTitleInitialWordV3 {
    uint32_t guest_address;
    uint32_t value;
} PorpoiseStubTitleInitialWordV3;
typedef struct PorpoiseStubTitleStartupFunctionV3 {
    uint32_t guest_address;
    uint32_t flags;
} PorpoiseStubTitleStartupFunctionV3;
typedef struct PorpoiseStubTitleEntryStateV3 {
    uint32_t gpr[32];
    uint32_t arena_lo;
    uint32_t arena_hi;
    uint32_t startup_function_count;
    PorpoiseStubTitleStartupFunctionV3 startup_functions[8];
    uint32_t initial_word_count;
    PorpoiseStubTitleInitialWordV3 initial_words[16];
} PorpoiseStubTitleEntryStateV3;
int PorpoiseHostPrepareTitleEntryV3(
    uint32_t entry_address,
    PorpoiseStubTitleEntryStateV3 *state_out);
uint32_t PorpoiseStubAdd(uint32_t left, uint32_t right);
void *PorpoiseStubIdentity(void *pointer);
double PorpoiseStubFloatMix(float left, double right);
struct PorpoisePpcState;
void PorpoiseStubReportAdapter(struct PorpoisePpcState *state);
unsigned int PorpoiseStubReportCount(void);
uint32_t PorpoiseAddOne(uint32_t value);
float PorpoiseAddFloat(float left, float right);
double PorpoiseAddDouble(double left, double right);

#ifdef __cplusplus
}
#endif

#endif
