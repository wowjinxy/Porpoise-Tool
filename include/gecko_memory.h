/**
 * @file gecko_memory.h
 * @brief Gekko/Broadway CPU Memory Structure for Emulation
 * 
 * This header defines the complete memory layout for GameCube (Gekko) and Wii (Broadway)
 * emulation, including main RAM, memory-mapped I/O registers, and hardware components.
 * 
 * Memory is structured for simplicity and ease of use:
 * - Flags are represented as bool types
 * - Dynamic pointers instead of hardware address registers
 * - No padding for hardware alignment
 * - Support for both GameCube and Wii configurations
 */

#ifndef GECKO_MEMORY_H
#define GECKO_MEMORY_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Memory size constants
#define MEM1_SIZE           (24 * 1024 * 1024)  // 24 MB - GameCube/Wii main memory
#define MEM2_SIZE           (64 * 1024 * 1024)  // 64 MB - Wii additional memory
#define L2_CACHE_SIZE       (256 * 1024)        // 256 KB - L2 cache
#define ARAM_SIZE           (16 * 1024 * 1024)  // 16 MB - Audio RAM (optional)

// Base addresses for virtual memory mirrors
#define PHYS_BASE           0x00000000  // Physical addresses
#define CACHED_BASE         0x80000000  // Cached mirror
#define UNCACHED_BASE       0xC0000000  // Uncached mirror
#define LOCKED_CACHE_BASE   0xE0000000  // Locked L1 cache

// Hardware register base addresses
#define HWREG_BASE          0x0C000000  // Physical hardware registers
#define HWREG_VIRTUAL       0xCC000000  // Virtual (uncached) hardware registers

//==============================================================================
// COMMAND PROCESSOR (CP) REGISTERS
//==============================================================================
typedef struct {
    // Control flags
    bool enabled;                   // CP enabled
    bool read_enabled;              // FIFO read enabled
    bool breakpoint_enabled;        // Breakpoint enabled
    bool overflow_int_enabled;      // Overflow interrupt enabled
    bool underflow_int_enabled;     // Underflow interrupt enabled
    
    // Status flags
    bool fifo_overflow;             // FIFO overflow occurred
    bool fifo_underflow;            // FIFO underflow occurred
    bool fifo_ready;                // FIFO ready for commands
    bool breakpoint_hit;            // Breakpoint was hit
    
    // Token
    uint16_t token;                 // Current token value
    
    // Bounding box
    uint16_t bbox_left;
    uint16_t bbox_top;
    uint16_t bbox_right;
    uint16_t bbox_bottom;
    
    // FIFO pointers (using direct 32-bit addresses)
    uint32_t *fifo_base;            // FIFO base address
    uint32_t *fifo_end;             // FIFO end address
    uint32_t *fifo_write_ptr;       // Current write position
    uint32_t *fifo_read_ptr;        // Current read position (read-only)
    uint32_t *fifo_breakpoint;      // Breakpoint address
    
    // FIFO watermarks
    uint32_t fifo_high_watermark;   // High watermark for interrupts
    uint32_t fifo_low_watermark;    // Low watermark for interrupts
} CP_Registers;

//==============================================================================
// PIXEL ENGINE (PE) REGISTERS
//==============================================================================
typedef struct {
    // Control/Status flags
    bool z_compare_enabled;         // Z buffer comparison enabled
    bool z_update_enabled;          // Z buffer updates enabled
    bool alpha_compare_enabled;     // Alpha comparison enabled
    bool alpha_update_enabled;      // Alpha updates enabled
    bool finished;                  // Frame rendering finished
    bool token_int_pending;         // Token interrupt pending
    
    // Configuration
    uint16_t z_format;              // Z buffer format
    uint16_t alpha_threshold;       // Alpha test threshold
    uint16_t dst_alpha_value;       // Destination alpha value
    
    // Token
    uint16_t token;                 // Current token value
    
    // Performance counters
    uint32_t perf_counter[4];       // 4 performance counters
} PE_Registers;

