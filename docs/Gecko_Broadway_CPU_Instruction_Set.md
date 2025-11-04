# Gecko/Broadway CPU Complete Instruction Set

## Overview
The Gecko CPU (GameCube) and Broadway CPU (Wii) are based on the IBM PowerPC 750CXe architecture with Nintendo-specific extensions.

**Total Instruction Count:**
- PowerPC 750CXe Base: ~200 instructions
- Gekko Paired-Single Extensions: ~50 instructions
- Broadway Extensions: Minor additions (not well documented)
- **Estimated Total: ~250 opcodes**

---

## 1. INTEGER ARITHMETIC INSTRUCTIONS

### Basic Arithmetic
| Mnemonic | Variants | Description |
|----------|----------|-------------|
| `add` | `add`, `add.`, `addo`, `addo.` | Add |
| `addc` | `addc`, `addc.`, `addco`, `addco.` | Add Carrying |
| `adde` | `adde`, `adde.`, `addeo`, `addeo.` | Add Extended |
| `addi` | `addi`, `la`, `subi` | Add Immediate |
| `addic` | `addic`, `subic` | Add Immediate Carrying |
| `addic.` | `addic.`, `subic.` | Add Immediate Carrying and Record |
| `addis` | `addis`, `lis`, `subis` | Add Immediate Shifted |
| `addme` | `addme`, `addme.`, `addmeo`, `addmeo.` | Add to Minus One Extended |
| `addze` | `addze`, `addze.`, `addzeo`, `addzeo.` | Add to Zero Extended |
| `subf` | `subf`, `subf.`, `subfo`, `subfo.`, `sub` | Subtract From |
| `subfc` | `subfc`, `subfc.`, `subfco`, `subfco.`, `subc` | Subtract From Carrying |
| `subfe` | `subfe`, `subfe.`, `subfeo`, `subfeo.` | Subtract From Extended |
| `subfic` | `subfic` | Subtract From Immediate Carrying |
| `subfme` | `subfme`, `subfme.`, `subfmeo`, `subfmeo.` | Subtract From Minus One Extended |
| `subfze` | `subfze`, `subfze.`, `subfzeo`, `subfzeo.` | Subtract From Zero Extended |
| `neg` | `neg`, `neg.`, `nego`, `nego.` | Negate |

### Multiplication
| Mnemonic | Variants | Description |
|----------|----------|-------------|
| `mulhw` | `mulhw`, `mulhw.` | Multiply High Word |
| `mulhwu` | `mulhwu`, `mulhwu.` | Multiply High Word Unsigned |
| `mullw` | `mullw`, `mullw.`, `mullwo`, `mullwo.`, `mulli` | Multiply Low Word |
| `mulli` | `mulli` | Multiply Low Immediate |

### Division
| Mnemonic | Variants | Description |
|----------|----------|-------------|
| `divw` | `divw`, `divw.`, `divwo`, `divwo.` | Divide Word |
| `divwu` | `divwu`, `divwu.`, `divwuo`, `divwuo.` | Divide Word Unsigned |

---

## 2. LOGICAL INSTRUCTIONS

| Mnemonic | Variants | Description |
|----------|----------|-------------|
| `and` | `and`, `and.` | Logical AND |
| `andc` | `andc`, `andc.` | AND with Complement |
| `andi.` | `andi.` | AND Immediate |
| `andis.` | `andis.` | AND Immediate Shifted |
| `or` | `or`, `or.`, `mr` | Logical OR (mr = move register) |
| `orc` | `orc`, `orc.` | OR with Complement |
| `ori` | `ori`, `nop` | OR Immediate (nop when RA=RS=0) |
| `oris` | `oris` | OR Immediate Shifted |
| `xor` | `xor`, `xor.` | Logical XOR |
| `xori` | `xori` | XOR Immediate |
| `xoris` | `xoris` | XOR Immediate Shifted |
| `nand` | `nand`, `nand.` | Logical NAND |
| `nor` | `nor`, `nor.`, `not` | Logical NOR (not when RS=RB) |
| `eqv` | `eqv`, `eqv.` | Equivalent |

