/**
 * @file porpoise_tool.c
 * @brief Porpoise Tool - Main Transpiler Implementation
 * 
 * PowerPC to C Transpiler for GameCube/Wii Assembly
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "porpoise_tool.h"
#include "opcode.h"
#include "project_generator.h"

// SDK function configuration
typedef enum {
    SDK_PARAM_PTR,    // Pointer (cast from register)
    SDK_PARAM_INT,    // Integer (cast from register)
    SDK_PARAM_FLOAT,  // Float (cast from register)
    SDK_PARAM_DOUBLE  // Double (cast from register)
} SDK_ParamType;

typedef struct {
    char name[128];
    SDK_ParamType param_types[10];
    int num_params;
} SDK_Function_Info;

static SDK_Function_Info *sdk_functions = NULL;
static int sdk_functions_count = 0;

// Transpiler configuration
typedef struct {
    bool transpile_sdk_functions;      // Transpile SDK functions (true) or ignore them (false)
    bool skip_stdlib_stubs;             // Skip stdlib_stubs.h inclusion (for MSL_C compatibility)
    bool ignore_cstd_calls;             // Ignore C++ standard library calls (std:: namespace)
    char sdk_functions_file[256];      // Path to SDK functions file
    char skip_list_file[256];          // Path to skip list file
} TranspilerConfig;

static TranspilerConfig config = {
    .transpile_sdk_functions = false,  // Default: ignore SDK functions
    .skip_stdlib_stubs = false,         // Default: include stdlib stubs
    .ignore_cstd_calls = true,          // Default: ignore C++ std calls
    .sdk_functions_file = "sdk_functions.txt",
    .skip_list_file = ""
};

// Function registry for indirect call resolution
typedef struct {
    char name[128];
    uint32_t gc_address;
    bool is_local;  // Skip local/static functions
} FunctionRegistryEntry;

static FunctionRegistryEntry *function_registry = NULL;
static int function_registry_count = 0;
static int function_registry_capacity = 0;

// Check if a function name is a C++ standard library call
static bool is_cstd_call(const char *name) {
    if (!name || name[0] == '\0') return false;
    
    // Check for std:: prefix (demangled names)
    if (strncmp(name, "std::", 5) == 0) {
        return true;
    }
    
    // Check for C++ mangled names starting with _ZNS (std:: namespace)
    // _ZNS = std:: namespace in Itanium ABI mangling
    if (strncmp(name, "_ZNS", 4) == 0) {
        return true;
    }
    
    // Check for _ZNSt (std:: namespace with template)
    if (strncmp(name, "_ZNSt", 5) == 0) {
        return true;
    }
    
    // Check for other common C++ std library patterns
    // _ZNK = const member function
    // _ZNSt = std:: namespace
    if (strncmp(name, "_ZNSt", 5) == 0 || strncmp(name, "_ZNKSt", 6) == 0) {
        return true;
    }
    
    // Check for operator new, delete, etc. (common C++ std library functions)
    if (strstr(name, "operator new") != NULL ||
        strstr(name, "operator delete") != NULL ||
        strstr(name, "operator new[]") != NULL ||
        strstr(name, "operator delete[]") != NULL) {
        return true;
    }
    
    return false;
}

// Check if a function is an SDK or standard library function
static bool is_sdk_or_stdlib_function(const char *name) {
    // Skip our custom __init_registers (we provide our own implementation)
    if (strcmp(name, "__init_registers") == 0) {
        return true;
    }
    
    // Pattern-based detection: Skip functions with SDK prefixes
    // This catches SDK functions even if they're not explicitly listed
    // Always enabled (not controlled by config.transpile_sdk_functions)
    if (strncmp(name, "OS", 2) == 0 ||          // OS functions (OSInit, OSReport, etc.)
        strncmp(name, "__OS", 4) == 0 ||        // Internal OS functions (__OSPSInit, etc.)
        strncmp(name, "EXI", 3) == 0 ||         // EXI functions (EXIInit, EXILock, etc.)
        strncmp(name, "DC", 2) == 0 ||          // Data cache functions (DCInvalidateRange, etc.)
        strncmp(name, "IC", 2) == 0 ||          // Instruction cache functions (ICInvalidateRange, etc.)
        strncmp(name, "LC", 2) == 0 ||          // L2 cache functions (LCDisable, etc.)
        strncmp(name, "L2", 2) == 0 ||          // L2 cache functions (L2GlobalInvalidate, etc.)
        strncmp(name, "SI", 2) == 0 ||          // Serial Interface functions (SIInit, etc.)
        strncmp(name, "VI", 2) == 0 ||          // Video Interface functions (VIInit, etc.)
        strncmp(name, "CARD", 4) == 0 ||        // Memory card functions (CARDInit, etc.)
        strncmp(name, "DVD", 3) == 0 ||         // DVD functions (DVDInit, etc.)
        strncmp(name, "AR", 2) == 0 ||          // Audio functions (ARInit, etc.)
        strncmp(name, "ARQ", 3) == 0 ||         // Audio Request Queue functions (ARQInit, etc.)
        strncmp(name, "PAD", 3) == 0 ||         // Controller functions (PADInit, etc.)
        strncmp(name, "GX", 2) == 0 ||          // Graphics functions (GXInit, etc.)
        strncmp(name, "AX", 2) == 0) {          // Audio DSP functions (AXInit, etc.)
        return true;
    }
    
    // Check explicit SDK functions list (only if we're NOT transpiling them)
    if (!config.transpile_sdk_functions) {
        for (int i = 0; i < sdk_functions_count; i++) {
            if (strcmp(name, sdk_functions[i].name) == 0) {
                return true;
            }
        }
    }
    
    // NOTE: Reserved names (like "main") should NOT be skipped - they should be renamed
    // to avoid conflicts (e.g., "main" -> "main_impl"). This is handled by sanitize_function_name.
    // Only SDK functions and standard library functions that are provided externally should be skipped.
    
    return false;
}

// Lookup function name by GameCube address (for compile-time function pointer resolution)
static const char* lookup_function_by_address(uint32_t gc_address) {
    if (!function_registry || gc_address == 0) {
        return NULL;
    }
    
    // Check if this is a GameCube address (0x80000000-0x84000000 range)
    if (gc_address < 0x80000000 || gc_address >= 0x84000000) {
        return NULL;
    }
    
    // Linear search (could be optimized with hash map if needed)
    for (int i = 0; i < function_registry_count; i++) {
        if (function_registry[i].gc_address == gc_address) {
            return function_registry[i].name;
        }
    }
    
    return NULL;
}

// Add function to registry for indirect call resolution
static void register_transpiled_function(const char *name, uint32_t gc_address, bool is_local) {
    // Initialize registry if needed
    if (!function_registry) {
        function_registry_capacity = 1000;
        function_registry = (FunctionRegistryEntry*)malloc(function_registry_capacity * sizeof(FunctionRegistryEntry));
        function_registry_count = 0;
    }
    
    // Expand if needed
    if (function_registry_count >= function_registry_capacity) {
        function_registry_capacity *= 2;
        function_registry = (FunctionRegistryEntry*)realloc(function_registry, 
                                                             function_registry_capacity * sizeof(FunctionRegistryEntry));
    }
    
    // Add entry (skip local/static, SDK, and stdlib functions)
    if (!is_local && gc_address != 0 && !is_sdk_or_stdlib_function(name)) {
        FunctionRegistryEntry *entry = &function_registry[function_registry_count++];
        
        // Store the SANITIZED name (e.g., "main" -> "main_impl") to avoid conflicts
        char sanitized_name[MAX_FUNCTION_NAME];
        sanitize_function_name(name, sanitized_name, sizeof(sanitized_name));
        strncpy(entry->name, sanitized_name, sizeof(entry->name) - 1);
        entry->name[sizeof(entry->name) - 1] = '\0';
        entry->gc_address = gc_address;
        entry->is_local = is_local;
    }
}

// Generate function_registry.c file
static void generate_function_registry(const char *project_dir) {
    char registry_path[512];
    snprintf(registry_path, sizeof(registry_path), "%s/src/function_registry.c", project_dir);
    
    FILE *f = fopen(registry_path, "w");
    if (!f) {
        fprintf(stderr, "Error: Cannot create function_registry.c\n");
        return;
    }
    
    fprintf(f, "/**\n");
    fprintf(f, " * @file function_registry.c\n");
    fprintf(f, " * @brief Auto-generated function registry for indirect call resolution\n");
    fprintf(f, " * \n");
    fprintf(f, " * This file maps GameCube function addresses to transpiled C functions.\n");
    fprintf(f, " * Used for vtables, callbacks, and other indirect calls.\n");
    fprintf(f, " */\n\n");
    fprintf(f, "#include \"function_address_map.h\"\n");
    fprintf(f, "#include \"all_functions.h\"\n\n");
    fprintf(f, "/**\n");
    fprintf(f, " * @brief Initialize all function mappings\n");
    fprintf(f, " * Must be called before any indirect calls are made\n");
    fprintf(f, " */\n");
    fprintf(f, "void init_function_registry(void) {\n");
    fprintf(f, "    // Initialize the address map\n");
    fprintf(f, "    if (!function_address_map_init()) {\n");
    fprintf(f, "        return;\n");
    fprintf(f, "    }\n\n");
    fprintf(f, "    // Register all %d transpiled functions\n", function_registry_count);
    
    // Generate all function registrations
    for (int i = 0; i < function_registry_count; i++) {
        const FunctionRegistryEntry *entry = &function_registry[i];
        
        // Check if function name is valid as a C identifier
        // C++ mangled names can have @, <, >, numeric prefixes like __32ClassName, Q23zen18..., etc.
        bool is_valid = true;
        const char *p = entry->name;
        
        // Check for invalid characters
        for (; *p; p++) {
            if (*p == '@' || *p == '<' || *p == '>' || *p == '?' || *p == '`' || *p == ' ') {
                is_valid = false;
                break;
            }
        }
        
        // Check for C++ mangled names with numbers (Q219, Q23zen18, __32Class, etc.)
        if (is_valid) {
            // Patterns like __32ClassName or __Q219 or Q23zen
            for (const char *scan = entry->name; scan && *scan; scan++) {
                // Check for digit followed by uppercase (like 32Class, 19Navi, 18ogScr)
                if (scan[0] >= '0' && scan[0] <= '9' && scan[1] >= 'A' && scan[1] <= 'Z') {
                    is_valid = false;
                    break;
                }
                // Check for consecutive uppercase then digit (like Q219, FP19)
                if (scan[0] >= 'A' && scan[0] <= 'Z' && scan[1] >= '0' && scan[1] <= '9') {
                    is_valid = false;
                    break;
                }
            }
        }
        
        // Skip internal functions (starting with __ are usually internal SDK/runtime)
        if (is_valid && entry->name[0] == '_' && entry->name[1] == '_') {
            is_valid = false;
        }
        
        // Only register valid function names
        if (is_valid) {
            fprintf(f, "    function_address_map_register(0x%08X, (TranspiledFunctionPtr)%s, \"%s\");\n",
                   entry->gc_address, entry->name, entry->name);
        }
    }
    
    fprintf(f, "}\n");
    fclose(f);
}

// Forward declarations
static void get_executable_dir(char *buffer, size_t buffer_size);

