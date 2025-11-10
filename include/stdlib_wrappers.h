/**
 * @file stdlib_wrappers.h
 * @brief Wrapper functions for standard library calls from transpiled code
 * 
 * Transpiled PowerPC code always passes 10 parameters (r3-r10, f1-f2) to all functions.
 * These wrappers accept that signature and call the real stdlib functions with correct params.
 */

#ifndef STDLIB_WRAPPERS_H
#define STDLIB_WRAPPERS_H

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdarg.h>

// Wrapper macros to allow variadic calls
// The transpiler always passes (r3, r4, r5, r6, r7, r8, r9, r10, f1, f2)
// We only use the parameters the real function needs

// String functions - r3 is always the result/first param
static inline void strcmp(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r10, double f1, double f2) {
    (void)r5; (void)r6; (void)r7; (void)r8; (void)r9; (void)r10; (void)f1; (void)f2;
    extern int __real_strcmp(const char*, const char*);
    r3 = (uint32_t)__real_strcmp((const char*)(uintptr_t)r3, (const char*)(uintptr_t)r4);
}

static inline void strncmp(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r10, double f1, double f2) {
    (void)r6; (void)r7; (void)r8; (void)r9; (void)r10; (void)f1; (void)f2;
    extern int __real_strncmp(const char*, const char*, size_t);
    r3 = (uint32_t)__real_strncmp((const char*)(uintptr_t)r3, (const char*)(uintptr_t)r4, r5);
}

static inline void strlen(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r10, double f1, double f2) {
    (void)r4; (void)r5; (void)r6; (void)r7; (void)r8; (void)r9; (void)r10; (void)f1; (void)f2;
    extern size_t __real_strlen(const char*);
    r3 = (uint32_t)__real_strlen((const char*)(uintptr_t)r3);
}

static inline void strcpy(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r10, double f1, double f2) {
    (void)r5; (void)r6; (void)r7; (void)r8; (void)r9; (void)r10; (void)f1; (void)f2;
    extern char* __real_strcpy(char*, const char*);
    __real_strcpy((char*)(uintptr_t)r3, (const char*)(uintptr_t)r4);
}

static inline void strncpy(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r10, double f1, double f2) {
    (void)r6; (void)r7; (void)r8; (void)r9; (void)r10; (void)f1; (void)f2;
    extern char* __real_strncpy(char*, const char*, size_t);
    __real_strncpy((char*)(uintptr_t)r3, (const char*)(uintptr_t)r4, r5);
}

static inline void memcpy(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r10, double f1, double f2) {
    (void)r6; (void)r7; (void)r8; (void)r9; (void)r10; (void)f1; (void)f2;
    extern void* __real_memcpy(void*, const void*, size_t);
    __real_memcpy((void*)(uintptr_t)r3, (const void*)(uintptr_t)r4, r5);
}

static inline void memset(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r10, double f1, double f2) {
    (void)r6; (void)r7; (void)r8; (void)r9; (void)r10; (void)f1; (void)f2;
    extern void* __real_memset(void*, int, size_t);
    __real_memset((void*)(uintptr_t)r3, (int)r4, r5);
}

static inline void memcmp(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r10, double f1, double f2) {
    (void)r6; (void)r7; (void)r8; (void)r9; (void)r10; (void)f1; (void)f2;
    extern int __real_memcmp(const void*, const void*, size_t);
    r3 = (uint32_t)__real_memcmp((const void*)(uintptr_t)r3, (const void*)(uintptr_t)r4, r5);
}

static inline void sprintf(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r10, double f1, double f2) {
    (void)r5; (void)r6; (void)r7; (void)r8; (void)r9; (void)r10; (void)f1; (void)f2;
    // sprintf is variadic - this is a simplification
    // Real implementation would need to handle format string properly
    extern int __real_sprintf(char*, const char*, ...);
    r3 = (uint32_t)__real_sprintf((char*)(uintptr_t)r3, (const char*)(uintptr_t)r4);
}

// Memory allocation
static inline void malloc(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r10, double f1, double f2) {
    (void)r4; (void)r5; (void)r6; (void)r7; (void)r8; (void)r9; (void)r10; (void)f1; (void)f2;
    extern void* __real_malloc(size_t);
    r3 = (uint32_t)(uintptr_t)__real_malloc(r3);
}

static inline void free(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r10, double f1, double f2) {
    (void)r4; (void)r5; (void)r6; (void)r7; (void)r8; (void)r9; (void)r10; (void)f1; (void)f2;
    extern void __real_free(void*);
    __real_free((void*)(uintptr_t)r3);
}

#endif // STDLIB_WRAPPERS_H


