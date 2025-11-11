/**
 * @file ppc_runtime.h
 * @brief PowerPC Runtime Library Header - Additional runtime support
 */

#ifndef PPC_RUNTIME_H
#define PPC_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Exit the process (transpiled from GameCube exit function)
 * @param r3 Exit code
 */
void _ExitProcess(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6,
                  uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r10,
                  double f1, double f2);

#ifdef __cplusplus
}
#endif

#endif // PPC_RUNTIME_H
