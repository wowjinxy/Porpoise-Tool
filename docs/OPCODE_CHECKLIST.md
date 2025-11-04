# Gecko/Broadway CPU Opcode Implementation Checklist

**Total Opcodes to Implement: ~246**

Progress: 26 / 246 (10.6%)

## Master Header

All implemented opcodes are automatically included in:
```c
#include "opcode.h"  // Master header in include/opcode.h
```

---

## 1. INTEGER ARITHMETIC INSTRUCTIONS (35 total)

### Basic Arithmetic (16)
- [x] `add.h` - Add (with variants: add, add., addo, addo.)
- [ ] `addc.h` - Add Carrying (addc, addc., addco, addco.)
- [ ] `adde.h` - Add Extended (adde, adde., addeo, addeo.)
- [x] `addi.h` - Add Immediate (addi, la, subi)
- [ ] `addic.h` - Add Immediate Carrying (addic, subic)
- [ ] `addic_dot.h` - Add Immediate Carrying and Record (addic., subic.)
- [x] `lis.h` - Add Immediate Shifted (addis, lis, subis)
- [ ] `addme.h` - Add to Minus One Extended (addme, addme., addmeo, addmeo.)
- [ ] `addze.h` - Add to Zero Extended (addze, addze., addzeo, addzeo.)
- [x] `subf.h` - Subtract From (subf, subf., subfo, subfo., sub)
- [ ] `subfc.h` - Subtract From Carrying (subfc, subfc., subfco, subfco., subc)
- [ ] `subfe.h` - Subtract From Extended (subfe, subfe., subfeo, subfeo.)
- [ ] `subfic.h` - Subtract From Immediate Carrying
- [ ] `subfme.h` - Subtract From Minus One Extended (subfme, subfme., subfmeo, subfmeo.)
- [ ] `subfze.h` - Subtract From Zero Extended (subfze, subfze., subfzeo, subfzeo.)
- [ ] `neg.h` - Negate (neg, neg., nego, nego.)

### Multiplication (4)
- [ ] `mulhw.h` - Multiply High Word (mulhw, mulhw.)
- [ ] `mulhwu.h` - Multiply High Word Unsigned (mulhwu, mulhwu.)
- [x] `mullw.h` - Multiply Low Word (mullw, mullw., mullwo, mullwo.)
- [x] `mulli.h` - Multiply Low Immediate

### Division (2)
- [ ] `divw.h` - Divide Word (divw, divw., divwo, divwo.)
- [ ] `divwu.h` - Divide Word Unsigned (divwu, divwu., divwuo, divwuo.)

---

## 2. LOGICAL INSTRUCTIONS (14 total)

- [x] `and.h` - Logical AND (and, and.)
- [ ] `andc.h` - AND with Complement (andc, andc.)
- [x] `andi.h` - AND Immediate (andi.)
- [ ] `andis.h` - AND Immediate Shifted (andis.)
- [x] `or.h` - Logical OR (or, or., mr)
- [ ] `orc.h` - OR with Complement (orc, orc.)
- [x] `ori.h` - OR Immediate (ori, nop)
- [ ] `oris.h` - OR Immediate Shifted
- [x] `xor.h` - Logical XOR (xor, xor.)
- [ ] `xori.h` - XOR Immediate
- [ ] `xoris.h` - XOR Immediate Shifted
- [ ] `nand.h` - Logical NAND (nand, nand.)
- [ ] `nor.h` - Logical NOR (nor, nor., not)
- [ ] `eqv.h` - Equivalent (eqv, eqv.)

---

## 3. SHIFT AND ROTATE INSTRUCTIONS (7 total)

### Shift (4)
- [x] `slw.h` - Shift Left Word (slw, slw.)
- [x] `srw.h` - Shift Right Word (srw, srw.)
- [ ] `sraw.h` - Shift Right Algebraic Word (sraw, sraw.)
- [x] `srawi.h` - Shift Right Algebraic Word Immediate (srawi, srawi.)

### Rotate (3)
- [ ] `rlwimi.h` - Rotate Left Word Immediate then Mask Insert (rlwimi, rlwimi.)
- [x] `rlwinm.h` - Rotate Left Word Immediate then AND with Mask (rlwinm, rlwinm.)
- [ ] `rlwnm.h` - Rotate Left Word then AND with Mask (rlwnm, rlwnm.)

---

## 4. COMPARISON INSTRUCTIONS (4 total)

- [x] `cmp.h` - Compare
- [x] `cmpi.h` - Compare Immediate
- [ ] `cmpl.h` - Compare Logical
- [ ] `cmpli.h` - Compare Logical Immediate