//==============================================================================
// VIDEO INTERFACE (VI) REGISTERS
//==============================================================================
typedef struct {
    // Control flags
    bool enabled;                   // VI output enabled
    bool interlaced;                // Interlaced mode
    bool progressive;               // Progressive scan mode
    bool vblank_occurred;           // VBlank occurred flag
    bool hblank_occurred;           // HBlank occurred flag
    
    // Display configuration
    uint16_t display_width;         // Display width in pixels
    uint16_t display_height;        // Display height in pixels
    uint16_t display_stride;        // Framebuffer stride
    
    // Framebuffer pointers
    void *top_field_left;           // Top field left eye framebuffer
    void *top_field_right;          // Top field right eye framebuffer (3D)
    void *bottom_field_left;        // Bottom field left eye framebuffer
    void *bottom_field_right;       // Bottom field right eye framebuffer (3D)
    
    // Timing
    uint16_t h_timing0;             // Horizontal timing 0
    uint16_t h_timing1;             // Horizontal timing 1
    uint16_t v_timing_odd;          // Vertical timing odd field
    uint16_t v_timing_even;         // Vertical timing even field
    uint16_t hblank_start;          // HBlank start position
    uint16_t hblank_end;            // HBlank end position
    
    // Display position
    uint16_t display_pos_horz;      // Horizontal display position
    uint16_t display_pos_vert;      // Vertical display position
    
    // Interrupts
    uint16_t display_interrupt[4];  // 4 display interrupt lines
    uint16_t display_latch[2];      // Display latches
    
    // Scaling and filtering
    uint32_t h_scaling;             // Horizontal scaling factor
    uint32_t h_scaling_width;       // Horizontal scaling width
    uint32_t filter_coef[7];        // Anti-aliasing filter coefficients
    
    // Clock
    uint16_t clock;                 // Video clock divider
} VI_Registers;

//==============================================================================
// PROCESSOR INTERFACE (PI) REGISTERS
//==============================================================================
typedef struct {
    // Interrupt flags (pending)
    bool int_cp;                    // Command Processor interrupt
    bool int_pe_token;              // Pixel Engine token interrupt
    bool int_pe_finish;             // Pixel Engine finish interrupt
    bool int_si;                    // Serial Interface interrupt
    bool int_exi;                   // External Interface interrupt
    bool int_ai;                    // Audio Interface interrupt
    bool int_dsp;                   // DSP interrupt
    bool int_mi;                    // Memory Interface interrupt
    bool int_vi;                    // Video Interface interrupt
    bool int_pi_error;              // PI error interrupt
    bool int_rsw;                   // Reset switch interrupt (Wii)
    bool int_di;                    // DVD Interface interrupt
    bool int_hsp;                   // HSP interrupt (Wii)
    bool int_debug;                 // Debug interrupt
    bool int_ipc;                   // IPC interrupt (Wii)
    
    // Interrupt masks (enabled)
    bool mask_cp;                   // CP interrupt enabled
    bool mask_pe_token;             // PE token interrupt enabled
    bool mask_pe_finish;            // PE finish interrupt enabled
    bool mask_si;                   // SI interrupt enabled
    bool mask_exi;                  // EXI interrupt enabled
    bool mask_ai;                   // AI interrupt enabled
    bool mask_dsp;                  // DSP interrupt enabled
    bool mask_mi;                   // MI interrupt enabled
    bool mask_vi;                   // VI interrupt enabled
    bool mask_pi_error;             // PI error interrupt enabled
    bool mask_rsw;                  // Reset switch interrupt enabled (Wii)
    bool mask_di;                   // DI interrupt enabled
    bool mask_hsp;                  // HSP interrupt enabled (Wii)
    bool mask_debug;                // Debug interrupt enabled
    bool mask_ipc;                  // IPC interrupt enabled (Wii)
    
    // Hardware info
    uint32_t flipper_revision;      // Flipper/Hollywood chip revision
} PI_Registers;

//==============================================================================
// MEMORY INTERFACE (MI) REGISTERS
//==============================================================================
typedef struct {
    // MEM1 configuration
    uint32_t mem1_size;             // MEM1 size (typically 24 MB)
    bool mem1_enabled;              // MEM1 enabled
    
    // MEM2 configuration (Wii only)
    uint32_t mem2_size;             // MEM2 size (64 MB on Wii)
    bool mem2_enabled;              // MEM2 enabled
    
    // Memory protection
    bool protection_enabled;        // Memory protection enabled
    uint32_t protection_start;      // Protection range start
    uint32_t protection_end;        // Protection range end
    
    // Timing
    uint32_t timer;                 // Memory interface timer
} MI_Registers;

