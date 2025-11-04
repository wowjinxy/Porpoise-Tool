/**
 * @file gecko_memory_example_simplified.c
 * @brief Example usage of the simplified Gecko memory structure with bool flags
 * 
 * This file demonstrates the new simplified memory structure using:
 * - bool types for all flags
 * - Dynamic pointers instead of split address registers
 * - No hardware padding
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gecko_memory.h"

int main(void) {
    printf("Gecko/Broadway Simplified Memory Structure Example\n");
    printf("==================================================\n\n");
    
    // Allocate memory structure
    Gecko_Memory *memory = (Gecko_Memory*)malloc(sizeof(Gecko_Memory));
    if (!memory) {
        fprintf(stderr, "Failed to allocate memory structure\n");
        return 1;
    }
    
    // Initialize for Wii mode (use false for GameCube mode)
    printf("Initializing memory (Wii mode)...\n");
    if (gecko_memory_init(memory, true) != 0) {
        fprintf(stderr, "Failed to initialize memory\n");
        free(memory);
        return 1;
    }
    
    printf("Memory initialized successfully!\n");
    printf("  MEM1 size: %u MB\n", MEM1_SIZE / (1024 * 1024));
    printf("  Is Wii: %s\n", memory->is_wii ? "true" : "false");
    printf("  MEM2 enabled: %s\n", memory->mem2_enabled ? "true" : "false");
    if (memory->mem2_enabled) {
        printf("  MEM2 size: %u MB\n", MEM2_SIZE / (1024 * 1024));
    }
    printf("\n");
    
    // Example 1: Using bool flags in Processor Interface
    printf("Example 1: Processor Interface (Bool Flags)\n");
    printf("-------------------------------------------\n");
    
    // Enable interrupts using bool flags
    memory->hw_regs.pi.mask_vi = true;          // Enable VI interrupt
    memory->hw_regs.pi.mask_pe_token = true;    // Enable PE token interrupt
    memory->hw_regs.pi.mask_cp = true;          // Enable CP interrupt
    
    printf("Enabled interrupts:\n");
    printf("  VI interrupt: %s\n", memory->hw_regs.pi.mask_vi ? "enabled" : "disabled");
    printf("  PE token interrupt: %s\n", memory->hw_regs.pi.mask_pe_token ? "enabled" : "disabled");
    printf("  CP interrupt: %s\n", memory->hw_regs.pi.mask_cp ? "enabled" : "disabled");
    
    // Simulate an interrupt
    memory->hw_regs.pi.int_vi = true;
    printf("\nVI interrupt pending: %s\n\n", memory->hw_regs.pi.int_vi ? "YES" : "NO");
    
    // Example 2: Video Interface with Direct Pointers
    printf("Example 2: Video Interface (Direct Pointers)\n");
    printf("--------------------------------------------\n");
    
    // Enable video output
    memory->hw_regs.vi.enabled = true;
    memory->hw_regs.vi.progressive = false;
    memory->hw_regs.vi.interlaced = true;
    
    // Set framebuffer pointers directly (no split high/low registers!)
    memory->hw_regs.vi.top_field_left = (void*)0x80100000;
    memory->hw_regs.vi.bottom_field_left = (void*)0x80150000;
    
    // Set display resolution
    memory->hw_regs.vi.display_width = 640;
    memory->hw_regs.vi.display_height = 480;
    
    printf("Video Interface configured:\n");
    printf("  Enabled: %s\n", memory->hw_regs.vi.enabled ? "true" : "false");
    printf("  Progressive: %s\n", memory->hw_regs.vi.progressive ? "true" : "false");
    printf("  Interlaced: %s\n", memory->hw_regs.vi.interlaced ? "true" : "false");
    printf("  Resolution: %ux%u\n", memory->hw_regs.vi.display_width, memory->hw_regs.vi.display_height);
    printf("  Top framebuffer: %p\n", memory->hw_regs.vi.top_field_left);
    printf("  Bottom framebuffer: %p\n\n", memory->hw_regs.vi.bottom_field_left);
    
    // Example 3: Command Processor with FIFO Pointers
    printf("Example 3: Command Processor (FIFO Pointers)\n");
    printf("--------------------------------------------\n");
    
    // Enable CP
    memory->hw_regs.cp.enabled = true;
    memory->hw_regs.cp.read_enabled = true;
    memory->hw_regs.cp.overflow_int_enabled = true;
    
    // Set FIFO pointers directly
    memory->hw_regs.cp.fifo_base = (uint32_t*)0x80200000;
    memory->hw_regs.cp.fifo_end = (uint32_t*)0x80220000;
    memory->hw_regs.cp.fifo_write_ptr = memory->hw_regs.cp.fifo_base;
    memory->hw_regs.cp.fifo_read_ptr = memory->hw_regs.cp.fifo_base;
    
    // Set watermarks
    memory->hw_regs.cp.fifo_high_watermark = 0xE000;
    memory->hw_regs.cp.fifo_low_watermark = 0x4000;
    
    printf("Command Processor configured:\n");
    printf("  Enabled: %s\n", memory->hw_regs.cp.enabled ? "true" : "false");
    printf("  Read enabled: %s\n", memory->hw_regs.cp.read_enabled ? "true" : "false");
    printf("  FIFO base: %p\n", memory->hw_regs.cp.fifo_base);
    printf("  FIFO end: %p\n", memory->hw_regs.cp.fifo_end);
    printf("  FIFO size: %zu KB\n", 
           ((uint8_t*)memory->hw_regs.cp.fifo_end - (uint8_t*)memory->hw_regs.cp.fifo_base) / 1024);
    printf("  High watermark: 0x%X\n", memory->hw_regs.cp.fifo_high_watermark);
    printf("  Low watermark: 0x%X\n\n", memory->hw_regs.cp.fifo_low_watermark);
    
    // Example 4: Pixel Engine with Bool Flags
    printf("Example 4: Pixel Engine (Bool Flags)\n");
    printf("------------------------------------\n");
    
    // Enable Z-buffer and alpha testing
    memory->hw_regs.pe.z_compare_enabled = true;
    memory->hw_regs.pe.z_update_enabled = true;
    memory->hw_regs.pe.alpha_compare_enabled = true;
    memory->hw_regs.pe.alpha_threshold = 128;
    
    printf("Pixel Engine configured:\n");
    printf("  Z compare: %s\n", memory->hw_regs.pe.z_compare_enabled ? "enabled" : "disabled");
    printf("  Z update: %s\n", memory->hw_regs.pe.z_update_enabled ? "enabled" : "disabled");
    printf("  Alpha compare: %s\n", memory->hw_regs.pe.alpha_compare_enabled ? "enabled" : "disabled");
    printf("  Alpha threshold: %u\n\n", memory->hw_regs.pe.alpha_threshold);
    
    // Example 5: DVD Interface with Bool Flags
    printf("Example 5: DVD Interface (Bool Flags)\n");
    printf("-------------------------------------\n");
    
    // Simulate disc state
    memory->hw_regs.di.drive_ready = true;
    memory->hw_regs.di.disc_present = true;
    memory->hw_regs.di.cover_open = false;
    memory->hw_regs.di.motor_running = true;
    
    // Set DMA pointer directly
    memory->hw_regs.di.dma_address = (void*)0x80300000;
    memory->hw_regs.di.dma_length = 2048;
    
    printf("DVD Interface status:\n");
    printf("  Drive ready: %s\n", memory->hw_regs.di.drive_ready ? "YES" : "NO");
    printf("  Disc present: %s\n", memory->hw_regs.di.disc_present ? "YES" : "NO");
    printf("  Cover open: %s\n", memory->hw_regs.di.cover_open ? "YES" : "NO");
    printf("  Motor running: %s\n", memory->hw_regs.di.motor_running ? "YES" : "NO");
    printf("  DMA address: %p\n", memory->hw_regs.di.dma_address);
    printf("  DMA length: %u bytes\n\n", memory->hw_regs.di.dma_length);
    
    // Example 6: Serial Interface (Controllers)
    printf("Example 6: Serial Interface (Controllers)\n");
    printf("-----------------------------------------\n");
    
    // Simulate controller data on port 0
    memory->hw_regs.si.port_in[0] = 0x123456789ABCDEF0ULL;
    memory->hw_regs.si.transfer_complete = true;
    memory->hw_regs.si.error = false;
    
    printf("Controller port 0:\n");
    printf("  Input data: 0x%016llX\n", (unsigned long long)memory->hw_regs.si.port_in[0]);
    printf("  Transfer complete: %s\n", memory->hw_regs.si.transfer_complete ? "YES" : "NO");
    printf("  Error: %s\n\n", memory->hw_regs.si.error ? "YES" : "NO");
    
    // Example 7: Audio Interface
    printf("Example 7: Audio Interface (Bool Flags)\n");
    printf("---------------------------------------\n");
    
    memory->hw_regs.ai.enabled = true;
    memory->hw_regs.ai.sample_rate = 48000;
    memory->hw_regs.ai.volume_left = 255;
    memory->hw_regs.ai.volume_right = 255;
    memory->hw_regs.ai.interrupt_timing = 1024;
    
    printf("Audio Interface configured:\n");
    printf("  Enabled: %s\n", memory->hw_regs.ai.enabled ? "true" : "false");
    printf("  Sample rate: %u Hz\n", memory->hw_regs.ai.sample_rate);
    printf("  Volume L/R: %u/%u\n", memory->hw_regs.ai.volume_left, memory->hw_regs.ai.volume_right);
    printf("  Interrupt timing: %u samples\n\n", memory->hw_regs.ai.interrupt_timing);
    
    // Example 8: DSP Interface
    printf("Example 8: DSP Interface (Bool Flags)\n");
    printf("-------------------------------------\n");
    
    memory->hw_regs.dsp.dsp_running = true;
    memory->hw_regs.dsp.dsp_reset = false;
    memory->hw_regs.dsp.aram_dma_active = false;
    
    // Set ARAM DMA pointers directly
    memory->hw_regs.dsp.aram_dma_mem_addr = (void*)0x80400000;
    memory->hw_regs.dsp.aram_dma_aram_addr = (void*)0x00000000;
    memory->hw_regs.dsp.aram_dma_length = 65536;
    
    printf("DSP Interface status:\n");
    printf("  DSP running: %s\n", memory->hw_regs.dsp.dsp_running ? "true" : "false");
    printf("  DSP reset: %s\n", memory->hw_regs.dsp.dsp_reset ? "true" : "false");
    printf("  ARAM DMA active: %s\n", memory->hw_regs.dsp.aram_dma_active ? "true" : "false");
    printf("  ARAM DMA mem addr: %p\n", memory->hw_regs.dsp.aram_dma_mem_addr);
    printf("  ARAM DMA aram addr: %p\n", memory->hw_regs.dsp.aram_dma_aram_addr);
    printf("  ARAM DMA length: %u bytes\n\n", memory->hw_regs.dsp.aram_dma_length);
    
    // Example 9: Memory Interface
    printf("Example 9: Memory Interface (Bool Flags)\n");
    printf("----------------------------------------\n");
    
    memory->hw_regs.mi.mem1_enabled = true;
    memory->hw_regs.mi.mem1_size = 24 * 1024 * 1024;
    memory->hw_regs.mi.mem2_enabled = memory->is_wii;
    if (memory->is_wii) {
        memory->hw_regs.mi.mem2_size = 64 * 1024 * 1024;
    }
    memory->hw_regs.mi.protection_enabled = false;
    
    printf("Memory Interface status:\n");
    printf("  MEM1 enabled: %s\n", memory->hw_regs.mi.mem1_enabled ? "true" : "false");
    printf("  MEM1 size: %u MB\n", memory->hw_regs.mi.mem1_size / (1024 * 1024));
    printf("  MEM2 enabled: %s\n", memory->hw_regs.mi.mem2_enabled ? "true" : "false");
    if (memory->hw_regs.mi.mem2_enabled) {
        printf("  MEM2 size: %u MB\n", memory->hw_regs.mi.mem2_size / (1024 * 1024));
    }
    printf("  Protection enabled: %s\n\n", memory->hw_regs.mi.protection_enabled ? "true" : "false");
    
    // Example 10: Interrupt handling demonstration
    printf("Example 10: Interrupt Handling\n");
    printf("------------------------------\n");
    
    // Simulate various interrupts
    memory->hw_regs.pi.int_vi = true;
    memory->hw_regs.pi.int_pe_finish = true;
    memory->hw_regs.pi.int_di = true;
    
    printf("Pending interrupts:\n");
    if (memory->hw_regs.pi.int_cp) printf("  - Command Processor\n");
    if (memory->hw_regs.pi.int_pe_token) printf("  - PE Token\n");
    if (memory->hw_regs.pi.int_pe_finish) printf("  - PE Finish\n");
    if (memory->hw_regs.pi.int_si) printf("  - Serial Interface\n");
    if (memory->hw_regs.pi.int_exi) printf("  - External Interface\n");
    if (memory->hw_regs.pi.int_ai) printf("  - Audio Interface\n");
    if (memory->hw_regs.pi.int_dsp) printf("  - DSP\n");
    if (memory->hw_regs.pi.int_mi) printf("  - Memory Interface\n");
    if (memory->hw_regs.pi.int_vi) printf("  - Video Interface\n");
    if (memory->hw_regs.pi.int_di) printf("  - DVD Interface\n");
    
    // Check which interrupts would fire (pending AND enabled)
    printf("\nInterrupts that would fire:\n");
    if (memory->hw_regs.pi.int_vi && memory->hw_regs.pi.mask_vi) 
        printf("  - Video Interface (masked AND pending)\n");
    if (memory->hw_regs.pi.int_pe_finish && memory->hw_regs.pi.mask_pe_finish) 
        printf("  - PE Finish (masked AND pending)\n");
    if (memory->hw_regs.pi.int_di && memory->hw_regs.pi.mask_di) 
        printf("  - DVD Interface (would fire if mask was enabled)\n");
    
    printf("\n");
    
    // Cleanup
    printf("Cleaning up...\n");
    gecko_memory_free(memory);
    free(memory);
    
    printf("Example completed successfully!\n");
    printf("\nKey improvements in simplified structure:\n");
    printf("  ✓ Bool types for all flags (true/false instead of 1/0)\n");
    printf("  ✓ Direct pointers instead of split high/low registers\n");
    printf("  ✓ No hardware padding - cleaner struct layout\n");
    printf("  ✓ More readable and maintainable code\n");
    printf("  ✓ Type-safe boolean operations\n");
    
    return 0;
}