// Load SDK functions list from file
static void load_sdk_functions(const char *filepath) {
    // Try to load from executable directory first (like config.json)
    char exe_dir[512];
    get_executable_dir(exe_dir, sizeof(exe_dir));
    
    char full_path[512];
    // If filepath is relative, try next to executable first
    if (filepath[0] != '/' && filepath[0] != '\\' && 
        (strlen(filepath) < 2 || filepath[1] != ':')) {
        // Relative path - try next to executable first
        snprintf(full_path, sizeof(full_path), "%s%s", exe_dir, filepath);
    } else {
        // Absolute path
        strncpy(full_path, filepath, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';
    }
    
    FILE *f = fopen(full_path, "r");
    if (!f) {
        // Try current directory as fallback
        f = fopen(filepath, "r");
        if (!f) {
            fprintf(stderr, "Warning: Could not open SDK functions file: %s\n", filepath);
            fprintf(stderr, "  Tried: %s (next to executable) and current directory\n", full_path);
            fprintf(stderr, "  SDK functions will not be automatically skipped.\n");
            fprintf(stderr, "  Make sure sdk_functions.txt is in the same directory as porpoise_tool.exe\n");
            return;
        }
        // Use the current directory path
        strncpy(full_path, filepath, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';
    }
    
    printf("Loading SDK functions from: %s\n", full_path);
    
    char line[256];
    int capacity = 100;
    sdk_functions = (SDK_Function_Info*)malloc(capacity * sizeof(SDK_Function_Info));
    sdk_functions_count = 0;
    
    while (fgets(line, sizeof(line), f)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        
        // Parse line: FunctionName:param1_type,param2_type,param3_type
        char func_name[128];
        char params_str[128] = "";
        
        int items = sscanf(line, "%127[^:\n]:%127[^\n]", func_name, params_str);
        if (items < 1) continue;
        
        // Expand capacity if needed
        if (sdk_functions_count >= capacity) {
            capacity *= 2;
            sdk_functions = (SDK_Function_Info*)realloc(sdk_functions, capacity * sizeof(SDK_Function_Info));
        }
        
        SDK_Function_Info *info = &sdk_functions[sdk_functions_count++];
        strncpy(info->name, func_name, sizeof(info->name) - 1);
        info->name[sizeof(info->name) - 1] = '\0';
        info->num_params = 0;
        
        // Parse parameter types
        if (items == 2 && params_str[0] != '\0' && params_str[0] != '\n' && params_str[0] != '\r') {
            char *token = strtok(params_str, ",");
            while (token && info->num_params < 10) {
                // Trim whitespace
                while (*token == ' ' || *token == '\t') token++;
                char *end = token + strlen(token) - 1;
                while (end > token && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
                    *end = '\0';
                    end--;
                }
                
                if (strcmp(token, "ptr") == 0) {
                    info->param_types[info->num_params++] = SDK_PARAM_PTR;
                } else if (strcmp(token, "int") == 0) {
                    info->param_types[info->num_params++] = SDK_PARAM_INT;
                } else if (strcmp(token, "float") == 0) {
                    info->param_types[info->num_params++] = SDK_PARAM_FLOAT;
                } else if (strcmp(token, "double") == 0) {
                    info->param_types[info->num_params++] = SDK_PARAM_DOUBLE;
                }
                token = strtok(NULL, ",");
            }
        }
    }
    
    fclose(f);
    printf("Loaded %d SDK function signatures\n", sdk_functions_count);
}

// Initialize register tracker
static void register_tracker_init(RegisterTracker *tracker) {
    memset(tracker, 0, sizeof(RegisterTracker));
}

// Set a register to a known address
static void register_tracker_set(RegisterTracker *tracker, int reg, uint32_t address) {
    if (reg >= 0 && reg < 32) {
        tracker->r[reg] = address;
        tracker->r_known[reg] = (address >= 0x80000000 && address < 0x84000000);
    }
}

// Get a register's known address (returns 0 if unknown)
static uint32_t register_tracker_get(RegisterTracker *tracker, int reg) {
    if (reg >= 0 && reg < 32 && tracker->r_known[reg]) {
        return tracker->r[reg];
    }
    return 0;
}

// Set LR to a known address
static void register_tracker_set_lr(RegisterTracker *tracker, uint32_t address) {
    tracker->lr = address;
    tracker->lr_known = (address >= 0x80000000 && address < 0x84000000);
}

// Get LR's known address (returns 0 if unknown)
static uint32_t register_tracker_get_lr(RegisterTracker *tracker) {
    return tracker->lr_known ? tracker->lr : 0;
}

// Set CTR to a known address
static void register_tracker_set_ctr(RegisterTracker *tracker, uint32_t address) {
    tracker->ctr = address;
    tracker->ctr_known = (address >= 0x80000000 && address < 0x84000000);
}

// Get CTR's known address (returns 0 if unknown)
static uint32_t register_tracker_get_ctr(RegisterTracker *tracker) {
    return tracker->ctr_known ? tracker->ctr : 0;
}

// Mark register as unknown (e.g., after arithmetic operations)
static void register_tracker_clear(RegisterTracker *tracker, int reg) {
    if (reg >= 0 && reg < 32) {
        tracker->r_known[reg] = false;
        tracker->r[reg] = 0;
    }
}

// Check if a function is an SDK function
static const SDK_Function_Info* get_sdk_function_info(const char *func_name) {
    for (int i = 0; i < sdk_functions_count; i++) {
        if (strcmp(sdk_functions[i].name, func_name) == 0) {
            return &sdk_functions[i];
        }
    }
    return NULL;
}

// Helper function to check if an address is a GameCube address that should be converted
// Returns true if the address should be converted to a host pointer
static inline bool is_gamecube_address(uint32_t addr) {
    // MEM1 cached: 0x80000000-0x84000000 (24 MB + some headroom)
    if (addr >= 0x80000000 && addr < 0x84000000) return true;
    // MEM1 uncached: 0xC0000000-0xC2000000
    if (addr >= 0xC0000000 && addr < 0xC2000000) return true;
    // Hardware I/O registers: 0xCC000000-0xCC010000 (VI, PI, MI, etc.)
    if (addr >= 0xCC000000 && addr < 0xCC010000) return true;
    // MEM2 cached (Wii): 0x90000000-0x94000000
    if (addr >= 0x90000000 && addr < 0x94000000) return true;
    // MEM2 uncached (Wii): 0xD0000000-0xD4000000
    if (addr >= 0xD0000000 && addr < 0xD4000000) return true;
    // Locked cache: 0xE0000000-0xE0010000
    if (addr >= 0xE0000000 && addr < 0xE0010000) return true;
    return false;
}

// Helper function to convert GameCube address to host pointer offset
// Returns the offset from mem base, or 0xFFFFFFFF if not a known GameCube address
static inline uint32_t gamecube_to_offset(uint32_t addr) {
    // MEM1 cached: map to mem buffer
    if (addr >= 0x80000000 && addr < 0x84000000) {
        return addr - 0x80000000;
    }
    // MEM1 uncached: also map to mem buffer (same physical memory)
    if (addr >= 0xC0000000 && addr < 0xC2000000) {
        return addr - 0xC0000000;
    }
        // Hardware I/O: map to mem buffer (SDK will handle actual I/O)
        // Use offset after MEM1 (24MB) but within 256MB buffer
        if (addr >= 0xCC000000 && addr < 0xCC010000) {
            return addr - 0xCC000000 + 0x1800000;  // After MEM1 (24MB), within 256MB buffer
        }
    // MEM2 cached: map to mem buffer (if allocated)
    if (addr >= 0x90000000 && addr < 0x94000000) {
        return addr - 0x90000000 + 0x1800000;  // After MEM1
    }
    // MEM2 uncached: map to mem buffer
    if (addr >= 0xD0000000 && addr < 0xD4000000) {
        return addr - 0xD0000000 + 0x1800000;  // After MEM1
    }
    // Locked cache: map to mem buffer
    if (addr >= 0xE0000000 && addr < 0xE0010000) {
        return addr - 0xE0000000 + 0x5800000;  // After MEM2
    }
    return 0xFFFFFFFF;  // Not a known GameCube address
}

/**
 * @brief Transpile from assembly text (mnemonic + operands)
 */
bool transpile_from_asm(const char *mnemonic, const char *operands, uint32_t address,
                        char *output, size_t output_size,
                        char *comment, size_t comment_size,
                        const char **prev_lines, int num_prev_lines,
                        const Function_Info *func_context,
                        const LabelMap *label_map,
                        const StringTable *string_table,
                        RegisterTracker *tracker) {
    // Detect string address loading patterns (lis followed by addi/ori)
    // Check if previous instruction was lis and current is addi
    static uint32_t last_lis_value = 0;
    static int last_lis_reg = -1;
    
    if (strcmp(mnemonic, "lis") == 0) {
        // Parse: lis rD, value
        int reg;
        uint32_t value;
        if (sscanf(operands, "r%d, %i", &reg, &value) == 2 ||
            sscanf(operands, "r%d,%i", &reg, &value) == 2) {
            uint32_t shifted_value = value << 16;  // lis shifts left by 16
            
            // Convert GameCube addresses to host pointers immediately
            if (is_gamecube_address(shifted_value)) {
                uint32_t offset = gamecube_to_offset(shifted_value);
                if (offset != 0xFFFFFFFF) {
                    // Generate code that directly creates host pointer: mem + offset
                    snprintf(output, output_size, "r%d = (uintptr_t)(mem + 0x%08X);",
                            reg, offset);
                    snprintf(comment, comment_size, "lis r%d, %i (GC 0x%08X -> host ptr)",
                            reg, value, shifted_value);
                    // Don't track - register now contains host pointer, not GameCube address
                    last_lis_reg = -1;  // Reset since we've converted it
                    if (tracker) {
                        register_tracker_clear(tracker, reg);
                    }
                    return true;
                }
            }
            // Not a GameCube address - track it for potential addi/ori combination
            last_lis_reg = reg;
            last_lis_value = shifted_value;
            // Track this partial address
            if (tracker) {
                register_tracker_set(tracker, reg, last_lis_value);
            }
        }
    }
    
    if ((strcmp(mnemonic, "addi") == 0 || strcmp(mnemonic, "ori") == 0)) {
        // Parse: addi rD, rA, simm
        int rD, rA;
        int32_t simm;
        if (sscanf(operands, "r%d, r%d, %i", &rD, &rA, &simm) == 3 ||
            sscanf(operands, "r%d,r%d,%i", &rD, &rA, &simm) == 3) {
            if (rA == last_lis_reg) {
                // Combine lis + addi to form full address
                uint32_t full_addr = last_lis_value + simm;
                
                // Check if this address matches a string
                if (string_table) {
                    const StringEntry *str_entry = string_table_find(string_table, full_addr);
                    if (str_entry) {
                        // Generate string reference instead of raw address
                        snprintf(output, output_size, "r%d = (uintptr_t)&%s;  /* \"%s\" */",
                                rD, str_entry->label, str_entry->content);
                        snprintf(comment, comment_size, "%s r%d, r%d, %d (string ref)",
                                mnemonic, rD, rA, simm);
                        last_lis_reg = -1;  // Reset
                        // Track as host pointer (string address)
                        if (tracker) {
                            register_tracker_clear(tracker, rD);  // Can't track string addresses
                        }
                        return true;
                    }
                }
                
                // Convert GameCube addresses to host pointers immediately
                if (is_gamecube_address(full_addr)) {
                    uint32_t offset = gamecube_to_offset(full_addr);
                    if (offset != 0xFFFFFFFF) {
                        // Generate code that directly creates host pointer: mem + offset
                        snprintf(output, output_size, "r%d = (uintptr_t)(mem + 0x%08X);",
                                rD, offset);
                        snprintf(comment, comment_size, "%s r%d, r%d, %d (GC 0x%08X -> host ptr)",
                                mnemonic, rD, rA, simm, full_addr);
                        last_lis_reg = -1;  // Reset
                        // Don't track - register now contains host pointer, not GameCube address
                        if (tracker) {
                            register_tracker_clear(tracker, rD);
                        }
                        return true;
                    }
                }
                // Not a GameCube address - just set the value
                // Track the full address
                if (tracker) {
                    register_tracker_set(tracker, rD, full_addr);
                }
            } else if (tracker && tracker->r_known[rA]) {
                // rA contains a known address, add offset to it
                uint32_t base_addr = tracker->r[rA];
                uint32_t new_addr = base_addr + simm;
                
                // Check if the result is a GameCube address - generate host pointer directly
                if (is_gamecube_address(new_addr)) {
                    uint32_t offset = gamecube_to_offset(new_addr);
                    if (offset != 0xFFFFFFFF) {
                        // Generate code that directly creates host pointer: mem + offset
                        snprintf(output, output_size, "r%d = (uintptr_t)(mem + 0x%08X);",
                                rD, offset);
                        snprintf(comment, comment_size, "%s r%d, r%d, %d (GC 0x%08X -> host ptr)",
                                mnemonic, rD, rA, simm, new_addr);
                        // Don't track - register now contains host pointer
                        register_tracker_clear(tracker, rD);
                        return true;
                    }
                }
                // Not a GameCube address - track it
                register_tracker_set(tracker, rD, new_addr);
            } else {
                // Unknown value - clear tracking
                if (tracker) register_tracker_clear(tracker, rD);
            }
        }
    }
    
    // Track mtlr (move to link register)
    if (strcmp(mnemonic, "mtlr") == 0) {
        int reg;
        if (sscanf(operands, "r%d", &reg) == 1) {
            if (tracker) {
                uint32_t addr = register_tracker_get(tracker, reg);
                if (addr != 0) {
                    register_tracker_set_lr(tracker, addr);
                } else {
                    tracker->lr_known = false;
                }
            }
        }
    }
    
    // Track mtctr (move to count register)
    if (strcmp(mnemonic, "mtctr") == 0) {
        int reg;
        if (sscanf(operands, "r%d", &reg) == 1) {
            if (tracker) {
                uint32_t addr = register_tracker_get(tracker, reg);
                if (addr != 0) {
                    register_tracker_set_ctr(tracker, addr);
                } else {
                    tracker->ctr_known = false;
                }
            }
        }
    }
    
    // Track and transpile lwz (load word) - might load function pointers from vtables or function tables
    if (strcmp(mnemonic, "lwz") == 0) {
        int rD, rA;
        int32_t offset;
        if (sscanf(operands, "r%d, %i(r%d)", &rD, &offset, &rA) == 3 ||
            sscanf(operands, "r%d,%i(r%d)", &rD, &offset, &rA) == 3) {
            // Check if this is loading from an absolute address (rA == 0)
            if (rA == 0) {
                // Absolute address load: lwz rD, offset(0) means load from address = offset
                uint32_t abs_addr = (uint32_t)offset;
                
                // Generate code with literal address (for transpiler resolution)
                // Add conversion to handle GameCube addresses loaded from memory
                snprintf(output, output_size, 
                        "r%u = *(uint32_t*)(uintptr_t)0x%08X; "
                        "r%u = convert_gc_address((uint32_t)r%u);", 
                        rD, abs_addr, rD, rD);
                snprintf(comment, comment_size, "lwz r%u, %i(0) [convert GC addr if needed]", rD, offset);
                
                // Check if this address contains a function pointer
                const char *func_name = lookup_function_by_address(abs_addr);
                if (func_name && tracker) {
                    // The address itself is a function - track that rD contains this function address
                    register_tracker_set(tracker, rD, abs_addr);
                } else if (tracker) {
                    // TODO: Parse assembly data sections to find what value is stored at abs_addr
                    // For now, mark as unknown
                    register_tracker_clear(tracker, rD);
                }
                return true;
            } else {
                // Register-based load: lwz rD, offset(rA)
                // Check if rA contains a known address (from lis/addi)
                if (tracker && tracker->r_known[rA]) {
                    uint32_t base_addr = tracker->r[rA];
                    uint32_t load_addr = base_addr + offset;
                    
                    // Generate code with literal address (for transpiler resolution)
                    // Add conversion to handle GameCube addresses loaded from memory
                    snprintf(output, output_size, 
                            "r%u = *(uint32_t*)(uintptr_t)0x%08X; "
                            "r%u = convert_gc_address((uint32_t)r%u);", 
                            rD, load_addr, rD, rD);
                    snprintf(comment, comment_size, "lwz r%u, %i(r%u) [resolved: 0x%08X, convert GC addr if needed]", rD, offset, rA, load_addr);
                    
                    // Check if the value at this address is a function pointer
                    // TODO: Parse assembly data sections to find the actual value stored at load_addr
                    const char *func_name = lookup_function_by_address(load_addr);
                    if (func_name) {
                        // The address being loaded from is a function - track it
                        register_tracker_set(tracker, rD, load_addr);
                    } else {
                        // Can't determine what value is stored there at compile time
                        register_tracker_clear(tracker, rD);
                    }
                    return true;
                } else {
                    // Base register unknown - fall through to opcode transpile function
                    // Registers contain host pointers, so opcode will use direct cast
                    if (tracker) register_tracker_clear(tracker, rD);
                }
            }
        }
    }
    
    // Track and transpile stw (store word)
    if (strcmp(mnemonic, "stw") == 0) {
        int rS, rA;
        int32_t offset;
        if (sscanf(operands, "r%d, %i(r%d)", &rS, &offset, &rA) == 3 ||
            sscanf(operands, "r%d,%i(r%d)", &rS, &offset, &rA) == 3) {
            // Check if this is storing to an absolute address (rA == 0)
            if (rA == 0) {
                // Absolute address store: stw rS, offset(0) means store to address = offset
                uint32_t abs_addr = (uint32_t)offset;
                
                // Generate code with literal address (for transpiler resolution)
                snprintf(output, output_size, "*(uint32_t*)(uintptr_t)0x%08X = r%u;", abs_addr, rS);
                snprintf(comment, comment_size, "stw r%u, %i(0)", rS, offset);
                return true;
            } else {
                // Register-based store: stw rS, offset(rA)
                // Check if rA contains a known address (from lis/addi)
                if (tracker && tracker->r_known[rA]) {
                    uint32_t base_addr = tracker->r[rA];
                    uint32_t store_addr = base_addr + offset;
                    
                    // Generate code with literal address (for transpiler resolution)
                    snprintf(output, output_size, "*(uint32_t*)(uintptr_t)0x%08X = r%u;", store_addr, rS);
                    snprintf(comment, comment_size, "stw r%u, %i(r%u) [resolved: 0x%08X]", rS, offset, rA, store_addr);
                    return true;
                } else {
                    // Base register unknown - fall through to opcode transpile function
                    // Registers contain host pointers, so opcode will use direct cast
                }
            }
        }
    }
    
    // Track and transpile stwu (store word with update)
    if (strcmp(mnemonic, "stwu") == 0) {
        int rS, rA;
        int32_t offset;
        if (sscanf(operands, "r%d, %i(r%d)", &rS, &offset, &rA) == 3 ||
            sscanf(operands, "r%d,%i(r%d)", &rS, &offset, &rA) == 3) {
            // Check if rA contains a known address (from lis/addi)
            if (tracker && tracker->r_known[rA]) {
                uint32_t base_addr = tracker->r[rA];
                uint32_t store_addr = base_addr + offset;
                
                // Generate code with literal address (for transpiler resolution)
                // Note: stwu updates rA, so we need to update the tracker too
                if (offset >= 0) {
                    snprintf(output, output_size, "r%u = r%u + 0x%x; *(uint32_t*)(uintptr_t)0x%08X = r%u;", 
                            rA, rA, (uint16_t)offset, store_addr, rS);
                } else {
                    snprintf(output, output_size, "r%u = r%u - 0x%x; *(uint32_t*)(uintptr_t)0x%08X = r%u;", 
                            rA, rA, (uint16_t)(-offset), store_addr, rS);
                }
                snprintf(comment, comment_size, "stwu r%u, %i(r%u) [resolved: 0x%08X]", rS, offset, rA, store_addr);
                
                // Update tracker with new rA value
                register_tracker_set(tracker, rA, store_addr);
                return true;
            } else {
                // Base register unknown or runtime address - use translate_address()
                // Fall through to opcode transpile function which will use translate_address()
            }
        }
    }
    
    // Track operations that clear register values (arithmetic, etc.)
    // These operations make register values unknown
    if (tracker && (
        strcmp(mnemonic, "add") == 0 || strcmp(mnemonic, "subf") == 0 ||
        strcmp(mnemonic, "mul") == 0 || strcmp(mnemonic, "div") == 0 ||
        strcmp(mnemonic, "and") == 0 || strcmp(mnemonic, "or") == 0 ||
        strcmp(mnemonic, "xor") == 0 || strcmp(mnemonic, "slw") == 0 ||
        strcmp(mnemonic, "srw") == 0 || strcmp(mnemonic, "sraw") == 0)) {
        // Parse destination register and clear it
        int rD;
        if (sscanf(operands, "r%d", &rD) == 1) {
            register_tracker_clear(tracker, rD);
        }
    }
    
    // Track mflr (move from link register) - copies LR to a register
    if (strcmp(mnemonic, "mflr") == 0) {
        int reg;
        if (sscanf(operands, "r%d", &reg) == 1) {
            if (tracker && tracker->lr_known) {
                register_tracker_set(tracker, reg, tracker->lr);
            } else if (tracker) {
                register_tracker_clear(tracker, reg);
            }
        }
    }
    
    // Track mfctr (move from count register) - copies CTR to a register
    if (strcmp(mnemonic, "mfctr") == 0) {
        int reg;
        if (sscanf(operands, "r%d", &reg) == 1) {
            if (tracker && tracker->ctr_known) {
                register_tracker_set(tracker, reg, tracker->ctr);
            } else if (tracker) {
                register_tracker_clear(tracker, reg);
            }
        }
    }
    
    // Handle blr (branch to link register = return)
    if (strcmp(mnemonic, "blr") == 0) {
        if (func_context && func_context->returns_value) {
            snprintf(output, output_size, "return r3;");
        } else {
            snprintf(output, output_size, "return;");
        }
        snprintf(comment, comment_size, "blr");
        return true;
    }
    
    // Handle blrl (branch to link register and link)
    if (strcmp(mnemonic, "blrl") == 0) {
        uint32_t return_addr = address + 4;
        uint32_t func_addr = 0;
        const char *func_name = NULL;
        
        // Check if LR contains a known function address
        if (tracker) {
            func_addr = register_tracker_get_lr(tracker);
            if (func_addr != 0) {
                func_name = lookup_function_by_address(func_addr);
            }
        }
        
        if (func_name) {
            // Replace with direct function call!
            snprintf(output, output_size,
                    "{ lr = 0x%08X; %s(r3, r4, r5, r6, r7, r8, r9, r10, f1, f2); }",
                    return_addr, func_name);
            snprintf(comment, comment_size, "blrl - replaced with direct call to %s (0x%08X)", func_name, func_addr);
        } else {
            // Fallback to runtime resolution
            snprintf(output, output_size, 
                    "{ uintptr_t saved_lr = lr; lr = 0x%08X; "
                    "call_function_by_address((uint32_t)saved_lr, r3, r4, r5, r6, r7, r8, r9, r10, f1, f2); }",
                    return_addr);
            snprintf(comment, comment_size, "blrl - indirect call via lr (address unknown at compile time)");
        }
        return true;
    }
    
    // Handle bctrl (branch to count register and link)
    if (strcmp(mnemonic, "bctrl") == 0) {
        uint32_t return_addr = address + 4;
        uint32_t func_addr = 0;
        const char *func_name = NULL;
        
        // Check if CTR contains a known function address
        if (tracker) {
            func_addr = register_tracker_get_ctr(tracker);
            if (func_addr != 0) {
                func_name = lookup_function_by_address(func_addr);
            }
        }
        
        if (func_name) {
            // Replace with direct function call!
            snprintf(output, output_size,
                    "{ lr = 0x%08X; %s(r3, r4, r5, r6, r7, r8, r9, r10, f1, f2); }",
                    return_addr, func_name);
            snprintf(comment, comment_size, "bctrl - replaced with direct call to %s (0x%08X)", func_name, func_addr);
        } else {
            // Fallback to runtime resolution
            snprintf(output, output_size,
                    "{ uintptr_t saved_ctr = ctr; lr = 0x%08X; "
                    "call_function_by_address((uint32_t)saved_ctr, r3, r4, r5, r6, r7, r8, r9, r10, f1, f2); }",
                    return_addr);
            snprintf(comment, comment_size, "bctrl - indirect call via ctr (address unknown at compile time)");
        }
        return true;
    }
    
    // Handle conditional returns (blelr, bgelr, bnelr, etc.)
    if (strncmp(mnemonic, "b", 1) == 0 && strstr(mnemonic, "lr") != NULL) {
        // Extract condition from mnemonic
        char clean_mnemonic[32];
        strncpy(clean_mnemonic, mnemonic, sizeof(clean_mnemonic) - 1);
        clean_mnemonic[sizeof(clean_mnemonic) - 1] = '\0';
        
        // Remove + or - suffix (branch prediction hints)
        char *hint = strchr(clean_mnemonic, '+');
        if (!hint) hint = strchr(clean_mnemonic, '-');
        if (hint) *hint = '\0';
        
        const char *condition = "";
        if (strcmp(clean_mnemonic, "beqlr") == 0) condition = "(cr0 & 0x2)";
        else if (strcmp(clean_mnemonic, "bnelr") == 0) condition = "(!(cr0 & 0x2))";
        else if (strcmp(clean_mnemonic, "bltlr") == 0) condition = "(cr0 & 0x8)";
        else if (strcmp(clean_mnemonic, "bgtlr") == 0) condition = "(cr0 & 0x4)";
        else if (strcmp(clean_mnemonic, "blelr") == 0) condition = "(cr0 & 0xA)";
        else if (strcmp(clean_mnemonic, "bgelr") == 0) condition = "(!(cr0 & 0x8))";
        else if (strcmp(clean_mnemonic, "blr") == 0) {
            // Unconditional return - already handled above
            snprintf(output, output_size, "return;");
            snprintf(comment, comment_size, "blr");
            return true;
        }
        else condition = "(1 /* unknown condition */)";
        
        snprintf(output, output_size, "if %s return;", condition);
        snprintf(comment, comment_size, "%s", mnemonic);
        return true;
    }
    
    // Handle bctr (branch to count register)
    if (strcmp(mnemonic, "bctr") == 0) {
        snprintf(output, output_size, "pc = ctr;  /* bctr - indirect branch (cannot be expressed as goto in C) */");
        snprintf(comment, comment_size, "bctr");
        return true;
    }
    
    // Handle bctrl (branch to count register and link)
    if (strcmp(mnemonic, "bctrl") == 0) {
        snprintf(output, output_size, "lr = 0x%08X; ((void (*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, double, double))ctr)(r3, r4, r5, r6, r7, r8, r9, r10, f1, f2);", address + 4);
        snprintf(comment, comment_size, "bctrl - indirect call via ctr");
        return true;
    }
    
    // Handle branches by parsing operands directly
    if (strcmp(mnemonic, "b") == 0 || strcmp(mnemonic, "bl") == 0 || 
        strcmp(mnemonic, "ba") == 0 || strcmp(mnemonic, "bla") == 0) {
        char target[512];  // Increased buffer for long C++ mangled names
        sscanf(operands, "%511s", target);
        
        // Strip quotes from function names (C++ templates use quoted names in assembly)
        if (target[0] == '"') {
            size_t len = strlen(target);
            // Move string left by 1 to remove leading quote
            memmove(target, target + 1, len);
            len = strlen(target);
            // Remove trailing quote if present
            if (len > 0 && target[len - 1] == '"') {
                target[len - 1] = '\0';
            }
        }
        
        // Check if it's an absolute address (starts with 0x)
        if (target[0] == '0' && target[1] == 'x') {
            // Absolute address - treat as function call
            uint32_t addr;
            sscanf(target, "%x", &addr);
            if (strcmp(mnemonic, "bla") == 0 || strcmp(mnemonic, "bl") == 0) {
                snprintf(output, output_size, "lr = 0x%08X; ((void (*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, double, double))0x%08X)(r3, r4, r5, r6, r7, r8, r9, r10, f1, f2);  /* call absolute */", address + 4, addr);
            } else {
                snprintf(output, output_size, "pc = 0x%08X;  /* branch absolute */", addr);
            }
        }
        // Check if target is a label (.L_ or .lbl_) or function name
        else if (target[0] == '.') {
            // It's a local label - convert to C label (remove leading dot)
            char label_name[128];
            strcpy(label_name, target + 1);
            
            // Extract address from label name for cross-function jump detection
            uint32_t label_addr = 0;
            if (strncmp(label_name, "L_", 2) == 0) {
                sscanf(label_name + 2, "%x", &label_addr);
            } else if (strncmp(label_name, "lbl_", 4) == 0) {
                sscanf(label_name + 4, "%x", &label_addr);
            }
            
            // For now, always use goto - trampolines will be detected later
            // and flagged with comments. But to avoid compilation errors,
            // we'll generate a placeholder that compiles.
            // Note: This might be a cross-function jump (trampoline)
            if (strcmp(mnemonic, "bl") == 0 || strcmp(mnemonic, "bla") == 0) {
                snprintf(output, output_size, "lr = 0x%08X; goto %s;  /* May be cross-function */", address + 4, label_name);
            } else {
                snprintf(output, output_size, "goto %s;  /* May be cross-function */", label_name);
            }
        } else {
            // Check if it's a label within the current function (from .sym directives)
            // Extract address from symbolic name like "GXPerf_80341E40" -> 0x80341E40
            bool is_local_label = false;
            uint32_t target_addr = 0;
            
            // Try to extract address from name (format: Name_HEXADDR)
            const char *underscore = strrchr(target, '_');
            if (underscore) {
                sscanf(underscore + 1, "%x", &target_addr);
                
                // Check if this address is in the current function's label map
                if (label_map && func_context && target_addr != 0) {
                    const char *mapped_func = labelmap_find_function(label_map, target_addr);
                    if (mapped_func && strcmp(mapped_func, func_context->name) == 0) {
                        is_local_label = true;
                    }
                }
            }
            
            if (is_local_label) {
                // It's a label within the current function - use goto
                if (strcmp(mnemonic, "bl") == 0 || strcmp(mnemonic, "bla") == 0) {
                    snprintf(output, output_size, "lr = 0x%08X; goto %s;", address + 4, target);
                } else {
                    snprintf(output, output_size, "goto %s;", target);
                }
            } else {
                // It's a function name (external or other file)
                // Both b and bl should be function calls for external symbols
                // IMPORTANT: We MUST sanitize reserved names when calling them too,
                // because the function was defined with a sanitized name (e.g., main -> main_impl)
                // This ensures calls to reserved names match the renamed definitions.
                
                // Sanitize the function name to get the actual C function name
                char sanitized_target[MAX_FUNCTION_NAME];
                static char stub_name[256];  // Static so it persists after function returns
                const char *actual_target;
                
                // Special case: __va_arg -> ppc_va_arg (to avoid MSVC intrinsic conflict)
                if (strcmp(target, "__va_arg") == 0) {
                    actual_target = "ppc_va_arg";
                }
                // Handle C++ template names, @unnamed@ patterns, and extremely long mangled names
                // These contain '<', '>', ',', '@' or are very long
                else if (strlen(target) > 80 || strchr(target, '<') != NULL || 
                         strchr(target, '>') != NULL || strchr(target, ',') != NULL ||
                         strchr(target, '@') != NULL) {
                    // Check if this is a C++ standard library call that should be ignored
                    if (config.ignore_cstd_calls && is_cstd_call(target)) {
                        // Generate a comment instead of a function call
                        if (strcmp(mnemonic, "bl") == 0 || strcmp(mnemonic, "bla") == 0) {
                            snprintf(output, output_size, "/* C++ std call ignored: %s */", target);
                        } else {
                            snprintf(output, output_size, "/* C++ std branch ignored: %s */", target);
                        }
                        snprintf(comment, comment_size, "%s %s (C++ std call - ignored)", mnemonic, target);
                        return true;
                    }
                    
                    // Create a shorter stub name based on hash
                    unsigned int hash = 0;
                    for (const char *p = target; *p; p++) {
                        hash = hash * 31 + (unsigned char)*p;
                    }
                    snprintf(stub_name, sizeof(stub_name), "cpp_stub_func_%08x", hash);
                    actual_target = stub_name;
                } else {
                    // Normal function name - check if it should be ignored first
                    if (config.ignore_cstd_calls && is_cstd_call(target)) {
                        // Generate a comment instead of a function call
                        if (strcmp(mnemonic, "bl") == 0 || strcmp(mnemonic, "bla") == 0) {
                            snprintf(output, output_size, "/* C++ std call ignored: %s */", target);
                        } else {
                            snprintf(output, output_size, "/* C++ std branch ignored: %s */", target);
                        }
                        snprintf(comment, comment_size, "%s %s (C++ std call - ignored)", mnemonic, target);
                        return true;
                    }
                    // Sanitize it (handles reserved names like main -> main_impl)
                    sanitize_function_name(target, sanitized_target, sizeof(sanitized_target));
                    actual_target = sanitized_target;
                }
                
                // Check if this is an SDK function
                const SDK_Function_Info *sdk_info = get_sdk_function_info(actual_target);
                
                char params[512];
                bool is_sdk_func = (sdk_info != NULL);
                
                if (is_sdk_func) {
                    // SDK function - generate typed parameters (or empty if no params)
                    if (sdk_info->num_params > 0) {
                        const char *int_regs[] = {"r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10"};
                        const char *float_regs[] = {"f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8"};
                        
                        char *p = params;
                        size_t remaining = sizeof(params);
                        int int_reg_idx = 0;
                        int float_reg_idx = 0;
                        
                        for (int i = 0; i < sdk_info->num_params && i < 10; i++) {
                            if (i > 0) {
                                int written = snprintf(p, remaining, ", ");
                                p += written;
                                remaining -= written;
                            }
                            
                            switch (sdk_info->param_types[i]) {
                                case SDK_PARAM_PTR:
                                    if (int_reg_idx < 8) {
                                        int written = snprintf(p, remaining, "(void*)%s", int_regs[int_reg_idx++]);
                                        p += written;
                                        remaining -= written;
                                    }
                                    break;
                                case SDK_PARAM_INT:
                                    if (int_reg_idx < 8) {
                                        int written = snprintf(p, remaining, "(int)%s", int_regs[int_reg_idx++]);
                                        p += written;
                                        remaining -= written;
                                    }
                                    break;
                                case SDK_PARAM_FLOAT:
                                    if (float_reg_idx < 8) {
                                        int written = snprintf(p, remaining, "(float)%s", float_regs[float_reg_idx++]);
                                        p += written;
                                        remaining -= written;
                                    }
                                    break;
                                case SDK_PARAM_DOUBLE:
                                    if (float_reg_idx < 8) {
                                        int written = snprintf(p, remaining, "%s", float_regs[float_reg_idx++]);
                                        p += written;
                                        remaining -= written;
                                    }
                                    break;
                            }
                        }
                    } else {
                        // SDK function with no parameters - empty parameter list
                        params[0] = '\0';
                    }
                } else {
                    // Game function - use standard 10-parameter convention
                    snprintf(params, sizeof(params), "r3, r4, r5, r6, r7, r8, r9, r10, f1, f2");
                }
                
                // Check if this SDK function returns a value (functions that return pointers or ints)
                // For now, we'll assume SDK functions with names starting with "OSGet" return values
                bool returns_value = false;
                if (is_sdk_func && (strncmp(actual_target, "OSGet", 5) == 0 || 
                                    strncmp(actual_target, "OSIs", 4) == 0 ||
                                    strncmp(actual_target, "OSCreate", 8) == 0 ||
                                    strncmp(actual_target, "OSAlloc", 7) == 0 ||
                                    strncmp(actual_target, "OSGetCurrent", 12) == 0 ||
                                    strncmp(actual_target, "OSGetThread", 11) == 0)) {
                    returns_value = true;
                }
                
                if (strcmp(mnemonic, "bl") == 0 || strcmp(mnemonic, "bla") == 0) {
                    if (is_sdk_func && returns_value) {
                        // SDK function that returns a value - capture return in r3
                        if (sdk_info->num_params > 0) {
                            snprintf(output, output_size, "r3 = (uintptr_t)%s(%s);", actual_target, params);
                        } else {
                            snprintf(output, output_size, "r3 = (uintptr_t)%s();", actual_target);
                        }
                    } else {
                        // SDK function with no return, or game function
                        if (is_sdk_func && sdk_info->num_params == 0) {
                            snprintf(output, output_size, "%s();", actual_target);
                        } else {
                            snprintf(output, output_size, "%s(%s);", actual_target, params);
                        }
                    }
                } else {
                    // Tail call optimization (branch without link)
                    if (is_sdk_func && sdk_info->num_params == 0) {
                        snprintf(output, output_size, "return %s();  /* Tail call */", actual_target);
                    } else {
                        snprintf(output, output_size, "return %s(%s);  /* Tail call */", actual_target, params);
                    }
                }
            }
        }
        snprintf(comment, comment_size, "%s %s", mnemonic, target);
        return true;
    }
    
    // Handle conditional branches
    if (strncmp(mnemonic, "b", 1) == 0 && strlen(mnemonic) > 1) {
        // Conditional branch (beq, bne, blt, etc.)
        // Extract mnemonic without branch prediction hints (+/-)
        char clean_mnemonic[32];
        strncpy(clean_mnemonic, mnemonic, sizeof(clean_mnemonic) - 1);
        clean_mnemonic[sizeof(clean_mnemonic) - 1] = '\0';
        
        // Remove + or - suffix (branch prediction hints)
        char *hint = strchr(clean_mnemonic, '+');
        if (!hint) hint = strchr(clean_mnemonic, '-');
        if (hint) *hint = '\0';
        
        // Parse operands - may be "cr1, .label" or just ".label"
        char cr_field[32] = "cr0";
        char target[128];
        
        if (sscanf(operands, "cr%*d , %127s", target) == 1 || 
            sscanf(operands, "cr%*d, %127s", target) == 1) {
            // Has explicit CR field
            sscanf(operands, "%31[^,]", cr_field);
            // Trim whitespace from cr_field
            char *end = cr_field + strlen(cr_field) - 1;
            while (end > cr_field && (*end == ' ' || *end == '\t')) end--;
            *(end + 1) = '\0';
        } else {
            // No explicit CR field, use cr0
            sscanf(operands, "%127s", target);
        }
        
        if (target[0] == '.') {
            char label_name[128];
            strcpy(label_name, target + 1);
            
            // Map branch conditions with proper parentheses
            if (strcmp(clean_mnemonic, "beq") == 0) {
                snprintf(output, output_size, "if (%s & 0x2) goto %s;", cr_field, label_name);
            }
            else if (strcmp(clean_mnemonic, "bne") == 0) {
                snprintf(output, output_size, "if (!(%s & 0x2)) goto %s;", cr_field, label_name);
            }
            else if (strcmp(clean_mnemonic, "blt") == 0) {
                snprintf(output, output_size, "if (%s & 0x8) goto %s;", cr_field, label_name);
            }
            else if (strcmp(clean_mnemonic, "bgt") == 0) {
                snprintf(output, output_size, "if (%s & 0x4) goto %s;", cr_field, label_name);
            }
            else if (strcmp(clean_mnemonic, "ble") == 0) {
                snprintf(output, output_size, "if (%s & 0xA) goto %s;", cr_field, label_name);
            }
            else if (strcmp(clean_mnemonic, "bge") == 0) {
                snprintf(output, output_size, "if (!(%s & 0x8)) goto %s;", cr_field, label_name);
            }
            else if (strcmp(clean_mnemonic, "bdnz") == 0) {
                snprintf(output, output_size, "if (--ctr) goto %s;", label_name);
            }
            else {
                snprintf(output, output_size, "if (1 /* unknown condition */) goto %s;", label_name);
            }
        } else {
            // Check if it's a label within the current function (from .sym directives)
            // Extract address from symbolic name like "GXPerf_80341E40" -> 0x80341E40
            bool is_local_label = false;
            uint32_t target_addr = 0;
            
            // Try to extract address from name (format: Name_HEXADDR)
            const char *underscore = strrchr(target, '_');
            if (underscore) {
                sscanf(underscore + 1, "%x", &target_addr);
                
                // Check if this address is in the current function's label map
                if (label_map && func_context && target_addr != 0) {
                    const char *mapped_func = labelmap_find_function(label_map, target_addr);
                    if (mapped_func && strcmp(mapped_func, func_context->name) == 0) {
                        is_local_label = true;
                    }
                }
            }
            
            if (is_local_label) {
                // It's a label within current function - use conditional goto
                if (strcmp(clean_mnemonic, "bgt") == 0) {
                    snprintf(output, output_size, "if (%s & 0x4) goto %s;", cr_field, target);
                }
                else if (strcmp(clean_mnemonic, "blt") == 0) {
                    snprintf(output, output_size, "if (%s & 0x8) goto %s;", cr_field, target);
                }
                else if (strcmp(clean_mnemonic, "beq") == 0) {
                    snprintf(output, output_size, "if (%s & 0x2) goto %s;", cr_field, target);
                }
                else if (strcmp(clean_mnemonic, "bne") == 0) {
                    snprintf(output, output_size, "if (!(%s & 0x2)) goto %s;", cr_field, target);
                }
                else if (strcmp(clean_mnemonic, "ble") == 0) {
                    snprintf(output, output_size, "if (%s & 0xA) goto %s;", cr_field, target);
                }
                else if (strcmp(clean_mnemonic, "bge") == 0) {
                    snprintf(output, output_size, "if (!(%s & 0x8)) goto %s;", cr_field, target);
                }
                else {
                    snprintf(output, output_size, "if (1 /* unknown condition: %s */) goto %s;", clean_mnemonic, target);
                }
            } else {
                // It's a function name - conditional function call
                // Check if this is a C++ standard library call that should be ignored
                if (config.ignore_cstd_calls && is_cstd_call(target)) {
                    // Generate a comment instead of a function call
                    snprintf(output, output_size, "/* C++ std call ignored: %s (conditional) */", target);
                    snprintf(comment, comment_size, "%s %s (C++ std call - ignored)", mnemonic, target);
                    return true;
                }
                
                // Generate proper conditional wrapper
                int written = 0;
                
                if (strcmp(clean_mnemonic, "bgt") == 0) {
                    written += snprintf(output + written, output_size - written, "if (%s & 0x4) {  // bgt: branch if greater than\n        ", cr_field);
                }
                else if (strcmp(clean_mnemonic, "blt") == 0) {
                    written += snprintf(output + written, output_size - written, "if (%s & 0x8) {  // blt: branch if less than\n        ", cr_field);
                }
                else if (strcmp(clean_mnemonic, "beq") == 0) {
                    written += snprintf(output + written, output_size - written, "if (%s & 0x2) {  // beq: branch if equal\n        ", cr_field);
                }
                else if (strcmp(clean_mnemonic, "bne") == 0) {
                    written += snprintf(output + written, output_size - written, "if (!(%s & 0x2)) {  // bne: branch if not equal\n        ", cr_field);
                }
                else if (strcmp(clean_mnemonic, "ble") == 0) {
                    written += snprintf(output + written, output_size - written, "if (%s & 0xA) {  // ble: branch if less than or equal\n        ", cr_field);
                }
                else if (strcmp(clean_mnemonic, "bge") == 0) {
                    written += snprintf(output + written, output_size - written, "if (!(%s & 0x8)) {  // bge: branch if greater than or equal\n        ", cr_field);
                }
                else {
                    written += snprintf(output + written, output_size - written, "if (1 /* unknown condition: %s */) {\n        ", clean_mnemonic);
                }
                
                written += snprintf(output + written, output_size - written, "%s(r3, r4, r5, r6, r7, r8, r9, r10, f1, f2);\n    }", target);
            }
        }
        snprintf(comment, comment_size, "%s %s", mnemonic, operands);
        return true;
    }
    
    // For all other instructions, return false to use byte-based decoding
    return false;
}

/**
 * @brief Transpile a single instruction
 */
bool transpile_instruction(uint32_t instruction, uint32_t address, 
                          char *output, size_t output_size,
                          char *comment, size_t comment_size) {
    // Try each opcode in order (TODO: use lookup table for efficiency)
    
    // Integer arithmetic
    ADD_Instruction add;
    if (decode_add(instruction, &add)) {
        transpile_add(&add, output, output_size);
        comment_add(&add, comment, comment_size);
        return true;
    }
    
    ADDI_Instruction addi;
    if (decode_addi(instruction, &addi)) {
        transpile_addi(&addi, output, output_size);
        comment_addi(&addi, comment, comment_size);
        return true;
    }
    
    LIS_Instruction lis;
    if (decode_lis(instruction, &lis)) {
        transpile_lis(&lis, output, output_size);
        comment_lis(&lis, comment, comment_size);
        return true;
    }
    
    SUBF_Instruction subf;
    if (decode_subf(instruction, &subf)) {
        transpile_subf(&subf, output, output_size);
        comment_subf(&subf, comment, comment_size);
        return true;
    }
    
    SUBFC_Instruction subfc;
    if (decode_subfc(instruction, &subfc)) {
        transpile_subfc(&subfc, output, output_size);
        comment_subfc(&subfc, comment, comment_size);
        return true;
    }
    
    SUBFE_Instruction subfe;
    if (decode_subfe(instruction, &subfe)) {
        transpile_subfe(&subfe, output, output_size);
        comment_subfe(&subfe, comment, comment_size);
        return true;
    }
    
    ADDC_Instruction addc;
    if (decode_addc(instruction, &addc)) {
        transpile_addc(&addc, output, output_size);
        comment_addc(&addc, comment, comment_size);
        return true;
    }
    
    ADDE_Instruction adde;
    if (decode_adde(instruction, &adde)) {
        transpile_adde(&adde, output, output_size);
        comment_adde(&adde, comment, comment_size);
        return true;
    }
    
    NEG_Instruction neg;
    if (decode_neg(instruction, &neg)) {
        transpile_neg(&neg, output, output_size);
        comment_neg(&neg, comment, comment_size);
        return true;
    }
    
    MULLI_Instruction mulli;
    if (decode_mulli(instruction, &mulli)) {
        transpile_mulli(&mulli, output, output_size);
        comment_mulli(&mulli, comment, comment_size);
        return true;
    }
    
    MULLW_Instruction mullw;
    if (decode_mullw(instruction, &mullw)) {
        transpile_mullw(&mullw, output, output_size);
        comment_mullw(&mullw, comment, comment_size);
        return true;
    }
    
    MULHWU_Instruction mulhwu;
    if (decode_mulhwu(instruction, &mulhwu)) {
        transpile_mulhwu(&mulhwu, output, output_size);
        comment_mulhwu(&mulhwu, comment, comment_size);
        return true;
    }
    
    // Logical
    AND_Instruction and;
    if (decode_and(instruction, &and)) {
        transpile_and(&and, output, output_size);
        comment_and(&and, comment, comment_size);
        return true;
    }
    
    ANDI_Instruction andi;
    if (decode_andi(instruction, &andi)) {
        transpile_andi(&andi, output, output_size);
        comment_andi(&andi, comment, comment_size);
        return true;
    }
    
    ANDIS_Instruction andis;
    if (decode_andis(instruction, &andis)) {
        transpile_andis(&andis, output, output_size);
        comment_andis(&andis, comment, comment_size);
        return true;
    }
    
    OR_Instruction or;
    if (decode_or(instruction, &or)) {
        transpile_or(&or, output, output_size);
        comment_or(&or, comment, comment_size);
        return true;
    }
    
    ORI_Instruction ori;
    if (decode_ori(instruction, &ori)) {
        transpile_ori(&ori, output, output_size);
        comment_ori(&ori, comment, comment_size);
        return true;
    }
    
    XOR_Instruction xor;
    if (decode_xor(instruction, &xor)) {
        transpile_xor(&xor, output, output_size);
        comment_xor(&xor, comment, comment_size);
        return true;
    }
    
    ORIS_Instruction oris;
    if (decode_oris(instruction, &oris)) {
        transpile_oris(&oris, output, output_size);
        comment_oris(&oris, comment, comment_size);
        return true;
    }
    
    XORIS_Instruction xoris;
    if (decode_xoris(instruction, &xoris)) {
        transpile_xoris(&xoris, output, output_size);
        comment_xoris(&xoris, comment, comment_size);
        return true;
    }
    
    // Shift/Rotate
    SLW_Instruction slw;
    if (decode_slw(instruction, &slw)) {
        transpile_slw(&slw, output, output_size);
        comment_slw(&slw, comment, comment_size);
        return true;
    }
    
    SRW_Instruction srw;
    if (decode_srw(instruction, &srw)) {
        transpile_srw(&srw, output, output_size);
        comment_srw(&srw, comment, comment_size);
        return true;
    }
    
    SRAWI_Instruction srawi;
    if (decode_srawi(instruction, &srawi)) {
        transpile_srawi(&srawi, output, output_size);
        comment_srawi(&srawi, comment, comment_size);
        return true;
    }
    
    RLWINM_Instruction rlwinm;
    if (decode_rlwinm(instruction, &rlwinm)) {
        transpile_rlwinm(&rlwinm, output, output_size);
        comment_rlwinm(&rlwinm, comment, comment_size);
        return true;
    }
    
    RLWNM_Instruction rlwnm;
    if (decode_rlwnm(instruction, &rlwnm)) {
        transpile_rlwnm(&rlwnm, output, output_size);
        comment_rlwnm(&rlwnm, comment, comment_size);
        return true;
    }

    // Comparison
    CMP_Instruction cmp;
    if (decode_cmp(instruction, &cmp)) {
        transpile_cmp(&cmp, output, output_size);
        comment_cmp(&cmp, comment, comment_size);
        return true;
    }
    
    CMPI_Instruction cmpi;
    if (decode_cmpi(instruction, &cmpi)) {
        transpile_cmpi(&cmpi, output, output_size);
        comment_cmpi(&cmpi, comment, comment_size);
        return true;
    }
    
    CMPLW_Instruction cmplw;
    if (decode_cmplw(instruction, &cmplw)) {
        transpile_cmplw(&cmplw, output, output_size);
        comment_cmplw(&cmplw, comment, comment_size);
        return true;
    }
    
    CMPLWI_Instruction cmplwi;
    if (decode_cmplwi(instruction, &cmplwi)) {
        transpile_cmplwi(&cmplwi, output, output_size);
        comment_cmplwi(&cmplwi, comment, comment_size);
        return true;
    }
    
    // Branch
    B_Instruction b;
    if (decode_b(instruction, &b)) {
        transpile_b(&b, address, output, output_size);
        comment_b(&b, address, comment, comment_size);
        return true;
    }
    
    BC_Instruction bc;
    if (decode_bc(instruction, &bc)) {
        transpile_bc(&bc, address, output, output_size);
        comment_bc(&bc, address, comment, comment_size);
        return true;
    }
    
    BLR_Instruction blr;
    if (decode_blr(instruction, &blr)) {
        transpile_blr(&blr, address, output, output_size, lookup_function_by_address);
        comment_blr(&blr, comment, comment_size);
        return true;
    }
    
    // Load/Store
    LBZ_Instruction lbz;
    if (decode_lbz(instruction, &lbz)) {
        transpile_lbz(&lbz, output, output_size);
        comment_lbz(&lbz, comment, comment_size);
        return true;
    }
    
    STB_Instruction stb;
    if (decode_stb(instruction, &stb)) {
        transpile_stb(&stb, output, output_size);
        comment_stb(&stb, comment, comment_size);
        return true;
    }
    
    LHZ_Instruction lhz;
    if (decode_lhz(instruction, &lhz)) {
        transpile_lhz(&lhz, output, output_size);
        comment_lhz(&lhz, comment, comment_size);
        return true;
    }
    
    STH_Instruction sth;
    if (decode_sth(instruction, &sth)) {
        transpile_sth(&sth, output, output_size);
        comment_sth(&sth, comment, comment_size);
        return true;
    }
    
    LWZ_Instruction lwz;
    if (decode_lwz(instruction, &lwz)) {
        transpile_lwz(&lwz, output, output_size);
        comment_lwz(&lwz, comment, comment_size);
        return true;
    }
    
    LWZU_Instruction lwzu;
    if (decode_lwzu(instruction, &lwzu)) {
        transpile_lwzu(&lwzu, output, output_size);
        comment_lwzu(&lwzu, comment, comment_size);
        return true;
    }
    
    LWZX_Instruction lwzx;
    if (decode_lwzx(instruction, &lwzx)) {
        transpile_lwzx(&lwzx, output, output_size);
        comment_lwzx(&lwzx, comment, comment_size);
        return true;
    }
    
    STW_Instruction stw;
    if (decode_stw(instruction, &stw)) {
        transpile_stw(&stw, output, output_size);
        comment_stw(&stw, comment, comment_size);
        return true;
    }
    
    STWU_Instruction stwu;
    if (decode_stwu(instruction, &stwu)) {
        transpile_stwu(&stwu, output, output_size);
        comment_stwu(&stwu, comment, comment_size);
        return true;
    }
    
    LMW_Instruction lmw;
    if (decode_lmw(instruction, &lmw)) {
        transpile_lmw(&lmw, output, output_size);
        comment_lmw(&lmw, comment, comment_size);
        return true;
    }
    
    STMW_Instruction stmw;
    if (decode_stmw(instruction, &stmw)) {
        transpile_stmw(&stmw, output, output_size);
        comment_stmw(&stmw, comment, comment_size);
        return true;
    }
    
    // SPR
    MFSPR_Instruction mfspr;
    if (decode_mfspr(instruction, &mfspr)) {
        transpile_mfspr(&mfspr, output, output_size);
        comment_mfspr(&mfspr, comment, comment_size);
        return true;
    }
    
    MTSPR_Instruction mtspr;
    if (decode_mtspr(instruction, &mtspr)) {
        transpile_mtspr(&mtspr, output, output_size);
        comment_mtspr(&mtspr, comment, comment_size);
        return true;
    }
    
    MFCR_Instruction mfcr;
    if (decode_mfcr(instruction, &mfcr)) {
        transpile_mfcr(&mfcr, output, output_size);
        comment_mfcr(&mfcr, comment, comment_size);
        return true;
    }
    
    MFXER_Instruction mfxer;
    if (decode_mfxer(instruction, &mfxer)) {
        transpile_mfxer(&mfxer, output, output_size);
        comment_mfxer(&mfxer, comment, comment_size);
        return true;
    }
    
    MTXER_Instruction mtxer;
    if (decode_mtxer(instruction, &mtxer)) {
        transpile_mtxer(&mtxer, output, output_size);
        comment_mtxer(&mtxer, comment, comment_size);
        return true;
    }
    
    MFLR_Instruction mflr;
    if (decode_mflr(instruction, &mflr)) {
        transpile_mflr(&mflr, output, output_size);
        comment_mflr(&mflr, comment, comment_size);
        return true;
    }
    
    MCRXR_Instruction mcrxr;
    if (decode_mcrxr(instruction, &mcrxr)) {
        transpile_mcrxr(&mcrxr, output, output_size);
        comment_mcrxr(&mcrxr, comment, comment_size);
        return true;
    }
    
    MFMSR_Instruction mfmsr;
    if (decode_mfmsr(instruction, &mfmsr)) {
        transpile_mfmsr(&mfmsr, output, output_size);
        comment_mfmsr(&mfmsr, comment, comment_size);
        return true;
    }
    
    MTMSR_Instruction mtmsr;
    if (decode_mtmsr(instruction, &mtmsr)) {
        transpile_mtmsr(&mtmsr, output, output_size);
        comment_mtmsr(&mtmsr, comment, comment_size);
        return true;
    }
    
    // System
    SYNC_Instruction sync_inst;
    if (decode_sync(instruction, &sync_inst)) {
        transpile_sync(&sync_inst, output, output_size);
        comment_sync(&sync_inst, comment, comment_size);
        return true;
    }
    
    RFI_Instruction rfi;
    if (decode_rfi(instruction, &rfi)) {
        transpile_rfi(&rfi, output, output_size);
        comment_rfi(&rfi, comment, comment_size);
        return true;
    }
    
    // Condition Register
    CRXOR_Instruction crxor;
    if (decode_crxor(instruction, &crxor)) {
        transpile_crxor(&crxor, output, output_size);
        comment_crxor(&crxor, comment, comment_size);
        return true;
    }
    
    // Floating-point
    FADD_Instruction fadd;
    if (decode_fadd(instruction, &fadd)) {
        transpile_fadd(&fadd, output, output_size);
        comment_fadd(&fadd, comment, comment_size);
        return true;
    }
    
    FADDS_Instruction fadds;
    if (decode_fadds(instruction, &fadds)) {
        transpile_fadds(&fadds, output, output_size);
        comment_fadds(&fadds, comment, comment_size);
        return true;
    }
    
    FSUBS_Instruction fsubs;
    if (decode_fsubs(instruction, &fsubs)) {
        transpile_fsubs(&fsubs, output, output_size);
        comment_fsubs(&fsubs, comment, comment_size);
        return true;
    }
    
    FMULS_Instruction fmuls;
    if (decode_fmuls(instruction, &fmuls)) {
        transpile_fmuls(&fmuls, output, output_size);
        comment_fmuls(&fmuls, comment, comment_size);
        return true;
    }
    
    FDIVS_Instruction fdivs;
    if (decode_fdivs(instruction, &fdivs)) {
        transpile_fdivs(&fdivs, output, output_size);
        comment_fdivs(&fdivs, comment, comment_size);
        return true;
    }
    
    FABS_Instruction fabs_inst;
    if (decode_fabs(instruction, &fabs_inst)) {
        transpile_fabs(&fabs_inst, output, output_size);
        comment_fabs(&fabs_inst, comment, comment_size);
        return true;
    }
    
    FRSP_Instruction frsp;
    if (decode_frsp(instruction, &frsp)) {
        transpile_frsp(&frsp, output, output_size);
        comment_frsp(&frsp, comment, comment_size);
        return true;
    }
    
    FMADD_Instruction fmadd;
    if (decode_fmadd(instruction, &fmadd)) {
        transpile_fmadd(&fmadd, output, output_size);
        comment_fmadd(&fmadd, comment, comment_size);
        return true;
    }
    
    FMADDS_Instruction fmadds;
    if (decode_fmadds(instruction, &fmadds)) {
        transpile_fmadds(&fmadds, output, output_size);
        comment_fmadds(&fmadds, comment, comment_size);
        return true;
    }
    
    FMSUB_Instruction fmsub;
    if (decode_fmsub(instruction, &fmsub)) {
        transpile_fmsub(&fmsub, output, output_size);
        comment_fmsub(&fmsub, comment, comment_size);
        return true;
    }
    
    FMSUBS_Instruction fmsubs;
    if (decode_fmsubs(instruction, &fmsubs)) {
        transpile_fmsubs(&fmsubs, output, output_size);
        comment_fmsubs(&fmsubs, comment, comment_size);
        return true;
    }
    
    FNMADD_Instruction fnmadd;
    if (decode_fnmadd(instruction, &fnmadd)) {
        transpile_fnmadd(&fnmadd, output, output_size);
        comment_fnmadd(&fnmadd, comment, comment_size);
        return true;
    }
    
    FNMADDS_Instruction fnmadds;
    if (decode_fnmadds(instruction, &fnmadds)) {
        transpile_fnmadds(&fnmadds, output, output_size);
        comment_fnmadds(&fnmadds, comment, comment_size);
        return true;
    }
    
    FNMSUB_Instruction fnmsub;
    if (decode_fnmsub(instruction, &fnmsub)) {
        transpile_fnmsub(&fnmsub, output, output_size);
        comment_fnmsub(&fnmsub, comment, comment_size);
        return true;
    }
    
    FNMSUBS_Instruction fnmsubs;
    if (decode_fnmsubs(instruction, &fnmsubs)) {
        transpile_fnmsubs(&fnmsubs, output, output_size);
        comment_fnmsubs(&fnmsubs, comment, comment_size);
        return true;
    }
    
    LFS_Instruction lfs;
    if (decode_lfs(instruction, &lfs)) {
        transpile_lfs(&lfs, output, output_size);
        comment_lfs(&lfs, comment, comment_size);
        return true;
    }
    
    LFD_Instruction lfd;
    if (decode_lfd(instruction, &lfd)) {
        transpile_lfd(&lfd, output, output_size);
        comment_lfd(&lfd, comment, comment_size);
        return true;
    }
    
    STFD_Instruction stfd;
    if (decode_stfd(instruction, &stfd)) {
        transpile_stfd(&stfd, output, output_size);
        comment_stfd(&stfd, comment, comment_size);
        return true;
    }
    
    FNABS_Instruction fnabs_inst;
    if (decode_fnabs(instruction, &fnabs_inst)) {
        transpile_fnabs(&fnabs_inst, output, output_size);
        comment_fnabs(&fnabs_inst, comment, comment_size);
        return true;
    }
    
    FSEL_Instruction fsel;
    if (decode_fsel(instruction, &fsel)) {
        transpile_fsel(&fsel, output, output_size);
        comment_fsel(&fsel, comment, comment_size);
        return true;
    }
    
    FRES_Instruction fres;
    if (decode_fres(instruction, &fres)) {
        transpile_fres(&fres, output, output_size);
        comment_fres(&fres, comment, comment_size);
        return true;
    }
    
    FRSQRTE_Instruction frsqrte;
    if (decode_frsqrte(instruction, &frsqrte)) {
        transpile_frsqrte(&frsqrte, output, output_size);
        comment_frsqrte(&frsqrte, comment, comment_size);
        return true;
    }
    
    FCTIW_Instruction fctiw;
    if (decode_fctiw(instruction, &fctiw)) {
        transpile_fctiw(&fctiw, output, output_size);
        comment_fctiw(&fctiw, comment, comment_size);
        return true;
    }
    
    LFSU_Instruction lfsu;
    if (decode_lfsu(instruction, &lfsu)) {
        transpile_lfsu(&lfsu, output, output_size);
        comment_lfsu(&lfsu, comment, comment_size);
        return true;
    }
    
    LFDU_Instruction lfdu;
    if (decode_lfdu(instruction, &lfdu)) {
        transpile_lfdu(&lfdu, output, output_size);
        comment_lfdu(&lfdu, comment, comment_size);
        return true;
    }
    
    LFSX_Instruction lfsx;
    if (decode_lfsx(instruction, &lfsx)) {
        transpile_lfsx(&lfsx, output, output_size);
        comment_lfsx(&lfsx, comment, comment_size);
        return true;
    }
    
    LFDX_Instruction lfdx;
    if (decode_lfdx(instruction, &lfdx)) {
        transpile_lfdx(&lfdx, output, output_size);
        comment_lfdx(&lfdx, comment, comment_size);
        return true;
    }
    
    STFS_Instruction stfs;
    if (decode_stfs(instruction, &stfs)) {
        transpile_stfs(&stfs, output, output_size);
        comment_stfs(&stfs, comment, comment_size);
        return true;
    }
    
    STFSX_Instruction stfsx;
    if (decode_stfsx(instruction, &stfsx)) {
        transpile_stfsx(&stfsx, output, output_size);
        comment_stfsx(&stfsx, comment, comment_size);
        return true;
    }
    
    STFDX_Instruction stfdx;
    if (decode_stfdx(instruction, &stfdx)) {
        transpile_stfdx(&stfdx, output, output_size);
        comment_stfdx(&stfdx, comment, comment_size);
        return true;
    }
    
    STFIWX_Instruction stfiwx;
    if (decode_stfiwx(instruction, &stfiwx)) {
        transpile_stfiwx(&stfiwx, output, output_size);
        comment_stfiwx(&stfiwx, comment, comment_size);
        return true;
    }
    
    STFSU_Instruction stfsu;
    if (decode_stfsu(instruction, &stfsu)) {
        transpile_stfsu(&stfsu, output, output_size);
        comment_stfsu(&stfsu, comment, comment_size);
        return true;
    }
    
    STFDU_Instruction stfdu;
    if (decode_stfdu(instruction, &stfdu)) {
        transpile_stfdu(&stfdu, output, output_size);
        comment_stfdu(&stfdu, comment, comment_size);
        return true;
    }
    
    LFSUX_Instruction lfsux;
    if (decode_lfsux(instruction, &lfsux)) {
        transpile_lfsux(&lfsux, output, output_size);
        comment_lfsux(&lfsux, comment, comment_size);
        return true;
    }
    
    LFDUX_Instruction lfdux;
    if (decode_lfdux(instruction, &lfdux)) {
        transpile_lfdux(&lfdux, output, output_size);
        comment_lfdux(&lfdux, comment, comment_size);
        return true;
    }
    
    STFSUX_Instruction stfsux;
    if (decode_stfsux(instruction, &stfsux)) {
        transpile_stfsux(&stfsux, output, output_size);
        comment_stfsux(&stfsux, comment, comment_size);
        return true;
    }
    
    STFDUX_Instruction stfdux;
    if (decode_stfdux(instruction, &stfdux)) {
        transpile_stfdux(&stfdux, output, output_size);
        comment_stfdux(&stfdux, comment, comment_size);
        return true;
    }
    
    LHZU_Instruction lhzu;
    if (decode_lhzu(instruction, &lhzu)) {
        transpile_lhzu(&lhzu, output, output_size);
        comment_lhzu(&lhzu, comment, comment_size);
        return true;
    }
    
    RLWIMI_Instruction rlwimi;
    if (decode_rlwimi(instruction, &rlwimi)) {
        transpile_rlwimi(&rlwimi, output, output_size);
        comment_rlwimi(&rlwimi, comment, comment_size);
        return true;
    }
    
    // Cache operations
    DCBF_Instruction dcbf;
    if (decode_dcbf(instruction, &dcbf)) {
        transpile_dcbf(&dcbf, output, output_size);
        comment_dcbf(&dcbf, comment, comment_size);
        return true;
    }
    
    DCBI_Instruction dcbi;
    if (decode_dcbi(instruction, &dcbi)) {
        transpile_dcbi(&dcbi, output, output_size);
        comment_dcbi(&dcbi, comment, comment_size);
        return true;
    }
    
    DCBST_Instruction dcbst;
    if (decode_dcbst(instruction, &dcbst)) {
        transpile_dcbst(&dcbst, output, output_size);
        comment_dcbst(&dcbst, comment, comment_size);
        return true;
    }
    
    ICBI_Instruction icbi;
    if (decode_icbi(instruction, &icbi)) {
        transpile_icbi(&icbi, output, output_size);
        comment_icbi(&icbi, comment, comment_size);
        return true;
    }
    
    DCBT_Instruction dcbt;
    if (decode_dcbt(instruction, &dcbt)) {
        transpile_dcbt(&dcbt, output, output_size);
        comment_dcbt(&dcbt, comment, comment_size);
        return true;
    }
    
    DCBTST_Instruction dcbtst;
    if (decode_dcbtst(instruction, &dcbtst)) {
        transpile_dcbtst(&dcbtst, output, output_size);
        comment_dcbtst(&dcbtst, comment, comment_size);
        return true;
    }
    
    DCBZ_Instruction dcbz;
    if (decode_dcbz(instruction, &dcbz)) {
        transpile_dcbz(&dcbz, output, output_size);
        comment_dcbz(&dcbz, comment, comment_size);
        return true;
    }
    
    // System
    ISYNC_Instruction isync;
    if (decode_isync(instruction, &isync)) {
        transpile_isync(&isync, output, output_size);
        comment_isync(&isync, comment, comment_size);
        return true;
    }
    
    EIEIO_Instruction eieio;
    if (decode_eieio(instruction, &eieio)) {
        transpile_eieio(&eieio, output, output_size);
        comment_eieio(&eieio, comment, comment_size);
        return true;
    }
    
    SC_Instruction sc;
    if (decode_sc(instruction, &sc)) {
        transpile_sc(&sc, output, output_size);
        comment_sc(&sc, comment, comment_size);
        return true;
    }
    
    TW_Instruction tw;
    if (decode_tw(instruction, &tw)) {
        transpile_tw(&tw, output, output_size);
        comment_tw(&tw, comment, comment_size);
        return true;
    }
    
    TWI_Instruction twi;
    if (decode_twi(instruction, &twi)) {
        transpile_twi(&twi, output, output_size);
        comment_twi(&twi, comment, comment_size);
        return true;
    }
    
    // FP Status
    MTFSF_Instruction mtfsf;
    if (decode_mtfsf(instruction, &mtfsf)) {
        transpile_mtfsf(&mtfsf, output, output_size);
        comment_mtfsf(&mtfsf, comment, comment_size);
        return true;
    }
    
    // Gekko Paired-Single
    PSQ_L_Instruction psq_l;
    if (decode_psq_l(instruction, &psq_l)) {
        transpile_psq_l(&psq_l, output, output_size);
        comment_psq_l(&psq_l, comment, comment_size);
        return true;
    }
    
    PSQ_ST_Instruction psq_st;
    if (decode_psq_st(instruction, &psq_st)) {
        transpile_psq_st(&psq_st, output, output_size);
        comment_psq_st(&psq_st, comment, comment_size);
        return true;
    }
    
    // More branches
    BCTR_Instruction bctr;
    if (decode_bctr(instruction, &bctr)) {
        transpile_bctr(&bctr, address, output, output_size, lookup_function_by_address);
        comment_bctr(&bctr, comment, comment_size);
        return true;
    }
    
    // More loads
    LHA_Instruction lha;
    if (decode_lha(instruction, &lha)) {
        transpile_lha(&lha, output, output_size);
        comment_lha(&lha, comment, comment_size);
        return true;
    }
    
    // Extended/Count ops
    EXTSH_Instruction extsh;
    if (decode_extsh(instruction, &extsh)) {
        transpile_extsh(&extsh, output, output_size);
        comment_extsh(&extsh, comment, comment_size);
        return true;
    }
    
    CNTLZW_Instruction cntlzw;
    if (decode_cntlzw(instruction, &cntlzw)) {
        transpile_cntlzw(&cntlzw, output, output_size);
        comment_cntlzw(&cntlzw, comment, comment_size);
        return true;
    }
    
    ANDC_Instruction andc;
    if (decode_andc(instruction, &andc)) {
        transpile_andc(&andc, output, output_size);
        comment_andc(&andc, comment, comment_size);
        return true;
    }
    
    // More SPR
    MTCRF_Instruction mtcrf;
    if (decode_mtcrf(instruction, &mtcrf)) {
        transpile_mtcrf(&mtcrf, output, output_size);
        comment_mtcrf(&mtcrf, comment, comment_size);
        return true;
    }
    
    MFTB_Instruction mftb;
    if (decode_mftb(instruction, &mftb)) {
        transpile_mftb(&mftb, output, output_size);
        comment_mftb(&mftb, comment, comment_size);
        return true;
    }
    
    // More FP
    MFFS_Instruction mffs;
    if (decode_mffs(instruction, &mffs)) {
        transpile_mffs(&mffs, output, output_size);
        comment_mffs(&mffs, comment, comment_size);
        return true;
    }
    
    // More arithmetic
    SUBFIC_Instruction subfic;
    if (decode_subfic(instruction, &subfic)) {
        transpile_subfic(&subfic, output, output_size);
        comment_subfic(&subfic, comment, comment_size);
        return true;
    }
    
    ADDZE_Instruction addze;
    if (decode_addze(instruction, &addze)) {
        transpile_addze(&addze, output, output_size);
        comment_addze(&addze, comment, comment_size);
        return true;
    }
    
    ADDME_Instruction addme;
    if (decode_addme(instruction, &addme)) {
        transpile_addme(&addme, output, output_size);
        comment_addme(&addme, comment, comment_size);
        return true;
    }
    
    MULHW_Instruction mulhw;
    if (decode_mulhw(instruction, &mulhw)) {
        transpile_mulhw(&mulhw, output, output_size);
        comment_mulhw(&mulhw, comment, comment_size);
        return true;
    }
    
    DIVW_Instruction divw;
    if (decode_divw(instruction, &divw)) {
        transpile_divw(&divw, output, output_size);
        comment_divw(&divw, comment, comment_size);
        return true;
    }
    
    DIVWU_Instruction divwu;
    if (decode_divwu(instruction, &divwu)) {
        transpile_divwu(&divwu, output, output_size);
        comment_divwu(&divwu, comment, comment_size);
        return true;
    }
    
    // More logical
    NOR_Instruction nor;
    if (decode_nor(instruction, &nor)) {
        transpile_nor(&nor, output, output_size);
        comment_nor(&nor, comment, comment_size);
        return true;
    }
    
    NAND_Instruction nand;
    if (decode_nand(instruction, &nand)) {
        transpile_nand(&nand, output, output_size);
        comment_nand(&nand, comment, comment_size);
        return true;
    }
    
    ORC_Instruction orc;
    if (decode_orc(instruction, &orc)) {
        transpile_orc(&orc, output, output_size);
        comment_orc(&orc, comment, comment_size);
        return true;
    }
    
    EXTSB_Instruction extsb;
    if (decode_extsb(instruction, &extsb)) {
        transpile_extsb(&extsb, output, output_size);
        comment_extsb(&extsb, comment, comment_size);
        return true;
    }
    
    // More shift
    SRAW_Instruction sraw;
    if (decode_sraw(instruction, &sraw)) {
        transpile_sraw(&sraw, output, output_size);
        comment_sraw(&sraw, comment, comment_size);
        return true;
    }
    
    // More indexed loads/stores
    LHZX_Instruction lhzx;
    if (decode_lhzx(instruction, &lhzx)) {
        transpile_lhzx(&lhzx, output, output_size);
        comment_lhzx(&lhzx, comment, comment_size);
        return true;
    }
    
    STHX_Instruction sthx;
    if (decode_sthx(instruction, &sthx)) {
        transpile_sthx(&sthx, output, output_size);
        comment_sthx(&sthx, comment, comment_size);
        return true;
    }
    
    LHAX_Instruction lhax;
    if (decode_lhax(instruction, &lhax)) {
        transpile_lhax(&lhax, output, output_size);
        comment_lhax(&lhax, comment, comment_size);
        return true;
    }
    
    LHAU_Instruction lhau;
    if (decode_lhau(instruction, &lhau)) {
        transpile_lhau(&lhau, output, output_size);
        comment_lhau(&lhau, comment, comment_size);
        return true;
    }
    
    LHBRX_Instruction lhbrx;
    if (decode_lhbrx(instruction, &lhbrx)) {
        transpile_lhbrx(&lhbrx, output, output_size);
        comment_lhbrx(&lhbrx, comment, comment_size);
        return true;
    }
    
    STHBRX_Instruction sthbrx;
    if (decode_sthbrx(instruction, &sthbrx)) {
        transpile_sthbrx(&sthbrx, output, output_size);
        comment_sthbrx(&sthbrx, comment, comment_size);
        return true;
    }
    
    LWBRX_Instruction lwbrx;
    if (decode_lwbrx(instruction, &lwbrx)) {
        transpile_lwbrx(&lwbrx, output, output_size);
        comment_lwbrx(&lwbrx, comment, comment_size);
        return true;
    }
    
    STWBRX_Instruction stwbrx;
    if (decode_stwbrx(instruction, &stwbrx)) {
        transpile_stwbrx(&stwbrx, output, output_size);
        comment_stwbrx(&stwbrx, comment, comment_size);
        return true;
    }
    
    STHU_Instruction sthu;
    if (decode_sthu(instruction, &sthu)) {
        transpile_sthu(&sthu, output, output_size);
        comment_sthu(&sthu, comment, comment_size);
        return true;
    }
    
    STWX_Instruction stwx;
    if (decode_stwx(instruction, &stwx)) {
        transpile_stwx(&stwx, output, output_size);
        comment_stwx(&stwx, comment, comment_size);
        return true;
    }
    
    // Byte with update
    LBZU_Instruction lbzu;
    if (decode_lbzu(instruction, &lbzu)) {
        transpile_lbzu(&lbzu, output, output_size);
        comment_lbzu(&lbzu, comment, comment_size);
        return true;
    }
    
    STBU_Instruction stbu;
    if (decode_stbu(instruction, &stbu)) {
        transpile_stbu(&stbu, output, output_size);
        comment_stbu(&stbu, comment, comment_size);
        return true;
    }
    
    // More arithmetic with carry
    ADDIC_Instruction addic;
    if (decode_addic(instruction, &addic)) {
        transpile_addic(&addic, output, output_size);
        comment_addic(&addic, comment, comment_size);
        return true;
    }
    
    SUBFZE_Instruction subfze;
    if (decode_subfze(instruction, &subfze)) {
        transpile_subfze(&subfze, output, output_size);
        comment_subfze(&subfze, comment, comment_size);
        return true;
    }
    
    SUBFME_Instruction subfme;
    if (decode_subfme(instruction, &subfme)) {
        transpile_subfme(&subfme, output, output_size);
        comment_subfme(&subfme, comment, comment_size);
        return true;
    }
    
    // Floating-point arithmetic
    FSUB_Instruction fsub;
    if (decode_fsub(instruction, &fsub)) {
        transpile_fsub(&fsub, output, output_size);
        comment_fsub(&fsub, comment, comment_size);
        return true;
    }
    
    FMUL_Instruction fmul;
    if (decode_fmul(instruction, &fmul)) {
        transpile_fmul(&fmul, output, output_size);
        comment_fmul(&fmul, comment, comment_size);
        return true;
    }
    
    FDIV_Instruction fdiv;
    if (decode_fdiv(instruction, &fdiv)) {
        transpile_fdiv(&fdiv, output, output_size);
        comment_fdiv(&fdiv, comment, comment_size);
        return true;
    }
    
    // FP move/negate/convert
    FMR_Instruction fmr;
    if (decode_fmr(instruction, &fmr)) {
        transpile_fmr(&fmr, output, output_size);
        comment_fmr(&fmr, comment, comment_size);
        return true;
    }
    
    FNEG_Instruction fneg;
    if (decode_fneg(instruction, &fneg)) {
        transpile_fneg(&fneg, output, output_size);
        comment_fneg(&fneg, comment, comment_size);
        return true;
    }
    
    FCTIWZ_Instruction fctiwz;
    if (decode_fctiwz(instruction, &fctiwz)) {
        transpile_fctiwz(&fctiwz, output, output_size);
        comment_fctiwz(&fctiwz, comment, comment_size);
        return true;
    }
    
    // FP compare
    FCMPU_Instruction fcmpu;
    if (decode_fcmpu(instruction, &fcmpu)) {
        transpile_fcmpu(&fcmpu, output, output_size);
        comment_fcmpu(&fcmpu, comment, comment_size);
        return true;
    }
    
    FCMPO_Instruction fcmpo;
    if (decode_fcmpo(instruction, &fcmpo)) {
        transpile_fcmpo(&fcmpo, output, output_size);
        comment_fcmpo(&fcmpo, comment, comment_size);
        return true;
    }
    
    // CR ops
    CROR_Instruction cror;
    if (decode_cror(instruction, &cror)) {
        transpile_cror(&cror, output, output_size);
        comment_cror(&cror, comment, comment_size);
        return true;
    }
    
    CRAND_Instruction crand;
    if (decode_crand(instruction, &crand)) {
        transpile_crand(&crand, output, output_size);
        comment_crand(&crand, comment, comment_size);
        return true;
    }
    
    CRANDC_Instruction crandc;
    if (decode_crandc(instruction, &crandc)) {
        transpile_crandc(&crandc, output, output_size);
        comment_crandc(&crandc, comment, comment_size);
        return true;
    }
    
    CREQV_Instruction creqv;
    if (decode_creqv(instruction, &creqv)) {
        transpile_creqv(&creqv, output, output_size);
        comment_creqv(&creqv, comment, comment_size);
        return true;
    }
    
    CRNAND_Instruction crnand;
    if (decode_crnand(instruction, &crnand)) {
        transpile_crnand(&crnand, output, output_size);
        comment_crnand(&crnand, comment, comment_size);
        return true;
    }
    
    CRNOR_Instruction crnor;
    if (decode_crnor(instruction, &crnor)) {
        transpile_crnor(&crnor, output, output_size);
        comment_crnor(&crnor, comment, comment_size);
        return true;
    }
    
    CRORC_Instruction crorc;
    if (decode_crorc(instruction, &crorc)) {
        transpile_crorc(&crorc, output, output_size);
        comment_crorc(&crorc, comment, comment_size);
        return true;
    }
    
    MCRF_Instruction mcrf;
    if (decode_mcrf(instruction, &mcrf)) {
        transpile_mcrf(&mcrf, output, output_size);
        comment_mcrf(&mcrf, comment, comment_size);
        return true;
    }

    // Final logical ops
    EQV_Instruction eqv;
    if (decode_eqv(instruction, &eqv)) {
        transpile_eqv(&eqv, output, output_size);
        comment_eqv(&eqv, comment, comment_size);
        return true;
    }
    
    XORI_Instruction xori;
    if (decode_xori(instruction, &xori)) {
        transpile_xori(&xori, output, output_size);
        comment_xori(&xori, comment, comment_size);
        return true;
    }
    
    // Indexed byte operations
    LBZX_Instruction lbzx;
    if (decode_lbzx(instruction, &lbzx)) {
        transpile_lbzx(&lbzx, output, output_size);
        comment_lbzx(&lbzx, comment, comment_size);
        return true;
    }
    
    STBX_Instruction stbx;
    if (decode_stbx(instruction, &stbx)) {
        transpile_stbx(&stbx, output, output_size);
        comment_stbx(&stbx, comment, comment_size);
        return true;
    }
    
    LBZUX_Instruction lbzux;
    if (decode_lbzux(instruction, &lbzux)) {
        transpile_lbzux(&lbzux, output, output_size);
        comment_lbzux(&lbzux, comment, comment_size);
        return true;
    }
    
    STBUX_Instruction stbux;
    if (decode_stbux(instruction, &stbux)) {
        transpile_stbux(&stbux, output, output_size);
        comment_stbux(&stbux, comment, comment_size);
        return true;
    }
    
    LHZUX_Instruction lhzux;
    if (decode_lhzux(instruction, &lhzux)) {
        transpile_lhzux(&lhzux, output, output_size);
        comment_lhzux(&lhzux, comment, comment_size);
        return true;
    }
    
    LHAUX_Instruction lhaux;
    if (decode_lhaux(instruction, &lhaux)) {
        transpile_lhaux(&lhaux, output, output_size);
        comment_lhaux(&lhaux, comment, comment_size);
        return true;
    }
    
    STHUX_Instruction sthux;
    if (decode_sthux(instruction, &sthux)) {
        transpile_sthux(&sthux, output, output_size);
        comment_sthux(&sthux, comment, comment_size);
        return true;
    }
    
    LWZUX_Instruction lwzux;
    if (decode_lwzux(instruction, &lwzux)) {
        transpile_lwzux(&lwzux, output, output_size);
        comment_lwzux(&lwzux, comment, comment_size);
        return true;
    }
    
    STWUX_Instruction stwux;
    if (decode_stwux(instruction, &stwux)) {
        transpile_stwux(&stwux, output, output_size);
        comment_stwux(&stwux, comment, comment_size);
        return true;
    }
    
    // Segment register operations
    MFSR_Instruction mfsr;
    if (decode_mfsr(instruction, &mfsr)) {
        transpile_mfsr(&mfsr, output, output_size);
        comment_mfsr(&mfsr, comment, comment_size);
        return true;
    }
    
    MTSR_Instruction mtsr;
    if (decode_mtsr(instruction, &mtsr)) {
        transpile_mtsr(&mtsr, output, output_size);
        comment_mtsr(&mtsr, comment, comment_size);
        return true;
    }
    
    // Paired-Single Operations
    PS_ABS_Instruction ps_abs;
    if (decode_ps_abs(instruction, &ps_abs)) {
        transpile_ps_abs(&ps_abs, output, output_size);
        comment_ps_abs(&ps_abs, comment, comment_size);
        return true;
    }
    
    PS_NEG_Instruction ps_neg;
    if (decode_ps_neg(instruction, &ps_neg)) {
        transpile_ps_neg(&ps_neg, output, output_size);
        comment_ps_neg(&ps_neg, comment, comment_size);
        return true;
    }
    
    PS_NABS_Instruction ps_nabs;
    if (decode_ps_nabs(instruction, &ps_nabs)) {
        transpile_ps_nabs(&ps_nabs, output, output_size);
        comment_ps_nabs(&ps_nabs, comment, comment_size);
        return true;
    }
    
    PS_MR_Instruction ps_mr;
    if (decode_ps_mr(instruction, &ps_mr)) {
        transpile_ps_mr(&ps_mr, output, output_size);
        comment_ps_mr(&ps_mr, comment, comment_size);
        return true;
    }
    
    PS_CMPU0_Instruction ps_cmpu0;
    if (decode_ps_cmpu0(instruction, &ps_cmpu0)) {
        transpile_ps_cmpu0(&ps_cmpu0, output, output_size);
        comment_ps_cmpu0(&ps_cmpu0, comment, comment_size);
        return true;
    }
    
    PS_CMPU1_Instruction ps_cmpu1;
    if (decode_ps_cmpu1(instruction, &ps_cmpu1)) {
        transpile_ps_cmpu1(&ps_cmpu1, output, output_size);
        comment_ps_cmpu1(&ps_cmpu1, comment, comment_size);
        return true;
    }
    
    PS_CMPO0_Instruction ps_cmpo0;
    if (decode_ps_cmpo0(instruction, &ps_cmpo0)) {
        transpile_ps_cmpo0(&ps_cmpo0, output, output_size);
        comment_ps_cmpo0(&ps_cmpo0, comment, comment_size);
        return true;
    }
    
    PS_CMPO1_Instruction ps_cmpo1;
    if (decode_ps_cmpo1(instruction, &ps_cmpo1)) {
        transpile_ps_cmpo1(&ps_cmpo1, output, output_size);
        comment_ps_cmpo1(&ps_cmpo1, comment, comment_size);
        return true;
    }
    
    PS_SEL_Instruction ps_sel;
    if (decode_ps_sel(instruction, &ps_sel)) {
        transpile_ps_sel(&ps_sel, output, output_size);
        comment_ps_sel(&ps_sel, comment, comment_size);
        return true;
    }
    
    PS_RES_Instruction ps_res;
    if (decode_ps_res(instruction, &ps_res)) {
        transpile_ps_res(&ps_res, output, output_size);
        comment_ps_res(&ps_res, comment, comment_size);
        return true;
    }
    
    PS_RSQRTE_Instruction ps_rsqrte;
    if (decode_ps_rsqrte(instruction, &ps_rsqrte)) {
        transpile_ps_rsqrte(&ps_rsqrte, output, output_size);
        comment_ps_rsqrte(&ps_rsqrte, comment, comment_size);
        return true;
    }
    
    PS_NMADD_Instruction ps_nmadd;
    if (decode_ps_nmadd(instruction, &ps_nmadd)) {
        transpile_ps_nmadd(&ps_nmadd, output, output_size);
        comment_ps_nmadd(&ps_nmadd, comment, comment_size);
        return true;
    }
    
    PS_NMSUB_Instruction ps_nmsub;
    if (decode_ps_nmsub(instruction, &ps_nmsub)) {
        transpile_ps_nmsub(&ps_nmsub, output, output_size);
        comment_ps_nmsub(&ps_nmsub, comment, comment_size);
        return true;
    }
    
    PS_SUM0_Instruction ps_sum0;
    if (decode_ps_sum0(instruction, &ps_sum0)) {
        transpile_ps_sum0(&ps_sum0, output, output_size);
        comment_ps_sum0(&ps_sum0, comment, comment_size);
        return true;
    }
    
    PS_SUM1_Instruction ps_sum1;
    if (decode_ps_sum1(instruction, &ps_sum1)) {
        transpile_ps_sum1(&ps_sum1, output, output_size);
        comment_ps_sum1(&ps_sum1, comment, comment_size);
        return true;
    }
    
    PS_MULS0_Instruction ps_muls0;
    if (decode_ps_muls0(instruction, &ps_muls0)) {
        transpile_ps_muls0(&ps_muls0, output, output_size);
        comment_ps_muls0(&ps_muls0, comment, comment_size);
        return true;
    }
    
    PS_MULS1_Instruction ps_muls1;
    if (decode_ps_muls1(instruction, &ps_muls1)) {
        transpile_ps_muls1(&ps_muls1, output, output_size);
        comment_ps_muls1(&ps_muls1, comment, comment_size);
        return true;
    }
    
    PS_MADDS0_Instruction ps_madds0;
    if (decode_ps_madds0(instruction, &ps_madds0)) {
        transpile_ps_madds0(&ps_madds0, output, output_size);
        comment_ps_madds0(&ps_madds0, comment, comment_size);
        return true;
    }
    
    PS_MADDS1_Instruction ps_madds1;
    if (decode_ps_madds1(instruction, &ps_madds1)) {
        transpile_ps_madds1(&ps_madds1, output, output_size);
        comment_ps_madds1(&ps_madds1, comment, comment_size);
        return true;
    }
    
    PSQ_LU_Instruction psq_lu;
    if (decode_psq_lu(instruction, &psq_lu)) {
        transpile_psq_lu(&psq_lu, output, output_size);
        comment_psq_lu(&psq_lu, comment, comment_size);
        return true;
    }
    
    PSQ_STU_Instruction psq_stu;
    if (decode_psq_stu(instruction, &psq_stu)) {
        transpile_psq_stu(&psq_stu, output, output_size);
        comment_psq_stu(&psq_stu, comment, comment_size);
        return true;
    }
    
    PSQ_LX_Instruction psq_lx;
    if (decode_psq_lx(instruction, &psq_lx)) {
        transpile_psq_lx(&psq_lx, output, output_size);
        comment_psq_lx(&psq_lx, comment, comment_size);
        return true;
    }
    
    PSQ_STX_Instruction psq_stx;
    if (decode_psq_stx(instruction, &psq_stx)) {
        transpile_psq_stx(&psq_stx, output, output_size);
        comment_psq_stx(&psq_stx, comment, comment_size);
        return true;
    }
    
    PSQ_LUX_Instruction psq_lux;
    if (decode_psq_lux(instruction, &psq_lux)) {
        transpile_psq_lux(&psq_lux, output, output_size);
        comment_psq_lux(&psq_lux, comment, comment_size);
        return true;
    }
    
    PSQ_STUX_Instruction psq_stux;
    if (decode_psq_stux(instruction, &psq_stux)) {
        transpile_psq_stux(&psq_stux, output, output_size);
        comment_psq_stux(&psq_stux, comment, comment_size);
        return true;
    }
    
    // Unknown instruction
    snprintf(output, output_size, "/* UNKNOWN OPCODE */");
    snprintf(comment, comment_size, "0x%08X", instruction);
    return false;
}

/**
 * @brief Transpile a single .s file
 */
int transpile_file(const char *input_filename, SkipList *skip_list) {
    printf("Processing: %s\n", input_filename);
    
    // Build label-to-function map for trampoline resolution
    LabelMap *label_map = build_label_map(input_filename);
    
    // Build string table for string literal tracking
    StringTable *string_table = build_string_table(input_filename);
    
    // Generate output filenames
    char output_c[256], output_h[256];
    generate_output_filenames(input_filename, output_c, output_h, sizeof(output_c));
    
    // Open input file
    FILE *input = fopen(input_filename, "r");
    if (!input) {
        fprintf(stderr, "Error: Cannot open input file %s\n", input_filename);
        return -1;
    }
    
    // Open output files
    FILE *c_file = fopen(output_c, "w");
    FILE *h_file = fopen(output_h, "w");
    
    if (!c_file || !h_file) {
        fprintf(stderr, "Error: Cannot create output files\n");
        if (input) fclose(input);
        if (c_file) fclose(c_file);
        if (h_file) fclose(h_file);
        return -1;
    }
    
    // Extract base name for header guard
    char guard_name[128];
    const char *base = strrchr(input_filename, '/');
    if (!base) base = strrchr(input_filename, '\\');
    if (!base) base = input_filename;
    else base++;
    
    strncpy(guard_name, base, sizeof(guard_name) - 1);
    char *dot = strchr(guard_name, '.');
    if (dot) *dot = '\0';
    
    // Convert to uppercase for guard and replace special chars with underscore
    for (char *p = guard_name; *p; p++) {
        if (*p == '.' || *p == '-' || *p == ' ' || *p == '(' || *p == ')') {
            *p = '_';
        } else {
            *p = toupper(*p);
        }
    }
    
    // Write file headers
    write_header_start(h_file, guard_name);
    write_c_file_start(c_file, output_h);
    
    // Process file line by line
    char line[MAX_LINE_LENGTH];
    bool in_function = false;
    bool in_data_section = false;
    Function_Info current_func = {0};
    
    // Register tracker for compile-time function pointer resolution
    RegisterTracker register_tracker;
    register_tracker_init(&register_tracker);
    
    // Buffer to track previous lines for parameter detection
    char line_buffer[MAX_LOOKBACK_LINES][MAX_LINE_LENGTH];
    const char *prev_lines[MAX_LOOKBACK_LINES];
    int line_index = 0;
    int lines_buffered = 0;
    
    // Initialize pointers
    for (int i = 0; i < MAX_LOOKBACK_LINES; i++) {
        prev_lines[i] = line_buffer[i];
        line_buffer[i][0] = '\0';
    }
    
    while (fgets(line, sizeof(line), input)) {
        ASM_Line parsed;
        
        if (!parse_asm_line(line, &parsed)) {
            // Failed to parse, write as comment
            fprintf(c_file, "    // %s", line);
            continue;
        }
        
        // Handle directives
        if (parsed.is_comment) {
            continue;  // Skip pure comment lines
        }
        
        if (parsed.is_directive) {
            // Handle .include
            if (strstr(line, ".include") != NULL) {
                char include_line[256];
                convert_include(line, include_line, sizeof(include_line));
                fprintf(h_file, "%s\n", include_line);
            }
            // .endfn marks end of function
            if (strstr(line, ".endfn") != NULL && in_function) {
                // Add trampoline fix instructions if detected
                if (current_func.is_trampoline && label_map) {
                    const char *target_func = labelmap_find_function(label_map, current_func.trampoline_target);
                    fprintf(c_file, "    /* TRAMPOLINE DETECTED - Cross-function jump to 0x%08X\n", 
                           current_func.trampoline_target);
                    fprintf(c_file, "     * Auto-fix: Replace the goto above with:\n");
                    fprintf(c_file, "     *   pc = 0x%08X;\n", current_func.trampoline_target);
                    if (target_func) {
                        fprintf(c_file, "     *   %s();  // Function containing L_%08X\n", 
                               target_func, current_func.trampoline_target);
                    } else {
                        fprintf(c_file, "     *   TARGET_FUNCTION();  // Function containing L_%08X (not found)\n", 
                               current_func.trampoline_target);
                    }
                    fprintf(c_file, "     * Then add to target function start:\n");
                    fprintf(c_file, "     *   if (pc == 0x%08X) goto L_%08X;\n", 
                           current_func.trampoline_target, current_func.trampoline_target);
                    fprintf(c_file, "     */\n");
                }
                write_function_end(c_file);
                in_function = false;
            }
            continue;
        }
        
        // Handle .data section
        if (parsed.is_data) {
            in_data_section = true;
            fprintf(c_file, "\n// === DATA SECTION ===\n");
            fprintf(c_file, "// (Data sections preserved as byte arrays)\n\n");
            continue;
        }
        
        // Handle function start
        if (parsed.is_function) {
            strcpy(current_func.name, parsed.function_name);
            // Strip quotes from function name if present
            if (current_func.name[0] == '"') {
                size_t len = strlen(current_func.name);
                memmove(current_func.name, current_func.name + 1, len);
                len = strlen(current_func.name);
                if (len > 0 && current_func.name[len - 1] == '"') {
                    current_func.name[len - 1] = '\0';
                }
            }
            
            current_func.start_address = 0;  // Will be set by first instruction
            current_func.instruction_count = 0;
            current_func.is_trampoline = false;
            current_func.trampoline_target = 0;
            current_func.is_local = parsed.is_local_function;  // Track if static
            current_func.is_data_only = false;  // Not used in this function but initialize anyway
            
            // Auto-skip gap functions (usually data misidentified as code)
            if (strncmp(current_func.name, "gap_", 4) == 0) {
                current_func.skip = true;
            } else {
                // Check skip list first
                current_func.skip = skiplist_should_skip(skip_list, current_func.name);
                // Also skip SDK functions (even if not in skip list)
                if (!current_func.skip && is_sdk_or_stdlib_function(current_func.name)) {
                    current_func.skip = true;
                }
            }
            
            // All functions accept all potential parameters (r3-r10, f1-f2) for simplicity
            // This avoids the need to scan ahead in the file which can hang
            if (!current_func.skip) {
                current_func.has_params = true;
                current_func.num_int_params = 8;  // r3-r10
                current_func.num_float_params = 2; // f1-f2
                current_func.returns_value = false; // Default to void, will be detected later
            }
            
            // Write to header with sanitized name (skip local/static functions)
            if (!current_func.skip && !current_func.is_local) {
                // Write function declaration to header (with detected parameters)
                write_function_declaration(h_file, &current_func);
            }
            
            if (!current_func.skip) {
                in_function = true;
            } else {
                // For skipped functions, don't set in_function to prevent closing brace
                in_function = false;
                // Determine skip reason for better logging
                const char *skip_reason = "gap or in skip list";
                if (strncmp(current_func.name, "gap_", 4) == 0) {
                    skip_reason = "gap function";
                } else if (is_sdk_or_stdlib_function(current_func.name)) {
                    skip_reason = "SDK or stdlib function";
                } else if (skiplist_should_skip(skip_list, current_func.name)) {
                    skip_reason = "in skip list";
                }
                fprintf(c_file, "// Function %s skipped (%s)\n\n", current_func.name, skip_reason);
            }
            
            in_data_section = false;
            continue;
        }
        
        // Handle labels
        if (parsed.is_label) {
            if (in_function && !in_data_section) {
                char c_label[MAX_LABEL_NAME + 2];
                convert_label(parsed.label_name, c_label, sizeof(c_label));
                fprintf(c_file, "\n%s\n", c_label);  // Add newline before label for readability
            }
            continue;
        }
        
        // Handle instructions
        if (parsed.instruction != 0) {
            if (in_data_section) {
                // In data section, write as hex data
                fprintf(c_file, "    0x%02X, 0x%02X, 0x%02X, 0x%02X,  // 0x%08X\n",
                       (parsed.instruction >> 24) & 0xFF,
                       (parsed.instruction >> 16) & 0xFF,
                       (parsed.instruction >> 8) & 0xFF,
                       parsed.instruction & 0xFF,
                       parsed.address);
            } else if (in_function) {
                // Set function start address if not set
                if (current_func.start_address == 0) {
                    current_func.start_address = parsed.address;
                    
                    if (current_func.skip) {
                        fprintf(c_file, "// Function %s skipped (in skip list)\n\n",
                               current_func.name);
                        in_function = false;
                        continue;
                    }
                    
                    // Parameters already detected earlier, just write function start
                    write_function_start(c_file, &current_func);
                    
                    // Register function for indirect call resolution
                    register_transpiled_function(current_func.name, current_func.start_address, current_func.is_local);
                    
                    // Reset register tracker for new function
                    register_tracker_init(&register_tracker);
                }
                
                // Transpile instruction
                char c_code[512];
                char asm_comment[128];
                
                // Try parsing from assembly text first
                bool success = transpile_from_asm(parsed.mnemonic, parsed.operands, parsed.address,
                                                  c_code, sizeof(c_code),
                                                  asm_comment, sizeof(asm_comment),
                                                  prev_lines, lines_buffered,
                                                  &current_func, label_map, string_table, &register_tracker);
                
                // Fall back to byte-based decoding
                if (!success) {
                    success = transpile_instruction(parsed.instruction, parsed.address,
                                                    c_code, sizeof(c_code),
                                                    asm_comment, sizeof(asm_comment));
                }
                
                if (success) {
                    // Check for backward jumps to labels (potential loop or misidentified function boundary)
                    if (strstr(c_code, "goto L_") != NULL || strstr(c_code, "goto lbl_") != NULL) {
                        char *goto_pos = strstr(c_code, "goto ");
                        if (goto_pos) {
                            char *addr_start = goto_pos + 5; // Skip "goto "
                            while (*addr_start == ' ') addr_start++;
                            uint32_t target_addr = 0;
                            if (strncmp(addr_start, "L_", 2) == 0) {
                                sscanf(addr_start + 2, "%x", &target_addr);
                            } else if (strncmp(addr_start, "lbl_", 4) == 0) {
                                sscanf(addr_start + 4, "%x", &target_addr);
                            }
                            
                            // Check if this is a backward jump (target address < current address)
                            if (target_addr != 0 && target_addr < parsed.address) {
                                // Check if target is before this function started (cross-function backward jump)
                                if (target_addr < current_func.start_address) {
                                    fprintf(stderr, "\n");
                                    fprintf(stderr, "WARNING: Backward jump detected across function boundary!\n");
                                    fprintf(stderr, "  Function: %s (starts at 0x%08X)\n", 
                                           current_func.name, current_func.start_address);
                                    fprintf(stderr, "  Jump from: 0x%08X\n", parsed.address);
                                    fprintf(stderr, "  Jump to: L_%08X (before function start)\n", target_addr);
                                    fprintf(stderr, "  This usually means the function boundaries are wrong.\n");
                                    fprintf(stderr, "  The function at 0x%08X may need to be merged with the previous function.\n",
                                           current_func.start_address);
                                    fprintf(stderr, "\n");
                                }
                            }
                        }
                    }
                    
                    // Detect trampoline: single branch instruction to a label
                    bool is_trampoline_branch = false;
                    uint32_t trampoline_target = 0;
                    
                    if (current_func.instruction_count == 0 && 
                        (strncmp(parsed.mnemonic, "b", 1) == 0) &&
                        (strstr(c_code, "goto L_") != NULL || strstr(c_code, "goto lbl_") != NULL)) {
                        // Extract target address from the goto statement
                        char *goto_pos = strstr(c_code, "goto ");
                        if (goto_pos) {
                            char *addr_start = goto_pos + 5; // Skip "goto "
                            while (*addr_start == ' ') addr_start++;
                            if (strncmp(addr_start, "L_", 2) == 0) {
                                sscanf(addr_start + 2, "%x", &trampoline_target);
                            } else if (strncmp(addr_start, "lbl_", 4) == 0) {
                                sscanf(addr_start + 4, "%x", &trampoline_target);
                            }
                            
                            if (trampoline_target != 0) {
                                is_trampoline_branch = true;
                                current_func.is_trampoline = true;
                                current_func.trampoline_target = trampoline_target;
                                
                                // Replace goto with valid C code that compiles
                                // Use a function call stub instead of goto
                                snprintf(c_code, sizeof(c_code), 
                                        "/* TRAMPOLINE to 0x%08X - Target function needed */ "
                                        "pc = 0x%08X; return;", 
                                        trampoline_target, trampoline_target);
                            }
                        }
                    }
                    
                    fprintf(c_file, "    %s  // 0x%08X: %s\n",
                           c_code, parsed.address, asm_comment);
                    current_func.instruction_count++;
                } else {
                    fprintf(c_file, "    /* 0x%08X: UNKNOWN 0x%08X - %s %s */\n",
                           parsed.address, parsed.instruction,
                           parsed.mnemonic, parsed.operands);
                    current_func.instruction_count++;
                }
            }
        }
        
        // Update line buffer (circular buffer for context)
        strncpy(line_buffer[line_index], line, MAX_LINE_LENGTH - 1);
        line_buffer[line_index][MAX_LINE_LENGTH - 1] = '\0';
        line_index = (line_index + 1) % MAX_LOOKBACK_LINES;
        if (lines_buffered < MAX_LOOKBACK_LINES) {
            lines_buffered++;
        }
    }
    
    // Close any open function
    if (in_function) {
        write_function_end(c_file);
    }
    
    // Write file footers
    write_header_end(h_file);
    
    // Close files
    fclose(input);
    fclose(c_file);
    fclose(h_file);
    
    // Cleanup label map and string table
    if (label_map) labelmap_free(label_map);
    if (string_table) string_table_free(string_table);
    
    printf("  Created: %s\n", output_c);
    printf("  Created: %s\n", output_h);
    
    return 0;
}

/**
 * @brief Transpile a single .s file directly to project folders
 */
int transpile_file_to_project(const char *input_filename, const char *src_dir, 
                               const char *inc_dir, const char *rel_path, SkipList *skip_list) {
    // Build label-to-function map for trampoline resolution
    LabelMap *label_map = build_label_map(input_filename);
    
    // Build string table for string literal tracking
    StringTable *string_table = build_string_table(input_filename);
    
    // Extract base name
    const char *base = strrchr(input_filename, '/');
    if (!base) base = strrchr(input_filename, '\\');
    base = base ? (base + 1) : input_filename;
    
    char base_name[256];
    strncpy(base_name, base, sizeof(base_name) - 1);
    char *ext = strrchr(base_name, '.');
    if (ext) *ext = '\0';
    
    // Generate output paths in project structure
    char output_c[512], output_h[512];
    snprintf(output_c, sizeof(output_c), "%s/%s.c", src_dir, base_name);
    snprintf(output_h, sizeof(output_h), "%s/%s.h", inc_dir, base_name);
    
    // Open input file
    FILE *input = fopen(input_filename, "r");
    if (!input) {
        fprintf(stderr, "  Error: Cannot open %s\n", input_filename);
        if (label_map) labelmap_free(label_map);
        if (string_table) string_table_free(string_table);
        return -1;
    }
    
    // Open output files
    FILE *c_file = fopen(output_c, "w");
    FILE *h_file = fopen(output_h, "w");
    
    if (!c_file || !h_file) {
        fprintf(stderr, "  Error: Cannot create output files\n");
        if (input) fclose(input);
        if (c_file) fclose(c_file);
        if (h_file) fclose(h_file);
        return -1;
    }
    
    // Generate header guard
    char guard_name[128];
    snprintf(guard_name, sizeof(guard_name), "%s_H", base_name);
    for (char *p = guard_name; *p; p++) {
        if (*p == '-' || *p == ' ' || *p == '.' || *p == '(' || *p == ')') {
            *p = '_';
        } else if (*p >= 'a' && *p <= 'z') {
            *p = *p - 32;  // Convert to uppercase
        }
    }
    
    // Write header file boilerplate
    fprintf(h_file, "#ifndef %s\n", guard_name);
    fprintf(h_file, "#define %s\n\n", guard_name);
    
    // Write C file boilerplate
    if (rel_path && rel_path[0]) {
        fprintf(c_file, "#include \"%s/%s.h\"\n", rel_path, base_name);
    } else {
        fprintf(c_file, "#include \"%s.h\"\n", base_name);
    }
    fprintf(c_file, "#include \"powerpc_state.h\"\n");
    fprintf(c_file, "#include \"all_functions.h\"  // For cross-file function calls\n");
    // PLACEHOLDER_FUNCTION_ADDRESS_MAP_INCLUDE - will be replaced if indirect calls are used
    
    // Track if this file uses indirect calls (will add function_address_map.h if needed)
    bool needs_address_map_header = false;
    
    // Generate string constants if any were found
    if (string_table && string_table->count > 0) {
        fprintf(c_file, "//==============================================================================\n");
        fprintf(c_file, "// String Constants (from .rodata/.data sections)\n");
        fprintf(c_file, "//==============================================================================\n\n");
        
        for (int i = 0; i < string_table->count; i++) {
            const StringEntry *entry = &string_table->entries[i];
            // Escape string content for C
            fprintf(c_file, "const char %s[] = \"%s\";  // @ 0x%08X\n", 
                   entry->label, entry->content, entry->address);
        }
        fprintf(c_file, "\n");
    }
    
    // First pass: collect all local (static) functions for forward declarations
    fprintf(c_file, "// Forward declarations for local (static) functions\n");
    FILE *scan_file = fopen(input_filename, "r");
    if (scan_file) {
        char scan_line[MAX_LINE_LENGTH];
        while (fgets(scan_line, sizeof(scan_line), scan_file)) {
            // Stop scanning at non-code sections (data sections, etc.)
            if (strstr(scan_line, ".section") != NULL && 
                strstr(scan_line, ".text") == NULL &&
                strstr(scan_line, ".init") == NULL) {
                break;  // Stop at data sections
            }
            
            ASM_Line parsed;
            if (parse_asm_line(scan_line, &parsed) && parsed.is_function && parsed.is_local_function) {
                // Strip quotes and handle special characters in function names
                char clean_name[512];
                strncpy(clean_name, parsed.function_name, sizeof(clean_name) - 1);
                clean_name[sizeof(clean_name) - 1] = '\0';
                
                // Strip quotes
                if (clean_name[0] == '"') {
                    size_t len = strlen(clean_name);
                    memmove(clean_name, clean_name + 1, len);
                    len = strlen(clean_name);
                    if (len > 0 && clean_name[len - 1] == '"') {
                        clean_name[len - 1] = '\0';
                    }
                }
                
                // Skip __init_registers even if it's marked as local
                if (strcmp(clean_name, "__init_registers") == 0) {
                    continue;
                }
                
                // Skip SDK functions even if they're marked as local
                if (is_sdk_or_stdlib_function(clean_name)) {
                    continue;
                }
                
                // Check if it needs to be stubbed (contains invalid C identifier chars)
                if (strlen(clean_name) > 80 || strchr(clean_name, '<') != NULL || 
                    strchr(clean_name, '>') != NULL || strchr(clean_name, ',') != NULL ||
                    strchr(clean_name, '@') != NULL) {
                    // Create stub name based on hash
                    unsigned int hash = 0;
                    for (const char *p = clean_name; *p; p++) {
                        hash = hash * 31 + (unsigned char)*p;
                    }
                    fprintf(c_file, "static void cpp_stub_func_%08x(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, double, double);\n", hash);
                } else {
                    fprintf(c_file, "static void %s(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, double, double);\n", 
                           clean_name);
                }
            }
        }
        fclose(scan_file);
    }
    fprintf(c_file, "\n");
    
    // Process file line by line
    char line[MAX_LINE_LENGTH];
    bool in_function = false;
    bool in_data_section = false;
    bool seen_text_section = false;  // Track if we've entered the code section
    Function_Info current_func = {0};
    
    // Register tracker for compile-time function pointer resolution
    RegisterTracker register_tracker;
    register_tracker_init(&register_tracker);
    
    // Buffer to track previous lines for parameter detection
    char line_buffer[MAX_LOOKBACK_LINES][MAX_LINE_LENGTH];
    const char *prev_lines[MAX_LOOKBACK_LINES];
    int line_index = 0;
    int lines_buffered = 0;
    
    // Initialize pointers
    for (int i = 0; i < MAX_LOOKBACK_LINES; i++) {
        prev_lines[i] = line_buffer[i];
        line_buffer[i][0] = '\0';
    }
    
    while (fgets(line, sizeof(line), input)) {
        ASM_Line parsed;
        
        if (!parse_asm_line(line, &parsed)) {
            fprintf(c_file, "    // %s", line);
            continue;
        }
        
        if (parsed.is_comment) continue;
        
        if (parsed.is_directive) {
            // Track when we enter the .text section
            if (strstr(line, ".text") != NULL || strstr(line, ".init") != NULL) {
                seen_text_section = true;
            }
            
            // Only stop at data sections AFTER we've seen the code section
            // This handles files where data sections appear before .text
            if (seen_text_section && strstr(line, ".section") != NULL && 
                strstr(line, ".text") == NULL && 
                strstr(line, ".init") == NULL) {
                fprintf(c_file, "\n// End of code section - data sections follow\n");
                break;  // Stop processing this file
            }
            
            if (strstr(line, ".include") != NULL) {
                char include_line[256];
                convert_include(line, include_line, sizeof(include_line));
                fprintf(h_file, "%s\n", include_line);
            }
            if (strstr(line, ".endfn") != NULL) {
                if (in_data_section && current_func.is_data_only) {
                    // Close data array
                    fprintf(c_file, "};\n\n");
                    in_data_section = false;
                } else if (in_function) {
                    // Add trampoline fix instructions if detected
                    if (current_func.is_trampoline) {
                        fprintf(c_file, "    /* TRAMPOLINE DETECTED - Cross-function jump to 0x%08X\n", 
                               current_func.trampoline_target);
                        fprintf(c_file, "     * Auto-fix: Replace the goto above with:\n");
                        fprintf(c_file, "     *   pc = 0x%08X;\n", current_func.trampoline_target);
                        fprintf(c_file, "     *   TARGET_FUNCTION();  // Function containing L_%08X\n", 
                               current_func.trampoline_target);
                        fprintf(c_file, "     * Then add to target function start:\n");
                        fprintf(c_file, "     *   if (pc == 0x%08X) goto L_%08X;\n", 
                               current_func.trampoline_target, current_func.trampoline_target);
                        fprintf(c_file, "     */\n");
                    }
                    write_function_end(c_file);
                    in_function = false;
                }
            }
            continue;
        }
        
        if (parsed.is_data) {
            in_data_section = true;
            fprintf(c_file, "\n// === DATA SECTION ===\n");
            fprintf(c_file, "// (Data sections preserved as byte arrays)\n\n");
            continue;
        }
        
        if (parsed.is_function) {
            strcpy(current_func.name, parsed.function_name);
            current_func.start_address = 0;
            current_func.instruction_count = 0;
            current_func.is_trampoline = false;
            current_func.trampoline_target = 0;
            current_func.is_local = parsed.is_local_function;  // Track if static
            current_func.is_data_only = false;  // TODO: Implement efficient data detection
            
            // Auto-skip gap functions (usually data misidentified as code)
            if (strncmp(current_func.name, "gap_", 4) == 0) {
                current_func.skip = true;
            } else {
                // Check skip list first
                current_func.skip = skiplist_should_skip(skip_list, current_func.name);
                // Also skip SDK functions (even if not in skip list)
                if (!current_func.skip && is_sdk_or_stdlib_function(current_func.name)) {
                    current_func.skip = true;
                }
            }
            
            // All functions accept all potential parameters (r3-r10, f1-f2) for simplicity
            // This avoids the need to scan ahead in the file which can hang
            if (!current_func.skip) {
                current_func.has_params = true;
                current_func.num_int_params = 8;  // r3-r10
                current_func.num_float_params = 2; // f1-f2
                current_func.returns_value = false; // Default to void, will be detected later
            }
            
            // For data-only functions, write as byte array instead of function
            if (current_func.is_data_only) {
                fprintf(c_file, "// Data section: %s (detected as %s)\n",
                       current_func.name, current_func.is_local ? "static" : "global");
                fprintf(c_file, "%sconst uint8_t %s[] __attribute__((aligned(4))) = {\n",
                       current_func.is_local ? "static " : "", current_func.name);
                in_function = false;  // Don't treat as function
                in_data_section = true;  // Flag to output as data
                continue;
            }
            
            if (!current_func.skip) {
                // Write function declaration to header (with detected parameters)
                write_function_declaration(h_file, &current_func);
                
                in_function = true;
            } else {
                // For skipped functions, don't set in_function to prevent closing brace
                in_function = false;
                fprintf(c_file, "// Function %s skipped (gap or in skip list)\n\n", current_func.name);
            }
            
            in_data_section = false;
            continue;
        }
        
        if (parsed.is_label) {
            // Only output labels when inside a function AND after the function signature has been written
            if (in_function && !in_data_section && current_func.start_address != 0) {
                char c_label[MAX_LABEL_NAME + 2];
                convert_label(parsed.label_name, c_label, sizeof(c_label));
                fprintf(c_file, "\n%s\n", c_label);
            }
            // If label appears before first instruction, it will be lost but that's okay
            // because it would be at the function entry point anyway
            continue;
        }
        
        if (parsed.instruction != 0) {
            if (in_data_section) {
                fprintf(c_file, "    0x%02X, 0x%02X, 0x%02X, 0x%02X,  // 0x%08X\n",
                       (parsed.instruction >> 24) & 0xFF,
                       (parsed.instruction >> 16) & 0xFF,
                       (parsed.instruction >> 8) & 0xFF,
                       parsed.instruction & 0xFF,
                       parsed.address);
            } else if (in_function) {
                if (current_func.start_address == 0) {
                    current_func.start_address = parsed.address;
                    
                    if (current_func.skip) {
                        fprintf(c_file, "// Function %s skipped (in skip list)\n\n",
                               current_func.name);
                        in_function = false;
                        continue;
                    }
                    
                    // Parameters already detected earlier, just write function start
                    write_function_start(c_file, &current_func);
                    
                    // Register function for indirect call resolution
                    register_transpiled_function(current_func.name, current_func.start_address, current_func.is_local);
                    
                    // Reset register tracker for new function
                    register_tracker_init(&register_tracker);
                }
                
                char c_code[512], asm_comment[128];
                
                // Try parsing from assembly text first (for branches, etc.)
                bool success = transpile_from_asm(parsed.mnemonic, parsed.operands, parsed.address,
                                                  c_code, sizeof(c_code),
                                                  asm_comment, sizeof(asm_comment),
                                                  prev_lines, lines_buffered,
                                                  &current_func, label_map, string_table, &register_tracker);
                
                // Fall back to byte-based decoding if text-based failed
                if (!success) {
                    success = transpile_instruction(parsed.instruction, parsed.address,
                                                    c_code, sizeof(c_code),
                                                    asm_comment, sizeof(asm_comment));
                }
                
                if (success) {
                    // Check if this instruction generates an indirect call that needs function_address_map.h
                    if (strstr(c_code, "call_function_by_address") != NULL) {
                        needs_address_map_header = true;
                    }
                    
                    // Check for backward jumps to labels (potential loop or misidentified function boundary)
                    if (strstr(c_code, "goto L_") != NULL || strstr(c_code, "goto lbl_") != NULL) {
                        char *goto_pos = strstr(c_code, "goto ");
                        if (goto_pos) {
                            char *addr_start = goto_pos + 5; // Skip "goto "
                            while (*addr_start == ' ') addr_start++;
                            uint32_t target_addr = 0;
                            if (strncmp(addr_start, "L_", 2) == 0) {
                                sscanf(addr_start + 2, "%x", &target_addr);
                            } else if (strncmp(addr_start, "lbl_", 4) == 0) {
                                sscanf(addr_start + 4, "%x", &target_addr);
                            }
                            
                            // Check if this is a backward jump (target address < current address)
                            if (target_addr != 0 && target_addr < parsed.address) {
                                // Check if target is before this function started (cross-function backward jump)
                                if (target_addr < current_func.start_address) {
                                    fprintf(stderr, "\n");
                                    fprintf(stderr, "WARNING: Backward jump detected across function boundary!\n");
                                    fprintf(stderr, "  Function: %s (starts at 0x%08X)\n", 
                                           current_func.name, current_func.start_address);
                                    fprintf(stderr, "  Jump from: 0x%08X\n", parsed.address);
                                    fprintf(stderr, "  Jump to: L_%08X (before function start)\n", target_addr);
                                    fprintf(stderr, "  This usually means the function boundaries are wrong.\n");
                                    fprintf(stderr, "  The function at 0x%08X may need to be merged with the previous function.\n",
                                           current_func.start_address);
                                    fprintf(stderr, "\n");
                                }
                            }
                        }
                    }
                    
                    // Detect trampoline: single branch instruction to a label
                    bool is_trampoline_branch = false;
                    uint32_t trampoline_target = 0;
                    
                    if (current_func.instruction_count == 0 && 
                        (strncmp(parsed.mnemonic, "b", 1) == 0) &&
                        (strstr(c_code, "goto L_") != NULL || strstr(c_code, "goto lbl_") != NULL)) {
                        // Extract target address from the goto statement
                        char *goto_pos = strstr(c_code, "goto ");
                        if (goto_pos) {
                            char *addr_start = goto_pos + 5; // Skip "goto "
                            while (*addr_start == ' ') addr_start++;
                            if (strncmp(addr_start, "L_", 2) == 0) {
                                sscanf(addr_start + 2, "%x", &trampoline_target);
                            } else if (strncmp(addr_start, "lbl_", 4) == 0) {
                                sscanf(addr_start + 4, "%x", &trampoline_target);
                            }
                            
                            if (trampoline_target != 0) {
                                is_trampoline_branch = true;
                                current_func.is_trampoline = true;
                                current_func.trampoline_target = trampoline_target;
                                
                                // Replace goto with valid C code that compiles
                                // Use a function call stub instead of goto
                                snprintf(c_code, sizeof(c_code), 
                                        "/* TRAMPOLINE to 0x%08X - Target function needed */ "
                                        "pc = 0x%08X; return;", 
                                        trampoline_target, trampoline_target);
                            }
                        }
                    }
                    
                    fprintf(c_file, "    %s  // 0x%08X: %s\n",
                           c_code, parsed.address, asm_comment);
                    current_func.instruction_count++;
                } else {
                    fprintf(c_file, "    /* 0x%08X: UNKNOWN 0x%08X - %s */\n",
                           parsed.address, parsed.instruction, asm_comment);
                    current_func.instruction_count++;
                }
            }
        }
        
        // Update line buffer (circular buffer for context)
        strncpy(line_buffer[line_index], line, MAX_LINE_LENGTH - 1);
        line_buffer[line_index][MAX_LINE_LENGTH - 1] = '\0';
        line_index = (line_index + 1) % MAX_LOOKBACK_LINES;
        if (lines_buffered < MAX_LOOKBACK_LINES) {
            lines_buffered++;
        }
    }
    
    fprintf(h_file, "\n#endif // %s\n", guard_name);
    
    // If indirect calls were used, add the include header
    if (needs_address_map_header) {
        // Read the C file, replace placeholder with include
        fclose(c_file);
        FILE *temp_file = fopen(output_c, "r");
        if (temp_file) {
            char temp_path[512];
            snprintf(temp_path, sizeof(temp_path), "%s.tmp", output_c);
            FILE *new_file = fopen(temp_path, "w");
            if (new_file) {
                char line[1024];
                while (fgets(line, sizeof(line), temp_file)) {
                    if (strstr(line, "PLACEHOLDER_FUNCTION_ADDRESS_MAP_INCLUDE") != NULL) {
                        fprintf(new_file, "#include \"function_address_map.h\"  // For indirect calls (vtables, callbacks)\n");
                    } else {
                        fputs(line, new_file);
                    }
                }
                fclose(new_file);
                fclose(temp_file);
                remove(output_c);
                rename(temp_path, output_c);
            } else {
                fclose(temp_file);
            }
        }
    } else {
        fclose(c_file);
    }
    fclose(h_file);
    
    // Cleanup label map
    if (label_map) labelmap_free(label_map);
    if (string_table) string_table_free(string_table);
    
    printf("  → %s/%s.c\n", src_dir, base_name);
    printf("  → %s/%s.h\n", inc_dir, base_name);
    
    return 0;
}

/**
 * @brief Process directory of .s files
 */
int transpile_directory(const char *dir_path, SkipList *skip_list) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        fprintf(stderr, "Error: Cannot open directory %s\n", dir_path);
        return -1;
    }
    
    int files_processed = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        // Check if it's a .s file
        size_t name_len = strlen(entry->d_name);
        if (name_len < 3) continue;
        
        if (strcmp(entry->d_name + name_len - 2, ".s") == 0) {
            // Construct full path
            char filepath[512];
            snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, entry->d_name);
            
            // Transpile the file
            if (transpile_file(filepath, skip_list) == 0) {
                files_processed++;
            }
        }
    }
    
    closedir(dir);
    return files_processed;
}