---

## 5. BRANCH INSTRUCTIONS (8 base + extended mnemonics)

### Base Branch Instructions (5)
- [x] `b.h` - Branch (b, ba, bl, bla)
- [ ] `bc.h` - Branch Conditional (bc, bca, bcl, bcla)
- [ ] `bcctr.h` - Branch Conditional to Count Register (bcctr, bcctrl)
- [x] `blr.h` - Branch Conditional to Link Register (bclr, bclrl, blr, blrl)
- [ ] `bctr.h` - Branch to Count Register (bctr, bctrl)

### Extended Branch Mnemonics (8)
- [ ] `beq.h` - Branch if Equal
- [ ] `bne.h` - Branch if Not Equal
- [ ] `blt.h` - Branch if Less Than
- [ ] `ble.h` - Branch if Less Than or Equal
- [ ] `bgt.h` - Branch if Greater Than
- [ ] `bge.h` - Branch if Greater Than or Equal
- [ ] `bdnz.h` - Branch if Decrement CTR Not Zero
- [ ] `bdz.h` - Branch if Decrement CTR Zero

---

## 6. LOAD AND STORE INSTRUCTIONS (38 total)

### Byte Operations (8)
- [x] `lbz.h` - Load Byte and Zero
- [ ] `lbzu.h` - Load Byte and Zero with Update
- [ ] `lbzx.h` - Load Byte and Zero Indexed
- [ ] `lbzux.h` - Load Byte and Zero with Update Indexed
- [x] `stb.h` - Store Byte
- [ ] `stbu.h` - Store Byte with Update
- [ ] `stbx.h` - Store Byte Indexed
- [ ] `stbux.h` - Store Byte with Update Indexed

### Halfword Operations (14)
- [x] `lhz.h` - Load Halfword and Zero
- [ ] `lhzu.h` - Load Halfword and Zero with Update
- [ ] `lhzx.h` - Load Halfword and Zero Indexed
- [ ] `lhzux.h` - Load Halfword and Zero with Update Indexed
- [ ] `lha.h` - Load Halfword Algebraic
- [ ] `lhau.h` - Load Halfword Algebraic with Update
- [ ] `lhax.h` - Load Halfword Algebraic Indexed
- [ ] `lhaux.h` - Load Halfword Algebraic with Update Indexed
- [x] `sth.h` - Store Halfword
- [ ] `sthu.h` - Store Halfword with Update
- [ ] `sthx.h` - Store Halfword Indexed
- [ ] `sthux.h` - Store Halfword with Update Indexed
- [ ] `lhbrx.h` - Load Halfword Byte-Reverse Indexed
- [ ] `sthbrx.h` - Store Halfword Byte-Reverse Indexed

### Word Operations (10)
- [x] `lwz.h` - Load Word and Zero
- [x] `lwzu.h` - Load Word and Zero with Update
- [ ] `lwzx.h` - Load Word and Zero Indexed
- [ ] `lwzux.h` - Load Word and Zero with Update Indexed
- [x] `stw.h` - Store Word
- [x] `stwu.h` - Store Word with Update
- [ ] `stwx.h` - Store Word Indexed
- [ ] `stwux.h` - Store Word with Update Indexed
- [ ] `lwbrx.h` - Load Word Byte-Reverse Indexed
- [ ] `stwbrx.h` - Store Word Byte-Reverse Indexed

### Multiple/String Operations (6)
- [x] `lmw.h` - Load Multiple Word
- [x] `stmw.h` - Store Multiple Word
- [ ] `lswi.h` - Load String Word Immediate
- [ ] `lswx.h` - Load String Word Indexed
- [ ] `stswi.h` - Store String Word Immediate
- [ ] `stswx.h` - Store String Word Indexed

---

## 7. FLOATING-POINT INSTRUCTIONS (30 total)

### Basic Arithmetic (8)
- [x] `fadd.h` - Floating-Point Add (Double) (fadd, fadd.)
- [ ] `fadds.h` - Floating-Point Add Single (fadds, fadds.)
- [ ] `fsub.h` - Floating-Point Subtract (Double) (fsub, fsub.)
- [ ] `fsubs.h` - Floating-Point Subtract Single (fsubs, fsubs.)
- [ ] `fmul.h` - Floating-Point Multiply (Double) (fmul, fmul.)
- [ ] `fmuls.h` - Floating-Point Multiply Single (fmuls, fmuls.)
- [ ] `fdiv.h` - Floating-Point Divide (Double) (fdiv, fdiv.)
- [ ] `fdivs.h` - Floating-Point Divide Single (fdivs, fdivs.)

