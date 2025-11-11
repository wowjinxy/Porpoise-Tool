/**
 * @file function_address_map.c
 * @brief Function Address Map Implementation
 * 
 * Maps GameCube function addresses to transpiled C function pointers,
 * allowing indirect calls to be resolved to direct function calls.
 */

#include "function_address_map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FUNCTION_MAP_ENTRIES 10000

static FunctionAddressEntry function_map[MAX_FUNCTION_MAP_ENTRIES];
static int function_map_count = 0;
static bool initialized = false;

bool function_address_map_init(void) {
    if (initialized) return true;
    
    memset(function_map, 0, sizeof(function_map));
    function_map_count = 0;
    initialized = true;
    return true;
}

void function_address_map_register(uint32_t gc_address, TranspiledFunctionPtr func_ptr, const char *name) {
    if (!initialized) {
        fprintf(stderr, "ERROR: Function address map not initialized before registering function %s (0x%08X)\n", 
                name ? name : "unknown", gc_address);
        return;
    }
    
    if (function_map_count >= MAX_FUNCTION_MAP_ENTRIES) {
        fprintf(stderr, "ERROR: Function address map full! Cannot register %s (0x%08X)\n", 
                name ? name : "unknown", gc_address);
        return;
    }
    
    // Check if address already registered
    for (int i = 0; i < function_map_count; i++) {
        if (function_map[i].gc_address == gc_address) {
            // Update existing entry
            function_map[i].func_ptr = func_ptr;
            function_map[i].name = name;
            return;
        }
    }
    
    // Add new entry
    function_map[function_map_count].gc_address = gc_address;
    function_map[function_map_count].func_ptr = func_ptr;
    function_map[function_map_count].name = name;
    function_map_count++;
}

void call_function_by_address(uint32_t gc_address, uintptr_t r3, uintptr_t r4, uintptr_t r5, uintptr_t r6,
                              uintptr_t r7, uintptr_t r8, uintptr_t r9, uintptr_t r10,
                              double f1, double f2) {
    if (!initialized) {
        fprintf(stderr, "FATAL ERROR: Indirect call to 0x%08X before function address map initialized!\n", gc_address);
        exit(1);
    }
    
    // Linear search for the address (could be optimized with hash map)
    for (int i = 0; i < function_map_count; i++) {
        if (function_map[i].gc_address == gc_address) {
            // Call the transpiled C function directly
            if (function_map[i].func_ptr) {
                function_map[i].func_ptr(r3, r4, r5, r6, r7, r8, r9, r10, f1, f2);
                return;
            }
        }
    }
    
    fprintf(stderr, "FATAL ERROR: Unresolved indirect call to GameCube address 0x%08X\n", gc_address);
    exit(1);
}