/**
 * @brief Generate CMake project from transpiled files
 */
int generate_project(const char *output_dir, const char *dir_path) {
    printf("\n===========================================\n");
    printf("   Generating CMake Project\n");
    printf("===========================================\n\n");
    
    // Create project directories
    printf("Creating project structure...\n");
    create_directory(output_dir);
    
    char src_dir[512], inc_dir[512];
    snprintf(src_dir, sizeof(src_dir), "%s/src", output_dir);
    snprintf(inc_dir, sizeof(inc_dir), "%s/include", output_dir);
    
    create_directory(src_dir);
    create_directory(inc_dir);
    
    // Collect all generated .c and .h files
    int c_file_count = 0;
    int h_file_count = 0;
    char **c_files = malloc(sizeof(char*) * 100);
    char **h_files = malloc(sizeof(char*) * 100);
    
    // Count and copy .c and .h files
    DIR *dir = opendir(dir_path);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            size_t name_len = strlen(entry->d_name);
            if (name_len < 3) continue;
            
            if (strcmp(entry->d_name + name_len - 2, ".c") == 0) {
                // Copy .c file to src/
                char src_path[512], dst_path[512];
                snprintf(src_path, sizeof(src_path), "%s/%s", dir_path, entry->d_name);
                snprintf(dst_path, sizeof(dst_path), "%s/%s", src_dir, entry->d_name);
                
                FILE *src_file = fopen(src_path, "r");
                FILE *dst_file = fopen(dst_path, "w");
                if (src_file && dst_file) {
                    char buffer[4096];
                    size_t bytes;
                    while ((bytes = fread(buffer, 1, sizeof(buffer), src_file)) > 0) {
                        fwrite(buffer, 1, bytes, dst_file);
                    }
                    fclose(src_file);
                    fclose(dst_file);
                    c_files[c_file_count++] = strdup(entry->d_name);
                }
            }
            else if (strcmp(entry->d_name + name_len - 2, ".h") == 0) {
                // Copy .h file to include/
                char src_path[512], dst_path[512];
                snprintf(src_path, sizeof(src_path), "%s/%s", dir_path, entry->d_name);
                snprintf(dst_path, sizeof(dst_path), "%s/%s", inc_dir, entry->d_name);
                
                FILE *src_file = fopen(src_path, "r");
                FILE *dst_file = fopen(dst_path, "w");
                if (src_file && dst_file) {
                    char buffer[4096];
                    size_t bytes;
                    while ((bytes = fread(buffer, 1, sizeof(buffer), src_file)) > 0) {
                        fwrite(buffer, 1, bytes, dst_file);
                    }
                    fclose(src_file);
                    fclose(dst_file);
                    h_files[h_file_count++] = strdup(entry->d_name);
                }
            }
        }
        closedir(dir);
    }
    
    // Extract project name from path
    const char *proj_name = strrchr(output_dir, '/');
    if (!proj_name) proj_name = strrchr(output_dir, '\\');
    proj_name = proj_name ? (proj_name + 1) : output_dir;
    
    // Generate project files
    printf("Generating CMakeLists.txt...\n");
    generate_cmake(output_dir, proj_name, c_file_count, (const char**)c_files, 
                   h_file_count, (const char**)h_files);
    
    printf("Generating runtime files...\n");
    generate_runtime_h(output_dir);
    generate_runtime_c(output_dir);
    generate_main_c(output_dir);
    
    printf("Copying core headers...\n");
    // Generate stdlib_headers.h with config settings
    char stdlib_dst[512];
    snprintf(stdlib_dst, sizeof(stdlib_dst), "%s/include/stdlib_headers.h", output_dir);
    FILE *dst_file = fopen(stdlib_dst, "w");
    if (dst_file) {
        fprintf(dst_file, "/**\n");
        fprintf(dst_file, " * @file stdlib_headers.h\n");
        fprintf(dst_file, " * @brief Standard library headers for transpiled code\n");
        fprintf(dst_file, " * \n");
        fprintf(dst_file, " * This header includes all standard C library headers needed for\n");
        fprintf(dst_file, " * transpiled GameCube/Wii PowerPC assembly code.\n");
        fprintf(dst_file, " * Include this in all generated .c files.\n");
        fprintf(dst_file, " */\n\n");
        fprintf(dst_file, "#ifndef STDLIB_HEADERS_H\n");
        fprintf(dst_file, "#define STDLIB_HEADERS_H\n\n");
        fprintf(dst_file, "// Standard library includes (BEFORE redeclarations)\n");
        fprintf(dst_file, "#include <stdint.h>\n");
        fprintf(dst_file, "#include <stdbool.h>\n");
        fprintf(dst_file, "#include <stddef.h>\n\n");
        fprintf(dst_file, "// PowerPC runtime library (64-bit intrinsics)\n");
        fprintf(dst_file, "#include \"ppc_runtime.h\"\n\n");
        fprintf(dst_file, "// Include stdlib stub declarations that match transpiler's 10-parameter signature\n");
        fprintf(dst_file, "// These override the standard library declarations\n");
        fprintf(dst_file, "// NOTE: If the project already has MSL_C headers (Metrowerks), they take precedence\n");
        if (config.skip_stdlib_stubs) {
            fprintf(dst_file, "#define SKIP_STDLIB_STUBS 1\n");
        }
        fprintf(dst_file, "#ifndef SKIP_STDLIB_STUBS\n");
        fprintf(dst_file, "    #include \"stdlib_stubs.h\"\n");
        fprintf(dst_file, "#endif\n\n");
        fprintf(dst_file, "// Suppress warnings for unused parameters (common in transpiled code)\n");
        fprintf(dst_file, "#ifdef _MSC_VER\n");
        fprintf(dst_file, "    #pragma warning(disable: 4100)  // unreferenced formal parameter\n");
        fprintf(dst_file, "    #pragma warning(disable: 4189)  // local variable initialized but not used\n");
        fprintf(dst_file, "    #pragma warning(disable: 4702)  // unreachable code\n");
        fprintf(dst_file, "    #pragma warning(disable: 4310)  // cast truncates constant value\n");
        fprintf(dst_file, "    #pragma warning(disable: 4146)  // unary minus on unsigned type\n");
        fprintf(dst_file, "#endif\n\n");
        fprintf(dst_file, "#endif // STDLIB_HEADERS_H\n");
        fclose(dst_file);
    }
    
    // Copy gecko_memory.h
    char gecko_src[512], gecko_dst[512];
    snprintf(gecko_src, sizeof(gecko_src), "include/gecko_memory.h");
    snprintf(gecko_dst, sizeof(gecko_dst), "%s/include/gecko_memory.h", output_dir);
    FILE *gecko_src_file = fopen(gecko_src, "r");
    FILE *gecko_dst_file = fopen(gecko_dst, "w");
    if (gecko_src_file && gecko_dst_file) {
        char buffer[4096];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), gecko_src_file)) > 0) {
            fwrite(buffer, 1, bytes, gecko_dst_file);
        }
        fclose(gecko_src_file);
        fclose(gecko_dst_file);
    }
    
    // Copy function_address_map.h and function_address_map.c
    char fam_src[512], fam_dst[512];
    
    // Copy header
    snprintf(fam_src, sizeof(fam_src), "include/function_address_map.h");
    snprintf(fam_dst, sizeof(fam_dst), "%s/include/function_address_map.h", output_dir);
    FILE *fam_src_file = fopen(fam_src, "r");
    FILE *fam_dst_file = fopen(fam_dst, "w");
    if (fam_src_file && fam_dst_file) {
        char buffer[4096];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), fam_src_file)) > 0) {
            fwrite(buffer, 1, bytes, fam_dst_file);
        }
        fclose(fam_src_file);
        fclose(fam_dst_file);
        printf("Copied function_address_map.h\n");
    } else {
        fprintf(stderr, "Warning: Could not copy function_address_map.h\n");
    }
    
    // Copy source file
    snprintf(fam_src, sizeof(fam_src), "src/function_address_map.c");
    snprintf(fam_dst, sizeof(fam_dst), "%s/src/function_address_map.c", output_dir);
    fam_src_file = fopen(fam_src, "r");
    fam_dst_file = fopen(fam_dst, "w");
    if (fam_src_file && fam_dst_file) {
        char buffer[4096];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), fam_src_file)) > 0) {
            fwrite(buffer, 1, bytes, fam_dst_file);
        }
        fclose(fam_src_file);
        fclose(fam_dst_file);
        printf("Copied function_address_map.c\n");
    } else {
        fprintf(stderr, "Warning: Could not copy function_address_map.c\n");
    }
    
    // Copy sdk_functions.txt to project (needed for SDK function signatures)
    char sdk_funcs_src[512], sdk_funcs_dst[512];
    snprintf(sdk_funcs_src, sizeof(sdk_funcs_src), "sdk_functions.txt");
    snprintf(sdk_funcs_dst, sizeof(sdk_funcs_dst), "%s/sdk_functions.txt", output_dir);
    FILE *sdk_funcs_src_file = fopen(sdk_funcs_src, "r");
    FILE *sdk_funcs_dst_file = fopen(sdk_funcs_dst, "w");
    if (sdk_funcs_src_file && sdk_funcs_dst_file) {
        char buffer[4096];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), sdk_funcs_src_file)) > 0) {
            fwrite(buffer, 1, bytes, sdk_funcs_dst_file);
        }
        fclose(sdk_funcs_src_file);
        fclose(sdk_funcs_dst_file);
        printf("Copied sdk_functions.txt\n");
    } else {
        fprintf(stderr, "Warning: Could not copy sdk_functions.txt\n");
    }
    
    // Generate skip_sdk_functions.txt next to the tool executable (not in project)
    // This file is used by the transpiler tool, not the project
    char skip_sdk_tool[512];
    snprintf(skip_sdk_tool, sizeof(skip_sdk_tool), "skip_sdk_functions.txt");
    FILE *skip_sdk_file = fopen(skip_sdk_tool, "w");
    if (skip_sdk_file) {
        fprintf(skip_sdk_file, "# Skip list for SDK functions\n");
        fprintf(skip_sdk_file, "# Generated automatically from sdk_functions.txt\n");
        fprintf(skip_sdk_file, "# These functions should not be transpiled as they are provided by the SDK\n");
        fprintf(skip_sdk_file, "# This file should be used with the transpiler tool: porpoise_tool.exe \"Project\" \"Input\" \"Output\" skip_sdk_functions.txt\n\n");
        
        // Read sdk_functions.txt to extract function names
        sdk_funcs_src_file = fopen(sdk_funcs_src, "r");
        if (sdk_funcs_src_file) {
            char line[256];
            while (fgets(line, sizeof(line), sdk_funcs_src_file)) {
                // Skip comments and empty lines
                if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
                
                // Extract function name (before the colon)
                char func_name[128];
                if (sscanf(line, "%127[^:\n]", func_name) == 1) {
                    fprintf(skip_sdk_file, "%s\n", func_name);
                }
            }
            fclose(sdk_funcs_src_file);
        }
        fclose(skip_sdk_file);
        printf("Generated skip_sdk_functions.txt (next to tool executable)\n");
    }
    
    printf("Generating documentation...\n");
    generate_readme(output_dir, proj_name);
    generate_gitignore(output_dir);
    
    // Cleanup
    for (int i = 0; i < c_file_count; i++) {
        free(c_files[i]);
    }
    free(c_files);
    
    for (int i = 0; i < h_file_count; i++) {
        free(h_files[i]);
    }
    free(h_files);
    
    printf("\n===========================================\n");
    printf("   Project Generated Successfully!\n");
    printf("   Location: %s\n", output_dir);
    printf("===========================================\n");
    printf("\nTo build the project:\n");
    printf("  cd %s\n", output_dir);
    printf("  mkdir build && cd build\n");
    printf("  cmake ..\n");
    printf("  cmake --build .\n\n");
    
    return 0;
}

