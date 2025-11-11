/**
 * @file porpoise_tool.h
 * @brief Porpoise Tool - PowerPC to C Transpiler for GameCube/Wii
 * 
 * A joke on "Dolphin" (GameCube codename) - porpoises are similar to dolphins!
 * 
 * This tool transpiles PowerPC assembly (.s files) to C code.
 * Designed for decompilation projects of GameCube/Wii games.
 */

#ifndef PORPOISE_TOOL_H
#define PORPOISE_TOOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef __cplusplus
extern "C" {
#endif

//==============================================================================
// CONFIGURATION
//==============================================================================

#define MAX_LINE_LENGTH         1024
#define MAX_FUNCTION_NAME       128
#define MAX_LABEL_NAME          128
#define MAX_SKIP_FUNCTIONS      256
#define MAX_OUTPUT_BUFFER       (64 * 1024)  // 64 KB per function
#define MAX_LOOKBACK_LINES      20           // How many lines to look back for parameters
#define MAX_LABELS              10000        // Maximum number of labels to track

//==============================================================================
// RESERVED NAME HANDLING
//==============================================================================

/**
 * @brief Check if a function name is reserved/conflicts with compiler intrinsics or standard library
 * @param name Function name to check
 * @return true if name should be sanitized, false otherwise
 */
static inline bool is_reserved_name(const char *name) {
    // Compiler intrinsics and reserved names
    const char *reserved[] = {
        "__va_arg",      // Variable argument intrinsic
        "__va_start",    // Variable argument start
        "__va_end",      // Variable argument end
        "__va_copy",     // Variable argument copy
        "__builtin",     // GCC builtins (prefix check)
        // Standard C library functions that conflict with declarations
        "main",          // Program entry point (reserved)
        "printf",        // stdio.h
        "fprintf",       // stdio.h
        "sprintf",       // stdio.h
        "snprintf",      // stdio.h
        "vprintf",       // stdio.h
        "vfprintf",      // stdio.h
        "vsprintf",      // stdio.h
        "vsnprintf",     // stdio.h
        "scanf",         // stdio.h
        "fscanf",        // stdio.h
        "sscanf",        // stdio.h
        "fopen",         // stdio.h
        "fclose",        // stdio.h
        "fread",         // stdio.h
        "fwrite",        // stdio.h
        "fseek",         // stdio.h
        "ftell",         // stdio.h
        "fgetc",         // stdio.h
        "fputc",         // stdio.h
        "fgets",         // stdio.h
        "fputs",         // stdio.h
        "getc",          // stdio.h
        "putc",          // stdio.h
        "ungetc",        // stdio.h
        "malloc",        // stdlib.h
        "calloc",        // stdlib.h
        "realloc",       // stdlib.h
        "free",          // stdlib.h
        "atoi",          // stdlib.h
        "atof",          // stdlib.h
        "atol",          // stdlib.h
        "strtod",        // stdlib.h
        "strtol",        // stdlib.h
        "strtoul",       // stdlib.h
        "rand",          // stdlib.h - random number
        "srand",         // stdlib.h - seed random
        "exit",          // stdlib.h - exit program
        "abort",         // stdlib.h - abort program
        "_Exit",         // stdlib.h - exit without cleanup
        "atexit",        // stdlib.h - register exit handler
        "abs",           // stdlib.h - absolute value (conflicts with transpiled version)
        "memcpy",        // string.h
        "memmove",       // string.h
        "memset",        // string.h
        "memcmp",        // string.h
        "strcpy",        // string.h
        "strncpy",       // string.h
        "strcat",        // string.h
        "strncat",       // string.h
        "strcmp",        // string.h
        "strncmp",       // string.h
        "strchr",        // string.h
        "strrchr",       // string.h
        "strstr",        // string.h
        "strlen",        // string.h
        "strerror",      // string.h
        "wcstombs",      // stdlib.h - wide char conversion
        "mbstowcs",      // stdlib.h - multibyte conversion
        "wctomb",        // stdlib.h
        "mbtowc",        // stdlib.h
        // Math functions - single precision (float)
        "sinf",          // math.h
        "cosf",          // math.h
        "tanf",          // math.h
        "asinf",         // math.h
        "acosf",         // math.h
        "atanf",         // math.h
        "atan2f",        // math.h
        "sinhf",         // math.h
        "coshf",         // math.h
        "tanhf",         // math.h
        "expf",          // math.h
        "logf",          // math.h
        "log10f",        // math.h
        "sqrtf",         // math.h
        "powf",          // math.h
        "fabsf",         // math.h
        "floorf",        // math.h
        "ceilf",         // math.h
        "roundf",        // math.h
        "truncf",        // math.h
        "fmodf",         // math.h
        "hypotf",        // math.h
        "copysignf",     // math.h
        "fdimf",         // math.h
        "fmaxf",         // math.h
        "fminf",         // math.h
        "fmaf",          // math.h
        // Math functions - double precision
        "sin",           // math.h
        "cos",           // math.h
        "tan",           // math.h
        "asin",          // math.h
        "acos",          // math.h
        "atan",          // math.h
        "atan2",         // math.h
        "sinh",          // math.h
        "cosh",          // math.h
        "tanh",          // math.h
        "exp",           // math.h
        "log",           // math.h
        "log10",         // math.h
        "sqrt",          // math.h
        "pow",           // math.h
        "fabs",          // math.h
        "floor",         // math.h
        "ceil",          // math.h
        "round",         // math.h
        "trunc",         // math.h
        "fmod",          // math.h
        "hypot",         // math.h
        "copysign",      // math.h
        "fdim",          // math.h
        "fmax",          // math.h
        "fmin",          // math.h
        "fma",           // math.h
        NULL
    };
    
    // Check exact matches
    for (int i = 0; reserved[i] != NULL; i++) {
        if (strcmp(name, reserved[i]) == 0) {
            return true;
        }
    }
    
    // Check for __builtin prefix
    if (strncmp(name, "__builtin", 9) == 0) {
        return true;
    }
    
    return false;
}

/**
 * @brief Sanitize a function name by removing quotes and special characters
 * @param name Original function name
 * @param output Buffer to store sanitized name
 * @param output_size Size of output buffer
 * @return Pointer to output buffer
 */
static inline const char* sanitize_function_name(const char *name, char *output, size_t output_size) {
    static char stub_name[256];  // Static for stub names
    
    // Create a clean copy without quotes first
    char clean_name[512];
    strncpy(clean_name, name, sizeof(clean_name) - 1);
    clean_name[sizeof(clean_name) - 1] = '\0';
    
    // Skip leading and trailing quotes
    if (clean_name[0] == '"') {
        size_t len = strlen(clean_name);
        memmove(clean_name, clean_name + 1, len);
        len = strlen(clean_name);
        if (len > 0 && clean_name[len - 1] == '"') {
            clean_name[len - 1] = '\0';
        }
    }
    
    // Check if it needs to be stubbed (contains problematic chars or is too long)
    if (strlen(clean_name) > 80 || strchr(clean_name, '<') != NULL || 
        strchr(clean_name, '>') != NULL || strchr(clean_name, ',') != NULL ||
        strchr(clean_name, '@') != NULL) {
        // Create stub name based on hash
        unsigned int hash = 0;
        for (const char *p = clean_name; *p; p++) {
            hash = hash * 31 + (unsigned char)*p;
        }
        snprintf(stub_name, sizeof(stub_name), "cpp_stub_func_%08x", hash);
        return stub_name;
    }
    
    // Otherwise, sanitize normally (replace invalid chars with underscores)
    const char *src = clean_name;
    char *dst = output;
    size_t remaining = output_size - 1; // Leave room for null terminator
    
    // Copy and sanitize characters
    while (*src && remaining > 0) {
        // Replace invalid C identifier characters
        if ((*src >= 'a' && *src <= 'z') ||
            (*src >= 'A' && *src <= 'Z') ||
            (*src >= '0' && *src <= '9') ||
            *src == '_') {
            // Valid identifier character
            *dst++ = *src++;
            remaining--;
        } else {
            // Invalid character - replace with underscore
            *dst++ = '_';
            src++;
            remaining--;
        }
    }
    
    *dst = '\0';
    
    // Check if the sanitized name is a reserved word
    if (is_reserved_name(output)) {
        // Add _impl suffix
        size_t len = strlen(output);
        if (len + 5 < output_size) { // "_impl" is 5 chars
            strcat(output, "_impl");
        }
    }
    
    return output;
}

//==============================================================================
// STRUCTURES
//==============================================================================

/**
 * @brief String table entry for tracking .string/.asciz directives
 */
typedef struct {
    uint32_t address;           // Address of the string in memory
    char content[256];          // String content (escaped)
    char label[64];             // Generated label name (e.g., str_80004000)
} StringEntry;

/**
 * @brief String table for tracking all strings in the file
 */
typedef struct {
    StringEntry *entries;
    int count;
    int capacity;
} StringTable;

/**
 * @brief Function skip list configuration
 */
typedef struct {
    char function_names[MAX_SKIP_FUNCTIONS][MAX_FUNCTION_NAME];
    int count;
} SkipList;

/**
 * @brief Label to function mapping (for trampoline resolution)
 */
typedef struct {
    uint32_t address;                      // Label address
    char function_name[MAX_FUNCTION_NAME]; // Function containing this label
} LabelMapping;

typedef struct {
    LabelMapping *mappings;
    int count;
    int capacity;
} LabelMap;

//==============================================================================
// STRING TABLE FUNCTIONS
//==============================================================================

/**
 * @brief Initialize string table
 */
static inline StringTable* string_table_init(void) {
    StringTable *table = (StringTable*)malloc(sizeof(StringTable));
    if (!table) return NULL;
    table->capacity = 100;
    table->count = 0;
    table->entries = (StringEntry*)malloc(sizeof(StringEntry) * table->capacity);
    if (!table->entries) {
        free(table);
        return NULL;
    }
    return table;
}

/**
 * @brief Add a string to the table
 */
static inline void string_table_add(StringTable *table, uint32_t address, const char *content) {
    if (!table || !content) return;
    
    // Grow if needed
    if (table->count >= table->capacity) {
        table->capacity *= 2;
        table->entries = (StringEntry*)realloc(table->entries, sizeof(StringEntry) * table->capacity);
        if (!table->entries) return;
    }
    
    StringEntry *entry = &table->entries[table->count];
    entry->address = address;
    
    // Copy and escape string content
    strncpy(entry->content, content, sizeof(entry->content) - 1);
    entry->content[sizeof(entry->content) - 1] = '\0';
    
    // Generate label name
    snprintf(entry->label, sizeof(entry->label), "str_%08X", address);
    
    table->count++;
}

/**
 * @brief Find string by address
 */
static inline const StringEntry* string_table_find(const StringTable *table, uint32_t address) {
    if (!table) return NULL;
    for (int i = 0; i < table->count; i++) {
        if (table->entries[i].address == address) {
            return &table->entries[i];
        }
    }
    return NULL;
}

/**
 * @brief Free string table
 */
static inline void string_table_free(StringTable *table) {
    if (!table) return;
    if (table->entries) free(table->entries);
    free(table);
}

/**
 * @brief Parsed assembly line
 */
typedef struct {
    uint32_t address;           // Instruction address
    uint32_t instruction;       // 32-bit instruction code
    char mnemonic[32];          // Assembly mnemonic
    char operands[128];         // Operand string
    char original[MAX_LINE_LENGTH]; // Original line
    bool is_label;              // Is this a label line?
    bool is_function;           // Is this a function start?
    bool is_local_function;     // Is this a local/static function?
    bool is_data;               // Is this data section?
    bool is_comment;            // Is this a comment?
    bool is_directive;          // Is this an assembler directive?
    char label_name[MAX_LABEL_NAME];
    char function_name[MAX_FUNCTION_NAME];
} ASM_Line;

// Forward declaration of parse_asm_line (used by label mapping)
static inline bool parse_asm_line(const char *line, ASM_Line *parsed);

/**
 * @brief Function metadata
 */
typedef struct {
    char name[MAX_FUNCTION_NAME];
    uint32_t start_address;
    uint32_t end_address;
    uint32_t size;
    bool is_global;
    bool is_local;              // Is this a local/static function?
    bool skip;                  // Skip transpiling this function
    int instruction_count;      // Number of instructions in function
    bool is_trampoline;         // Is this a trampoline (single branch)?
    uint32_t trampoline_target; // Target address for trampoline
    bool returns_value;         // Does this function return a value in r3?
    bool is_data_only;          // Is this function actually just data (strings, tables, etc.)?
    
    // Parameter detection
    bool has_params;            // Does this function have parameters?
    bool param_r3, param_r4, param_r5, param_r6;
    bool param_r7, param_r8, param_r9, param_r10;
    bool param_f1, param_f2;    // Float parameters (first 2 for now)
    int num_int_params;         // Number of integer parameters
    int num_float_params;       // Number of float parameters
} Function_Info;

/**
 * @brief File context for transpilation
 */
typedef struct {
    char input_filename[256];
    char output_c_filename[256];
    char output_h_filename[256];
    
    FILE *input_file;
    FILE *output_c;
    FILE *output_h;
    
    Function_Info *current_function;
    SkipList *skip_list;
    
    bool in_data_section;
    bool in_text_section;
} Transpiler_Context;

//==============================================================================
// SKIP LIST FUNCTIONS
//==============================================================================

/**
 * @brief Initialize skip list
 */
static inline void skiplist_init(SkipList *list) {
    list->count = 0;
}

/**
 * @brief Add function to skip list
 */
static inline bool skiplist_add(SkipList *list, const char *function_name) {
    if (list->count >= MAX_SKIP_FUNCTIONS) {
        return false;
    }
    
    strncpy(list->function_names[list->count], function_name, MAX_FUNCTION_NAME - 1);
    list->function_names[list->count][MAX_FUNCTION_NAME - 1] = '\0';
    list->count++;
    
    return true;
}

/**
 * @brief Check if function should be skipped
 */
static inline bool skiplist_should_skip(const SkipList *list, const char *function_name) {
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->function_names[i], function_name) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Load skip list from file
 * @param list Skip list to populate
 * @param filename Path to skip list file (one function name per line)
 * @return Number of functions loaded, or -1 on error
 */
static inline int skiplist_load_from_file(SkipList *list, const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        return -1;
    }
    
    char line[MAX_FUNCTION_NAME];
    while (fgets(line, sizeof(line), f)) {
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        
        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        
        skiplist_add(list, line);
    }
    
    fclose(f);
    return list->count;
}

