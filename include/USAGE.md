# Opcode Headers Usage Guide

## Quick Start

### Include All Opcodes

```c
#include "opcode.h"  // Master header - includes all implemented opcodes
```

This single include gives you access to all implemented opcode headers:
- `add.h`, `addi.h`, `lwz.h`, `stw.h`, `fadd.h`, `mfspr.h`
- All decode, transpile, and comment functions

### Basic Usage Pattern

```c
#include "opcode.h"

void transpile_instruction(uint32_t instruction) {
    char c_code[512];
    char comment[128];
    
    // Try decoding as ADDI
    ADDI_Instruction addi;
    if (decode_addi(instruction, &addi)) {
        transpile_addi(&addi, c_code, sizeof(c_code));
        comment_addi(&addi, comment, sizeof(comment));
        printf("%s  // %s\n", c_code, comment);
        return;
    }
    
    // Try decoding as STW
    STW_Instruction stw;
    if (decode_stw(instruction, &stw)) {
        transpile_stw(&stw, c_code, sizeof(c_code));
        comment_stw(&stw, comment, sizeof(comment));
        printf("%s  // %s\n", c_code, comment);
        return;
    }
    
    // Add more as needed...
    
    printf("/* UNKNOWN: 0x%08X */\n", instruction);
}
```

## Available Functions

Each opcode provides three functions:

### 1. Decode Function
```c
bool decode_<opcode>(uint32_t instruction, <OPCODE>_Instruction *decoded)
```

**Purpose:** Parse instruction and extract operands  
**Returns:** `true` if instruction matches this opcode, `false` otherwise

**Example:**
```c
ADDI_Instruction inst;
if (decode_addi(0x38630010, &inst)) {
    // inst.rD = 3, inst.rA = 3, inst.SIMM = 0x10
}
```

### 2. Transpile Function
```c
int transpile_<opcode>(const <OPCODE>_Instruction *decoded, 
                       char *output, 
                       size_t output_size)
```

**Purpose:** Generate C code as a string  
**Returns:** Number of characters written (like `snprintf`)

**Example:**
```c
char code[256];
transpile_addi(&inst, code, sizeof(code));
// code = "r3 = r3 + 0x10;"
```

### 3. Comment Function
```c
int comment_<opcode>(const <OPCODE>_Instruction *decoded,
                     char *output,
                     size_t output_size)
```

**Purpose:** Generate assembly-style comment  
**Returns:** Number of characters written

**Example:**
```c
char comment[128];
comment_addi(&inst, comment, sizeof(comment));
// comment = "addi r3, r3, 0x10"
```

## Currently Implemented (6 opcodes)

| Opcode | Header | Decode | Transpile | Comment |
|--------|--------|--------|-----------|---------|
| `add` | `add.h` | `decode_add()` | `transpile_add()` | `comment_add()` |
| `addi` | `addi.h` | `decode_addi()` | `transpile_addi()` | `comment_addi()` |
| `lwz` | `lwz.h` | `decode_lwz()` | `transpile_lwz()` | `comment_lwz()` |
| `stw` | `stw.h` | `decode_stw()` | `transpile_stw()` | `comment_stw()` |
| `fadd` | `fadd.h` | `decode_fadd()` | `transpile_fadd()` | `comment_fadd()` |
| `mfspr` | `mfspr.h` | `decode_mfspr()` | `transpile_mfspr()` | `comment_mfspr()` |

## Progress Tracking

```c
#include "opcode.h"

int main() {
    printf("Implemented: %d / 246 opcodes\n", get_implemented_opcode_count());
    printf("Progress: %.1f%%\n", get_implementation_progress());
}
```

## Full Transpiler Example