/**
 * @brief Recursively process .s files in directory and subdirectories
 * @param input_dir Input directory path
 * @param output_src Output src directory
 * @param output_inc Output include directory
 * @param rel_path Relative path from input root (for maintaining structure)
 * @param skip_list Skip list
 * @param files_processed Counter for files processed
 * @param c_files Array to store .c filenames with paths
 * @param h_files Array to store .h filenames with paths
 * @param file_count Current file count
 * @param max_files Maximum files
 * @return 0 on success
 */
static int process_directory_recursive(const char *input_dir, const char *output_src, const char *output_inc,
                                        const char *rel_path, SkipList *skip_list,
                                        int *files_processed, char **c_files, char **h_files,
                                        int *file_count, int max_files) {
    DIR *dir = opendir(input_dir);
    if (!dir) {
        fprintf(stderr, "Warning: Cannot open directory %s\n", input_dir);
        return -1;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", input_dir, entry->d_name);
        
        // Check if it's a directory
        struct stat path_stat;
        if (stat(full_path, &path_stat) == 0 && S_ISDIR(path_stat.st_mode)) {
            // Recursively process subdirectory
            char new_rel_path[512];
            if (rel_path[0] == '\0') {
                snprintf(new_rel_path, sizeof(new_rel_path), "%s", entry->d_name);
            } else {
                snprintf(new_rel_path, sizeof(new_rel_path), "%s/%s", rel_path, entry->d_name);
            }
            
            // Create matching subdirectories in output
            char sub_src[512], sub_inc[512];
            snprintf(sub_src, sizeof(sub_src), "%s/%s", output_src, entry->d_name);
            snprintf(sub_inc, sizeof(sub_inc), "%s/%s", output_inc, entry->d_name);
            create_directory(sub_src);
            create_directory(sub_inc);
            
            // Recurse
            process_directory_recursive(full_path, sub_src, sub_inc, new_rel_path,
                                       skip_list, files_processed, c_files, h_files,
                                       file_count, max_files);
        }
        // Check if it's a .s file
        else {
            size_t name_len = strlen(entry->d_name);
            if (name_len >= 3 && strcmp(entry->d_name + name_len - 2, ".s") == 0) {
                printf("Processing [%d]: %s%s\n", *files_processed + 1,
                       rel_path[0] ? rel_path : "", rel_path[0] ? "/" : "");
                printf("%s\n", entry->d_name);
                fflush(stdout);
                
                if (*file_count >= max_files) {
                    fprintf(stderr, "Warning: Too many files (max %d)\n", max_files);
                    continue;
                }
                
                // Transpile to project structure
                int result = transpile_file_to_project(full_path, output_src, output_inc, rel_path, skip_list);
                if (result == 0) {
                    // Track filenames with relative paths
                    char base_name[256];
                    strncpy(base_name, entry->d_name, name_len - 2);
                    base_name[name_len - 2] = '\0';
                    
                    char c_rel[512], h_rel[512];
                    if (rel_path[0]) {
                        snprintf(c_rel, sizeof(c_rel), "%s/%s.c", rel_path, base_name);
                        snprintf(h_rel, sizeof(h_rel), "%s/%s.h", rel_path, base_name);
                    } else {
                        snprintf(c_rel, sizeof(c_rel), "%s.c", base_name);
                        snprintf(h_rel, sizeof(h_rel), "%s.h", base_name);
                    }
                    
                    c_files[*file_count] = strdup(c_rel);
                    h_files[*file_count] = strdup(h_rel);
                    (*file_count)++;
                    (*files_processed)++;
                    
                    printf("  ✓ Success\n");
                } else {
                    fprintf(stderr, "  ✗ Failed\n");
                }
            }
        }
    }
    
    closedir(dir);
    return 0;
}