//==============================================================================
// LABEL MAPPING (For Trampoline Resolution)
//==============================================================================

/**
 * @brief Initialize label map
 */
static inline LabelMap* labelmap_init(void) {
    LabelMap *map = (LabelMap*)malloc(sizeof(LabelMap));
    if (!map) return NULL;
    map->capacity = 1000;
    map->count = 0;
    map->mappings = (LabelMapping*)malloc(sizeof(LabelMapping) * map->capacity);
    if (!map->mappings) {
        free(map);
        return NULL;
    }
    return map;
}

/**
 * @brief Add a label to the map
 */
static inline void labelmap_add(LabelMap *map, uint32_t address, const char *function_name) {
    if (!map || !function_name) return;
    
    // Grow if needed
    if (map->count >= map->capacity) {
        map->capacity *= 2;
        map->mappings = (LabelMapping*)realloc(map->mappings, sizeof(LabelMapping) * map->capacity);
        if (!map->mappings) return;
    }
    
    map->mappings[map->count].address = address;
    strncpy(map->mappings[map->count].function_name, function_name, MAX_FUNCTION_NAME - 1);
    map->mappings[map->count].function_name[MAX_FUNCTION_NAME - 1] = '\0';
    map->count++;
}

/**
 * @brief Find which function contains a label address
 */
