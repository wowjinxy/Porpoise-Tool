#define LIBPORPOISE_MAIN_HANDLED 1
#include <dolphin.h>
#include <porpoise/stub.h>

#include <string.h>

extern void DolphinMain(void);

int main(void) {
    DolphinMain();
    return PorpoiseStubOSInitCount() == 1U &&
                   PorpoiseStubDVDInitCount() == 1U &&
                   PorpoiseStubRuntimePrepareCount() == 1U &&
                   PorpoiseStubBootstrapCount() == 1U &&
                   strcmp(PorpoiseStubDVDRoot(), "stub-files") == 0 &&
                   PorpoiseStubTitleSentinelsValid()
               ? 0
               : 2;
}
