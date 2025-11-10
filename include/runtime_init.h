/**
 * @file runtime_init.h
 * @brief Runtime Initialization for Transpiled PowerPC Code
 * 
 * This header must be included in the main application to properly
 * initialize the runtime environment and link the custom CRT.
 */

#ifndef RUNTIME_INIT_H
#define RUNTIME_INIT_H

#include <stdint.h>
#include "ppc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

//==============================================================================
// RUNTIME INITIALIZATION
//==============================================================================

/**
 * @brief Initialize the PowerPC runtime environment
 * 
 * Call this before executing any transpiled PowerPC code.
 * This sets up memory, registers, and runtime state.
 */
void ppc_runtime_init(void);

/**
 * @brief Cleanup the PowerPC runtime environment
 * 
 * Call this when done executing transpiled code.
 */
void ppc_runtime_cleanup(void);

//==============================================================================
// LINKER DIRECTIVES (MSVC)
//==============================================================================

#ifdef _MSC_VER
    // Tell the linker to use static runtime to avoid ucrtd.lib conflicts
    #pragma comment(linker, "/NODEFAULTLIB:msvcrt.lib")
    #pragma comment(linker, "/NODEFAULTLIB:msvcrtd.lib")
    #pragma comment(linker, "/NODEFAULTLIB:ucrt.lib")
    #pragma comment(linker, "/NODEFAULTLIB:ucrtd.lib")
    
    // Link with static runtime
    #ifdef _DEBUG
        #pragma comment(lib, "libcmtd.lib")  // Static debug runtime
    #else
        #pragma comment(lib, "libcmt.lib")   // Static release runtime
    #endif
    
    // Link with our custom runtime
    #pragma comment(lib, "ppc_runtime.lib")
    
    // Suppress warnings about deprecated CRT functions
    #pragma warning(disable: 4996)
#endif

//==============================================================================
// MEMORY MANAGEMENT
//==============================================================================

/**
 * @brief Allocate GameCube/Wii memory space
 * @param size Size in bytes (should be 24MB for GameCube, 64MB for Wii)
 * @return Pointer to allocated memory, or NULL on failure
 */
void* ppc_alloc_memory(size_t size);

/**
 * @brief Free GameCube/Wii memory space
 */
void ppc_free_memory(void* ptr);

/**
 * @brief Set the base address for memory translation
 * @param base_addr Base address (usually 0x80000000 for GameCube)
 */
void ppc_set_memory_base(uint32_t base_addr);

//==============================================================================
// DEBUGGING AND DIAGNOSTICS
//==============================================================================

/**
 * @brief Enable runtime diagnostics
 * @param enable true to enable, false to disable
 */
void ppc_set_diagnostics(int enable);

/**
 * @brief Print runtime statistics
 */
void ppc_print_stats(void);

#ifdef __cplusplus
}
#endif

#endif // RUNTIME_INIT_H

