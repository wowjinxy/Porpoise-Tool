/**
 * @file ppc_runtime.c
 * @brief PowerPC Runtime Library - Additional runtime support
 */

#include "ppc_runtime.h"
#include <stdint.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

/**
 * @brief Exit the process (transpiled from GameCube exit function)
 * This is called when the game exits. On Windows, we call ExitProcess.
 */
void _ExitProcess(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6,
                  uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r10,
                  double f1, double f2) {
    (void)r4; (void)r5; (void)r6; (void)r7; (void)r8; (void)r9; (void)r10;
    (void)f1; (void)f2;
    
    // r3 contains the exit code
#ifdef _WIN32
    ExitProcess(r3);
#else
    exit(r3);
#endif
}