---

## 3. SHIFT AND ROTATE INSTRUCTIONS

### Shift Instructions
| Mnemonic | Variants | Description |
|----------|----------|-------------|
| `slw` | `slw`, `slw.` | Shift Left Word |
| `srw` | `srw`, `srw.` | Shift Right Word |
| `sraw` | `sraw`, `sraw.` | Shift Right Algebraic Word |
| `srawi` | `srawi`, `srawi.` | Shift Right Algebraic Word Immediate |

### Rotate Instructions
| Mnemonic | Variants | Description |
|----------|----------|-------------|
| `rlwimi` | `rlwimi`, `rlwimi.` | Rotate Left Word Immediate then Mask Insert |
| `rlwinm` | `rlwinm`, `rlwinm.` | Rotate Left Word Immediate then AND with Mask |
| `rlwnm` | `rlwnm`, `rlwnm.` | Rotate Left Word then AND with Mask |

---

## 4. COMPARISON INSTRUCTIONS

### Integer Compare
| Mnemonic | Description |
|----------|-------------|
| `cmp` | Compare |
| `cmpi` | Compare Immediate |
| `cmpl` | Compare Logical |
| `cmpli` | Compare Logical Immediate |

---

## 5. BRANCH INSTRUCTIONS

| Mnemonic | Variants | Description |
|----------|----------|-------------|
| `b` | `b`, `ba`, `bl`, `bla` | Branch (Absolute/Link variants) |
| `bc` | `bc`, `bca`, `bcl`, `bcla` | Branch Conditional |
| `bcctr` | `bcctr`, `bcctrl` | Branch Conditional to Count Register |
| `bclr` | `bclr`, `bclrl`, `blr`, `blrl` | Branch Conditional to Link Register |
| `bctr` | `bctr`, `bctrl` | Branch to Count Register |

### Extended Branch Mnemonics
| Mnemonic | Description |
|----------|-------------|
| `beq` | Branch if Equal |
| `bne` | Branch if Not Equal |
| `blt` | Branch if Less Than |
| `ble` | Branch if Less Than or Equal |
| `bgt` | Branch if Greater Than |
| `bge` | Branch if Greater Than or Equal |
| `bdnz` | Branch if Decrement CTR Not Zero |
| `bdz` | Branch if Decrement CTR Zero |

---

## 6. LOAD AND STORE INSTRUCTIONS

### Byte Operations
| Mnemonic | Description |
|----------|-------------|
| `lbz` | Load Byte and Zero |
| `lbzu` | Load Byte and Zero with Update |
| `lbzx` | Load Byte and Zero Indexed |
| `lbzux` | Load Byte and Zero with Update Indexed |
| `stb` | Store Byte |
| `stbu` | Store Byte with Update |
| `stbx` | Store Byte Indexed |
| `stbux` | Store Byte with Update Indexed |

### Halfword Operations
| Mnemonic | Description |
|----------|-------------|
| `lhz` | Load Halfword and Zero |
| `lhzu` | Load Halfword and Zero with Update |
| `lhzx` | Load Halfword and Zero Indexed |
| `lhzux` | Load Halfword and Zero with Update Indexed |
| `lha` | Load Halfword Algebraic |
| `lhau` | Load Halfword Algebraic with Update |
| `lhax` | Load Halfword Algebraic Indexed |
| `lhaux` | Load Halfword Algebraic with Update Indexed |
| `sth` | Store Halfword |
| `sthu` | Store Halfword with Update |
| `sthx` | Store Halfword Indexed |
| `sthux` | Store Halfword with Update Indexed |
| `lhbrx` | Load Halfword Byte-Reverse Indexed |
| `sthbrx` | Store Halfword Byte-Reverse Indexed |

