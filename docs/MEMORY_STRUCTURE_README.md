# Gecko/Broadway Memory Structure for Emulation

## Overview

This memory structure (`gecko_memory.h`) provides a complete representation of the GameCube (Gekko) and Wii (Broadway) memory architecture for use in emulators. It accurately models hardware memory layout, including main RAM, memory-mapped I/O registers, and virtual address translation.

## Features

✅ **Complete Hardware Register Definitions** - All memory-mapped I/O registers for:
- Command Processor (CP)
- Pixel Engine (PE)
- Video Interface (VI)
- Processor Interface (PI)
- Memory Interface (MI)
- DSP Interface
- DVD Interface (DI)
- Serial Interface (SI)
- External Interface (EXI)
- Audio Interface (AI)

✅ **Memory Regions**
- MEM1: 24 MB main memory (GameCube/Wii)
- MEM2: 64 MB additional memory (Wii only)
- ARAM: 16 MB audio RAM (optional)
- Locked L1 Cache: 16 KB scratchpad

✅ **Virtual Address Translation**
- Cached mirrors (0x80000000)
- Uncached mirrors (0xC0000000)
- MEM2 mirrors (0x90000000, 0xD0000000)
- Hardware register mapping (0xCC000000)

✅ **Big-Endian Memory Access**
- All read/write functions handle big-endian byte order correctly
- 8-bit, 16-bit, and 32-bit access functions

✅ **GameCube & Wii Support**
- Runtime configuration for GameCube vs Wii mode
- Conditional MEM2 allocation
- Compatible with both architectures

---

## Quick Start

### 1. Include the Header

```c
#include "gecko_memory.h"
```

### 2. Allocate and Initialize Memory

```c
// Allocate memory structure
Gecko_Memory *memory = (Gecko_Memory*)malloc(sizeof(Gecko_Memory));

// Initialize for Wii mode (use 0 for GameCube)
if (gecko_memory_init(memory, 1) != 0) {
    fprintf(stderr, "Failed to initialize memory\n");
    return -1;
}

// Optional: Allocate ARAM
gecko_memory_alloc_aram(memory);
```

### 3. Read/Write Memory

```c
// Write 32-bit value to cached MEM1
gecko_write32(memory, 0x80001000, 0xDEADBEEF);

// Read back from uncached mirror
uint32_t value = gecko_read32(memory, 0xC0001000);

// 16-bit and 8-bit access also available
gecko_write16(memory, 0x80002000, 0x1234);
uint8_t byte = gecko_read8(memory, 0x80002001);
```

### 4. Access Hardware Registers

```c
// Direct struct access
memory->hw_regs->pi.interrupt_mask = PI_INT_VI | PI_INT_CP;
memory->hw_regs->vi.top_field_base_l = 0x01C00000;

// Or through memory read/write functions
gecko_write32(memory, 0xCC003004, 0x00000001);
```

### 5. Cleanup

```c
gecko_memory_free(memory);
free(memory);
```

---

## Memory Map Reference

### Virtual Address Mirrors

| Virtual Address Range | Mirror Of | Cached | Description |
|----------------------|-----------|--------|-------------|
| `0x00000000-0x017FFFFF` | - | Yes | MEM1 Physical |
| `0x80000000-0x817FFFFF` | MEM1 | Yes | MEM1 Cached (common) |
| `0xC0000000-0xC17FFFFF` | MEM1 | No | MEM1 Uncached |
| `0x90000000-0x93FFFFFF` | MEM2 | Yes | MEM2 Cached (Wii) |
| `0xD0000000-0xD3FFFFFF` | MEM2 | No | MEM2 Uncached (Wii) |
| `0xCC000000-0xCC008FFF` | I/O | No | Hardware Registers |

### Hardware Register Map

| Base Address | Component | Struct Field |
|-------------|-----------|--------------|
| `0xCC000000` | Command Processor | `hw_regs->cp` |
| `0xCC001000` | Pixel Engine | `hw_regs->pe` |
| `0xCC002000` | Video Interface | `hw_regs->vi` |
| `0xCC003000` | Processor Interface | `hw_regs->pi` |
| `0xCC004000` | Memory Interface | `hw_regs->mi` |
| `0xCC005000` | DSP Interface | `hw_regs->dsp` |
| `0xCC006000` | DVD Interface | `hw_regs->di` |
| `0xCC006400` | Serial Interface | `hw_regs->si` |
| `0xCC006800` | External Interface | `hw_regs->exi` |
| `0xCC006C00` | Audio Interface | `hw_regs->ai` |
| `0xCC008000` | Graphics FIFO | `hw_regs->gx_fifo` |

---

## API Reference

### Initialization Functions

#### `gecko_memory_init(Gecko_Memory *mem, int is_wii)`
Initializes the memory structure.
- **Parameters:**
  - `mem`: Pointer to memory structure
  - `is_wii`: 1 for Wii mode, 0 for GameCube mode
- **Returns:** 0 on success, -1 on failure

