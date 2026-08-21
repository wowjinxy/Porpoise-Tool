#include <porpoise/stub.h>

#include <stdint.h>

int porpoise_dispatch_available(uint32_t address)
{
    return PorpoiseStubDispatchAvailable(address);
}