/**
 * @brief Simple JSON value extractor (handles basic key:value pairs)
 * Returns pointer to value string, or NULL if not found
 * Note: Returns pointer to static buffer - copy result if needed
 */
static const char* json_get_value(const char *json, const char *key) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);
    
    const char *key_pos = strstr(json, search_key);
    if (!key_pos) return NULL;
    
    // Find colon after key
    const char *colon = strchr(key_pos, ':');
    if (!colon) return NULL;
    
    // Skip whitespace after colon
    const char *value_start = colon + 1;
    while (*value_start == ' ' || *value_start == '\t') value_start++;
    
    // Handle string values (quoted)
    if (*value_start == '"') {
        value_start++;  // Skip opening quote
        static char string_buffer[256];
        int i = 0;
        while (*value_start != '"' && *value_start != '\0' && *value_start != '\n' && i < sizeof(string_buffer) - 1) {
            string_buffer[i++] = *value_start++;
        }
        string_buffer[i] = '\0';
        return string_buffer;
    }
    
    // Handle boolean/number values (unquoted)
    static char value_buffer[256];
    int i = 0;
    const char *value_end = value_start;
    while (*value_end != ',' && *value_end != '}' && *value_end != '\0' && *value_end != '\n' && *value_end != ' ' && *value_end != '\t' && i < sizeof(value_buffer) - 1) {
        value_buffer[i++] = *value_end++;
    }
    value_buffer[i] = '\0';
    return value_buffer;
}

