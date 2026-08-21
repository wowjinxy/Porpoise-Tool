#ifndef TEST_LIBPORPOISE_DISPATCH_PRIVATE_H
#define TEST_LIBPORPOISE_DISPATCH_PRIVATE_H

#include <stdint.h>

/* Resolved from the fixture's linked guest symbol table. */
#define PORPOISE_GUEST_ARQ_CALLBACK_HACK_ADDRESS UINT32_C(0x80001234)

int porpoise_dispatch_available(uint32_t address);

#endif