static inline const char* labelmap_find_function(const LabelMap *map, uint32_t address) {
    if (!map) return NULL;
    for (int i = 0; i < map->count; i++) {
        if (map->mappings[i].address == address) {
            return map->mappings[i].function_name;
        }
    }
    return NULL;
}

/**
 * @brief Free label map
 */
static inline void labelmap_free(LabelMap *map) {
    if (!map) return;
    if (map->mappings) free(map->mappings);
    free(map);
}

/**
 * @brief Parse .string or .asciz directive and extract address
 * @param line Assembly line containing string directive
 * @param address_out Output address where string is located
 * @param content_out Output buffer for string content
 * @param content_size Size of content buffer
 * @return true if successfully parsed
 */
static inline bool parse_string_directive(const char *line, uint32_t *address_out, 
                                          char *content_out, size_t content_size) {
    // Look for address comment like: # .rodata:0x8 | 0x804D8C08 | size: 0x20
    const char *comment = strchr(line, '#');
    if (!comment) return false;
    
    // Try to extract address from comment
    uint32_t addr = 0;
    const char *addr_marker = strstr(comment, "0x");
    if (addr_marker) {
        // Skip the first occurrence (might be offset), look for the actual address
        const char *next_addr = strstr(addr_marker + 2, "0x");
        if (next_addr) {
            sscanf(next_addr, "%x", &addr);
        } else {
            sscanf(addr_marker, "%x", &addr);
        }
    }
    
    if (addr == 0) return false;
    
    // Extract string content between quotes
    const char *quote_start = strchr(line, '"');
    if (!quote_start) return false;
    
    const char *quote_end = strchr(quote_start + 1, '"');
    if (!quote_end) return false;
    
    size_t len = quote_end - quote_start - 1;
    if (len >= content_size) len = content_size - 1;
    
    strncpy(content_out, quote_start + 1, len);
    content_out[len] = '\0';
    
    *address_out = addr;
    return true;
}

/**
 * @brief Build string table by pre-scanning file for .string/.asciz directives
 */
static inline StringTable* build_string_table(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return NULL;
    
    StringTable *table = string_table_init();
    if (!table) {
        fclose(f);
        return NULL;
    }
    
    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), f)) {
        // Look for .string or .asciz directives
        if (strstr(line, ".string") != NULL || strstr(line, ".asciz") != NULL) {
            uint32_t address;
            char content[256];
            
            if (parse_string_directive(line, &address, content, sizeof(content))) {
                string_table_add(table, address, content);
            }
        }
    }
    
    fclose(f);
    
    fprintf(stderr, "  [Debug: Found %d string(s) in file]\n", table->count);
    return table;
}

/**
 * @brief Build label-to-function map by pre-scanning file
 */
static inline LabelMap* build_label_map(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return NULL;
    
    LabelMap *map = labelmap_init();
    if (!map) {
        fclose(f);
        return NULL;
    }
    
    char line[MAX_LINE_LENGTH];
    char current_function[MAX_FUNCTION_NAME] = {0};
    bool in_function = false;
    
    while (fgets(line, sizeof(line), f)) {
        ASM_Line parsed;
        if (!parse_asm_line(line, &parsed)) continue;
        
        // Track function boundaries
        if (parsed.is_function) {
            strncpy(current_function, parsed.function_name, MAX_FUNCTION_NAME - 1);
            current_function[MAX_FUNCTION_NAME - 1] = '\0';
            in_function = true;
        }
        
        if (parsed.is_directive && strstr(line, ".endfn")) {
            in_function = false;
            current_function[0] = '\0';
        }
        
        // Track labels within functions
        if (parsed.is_label && in_function && current_function[0] != '\0') {
            // Extract address from label name (e.g., L_8043D430 -> 0x8043D430)
            const char *label = parsed.label_name;
            uint32_t addr = 0;
            
            // Check for L_ or lbl_ pattern labels
            if ((label[0] == 'L' && label[1] == '_') || 
                (strncmp(label, "lbl_", 4) == 0)) {
                // Try L_XXXXXXXX format
                if (label[0] == 'L' && label[1] == '_') {
                    sscanf(label + 2, "%x", &addr);
                }
                // Try lbl_XXXXXXXX format
                else if (strncmp(label, "lbl_", 4) == 0) {
                    sscanf(label + 4, "%x", &addr);
                }
            }
            // Check for .sym named labels (e.g., GXPerf_80341E40)
            else {
                // Extract address from symbolic name (format: Name_HEXADDR)
                const char *underscore = strrchr(label, '_');
                if (underscore) {
                    sscanf(underscore + 1, "%x", &addr);
                }
            }
            
            if (addr != 0) {
                labelmap_add(map, addr, current_function);
            }
        }
    }
    
    fclose(f);
    
    // Debug: Report how many labels were mapped
    if (map) {
        fprintf(stderr, "  [Debug: Mapped %d labels in file]\n", map->count);
    }
    
    return map;
}