#### `gecko_memory_alloc_aram(Gecko_Memory *mem)`
Allocates ARAM (Audio RAM).
- **Parameters:**
  - `mem`: Pointer to memory structure
- **Returns:** 0 on success, -1 on failure

#### `gecko_memory_free(Gecko_Memory *mem)`
Frees all allocated memory.
- **Parameters:**
  - `mem`: Pointer to memory structure

### Memory Access Functions

#### `gecko_translate_address(uint32_t vaddr)`
Translates virtual address to physical address.
- **Parameters:**
  - `vaddr`: Virtual (effective) address
- **Returns:** Physical address

#### `gecko_read8(const Gecko_Memory *mem, uint32_t vaddr)`
Reads 8-bit value from memory.
- **Parameters:**
  - `mem`: Pointer to memory structure
  - `vaddr`: Virtual address
- **Returns:** 8-bit value

#### `gecko_write8(Gecko_Memory *mem, uint32_t vaddr, uint8_t value)`
Writes 8-bit value to memory.
- **Parameters:**
  - `mem`: Pointer to memory structure
  - `vaddr`: Virtual address
  - `value`: Value to write

#### `gecko_read16(const Gecko_Memory *mem, uint32_t vaddr)`
Reads 16-bit value (big-endian).
- **Parameters:**
  - `mem`: Pointer to memory structure
  - `vaddr`: Virtual address
- **Returns:** 16-bit value

#### `gecko_write16(Gecko_Memory *mem, uint32_t vaddr, uint16_t value)`
Writes 16-bit value (big-endian).
- **Parameters:**
  - `mem`: Pointer to memory structure
  - `vaddr`: Virtual address
  - `value`: Value to write

#### `gecko_read32(const Gecko_Memory *mem, uint32_t vaddr)`
Reads 32-bit value (big-endian).
- **Parameters:**
  - `mem`: Pointer to memory structure
  - `vaddr`: Virtual address
- **Returns:** 32-bit value

#### `gecko_write32(Gecko_Memory *mem, uint32_t vaddr, uint32_t value)`
Writes 32-bit value (big-endian).
- **Parameters:**
  - `mem`: Pointer to memory structure
  - `vaddr`: Virtual address
  - `value`: Value to write

#### `gecko_get_pointer(Gecko_Memory *mem, uint32_t vaddr)`
Gets direct pointer to memory region (use with caution).
- **Parameters:**
  - `mem`: Pointer to memory structure
  - `vaddr`: Virtual address
- **Returns:** Pointer to memory or NULL if invalid

---

## Hardware Register Examples

### Setting Up Graphics FIFO

```c
// Set FIFO base and end addresses
uint32_t fifo_base = 0x80100000;
uint32_t fifo_end = 0x80120000;

memory->hw_regs->cp.fifo_base_lo = fifo_base & 0xFFFF;
memory->hw_regs->cp.fifo_base_hi = (fifo_base >> 16) & 0xFFFF;
memory->hw_regs->cp.fifo_end_lo = fifo_end & 0xFFFF;
memory->hw_regs->cp.fifo_end_hi = (fifo_end >> 16) & 0xFFFF;

// Set watermarks
memory->hw_regs->cp.fifo_hiwmark_lo = 0xE000 & 0xFFFF;
memory->hw_regs->cp.fifo_hiwmark_hi = (0xE000 >> 16) & 0xFFFF;
```

### Configuring Video Interface

```c
// Set framebuffer address
memory->hw_regs->vi.top_field_base_l = 0x01C00000;

// Set display configuration
memory->hw_regs->vi.display_cfg = 0x011E;  // Example config

// Set vertical timing
memory->hw_regs->vi.vertical_timing = 0x1E0F;
```

### Enabling Interrupts

```c
// Enable VI, PE, and CP interrupts
memory->hw_regs->pi.interrupt_mask = 
    PI_INT_VI | 
    PI_INT_PE_TOKEN | 
    PI_INT_PE_FINISH | 
    PI_INT_CP;

// Check interrupt cause
if (memory->hw_regs->pi.interrupt_cause & PI_INT_VI) {
    // Handle VI interrupt
}
```

### Serial Interface (Controllers)

```c
// Check controller data on port 0
uint32_t buttons_high = memory->hw_regs->si.c0_inbuf_h;
uint32_t buttons_low = memory->hw_regs->si.c0_inbuf_l;

// Send command to controller
memory->hw_regs->si.c0_outbuf = 0x00400300;  // Example command

// Check if transfer complete
if (memory->hw_regs->si.comcsr & 0x00000001) {
    // Transfer complete
}
```

### Audio Interface

```c
// Enable audio
memory->hw_regs->ai.control = 0x00000001;

// Set volume (0-255)
memory->hw_regs->ai.volume = 0xFF;

// Set sample rate
memory->hw_regs->ai.sample_counter = 48000;

// Configure interrupt timing
memory->hw_regs->ai.interrupt_timing = 0x1000;
```

---

## Important Notes

