# Porpoise Tool - Implementation Summary

## ✅ Project Setup Complete

### Directory Structure
```
✅ include/                    (All header files)
   ✅ opcode/                 (26 opcode headers)
   ✅ opcode.h                (Master header)
   ✅ porpoise_tool.h         (Transpiler core)
   ✅ gecko_memory.h          (Memory model)

✅ src/                       (All source files)
   ✅ porpoise_tool.c         (Main transpiler)
   ✅ Examples (3 files)

✅ Documentation              (Complete references)
✅ Makefile                   (Build system)
✅ README.md                  (User guide)
```

---

## 🎯 Porpoise Tool Features

### Input Format
Handles PowerPC assembly with:
- ✅ `.include "file.inc"` → `#include "file.h"`
- ✅ `/* */` comments (ignored)
- ✅ `#` comments (ignored)
- ✅ `.fn function_name` (function markers)
- ✅ `.lbl_80xxxxxx` (labels → `lbl_80xxxxxx:`)
- ✅ `.data` sections (preserved as byte arrays)
- ✅ Skip list support (for SDK/system functions)

### Output
Generates for each `.s` file:
- ✅ `.c` file with transpiled C code
- ✅ `.h` file with function declarations
- ✅ Preserves original addresses in comments
- ✅ Maintains function and label names

---

## 📊 Implementation Progress

### Opcodes: 26 / 246 (10.6%)

#### Completed Categories:

**Integer Arithmetic (6/35)**
- ✅ add, addi, lis (addis), subf, mulli, mullw

**Logical Operations (5/14)**
- ✅ and, andi, or, ori, xor

**Shift and Rotate (4/7)**
- ✅ slw, srw, srawi, rlwinm

**Comparison (2/4)**
- ✅ cmp, cmpi

**Branch (2/8+)**
- ✅ b (+ ba, bl, bla variants)
- ✅ blr

**Load/Store (10/38)**
- ✅ lbz, lhz, lwz, lwzu, lmw
- ✅ stb, sth, stw, stwu, stmw

**Special Purpose Registers (3/16)**
- ✅ mfspr, mtspr, mfcr

**Floating-Point (1/30)**
- ✅ fadd

---

## 🔧 How It Works

### 1. Parse Assembly
```asm
/* 804283A0 004251A0  38 63 BE C8 */	addi r3, r3, lbl_8058BEC8@l
```

### 2. Decode Instruction
```c
ADDI_Instruction decoded;
decode_addi(0x3863BEC8, &decoded);
// decoded.rD = 3, decoded.rA = 3, decoded.SIMM = 0xBEC8
```

### 3. Transpile to C
```c
char output[256];
transpile_addi(&decoded, output, sizeof(output));
// output = "r3 = r3 + 0xBEC8;"
```

### 4. Generate Output
```c
r3 = r3 + 0xBEC8;  // 0x804283A0: addi r3, r3, 0xBEC8
```

---

## 📦 Deliverables

### Core Components
- ✅ **Porpoise Tool** (`src/porpoise_tool.c`) - Main transpiler
- ✅ **26 Opcode Headers** (`include/opcode/*.h`) - Decode & transpile logic
- ✅ **Master Header** (`include/opcode.h`) - Single include for all opcodes
- ✅ **Memory Model** (`include/gecko_memory.h`) - GameCube/Wii memory structure
- ✅ **Makefile** - Build system

### Documentation
- ✅ **README.md** - User guide
- ✅ **OPCODE_CHECKLIST.md** - All 246 opcodes with progress tracking
- ✅ **PROJECT_STRUCTURE.md** - Organization and development guide
- ✅ **Gecko_Broadway_CPU_Instruction_Set.md** - Complete ISA reference (246 opcodes)
- ✅ **Gecko_Broadway_CPU_Architecture.md** - CPU architecture (~174 registers)
- ✅ **TRANSPILER_DESIGN.md** - Design philosophy
- ✅ **USAGE.md** - API documentation

### Examples
- ✅ **transpiler_example.c** - How to use opcode headers
- ✅ **gecko_memory_example.c** - Memory model examples
- ✅ **skip_functions_example.txt** - Skip list template

---

## 🚀 Usage

### Build
```bash
make
```

### Run
```bash
bin/porpoise_tool ./asm_directory
bin/porpoise_tool ./asm_directory skip_functions.txt
```

### Input Example
```asm
.fn fn_80428398, global
/* 80428398 00425198  7C 70 43 A6 */	mtsprg 0, r3
/* 8042839C 0042519C  3C 60 80 59 */	lis r3, 0x8059
/* 804283A0 004251A0  38 63 BE C8 */	addi r3, r3, 0xBEC8
.endfn fn_80428398
```

