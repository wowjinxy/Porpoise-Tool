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

/**
 * @brief PowerPC variadic argument handler
 * @param r3 Pointer to va_list
 * @param r4 Type of argument to extract
 * @return The extracted argument value
 */
void* ppc_va_arg(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6,
                 uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r10,
                 double f1, double f2);

/**
 * @brief Case-insensitive string comparison (Windows compatibility)
 * On Windows, strcasecmp might not be available
 */
int strcasecmp(const char *s1, const char *s2);

/**
 * @brief Initialize PowerPC registers to host pointers
 * This replaces the game's __init_registers() function to ensure registers
 * contain host pointers instead of GameCube addresses.
 * 
 * @param param_r3-r10 Parameters passed in registers r3-r10
 * @param param_f1-f2 Parameters passed in floating-point registers f1-f2
 */
void __init_registers(uint32_t param_r3, uint32_t param_r4, uint32_t param_r5, uint32_t param_r6,
                      uint32_t param_r7, uint32_t param_r8, uint32_t param_r9, uint32_t param_r10,
                      double param_f1, double param_f2);

#ifdef __cplusplus
}
#endif

#endif // PPC_RUNTIME_H