//==============================================================================
// PARSING FUNCTIONS
//==============================================================================

/**
 * @brief Parse a line of assembly
 * @param line Input assembly line
 * @param parsed Output parsed line structure
 * @return true if line was parsed successfully
 */
static inline bool parse_asm_line(const char *line, ASM_Line *parsed) {
    memset(parsed, 0, sizeof(ASM_Line));
    strncpy(parsed->original, line, MAX_LINE_LENGTH - 1);
    
    // Skip leading whitespace
    const char *p = line;
    while (*p && isspace(*p)) p++;
    
    // Empty line or comment starting with #
    if (*p == '\0' || *p == '#') {
        parsed->is_comment = true;
        return true;
    }
    
    // Check for .include directive
    if (strncmp(p, ".include", 8) == 0) {
        parsed->is_directive = true;
        return true;
    }
    
    // Check for .hidden directive (not a function)
    if (strncmp(p, ".hidden", 7) == 0) {
        parsed->is_directive = true;
        return true;
    }
    
    // Check for other common directives
    if (strncmp(p, ".align", 6) == 0 || strncmp(p, ".balign", 7) == 0 ||
        strncmp(p, ".section", 8) == 0 || strncmp(p, ".file", 5) == 0 ||
        strncmp(p, ".global", 7) == 0 || strncmp(p, ".weak", 5) == 0 ||
        strncmp(p, ".obj", 4) == 0 || strncmp(p, ".endobj", 7) == 0 ||
        strncmp(p, ".float", 6) == 0 || strncmp(p, ".4byte", 6) == 0 ||
        strncmp(p, ".byte", 5) == 0 || strncmp(p, ".2byte", 6) == 0 ||
        strncmp(p, ".string", 7) == 0 || strncmp(p, ".asciz", 6) == 0) {
        parsed->is_directive = true;
        return true;
    }
    
    // Check for .fn (function start)
    if (strncmp(p, ".fn", 3) == 0) {
        parsed->is_function = true;
        parsed->is_local_function = false;  // Default to global
        
        // Extract function name
        p += 3;
        while (*p && isspace(*p)) p++;
        sscanf(p, "%127[^, \t\n]", parsed->function_name);
        
        // Check for ", local" or ", global" modifier
        const char *comma = strchr(p, ',');
        if (comma) {
            comma++;
            while (*comma && isspace(*comma)) comma++;
            if (strncmp(comma, "local", 5) == 0) {
                parsed->is_local_function = true;
                // Debug: Uncomment to verify local function detection
                fprintf(stderr, "[DEBUG] Detected local function: %s\n", parsed->function_name);
            }
        }
        return true;
    }
    
    // Check for .sym (symbol/label within a function - like .sym GXPerf_80341E40, global)
    if (strncmp(p, ".sym", 4) == 0) {
        parsed->is_label = true;
        
        // Extract label name
        p += 4;
        while (*p && isspace(*p)) p++;
        sscanf(p, "%127[^, \t\n]", parsed->label_name);
        return true;
    }
    
    // Check for .endfn (function end)
    if (strncmp(p, ".endfn", 6) == 0) {
        parsed->is_directive = true;
        return true;
    }
    
    // Check for .data section
    if (strncmp(p, ".data", 5) == 0) {
        parsed->is_data = true;
        return true;
    }
    
    // Check for .text section
    if (strncmp(p, ".text", 5) == 0) {
        parsed->is_directive = true;
        return true;
    }
    
    // Check for label (.lbl_xxx, .L_xxx, or label:)
    if (*p == '.') {
        p++;
        if (strncmp(p, "lbl_", 4) == 0 || strncmp(p, "L_", 2) == 0) {
            parsed->is_label = true;
            sscanf(p, "%127[^:\n]", parsed->label_name);
            return true;
        }
    }
    
    // Parse instruction line: /* address offset */ opcode operands
    // Format: /* 80428398 00425198  7C 70 43 A6 */	mtsprg 0, r3
    if (strstr(p, "/*") != NULL) {
        const char *comment_start = strstr(p, "/*");
        const char *comment_end = strstr(comment_start, "*/");
        
        if (comment_end) {
            // Parse address and instruction from comment
            uint32_t addr, offset, b0, b1, b2, b3;
            int scanned = sscanf(comment_start, "/* %X %X %X %X %X %X */",
                                &addr, &offset, &b0, &b1, &b2, &b3);
            
            if (scanned == 6) {
                parsed->address = addr;
                parsed->instruction = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
                
                // Parse mnemonic and operands
                const char *code_start = comment_end + 2;
                while (*code_start && isspace(*code_start)) code_start++;
                
                sscanf(code_start, "%31s %127[^\n]", parsed->mnemonic, parsed->operands);
                
                return true;
            }
        }
    }
    
    return false;
}

/**
 * @brief Convert .include directive to #include
 * @param line Input line with .include
 * @param output Output buffer
 * @param output_size Size of output buffer
 * @return Number of characters written
 */
static inline int convert_include(const char *line, char *output, size_t output_size) {
    // Find the filename in quotes
    const char *start = strchr(line, '"');
    if (!start) {
        return snprintf(output, output_size, "// %s", line);
    }
    
    const char *end = strchr(start + 1, '"');
    if (!end) {
        return snprintf(output, output_size, "// %s", line);
    }
    
    // Extract filename and change extension
    char filename[256];
    size_t len = end - start - 1;
    if (len >= sizeof(filename)) len = sizeof(filename) - 1;
    
    strncpy(filename, start + 1, len);
    filename[len] = '\0';
    
    // Change .inc to .h
    char *ext = strstr(filename, ".inc");
    if (ext) {
        strcpy(ext, ".h");
    }
    
    return snprintf(output, output_size, "#include \"%s\"", filename);
}

/**
 * @brief Convert label name from assembly to C
 * @param asm_label Assembly label (e.g., "lbl_80428398" or "L_80005590")
 * @param c_label Output C label (e.g., "lbl_80428398:" or "L_80005590:")
 * @param output_size Size of output buffer
 * 
 * Note: Input should NOT have leading dot - that's already removed by parser
 */
static inline void convert_label(const char *asm_label, char *c_label, size_t output_size) {
    snprintf(c_label, output_size, "%s:", asm_label);
}

//==============================================================================
// FILE I/O
//==============================================================================

/**
 * @brief Generate output filenames from input filename
 * @param input_filename Input .s filename
 * @param output_c Output .c filename buffer
 * @param output_h Output .h filename buffer
 * @param buffer_size Size of output buffers
 */