/**
 * @brief Get executable directory path
 */
static void get_executable_dir(char *buffer, size_t buffer_size) {
#ifdef _WIN32
    // Windows: GetModuleFileName
    HMODULE hModule = GetModuleHandle(NULL);
    if (hModule) {
        DWORD size = GetModuleFileNameA(hModule, buffer, (DWORD)buffer_size);
        if (size > 0 && size < buffer_size) {
            // Find last backslash and null-terminate there
            char *last_slash = strrchr(buffer, '\\');
            if (last_slash) {
                *(last_slash + 1) = '\0';
            } else {
                buffer[0] = '\0';  // No directory found
            }
        }
    }
#else
    // Linux/Unix: readlink /proc/self/exe or use argv[0]
    char link_path[512];
    snprintf(link_path, sizeof(link_path), "/proc/self/exe");
    ssize_t len = readlink(link_path, buffer, buffer_size - 1);
    if (len > 0) {
        buffer[len] = '\0';
        // Find last slash and null-terminate there
        char *last_slash = strrchr(buffer, '/');
        if (last_slash) {
            *(last_slash + 1) = '\0';
        } else {
            buffer[0] = '\0';
        }
    } else {
        // Fallback: use current directory
        buffer[0] = '.';
        buffer[1] = '/';
        buffer[2] = '\0';
    }
#endif
}