### Multiply-Add (8)
- [ ] `fmadd.h` - Floating-Point Multiply-Add (Double) (fmadd, fmadd.)
- [ ] `fmadds.h` - Floating-Point Multiply-Add Single (fmadds, fmadds.)
- [ ] `fmsub.h` - Floating-Point Multiply-Subtract (Double) (fmsub, fmsub.)
- [ ] `fmsubs.h` - Floating-Point Multiply-Subtract Single (fmsubs, fmsubs.)
- [ ] `fnmadd.h` - Floating-Point Negative Multiply-Add (Double) (fnmadd, fnmadd.)
- [ ] `fnmadds.h` - Floating-Point Negative Multiply-Add Single (fnmadds, fnmadds.)
- [ ] `fnmsub.h` - Floating-Point Negative Multiply-Subtract (Double) (fnmsub, fnmsub.)
- [ ] `fnmsubs.h` - Floating-Point Negative Multiply-Subtract Single (fnmsubs, fnmsubs.)

### Other Operations (8)
- [ ] `fabs.h` - Floating-Point Absolute Value (fabs, fabs.)
- [ ] `fnabs.h` - Floating-Point Negative Absolute Value (fnabs, fnabs.)
- [ ] `fneg.h` - Floating-Point Negate (fneg, fneg.)
- [ ] `fres.h` - Floating-Point Reciprocal Estimate Single (fres, fres.)
- [ ] `frsqrte.h` - Floating-Point Reciprocal Square Root Estimate (frsqrte, frsqrte.)
- [ ] `fsel.h` - Floating-Point Select (fsel, fsel.)
- [ ] `fsqrt.h` - Floating-Point Square Root (Double) (fsqrt, fsqrt.)
- [ ] `fsqrts.h` - Floating-Point Square Root Single (fsqrts, fsqrts.)

### Rounding/Conversion (3)
- [ ] `frsp.h` - Floating-Point Round to Single-Precision (frsp, frsp.)
- [ ] `fctiw.h` - Floating-Point Convert to Integer Word (fctiw, fctiw.)
- [ ] `fctiwz.h` - Floating-Point Convert to Integer Word with Round toward Zero (fctiwz, fctiwz.)

### Comparison (2)
- [ ] `fcmpo.h` - Floating-Point Compare Ordered
- [ ] `fcmpu.h` - Floating-Point Compare Unordered

### Move/Copy (1)
- [ ] `fmr.h` - Floating-Point Move Register (fmr, fmr.)

---

## 8. FLOATING-POINT LOAD/STORE INSTRUCTIONS (17 total)

### Double-Precision (8)
- [ ] `lfd.h` - Load Floating-Point Double
- [ ] `lfdu.h` - Load Floating-Point Double with Update
- [ ] `lfdx.h` - Load Floating-Point Double Indexed
- [ ] `lfdux.h` - Load Floating-Point Double with Update Indexed
- [ ] `stfd.h` - Store Floating-Point Double
- [ ] `stfdu.h` - Store Floating-Point Double with Update
- [ ] `stfdx.h` - Store Floating-Point Double Indexed
- [ ] `stfdux.h` - Store Floating-Point Double with Update Indexed

### Single-Precision (8)
- [ ] `lfs.h` - Load Floating-Point Single
- [ ] `lfsu.h` - Load Floating-Point Single with Update
- [ ] `lfsx.h` - Load Floating-Point Single Indexed
- [ ] `lfsux.h` - Load Floating-Point Single with Update Indexed
- [ ] `stfs.h` - Store Floating-Point Single
- [ ] `stfsu.h` - Store Floating-Point Single with Update
- [ ] `stfsx.h` - Store Floating-Point Single Indexed
- [ ] `stfsux.h` - Store Floating-Point Single with Update Indexed

### Special (1)
- [ ] `stfiwx.h` - Store Floating-Point as Integer Word Indexed

---

## 9. CACHE MANAGEMENT INSTRUCTIONS (7 total)

- [ ] `dcbf.h` - Data Cache Block Flush
- [ ] `dcbi.h` - Data Cache Block Invalidate
- [ ] `dcbst.h` - Data Cache Block Store
- [ ] `dcbt.h` - Data Cache Block Touch
- [ ] `dcbtst.h` - Data Cache Block Touch for Store
- [ ] `dcbz.h` - Data Cache Block Clear to Zero
- [ ] `icbi.h` - Instruction Cache Block Invalidate

