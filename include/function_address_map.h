/**
 * @file function_address_map.h
 * @brief Function Address Map - Maps GameCube addresses to transpiled C function pointers
 * 
 * This provides compile-time mapping of GameCube function addresses to their
 * corresponding transpiled C function pointers, allowing indirect calls to be
 * resolved to direct function calls.
 */

#ifndef FUNCTION_ADDRESS_MAP_H
#define FUNCTION_ADDRESS_MAP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Function pointer type for transpiled functions (10-parameter convention)
 */
typedef void (*TranspiledFunctionPtr)(uintptr_t r3, uintptr_t r4, uintptr_t r5, uintptr_t r6,
                                      uintptr_t r7, uintptr_t r8, uintptr_t r9, uintptr_t r10,
                                      double f1, double f2);

/**
 * @brief Function address map entry
 */
typedef struct {
    uint32_t gc_address;          // GameCube function address
    TranspiledFunctionPtr func_ptr; // Pointer to transpiled C function
    const char *name;              // Function name (for debugging)
} FunctionAddressEntry;

/**
 * @brief Initialize the function address map
 * @return true on success, false on failure
 */
bool function_address_map_init(void);

/**
 * @brief Register a function in the address map
 * @param gc_address GameCube address of the function
 * @param func_ptr Pointer to the transpiled C function
 * @param name Function name (for debugging)
 */
void function_address_map_register(uint32_t gc_address, TranspiledFunctionPtr func_ptr, const char *name);

/**
 * @brief Call a function by its GameCube address
 * This looks up the address in the map and calls the corresponding function directly.
 * @param gc_address GameCube address of the function to call
 * @param r3-r10, f1, f2 Parameters to pass to the function
 */
void call_function_by_address(uint32_t gc_address, uintptr_t r3, uintptr_t r4, uintptr_t r5, uintptr_t r6,
                              uintptr_t r7, uintptr_t r8, uintptr_t r9, uintptr_t r10,
                              double f1, double f2);

#ifdef __cplusplus
}
#endif

#endif // FUNCTION_ADDRESS_MAP_H

