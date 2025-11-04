# Porpoise Tool - Project Structure

## Directory Layout

```
E:\GPT5\Gamecube/
│
├── include/                          # All header files
│   ├── opcode.h                      # Master opcode header (includes all opcodes)
│   ├── porpoise_tool.h               # Transpiler core definitions
│   ├── gecko_memory.h                # Memory structure for emulation
│   │
│   ├── opcode/                       # Individual opcode headers
│   │   ├── README.md                 # Opcode header documentation
│   │   ├── TRANSPILER_DESIGN.md      # Transpiler architecture
│   │   ├── add.h                     # Integer arithmetic
│   │   ├── addi.h
│   │   ├── lis.h
│   │   ├── subf.h
│   │   ├── mulli.h
│   │   ├── mullw.h
│   │   ├── and.h                     # Logical operations
│   │   ├── andi.h
│   │   ├── or.h
│   │   ├── ori.h
│   │   ├── xor.h
│   │   ├── slw.h                     # Shift/rotate
│   │   ├── srw.h
│   │   ├── srawi.h
│   │   ├── rlwinm.h
│   │   ├── cmp.h                     # Comparisons
│   │   ├── cmpi.h
│   │   ├── b.h                       # Branches
│   │   ├── blr.h
│   │   ├── lbz.h                     # Load/store
│   │   ├── lhz.h
│   │   ├── lwz.h
│   │   ├── lwzu.h
│   │   ├── lmw.h
│   │   ├── stb.h
│   │   ├── sth.h
│   │   ├── stw.h
│   │   ├── stwu.h
│   │   ├── stmw.h
│   │   ├── mfspr.h                   # Special purpose registers
│   │   ├── mtspr.h
│   │   ├── mfcr.h
│   │   ├── fadd.h                    # Floating-point
│   │   └── lfs.h
│   │
│   └── USAGE.md                      # Usage guide for headers
│
├── src/                              # All C source files
│   ├── porpoise_tool.c               # Main transpiler implementation
│   ├── transpiler_example.c          # Example transpiler usage
│   ├── gecko_memory_example.c        # Memory structure example
│   └── gecko_memory_example_simplified.c
│
├── bin/                              # Compiled binaries (created by make)
│   ├── porpoise_tool(.exe)           # Main transpiler tool
│   └── *_example(.exe)               # Example executables
│
├── Documentation/                    # Reference documentation
│   ├── OPCODE_CHECKLIST.md           # All 246 opcodes with checkboxes
│   ├── Gecko_Broadway_CPU_Instruction_Set.md
│   ├── Gecko_Broadway_CPU_Architecture.md
│   ├── MEMORY_STRUCTURE_README.md
│   ├── MEMORY_STRUCTURE_CHANGES.md
│   └── PROJECT_STRUCTURE.md          # This file
│
├── Makefile                          # Build system
├── README.md                         # Main project README
├── skip_functions_example.txt        # Example skip list
│
└── (Output from transpiler)
    └── (Generated .c and .h files appear in the input directory)
```

---

## File Organization Rules

### Headers (`include/`)
- **All `.h` files** go in `include/`
- **Opcode headers** go in `include/opcode/`
- **No executable code** (only inline functions and declarations)

### Source Files (`src/`)
- **All `.c` files** go in `src/`
- **Implementations** and executable programs
- **Examples** and test programs

### Binaries (`bin/`)
- **Compiled executables** (created by make)
- **Automatically generated**, not committed to version control

### Documentation
- **Markdown files** in root directory
- **Reference docs** for CPU architecture and instructions
- **Guides** for usage and development

---

## Include Path Structure

When compiling, use `-Iinclude` flag so that source files can:

```c
#include "opcode.h"           // Resolves to include/opcode.h
#include "gecko_memory.h"     // Resolves to include/gecko_memory.h
#include "porpoise_tool.h"    // Resolves to include/porpoise_tool.h
```

---

## Compilation

### Compiler Flags
```bash
gcc -Iinclude -Wall -Wextra -std=c99 -O2
```

- `-Iinclude` - Add include/ to search path
- `-Wall -Wextra` - Enable warnings
- `-std=c99` - C99 standard (for bool, inline, etc.)
- `-O2` - Optimization level 2

### Build Targets

```bash
make              # Build porpoise_tool
make examples     # Build example programs
make clean        # Remove build artifacts
make help         # Show help
```

---

## Adding New Files

### Adding a New Opcode Header
1. Create `include/opcode/newopcode.h`
2. Follow template from existing headers
3. Add `#include "opcode/newopcode.h"` to `include/opcode.h`
4. Update count in `get_implemented_opcode_count()`
5. Add decode/transpile call in `src/porpoise_tool.c`

### Adding a New Feature
1. Declare in `include/porpoise_tool.h` (if header-only)
2. Implement in `src/porpoise_tool.c` (if needs compilation)
3. Update `Makefile` if adding new source files
4. Document in appropriate .md file

### Adding Examples
1. Create `src/example_name.c`
2. Add target to `Makefile`
3. Document in `README.md`

---

## Workflow

### For Transpiler Development:
1. Add new opcode headers in `include/opcode/`
2. Update `include/opcode.h` to include them
3. Add decode logic to `src/porpoise_tool.c`
4. Test with sample assembly
5. Update `OPCODE_CHECKLIST.md`

### For Using Porpoise Tool:
1. Build: `make`
2. Prepare .s files in a directory
3. (Optional) Create skip list
4. Run: `bin/porpoise_tool <dir> [skip_list]`
5. Find generated .c and .h files in input directory

---

## Design Principles

### 1. Modularity
Each opcode is self-contained in its own header file. Easy to add, test, and maintain.

### 2. Simplicity
- Bool types for flags (not bitfields)
- Direct pointers (not split high/low registers)
- Clean, readable generated code

### 3. Accuracy
- Faithful to PowerPC semantics
- Handles edge cases (Rc bit, OE bit, pseudo-ops)
- Preserves original addresses in comments

### 4. Extensibility
- Easy to add new opcodes
- Skip list for flexibility
- Modular design for future features

---

## Notes

- **Porpoise** is a joke on **Dolphin** (GameCube codename)
- Project targets GameCube (Gekko CPU) and Wii (Broadway CPU)
- Designed for decompilation projects, not runtime emulation
- Generated C code is meant to be further refined/decompiled

---

**Last Updated:** November 3, 2025  
**Implemented Opcodes:** 26 / 246 (10.6%)