### Word Operations
| Mnemonic | Description |
|----------|-------------|
| `lwz` | Load Word and Zero |
| `lwzu` | Load Word and Zero with Update |
| `lwzx` | Load Word and Zero Indexed |
| `lwzux` | Load Word and Zero with Update Indexed |
| `stw` | Store Word |
| `stwu` | Store Word with Update |
| `stwx` | Store Word Indexed |
| `stwux` | Store Word with Update Indexed |
| `lwbrx` | Load Word Byte-Reverse Indexed |
| `stwbrx` | Store Word Byte-Reverse Indexed |

### Multiple/String Operations
| Mnemonic | Description |
|----------|-------------|
| `lmw` | Load Multiple Word |
| `stmw` | Store Multiple Word |
| `lswi` | Load String Word Immediate |
| `lswx` | Load String Word Indexed |
| `stswi` | Store String Word Immediate |
| `stswx` | Store String Word Indexed |

---

## 7. FLOATING-POINT INSTRUCTIONS

### Basic Arithmetic
| Mnemonic | Variants | Description |
|----------|----------|-------------|
| `fadd` | `fadd`, `fadd.` | Floating-Point Add (Double) |
| `fadds` | `fadds`, `fadds.` | Floating-Point Add Single |
| `fsub` | `fsub`, `fsub.` | Floating-Point Subtract (Double) |
| `fsubs` | `fsubs`, `fsubs.` | Floating-Point Subtract Single |
| `fmul` | `fmul`, `fmul.` | Floating-Point Multiply (Double) |
| `fmuls` | `fmuls`, `fmuls.` | Floating-Point Multiply Single |
| `fdiv` | `fdiv`, `fdiv.` | Floating-Point Divide (Double) |
| `fdivs` | `fdivs`, `fdivs.` | Floating-Point Divide Single |

### Multiply-Add
| Mnemonic | Variants | Description |
|----------|----------|-------------|
| `fmadd` | `fmadd`, `fmadd.` | Floating-Point Multiply-Add (Double) |
| `fmadds` | `fmadds`, `fmadds.` | Floating-Point Multiply-Add Single |
| `fmsub` | `fmsub`, `fmsub.` | Floating-Point Multiply-Subtract (Double) |
| `fmsubs` | `fmsubs`, `fmsubs.` | Floating-Point Multiply-Subtract Single |
| `fnmadd` | `fnmadd`, `fnmadd.` | Floating-Point Negative Multiply-Add (Double) |
| `fnmadds` | `fnmadds`, `fnmadds.` | Floating-Point Negative Multiply-Add Single |
| `fnmsub` | `fnmsub`, `fnmsub.` | Floating-Point Negative Multiply-Subtract (Double) |
| `fnmsubs` | `fnmsubs`, `fnmsubs.` | Floating-Point Negative Multiply-Subtract Single |

### Other Operations
| Mnemonic | Variants | Description |
|----------|----------|-------------|
| `fabs` | `fabs`, `fabs.` | Floating-Point Absolute Value |
| `fnabs` | `fnabs`, `fnabs.` | Floating-Point Negative Absolute Value |
| `fneg` | `fneg`, `fneg.` | Floating-Point Negate |
| `fres` | `fres`, `fres.` | Floating-Point Reciprocal Estimate Single |
| `frsqrte` | `frsqrte`, `frsqrte.` | Floating-Point Reciprocal Square Root Estimate |
| `fsel` | `fsel`, `fsel.` | Floating-Point Select |
| `fsqrt` | `fsqrt`, `fsqrt.` | Floating-Point Square Root (Double) |
| `fsqrts` | `fsqrts`, `fsqrts.` | Floating-Point Square Root Single |

### Rounding/Conversion
| Mnemonic | Variants | Description |
|----------|----------|-------------|
| `frsp` | `frsp`, `frsp.` | Floating-Point Round to Single-Precision |
| `fctiw` | `fctiw`, `fctiw.` | Floating-Point Convert to Integer Word |
| `fctiwz` | `fctiwz`, `fctiwz.` | Floating-Point Convert to Integer Word with Round toward Zero |

