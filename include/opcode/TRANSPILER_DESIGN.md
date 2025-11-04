# PowerPC to C Transpiler Design

## Overview

This project is a **transpiler** that converts PowerPC assembly (from GameCube/Wii binaries) into equivalent C code. Each opcode header provides:

1. **Decode function** - Parse the 32-bit instruction
2. **Transpile function** - Generate equivalent C code as a string
3. **Comment function** - Generate assembly-style comments

## Architecture

```
PowerPC Binary → Decoder → Transpiler → C Code
   (machine code)  (headers)  (headers)   (output)
```

### Example Flow

**Input (Assembly):**
```asm
lis r3, 0x8059
addi r3, r3, 0xBEC8
stw r4, 0x1a4(r3)
```

**Output (C Code):**
```c
r3 = 0x8059 << 16;     // lis r3, 0x8059
r3 = r3 + 0xBEC8;      // addi r3, r3, 0xBEC8
*(uint32_t*)(mem + r3 + 0x1a4) = r4;  // stw r4, 0x1a4(r3)
```

## Header File Structure

Each opcode header follows this pattern:

```c
/**
 * @file <opcode>.h
 * @brief <INSTRUCTION> - Description
 */

#ifndef OPCODE_<NAME>_H
#define OPCODE_<NAME>_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Opcode constants
#define OP_<NAME>_PRIMARY    <value>

// Masks and shifts for decoding
#define <NAME>_OPCD_MASK     0xFC000000
// ... more masks

// Decoded instruction structure
typedef struct {
    uint8_t rD;
    uint8_t rA;
    // ... operand fields
} <NAME>_Instruction;

// Decode function
static inline bool decode_<name>(uint32_t instruction, 
                                  <NAME>_Instruction *decoded);

// Transpile function - GENERATES C CODE
static inline int transpile_<name>(const <NAME>_Instruction *decoded,
                                    char *output,
                                    size_t output_size);

// Comment function - GENERATES ASSEMBLY COMMENT
static inline int comment_<name>(const <NAME>_Instruction *decoded,
                                  char *output,
                                  size_t output_size);

#endif
```

## Transpile Function Design

The `transpile_*()` function generates C code as a string:

```c
static inline int transpile_add(const ADD_Instruction *decoded,
                                char *output,
                                size_t output_size) {
    return snprintf(output, output_size,
                   "r%u = r%u + r%u;",
                   decoded->rD, decoded->rA, decoded->rB);
}
```

### Return Value
Returns the number of characters written (excluding null terminator), compatible with `snprintf()`.

### Buffer Management
- Use `snprintf()` for safe string formatting
- Track written bytes if appending multiple statements
- Check buffer size before writing

## Variable Naming Conventions

### Registers
- **GPRs (r0-r31)**: `r0`, `r1`, ..., `r31`
- **FPRs (f0-f31)**: `f0`, `f1`, ..., `f31`
- **Special registers**: lowercase names
  - `lr` - Link Register
  - `ctr` - Count Register
  - `xer` - Fixed-Point Exception Register
  - `cr0`, `cr1`, ..., `cr7` - Condition Register fields
  - `gqr0`-`gqr7` - Graphics Quantization Registers
  - `srr0`, `srr1` - Save/Restore Registers

### Memory Access
- `mem` - Base pointer to emulated memory
- Use pointer arithmetic: `*(uint32_t*)(mem + address)`
- Cast to appropriate type: `uint8_t*`, `uint16_t*`, `uint32_t*`

### Pseudo-Code Elements
```c
// Arithmetic
r3 = r4 + r5;                    // add r3, r4, r5
r3 = r4 + 100;                   // addi r3, r4, 100
r3 = 42;                         // li r3, 42

// Memory operations
r3 = *(uint32_t*)(mem + r4);    // lwz r3, 0(r4)
*(uint32_t*)(mem + r3) = r4;    // stw r4, 0(r3)

// Special register access
r3 = lr;                         // mflr r3
ctr = r3;                        // mtctr r3

// Condition codes
if ((int32_t)r3 < 0) { ... }    // Check sign
cr0 = (r3 == 0 ? 0x2 : 0x4);    // Set CR0
```

## Common Patterns

### 1. Load Immediate Address (lis + addi)

**Assembly:**
```asm
lis r3, lbl@ha       # Load high bits
addi r3, r3, lbl@l   # Add low bits
```

**Transpiled:**
```c
r3 = 0x8059 << 16;   // lis r3, 0x8059
r3 = r3 + 0xBEC8;    // addi r3, r3, 0xBEC8
```

**Optimized (optional):**
```c
r3 = 0x8058BEC8;     // lis/addi pair for lbl_8058BEC8
```

### 2. Memory Access with Offset

**Assembly:**
```asm
lwz r4, 0x1a4(r3)
stw r4, 0x20(r5)
```

**Transpiled:**
```c
r4 = *(uint32_t*)(mem + r3 + 0x1a4);
*(uint32_t*)(mem + r5 + 0x20) = r4;
```

### 3. Branch Instructions

**Assembly:**
```asm
b target
beq target
bne cr0, target
```

**Transpiled:**
```c
goto label_80001234;                    // b target
if (cr0 & 0x2) goto label_80001234;     // beq
if (!(cr0 & 0x2)) goto label_80001234;  // bne
```

### 4. Function Calls

**Assembly:**
```asm
bl function
blr
```

