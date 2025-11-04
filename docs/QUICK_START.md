# Porpoise Tool - Quick Start Guide

## 🚀 Get Started in 3 Steps

### Step 1: Build
```bash
make
```

Creates `bin/porpoise_tool` (or `bin/porpoise_tool.exe` on Windows)

### Step 2: Prepare Assembly Files
Put your PowerPC `.s` files in a directory:
```bash
mkdir my_asm
cp game_dump/*.s my_asm/
```

### Step 3: Transpile
```bash
bin/porpoise_tool my_asm
```

Done! Find `.c` and `.h` files in `my_asm/` directory.

---

## 📂 Project Organization

```
include/         ← All .h files (headers)
src/             ← All .c files (source code)
bin/             ← Compiled programs (created by make)
```

**Remember:**
- Headers go in `include/`
- Source files go in `src/`

---

## 📝 Assembly File Format

Your `.s` files should look like this:

```asm
.include "macros.inc"

.fn my_function, global

/* 80000000 00000000  38 60 00 01 */	li r3, 1
/* 80000004 00000004  4E 80 00 20 */	blr

.endfn my_function
```

**Key elements:**
- `.fn name` - Starts a function
- `/* address offset  bytes */  mnemonic operands` - Instruction
- `.endfn` - Ends a function
- `.lbl_xxxxx` - Labels
- `.include` - Include directives

---

## ⚙️ Common Tasks

### Transpile Single File
```bash
bin/porpoise_tool my_file.s
```

### Transpile Directory
```bash
bin/porpoise_tool ./asm_dir
```

### Use Skip List
```bash
bin/porpoise_tool ./asm_dir skip_functions.txt
```

**skip_functions.txt:**
```
OSReport
memcpy
strlen
```

### Build Examples
```bash
make examples
bin/transpiler_example
```

---

## 🔍 What Gets Generated

### Input: `test.s`
```asm
.fn test_add
/* 80000000 00000000  7C 84 2A 14 */	add r4, r4, r5
/* 80000004 00000004  4E 80 00 20 */	blr
.endfn test_add
```

### Output: `test.c`
```c
#include "test.h"
#include "gecko_memory.h"

static uint32_t r[32];
static uint32_t lr, ctr, xer;
// ...

void test_add(void) {
    r4 = r4 + r5;        // 0x80000000: add r4, r4, r5
    return;              // 0x80000004: blr
}
```

### Output: `test.h`
```c
#ifndef TEST_H
#define TEST_H

void test_add(void);  // 0x80000000 (size: 0x8)

#endif
```

---

## 🎓 Current Capabilities

### ✅ Can Transpile
- Basic arithmetic (add, sub, mul)
- Logical operations (and, or, xor)
- Shifts and rotates
- Comparisons
- Unconditional branches
- Load/store (byte, halfword, word)
- Load/store multiple
- SPR access (lr, ctr, xer, gqr, etc.)
- Basic floating-point

### ⏳ Coming Soon
- Conditional branches (beq, bne, blt, etc.)
- Division instructions
- More floating-point operations
- Paired-single SIMD (GameCube specific)
- Cache management

---

## 🛠️ Adding New Opcodes

See `OPCODE_CHECKLIST.md` for the complete list of 246 opcodes.

**Currently: 26 / 246 (10.6%) implemented**

To add a new opcode:
1. Copy an existing header from `include/opcode/`
2. Modify for your opcode
3. Add include to `include/opcode.h`
4. Add decode call to `src/porpoise_tool.c`
5. Test and mark checkbox in checklist

---

## 📚 Documentation

| File | Description |
|------|-------------|
| `README.md` | Main user guide |
| `OPCODE_CHECKLIST.md` | All 246 opcodes with checkboxes |
| `PROJECT_STRUCTURE.md` | File organization |
| `PORPOISE_TOOL_SUMMARY.md` | Implementation summary |
| `include/opcode/README.md` | Opcode header guide |
| `include/opcode/TRANSPILER_DESIGN.md` | Architecture |
| `include/USAGE.md` | API documentation |

---

## ❓ FAQ

**Q: Why "Porpoise"?**  
A: It's a joke on "Dolphin" (GameCube codename). Porpoises and dolphins are similar!

**Q: Can I use this for Wii games too?**  
A: Yes! Both GameCube (Gekko) and Wii (Broadway) use the same PowerPC instruction set.

**Q: What about SDK functions?**  
A: Use the skip list to avoid transpiling SDK/system functions.

**Q: Can I modify the generated C code?**  
A: Yes! The output is meant to be further refined and decompiled.

**Q: How accurate is the transpilation?**  
A: Very accurate for implemented opcodes. Each instruction is faithfully converted.

---

## 🐛 Troubleshooting

### Build fails
- Ensure GCC is installed
- Check include paths are correct
- Try `make clean` then `make`

### Transpiler crashes
- Check assembly file format
- Verify instruction encoding
- File an issue with problematic .s file

### Unknown opcodes
- Check `OPCODE_CHECKLIST.md` to see if implemented
- Opcodes not yet implemented will show as `/* UNKNOWN */`
- Add the missing opcode header

---

**Ready to transpile? Run `make` and start converting assembly!**

---

Last Updated: November 3, 2025