### Big-Endian Architecture
The GameCube and Wii use **big-endian** byte order. All read/write functions handle this automatically:
```c
gecko_write32(memory, 0x80000000, 0x12345678);
// Memory layout: [0x12] [0x34] [0x56] [0x78]
```

### Address Mirrors
Multiple virtual addresses can map to the same physical memory:
```c
// All these point to the same physical location
gecko_write32(memory, 0x00001000, 0xAABBCCDD);  // Physical
gecko_write32(memory, 0x80001000, 0xAABBCCDD);  // Cached
gecko_write32(memory, 0xC0001000, 0xAABBCCDD);  // Uncached
```

### Hardware Register Access
Hardware registers can be accessed either:
1. **Directly via struct** (recommended for emulator internal code):
   ```c
   memory->hw_regs->vi.top_field_base_l = 0x01C00000;
   ```

2. **Through memory functions** (for emulating CPU reads/writes):
   ```c
   gecko_write32(memory, 0xCC002010, 0x01C00000);
   ```

### Memory Safety
The `gecko_get_pointer()` function bypasses memory protection and should be used carefully:
```c
// Safe for known-good addresses
char *str = (char*)gecko_get_pointer(memory, 0x80000100);
if (str) {
    strcpy(str, "Hello");
}
```

---

## Integration with Emulator

### CPU Memory Operations
```c
// In your CPU emulator's load/store handlers:
uint32_t cpu_load_word(uint32_t address) {
    return gecko_read32(g_memory, address);
}

void cpu_store_word(uint32_t address, uint32_t value) {
    gecko_write32(g_memory, address, value);
}
```

### Hardware Register Updates
```c
// In your VI timing code:
void vi_update(void) {
    // Trigger VI interrupt
    memory->hw_regs->pi.interrupt_cause |= PI_INT_VI;
    
    // Update display latch
    memory->hw_regs->vi.display_latch0++;
}
```

### DMA Operations
```c
// Example: EXI DMA transfer
void exi_dma_transfer(int channel) {
    EXI_Channel *exi = &memory->hw_regs->exi.channel[channel];
    
    uint32_t mem_addr = exi->mar;
    uint32_t length = exi->length;
    
    // Transfer data...
    for (uint32_t i = 0; i < length; i++) {
        uint8_t data = read_exi_device(channel);
        gecko_write8(memory, mem_addr + i, data);
    }
}
```

---

## Building the Example

### Compile the example program:
```bash
gcc -o gecko_memory_example gecko_memory_example.c -std=c99 -Wall
./gecko_memory_example
```

### Expected output:
```
Gecko/Broadway Memory Emulation Example
========================================

Initializing memory (Wii mode)...
Memory initialized successfully!
  MEM1 size: 24 MB
  MEM2 size: 64 MB (Wii)
  MEM2 enabled: Yes
...
```

---

## Memory Layout Diagram

```
Virtual Address Space (32-bit):

0x00000000 ┌─────────────────────────┐
           │ MEM1 (Physical)         │ 24 MB
0x017FFFFF └─────────────────────────┘
           │                         │
0x0C000000 ┌─────────────────────────┐
           │ Hardware Registers      │ 16 KB
0x0C004000 └─────────────────────────┘
           │                         │
0x10000000 ┌─────────────────────────┐
           │ MEM2 (Physical - Wii)   │ 64 MB
0x13FFFFFF └─────────────────────────┘
           │                         │
0x80000000 ┌─────────────────────────┐
           │ MEM1 Mirror (Cached)    │ 24 MB  ◄─ Most common
0x817FFFFF └─────────────────────────┘
           │                         │
0x90000000 ┌─────────────────────────┐
           │ MEM2 Mirror (Cached)    │ 64 MB (Wii)
0x93FFFFFF └─────────────────────────┘
           │                         │
0xC0000000 ┌─────────────────────────┐
           │ MEM1 Mirror (Uncached)  │ 24 MB
0xC17FFFFF └─────────────────────────┘
           │                         │
0xCC000000 ┌─────────────────────────┐
           │ Hardware Regs (Virtual) │ 16 KB  ◄─ Memory-mapped I/O
0xCC008FFF └─────────────────────────┘
           │                         │
0xD0000000 ┌─────────────────────────┐
           │ MEM2 Mirror (Uncached)  │ 64 MB (Wii)
0xD3FFFFFF └─────────────────────────┘
           │                         │
0xE0000000 ┌─────────────────────────┐
           │ Locked L1 Cache         │ 16 KB
0xE0004000 └─────────────────────────┘
```

---

## References

- `Gecko_Broadway_CPU_Architecture.md` - Complete CPU architecture documentation
- `Gecko_Broadway_CPU_Instruction_Set.md` - Full instruction set reference
- IBM Gekko RISC Microprocessor User's Manual
- Yet Another GameCube Documentation (YAGCD)
- WiiBrew.org Hardware Documentation

---

## License

This code is provided for educational and emulation development purposes.

---

**Last Updated:** November 3, 2025