### Comparison
| Mnemonic | Description |
|----------|-------------|
| `fcmpo` | Floating-Point Compare Ordered |
| `fcmpu` | Floating-Point Compare Unordered |

### Move/Copy
| Mnemonic | Variants | Description |
|----------|----------|-------------|
| `fmr` | `fmr`, `fmr.` | Floating-Point Move Register |

---

## 8. FLOATING-POINT LOAD/STORE INSTRUCTIONS

| Mnemonic | Description |
|----------|-------------|
| `lfd` | Load Floating-Point Double |
| `lfdu` | Load Floating-Point Double with Update |
| `lfdx` | Load Floating-Point Double Indexed |
| `lfdux` | Load Floating-Point Double with Update Indexed |
| `lfs` | Load Floating-Point Single |
| `lfsu` | Load Floating-Point Single with Update |
| `lfsx` | Load Floating-Point Single Indexed |
| `lfsux` | Load Floating-Point Single with Update Indexed |
| `stfd` | Store Floating-Point Double |
| `stfdu` | Store Floating-Point Double with Update |
| `stfdx` | Store Floating-Point Double Indexed |
| `stfdux` | Store Floating-Point Double with Update Indexed |
| `stfs` | Store Floating-Point Single |
| `stfsu` | Store Floating-Point Single with Update |
| `stfsx` | Store Floating-Point Single Indexed |
| `stfsux` | Store Floating-Point Single with Update Indexed |
| `stfiwx` | Store Floating-Point as Integer Word Indexed |

---

## 9. CACHE MANAGEMENT INSTRUCTIONS

| Mnemonic | Description |
|----------|-------------|
| `dcbf` | Data Cache Block Flush |
| `dcbi` | Data Cache Block Invalidate |
| `dcbst` | Data Cache Block Store |
| `dcbt` | Data Cache Block Touch |
| `dcbtst` | Data Cache Block Touch for Store |
| `dcbz` | Data Cache Block Clear to Zero |
| `icbi` | Instruction Cache Block Invalidate |

---

## 10. SPECIAL PURPOSE REGISTER INSTRUCTIONS

### Move To/From SPR
| Mnemonic | Description |
|----------|-------------|
| `mtspr` | Move To Special Purpose Register |
| `mfspr` | Move From Special Purpose Register |
| `mtcrf` | Move To Condition Register Fields |
| `mfcr` | Move From Condition Register |
| `mtmsr` | Move To Machine State Register |
| `mfmsr` | Move From Machine State Register |
| `mftb` | Move From Time Base |

### Specific SPR Access
| Mnemonic | Description |
|----------|-------------|
| `mtlr` | Move To Link Register |
| `mflr` | Move From Link Register |
| `mtctr` | Move To Count Register |
| `mfctr` | Move From Count Register |
| `mtxer` | Move To XER |
| `mfxer` | Move From XER |

---

## 11. FLOATING-POINT STATUS AND CONTROL

| Mnemonic | Variants | Description |
|----------|----------|-------------|
| `mcrfs` | `mcrfs` | Move to Condition Register from FPSCR |
| `mffs` | `mffs`, `mffs.` | Move From FPSCR |
| `mtfsb0` | `mtfsb0`, `mtfsb0.` | Move To FPSCR Bit 0 |
| `mtfsb1` | `mtfsb1`, `mtfsb1.` | Move To FPSCR Bit 1 |
| `mtfsf` | `mtfsf`, `mtfsf.` | Move To FPSCR Fields |
| `mtfsfi` | `mtfsfi`, `mtfsfi.` | Move To FPSCR Field Immediate |

---

## 12. CONDITION REGISTER INSTRUCTIONS

| Mnemonic | Description |
|----------|-------------|
| `crand` | Condition Register AND |
| `crandc` | Condition Register AND with Complement |
| `creqv` | Condition Register Equivalent |
| `crnand` | Condition Register NAND |
| `crnor` | Condition Register NOR |
| `cror` | Condition Register OR |
| `crorc` | Condition Register OR with Complement |
| `crxor` | Condition Register XOR |
| `mcrf` | Move Condition Register Field |