```c
#include <stdio.h>
#include "opcode.h"

int main() {
    // Example PowerPC instructions
    uint32_t instructions[] = {
        0x38630010,  // addi r3, r3, 0x10
        0x90830020,  // stw r4, 0x20(r3)
        0x7C8802A6,  // mflr r4
        0x7C842A14,  // add r4, r4, r5
    };
    
    printf("void example_function() {\n");
    
    for (int i = 0; i < 4; i++) {
        uint32_t inst = instructions[i];
        char c_code[256];
        char comment[128];
        bool decoded = false;
        
        // Try each opcode (in a real transpiler, use a lookup table)
        ADDI_Instruction addi;
        if (decode_addi(inst, &addi)) {
            transpile_addi(&addi, c_code, sizeof(c_code));
            comment_addi(&addi, comment, sizeof(comment));
            decoded = true;
        }
        
        STW_Instruction stw;
        if (!decoded && decode_stw(inst, &stw)) {
            transpile_stw(&stw, c_code, sizeof(c_code));
            comment_stw(&stw, comment, sizeof(comment));
            decoded = true;
        }
        
        MFSPR_Instruction mfspr;
        if (!decoded && decode_mfspr(inst, &mfspr)) {
            transpile_mfspr(&mfspr, c_code, sizeof(c_code));
            comment_mfspr(&mfspr, comment, sizeof(comment));
            decoded = true;
        }
        
        ADD_Instruction add;
        if (!decoded && decode_add(inst, &add)) {
            transpile_add(&add, c_code, sizeof(c_code));
            comment_add(&add, comment, sizeof(comment));
            decoded = true;
        }
        
        if (decoded) {
            printf("    %s  // %s\n", c_code, comment);
        } else {
            printf("    /* UNKNOWN: 0x%08X */\n", inst);
        }
    }
    
    printf("}\n");
    return 0;
}
```

**Output:**
```c
void example_function() {
    r3 = r3 + 0x10;  // addi r3, r3, 0x10
    *(uint32_t*)(mem + r3 + 0x20) = r4;  // stw r4, 0x20(r3)
    r4 = lr;  // mflr r4
    r4 = r4 + r5;  // add r4, r4, r5
}
```

## Adding New Opcodes

1. **Create header**: `include/opcode/myopcode.h`
   - Follow the pattern from existing headers
   - Implement `decode_*()`, `transpile_*()`, `comment_*()`

2. **Add to master header**: `include/opcode.h`
   - Uncomment or add `#include "opcode/myopcode.h"`
   - Update `get_implemented_opcode_count()`

3. **Update checklist**: `OPCODE_CHECKLIST.md`
   - Mark `[x]` for the completed opcode

4. **Test**: Create a test case in your transpiler

## Tips

### Optimization: Use a Lookup Table
Instead of trying each opcode sequentially, use the primary opcode as an index:

```c
typedef bool (*decode_func_t)(uint32_t, void*);

decode_func_t decode_table[64] = {
    [14] = (decode_func_t)decode_addi,
    [31] = (decode_func_t)decode_add,  // Note: Extended opcodes need sub-table
    [32] = (decode_func_t)decode_lwz,
    [36] = (decode_func_t)decode_stw,
    // ...
};
```

### Handle Extended Opcodes (Primary 31, 63, etc.)
Some primary opcodes have extended opcodes. Use a two-level lookup:

```c
// Primary opcode 31 has many extended opcodes
if (primary == 31) {
    uint32_t extended = (inst >> 1) & 0x3FF;
    switch (extended) {
        case 266: decode_add(...); break;
        case 339: decode_mfspr(...); break;
        // ...
    }
}
```

### Combine Similar Instructions
Instructions like `lwz`, `lwzu`, `lwzx`, `lwzux` can share logic:

```c
// All load word variants
bool decode_lwz_family(uint32_t inst, LWZ_Instruction *decoded) {
    // Check if any LWZ variant
    // Set decoded->variant field
}
```

## See Also

- `transpiler_example.c` - Complete working example
- `OPCODE_CHECKLIST.md` - List of all 246 opcodes to implement
- `opcode/README.md` - Detailed opcode header documentation
- `opcode/TRANSPILER_DESIGN.md` - Transpiler architecture guide

---

**Last Updated:** November 3, 2025  
**Progress:** 6 / 246 opcodes (2.4%)