**Transpiled:**
```c
lr = pc + 4; goto function;  // bl function
return;                      // blr (or goto lr in some cases)
```

## Instruction Variants

### Record Bit (Rc)
Instructions with `.` suffix update CR0:

```c
// add r3, r4, r5   (Rc=0)
r3 = r4 + r5;

// add. r3, r4, r5  (Rc=1)
r3 = r4 + r5;
cr0 = ((int32_t)r3 < 0 ? 0x8 : (int32_t)r3 > 0 ? 0x4 : 0x2);
```

### Overflow Enable (OE)
Instructions with `o` suffix check overflow:

```c
// addo r3, r4, r5
r3 = r4 + r5;
if (/* overflow condition */) {
    xer |= 0xC0000000;  // Set SO and OV
} else {
    xer &= ~0x80000000; // Clear OV
}
```

## Memory Model

### Address Space
The transpiled code assumes a flat memory model:
```c
uint8_t mem[24 * 1024 * 1024];  // 24 MB for GameCube
```

### Endianness
PowerPC is big-endian. Memory accesses should preserve this:
```c
// For little-endian host, may need byte swapping
uint32_t value = __builtin_bswap32(*(uint32_t*)(mem + addr));
```

Or use helper macros:
```c
#define READ32(addr)  (/* big-endian read */)
#define WRITE32(addr, val)  (/* big-endian write */)
```

## Function Generation

### Function Prologue
```c
void fn_80428398() {
    // Register declarations
    uint32_t r0, r1, r2, ..., r31;
    double f0, f1, ..., f31;
    uint32_t lr, ctr, xer;
    uint32_t cr0, cr1, ..., cr7;
    
    // Function body (transpiled instructions)
    // ...
}
```

### Label Handling
Convert branch targets to C labels:
```c
label_80428420:
    r4 = *(uint16_t*)(mem + r3 + 0x1a2);
    r4 = r4 | 0x1;
    *(uint16_t*)(mem + r3 + 0x1a2) = r4;
    goto label_803D4BBC;
```

## Optimization Opportunities

### 1. Constant Folding
```c
// Before:
r3 = 0x8059 << 16;
r3 = r3 + 0xBEC8;

// After:
r3 = 0x8058BEC8;
```

### 2. Dead Code Elimination
```c
// If r3 is overwritten before use, previous assignment is dead
r3 = 100;  // Dead if next line executes
r3 = 200;  // This is the actual value used
```

### 3. Register Allocation
```c
// PowerPC uses r3-r10 for function arguments
// These could map to C function parameters
void fn_example(uint32_t r3, uint32_t r4) {
    // Use r3, r4 directly as parameters
}
```

## Special Cases

### Pseudo-Instructions
Some assembly is actually pseudo-ops:
- `li r3, 100` → `addi r3, r0, 100`
- `mr r3, r4` → `or r3, r4, r4`
- `nop` → `ori r0, r0, 0`

Transpiler should recognize and simplify these:
```c
// li r3, 100
r3 = 100;  // Not "r3 = 0 + 100;"

// mr r3, r4
r3 = r4;   // Not "r3 = r4 | r4;"

// nop
// (omit entirely or comment)
```

### Paired-Single (PS) Instructions
Gekko-specific SIMD operations:
```c
// ps_add f1, f2, f3
f1.ps0 = f2.ps0 + f3.ps0;
f1.ps1 = f2.ps1 + f3.ps1;

// Or with explicit unpacking:
float *f1_ps = (float*)&f1;
float *f2_ps = (float*)&f2;
float *f3_ps = (float*)&f3;
f1_ps[0] = f2_ps[0] + f3_ps[0];
f1_ps[1] = f2_ps[1] + f3_ps[1];
```

## Testing Strategy

### Unit Tests
Test each opcode header independently:
```c
void test_add_transpile() {
    uint32_t instruction = 0x7C632214;  // add r3, r3, r4
    ADD_Instruction decoded;
    char output[256];
    
    assert(decode_add(instruction, &decoded));
    transpile_add(&decoded, output, sizeof(output));
    
    assert(strcmp(output, "r3 = r3 + r4;") == 0);
}
```

### Integration Tests
Test full function transpilation:
```c
void test_function_transpile() {
    // Feed in a sequence of instructions
    // Verify generated C code compiles
    // Verify behavior matches original
}
```

## Toolchain Integration

### Input Format
Expected input: PowerPC assembly or machine code
```
0x80428398  7C7043A6  mtsprg 0, r3
0x8042839C  3C608059  lis r3, 0x8059
0x804283A0  3863BEC8  addi r3, r3, 0xBEC8
```

### Output Format
Generated C code with comments:
```c
void fn_80428398() {
    sprg0 = r3;                              // 0x80428398: mtsprg 0, r3
    r3 = 0x8059 << 16;                       // 0x8042839C: lis r3, 0x8059
    r3 = r3 + 0xBEC8;                        // 0x804283A0: addi r3, r3, 0xBEC8
}
```

## Future Enhancements

1. **Control Flow Analysis** - Detect functions, loops, conditionals
2. **Type Inference** - Determine variable types from usage
3. **Decompilation** - Generate higher-level C (not just transpilation)
4. **Symbol Resolution** - Use symbol tables to name functions/variables
5. **Optimization Passes** - Constant folding, dead code elimination, etc.

---

**See also:**
- `transpiler_example.c` - Working example
- `OPCODE_CHECKLIST.md` - List of all opcodes to implement
- `README.md` - General opcode header documentation

