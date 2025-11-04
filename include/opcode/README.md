# Opcode Header Files

This directory contains individual header files for each PowerPC opcode supported by the Gecko/Broadway CPU.

## Structure

Each opcode header file follows a consistent pattern:

```c
/**
 * @file <mnemonic>.h
 * @brief <INSTRUCTION_NAME> - Description
 * 
 * Opcode: <primary> (and extended if applicable)
 * Format: <instruction format>
 * Syntax: <assembly syntax>
 */

#ifndef OPCODE_<MNEMONIC>_H
#define OPCODE_<MNEMONIC>_H

#include <stdint.h>
#include <stdbool.h>

// Opcode encoding constants
#define OP_<NAME>_PRIMARY      <value>
#define OP_<NAME>_EXTENDED     <value>  // if applicable

// Instruction format masks and shifts
// ...

// Decoded instruction structure
typedef struct {
    // Operand fields
} <NAME>_Instruction;

// Decode function
static inline bool decode_<name>(uint32_t instruction, <NAME>_Instruction *decoded);

// Execute function
static inline void execute_<name>(const <NAME>_Instruction *decoded, ...);

#endif
```

## Usage Example

```c
#include "opcode/add.h"
#include "gecko_memory.h"

// Decode an instruction
uint32_t instruction = 0x7C632214;  // add r3, r3, r4
ADD_Instruction decoded;

if (decode_add(instruction, &decoded)) {
    // Execute the instruction
    uint32_t gpr[32];  // General purpose registers
    uint32_t xer = 0;  // XER register
    uint32_t cr0 = 0;  // CR0 field
    
    execute_add(&decoded, gpr, &xer, &cr0);
}
```

## Instruction Formats

PowerPC instructions use several standard formats:

### D-Form (Immediate)
```
| OPCD | rD/rS | rA | d (immediate) |
  0-5    6-10   11-15   16-31
```
Example: `addi r3, r4, 100`

### X-Form (Register)
```
| OPCD | rD/rS | rA | rB | XO | Rc |
  0-5    6-10   11-15 16-20 21-30 31
```
Example: `add r3, r4, r5`

### XO-Form (Overflow)
```
| OPCD | rD | rA | rB | OE | XO | Rc |
  0-5   6-10 11-15 16-20 21 22-30 31
```
Example: `addo. r3, r4, r5`

### I-Form (Branch)
```
| OPCD | LI (24-bit) | AA | LK |
  0-5    6-29          30   31
```
Example: `b target`

### B-Form (Conditional Branch)
```
| OPCD | BO | BI | BD (14-bit) | AA | LK |
  0-5   6-10 11-15 16-29        30   31
```
Example: `beq target`

### A-Form (Floating-Point)
```
| OPCD | frD | frA | frB | frC | XO | Rc |
  0-5   6-10  11-15 16-20 21-25 26-30 31
```
Example: `fmadd f1, f2, f3, f4`

## Instruction Categories

See `OPCODE_CHECKLIST.md` for the complete list organized by category:

1. **Integer Arithmetic** (35 instructions)
2. **Logical Operations** (14 instructions)
3. **Shift and Rotate** (7 instructions)
4. **Comparison** (4 instructions)
5. **Branch** (8 base + extended mnemonics)
6. **Load/Store** (38 instructions)
7. **Floating-Point** (30 instructions)
8. **FP Load/Store** (17 instructions)
9. **Cache Management** (7 instructions)
10. **Special Purpose Registers** (16 instructions)
11. **FP Status/Control** (6 instructions)
12. **Condition Register** (9 instructions)
13. **System** (12 instructions)
14. **Gekko Paired-Single** (~50 instructions)

## Decode Functions

All decode functions follow this pattern:

```c
bool decode_<name>(uint32_t instruction, <NAME>_Instruction *decoded)
```

**Returns:**
- `true` if instruction was successfully decoded
- `false` if instruction doesn't match this opcode