---

## 13. SYSTEM INSTRUCTIONS

### Synchronization
| Mnemonic | Description |
|----------|-------------|
| `sync` | Synchronize |
| `isync` | Instruction Synchronize |
| `eieio` | Enforce In-order Execution of I/O |

### TLB Management
| Mnemonic | Description |
|----------|-------------|
| `tlbie` | TLB Invalidate Entry |
| `tlbia` | TLB Invalidate All (Optional) |
| `tlbsync` | TLB Synchronize |

### Segment Register
| Mnemonic | Description |
|----------|-------------|
| `mtsr` | Move To Segment Register |
| `mfsr` | Move From Segment Register |
| `mtsrin` | Move To Segment Register Indirect |
| `mfsrin` | Move From Segment Register Indirect |

### Other System
| Mnemonic | Description |
|----------|-------------|
| `sc` | System Call |
| `rfi` | Return From Interrupt |
| `tw` | Trap Word |
| `twi` | Trap Word Immediate |

---

## 14. GEKKO PAIRED-SINGLE (PS) INSTRUCTIONS

### Paired-Single Load/Store (Quantized)
| Mnemonic | Description |
|----------|-------------|
| `psq_l` | Paired Single Quantized Load |
| `psq_lu` | Paired Single Quantized Load with Update |
| `psq_lx` | Paired Single Quantized Load Indexed |
| `psq_lux` | Paired Single Quantized Load with Update Indexed |
| `psq_st` | Paired Single Quantized Store |
| `psq_stu` | Paired Single Quantized Store with Update |
| `psq_stx` | Paired Single Quantized Store Indexed |
| `psq_stux` | Paired Single Quantized Store with Update Indexed |

### Paired-Single Arithmetic
| Mnemonic | Variants | Description |
|----------|----------|-------------|
| `ps_add` | `ps_add`, `ps_add.` | Paired Single Add |
| `ps_sub` | `ps_sub`, `ps_sub.` | Paired Single Subtract |
| `ps_mul` | `ps_mul`, `ps_mul.` | Paired Single Multiply |
| `ps_div` | `ps_div`, `ps_div.` | Paired Single Divide |
| `ps_abs` | `ps_abs`, `ps_abs.` | Paired Single Absolute Value |
| `ps_neg` | `ps_neg`, `ps_neg.` | Paired Single Negate |
| `ps_nabs` | `ps_nabs`, `ps_nabs.` | Paired Single Negative Absolute Value |

### Paired-Single Multiply-Add
| Mnemonic | Variants | Description |
|----------|----------|-------------|
| `ps_madd` | `ps_madd`, `ps_madd.` | Paired Single Multiply-Add |
| `ps_madds0` | `ps_madds0`, `ps_madds0.` | Paired Single Multiply-Add Scalar 0 |
| `ps_madds1` | `ps_madds1`, `ps_madds1.` | Paired Single Multiply-Add Scalar 1 |
| `ps_msub` | `ps_msub`, `ps_msub.` | Paired Single Multiply-Subtract |
| `ps_nmadd` | `ps_nmadd`, `ps_nmadd.` | Paired Single Negative Multiply-Add |
| `ps_nmsub` | `ps_nmsub`, `ps_nmsub.` | Paired Single Negative Multiply-Subtract |

### Paired-Single Utility
| Mnemonic | Variants | Description |
|----------|----------|-------------|
| `ps_mr` | `ps_mr`, `ps_mr.` | Paired Single Move Register |
| `ps_res` | `ps_res`, `ps_res.` | Paired Single Reciprocal Estimate |
| `ps_rsqrte` | `ps_rsqrte`, `ps_rsqrte.` | Paired Single Reciprocal Square Root Estimate |
| `ps_sel` | `ps_sel`, `ps_sel.` | Paired Single Select |
| `ps_sum0` | `ps_sum0`, `ps_sum0.` | Paired Single Sum High |
| `ps_sum1` | `ps_sum1`, `ps_sum1.` | Paired Single Sum Low |
| `ps_muls0` | `ps_muls0`, `ps_muls0.` | Paired Single Multiply Scalar 0 |
| `ps_muls1` | `ps_muls1`, `ps_muls1.` | Paired Single Multiply Scalar 1 |