static inline void generate_output_filenames(const char *input_filename,
                                            char *output_c,
                                            char *output_h,
                                            size_t buffer_size) {
    // Copy base filename
    strncpy(output_c, input_filename, buffer_size - 1);
    strncpy(output_h, input_filename, buffer_size - 1);
    
    // Replace .s with .c and .h
    char *ext_c = strstr(output_c, ".s");
    char *ext_h = strstr(output_h, ".s");
    
    if (ext_c && (ext_c[2] == '\0' || ext_c[2] == '\n')) {
        strcpy(ext_c, ".c");
    } else {
        strcat(output_c, ".c");
    }
    
    if (ext_h && (ext_h[2] == '\0' || ext_h[2] == '\n')) {
        strcpy(ext_h, ".h");
    } else {
        strcat(output_h, ".h");
    }
}

//==============================================================================
// HEADER GENERATION
//==============================================================================

/**
 * @brief Write header file guard and includes
 */
static inline void write_header_start(FILE *h_file, const char *guard_name) {
    fprintf(h_file, "#ifndef %s_H\n", guard_name);
    fprintf(h_file, "#define %s_H\n\n", guard_name);
    fprintf(h_file, "#include <stdint.h>\n");
    fprintf(h_file, "#include <stdbool.h>\n\n");
    fprintf(h_file, "#ifdef __cplusplus\n");
    fprintf(h_file, "extern \"C\" {\n");
    fprintf(h_file, "#endif\n\n");
}

/**
 * @brief Write header file ending
 */
static inline void write_header_end(FILE *h_file) {
    fprintf(h_file, "\n#ifdef __cplusplus\n");
    fprintf(h_file, "}\n");
    fprintf(h_file, "#endif\n\n");
    fprintf(h_file, "#endif\n");
}

/**
 * @brief Write function declaration to header
 */
static inline void write_function_declaration(FILE *h_file, const Function_Info *func) {
    // Don't declare local/static functions in the header - they're file-private
    if (func->is_local) {
        return;
    }
    
    // Handle data-only functions as extern byte arrays
    if (func->is_data_only) {
        fprintf(h_file, "extern const uint8_t %s[];  // Data at 0x%08X\n", 
               func->name, func->start_address);
        return;
    }
    
    // Skip standard library and SDK functions - these have their own declarations
    const char *intrinsics[] = {
        // Standard library
        "memset", "memcpy", "memmove", "memcmp",
        "strcmp", "strcpy", "strncpy", "strlen", "strncmp",
        "sprintf", "printf", "vprintf", "vsnprintf",
        "fwrite", "fread", "fopen", "fclose", "ftell", "fseek",
        "wcstombs", "mbstowcs",
        "strtoul", "strtol", "atoi", "atof",
        "sqrt", "round",
        "malloc", "free", "calloc", "realloc",
        "rand", "srand",
        "main",
        // SDK OS functions
        "OSInit", "OSReport", "OSPanic", "OSError",
        "OSInitThreadQueue", "OSGetCurrentThread", "OSIsThreadSuspended", "OSIsThreadTerminated",
        "OSDisableScheduler", "OSEnableScheduler", "OSYieldThread",
        "OSCreateThread", "OSExitThread", "OSCancelThread", "OSJoinThread", "OSDetachThread",
        "OSResumeThread", "OSSuspendThread", "OSSetThreadPriority", "OSGetThreadPriority",
        "OSSleepThread", "OSWakeupThread", "OSGetThreadSpecific", "OSSetThreadSpecific",
        "OSClearStack", "OSCheckActiveThreads", "OSSleepTicks",
        "OSInitMessageQueue", "OSSendMessage", "OSJamMessage", "OSReceiveMessage",
        "OSGetArenaHi", "OSGetArenaLo", "OSSetArenaHi", "OSSetArenaLo",
        "OSGetMEM1ArenaHi", "OSGetMEM1ArenaLo", "OSSetMEM1ArenaHi", "OSSetMEM1ArenaLo",
        "OSGetMEM2ArenaHi", "OSGetMEM2ArenaLo", "OSSetMEM2ArenaHi", "OSSetMEM2ArenaLo",
        "OSInitAlloc", "OSCreateHeap", "OSDestroyHeap", "OSSetCurrentHeap", "OSGetCurrentHeap",
        "OSAllocFromHeap", "OSFreeToHeap",
        // SDK DVD functions
        "DVDInit", "DVDOpen", "DVDClose", "DVDReadAsync",
        // SDK Card functions
        "CARDInit",
        // SDK VI functions
        "VIInit", "VISetPostRetraceCallback",
        // SDK PAD functions
        "PADInit", "PADRead",
        // SDK AR/ARQ functions
        "ARInit", "ARQInit",
        // SDK EXI functions
        "EXIInit",
        NULL
    };
    
    for (int i = 0; intrinsics[i] != NULL; i++) {
        if (strcmp(func->name, intrinsics[i]) == 0) {
            return;  // Skip this function
        }
    }
    
    // Get the actual function name (renamed if necessary to avoid conflicts)
    char func_name_buffer[512];
    const char *func_name = sanitize_function_name(func->name, func_name_buffer, sizeof(func_name_buffer));
    
    if (func->skip) {
        fprintf(h_file, "// Skipped: ");
    }
    
    const char *return_type = func->returns_value ? "uint32_t" : "void";
    fprintf(h_file, "%s %s(", return_type, func_name);
    
    // Generate parameter list
    if (!func->has_params) {
        fprintf(h_file, "void");
    } else {
        int param_idx = 0;
        for (int i = 0; i < func->num_int_params; i++) {
            if (param_idx > 0) fprintf(h_file, ", ");
            fprintf(h_file, "uint32_t param_r%d", 3 + i);
            param_idx++;
        }
        for (int i = 0; i < func->num_float_params; i++) {
            if (param_idx > 0) fprintf(h_file, ", ");
            fprintf(h_file, "double param_f%d", 1 + i);
            param_idx++;
        }
    }
    
    fprintf(h_file, ");  // 0x%08X (size: 0x%X)\n", func->start_address, func->size);
}

//==============================================================================
// C FILE GENERATION
//==============================================================================

/**
 * @brief Write C file preamble
 */