/**
 * @brief Load configuration from config.json file beside executable
 */
static void load_config(void) {
    char exe_dir[512];
    get_executable_dir(exe_dir, sizeof(exe_dir));
    
    char config_path[512];
    snprintf(config_path, sizeof(config_path), "%sconfig.json", exe_dir);
    
    FILE *f = fopen(config_path, "r");
    if (!f) {
        // Config file not found - use defaults
        return;
    }
    
    // Read entire file
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > 8192) {
        fclose(f);
        return;
    }
    
    char *json_content = (char*)malloc(file_size + 1);
    if (!json_content) {
        fclose(f);
        return;
    }
    
    size_t read_size = fread(json_content, 1, file_size, f);
    json_content[read_size] = '\0';
    fclose(f);
    
    // Parse JSON values
    const char *value;
    
    // transpile_sdk_functions
    value = json_get_value(json_content, "transpile_sdk_functions");
    if (value) {
        if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
            config.transpile_sdk_functions = true;
        } else {
            config.transpile_sdk_functions = false;
        }
    }
    
    // skip_stdlib_stubs
    value = json_get_value(json_content, "skip_stdlib_stubs");
    if (value) {
        if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
            config.skip_stdlib_stubs = true;
        } else {
            config.skip_stdlib_stubs = false;
        }
    }
    
    // ignore_cstd_calls
    value = json_get_value(json_content, "ignore_cstd_calls");
    if (value) {
        if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
            config.ignore_cstd_calls = true;
        } else {
            config.ignore_cstd_calls = false;
        }
    }
    
    // sdk_functions_file
    value = json_get_value(json_content, "sdk_functions_file");
    if (value && strlen(value) > 0) {
        strncpy(config.sdk_functions_file, value, sizeof(config.sdk_functions_file) - 1);
        config.sdk_functions_file[sizeof(config.sdk_functions_file) - 1] = '\0';
    }
    
    // skip_list_file
    value = json_get_value(json_content, "skip_list_file");
    if (value && strlen(value) > 0) {
        strncpy(config.skip_list_file, value, sizeof(config.skip_list_file) - 1);
        config.skip_list_file[sizeof(config.skip_list_file) - 1] = '\0';
    }
    
    free(json_content);
    printf("Loaded configuration from: %s\n", config_path);
}