---

## 10. SPECIAL PURPOSE REGISTER INSTRUCTIONS (16 total)

### Move To/From SPR (7)
- [x] `mtspr.h` - Move To Special Purpose Register
- [x] `mfspr.h` - Move From Special Purpose Register
- [ ] `mtcrf.h` - Move To Condition Register Fields
- [x] `mfcr.h` - Move From Condition Register
- [ ] `mtmsr.h` - Move To Machine State Register
- [ ] `mfmsr.h` - Move From Machine State Register
- [ ] `mftb.h` - Move From Time Base

### Specific SPR Access (6)
- [ ] `mtlr.h` - Move To Link Register
- [ ] `mflr.h` - Move From Link Register
- [ ] `mtctr.h` - Move To Count Register
- [ ] `mfctr.h` - Move From Count Register
- [ ] `mtxer.h` - Move To XER
- [ ] `mfxer.h` - Move From XER

---

## 11. FLOATING-POINT STATUS AND CONTROL (6 total)

- [ ] `mcrfs.h` - Move to Condition Register from FPSCR
- [ ] `mffs.h` - Move From FPSCR (mffs, mffs.)
- [ ] `mtfsb0.h` - Move To FPSCR Bit 0 (mtfsb0, mtfsb0.)
- [ ] `mtfsb1.h` - Move To FPSCR Bit 1 (mtfsb1, mtfsb1.)
- [ ] `mtfsf.h` - Move To FPSCR Fields (mtfsf, mtfsf.)
- [ ] `mtfsfi.h` - Move To FPSCR Field Immediate (mtfsfi, mtfsfi.)

---

## 12. CONDITION REGISTER INSTRUCTIONS (9 total)

- [ ] `crand.h` - Condition Register AND
- [ ] `crandc.h` - Condition Register AND with Complement
- [ ] `creqv.h` - Condition Register Equivalent
- [ ] `crnand.h` - Condition Register NAND
- [ ] `crnor.h` - Condition Register NOR
- [ ] `cror.h` - Condition Register OR
- [ ] `crorc.h` - Condition Register OR with Complement
- [ ] `crxor.h` - Condition Register XOR
- [ ] `mcrf.h` - Move Condition Register Field

---

## 13. SYSTEM INSTRUCTIONS (12 total)

### Synchronization (3)
- [ ] `sync.h` - Synchronize
- [ ] `isync.h` - Instruction Synchronize
- [ ] `eieio.h` - Enforce In-order Execution of I/O

### TLB Management (3)
- [ ] `tlbie.h` - TLB Invalidate Entry
- [ ] `tlbia.h` - TLB Invalidate All (Optional)
- [ ] `tlbsync.h` - TLB Synchronize

### Segment Register (4)
- [ ] `mtsr.h` - Move To Segment Register
- [ ] `mfsr.h` - Move From Segment Register
- [ ] `mtsrin.h` - Move To Segment Register Indirect
- [ ] `mfsrin.h` - Move From Segment Register Indirect

### Other System (4)
- [ ] `sc.h` - System Call
- [ ] `rfi.h` - Return From Interrupt
- [ ] `tw.h` - Trap Word
- [ ] `twi.h` - Trap Word Immediate

---

## 14. GEKKO PAIRED-SINGLE (PS) INSTRUCTIONS (~50 total)

### Paired-Single Load/Store (Quantized) (8)
- [ ] `psq_l.h` - Paired Single Quantized Load
- [ ] `psq_lu.h` - Paired Single Quantized Load with Update
- [ ] `psq_lx.h` - Paired Single Quantized Load Indexed
- [ ] `psq_lux.h` - Paired Single Quantized Load with Update Indexed
- [ ] `psq_st.h` - Paired Single Quantized Store
- [ ] `psq_stu.h` - Paired Single Quantized Store with Update
- [ ] `psq_stx.h` - Paired Single Quantized Store Indexed
- [ ] `psq_stux.h` - Paired Single Quantized Store with Update Indexed

### Paired-Single Arithmetic (7)
- [ ] `ps_add.h` - Paired Single Add (ps_add, ps_add.)
- [ ] `ps_sub.h` - Paired Single Subtract (ps_sub, ps_sub.)
- [ ] `ps_mul.h` - Paired Single Multiply (ps_mul, ps_mul.)
- [ ] `ps_div.h` - Paired Single Divide (ps_div, ps_div.)
- [ ] `ps_abs.h` - Paired Single Absolute Value (ps_abs, ps_abs.)
- [ ] `ps_neg.h` - Paired Single Negate (ps_neg, ps_neg.)
- [ ] `ps_nabs.h` - Paired Single Negative Absolute Value (ps_nabs, ps_nabs.)

