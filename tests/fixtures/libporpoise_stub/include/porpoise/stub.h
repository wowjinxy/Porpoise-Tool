#ifndef TEST_LIBPORPOISE_STUB_H
#define TEST_LIBPORPOISE_STUB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned int PorpoiseStubOSInitCount(void);
void *PorpoiseStubNativePointer(void);
uint32_t PorpoiseStubTokenAddress(void);
unsigned int PorpoiseStubTokenReleaseCount(void);
unsigned int PorpoiseStubBootstrapCount(void);
int PorpoiseStubTitleSentinelsValid(void);
int PorpoiseHostPrepareTitleEntryV1(
    uint32_t entry_address,
    uint32_t gpr_out[32]);
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