//==============================================================================
// DSP INTERFACE REGISTERS
//==============================================================================
typedef struct {
    // Control/Status flags
    bool dsp_running;               // DSP is running
    bool dsp_reset;                 // DSP in reset
    bool dsp_int_pending;           // DSP interrupt pending
    bool aram_dma_active;           // ARAM DMA transfer active
    bool ai_dma_active;             // Audio DMA active
    
    // Mailboxes
    uint32_t cpu_to_dsp_mbox;       // CPU to DSP mailbox
    uint32_t dsp_to_cpu_mbox;       // DSP to CPU mailbox
    
    // ARAM DMA
    void *aram_dma_mem_addr;        // Main memory address for ARAM DMA
    void *aram_dma_aram_addr;       // ARAM address for DMA
    uint32_t aram_dma_length;       // ARAM DMA transfer length
    
    // Audio Interface DMA
    uint32_t ai_dma_control;        // AI DMA control
    uint32_t ai_volume;             // AI volume
    uint32_t ai_sample_count;       // AI sample counter
    uint32_t ai_interrupt_timing;   // AI interrupt timing
} DSP_Registers;

//==============================================================================
// DVD INTERFACE (DI) REGISTERS
//==============================================================================
typedef struct {
    // Status flags
    bool drive_ready;               // Drive is ready
    bool disc_present;              // Disc inserted
    bool cover_open;                // Drive cover is open
    bool transfer_complete;         // DMA transfer complete
    bool error;                     // Drive error occurred
    bool motor_running;             // Disc motor running
    
    // Command buffers
    uint32_t command_buffer[3];     // Command buffers
    
    // DMA
    void *dma_address;              // DMA memory address
    uint32_t dma_length;            // DMA transfer length
    
    // Immediate data buffer
    uint32_t immediate_buffer;      // Immediate data from drive
    
    // Configuration
    uint32_t config;                // Drive configuration
} DI_Registers;

//==============================================================================
// SERIAL INTERFACE (SI) REGISTERS
//==============================================================================
typedef struct {
    // Controller port data (4 ports)
    uint32_t port_out[4];           // Output data to controllers
    uint64_t port_in[4];            // Input data from controllers (8 bytes each)
    
    // Status flags
    bool transfer_active;           // Transfer in progress
    bool transfer_complete;         // Transfer completed
    bool error;                     // Communication error
    
    // Poll configuration
    uint32_t poll_rate;             // Controller polling rate
} SI_Registers;

//==============================================================================
// EXTERNAL INTERFACE (EXI) REGISTERS
//==============================================================================
typedef struct {
    // Control/Status flags
    bool dma_active;                // DMA transfer active
    bool transfer_complete;         // Transfer complete
    bool interrupt_pending;         // Interrupt pending
    
    // DMA
    void *dma_address;              // DMA memory address
    uint32_t dma_length;            // DMA transfer length
    
    // Immediate data
    uint32_t data;                  // Immediate data register
    
    // Device select
    uint8_t device_select;          // Selected device (0-2)
} EXI_Channel;

typedef struct {
    EXI_Channel channel[3];         // 3 EXI channels
} EXI_Registers;

//==============================================================================
// AUDIO INTERFACE (AI) REGISTERS
//==============================================================================
typedef struct {
    // Control flags
    bool enabled;                   // Audio output enabled
    bool interrupt_pending;         // Sample interrupt pending
    
    // Configuration
    uint32_t sample_rate;           // Sample rate (Hz)
    uint32_t volume_left;           // Left channel volume (0-255)
    uint32_t volume_right;          // Right channel volume (0-255)
    
    // Sample counter
    uint32_t sample_counter;        // Running sample counter
    
    // Interrupt timing
    uint32_t interrupt_timing;      // Samples per interrupt
} AI_Registers;

//==============================================================================
// HARDWARE REGISTER BLOCK (Simplified, no padding)
//==============================================================================
typedef struct {
    CP_Registers cp;                // Command Processor
    PE_Registers pe;                // Pixel Engine
    VI_Registers vi;                // Video Interface
    PI_Registers pi;                // Processor Interface
    MI_Registers mi;                // Memory Interface
    DSP_Registers dsp;              // DSP Interface
    DI_Registers di;                // DVD Interface
    SI_Registers si;                // Serial Interface (Controllers)
    EXI_Registers exi;              // External Interface (Memory Cards, etc.)
    AI_Registers ai;                // Audio Interface
} HW_Registers;

//==============================================================================
// MAIN MEMORY STRUCTURE
//==============================================================================
typedef struct {
    // Main Memory 1 (24 MB - GameCube/Wii compatible)
    uint8_t mem1[MEM1_SIZE];
    
    // Main Memory 2 (64 MB - Wii only, NULL for GameCube)
    uint8_t *mem2;
    
    // Audio RAM (16 MB - used by DSP)
    uint8_t *aram;
    
    // Hardware Registers (memory-mapped I/O)
    HW_Registers hw_regs;           // Direct struct instead of pointer (simplified)
    
    // Locked L1 Cache (can be used as scratchpad - 16 KB)
    uint8_t locked_cache[16 * 1024];
    
    // Configuration flags
    bool is_wii;                    // true = Wii mode, false = GameCube mode
    bool mem2_enabled;              // true = MEM2 allocated and available
    bool aram_enabled;              // true = ARAM allocated
} Gecko_Memory;