static inline void write_c_file_start(FILE *c_file, const char *header_filename) {
    fprintf(c_file, "/**\n");
    fprintf(c_file, " * Transpiled by Porpoise Tool\n");
    fprintf(c_file, " * PowerPC to C Transpiler for GameCube/Wii\n");
    fprintf(c_file, " */\n\n");
    fprintf(c_file, "#include \"stdlib_headers.h\"  // Standard library headers\n");
    fprintf(c_file, "#include \"%s\"\n", header_filename);
    fprintf(c_file, "#include \"gecko_memory.h\"  // For memory access\n\n");
    fprintf(c_file, "// CPU Register declarations (global for simplicity)\n");
    fprintf(c_file, "static uint32_t r[32];    // General purpose registers\n");
    fprintf(c_file, "static double f[32];      // Floating-point registers\n");
    fprintf(c_file, "static uint32_t lr, ctr, xer, msr;\n");
    fprintf(c_file, "static uint32_t cr0, cr1, cr2, cr3, cr4, cr5, cr6, cr7;\n");
    fprintf(c_file, "static uint32_t gqr[8];   // Graphics quantization registers\n");
    fprintf(c_file, "static uint32_t sprg[4];  // Special purpose register general\n");
    fprintf(c_file, "static uint32_t srr0, srr1;\n");
    fprintf(c_file, "static uint32_t fpscr;    // Floating-point status/control register\n");
    fprintf(c_file, "static uint8_t *mem;      // Memory pointer (set externally)\n\n");
}

/**
 * @brief Write function start
 */
static inline void write_function_start(FILE *c_file, const Function_Info *func) {
    // Sanitize function name to avoid conflicts with compiler intrinsics
    char sanitized_name[MAX_FUNCTION_NAME];
    const char *func_name = sanitize_function_name(func->name, sanitized_name, sizeof(sanitized_name));
    
    fprintf(c_file, "/**\n");
    fprintf(c_file, " * Function: %s", func->name);
    if (strcmp(func->name, func_name) != 0) {
        fprintf(c_file, " (renamed to %s)", func_name);
    }
    fprintf(c_file, "\n");
    fprintf(c_file, " * Address: 0x%08X\n", func->start_address);
    fprintf(c_file, " * Size: 0x%X (%u bytes)\n", func->size, func->size);
    bool is_renamed = (strcmp(func->name, func_name) != 0);
    if (func->is_local) {
        fprintf(c_file, " * Scope: static (local to this file)\n");
    } else if (is_renamed) {
        fprintf(c_file, " * Scope: global (renamed from %s to avoid conflicts)\n", func->name);
    }
    if (func->has_params) {
        fprintf(c_file, " * Parameters: %d int", func->num_int_params);
        if (func->num_float_params > 0) {
            fprintf(c_file, ", %d float", func->num_float_params);
        }
        fprintf(c_file, "\n");
    }
    fprintf(c_file, " */\n");
    
    // Add "static" keyword ONLY for truly local functions (not for renamed functions)
    const char *return_type = func->returns_value ? "uint32_t" : "void";
    fprintf(c_file, "%s%s %s(", func->is_local ? "static " : "", return_type, func_name);
    
    // Generate parameter list
    if (!func->has_params) {
        fprintf(c_file, "void");
    } else {
        int param_idx = 0;
        for (int i = 0; i < func->num_int_params; i++) {
            if (param_idx > 0) fprintf(c_file, ", ");
            fprintf(c_file, "uint32_t param_r%d", 3 + i);
            param_idx++;
        }
        for (int i = 0; i < func->num_float_params; i++) {
            if (param_idx > 0) fprintf(c_file, ", ");
            fprintf(c_file, "double param_f%d", 1 + i);
            param_idx++;
        }
    }
    
    fprintf(c_file, ") {\n");
    
    // Generate parameter marshaling code (move C params to register globals)
    if (func->has_params) {
        fprintf(c_file, "    // Parameter marshaling\n");
        // Only marshal the consecutive parameters that were actually detected
        for (int i = 0; i < func->num_int_params; i++) {
            fprintf(c_file, "    r%d = param_r%d;\n", 3 + i, 3 + i);
        }
        for (int i = 0; i < func->num_float_params; i++) {
            fprintf(c_file, "    f%d = param_f%d;\n", 1 + i, 1 + i);
        }
        fprintf(c_file, "\n");
    }
}

/**
 * @brief Write function end
 */
static inline void write_function_end(FILE *c_file) {
    fprintf(c_file, "}\n\n");
}

/**
 * @brief Write data section as byte array
 */
static inline void write_data_section(FILE *c_file, const char *name, 
                                     const uint8_t *data, size_t size) {
    fprintf(c_file, "// Data section\n");
    fprintf(c_file, "const uint8_t %s[] = {\n", name);
    
    for (size_t i = 0; i < size; i++) {
        if (i % 16 == 0) {
            fprintf(c_file, "    ");
        }
        
        fprintf(c_file, "0x%02X", data[i]);
        
        if (i < size - 1) {
            fprintf(c_file, ", ");
        }
        
        if (i % 16 == 15) {
            fprintf(c_file, "\n");
        }
    }
    
    if (size % 16 != 0) {
        fprintf(c_file, "\n");
    }
    
    fprintf(c_file, "};\n\n");
}

//==============================================================================
// FUNCTION ANALYSIS
//==============================================================================

/**
 * @brief Analyze function to detect parameters by checking which registers are read before written
 * @param input_file File handle positioned at function start
 * @param func Function info to populate with parameter information
 */