### Output Example
```c
void fn_80428398(void) {
    sprg0 = r3;                              // 0x80428398: mtsprg 0, r3
    r3 = 0x8059 << 16;                       // 0x8042839C: lis r3, 0x8059
    r3 = r3 + 0xBEC8;                        // 0x804283A0: addi r3, r3, 0xBEC8
}
```

---

## 🎨 Design Highlights

### Simplified Memory Model
- **Bool types** for all flags (not bitfields)
- **Direct pointers** (not split high/low registers)
- **No hardware padding** (cleaner structs)

### Modular Opcodes
Each opcode header provides:
- `decode_*()` - Parse instruction
- `transpile_*()` - Generate C code
- `comment_*()` - Generate assembly comment

### Smart Parsing
- Handles PowerPC assembly dump format
- Preserves function structure
- Converts labels to C labels
- Respects skip lists

---

## 📈 Next Steps

### High Priority (Core Functionality)
1. Add remaining basic load/store variants
2. Implement conditional branches (bc, beq, bne, etc.)
3. Add more SPR access instructions
4. Implement basic floating-point operations

### Medium Priority (Common Instructions)
1. Complete arithmetic (divw, divwu, etc.)
2. Add logical immediates (xori, oris, etc.)
3. Implement rotate/mask variants
4. Add more floating-point load/store

### Low Priority (Advanced Features)
1. Paired-single instructions (Gekko specific)
2. Cache management
3. System/privileged instructions
4. Optimization passes

---

## 🔍 Testing Strategy

### Unit Tests
Test each opcode header:
```c
void test_addi() {
    ADDI_Instruction inst;
    assert(decode_addi(0x38630010, &inst));
    assert(inst.rD == 3);
    assert(inst.rA == 3);
    assert(inst.SIMM == 0x10);
}
```

### Integration Tests
Test full file transpilation:
```bash
# Create test .s file
echo '.fn test_func' > test.s
echo '/* 80000000 00000000  38 60 00 01 */	li r3, 1' >> test.s
echo '.endfn test_func' >> test.s

# Transpile
bin/porpoise_tool . 

# Verify output
cat test.c
```

### Real-World Tests
Use actual GameCube/Wii game assembly dumps to verify correctness.

---

## 💡 Key Concepts

### Transpiler vs Decompiler
- **Transpiler**: Converts assembly → low-level C (1:1 mapping)
- **Decompiler**: Converts assembly → high-level C (analyzes and reconstructs)

Porpoise Tool is currently a **transpiler**. Future versions may add decompilation features.

### Register Model
Generated C code uses:
```c
uint32_t r[32];      // GPRs (r0-r31)
double f[32];        // FPRs (f0-f31)
uint32_t lr, ctr, xer;
uint32_t cr0-cr7;
uint32_t gqr[8];
```

### Memory Model
```c
uint8_t *mem;        // Pointer to emulated memory
// Access: *(uint32_t*)(mem + address)
```

---

## 🛠️ Development Workflow

1. **Choose opcode** from `OPCODE_CHECKLIST.md`
2. **Create header** in `include/opcode/`
3. **Add to master header** `include/opcode.h`
4. **Add to transpiler** `src/porpoise_tool.c`
5. **Test** with sample instructions
6. **Update checklist** mark `[x]`
7. **Commit** and document

---

## 📝 Naming Conventions

### Files
- **Lowercase**: `add.h`, `lwz.h`, `stw.h`
- **Underscore for multi-word**: `ps_add.h`, `psq_l.h`

### Functions
- **lowercase_with_underscores**: `decode_add()`, `transpile_lwz()`

### Types
- **PascalCase_Suffix**: `ADD_Instruction`, `LWZ_Instruction`

### Constants
- **UPPERCASE_UNDERSCORE**: `OP_ADD_PRIMARY`, `ADD_RT_MASK`

---

## 🎯 Goals

### Short Term
- [ ] Reach 50 opcodes (20%)
- [ ] Handle all common game code patterns
- [ ] Robust error handling

### Medium Term
- [ ] Reach 150 opcodes (60%)
- [ ] Add optimization passes
- [ ] Control flow analysis

### Long Term
- [ ] Complete all 246 opcodes
- [ ] Decompilation features
- [ ] Type inference
- [ ] Variable naming

---

**Status:** ✅ Infrastructure Complete, Ready for Development  
**Progress:** 26 / 246 opcodes (10.6%)  
**Last Updated:** November 3, 2025

