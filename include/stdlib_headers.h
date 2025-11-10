/**
 * @file stdlib_headers.h
 * @brief Standard library headers for transpiled code
 * 
 * This header includes all standard C library headers needed for
 * transpiled GameCube/Wii PowerPC assembly code.
 * Include this in all generated .c files.
 */

#ifndef STDLIB_HEADERS_H
#define STDLIB_HEADERS_H

// Standard library includes (BEFORE redeclarations)
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// PowerPC runtime library (64-bit intrinsics)
#include "ppc_runtime.h"

// Include stdlib stub declarations that match transpiler's 10-parameter signature
// These override the standard library declarations
#include "stdlib_stubs.h"

// Suppress warnings for unused parameters (common in transpiled code)
#ifdef _MSC_VER
    #pragma warning(disable: 4100)  // unreferenced formal parameter
    #pragma warning(disable: 4189)  // local variable initialized but not used
    #pragma warning(disable: 4702)  // unreachable code
    #pragma warning(disable: 4310)  // cast truncates constant value
    #pragma warning(disable: 4146)  // unary minus on unsigned type
    #pragma warning(disable: 4312)  // type cast to greater size
    #pragma warning(disable: 4244)  // conversion, possible loss of data
    #pragma warning(disable: 4267)  // conversion, possible loss of data
#endif

#endif // STDLIB_HEADERS_H