static inline void analyze_function_params(FILE *input_file, Function_Info *func) {
    long original_pos = ftell(input_file);
    char line[MAX_LINE_LENGTH];
    
    // Track which registers have been written to
    bool written_r3 = false, written_r4 = false, written_r5 = false, written_r6 = false;
    bool written_r7 = false, written_r8 = false, written_r9 = false, written_r10 = false;
    bool written_f1 = false, written_f2 = false;
    
    // Track which registers are read before written (those are parameters)
    func->param_r3 = false; func->param_r4 = false; func->param_r5 = false; func->param_r6 = false;
    func->param_r7 = false; func->param_r8 = false; func->param_r9 = false; func->param_r10 = false;
    func->param_f1 = false; func->param_f2 = false;
    
    int lines_checked = 0;
    const int MAX_LINES = 50; // Only check first 50 lines of function
    
    while (fgets(line, sizeof(line), input_file) && lines_checked++ < MAX_LINES) {
        // Stop at end of function
        if (strstr(line, ".endfn") || strstr(line, ".fn ")) break;
        
        // Check for reads of r3-r10 BEFORE they are written
        // Patterns: "mr rX, r3", "addi rX, r3", "lwz rX, offset(r3)", etc.
        if (!written_r3 && (strstr(line, ", r3") || strstr(line, "(r3)") || strstr(line, "r3,") || strstr(line, "r3;") || strstr(line, "r3 +"))) {
            // Check it's not a write like "r3 = " or "mr r3,"
            if (!strstr(line, "r3 = ") && !strstr(line, "mr r3,") && !strstr(line, "li r3,") && 
                !strstr(line, "addi r3,") && !strstr(line, "lwz r3,") && !strstr(line, "lhz r3,")) {
                func->param_r3 = true;
            }
        }
        if (!written_r4 && (strstr(line, ", r4") || strstr(line, "(r4)") || strstr(line, "r4,") || strstr(line, "r4;") || strstr(line, "r4 +"))) {
            if (!strstr(line, "r4 = ") && !strstr(line, "mr r4,") && !strstr(line, "li r4,") && 
                !strstr(line, "addi r4,") && !strstr(line, "lwz r4,")) {
                func->param_r4 = true;
            }
        }
        if (!written_r5 && (strstr(line, ", r5") || strstr(line, "(r5)") || strstr(line, "r5,") || strstr(line, "r5;") || strstr(line, "r5 +"))) {
            if (!strstr(line, "r5 = ") && !strstr(line, "mr r5,") && !strstr(line, "li r5,") && 
                !strstr(line, "addi r5,") && !strstr(line, "lwz r5,")) {
                func->param_r5 = true;
            }
        }
        if (!written_r6 && (strstr(line, ", r6") || strstr(line, "(r6)") || strstr(line, "r6,") || strstr(line, "r6;") || strstr(line, "r6 +"))) {
            if (!strstr(line, "r6 = ") && !strstr(line, "mr r6,") && !strstr(line, "li r6,") && 
                !strstr(line, "addi r6,") && !strstr(line, "lwz r6,")) {
                func->param_r6 = true;
            }
        }
        if (!written_r7 && (strstr(line, ", r7") || strstr(line, "(r7)") || strstr(line, "r7,") || strstr(line, "r7;") || strstr(line, "r7 +"))) {
            if (!strstr(line, "r7 = ") && !strstr(line, "mr r7,") && !strstr(line, "li r7,")) {
                func->param_r7 = true;
            }
        }
        if (!written_r8 && (strstr(line, ", r8") || strstr(line, "(r8)") || strstr(line, "r8,") || strstr(line, "r8;") || strstr(line, "r8 +"))) {
            if (!strstr(line, "r8 = ") && !strstr(line, "mr r8,") && !strstr(line, "li r8,")) {
                func->param_r8 = true;
            }
        }
        if (!written_r9 && (strstr(line, ", r9") || strstr(line, "(r9)") || strstr(line, "r9,") || strstr(line, "r9;") || strstr(line, "r9 +"))) {
            if (!strstr(line, "r9 = ") && !strstr(line, "mr r9,") && !strstr(line, "li r9,")) {
                func->param_r9 = true;
            }
        }
        if (!written_r10 && (strstr(line, ", r10") || strstr(line, "(r10)") || strstr(line, "r10,") || strstr(line, "r10;") || strstr(line, "r10 +"))) {
            if (!strstr(line, "r10 = ") && !strstr(line, "mr r10,") && !strstr(line, "li r10,")) {
                func->param_r10 = true;
            }
        }
        
        // Check for float parameter usage
        if (!written_f1 && (strstr(line, ", f1") || strstr(line, "f1,") || strstr(line, "f1;") || strstr(line, "f1)"))) {
            if (!strstr(line, "f1 = ") && !strstr(line, "lfs f1,") && !strstr(line, "lfd f1,")) {
                func->param_f1 = true;
            }
        }
        if (!written_f2 && (strstr(line, ", f2") || strstr(line, "f2,") || strstr(line, "f2;") || strstr(line, "f2)"))) {
            if (!strstr(line, "f2 = ") && !strstr(line, "lfs f2,") && !strstr(line, "lfd f2,")) {
                func->param_f2 = true;
            }
        }
        
        // Mark registers as written
        if (strstr(line, "r3 = ") || strstr(line, "mr r3,") || strstr(line, "li r3,") || strstr(line, "addi r3,") || strstr(line, "lwz r3,")) written_r3 = true;
        if (strstr(line, "r4 = ") || strstr(line, "mr r4,") || strstr(line, "li r4,") || strstr(line, "addi r4,") || strstr(line, "lwz r4,")) written_r4 = true;
        if (strstr(line, "r5 = ") || strstr(line, "mr r5,") || strstr(line, "li r5,") || strstr(line, "addi r5,") || strstr(line, "lwz r5,")) written_r5 = true;
        if (strstr(line, "r6 = ") || strstr(line, "mr r6,") || strstr(line, "li r6,") || strstr(line, "addi r6,") || strstr(line, "lwz r6,")) written_r6 = true;
        if (strstr(line, "r7 = ") || strstr(line, "mr r7,") || strstr(line, "li r7,")) written_r7 = true;
        if (strstr(line, "r8 = ") || strstr(line, "mr r8,") || strstr(line, "li r8,")) written_r8 = true;
        if (strstr(line, "r9 = ") || strstr(line, "mr r9,") || strstr(line, "li r9,")) written_r9 = true;
        if (strstr(line, "r10 = ") || strstr(line, "mr r10,") || strstr(line, "li r10,")) written_r10 = true;
        if (strstr(line, "f1 = ") || strstr(line, "lfs f1,") || strstr(line, "lfd f1,")) written_f1 = true;
        if (strstr(line, "f2 = ") || strstr(line, "lfs f2,") || strstr(line, "lfd f2,")) written_f2 = true;
    }
    
    // Count parameters (must be consecutive from r3)
    func->num_int_params = 0;
    if (func->param_r3) func->num_int_params = 1;
    if (func->param_r4 && func->num_int_params >= 1) func->num_int_params = 2;
    if (func->param_r5 && func->num_int_params >= 2) func->num_int_params = 3;
    if (func->param_r6 && func->num_int_params >= 3) func->num_int_params = 4;
    if (func->param_r7 && func->num_int_params >= 4) func->num_int_params = 5;
    if (func->param_r8 && func->num_int_params >= 5) func->num_int_params = 6;
    if (func->param_r9 && func->num_int_params >= 6) func->num_int_params = 7;
    if (func->param_r10 && func->num_int_params >= 7) func->num_int_params = 8;
    
    func->num_float_params = 0;
    if (func->param_f1) func->num_float_params = 1;
    if (func->param_f2 && func->num_float_params >= 1) func->num_float_params = 2;
    
    func->has_params = (func->num_int_params > 0 || func->num_float_params > 0);
    
    fseek(input_file, original_pos, SEEK_SET);
}

/**
 * @brief Detect if a "function" is actually just data (strings, tables, etc.)
 * by scanning for high percentage of .4byte directives or invalid instructions
 */
static inline bool detect_data_only_function(FILE *input_file) {
    if (!input_file) return false;
    
    long original_pos = ftell(input_file);
    rewind(input_file);
    
    char line[MAX_LINE_LENGTH];
    int total_lines = 0;
    int data_lines = 0;
    bool in_function = false;
    
    while (fgets(line, sizeof(line), input_file)) {
        // Look for function start
        if (strstr(line, ".fn ") != NULL) {
            in_function = true;
            continue;
        }
        
        // Stop at function end
        if (strstr(line, ".endfn") != NULL) {
            break;
        }
        
        if (!in_function) continue;
        
        // Count lines that indicate data rather than code
        if (strstr(line, ".4byte") != NULL ||
            strstr(line, "/* invalid */") != NULL ||
            strstr(line, "/* illegal:") != NULL) {
            data_lines++;
        }
        
        total_lines++;
    }
    
    fseek(input_file, original_pos, SEEK_SET);
    
    // If more than 80% of lines are data directives/invalid, it's a data section
    return (total_lines > 10 && data_lines > total_lines * 0.8);
}