//==============================================================================
// MEMORY ACCESS HELPER FUNCTIONS
//==============================================================================

/**
 * Initialize memory structure
 * @param mem Pointer to memory structure
 * @param is_wii true for Wii mode, false for GameCube mode
 * @return 0 on success, -1 on failure
 */
static inline int gecko_memory_init(Gecko_Memory *mem, bool is_wii) {
    if (!mem) return -1;
    
    // Clear MEM1
    memset(mem->mem1, 0, MEM1_SIZE);
    
    // Set configuration
    mem->is_wii = is_wii;
    mem->mem2_enabled = false;
    mem->aram_enabled = false;
    
    // Clear hardware registers
    memset(&mem->hw_regs, 0, sizeof(HW_Registers));
    
    // Allocate MEM2 if Wii mode
    if (is_wii) {
        mem->mem2 = (uint8_t*)malloc(MEM2_SIZE);
        if (mem->mem2) {
            memset(mem->mem2, 0, MEM2_SIZE);
            mem->mem2_enabled = true;
        }
    } else {
        mem->mem2 = NULL;
    }
    
    // ARAM allocation (optional, can be done later)
    mem->aram = NULL;
    
    // Clear locked cache
    memset(mem->locked_cache, 0, sizeof(mem->locked_cache));
    
    return 0;
}

/**
 * Allocate ARAM
 * @param mem Pointer to memory structure
 * @return 0 on success, -1 on failure
 */
static inline int gecko_memory_alloc_aram(Gecko_Memory *mem) {
    if (!mem || mem->aram_enabled) return -1;
    
    mem->aram = (uint8_t*)malloc(ARAM_SIZE);
    if (!mem->aram) return -1;
    
    memset(mem->aram, 0, ARAM_SIZE);
    mem->aram_enabled = true;
    return 0;
}

/**
 * Free memory structure
 * @param mem Pointer to memory structure
 */
static inline void gecko_memory_free(Gecko_Memory *mem) {
    if (!mem) return;
    
    if (mem->mem2) {
        free(mem->mem2);
        mem->mem2 = NULL;
        mem->mem2_enabled = false;
    }
    
    if (mem->aram) {
        free(mem->aram);
        mem->aram = NULL;
        mem->aram_enabled = false;
    }
}

/**
 * Translate virtual address to physical address
 * @param vaddr Virtual address (effective address)
 * @return Physical address
 */
static inline uint32_t gecko_translate_address(uint32_t vaddr) {
    // Handle common virtual address mirrors
    if (vaddr >= 0x80000000 && vaddr < 0x81800000) {
        // Cached mirror of MEM1
        return vaddr & 0x01FFFFFF;
    } else if (vaddr >= 0xC0000000 && vaddr < 0xC1800000) {
        // Uncached mirror of MEM1
        return vaddr & 0x01FFFFFF;
    } else if (vaddr >= 0x90000000 && vaddr < 0x94000000) {
        // Cached mirror of MEM2 (Wii)
        return vaddr & 0x03FFFFFF;
    } else if (vaddr >= 0xD0000000 && vaddr < 0xD4000000) {
        // Uncached mirror of MEM2 (Wii)
        return vaddr & 0x03FFFFFF;
    } else if (vaddr >= 0xCC000000 && vaddr < 0xCD000000) {
        // Hardware registers (uncached)
        return (vaddr & 0x00FFFFFF) | 0x0C000000;
    }
    
    // Already physical address or unmapped
    return vaddr;
}

/**
 * Read 8-bit value from memory
 * @param mem Pointer to memory structure
 * @param vaddr Virtual address
 * @return 8-bit value
 */
static inline uint8_t gecko_read8(const Gecko_Memory *mem, uint32_t vaddr) {
    uint32_t paddr = gecko_translate_address(vaddr);
    
    // MEM1
    if (paddr < MEM1_SIZE) {
        return mem->mem1[paddr];
    }
    
    // MEM2 (Wii)
    if (mem->mem2_enabled && paddr >= 0x10000000 && paddr < 0x14000000) {
        return mem->mem2[paddr - 0x10000000];
    }
    
    // Hardware registers
    if (paddr >= 0x0C000000 && paddr < 0x0D000000) {
        uint32_t offset = paddr - 0x0C000000;
        return ((const uint8_t*)&mem->hw_regs)[offset];
    }
    
    // Unmapped
    return 0xFF;
}