### Paired-Single Multiply-Add (6)
- [ ] `ps_madd.h` - Paired Single Multiply-Add (ps_madd, ps_madd.)
- [ ] `ps_madds0.h` - Paired Single Multiply-Add Scalar 0 (ps_madds0, ps_madds0.)
- [ ] `ps_madds1.h` - Paired Single Multiply-Add Scalar 1 (ps_madds1, ps_madds1.)
- [ ] `ps_msub.h` - Paired Single Multiply-Subtract (ps_msub, ps_msub.)
- [ ] `ps_nmadd.h` - Paired Single Negative Multiply-Add (ps_nmadd, ps_nmadd.)
- [ ] `ps_nmsub.h` - Paired Single Negative Multiply-Subtract (ps_nmsub, ps_nmsub.)

### Paired-Single Utility (8)
- [ ] `ps_mr.h` - Paired Single Move Register (ps_mr, ps_mr.)
- [ ] `ps_res.h` - Paired Single Reciprocal Estimate (ps_res, ps_res.)
- [ ] `ps_rsqrte.h` - Paired Single Reciprocal Square Root Estimate (ps_rsqrte, ps_rsqrte.)
- [ ] `ps_sel.h` - Paired Single Select (ps_sel, ps_sel.)
- [ ] `ps_sum0.h` - Paired Single Sum High (ps_sum0, ps_sum0.)
- [ ] `ps_sum1.h` - Paired Single Sum Low (ps_sum1, ps_sum1.)
- [ ] `ps_muls0.h` - Paired Single Multiply Scalar 0 (ps_muls0, ps_muls0.)
- [ ] `ps_muls1.h` - Paired Single Multiply Scalar 1 (ps_muls1, ps_muls1.)

### Paired-Single Merge/Permute (4)
- [ ] `ps_merge00.h` - Paired Single Merge High (ps_merge00, ps_merge00.)
- [ ] `ps_merge01.h` - Paired Single Merge Direct (ps_merge01, ps_merge01.)
- [ ] `ps_merge10.h` - Paired Single Merge Swapped (ps_merge10, ps_merge10.)
- [ ] `ps_merge11.h` - Paired Single Merge Low (ps_merge11, ps_merge11.)

### Paired-Single Comparison (4)
- [ ] `ps_cmpu0.h` - Paired Single Compare Unordered 0
- [ ] `ps_cmpo0.h` - Paired Single Compare Ordered 0
- [ ] `ps_cmpu1.h` - Paired Single Compare Unordered 1
- [ ] `ps_cmpo1.h` - Paired Single Compare Ordered 1

---

## PRIORITY GUIDE

### High Priority (Core Instructions - ~30)
Essential for basic emulation:
- Integer arithmetic: add, addi, sub, mul, div
- Logical: and, or, xor, nor
- Shifts: slw, srw, sraw
- Comparisons: cmp, cmpi, cmpl, cmpli
- Branches: b, bc, bclr, bctr
- Load/Store: lwz, stw, lbz, stb, lhz, sth
- Move registers: mr, li

### Medium Priority (Common Instructions - ~80)
Frequently used operations:
- All integer load/store variants
- Floating-point basic ops: fadd, fsub, fmul, fdiv
- Floating-point load/store: lfs, lfd, stfs, stfd
- SPR access: mfspr, mtspr, mflr, mtlr, mfcr, mtcr
- Cache management: dcbz, dcbf, icbi
- More arithmetic: addic, addis, subf, neg

### Low Priority (Advanced/Rare - ~136)
Less frequently used:
- String/multiple operations: lmw, stmw, lswi, stswi
- Extended multiply-add: fmadd, fmsub, fnmadd, fnmsub
- TLB management: tlbie, tlbia, tlbsync
- Condition register logic: crand, cror, etc.
- Paired-single instructions (Gekko-specific)

---

## NOTES

- Instructions with `.` suffix update CR0 or CR1
- Instructions with `o` suffix enable overflow detection
- Many instructions are aliases (e.g., `mr` = `or rA,rS,rS`)
- Extended mnemonics (beq, bne, etc.) are simplified forms of `bc`
- Paired-single instructions are GameCube/Wii specific
- Some instructions have indexed (`x`) and update (`u`) variants

---

**Last Updated:** November 3, 2025
**Directory:** `include/opcode/`
**Template Files Created:** 3 (add.h, lwz.h, fadd.h)