/**
 * @brief Detect if a function returns a value by analyzing if r3 is set before blr
 * @param input_file File handle positioned at function start
 * @param func Function info
 * @return true if function appears to return a value in r3
 */
static inline bool detect_function_returns_value(FILE *input_file, const Function_Info *func) {
    long original_pos = ftell(input_file);
    char line[MAX_LINE_LENGTH];
    int lines_checked = 0;
    const int MAX_LINES_TO_CHECK = 500; // Check up to 500 lines
    
    // Track all return points
    int lines_since_r3_set = 999; // Distance from last r3 assignment
    bool found_return_with_r3 = false;
    
    // Scan through the function looking for r3 assignments close to blr
    while (fgets(line, sizeof(line), input_file) && lines_checked < MAX_LINES_TO_CHECK) {
        lines_checked++;
        
        // Check if we've hit the end of the function
        if (strstr(line, ".endfn") != NULL || strstr(line, ".fn ") != NULL) {
            break;
        }
        
        ASM_Line parsed;
        if (!parse_asm_line(line, &parsed)) continue;
        
        // Check for r3 being set (destination register)
        if (strstr(line, "r3 = ") != NULL || 
            strstr(line, "mr r3,") != NULL ||
            strstr(line, "li r3,") != NULL ||
            strstr(line, "addi r3,") != NULL ||
            strstr(line, "lwz r3,") != NULL ||
            strstr(line, "lhz r3,") != NULL ||
            strstr(line, "lbz r3,") != NULL ||
            strstr(line, "rlwinm r3,") != NULL ||
            strstr(line, "xori r3,") != NULL ||
            strstr(line, "ori r3,") != NULL ||
            strstr(line, "andi r3,") != NULL ||
            strstr(line, "add r3,") != NULL ||
            strstr(line, "sub r3,") != NULL ||
            strstr(line, "mullw r3,") != NULL ||
            strstr(line, "and r3,") != NULL ||
            strstr(line, "or r3,") != NULL ||
            strstr(line, "xor r3,") != NULL ||
            strstr(line, "slwi r3,") != NULL ||
            strstr(line, "srwi r3,") != NULL) {
            lines_since_r3_set = 0;  // Reset counter
        } else {
            lines_since_r3_set++;
        }
        
        // If we find blr/return within 3 lines of r3 being set, it's likely a return value
        if ((strstr(line, "blr") != NULL || strstr(line, "return") != NULL) && lines_since_r3_set <= 3) {
            found_return_with_r3 = true;
        }
    }
    
    fseek(input_file, original_pos, SEEK_SET);
    return found_return_with_r3;
}

//==============================================================================
// LINE PROCESSING
//==============================================================================

/**
 * @brief Check if line is inside a comment block
 */
static inline bool is_comment_line(const char *line) {
    const char *p = line;
    while (*p && isspace(*p)) p++;
    
    // Check for /* */ style comments
    if (strstr(p, "/*") != NULL && strstr(p, "*/") != NULL) {
        // Single-line comment
        const char *code_start = strstr(p, "*/") + 2;
        // Check if there's actual code after the comment
        while (*code_start && isspace(*code_start)) code_start++;
        if (*code_start == '\0') {
            return true;  // Only comment, no code
        }
    }
    
    return false;
}

/**
 * @brief Trim whitespace from string
 */
static inline char* trim_whitespace(char *str) {
    char *end;
    
    // Trim leading space
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) {
        return str;
    }
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    end[1] = '\0';
    
    return str;
}

/**
 * @brief Detect function call parameters from previous assembly lines
 * @param lines Array of previous assembly lines (in reverse order - most recent first)
 * @param num_lines Number of lines in the array
 * @param params Output buffer for detected parameters (e.g., "r3, r4, r5")
 * @param params_size Size of params buffer
 * @return Number of parameters detected
 */
static inline int detect_function_parameters(const char **lines, int num_lines, 
                                             char *params, size_t params_size) {
    bool used_regs[32] = {false};  // Track which registers are used
    bool used_fregs[32] = {false}; // Track which FP registers are used
    int param_count = 0;
    params[0] = '\0';
    
    // Look backwards through lines to find register assignments
    for (int i = 0; i < num_lines; i++) {
        const char *line = lines[i];
        if (!line) break;
        
        // Skip comment lines
        const char *p = line;
        while (*p && isspace(*p)) p++;
        if (*p == '#' || *p == '\0') continue;
        
        // Stop at labels (function boundaries)
        if (*p != '\0' && !isspace(*p) && strchr(line, ':') != NULL) {
            // It's a label, stop here
            break;
        }
        
        // Check for register assignments to r3-r10 (integer parameters)
        for (int r = 3; r <= 10 && !used_regs[r]; r++) {
            char reg_pattern[16];
            snprintf(reg_pattern, sizeof(reg_pattern), " r%d,", r);
            if (strstr(line, reg_pattern) || strstr(line, reg_pattern + 1)) {
                // Found assignment to this register
                used_regs[r] = true;
            }
        }
        
        // Check for floating-point register assignments f1-f13
        for (int f = 1; f <= 13 && !used_fregs[f]; f++) {
            char freg_pattern[16];
            snprintf(freg_pattern, sizeof(freg_pattern), " f%d,", f);
            if (strstr(line, freg_pattern) || strstr(line, freg_pattern + 1)) {
                used_fregs[f] = true;
            }
        }
    }
    
    // Build parameter list from r3, r4, r5, ... in order
    for (int r = 3; r <= 10; r++) {
        if (used_regs[r]) {
            if (param_count > 0) {
                strncat(params, ", ", params_size - strlen(params) - 1);
            }
            char reg[8];
            snprintf(reg, sizeof(reg), "r%d", r);
            strncat(params, reg, params_size - strlen(params) - 1);
            param_count++;
        } else {
            // Stop at first unused parameter register
            break;
        }
    }
    
    // Add floating-point parameters
    for (int f = 1; f <= 13; f++) {
        if (used_fregs[f]) {
            if (param_count > 0) {
                strncat(params, ", ", params_size - strlen(params) - 1);
            }
            char freg[8];
            snprintf(freg, sizeof(freg), "f%d", f);
            strncat(params, freg, params_size - strlen(params) - 1);
            param_count++;
        }
    }
    
    return param_count;
}

#ifdef __cplusplus
}
#endif

#endif // PORPOISE_TOOL_H