### Paired-Single Merge/Permute
| Mnemonic | Variants | Description |
|----------|----------|-------------|
| `ps_merge00` | `ps_merge00`, `ps_merge00.` | Paired Single Merge High |
| `ps_merge01` | `ps_merge01`, `ps_merge01.` | Paired Single Merge Direct |
| `ps_merge10` | `ps_merge10`, `ps_merge10.` | Paired Single Merge Swapped |
| `ps_merge11` | `ps_merge11`, `ps_merge11.` | Paired Single Merge Low |

### Paired-Single Comparison
| Mnemonic | Description |
|----------|-------------|
| `ps_cmpu0` | Paired Single Compare Unordered 0 |
| `ps_cmpo0` | Paired Single Compare Ordered 0 |
| `ps_cmpu1` | Paired Single Compare Unordered 1 |
| `ps_cmpo1` | Paired Single Compare Ordered 1 |

---

## 15. BROADWAY (WII) SPECIFIC INSTRUCTIONS

**Note:** Broadway is largely compatible with Gekko. Most differences are internal optimizations rather than new instructions. No significant new opcodes are publicly documented.

Possible additions (not confirmed):
- Enhanced cache management
- Minor performance optimizations
- Backward compatible with all Gekko instructions

---

## 16. ILLEGAL/RESERVED OPCODES

According to the Gekko documentation, the following primary opcodes are **illegal** on the Gekko processor:
- Primary opcode **2** (reserved)
- Primary opcode **30** (reserved)
- Primary opcode **58** (reserved for future extensions)
- Primary opcode **62** (reserved for future extensions)
- All unused extended opcodes

**Behavior:** Executing illegal opcodes triggers the system's illegal instruction exception handler.

**Unofficial Opcodes:** Unlike older CPUs (such as the MOS 6502), the PowerPC architecture and Gekko CPU do not have widely-known "unofficial" opcodes that perform unintended operations. Illegal opcodes simply cause exceptions.

---

## SUMMARY TABLE

| Category | Approximate Count |
|----------|-------------------|
| Integer Arithmetic | 35 |
| Logical Operations | 13 |
| Shift/Rotate | 7 |
| Comparison | 4 |
| Branch Instructions | 8 (+ extended mnemonics) |
| Load/Store (Integer) | 38 |
| Floating-Point Arithmetic | 30 |
| Floating-Point Load/Store | 17 |
| Cache Management | 7 |
| Special Purpose Registers | 16 |
| Condition Register | 9 |
| System/Privileged | 12 |
| **Gekko Paired-Single** | **~50** |
| **Total (Base + Gekko)** | **~246 opcodes** |

---

## REFERENCES

1. PowerPC User Instruction Set Architecture Manual
2. IBM PowerPC 750CXe Technical Documentation
3. Gekko User Manual (IBM/Nintendo)
4. PowerPC Instruction Set Reference: https://fenixfox-studios.com/manual/powerpc
5. Gekko CPU Wikipedia: https://en.wikipedia.org/wiki/Gekko_(processor)

---

## NOTES

- Many instructions have multiple variants (with/without record bit, overflow variants, etc.)
- The `.` suffix indicates "record bit set" which updates CR0
- The `o` suffix indicates overflow checking enabled
- Broadway (Wii) maintains full backward compatibility with Gekko instructions
- Instruction counts are approximate due to variant counting methodology
- This document focuses on user-level instructions; supervisor-level instructions are included but not exhaustive

**Last Updated:** November 3, 2025

