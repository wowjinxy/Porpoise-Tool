/**
 * @file gecko_memory_example.c
 * @brief Example usage of the Gecko memory structure
 * 
 * This file demonstrates how to use the gecko_memory.h structures
 * for emulating GameCube/Wii memory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gecko_memory.h"

int main(void) {
    printf("Gecko/Broadway Memory Emulation Example\n");
    printf("========================================\n\n");
    
    // Allocate memory structure
    Gecko_Memory *memory = (Gecko_Memory*)malloc(sizeof(Gecko_Memory));
    if (!memory) {
        fprintf(stderr, "Failed to allocate memory structure\n");
        return 1;
    }
    
    // Initialize for Wii mode (use 0 for GameCube mode)
    printf("Initializing memory (Wii mode)...\n");
    if (gecko_memory_init(memory, 1) != 0) {
        fprintf(stderr, "Failed to initialize memory\n");
        free(memory);
        return 1;
    }
    
    printf("Memory initialized successfully!\n");
    printf("  MEM1 size: %u MB\n", MEM1_SIZE / (1024 * 1024));
    printf("  MEM2 size: %u MB (Wii)\n", MEM2_SIZE / (1024 * 1024));
    printf("  MEM2 enabled: %s\n", memory->mem2_enabled ? "Yes" : "No");
    printf("\n");
    
    // Allocate ARAM
    printf("Allocating ARAM...\n");
    if (gecko_memory_alloc_aram(memory) == 0) {
        printf("ARAM allocated: %u MB\n\n", ARAM_SIZE / (1024 * 1024));
    }
    
    // Example 1: Writing to MEM1 using different address mirrors
    printf("Example 1: Writing to MEM1\n");
    printf("--------------------------\n");
    
    uint32_t test_addr_phys = 0x80001000;  // Cached virtual address
    uint32_t test_value = 0xDEADBEEF;
    
    printf("Writing 0x%08X to address 0x%08X (cached mirror)...\n", test_value, test_addr_phys);
    gecko_write32(memory, test_addr_phys, test_value);
    
    // Read back using uncached mirror
    uint32_t test_addr_uncached = 0xC0001000;
    uint32_t read_value = gecko_read32(memory, test_addr_uncached);
    printf("Reading from 0x%08X (uncached mirror): 0x%08X\n", test_addr_uncached, read_value);
    printf("Match: %s\n\n", (read_value == test_value) ? "YES" : "NO");
    
    // Example 2: Accessing hardware registers
    printf("Example 2: Hardware Registers\n");
    printf("-----------------------------\n");
    
    // Set PI interrupt mask
    printf("Setting PI interrupt mask...\n");
    memory->hw_regs->pi.interrupt_mask = PI_INT_VI | PI_INT_PE_TOKEN | PI_INT_CP;
    printf("PI interrupt mask: 0x%08X\n", memory->hw_regs->pi.interrupt_mask);
    
    // Check Flipper/Hollywood revision
    memory->hw_regs->pi.flipper_revision = 0x00000100;  // Example revision
    printf("Flipper revision: 0x%08X\n\n", memory->hw_regs->pi.flipper_revision);
    
    // Example 3: Video Interface registers
    printf("Example 3: Video Interface\n");
    printf("-------------------------\n");
    
    // Set up framebuffer address
    uint32_t fb_addr = 0x01C00000;  // Example framebuffer at 28 MB offset
    memory->hw_regs->vi.top_field_base_l = fb_addr;
    memory->hw_regs->vi.bottom_field_base_l = fb_addr + 0x20000;
    
    printf("Top field framebuffer: 0x%08X\n", memory->hw_regs->vi.top_field_base_l);
    printf("Bottom field framebuffer: 0x%08X\n\n", memory->hw_regs->vi.bottom_field_base_l);
    
    // Example 4: Command Processor FIFO
    printf("Example 4: Command Processor FIFO\n");
    printf("---------------------------------\n");
    
    uint32_t fifo_base = 0x80100000;
    uint32_t fifo_end = 0x80120000;
    
    memory->hw_regs->cp.fifo_base_lo = fifo_base & 0xFFFF;
    memory->hw_regs->cp.fifo_base_hi = (fifo_base >> 16) & 0xFFFF;
    memory->hw_regs->cp.fifo_end_lo = fifo_end & 0xFFFF;
    memory->hw_regs->cp.fifo_end_hi = (fifo_end >> 16) & 0xFFFF;
    
    printf("FIFO base: 0x%04X%04X\n", memory->hw_regs->cp.fifo_base_hi, memory->hw_regs->cp.fifo_base_lo);
    printf("FIFO end:  0x%04X%04X\n", memory->hw_regs->cp.fifo_end_hi, memory->hw_regs->cp.fifo_end_lo);
    printf("FIFO size: %u KB\n\n", (fifo_end - fifo_base) / 1024);
    
    // Example 5: Using memory pointers
    printf("Example 5: Direct Memory Access\n");
    printf("-------------------------------\n");
    
    uint32_t string_addr = 0x80002000;
    char *str_ptr = (char*)gecko_get_pointer(memory, string_addr);
    
    if (str_ptr) {
        strcpy(str_ptr, "Hello from Gekko!");
        printf("Wrote string to 0x%08X\n", string_addr);
        
        // Read back using normal read functions
        printf("Reading back: \"");
        for (int i = 0; i < 17; i++) {
            printf("%c", gecko_read8(memory, string_addr + i));
        }
        printf("\"\n\n");
    }
    
    // Example 6: MEM2 access (Wii only)
    if (memory->is_wii && memory->mem2_enabled) {
        printf("Example 6: MEM2 Access (Wii)\n");
        printf("----------------------------\n");
        
        uint32_t mem2_addr = 0x90000000;  // MEM2 cached mirror
        uint32_t mem2_value = 0xCAFEBABE;
        
        gecko_write32(memory, mem2_addr, mem2_value);
        printf("Wrote 0x%08X to MEM2 at 0x%08X\n", mem2_value, mem2_addr);
        
        uint32_t mem2_read = gecko_read32(memory, mem2_addr);
        printf("Read back: 0x%08X\n", mem2_read);
        printf("Match: %s\n\n", (mem2_read == mem2_value) ? "YES" : "NO");
    }
    
    // Example 7: Big-endian byte order verification
    printf("Example 7: Big-Endian Byte Order\n");
    printf("--------------------------------\n");
    
    uint32_t be_addr = 0x80003000;
    gecko_write8(memory, be_addr + 0, 0x12);
    gecko_write8(memory, be_addr + 1, 0x34);
    gecko_write8(memory, be_addr + 2, 0x56);
    gecko_write8(memory, be_addr + 3, 0x78);
    
    uint32_t be_value = gecko_read32(memory, be_addr);
    printf("Wrote bytes: 0x12 0x34 0x56 0x78\n");
    printf("Read as 32-bit: 0x%08X\n", be_value);
    printf("Correct big-endian: %s\n\n", (be_value == 0x12345678) ? "YES" : "NO");
    
    // Example 8: Serial Interface (controller ports)
    printf("Example 8: Serial Interface\n");
    printf("---------------------------\n");
    
    // Simulate controller data on port 0
    memory->hw_regs->si.c0_inbuf_h = 0x12345678;
    memory->hw_regs->si.c0_inbuf_l = 0x9ABCDEF0;
    memory->hw_regs->si.comcsr = 0x00000001;  // Transfer complete
    
    printf("Controller 0 input buffer high: 0x%08X\n", memory->hw_regs->si.c0_inbuf_h);
    printf("Controller 0 input buffer low:  0x%08X\n", memory->hw_regs->si.c0_inbuf_l);
    printf("SI communication status: 0x%08X\n\n", memory->hw_regs->si.comcsr);
    
    // Example 9: Audio Interface
    printf("Example 9: Audio Interface\n");
    printf("-------------------------\n");
    
    memory->hw_regs->ai.control = 0x00000001;  // Enable audio
    memory->hw_regs->ai.volume = 0xFF;         // Max volume
    memory->hw_regs->ai.sample_counter = 48000; // 48 kHz
    
    printf("AI Control: 0x%08X\n", memory->hw_regs->ai.control);
    printf("AI Volume: 0x%08X\n", memory->hw_regs->ai.volume);
    printf("AI Sample Counter: %u Hz\n\n", memory->hw_regs->ai.sample_counter);
    
    // Example 10: Address translation
    printf("Example 10: Address Translation\n");
    printf("-------------------------------\n");
    
    uint32_t test_addresses[] = {
        0x00001000,  // Physical
        0x80001000,  // Cached mirror
        0xC0001000,  // Uncached mirror
        0xCC006400,  // Hardware registers
    };
    
    const char *test_names[] = {
        "Physical MEM1",
        "Cached MEM1",
        "Uncached MEM1",
        "Hardware SI"
    };
    
    for (int i = 0; i < 4; i++) {
        uint32_t physical = gecko_translate_address(test_addresses[i]);
        printf("%s: 0x%08X -> 0x%08X\n", test_names[i], test_addresses[i], physical);
    }
    printf("\n");
    
    // Cleanup
    printf("Cleaning up...\n");
    gecko_memory_free(memory);
    free(memory);
    
    printf("Example completed successfully!\n");
    
    return 0;
}