/**
 * Write 8-bit value to memory
 * @param mem Pointer to memory structure
 * @param vaddr Virtual address
 * @param value 8-bit value to write
 */
static inline void gecko_write8(Gecko_Memory *mem, uint32_t vaddr, uint8_t value) {
    uint32_t paddr = gecko_translate_address(vaddr);
    
    // MEM1
    if (paddr < MEM1_SIZE) {
        mem->mem1[paddr] = value;
        return;
    }
    
    // MEM2 (Wii)
    if (mem->mem2_enabled && paddr >= 0x10000000 && paddr < 0x14000000) {
        mem->mem2[paddr - 0x10000000] = value;
        return;
    }
    
    // Hardware registers
    if (paddr >= 0x0C000000 && paddr < 0x0D000000) {
        uint32_t offset = paddr - 0x0C000000;
        ((uint8_t*)&mem->hw_regs)[offset] = value;
        return;
    }
}

/**
 * Read 16-bit value from memory (big-endian)
 * @param mem Pointer to memory structure
 * @param vaddr Virtual address
 * @return 16-bit value
 */
static inline uint16_t gecko_read16(const Gecko_Memory *mem, uint32_t vaddr) {
    uint8_t b0 = gecko_read8(mem, vaddr);
    uint8_t b1 = gecko_read8(mem, vaddr + 1);
    return ((uint16_t)b0 << 8) | b1;
}

/**
 * Write 16-bit value to memory (big-endian)
 * @param mem Pointer to memory structure
 * @param vaddr Virtual address
 * @param value 16-bit value to write
 */
static inline void gecko_write16(Gecko_Memory *mem, uint32_t vaddr, uint16_t value) {
    gecko_write8(mem, vaddr, (value >> 8) & 0xFF);
    gecko_write8(mem, vaddr + 1, value & 0xFF);
}

/**
 * Read 32-bit value from memory (big-endian)
 * @param mem Pointer to memory structure
 * @param vaddr Virtual address
 * @return 32-bit value
 */
static inline uint32_t gecko_read32(const Gecko_Memory *mem, uint32_t vaddr) {
    uint8_t b0 = gecko_read8(mem, vaddr);
    uint8_t b1 = gecko_read8(mem, vaddr + 1);
    uint8_t b2 = gecko_read8(mem, vaddr + 2);
    uint8_t b3 = gecko_read8(mem, vaddr + 3);
    return ((uint32_t)b0 << 24) | ((uint32_t)b1 << 16) | ((uint32_t)b2 << 8) | b3;
}

/**
 * Write 32-bit value to memory (big-endian)
 * @param mem Pointer to memory structure
 * @param vaddr Virtual address
 * @param value 32-bit value to write
 */
static inline void gecko_write32(Gecko_Memory *mem, uint32_t vaddr, uint32_t value) {
    gecko_write8(mem, vaddr, (value >> 24) & 0xFF);
    gecko_write8(mem, vaddr + 1, (value >> 16) & 0xFF);
    gecko_write8(mem, vaddr + 2, (value >> 8) & 0xFF);
    gecko_write8(mem, vaddr + 3, value & 0xFF);
}

/**
 * Get pointer to memory region (for direct access)
 * WARNING: Use with caution, bypasses memory protection
 * @param mem Pointer to memory structure
 * @param vaddr Virtual address
 * @return Pointer to memory or NULL if invalid
 */
static inline void* gecko_get_pointer(Gecko_Memory *mem, uint32_t vaddr) {
    uint32_t paddr = gecko_translate_address(vaddr);
    
    // MEM1
    if (paddr < MEM1_SIZE) {
        return &mem->mem1[paddr];
    }
    
    // MEM2 (Wii)
    if (mem->mem2_enabled && paddr >= 0x10000000 && paddr < 0x14000000) {
        return &mem->mem2[paddr - 0x10000000];
    }
    
    // Hardware registers
    if (paddr >= 0x0C000000 && paddr < 0x0D000000) {
        uint32_t offset = paddr - 0x0C000000;
        return (uint8_t*)&mem->hw_regs + offset;
    }
    
    return NULL;
}

#ifdef __cplusplus
}
#endif

#endif // GECKO_MEMORY_H

