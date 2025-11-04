# Gecko/Broadway CPU Architecture Reference

## Complete Register and Memory Location Documentation

This document provides a comprehensive reference for all registers and memory locations in the Nintendo GameCube (Gekko) and Wii (Broadway) CPUs.

---

## TABLE OF CONTENTS

1. [CPU Registers](#cpu-registers)
   - General Purpose Registers (GPRs)
   - Floating-Point Registers (FPRs)
   - Special Purpose Registers (SPRs)
   - Condition Register
   - Segment Registers
   - BAT Registers
2. [Memory Map](#memory-map)
3. [Hardware I/O Registers](#hardware-io-registers)
4. [Cache Architecture](#cache-architecture)

---

# CPU REGISTERS

## 1. GENERAL PURPOSE REGISTERS (GPRs)

### Integer Registers
| Register | Name | Width | Description |
|----------|------|-------|-------------|
| `r0` | GPR0 | 32-bit | General Purpose Register 0 (Special: used as 0 in some contexts) |
| `r1` | GPR1 / SP | 32-bit | Stack Pointer (by convention) |
| `r2` | GPR2 / RTOC | 32-bit | Table of Contents pointer (by convention) |
| `r3` | GPR3 | 32-bit | Function return value / First argument |
| `r4-r10` | GPR4-10 | 32-bit | Function arguments 2-8 |
| `r11-r12` | GPR11-12 | 32-bit | Volatile registers |
| `r13` | GPR13 / SDA | 32-bit | Small Data Area pointer (by convention) |
| `r14-r31` | GPR14-31 | 32-bit | Non-volatile registers (callee-saved) |

**Total: 32 registers (r0 through r31)**
**Usage:**
- Integer arithmetic and logical operations
- Memory addressing
- Data storage and manipulation
- Function parameters and return values

---

## 2. FLOATING-POINT REGISTERS (FPRs)

### Floating-Point Registers
| Register | Name | Width | Description |
|----------|------|-------|-------------|
| `f0` | FPR0 | 64-bit | Volatile FP register |
| `f1-f13` | FPR1-13 | 64-bit | Function arguments and volatile registers |
| `f14-f31` | FPR14-31 | 64-bit | Non-volatile FP registers (callee-saved) |

**Total: 32 registers (f0 through f31)**
**Width:** 64 bits each (double-precision)
**Usage:**
- Double-precision floating-point operations
- Single-precision floating-point operations (using lower 32 bits)
- Paired-single SIMD operations (Gekko/Broadway extension)

**Note:** In paired-single mode, each FPR is treated as two 32-bit single-precision floats.

---

## 3. SPECIAL PURPOSE REGISTERS (SPRs)

### 3.1 User-Level SPRs

| SPR# | Name | Description |
|------|------|-------------|
| 1 | XER | Fixed-Point Exception Register |
| 8 | LR | Link Register (return addresses) |
| 9 | CTR | Count Register (loop counter) |
| 268-269 | TBL/TBU | Time Base Lower/Upper (read-only in user mode) |

#### XER (SPR 1) - Fixed-Point Exception Register
**Bits:**
- 0: Summary Overflow (SO)
- 1: Overflow (OV)
- 2: Carry (CA)
- 3-24: Reserved
- 25-31: Byte count for lswx/stswx

#### Link Register (SPR 8)
**Purpose:** Stores return address for function calls (used by `bl`, `blr`)

#### Count Register (SPR 9)
**Purpose:** Loop counter for `bdnz`, `bdz` instructions; also used for indirect branches

### 3.2 Supervisor-Level SPRs

#### Exception and Interrupt Registers
| SPR# | Name | Description |
|------|------|-------------|
| 18 | DSISR | Data Storage Interrupt Status Register |
| 19 | DAR | Data Address Register (faulting address) |
| 22 | DEC | Decrementer Register |
| 26 | SRR0 | Save/Restore Register 0 (exception PC) |
| 27 | SRR1 | Save/Restore Register 1 (exception MSR) |
| 272-275 | SPRG0-3 | Special Purpose Register General 0-3 |
| 282 | EAR | External Access Register |
| 287 | PVR | Processor Version Register |

#### Time Base Registers (Write)
| SPR# | Name | Description |
|------|------|-------------|
| 284 | TBL | Time Base Lower (write, supervisor only) |
| 285 | TBU | Time Base Upper (write, supervisor only) |

### 3.3 Gekko/Broadway Specific SPRs

#### Graphics Quantization Registers
| SPR# | Name | Description |
|------|------|-------------|
| 912 | GQR0 | Graphics Quantization Register 0 |
| 913 | GQR1 | Graphics Quantization Register 1 |
| 914 | GQR2 | Graphics Quantization Register 2 |
| 915 | GQR3 | Graphics Quantization Register 3 |
| 916 | GQR4 | Graphics Quantization Register 4 |
| 917 | GQR5 | Graphics Quantization Register 5 |
| 918 | GQR6 | Graphics Quantization Register 6 |
| 919 | GQR7 | Graphics Quantization Register 7 |

**Purpose:** Control quantization and de-quantization for paired-single load/store operations
**Bits:**
- 0-15: Store quantization parameters (type, scale)
- 16-31: Load quantization parameters (type, scale)

#### Hardware Implementation Dependent Registers
| SPR# | Name | Description |
|------|------|-------------|
| 920 | HID2 | Hardware Implementation Dependent Register 2 |
| 921 | WPAR | Write Gather Pipe Address Register |
| 922 | DMA_U | DMA Upper Address |
| 923 | DMA_L | DMA Lower Address |
| 936-939 | UMMCR0, UPMC1, UPMC2, USIA | User Performance Monitor (read-only) |
| 940-943 | UMMCR1, UPMC3, UPMC4, USDA | User Performance Monitor (read-only) |
| 952 | MMCR0 | Monitor Mode Control Register 0 |
| 953 | PMC1 | Performance Monitor Counter 1 |
| 954 | PMC2 | Performance Monitor Counter 2 |
| 955 | SIA | Sampled Instruction Address |
| 956 | MMCR1 | Monitor Mode Control Register 1 |
| 957 | PMC3 | Performance Monitor Counter 3 |
| 958 | PMC4 | Performance Monitor Counter 4 |
| 959 | SDA | Sampled Data Address |
| 1008 | HID0 | Hardware Implementation Dependent Register 0 |
| 1009 | HID1 | Hardware Implementation Dependent Register 1 |
| 1010 | IABR | Instruction Address Breakpoint Register |
| 1011 | HID4 | Hardware Implementation Dependent Register 4 (Broadway) |
| 1013 | DABR | Data Address Breakpoint Register |
| 1017 | L2CR | L2 Cache Control Register |
| 1019 | ICTC | Instruction Cache Throttling Control |
| 1020 | THRM1 | Thermal Management Register 1 |
| 1021 | THRM2 | Thermal Management Register 2 |
| 1022 | THRM3 | Thermal Management Register 3 |

#### HID0 (SPR 1008) - Key Control Bits
- Bit 0: EMCP - Enable Machine Check Pin
- Bit 3: EBA - Enable Bus Address Parity
- Bit 4: EBD - Enable Bus Data Parity
- Bit 7: BCLK - Bus Clock
- Bit 8: EICE - Enable ICE (In-Circuit Emulation)
- Bit 11: ECLK - Enable CLK_OUT
- Bit 15: ICE - Instruction Cache Enable
- Bit 16: DCE - Data Cache Enable
- Bit 17: ILOCK - Instruction Cache Lock
- Bit 18: DLOCK - Data Cache Lock
- Bit 19: ICFI - Instruction Cache Flash Invalidate
- Bit 20: DCFI - Data Cache Flash Invalidate
- Bit 21: SPD - Speculative Cache Access Disable
- Bit 22: IFEM - Instruction Fetch Enable Monitor
- Bit 23: SGE - Store Gathering Enable
- Bit 24: DCFA - Data Cache Flush Assist
- Bit 25: BTIC - Branch Target Instruction Cache Enable
- Bit 27: BHT - Branch History Table Enable
- Bit 31: NOOPTI - No-op Touch Instructions

#### HID1 (SPR 1009)
Controls L2 cache modes and bus settings

#### HID2 (SPR 920) - Gekko/Broadway Specific
- Bit 29: LCE - Locked Cache Enable
- Bit 30: PSE - Paired Single Enable
- Bit 31: WPE - Write Pipe Enable

#### HID4 (SPR 1011) - Broadway Only
- Bit 31: H4A - Special Broadway feature
- Other bits: Implementation-specific

#### L2CR (SPR 1017) - L2 Cache Control
Controls L2 cache configuration, invalidation, and enablement

---

## 4. CONDITION REGISTER (CR)

**Width:** 32 bits (8 fields of 4 bits each)
**Fields:** CR0 through CR7

### CR Field Bits (4 bits per field)
| Bit | Name | Description |
|-----|------|-------------|
| 0 | LT | Less Than |
| 1 | GT | Greater Than |
| 2 | EQ | Equal |
| 3 | SO | Summary Overflow (copied from XER[SO]) |

**Usage:**
- CR0: Automatically set by integer instructions with record bit (e.g., `add.`)
- CR1: Automatically set by floating-point instructions with record bit (e.g., `fadd.`)
- CR2-CR7: Set by `cmp`, `cmpi`, `cmpl`, `cmpli` instructions with explicit field specification

---

## 5. MACHINE STATE REGISTER (MSR)

**SPR Access:** Accessed via `mtmsr`, `mfmsr`
**Width:** 32 bits

### MSR Bit Fields
| Bit | Name | Description |
|-----|------|-------------|
| 13 | POW | Power Management Enable |
| 15 | ILE | Interrupt Little-Endian Mode |
| 16 | EE | External Interrupt Enable |
| 17 | PR | Privilege Level (0=supervisor, 1=user) |
| 18 | FP | Floating-Point Available |
| 19 | ME | Machine Check Enable |
| 20 | FE0 | Floating-Point Exception Mode 0 |
| 21 | SE | Single-Step Trace Enable |
| 22 | BE | Branch Trace Enable |
| 23 | FE1 | Floating-Point Exception Mode 1 |
| 25 | IP | Interrupt Prefix |
| 26 | IR | Instruction Address Translation Enable |
| 27 | DR | Data Address Translation Enable |
| 29 | PM | Performance Monitor Marked Mode |
| 30 | RI | Recoverable Interrupt |
| 31 | LE | Little-Endian Mode |

---

## 6. FLOATING-POINT STATUS AND CONTROL REGISTER (FPSCR)

**Width:** 32 bits
**Access:** Via `mffs`, `mtfsf`, `mtfsfi`, `mtfsb0`, `mtfsb1`

### FPSCR Bit Fields
| Bit | Name | Description |
|-----|------|-------------|
| 0 | FX | Floating-Point Exception Summary |
| 1 | FEX | Floating-Point Enabled Exception Summary |
| 2 | VX | Floating-Point Invalid Operation Exception Summary |
| 3 | OX | Floating-Point Overflow Exception |
| 4 | UX | Floating-Point Underflow Exception |
| 5 | ZX | Floating-Point Zero Divide Exception |
| 6 | XX | Floating-Point Inexact Exception |
| 7 | VXSNAN | Invalid Operation Exception (SNaN) |
| 8 | VXISI | Invalid Operation Exception (∞-∞) |
| 9 | VXIDI | Invalid Operation Exception (∞/∞) |
| 10 | VXZDZ | Invalid Operation Exception (0/0) |
| 11 | VXIMZ | Invalid Operation Exception (∞*0) |
| 12 | VXVC | Invalid Operation Exception (Invalid Compare) |
| 13 | FR | Floating-Point Fraction Rounded |
| 14 | FI | Floating-Point Fraction Inexact |
| 15-19 | FPRF | Floating-Point Result Flags |
| 20 | - | Reserved |
| 21 | VXSOFT | Invalid Operation Exception (Software Request) |
| 22 | VXSQRT | Invalid Operation Exception (Invalid Square Root) |
| 23 | VXCVI | Invalid Operation Exception (Invalid Integer Convert) |
| 24 | VE | Invalid Operation Exception Enable |
| 25 | OE | Overflow Exception Enable |
| 26 | UE | Underflow Exception Enable |
| 27 | ZE | Zero Divide Exception Enable |
| 28 | XE | Inexact Exception Enable |
| 29 | NI | Non-IEEE Mode |
| 30-31 | RN | Rounding Mode (00=nearest, 01=toward zero, 10=toward +∞, 11=toward -∞) |

---

## 7. SEGMENT REGISTERS

**Count:** 16 registers (SR0 through SR15)
**Width:** 32 bits each
**Access:** Via `mtsr`, `mfsr`, `mtsrin`, `mfsrin` (supervisor only)

| Register | Description |
|----------|-------------|
| SR0 | Segment Register 0 (effective addresses 0x00000000-0x0FFFFFFF) |
| SR1 | Segment Register 1 (effective addresses 0x10000000-0x1FFFFFFF) |
| SR2 | Segment Register 2 (effective addresses 0x20000000-0x2FFFFFFF) |
| SR3 | Segment Register 3 (effective addresses 0x30000000-0x3FFFFFFF) |
| SR4 | Segment Register 4 (effective addresses 0x40000000-0x4FFFFFFF) |
| SR5 | Segment Register 5 (effective addresses 0x50000000-0x5FFFFFFF) |
| SR6 | Segment Register 6 (effective addresses 0x60000000-0x6FFFFFFF) |
| SR7 | Segment Register 7 (effective addresses 0x70000000-0x7FFFFFFF) |
| SR8 | Segment Register 8 (effective addresses 0x80000000-0x8FFFFFFF) |
| SR9 | Segment Register 9 (effective addresses 0x90000000-0x9FFFFFFF) |
| SR10 | Segment Register 10 (effective addresses 0xA0000000-0xAFFFFFFF) |
| SR11 | Segment Register 11 (effective addresses 0xB0000000-0xBFFFFFFF) |
| SR12 | Segment Register 12 (effective addresses 0xC0000000-0xCFFFFFFF) |
| SR13 | Segment Register 13 (effective addresses 0xD0000000-0xDFFFFFFF) |
| SR14 | Segment Register 14 (effective addresses 0xE0000000-0xEFFFFFFF) |
| SR15 | Segment Register 15 (effective addresses 0xF0000000-0xFFFFFFFF) |

**Usage:** Virtual to physical address translation (256 MB segments)

---

## 8. BLOCK ADDRESS TRANSLATION (BAT) REGISTERS

### Instruction BAT Registers
| SPR# | Name | Description |
|------|------|-------------|
| 528 | IBAT0U | Instruction BAT 0 Upper |
| 529 | IBAT0L | Instruction BAT 0 Lower |
| 530 | IBAT1U | Instruction BAT 1 Upper |
| 531 | IBAT1L | Instruction BAT 1 Lower |
| 532 | IBAT2U | Instruction BAT 2 Upper |
| 533 | IBAT2L | Instruction BAT 2 Lower |
| 534 | IBAT3U | Instruction BAT 3 Upper |
| 535 | IBAT3L | Instruction BAT 3 Lower |

### Data BAT Registers
| SPR# | Name | Description |
|------|------|-------------|
| 536 | DBAT0U | Data BAT 0 Upper |
| 537 | DBAT0L | Data BAT 0 Lower |
| 538 | DBAT1U | Data BAT 1 Upper |
| 539 | DBAT1L | Data BAT 1 Lower |
| 540 | DBAT2U | Data BAT 2 Upper |
| 541 | DBAT2L | Data BAT 2 Lower |
| 542 | DBAT3U | Data BAT 3 Upper |
| 543 | DBAT3L | Data BAT 3 Lower |

**Usage:** Direct mapping of large memory blocks (128 KB to 256 MB)
**Total:** 4 pairs of IBATs and 4 pairs of DBATs

### BATU (Upper BAT Register) Format
- Bits 0-14: Block Effective Page Index (BEPI)
- Bits 15-18: Reserved
- Bits 19-29: Block Length (BL) - encoded size
- Bit 30: Vs (Valid in supervisor mode)
- Bit 31: Vp (Valid in user mode)

### BATL (Lower BAT Register) Format
- Bits 0-14: Block Real Page Number (BRPN)
- Bits 15-24: Reserved
- Bit 25: W (Write-through)
- Bit 26: I (Caching inhibited)
- Bit 27: M (Memory coherence)
- Bit 28: G (Guarded)
- Bits 29-30: Reserved
- Bit 31: PP (Protection)

---

# MEMORY MAP

## GAMECUBE MEMORY MAP

### Physical Memory Regions

| Address Range | Size | Description |
|---------------|------|-------------|
| 0x00000000 - 0x017FFFFF | 24 MB | Main Memory (MEM1) - Physical |
| 0x01800000 - 0x01FFFFFF | 8 MB | Main Memory (reserved/unusable) |
| 0x02000000 - 0x02FFFFFF | 16 MB | Embedded Frame Buffer (EFB) |
| 0x08000000 - 0x0BFFFFFF | 64 MB | Locked Cache / General Purpose |
| 0x0C000000 - 0x0C003FFF | 16 KB | Memory Mapped I/O (CP/PE/VI/PI) |
| 0x0C004000 - 0x0C005FFF | 8 KB | Memory Mapped I/O (MI) |
| 0x0C006000 - 0x0C006FFF | 4 KB | Memory Mapped I/O (DSP) |
| 0x0C006C00 - 0x0C006FFF | 1 KB | Memory Mapped I/O (DI) |
| 0x0C008000 - 0x0FFFFFFF | ~64 MB | External Hardware Registers |
| 0x10000000 - 0x13FFFFFF | 64 MB | BootROM / SRAM |

### Virtual Memory Regions (Effective Addresses)

| Address Range | Mirror | Cache | Description |
|---------------|--------|-------|-------------|
| 0x00000000 - 0x017FFFFF | Physical | Yes | Main Memory - Cached |
| 0x80000000 - 0x817FFFFF | 0x00000000 | Yes | Main Memory - Cached (most common) |
| 0xC0000000 - 0xC17FFFFF | 0x00000000 | No | Main Memory - Uncached |
| 0xE0000000 - 0xFFFFFFFF | 0x00000000 | No | L1 Cache locked region |

---

## WII MEMORY MAP

### Physical Memory Regions

| Address Range | Size | Description |
|---------------|------|-------------|
| 0x00000000 - 0x017FFFFF | 24 MB | MEM1 - Main Memory (GameCube compatible) |
| 0x02000000 - 0x02FFFFFF | 16 MB | Embedded Frame Buffer (EFB) |
| 0x08000000 - 0x0BFFFFFF | 64 MB | Locked Cache |
| 0x0C000000 - 0x0C003FFF | 16 KB | Memory Mapped I/O (CP/PE/VI/PI) |
| 0x0C004000 - 0x0C005FFF | 8 KB | Memory Mapped I/O (MI) |
| 0x0C006000 - 0x0C006FFF | 4 KB | Memory Mapped I/O (DSP) |
| 0x0C006C00 - 0x0C006FFF | 1 KB | Memory Mapped I/O (DI) |
| 0x0C008000 - 0x0CFFFFFF | ~16 MB | External Hardware Registers |
| 0x0D000000 - 0x0D7FFFFF | 8 MB | Internal I/O (Starlet/ARM) |
| 0x0D800000 - 0x0DFFFFFF | 8 MB | Internal SRAM |
| 0x10000000 - 0x13FFFFFF | 64 MB | MEM2 - Additional Memory (Wii specific) |

### Virtual Memory Regions (Effective Addresses)

| Address Range | Mirror | Cache | Description |
|---------------|--------|-------|-------------|
| 0x00000000 - 0x017FFFFF | Physical | Yes | MEM1 - Cached |
| 0x80000000 - 0x817FFFFF | 0x00000000 | Yes | MEM1 - Cached (common) |
| 0x90000000 - 0x93FFFFFF | 0x10000000 | Yes | MEM2 - Cached |
| 0xC0000000 - 0xC17FFFFF | 0x00000000 | No | MEM1 - Uncached |
| 0xD0000000 - 0xD3FFFFFF | 0x10000000 | No | MEM2 - Uncached |
| 0xCD000000 - 0xCD008000 | I/O | No | Hardware I/O Registers |

---

# HARDWARE I/O REGISTERS

## MEMORY-MAPPED I/O BASE ADDRESSES

### GameCube/Wii Hardware Register Regions

| Base Address | Size | Component | Description |
|--------------|------|-----------|-------------|
| 0xCC000000 | 64 bytes | CP | Command Processor (Fifo) |
| 0xCC001000 | 64 bytes | PE | Pixel Engine |
| 0xCC002000 | 256 bytes | VI | Video Interface |
| 0xCC003000 | 64 bytes | PI | Processor Interface |
| 0xCC004000 | 256 bytes | MI | Memory Interface |
| 0xCC005000 | 1024 bytes | DSP | Audio DSP Interface |
| 0xCC006000 | 256 bytes | DI | DVD Interface |
| 0xCC006400 | 256 bytes | SI | Serial Interface |
| 0xCC006800 | 256 bytes | EXI | External Interface |
| 0xCC006C00 | 64 bytes | AI | Audio Interface |
| 0xCC008000 | Varies | GX | Graphics FIFO (Write-Gather Pipe) |

**Note:** In virtual address space, these are typically accessed at **0xCC00xxxx** (uncached)

---

## 1. COMMAND PROCESSOR (CP) - 0xCC000000

| Offset | Name | Access | Description |
|--------|------|--------|-------------|
| 0x00 | CP_SR | R/W | Command Processor Status Register |
| 0x02 | CP_CR | R/W | Command Processor Control Register |
| 0x04 | CP_CLEAR | W | Clear overflow/underflow |
| 0x0C | CP_TOKEN | R | Current token value |
| 0x10 | CP_BBOXLEFT | R | Bounding Box Left |
| 0x12 | CP_BBOXTOP | R | Bounding Box Top |
| 0x14 | CP_BBOXRIGHT | R | Bounding Box Right |
| 0x16 | CP_BBOXBOTTOM | R | Bounding Box Bottom |
| 0x20 | CP_FIFO_BASE_LO | R/W | FIFO Base Address (low) |
| 0x22 | CP_FIFO_BASE_HI | R/W | FIFO Base Address (high) |
| 0x24 | CP_FIFO_END_LO | R/W | FIFO End Address (low) |
| 0x26 | CP_FIFO_END_HI | R/W | FIFO End Address (high) |
| 0x28 | CP_FIFO_HIWMARK_LO | R/W | FIFO High Watermark (low) |
| 0x2A | CP_FIFO_HIWMARK_HI | R/W | FIFO High Watermark (high) |
| 0x2C | CP_FIFO_LOWMARK_LO | R/W | FIFO Low Watermark (low) |
| 0x2E | CP_FIFO_LOWMARK_HI | R/W | FIFO Low Watermark (high) |
| 0x30 | CP_FIFO_RW_DIST_LO | R/W | FIFO Read/Write Distance (low) |
| 0x32 | CP_FIFO_RW_DIST_HI | R/W | FIFO Read/Write Distance (high) |
| 0x34 | CP_FIFO_WRITE_PTR_LO | R/W | FIFO Write Pointer (low) |
| 0x36 | CP_FIFO_WRITE_PTR_HI | R/W | FIFO Write Pointer (high) |
| 0x38 | CP_FIFO_READ_PTR_LO | R | FIFO Read Pointer (low) |
| 0x3A | CP_FIFO_READ_PTR_HI | R | FIFO Read Pointer (high) |
| 0x3C | CP_FIFO_BP_LO | R | FIFO Breakpoint (low) |
| 0x3E | CP_FIFO_BP_HI | R | FIFO Breakpoint (high) |

---

## 2. PIXEL ENGINE (PE) - 0xCC001000

| Offset | Name | Access | Description |
|--------|------|--------|-------------|
| 0x00 | PE_ZCONF | R/W | Z Configuration |
| 0x02 | PE_ALPHACONF | R/W | Alpha Configuration |
| 0x04 | PE_DSTALPHACONF | R/W | Destination Alpha Configuration |
| 0x06 | PE_ALPHAMODE | R/W | Alpha Mode |
| 0x08 | PE_ALPHAREAD | R/W | Alpha Read Mode |
| 0x0A | PE_CTRL | R/W | Pixel Engine Control |
| 0x0C | PE_TOKEN | R/W | Token Register |
| 0x0E | PE_TOKEN_INT | R/W | Token Interrupt |
| 0x10 | PE_PERF0 | R | Performance Counter 0 |
| 0x12 | PE_PERF1 | R | Performance Counter 1 |
| 0x14 | PE_PERF2 | R | Performance Counter 2 |
| 0x16 | PE_PERF3 | R | Performance Counter 3 |

---

## 3. VIDEO INTERFACE (VI) - 0xCC002000

| Offset | Name | Access | Description |
|--------|------|--------|-------------|
| 0x00 | VI_VTR | R/W | Video Timing Register (Vertical) |
| 0x02 | VI_DCR | R/W | Display Configuration Register |
| 0x04 | VI_HTR0 | R/W | Horizontal Timing 0 |
| 0x06 | VI_HTR1 | R/W | Horizontal Timing 1 |
| 0x08 | VI_VTO | R/W | Vertical Timing Odd field |
| 0x0A | VI_VTE | R/W | Vertical Timing Even field |
| 0x0C | VI_BBOI | R/W | Burst Blanking Odd Interval |
| 0x0E | VI_BBEI | R/W | Burst Blanking Even Interval |
| 0x10 | VI_TFBL | R/W | Top Field Base (Left) |
| 0x14 | VI_TFBR | R/W | Top Field Base (Right) |
| 0x18 | VI_BFBL | R/W | Bottom Field Base (Left) |
| 0x1C | VI_BFBR | R/W | Bottom Field Base (Right) |
| 0x20 | VI_DPV | R/W | Display Position Vertical |
| 0x22 | VI_DPH | R/W | Display Position Horizontal |
| 0x24 | VI_DI0 | R/W | Display Interrupt 0 |
| 0x26 | VI_DI1 | R/W | Display Interrupt 1 |
| 0x28 | VI_DI2 | R/W | Display Interrupt 2 |
| 0x2A | VI_DI3 | R/W | Display Interrupt 3 |
| 0x2C | VI_DL0 | R/W | Display Latch 0 |
| 0x2E | VI_DL1 | R/W | Display Latch 1 |
| 0x30 | VI_HSR | R/W | Horizontal Scaling (width) |
| 0x34 | VI_HSW | R/W | Horizontal Scaling Width |
| 0x38 | VI_FBW | R/W | Filter Coefficient Table 0 |
| 0x3C | VI_FCT1 | R/W | Filter Coefficient Table 1 |
| 0x40 | VI_FCT2 | R/W | Filter Coefficient Table 2 |
| 0x44 | VI_FCT3 | R/W | Filter Coefficient Table 3 |
| 0x48 | VI_FCT4 | R/W | Filter Coefficient Table 4 |
| 0x4C | VI_FCT5 | R/W | Filter Coefficient Table 5 |
| 0x50 | VI_FCT6 | R/W | Filter Coefficient Table 6 |
| 0x56 | VI_VICLK | R/W | Video Clock |
| 0x58 | VI_VISEL | R/W | Video Select |
| 0x6C | VI_HBE | R/W | Horizontal Blank End |
| 0x6E | VI_HBS | R/W | Horizontal Blank Start |

---

## 4. PROCESSOR INTERFACE (PI) - 0xCC003000

| Offset | Name | Access | Description |
|--------|------|--------|-------------|
| 0x00 | PI_INTSR | R/W | Interrupt Cause |
| 0x04 | PI_INTMASK | R/W | Interrupt Mask |
| 0x0C | PI_RESET | W | Reset (write 0 to reset) |
| 0x18 | PI_FLIPPER_REV | R | Flipper/Hollywood Revision |
| 0x24 | PI_FLIPPER_UNK | R/W | Unknown Flipper Register |

### Interrupt Bits (PI_INTSR / PI_INTMASK)
- Bit 0: CP Interrupt
- Bit 1: PE Token Interrupt
- Bit 2: PE Finish Interrupt
- Bit 3: SI Interrupt
- Bit 4: EXI Interrupt
- Bit 5: AI Interrupt
- Bit 6: DSP Interrupt
- Bit 7: Memory Interface Interrupt
- Bit 8: VI Interrupt
- Bit 9: PI Error Interrupt
- Bit 10: RSW Interrupt (Wii only)
- Bit 11: DI Interrupt
- Bit 12: HSP Interrupt (Wii only)
- Bit 13: Debug Interrupt
- Bit 14: IPC Interrupt (Wii only)

---

## 5. MEMORY INTERFACE (MI) - 0xCC004000

| Offset | Name | Access | Description |
|--------|------|--------|-------------|
| 0x00 | MI_MEM0_CTRL | R/W | Memory 0 Control |
| 0x04 | MI_MEM0_UNK0 | R/W | Memory 0 Unknown 0 |
| 0x08 | MI_MEM0_UNK1 | R/W | Memory 0 Unknown 1 |
| 0x0C | MI_MEM0_ADDR0 | R/W | Memory 0 Address 0 |
| 0x10 | MI_MEM0_ADDR1 | R/W | Memory 0 Address 1 |
| 0x20 | MI_MEM1_CTRL | R/W | Memory 1 Control (Wii MEM2) |
| 0x24 | MI_MEM1_UNK0 | R/W | Memory 1 Unknown 0 |
| 0x28 | MI_MEM1_UNK1 | R/W | Memory 1 Unknown 1 |
| 0x2C | MI_MEM1_ADDR0 | R/W | Memory 1 Address 0 |
| 0x30 | MI_MEM1_ADDR1 | R/W | Memory 1 Address 1 |
| 0x34 | MI_PROT | R/W | Memory Protection Range |
| 0x36 | MI_PROT_TYPE | R/W | Memory Protection Type |
| 0x3E | MI_TIMER | R | Memory Interface Timer |
| 0x40 | MI_UNKNOWN | R/W | Unknown Register |

---

## 6. DSP INTERFACE (DSP) - 0xCC005000

| Offset | Name | Access | Description |
|--------|------|--------|-------------|
| 0x00 | DSP_OUTMBOX_H | R | DSP to CPU Mailbox High |
| 0x02 | DSP_OUTMBOX_L | R | DSP to CPU Mailbox Low |
| 0x04 | DSP_INMBOX_H | W | CPU to DSP Mailbox High |
| 0x06 | DSP_INMBOX_L | W | CPU to DSP Mailbox Low |
| 0x0A | DSP_CSR | R/W | DSP Control/Status Register |
| 0x0C | DSP_AR_DMA_MMADDR_H | R/W | ARAM DMA Main Memory Address High |
| 0x0E | DSP_AR_DMA_MMADDR_L | R/W | ARAM DMA Main Memory Address Low |
| 0x10 | DSP_AR_DMA_ARADDR_H | R/W | ARAM DMA ARAM Address High |
| 0x12 | DSP_AR_DMA_ARADDR_L | R/W | ARAM DMA ARAM Address Low |
| 0x14 | DSP_AR_DMA_CNT_H | R/W | ARAM DMA Count High |
| 0x16 | DSP_AR_DMA_CNT_L | R/W | ARAM DMA Count Low |
| 0x20 | DSP_AIDCR | R/W | Audio Interface DMA Control |
| 0x24 | DSP_AIVR | R/W | Audio Interface Volume |
| 0x28 | DSP_AISCNT | R/W | Audio Interface Sample Count |
| 0x2A | DSP_AIIT | R/W | Audio Interface Interrupt Timing |

---

## 7. DVD INTERFACE (DI) - 0xCC006000

| Offset | Name | Access | Description |
|--------|------|--------|-------------|
| 0x00 | DI_SR | R/W | DI Status Register |
| 0x04 | DI_CVR | R/W | DI Cover Register |
| 0x08 | DI_CMDBUF0 | R/W | DI Command Buffer 0 |
| 0x0C | DI_CMDBUF1 | R/W | DI Command Buffer 1 |
| 0x10 | DI_CMDBUF2 | R/W | DI Command Buffer 2 |
| 0x14 | DI_MAR | R/W | DMA Memory Address |
| 0x18 | DI_LENGTH | R/W | DMA Transfer Length |
| 0x1C | DI_CR | R/W | DI Control Register |
| 0x20 | DI_IMMBUF | R | Immediate Data Buffer |
| 0x24 | DI_CONFIG | R/W | DI Configuration |

---

## 8. SERIAL INTERFACE (SI) - 0xCC006400

| Offset | Name | Access | Description |
|--------|------|--------|-------------|
| 0x00 | SI_C0OUTBUF | R/W | Channel 0 Output Buffer |
| 0x04 | SI_C0INBUF_H | R | Channel 0 Input Buffer High |
| 0x08 | SI_C0INBUF_L | R | Channel 0 Input Buffer Low |
| 0x0C | SI_C1OUTBUF | R/W | Channel 1 Output Buffer |
| 0x10 | SI_C1INBUF_H | R | Channel 1 Input Buffer High |
| 0x14 | SI_C1INBUF_L | R | Channel 1 Input Buffer Low |
| 0x18 | SI_C2OUTBUF | R/W | Channel 2 Output Buffer |
| 0x1C | SI_C2INBUF_H | R | Channel 2 Input Buffer High |
| 0x20 | SI_C2INBUF_L | R | Channel 2 Input Buffer Low |
| 0x24 | SI_C3OUTBUF | R/W | Channel 3 Output Buffer |
| 0x28 | SI_C3INBUF_H | R | Channel 3 Input Buffer High |
| 0x2C | SI_C3INBUF_L | R | Channel 3 Input Buffer Low |
| 0x30 | SI_POLL | R/W | SI Poll Register |
| 0x34 | SI_COMCSR | R/W | SI Communication Control/Status |
| 0x38 | SI_SR | R/W | SI Status Register |
| 0x3C | SI_EXILK | R/W | SI EXI Clock Lock |

---

## 9. EXTERNAL INTERFACE (EXI) - 0xCC006800

Each EXI channel has the following registers:

### EXI Channel 0 - 0xCC006800
| Offset | Name | Access | Description |
|--------|------|--------|-------------|
| 0x00 | EXI0_CSR | R/W | Channel 0 Control/Status |
| 0x04 | EXI0_MAR | R/W | Channel 0 DMA Memory Address |
| 0x08 | EXI0_LENGTH | R/W | Channel 0 DMA Length |
| 0x0C | EXI0_CR | R/W | Channel 0 Control Register |
| 0x10 | EXI0_DATA | R/W | Channel 0 Immediate Data |

### EXI Channel 1 - 0xCC006814
| Offset | Name | Access | Description |
|--------|------|--------|-------------|
| 0x14 | EXI1_CSR | R/W | Channel 1 Control/Status |
| 0x18 | EXI1_MAR | R/W | Channel 1 DMA Memory Address |
| 0x1C | EXI1_LENGTH | R/W | Channel 1 DMA Length |
| 0x20 | EXI1_CR | R/W | Channel 1 Control Register |
| 0x24 | EXI1_DATA | R/W | Channel 1 Immediate Data |

### EXI Channel 2 - 0xCC006828
| Offset | Name | Access | Description |
|--------|------|--------|-------------|
| 0x28 | EXI2_CSR | R/W | Channel 2 Control/Status |
| 0x2C | EXI2_MAR | R/W | Channel 2 DMA Memory Address |
| 0x30 | EXI2_LENGTH | R/W | Channel 2 DMA Length |
| 0x34 | EXI2_CR | R/W | Channel 2 Control Register |
| 0x38 | EXI2_DATA | R/W | Channel 2 Immediate Data |

**EXI Devices:**
- EXI Channel 0: Memory Card Slot A
- EXI Channel 1: Memory Card Slot B
- EXI Channel 2: Real-Time Clock, SRAM, AD16 (Wii: Bluetooth/WiFi)

---

## 10. AUDIO INTERFACE (AI) - 0xCC006C00

| Offset | Name | Access | Description |
|--------|------|--------|-------------|
| 0x00 | AI_CR | R/W | Audio Interface Control |
| 0x04 | AI_VR | R/W | Audio Interface Volume |
| 0x08 | AI_SCNT | R | Audio Interface Sample Counter |
| 0x0C | AI_IT | R/W | Audio Interface Interrupt Timing |

---

## 11. WRITE-GATHER PIPE (FIFO) - 0xCC008000

| Address | Description |
|---------|-------------|
| 0xCC008000 | Graphics FIFO Write Address |

**Usage:** Direct writes to this address are gathered and sent to the GPU's command processor
**Access:** Write-only, 32-byte aligned writes recommended

---

# CACHE ARCHITECTURE

## L1 INSTRUCTION CACHE

| Property | Value |
|----------|-------|
| Size | 32 KB |
| Line Size | 32 bytes |
| Associativity | 8-way set-associative |
| Sets | 128 sets |
| Write Policy | N/A (instruction cache) |
| Replacement | LRU (Least Recently Used) |

**Control:** Enabled/disabled via HID0[ICE]
**Invalidation:** Via `icbi` instruction or HID0[ICFI]

---

## L1 DATA CACHE

| Property | Value |
|----------|-------|
| Size | 32 KB |
| Line Size | 32 bytes |
| Associativity | 8-way set-associative |
| Sets | 128 sets |
| Write Policy | Write-back, write-allocate |
| Replacement | LRU (Least Recently Used) |

**Control:** Enabled/disabled via HID0[DCE]
**Locked Mode:** 16 KB can be locked as data scratchpad
**Invalidation:** Via `dcbi` instruction or HID0[DCFI]

---

## L2 CACHE

| Property | Value |
|----------|-------|
| Size | 256 KB |
| Line Size | 64 bytes (2x L1 line size) |
| Associativity | 2-way set-associative |
| Sets | 2048 sets |
| Write Policy | Write-back |
| Unified | Yes (instructions and data) |

**Control:** Configured via L2CR (SPR 1017)
**Inclusion:** L2 includes all L1 cache contents

---

## CACHE MANAGEMENT INSTRUCTIONS

| Instruction | Description |
|-------------|-------------|
| `dcbf rA, rB` | Data Cache Block Flush |
| `dcbi rA, rB` | Data Cache Block Invalidate (supervisor only) |
| `dcbst rA, rB` | Data Cache Block Store |
| `dcbt rA, rB` | Data Cache Block Touch (prefetch) |
| `dcbtst rA, rB` | Data Cache Block Touch for Store |
| `dcbz rA, rB` | Data Cache Block Clear to Zero |
| `icbi rA, rB` | Instruction Cache Block Invalidate |
| `sync` | Synchronize (enforce memory ordering) |
| `isync` | Instruction Synchronize |
| `eieio` | Enforce In-Order Execution of I/O |

---

## TRANSLATION LOOKASIDE BUFFER (TLB)

| Property | Value |
|----------|-------|
| Size | 128 entries |
| Associativity | 2-way set-associative |
| Page Size | 4 KB (standard) |
| Replacement | Hardware-managed, pseudo-LRU |

**Purpose:** Caches page table translations for fast virtual-to-physical address lookup

---

# SUMMARY

## Register Count Summary

| Category | Count | Total Width |
|----------|-------|-------------|
| General Purpose Registers (GPRs) | 32 | 32-bit each |
| Floating-Point Registers (FPRs) | 32 | 64-bit each |
| Special Purpose Registers (SPRs) | ~70 | 32-bit each |
| Condition Register Fields | 8 | 4-bit each |
| Segment Registers | 16 | 32-bit each |
| BAT Registers (IBAT + DBAT) | 16 | 32-bit each |
| **Total Architectural Registers** | **~174** | - |

---

## Memory Region Summary

| System | Main RAM | Additional RAM | I/O Range | Total Addressable |
|--------|----------|----------------|-----------|-------------------|
| GameCube | 24 MB | - | 16 MB | 4 GB (32-bit) |
| Wii | 24 MB (MEM1) | 64 MB (MEM2) | 16 MB | 4 GB (32-bit) |

---

## References

1. IBM Gekko RISC Microprocessor User's Manual
2. PowerPC Microprocessor Family: The Programming Environments Manual
3. Yet Another GameCube Documentation (YAGCD)
4. WiiBrew.org Hardware Documentation
5. Dolphin Emulator Source Code
6. PowerPC 750CXe Technical Summary

---

**Document Version:** 1.0
**Last Updated:** November 3, 2025
**Target Platforms:** Nintendo GameCube (Gekko CPU), Nintendo Wii (Broadway CPU)

