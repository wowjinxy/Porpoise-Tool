/**
 * @file runtime_init.c
 * @brief Runtime Initialization Implementation
 */

#include "runtime_init.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Global runtime state
static void* g_ppc_memory = NULL;
static size_t g_memory_size = 0;
static uint32_t g_memory_base = 0x80000000;  // Default GameCube base
static int g_diagnostics_enabled = 0;

// Statistics
static struct {
    uint64_t memory_allocations;
    uint64_t memory_frees;
    uint64_t div64_calls;
    uint64_t mod64_calls;
    uint64_t mul64_calls;
} g_stats = {0};

//==============================================================================
// RUNTIME INITIALIZATION
//==============================================================================

void ppc_runtime_init(void) {
    // Initialize statistics
    memset(&g_stats, 0, sizeof(g_stats));
    
    if (g_diagnostics_enabled) {
        printf("[PPC Runtime] Initialized\n");
        printf("[PPC Runtime] Memory base: 0x%08X\n", g_memory_base);
    }
}

void ppc_runtime_cleanup(void) {
    // Free any allocated memory
    if (g_ppc_memory) {
        free(g_ppc_memory);
        g_ppc_memory = NULL;
        g_memory_size = 0;
    }
    
    if (g_diagnostics_enabled) {
        printf("[PPC Runtime] Cleanup complete\n");
        ppc_print_stats();
    }
}

//==============================================================================
// MEMORY MANAGEMENT
//==============================================================================

void* ppc_alloc_memory(size_t size) {
    if (g_ppc_memory) {
        fprintf(stderr, "[PPC Runtime] Warning: Memory already allocated, freeing...\n");
        free(g_ppc_memory);
    }
    
    g_ppc_memory = malloc(size);
    if (!g_ppc_memory) {
        fprintf(stderr, "[PPC Runtime] ERROR: Failed to allocate %zu bytes\n", size);
        return NULL;
    }
    
    g_memory_size = size;
    memset(g_ppc_memory, 0, size);
    
    g_stats.memory_allocations++;
    
    if (g_diagnostics_enabled) {
        printf("[PPC Runtime] Allocated %zu bytes at %p\n", size, g_ppc_memory);
    }
    
    return g_ppc_memory;
}

void ppc_free_memory(void* ptr) {
    if (ptr == g_ppc_memory) {
        free(ptr);
        g_ppc_memory = NULL;
        g_memory_size = 0;
        g_stats.memory_frees++;
        
        if (g_diagnostics_enabled) {
            printf("[PPC Runtime] Freed memory\n");
        }
    } else {
        fprintf(stderr, "[PPC Runtime] Warning: Attempt to free unknown pointer\n");
    }
}

void ppc_set_memory_base(uint32_t base_addr) {
    g_memory_base = base_addr;
    
    if (g_diagnostics_enabled) {
        printf("[PPC Runtime] Memory base set to 0x%08X\n", base_addr);
    }
}

//==============================================================================
// DEBUGGING AND DIAGNOSTICS
//==============================================================================

void ppc_set_diagnostics(int enable) {
    g_diagnostics_enabled = enable;
    
    if (enable) {
        printf("[PPC Runtime] Diagnostics enabled\n");
    }
}

void ppc_print_stats(void) {
    printf("\n");
    printf("========================================\n");
    printf("PowerPC Runtime Statistics\n");
    printf("========================================\n");
    printf("Memory:\n");
    printf("  Allocations: %llu\n", g_stats.memory_allocations);
    printf("  Frees: %llu\n", g_stats.memory_frees);
    printf("  Current size: %zu bytes\n", g_memory_size);
    printf("\n");
    printf("64-bit Operations:\n");
    printf("  Divisions: %llu\n", g_stats.div64_calls);
    printf("  Modulos: %llu\n", g_stats.mod64_calls);
    printf("  Multiplications: %llu\n", g_stats.mul64_calls);
    printf("========================================\n");
    printf("\n");
}

//==============================================================================
// RUNTIME HOOKS FOR STATISTICS
//==============================================================================

// These can be called by the intrinsic functions to track usage
void __ppc_stat_div64(void) {
    g_stats.div64_calls++;
}

void __ppc_stat_mod64(void) {
    g_stats.mod64_calls++;
}

void __ppc_stat_mul64(void) {
    g_stats.mul64_calls++;
}