**Parameters:**
- `instruction` - The 32-bit instruction word (big-endian)
- `decoded` - Pointer to structure to fill with decoded fields

## Execute Functions

Execute functions vary by instruction type but generally follow:

```c
void execute_<name>(const <NAME>_Instruction *decoded, 
                   uint32_t *gpr,           // General purpose registers
                   uint32_t *xer,           // XER register (if needed)
                   uint32_t *cr0,           // CR0 field (if Rc=1)
                   /* other context as needed */);
```

**Common Parameters:**
- `gpr` - Array of 32 general-purpose registers
- `fpr` - Array of 32 floating-point registers (for FP ops)
- `xer` - XER register for overflow/carry
- `cr0/cr1` - Condition register fields (if record bit set)
- Memory access callbacks for load/store operations

## Naming Conventions

### File Names
- Use lowercase: `add.h`, `lwz.h`, `fadd.h`
- Use underscore for multi-word: `ps_add.h`, `psq_l.h`
- Use `_dot` for record bit suffix: `addic_dot.h` (for `addic.`)

### Constants
- Use UPPERCASE: `OP_ADD_PRIMARY`, `ADD_RT_MASK`
- Prefix with opcode name: `ADD_`, `LWZ_`, etc.

### Structures
- CamelCase with opcode: `ADD_Instruction`, `LWZ_Instruction`

### Functions
- Lowercase with underscores: `decode_add()`, `execute_add()`

## Implementation Guidelines

1. **Include Guards**: Always use `#ifndef OPCODE_<NAME>_H`

2. **Documentation**: Include opcode number, format, and syntax

3. **Masks and Shifts**: Define constants for all bit fields

4. **Validation**: Decode functions should validate opcode matches

5. **Variants**: Handle all instruction variants (Rc, OE, etc.)

6. **Inline Functions**: Use `static inline` for performance

7. **Dependencies**: Minimize external dependencies

8. **Testing**: Each opcode should be independently testable

## Special Cases

### Record Bit (Rc)
Many instructions have a variant with `.` suffix that updates CR0 or CR1.
Handle this with a bool field in the decoded structure.

### Overflow Enable (OE)
Some arithmetic instructions have overflow variants with `o` suffix.
Handle with bool field and update XER[SO] and XER[OV].

### Extended Mnemonics
Instructions like `mr` (move register) are aliases for `or rA,rS,rS`.
These can share the same header file as their base instruction.

### Paired-Single Instructions
Gekko-specific instructions operate on two 32-bit floats packed in a 64-bit FPR.
Treat each FPR as containing PS0 (high 32 bits) and PS1 (low 32 bits).

## Testing

Example test structure:

```c
#include "opcode/add.h"

void test_add() {
    uint32_t instruction = 0x7C632214;  // add r3, r3, r4
    ADD_Instruction decoded;
    
    assert(decode_add(instruction, &decoded));
    assert(decoded.rD == 3);
    assert(decoded.rA == 3);
    assert(decoded.rB == 4);
    assert(decoded.OE == false);
    assert(decoded.Rc == false);
    
    uint32_t gpr[32] = {0};
    gpr[3] = 10;
    gpr[4] = 20;
    
    execute_add(&decoded, gpr, NULL, NULL);
    assert(gpr[3] == 30);
}
```

## Progress Tracking

See `OPCODE_CHECKLIST.md` for:
- Complete list of all opcodes to implement
- Checkboxes to track progress
- Priority ratings (high/medium/low)
- Current progress: 3/246 (1.2%)

## References

- **PowerPC ISA Manual**: Primary reference for instruction encoding
- **Gekko User Manual**: Nintendo-specific paired-single extensions
- **IBM PowerPC 750CXe**: Base architecture documentation
- **Gecko_Broadway_CPU_Instruction_Set.md**: Complete instruction reference

---

**Last Updated:** November 3, 2025

