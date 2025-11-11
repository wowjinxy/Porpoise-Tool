/**
 * @file ppc_runtime.c
 * @brief PowerPC Runtime Library - Additional runtime support
 */

#include "ppc_runtime.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>  // For sprintf declaration

#ifdef _WIN32
#include <windows.h>
// Note: strings.h is not available on Windows - strcasecmp is implemented below
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

/**
 * @brief PowerPC variadic argument handler
 * This is called for variadic functions (like printf, sprintf, etc.)
 * On PowerPC, variadic arguments are passed in registers r3-r10, f1-f13
 * This function extracts the next argument from the variadic list.
 * 
 * @param ap Pointer to va_list (passed in r3)
 * @param type Type of argument to extract (passed in r4)
 * @return The extracted argument value
 */
void* ppc_va_arg(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6,
                 uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r10,
                 double f1, double f2) {
    (void)r5; (void)r6; (void)r7; (void)r8; (void)r9; (void)r10;
    (void)f1; (void)f2;
    
    // r3 contains the va_list pointer
    // r4 contains the type/size information
    // This is a simplified implementation - actual PowerPC va_arg is more complex
    va_list *ap = (va_list*)(uintptr_t)r3;
    void *result = NULL;
    
    // Extract argument based on type
    // Type encoding: 0 = int, 1 = long, 2 = double, 3 = pointer
    int type = (int)r4;
    
    switch (type) {
        case 0: { // int
            int val = va_arg(*ap, int);
            result = (void*)(intptr_t)val;
            break;
        }
        case 1: { // long
            long val = va_arg(*ap, long);
            result = (void*)(intptr_t)val;
            break;
        }
        case 2: { // double
            double val = va_arg(*ap, double);
            // For double, we need to return it differently
            // This is a simplified version
            result = (void*)(intptr_t)(long)val;
            break;
        }
        case 3: // pointer
        default:
            result = va_arg(*ap, void*);
            break;
    }
    
    return result;
}

/**
 * @brief Case-insensitive string comparison (Windows compatibility)
 * On Windows, strcasecmp is not in the standard library, so we provide it.
 * On Unix/Linux, strcasecmp is typically in <strings.h>, but we provide a fallback.
 */
#ifdef _WIN32
// On Windows, strcasecmp is not in the standard library
// Provide our own implementation using _stricmp (MSVC) or manual comparison
#ifdef _MSC_VER
// MSVC has _stricmp in <string.h> - use it (string.h already included above)
int strcasecmp(const char *s1, const char *s2) {
    return _stricmp(s1, s2);
}
#else
// MinGW or other Windows compilers - provide our own implementation
int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        // Convert to lowercase for comparison
        if (c1 >= 'A' && c1 <= 'Z') c1 = c1 - 'A' + 'a';
        if (c2 >= 'A' && c2 <= 'Z') c2 = c2 - 'A' + 'a';
        if (c1 != c2) return (int)(unsigned char)c1 - (int)(unsigned char)c2;
        s1++;
        s2++;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}
#endif
#else
// On Unix/Linux, strcasecmp should be available in <strings.h>
// Include it and provide a fallback implementation if not available
#include <strings.h>
#ifndef strcasecmp
// strcasecmp not defined after including <strings.h> - provide our own
int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        // Convert to lowercase for comparison
        if (c1 >= 'A' && c1 <= 'Z') c1 = c1 - 'A' + 'a';
        if (c2 >= 'A' && c2 <= 'Z') c2 = c2 - 'A' + 'a';
        if (c1 != c2) return (int)(unsigned char)c1 - (int)(unsigned char)c2;
        s1++;
        s2++;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}
#endif
#endif
