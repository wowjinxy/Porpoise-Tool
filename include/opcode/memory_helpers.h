/**
 * @file memory_helpers.h
 * @brief Memory access helpers for address translation
 * 
 * GameCube memory starts at 0x80000000, but our mem[] array starts at 0.
 * All memory accesses must subtract the base address.
 */

#ifndef OPCODE_MEMORY_HELPERS_H
#define OPCODE_MEMORY_HELPERS_H

// GameCube memory base address
#define GC_MEM_BASE 0x80000000

// Memory access macros that handle address translation
// These subtract 0x80000000 from addresses to map to our mem[] array

// For expressions that might have the base already subtracted or be small offsets
#define MEM_ADDR(addr_expr) "(((" #addr_expr ") & 0xFF000000) ? (" #addr_expr ") - " STR(GC_MEM_BASE) " : (" #addr_expr "))"

// Helper to stringify
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#endif // OPCODE_MEMORY_HELPERS_H

