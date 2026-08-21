#ifndef PORPOISE_THREAD_CARRIER_STUB_H
#define PORPOISE_THREAD_CARRIER_STUB_H

#ifdef __cplusplus
extern "C" {
#endif

void PorpoiseThreadCarrierStubResetObservers(void);
unsigned int PorpoiseThreadCarrierStubCreateCount(void);
unsigned int PorpoiseThreadCarrierStubResumeCount(void);
unsigned int PorpoiseThreadCarrierStubSuspendCount(void);
unsigned int PorpoiseThreadCarrierStubStopCount(void);
unsigned int PorpoiseThreadCarrierStubJoinCount(void);
unsigned int PorpoiseThreadCarrierStubDestroyCount(void);

#ifdef __cplusplus
}
#endif

#endif
