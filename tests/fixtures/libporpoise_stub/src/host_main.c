#define LIBPORPOISE_MAIN_HANDLED 1
#include <dolphin.h>
#include <porpoise/stub.h>

extern void DolphinMain(void);

int main(void) {
    DolphinMain();
    return PorpoiseStubOSInitCount() == 1U ? 0 : 2;
}