/**
 * @brief Main entry point
 */
int main(int argc, char *argv[]) {
    printf("===========================================\n");
    printf("   Porpoise Tool - PowerPC to C Transpiler\n");
    printf("   For GameCube/Wii Decompilation Projects\n");
    printf("===========================================\n");
    printf("   Opcodes: %d / %d (%.1f%% - COMPLETE!) 🎉\n",
           get_implemented_opcode_count(),
           get_implemented_opcode_count(),
           get_implementation_progress());
    printf("===========================================\n\n");
    
    // Load configuration from config.json (beside executable)
    load_config();
    
    // Load SDK functions configuration (use path from config if specified)
    load_sdk_functions(config.sdk_functions_file);
    
    // Check for help flag
    bool show_help = (argc < 2);
    if (argc >= 2) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 || 
            strcmp(argv[1], "-?") == 0 || strcmp(argv[1], "/?") == 0) {
            show_help = true;
        }
    }
    
    if (show_help) {
        printf("Porpoise transpiles PowerPC assembly (.s files) to portable C code.\n\n");
        
        printf("USAGE:\n");
        printf("  %s <input_dir> [output_project] [skip_list.txt]\n", argv[0]);
        printf("  %s --help | -h | -? | /?     Show this help message\n\n", argv[0]);
        
        printf("ARGUMENTS:\n");
        printf("  <input_dir>         Directory containing .s assembly files to transpile\n");
        printf("  [output_project]    Output project directory (default: GameCube_Project)\n");
        printf("  [skip_list.txt]     Optional text file with function names to skip (one per line)\n\n");
        
        printf("FEATURES:\n");
        printf("  • Transpiles 248 PowerPC + Gekko opcodes (100%% coverage!)\n");
        printf("  • Automatic parameter detection from register usage\n");
        printf("  • Generates complete CMake project with headers and source files\n");
        printf("  • Preserves labels, data sections, and function boundaries\n");
        printf("  • Cross-platform compatible C output (Windows/Linux/Mac)\n");
        printf("  • Runtime environment for emulated registers and memory\n\n");
        
        printf("EXAMPLES:\n");
        printf("  Basic usage (generates GameCube_Project/):\n");
        printf("    %s \"Test Asm\"\n\n", argv[0]);
        
        printf("  Custom output project name:\n");
        printf("    %s \"AirRide\" MyGame\n\n", argv[0]);
        
        printf("  Skip specific functions during transpilation:\n");
        printf("    %s \"Test Asm\" MyGame skip_functions.txt\n\n", argv[0]);
        
        printf("SKIP LIST FORMAT:\n");
        printf("  Create a text file with one function name per line:\n");
        printf("    fn_80003100\n");
        printf("    fn_80003200\n");
        printf("    InitSystem\n\n");
        
        printf("OUTPUT:\n");
        printf("  • <project>/src/       - Transpiled C source files\n");
        printf("  • <project>/include/   - Header files and declarations\n");
        printf("  • <project>/CMakeLists.txt - Build configuration\n");
        printf("  • <project>/<project>.sln - Visual Studio solution (on Windows)\n\n");
        
        printf("BUILDING THE OUTPUT:\n");
        printf("  cd <output_project>\n");
        printf("  cmake -B build\n");
        printf("  cmake --build build\n\n");
        
        printf("For more information, see docs/QUICK_START.md\n\n");
        return (argc < 2) ? 1 : 0;  // Error if no args, success if --help
    }
    
    const char *input_dir = argv[1];
    const char *output_project = (argc >= 3) ? argv[2] : "GameCube_Project";
    const char *skip_file = (argc >= 4) ? argv[3] : NULL;
    
    // Use skip_list_file from config if not provided via command line
    if (!skip_file && config.skip_list_file[0] != '\0') {
        skip_file = config.skip_list_file;
    }
    
    // Initialize skip list
    SkipList skip_list;
    skiplist_init(&skip_list);
    
    if (skip_file) {
        int loaded = skiplist_load_from_file(&skip_list, skip_file);
        if (loaded >= 0) {
            printf("Loaded skip list: %d functions to skip\n\n", loaded);
        } else {
            printf("Warning: Could not load skip list file %s\n\n", skip_file);
        }
    }
    
    // Create project structure
    printf("\n===========================================\n");
    printf("   Creating Project: %s\n", output_project);
    printf("===========================================\n");
    
    create_directory(output_project);
    
    char src_dir[512], inc_dir[512];
    snprintf(src_dir, sizeof(src_dir), "%s/src", output_project);
    snprintf(inc_dir, sizeof(inc_dir), "%s/include", output_project);
    
    create_directory(src_dir);
    create_directory(inc_dir);
    
    printf("Processing assembly files from: %s (recursive)\n\n", input_dir);
    
    // Process all .s files recursively and output directly to project
    int files_processed = 0;
    int max_files = 5000;  // Support large games with thousands of files
    char **c_files = malloc(sizeof(char*) * max_files);
    char **h_files = malloc(sizeof(char*) * max_files);
    int file_count = 0;
    
    if (!c_files || !h_files) {
        fprintf(stderr, "Error: Failed to allocate memory for file arrays\n");
        return 1;
    }
    
    // Process recursively starting from root
    process_directory_recursive(input_dir, src_dir, inc_dir, "",
                               &skip_list, &files_processed,
                               c_files, h_files, &file_count, max_files);
    
    printf("\n===========================================\n");
    printf("   Transpilation Complete!\n");
    printf("   Files processed: %d\n", files_processed);
    printf("===========================================\n\n");
    
    // Generate CMake project files
    printf("Generating CMake project files...\n");
    
    // Extract project name from path
    const char *proj_name = strrchr(output_project, '/');
    if (!proj_name) proj_name = strrchr(output_project, '\\');
    proj_name = proj_name ? (proj_name + 1) : output_project;
    
    // Add powerpc_state.c, compiler_runtime.c, and main.c to sources
    int total_c_count = file_count + 3;
    char **all_c_files = malloc(sizeof(char*) * total_c_count);
    for (int i = 0; i < file_count; i++) {
        all_c_files[i] = c_files[i];
    }
    all_c_files[file_count] = strdup("powerpc_state.c");
    all_c_files[file_count + 1] = strdup("compiler_runtime.c");
    all_c_files[file_count + 2] = strdup("main.c");
    
    // Add powerpc_state.h, all_functions.h, and macros.h to headers
    int total_h_count = file_count + 3;
    char **all_h_files = malloc(sizeof(char*) * total_h_count);
    for (int i = 0; i < file_count; i++) {
        all_h_files[i] = h_files[i];
    }
    all_h_files[file_count] = strdup("powerpc_state.h");
    all_h_files[file_count + 1] = strdup("all_functions.h");
    all_h_files[file_count + 2] = strdup("macros.h");
    
    generate_cmake(output_project, proj_name, total_c_count, (const char**)all_c_files, 
                   total_h_count, (const char**)all_h_files);
    generate_all_functions_h(output_project, file_count, (const char**)h_files);
    generate_runtime_h(output_project);
    generate_runtime_c(output_project);
    generate_compiler_runtime_c(output_project);
    generate_main_c(output_project);
    generate_macros_h(output_project);
    
    // Copy core headers to project
    printf("Copying core headers to project...\n");
    
    // Generate stdlib_headers.h with config settings
    char stdlib_dst[512];
    snprintf(stdlib_dst, sizeof(stdlib_dst), "%s/include/stdlib_headers.h", output_project);
    FILE *dst_file = fopen(stdlib_dst, "w");
    if (dst_file) {
        fprintf(dst_file, "/**\n");
        fprintf(dst_file, " * @file stdlib_headers.h\n");
        fprintf(dst_file, " * @brief Standard library headers for transpiled code\n");
        fprintf(dst_file, " * \n");
        fprintf(dst_file, " * This header includes all standard C library headers needed for\n");
        fprintf(dst_file, " * transpiled GameCube/Wii PowerPC assembly code.\n");
        fprintf(dst_file, " * Include this in all generated .c files.\n");
        fprintf(dst_file, " */\n\n");
        fprintf(dst_file, "#ifndef STDLIB_HEADERS_H\n");
        fprintf(dst_file, "#define STDLIB_HEADERS_H\n\n");
        fprintf(dst_file, "// Standard library includes (BEFORE redeclarations)\n");
        fprintf(dst_file, "#include <stdint.h>\n");
        fprintf(dst_file, "#include <stdbool.h>\n");
        fprintf(dst_file, "#include <stddef.h>\n\n");
        fprintf(dst_file, "// PowerPC runtime library (64-bit intrinsics)\n");
        fprintf(dst_file, "#include \"ppc_runtime.h\"\n\n");
        fprintf(dst_file, "// Include stdlib stub declarations that match transpiler's 10-parameter signature\n");
        fprintf(dst_file, "// These override the standard library declarations\n");
        fprintf(dst_file, "// NOTE: If the project already has MSL_C headers (Metrowerks), they take precedence\n");
        if (config.skip_stdlib_stubs) {
            fprintf(dst_file, "#define SKIP_STDLIB_STUBS 1\n");
        }
        fprintf(dst_file, "#ifndef SKIP_STDLIB_STUBS\n");
        fprintf(dst_file, "    #include \"stdlib_stubs.h\"\n");
        fprintf(dst_file, "#endif\n\n");
        fprintf(dst_file, "// Suppress warnings for unused parameters (common in transpiled code)\n");
        fprintf(dst_file, "#ifdef _MSC_VER\n");
        fprintf(dst_file, "    #pragma warning(disable: 4100)  // unreferenced formal parameter\n");
        fprintf(dst_file, "    #pragma warning(disable: 4189)  // local variable initialized but not used\n");
        fprintf(dst_file, "    #pragma warning(disable: 4702)  // unreachable code\n");
        fprintf(dst_file, "    #pragma warning(disable: 4310)  // cast truncates constant value\n");
        fprintf(dst_file, "    #pragma warning(disable: 4146)  // unary minus on unsigned type\n");
        fprintf(dst_file, "#endif\n\n");
        fprintf(dst_file, "#endif // STDLIB_HEADERS_H\n");
        fclose(dst_file);
    }
    
    // List of other headers to copy
    const char *headers_to_copy[] = {
        "stdlib_stubs.h",
        "gecko_memory.h",
        "function_address_map.h",
        NULL
    };
    
    for (int i = 0; headers_to_copy[i] != NULL; i++) {
        char src_path[512], dst_path[512];
        snprintf(src_path, sizeof(src_path), "include/%s", headers_to_copy[i]);
        snprintf(dst_path, sizeof(dst_path), "%s/include/%s", output_project, headers_to_copy[i]);
        
        FILE *src_file = fopen(src_path, "r");
        FILE *dst_file = fopen(dst_path, "w");
        if (src_file && dst_file) {
            char buffer[4096];
            size_t bytes;
            while ((bytes = fread(buffer, 1, sizeof(buffer), src_file)) > 0) {
                fwrite(buffer, 1, bytes, dst_file);
            }
            fclose(src_file);
            fclose(dst_file);
        } else {
            fprintf(stderr, "Warning: Could not copy %s\n", headers_to_copy[i]);
        }
    }
    
    // Copy function_address_map.c source file
    char src_c_path[512], dst_c_path[512];
    snprintf(src_c_path, sizeof(src_c_path), "src/function_address_map.c");
    snprintf(dst_c_path, sizeof(dst_c_path), "%s/src/function_address_map.c", output_project);
    FILE *src_c = fopen(src_c_path, "r");
    FILE *dst_c = fopen(dst_c_path, "w");
    if (src_c && dst_c) {
        char buffer[4096];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), src_c)) > 0) {
            fwrite(buffer, 1, bytes, dst_c);
        }
        fclose(src_c);
        fclose(dst_c);
        printf("  Copied function_address_map.c\n");
    } else {
        fprintf(stderr, "Warning: Could not copy function_address_map.c\n");
    }
    
    // Copy ppc_runtime.c source file
    snprintf(src_c_path, sizeof(src_c_path), "src/ppc_runtime.c");
    snprintf(dst_c_path, sizeof(dst_c_path), "%s/src/ppc_runtime.c", output_project);
    src_c = fopen(src_c_path, "r");
    dst_c = fopen(dst_c_path, "w");
    if (src_c && dst_c) {
        char buffer[4096];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), src_c)) > 0) {
            fwrite(buffer, 1, bytes, dst_c);
        }
        fclose(src_c);
        fclose(dst_c);
        printf("  Copied ppc_runtime.c\n");
    } else {
        fprintf(stderr, "Warning: Could not copy ppc_runtime.c (tried: %s)\n", src_c_path);
    }
    
    // Copy ppc_runtime.h header file (if not already in headers_to_copy list)
    char ppc_runtime_h_src[512], ppc_runtime_h_dst[512];
    snprintf(ppc_runtime_h_src, sizeof(ppc_runtime_h_src), "include/ppc_runtime.h");
    snprintf(ppc_runtime_h_dst, sizeof(ppc_runtime_h_dst), "%s/include/ppc_runtime.h", output_project);
    FILE *ppc_runtime_h_src_file = fopen(ppc_runtime_h_src, "r");
    FILE *ppc_runtime_h_dst_file = fopen(ppc_runtime_h_dst, "w");
    if (ppc_runtime_h_src_file && ppc_runtime_h_dst_file) {
        char buffer[4096];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), ppc_runtime_h_src_file)) > 0) {
            fwrite(buffer, 1, bytes, ppc_runtime_h_dst_file);
        }
        fclose(ppc_runtime_h_src_file);
        fclose(ppc_runtime_h_dst_file);
        printf("  Copied ppc_runtime.h\n");
    } else {
        fprintf(stderr, "Warning: Could not copy ppc_runtime.h (tried: %s)\n", ppc_runtime_h_src);
    }
    
    // Copy sdk_functions.txt to project (needed for SDK function signatures)
    char sdk_funcs_src[512], sdk_funcs_dst[512];
    snprintf(sdk_funcs_src, sizeof(sdk_funcs_src), "sdk_functions.txt");
    snprintf(sdk_funcs_dst, sizeof(sdk_funcs_dst), "%s/sdk_functions.txt", output_project);
    FILE *sdk_funcs_src_file = fopen(sdk_funcs_src, "r");
    FILE *sdk_funcs_dst_file = fopen(sdk_funcs_dst, "w");
    if (sdk_funcs_src_file && sdk_funcs_dst_file) {
        char buffer[4096];
        size_t bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), sdk_funcs_src_file)) > 0) {
            fwrite(buffer, 1, bytes, sdk_funcs_dst_file);
        }
        fclose(sdk_funcs_src_file);
        fclose(sdk_funcs_dst_file);
        printf("  Copied sdk_functions.txt\n");
    } else {
        fprintf(stderr, "Warning: Could not copy sdk_functions.txt\n");
    }
    
    // Generate skip_sdk_functions.txt next to the tool executable (not in project)
    // This file is used by the transpiler tool, not the project
    char skip_sdk_tool[512];
    snprintf(skip_sdk_tool, sizeof(skip_sdk_tool), "skip_sdk_functions.txt");
    FILE *skip_sdk_file = fopen(skip_sdk_tool, "w");
    if (skip_sdk_file) {
        fprintf(skip_sdk_file, "# Skip list for SDK functions\n");
        fprintf(skip_sdk_file, "# Generated automatically from sdk_functions.txt\n");
        fprintf(skip_sdk_file, "# These functions should not be transpiled as they are provided by the SDK\n");
        fprintf(skip_sdk_file, "# This file should be used with the transpiler tool: porpoise_tool.exe \"Project\" \"Input\" \"Output\" skip_sdk_functions.txt\n\n");
        
        // Read sdk_functions.txt to extract function names
        sdk_funcs_src_file = fopen(sdk_funcs_src, "r");
        if (sdk_funcs_src_file) {
            char line[256];
            while (fgets(line, sizeof(line), sdk_funcs_src_file)) {
                // Skip comments and empty lines
                if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
                
                // Extract function name (before the colon)
                char func_name[128];
                if (sscanf(line, "%127[^:\n]", func_name) == 1) {
                    fprintf(skip_sdk_file, "%s\n", func_name);
                }
            }
            fclose(sdk_funcs_src_file);
        }
        fclose(skip_sdk_file);
        printf("  Generated skip_sdk_functions.txt (next to tool executable)\n");
    }
    
    generate_readme(output_project, proj_name);
    generate_gitignore(output_project);
    
    // Generate function registry for indirect call resolution
    printf("Generating function registry (%d functions)...\n", function_registry_count);
    generate_function_registry(output_project);
    
    // Cleanup
    for (int i = 0; i < file_count; i++) {
        free(c_files[i]);
        free(h_files[i]);
    }
    free(c_files);
    free(h_files);
    
    // Free the combined arrays
    for (int i = file_count; i < total_c_count; i++) {
        free(all_c_files[i]);
    }
    free(all_c_files);
    
    for (int i = file_count; i < total_h_count; i++) {
        free(all_h_files[i]);
    }
    free(all_h_files);
    
    printf("\n===========================================\n");
    printf("   CMake Project Generated!\n");
    printf("   Location: %s\n", output_project);
    printf("===========================================\n\n");
    
    printf("To build the project:\n");
    printf("  cd %s\n", output_project);
    printf("  mkdir build && cd build\n");
    printf("  cmake ..\n");
    printf("  cmake --build .\n\n");
    
    return 0;
}

