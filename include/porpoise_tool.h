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
 * @brief Sanitize a function name by adding suffix if it's reserved
 * @param name Original function name
 * @param output Buffer to store sanitized name
 * @param output_size Size of output buffer
 * @return Pointer to output buffer
 */
static inline const char* sanitize_function_name(const char *name, char *output, size_t output_size) {
    if (is_reserved_name(name)) {
        // Add _impl suffix to avoid conflicts
        snprintf(output, output_size, "%s_impl", name);
        return output;
    }
    // Name is fine as-is
    return name;
}

//==============================================================================
// STRUCTURES
//==============================================================================

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
    bool skip;                  // Skip transpiling this function
    int instruction_count;      // Number of instructions in function
    bool is_trampoline;         // Is this a trampoline (single branch)?
    uint32_t trampoline_target; // Target address for trampoline
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
            // Label name has '.' already stripped by parser, so check for L_ or lbl_
            if ((label[0] == 'L' && label[1] == '_') || 
                (strncmp(label, "lbl_", 4) == 0)) {
                uint32_t addr = 0;
                // Try L_XXXXXXXX format
                if (label[0] == 'L' && label[1] == '_') {
                    sscanf(label + 2, "%x", &addr);
                }
                // Try lbl_XXXXXXXX format
                else if (strncmp(label, "lbl_", 4) == 0) {
                    sscanf(label + 4, "%x", &addr);
                }
                if (addr != 0) {
                    labelmap_add(map, addr, current_function);
                }
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
        strncmp(p, ".global", 7) == 0 || strncmp(p, ".weak", 5) == 0) {
        parsed->is_directive = true;
        return true;
    }
    
    // Check for .fn (function start)
    if (strncmp(p, ".fn", 3) == 0) {
        parsed->is_function = true;
        // Extract function name
        p += 3;
        while (*p && isspace(*p)) p++;
        sscanf(p, "%127[^, \t\n]", parsed->function_name);
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
    if (func->skip) {
        fprintf(h_file, "// Skipped: ");
    }
    
    fprintf(h_file, "void %s(void);  // 0x%08X (size: 0x%X)\n",
            func->name, func->start_address, func->size);
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
    fprintf(c_file, " */\n");
    fprintf(c_file, "void %s(void) {\n", func_name);
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

