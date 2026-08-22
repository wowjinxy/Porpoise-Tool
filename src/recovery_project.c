#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "porpoise/recovery_project.h"
#include "porpoise/recovery_title_host.h"
#include "porpoise/sha256.h"

#include "porpoise/util.h"
#include "jsmn.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <process.h>
#define RECOVERY_CLOSE _close
#define RECOVERY_COMMIT _commit
#define RECOVERY_FDOPEN _fdopen
#define RECOVERY_FILENO _fileno
#define RECOVERY_GETPID _getpid
#define RECOVERY_OPEN _open
#else
#include <unistd.h>
#define RECOVERY_CLOSE close
#define RECOVERY_COMMIT fsync
#define RECOVERY_FDOPEN fdopen
#define RECOVERY_FILENO fileno
#define RECOVERY_GETPID getpid
#define RECOVERY_OPEN open
#endif

#define RECOVERY_JSON_MAX_DEPTH 48U
#define RECOVERY_READ_CHUNK 4096U

typedef struct RecoveryParseContext {
    const char *path;
    const char *directory;
    const char *json;
    size_t json_length;
    const jsmntok_t *tokens;
    int token_count;
    PorpoiseDiagnostics *diagnostics;
    uint32_t source_schema_version;
    int result;
} RecoveryParseContext;

typedef enum RecoveryPathRootKind {
    RECOVERY_PATH_RELATIVE = 0,
    RECOVERY_PATH_POSIX,
    RECOVERY_PATH_DRIVE,
    RECOVERY_PATH_UNC
} RecoveryPathRootKind;

typedef struct RecoveryPathParts {
    RecoveryPathRootKind kind;
    char root[PORPOISE_PATH_CAPACITY];
    char storage[PORPOISE_PATH_CAPACITY];
    const char *segments[PORPOISE_PATH_CAPACITY / 2U];
    size_t segment_count;
} RecoveryPathParts;

bool porpoise_recovery_target_id_is_valid(const char *id) {
    return id != NULL && id[0] != '\0';
}

static unsigned char recovery_ascii_upper(unsigned char value) {
    return value >= 'a' && value <= 'z'
        ? (unsigned char)(value - ('a' - 'A')) : value;
}

static bool recovery_target_id_is_portable_path_component(const char *id) {
    size_t length = strlen(id);
    size_t index;
    char upper[5] = "";
    if (length == 0U || length > 64U) return false;
    for (index = 0U; index < length; index++) {
        const unsigned char value = (unsigned char)id[index];
        if (!((value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '_' ||
              value == '-')) return false;
        if (index < sizeof(upper) - 1U)
            upper[index] = (char)recovery_ascii_upper(value);
    }
    if ((length == 3U &&
         (strcmp(upper, "CON") == 0 || strcmp(upper, "PRN") == 0 ||
          strcmp(upper, "AUX") == 0 || strcmp(upper, "NUL") == 0)) ||
        (length == 4U &&
         (strncmp(upper, "COM", 3U) == 0 ||
          strncmp(upper, "LPT", 3U) == 0) &&
         upper[3] >= '1' && upper[3] <= '9')) return false;
    return true;
}

bool porpoise_recovery_target_cache_key(
    const char *id,
    char output[PORPOISE_RECOVERY_TARGET_CACHE_KEY_SIZE]) {
    PorpoiseSha256Context context;
    unsigned char digest[PORPOISE_SHA256_DIGEST_SIZE];
    char hex[PORPOISE_SHA256_HEX_SIZE];
    if (!porpoise_recovery_target_id_is_valid(id) || output == NULL)
        return false;
    if (recovery_target_id_is_portable_path_component(id)) {
        return porpoise_copy_string(
            output, PORPOISE_RECOVERY_TARGET_CACHE_KEY_SIZE, id);
    }
    porpoise_sha256_init(&context);
    porpoise_sha256_update(&context, id, strlen(id));
    porpoise_sha256_final(&context, digest);
    porpoise_sha256_hex(digest, hex);
    return snprintf(
               output, PORPOISE_RECOVERY_TARGET_CACHE_KEY_SIZE,
               "target-%s", hex) ==
           (int)(sizeof("target-") - 1U + PORPOISE_SHA256_HEX_SIZE - 1U);
}

static int recovery_add_diagnostic(
    PorpoiseDiagnostics *diagnostics,
    int result,
    const char *path,
    size_t line,
    const char *format,
    ...) {
    char message[PORPOISE_MESSAGE_CAPACITY];
    va_list arguments;
    int written;

    if (diagnostics == NULL) return result;
    va_start(arguments, format);
    written = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (written < 0 || !porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            path == NULL ? "" : path, line, 0U, "%s",
            written < 0 ? "failed to format project diagnostic" : message)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    return result;
}

static size_t recovery_line_at_offset(
    const RecoveryParseContext *context,
    size_t offset) {
    size_t index;
    size_t line = 1U;
    if (offset > context->json_length) offset = context->json_length;
    for (index = 0U; index < offset; index++) {
        if (context->json[index] == '\n') line++;
    }
    return line;
}

static size_t recovery_token_line(
    const RecoveryParseContext *context,
    int token_index) {
    if (token_index < 0 || token_index >= context->token_count ||
        context->tokens[token_index].start < 0) {
        return 1U;
    }
    return recovery_line_at_offset(
        context, (size_t)context->tokens[token_index].start);
}

static bool recovery_report(
    RecoveryParseContext *context,
    int result,
    int token_index,
    const char *format,
    ...) {
    char message[PORPOISE_MESSAGE_CAPACITY];
    va_list arguments;
    int written;

    if (context->result == PORPOISE_EXIT_INTERNAL) return false;
    va_start(arguments, format);
    written = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (written < 0) {
        context->result = PORPOISE_EXIT_INTERNAL;
        return false;
    }
    context->result = result;
    if (context->diagnostics != NULL && !porpoise_diagnostics_add(
            context->diagnostics, PORPOISE_SEVERITY_ERROR,
            context->path == NULL ? "" : context->path,
            recovery_token_line(context, token_index), 0U, "%s", message)) {
        context->result = PORPOISE_EXIT_INTERNAL;
    }
    return false;
}

static bool recovery_report_offset(
    RecoveryParseContext *context,
    size_t offset,
    const char *message) {
    context->result = PORPOISE_EXIT_USAGE;
    if (context->diagnostics != NULL && !porpoise_diagnostics_add(
            context->diagnostics, PORPOISE_SEVERITY_ERROR,
            context->path == NULL ? "" : context->path,
            recovery_line_at_offset(context, offset), 0U, "%s", message)) {
        context->result = PORPOISE_EXIT_INTERNAL;
    }
    return false;
}

static bool recovery_separator(char value) {
    return value == '/' || value == '\\';
}

static bool recovery_path_has_expansion(const char *path) {
    const char *percent;
    if (path == NULL) return false;
    if (path[0] == '~' || strchr(path, '$') != NULL) return true;
    percent = strchr(path, '%');
    return percent != NULL && strchr(percent + 1, '%') != NULL;
}

static bool recovery_path_is_drive_absolute(const char *path) {
    return path != NULL && isalpha((unsigned char)path[0]) &&
           path[1] == ':' && recovery_separator(path[2]);
}

static bool recovery_path_is_drive_relative(const char *path) {
    return path != NULL && isalpha((unsigned char)path[0]) &&
           path[1] == ':' && !recovery_separator(path[2]);
}

static bool recovery_path_is_unc(const char *path) {
    return path != NULL && recovery_separator(path[0]) &&
           recovery_separator(path[1]) && path[2] != '\0' &&
           !recovery_separator(path[2]);
}

static bool recovery_path_is_generic_absolute(const char *path) {
    return recovery_path_is_drive_absolute(path) ||
           recovery_path_is_unc(path) ||
           (path != NULL && path[0] == '/');
}

static bool recovery_path_push_segment(
    RecoveryPathParts *parts,
    char *segment) {
    if (strcmp(segment, ".") == 0 || segment[0] == '\0') return true;
    if (strcmp(segment, "..") == 0) {
        if (parts->segment_count != 0U &&
            strcmp(parts->segments[parts->segment_count - 1U], "..") != 0) {
            parts->segment_count--;
            return true;
        }
        if (parts->kind != RECOVERY_PATH_RELATIVE) return true;
    }
    if (parts->segment_count >=
        sizeof(parts->segments) / sizeof(parts->segments[0])) {
        return false;
    }
    parts->segments[parts->segment_count++] = segment;
    return true;
}

static bool recovery_path_split(
    const char *path,
    RecoveryPathParts *parts) {
    char *cursor;
    char *segment;
    size_t length;
    size_t root_end = 0U;

    if (path == NULL || path[0] == '\0' ||
        !porpoise_copy_string(parts->storage, sizeof(parts->storage), path)) {
        return false;
    }
    memset(parts->root, 0, sizeof(parts->root));
    parts->segment_count = 0U;
    length = strlen(parts->storage);
    {
        size_t index;
        for (index = 0U; index < length; index++) {
            if (parts->storage[index] == '\\') parts->storage[index] = '/';
        }
    }

    if (recovery_path_is_drive_absolute(parts->storage)) {
        parts->kind = RECOVERY_PATH_DRIVE;
        parts->root[0] = (char)toupper((unsigned char)parts->storage[0]);
        parts->root[1] = ':';
        parts->root[2] = '\0';
        root_end = 3U;
    } else if (recovery_path_is_unc(parts->storage)) {
        char *server = parts->storage + 2U;
        char *server_end = strchr(server, '/');
        char *share;
        char *share_end;
        size_t root_length;
        if (server_end == NULL || server_end == server) return false;
        share = server_end + 1U;
        if (*share == '\0') return false;
        share_end = strchr(share, '/');
        root_length = share_end == NULL ? strlen(parts->storage) :
            (size_t)(share_end - parts->storage);
        if (root_length + 1U > sizeof(parts->root)) return false;
        memcpy(parts->root, parts->storage, root_length);
        parts->root[root_length] = '\0';
        parts->kind = RECOVERY_PATH_UNC;
        root_end = share_end == NULL ? length : root_length + 1U;
    } else if (parts->storage[0] == '/') {
        parts->kind = RECOVERY_PATH_POSIX;
        porpoise_copy_string(parts->root, sizeof(parts->root), "/");
        root_end = 1U;
    } else {
        parts->kind = RECOVERY_PATH_RELATIVE;
    }

    cursor = parts->storage + root_end;
    while (*cursor == '/') cursor++;
    segment = cursor;
    for (;;) {
        if (*cursor == '/' || *cursor == '\0') {
            char saved = *cursor;
            *cursor = '\0';
            if (!recovery_path_push_segment(parts, segment)) return false;
            if (saved == '\0') break;
            cursor++;
            while (*cursor == '/') cursor++;
            segment = cursor;
        } else {
            cursor++;
        }
    }
    return true;
}

static bool recovery_path_build(
    const RecoveryPathParts *parts,
    char *output,
    size_t capacity) {
    size_t length = 0U;
    size_t index;
    const char *root = parts->root;
    if (parts->kind == RECOVERY_PATH_RELATIVE) {
        if (capacity == 0U) return false;
        output[0] = '\0';
    } else {
        size_t root_length = strlen(root);
        if (root_length + 1U > capacity) return false;
        memcpy(output, root, root_length + 1U);
        length = root_length;
        if (parts->kind == RECOVERY_PATH_DRIVE ||
            parts->kind == RECOVERY_PATH_UNC) {
            if (length + 2U > capacity) return false;
            output[length++] = '/';
            output[length] = '\0';
        }
    }
    for (index = 0U; index < parts->segment_count; index++) {
        size_t segment_length = strlen(parts->segments[index]);
        if (length != 0U && output[length - 1U] != '/') {
            if (length + 2U > capacity) return false;
            output[length++] = '/';
        }
        if (segment_length + 1U > capacity - length) return false;
        memcpy(output + length, parts->segments[index], segment_length + 1U);
        length += segment_length;
    }
    if (length == 0U) return porpoise_copy_string(output, capacity, ".");
    return true;
}

static bool recovery_path_normalize_generic(
    const char *path,
    char *output,
    size_t capacity) {
    RecoveryPathParts parts;
    memset(&parts, 0, sizeof(parts));
    return recovery_path_split(path, &parts) &&
           recovery_path_build(&parts, output, capacity);
}

static bool recovery_path_resolve(
    const char *directory,
    const char *value,
    char *output,
    size_t capacity) {
    char joined[PORPOISE_PATH_CAPACITY];
    if (recovery_path_is_drive_relative(value)) return false;
    if (recovery_path_is_generic_absolute(value)) {
        return recovery_path_normalize_generic(value, output, capacity);
    }
    if (directory == NULL || !porpoise_path_join(
            joined, sizeof(joined), directory, value)) {
        return false;
    }
    return recovery_path_normalize_generic(joined, output, capacity);
}

static bool recovery_root_equal(
    const RecoveryPathParts *left,
    const RecoveryPathParts *right) {
    size_t index;
    if (left->kind != right->kind) return false;
    if (left->kind == RECOVERY_PATH_DRIVE ||
        left->kind == RECOVERY_PATH_UNC) {
        size_t left_length = strlen(left->root);
        size_t right_length = strlen(right->root);
        if (left_length != right_length) return false;
        for (index = 0U; index < left_length; index++) {
            if (tolower((unsigned char)left->root[index]) !=
                tolower((unsigned char)right->root[index])) return false;
        }
        return true;
    }
    return strcmp(left->root, right->root) == 0;
}

static bool recovery_segment_equal(
    const RecoveryPathParts *parts,
    const char *left,
    const char *right) {
    if (parts->kind == RECOVERY_PATH_DRIVE ||
        parts->kind == RECOVERY_PATH_UNC) {
        while (*left != '\0' && *right != '\0') {
            if (tolower((unsigned char)*left) !=
                tolower((unsigned char)*right)) return false;
            left++;
            right++;
        }
        return *left == *right;
    }
    return strcmp(left, right) == 0;
}

static bool recovery_path_rebase(
    const char *resolved,
    const char *destination_directory,
    char *output,
    size_t capacity) {
    RecoveryPathParts path_parts;
    RecoveryPathParts base_parts;
    size_t common = 0U;
    size_t index;
    size_t length = 0U;

    memset(&path_parts, 0, sizeof(path_parts));
    memset(&base_parts, 0, sizeof(base_parts));
    if (!recovery_path_split(resolved, &path_parts) ||
        !recovery_path_split(destination_directory, &base_parts)) {
        return false;
    }
    if (!recovery_root_equal(&path_parts, &base_parts) ||
        path_parts.kind == RECOVERY_PATH_RELATIVE) {
        return recovery_path_build(&path_parts, output, capacity);
    }
    while (common < path_parts.segment_count &&
           common < base_parts.segment_count &&
           recovery_segment_equal(
               &path_parts, path_parts.segments[common],
               base_parts.segments[common])) {
        common++;
    }
    if (capacity == 0U) return false;
    output[0] = '\0';
    for (index = common; index < base_parts.segment_count; index++) {
        const char *piece = length == 0U ? ".." : "/..";
        size_t piece_length = strlen(piece);
        if (piece_length + 1U > capacity - length) return false;
        memcpy(output + length, piece, piece_length + 1U);
        length += piece_length;
    }
    for (index = common; index < path_parts.segment_count; index++) {
        size_t segment_length = strlen(path_parts.segments[index]);
        if (length != 0U) {
            if (length + 2U > capacity) return false;
            output[length++] = '/';
        }
        if (segment_length + 1U > capacity - length) return false;
        memcpy(output + length, path_parts.segments[index], segment_length + 1U);
        length += segment_length;
    }
    if (length == 0U) return porpoise_copy_string(output, capacity, ".");
    return true;
}

static void recovery_path_free(PorpoiseRecoveryPath *path) {
    if (path == NULL) return;
    free(path->value);
    free(path->resolved);
    memset(path, 0, sizeof(*path));
}

static void recovery_symbol_source_free(PorpoiseRecoverySymbolSource *source) {
    if (source == NULL) return;
    recovery_path_free(&source->path);
    recovery_path_free(&source->auxiliary_path);
    free(source->module);
    memset(source, 0, sizeof(*source));
}

static void recovery_override_free(PorpoiseRecoveryOverride *override) {
    if (override == NULL) return;
    free(override->target);
    free(override->module);
    free(override->normalized_fingerprint);
    free(override->contract_name);
    memset(override, 0, sizeof(*override));
}

static void recovery_annotation_free(PorpoiseRecoveryAnnotation *annotation) {
    if (annotation == NULL) return;
    free(annotation->target);
    free(annotation->module);
    free(annotation->normalized_fingerprint);
    free(annotation->exact_bytes_sha256);
    free(annotation->encoding);
    memset(annotation, 0, sizeof(*annotation));
}

static void recovery_cache_free(PorpoiseRecoveryTargetCache *cache) {
    size_t index;
    if (cache == NULL) return;
    free(cache->input_sha256);
    free(cache->settings_sha256);
    free(cache->dtk_version);
    for (index = 0U; index < cache->dependency_count; index++) {
        recovery_path_free(&cache->dependencies[index].path);
        free(cache->dependencies[index].sha256);
    }
    for (index = 0U; index < cache->match_count; index++) {
        PorpoiseRecoveryMatchCacheEntry *match = &cache->matches[index];
        free(match->module);
        free(match->normalized_fingerprint);
        free(match->canonical_identity);
        free(match->contract_name);
    }
    free(cache->dependencies);
    free(cache->matches);
    memset(cache, 0, sizeof(*cache));
}

static void recovery_target_free(PorpoiseRecoveryTarget *target) {
    size_t index;
    if (target == NULL) return;
    free(target->id);
    recovery_path_free(&target->input);
    recovery_path_free(&target->output);
    free(target->entry);
    for (index = 0U; index < target->symbol_source_count; index++)
        recovery_symbol_source_free(&target->symbol_sources[index]);
    free(target->symbol_sources);
    recovery_path_free(&target->skip_list);
    for (index = 0U; index < target->override_count; index++)
        recovery_override_free(&target->overrides[index]);
    free(target->overrides);
    for (index = 0U; index < target->annotation_count; index++)
        recovery_annotation_free(&target->annotations[index]);
    free(target->annotations);
    recovery_cache_free(&target->cache);
    porpoise_recovery_title_host_profile_free(&target->title_host);
    memset(target, 0, sizeof(*target));
}

void porpoise_recovery_project_init(PorpoiseRecoveryProject *project) {
    if (project == NULL) return;
    memset(project, 0, sizeof(*project));
    project->schema_version = PORPOISE_RECOVERY_PROJECT_SCHEMA_VERSION;
}

void porpoise_recovery_project_free(PorpoiseRecoveryProject *project) {
    size_t index;
    if (project == NULL) return;
    free(project->path);
    free(project->directory);
    for (index = 0U; index < project->sdk_catalog_count; index++)
        recovery_path_free(&project->sdk_catalogs[index]);
    free(project->sdk_catalogs);
    for (index = 0U; index < project->abi_contract_count; index++)
        recovery_path_free(&project->abi_contracts[index]);
    free(project->abi_contracts);
    for (index = 0U; index < project->target_count; index++)
        recovery_target_free(&project->targets[index]);
    free(project->targets);
    porpoise_recovery_project_init(project);
}

const char *porpoise_recovery_source_kind_name(
    PorpoiseRecoverySourceKind kind) {
    switch (kind) {
    case PORPOISE_RECOVERY_SOURCE_ASSEMBLY: return "assembly";
    case PORPOISE_RECOVERY_SOURCE_MANAGED_ELF: return "managed_elf";
    case PORPOISE_RECOVERY_SOURCE_DTK_PREPARED_ASSEMBLY:
        return "dtk_prepared_assembly";
    default: return "unknown";
    }
}

bool porpoise_recovery_source_kind_from_name(
    const char *name,
    PorpoiseRecoverySourceKind *kind_out) {
    PorpoiseRecoverySourceKind kind;
    if (name == NULL) return false;
    if (strcmp(name, "assembly") == 0)
        kind = PORPOISE_RECOVERY_SOURCE_ASSEMBLY;
    else if (strcmp(name, "managed_elf") == 0)
        kind = PORPOISE_RECOVERY_SOURCE_MANAGED_ELF;
    else if (strcmp(name, "dtk_prepared_assembly") == 0)
        kind = PORPOISE_RECOVERY_SOURCE_DTK_PREPARED_ASSEMBLY;
    else return false;
    if (kind_out != NULL) *kind_out = kind;
    return true;
}

const char *porpoise_recovery_annotation_interpretation_name(
    PorpoiseRecoveryAnnotationInterpretation interpretation) {
    static const char *const names[] = {
        "raw_bytes", "zero_fill", "ascii", "utf8", "shift_jis", "utf16",
        "s8_array", "u8_array", "s16_array", "u16_array", "s32_array",
        "u32_array", "f32_array", "f64_array", "pointer32_array"
    };
    if ((unsigned int)interpretation >=
        sizeof(names) / sizeof(names[0])) return "unknown";
    return names[(unsigned int)interpretation];
}

bool porpoise_recovery_annotation_interpretation_from_name(
    const char *name,
    PorpoiseRecoveryAnnotationInterpretation *interpretation_out) {
    PorpoiseRecoveryAnnotationInterpretation interpretation;
    if (name == NULL) return false;
    for (interpretation = PORPOISE_RECOVERY_ANNOTATION_RAW_BYTES;
         interpretation <= PORPOISE_RECOVERY_ANNOTATION_POINTER32_ARRAY;
         interpretation =
             (PorpoiseRecoveryAnnotationInterpretation)(interpretation + 1)) {
        if (strcmp(name,
                   porpoise_recovery_annotation_interpretation_name(
                       interpretation)) == 0) {
            if (interpretation_out != NULL) *interpretation_out = interpretation;
            return true;
        }
    }
    return false;
}

const PorpoiseRecoveryTarget *porpoise_recovery_project_find_target(
    const PorpoiseRecoveryProject *project,
    const char *id) {
    size_t index;
    if (project == NULL || id == NULL) return NULL;
    for (index = 0U; index < project->target_count; index++) {
        if (project->targets[index].id != NULL &&
            strcmp(project->targets[index].id, id) == 0) {
            return &project->targets[index];
        }
    }
    return NULL;
}

PorpoiseRecoveryTarget *porpoise_recovery_project_find_target_mutable(
    PorpoiseRecoveryProject *project,
    const char *id) {
    return (PorpoiseRecoveryTarget *)porpoise_recovery_project_find_target(
        project, id);
}

static void recovery_skip_whitespace(
    const char *json,
    size_t length,
    size_t *position) {
    while (*position < length) {
        char value = json[*position];
        if (value != ' ' && value != '\t' &&
            value != '\r' && value != '\n') break;
        (*position)++;
    }
}

static bool recovery_json_number_is_valid(const char *text, size_t length) {
    size_t position = 0U;
    if (length == 0U) return false;
    if (text[position] == '-') {
        position++;
        if (position == length) return false;
    }
    if (text[position] == '0') {
        position++;
        if (position < length && isdigit((unsigned char)text[position]))
            return false;
    } else {
        if (text[position] < '1' || text[position] > '9') return false;
        do {
            position++;
        } while (position < length &&
                 isdigit((unsigned char)text[position]));
    }
    if (position < length && text[position] == '.') {
        position++;
        if (position == length || !isdigit((unsigned char)text[position]))
            return false;
        do {
            position++;
        } while (position < length &&
                 isdigit((unsigned char)text[position]));
    }
    if (position < length &&
        (text[position] == 'e' || text[position] == 'E')) {
        position++;
        if (position < length &&
            (text[position] == '+' || text[position] == '-')) position++;
        if (position == length || !isdigit((unsigned char)text[position]))
            return false;
        do {
            position++;
        } while (position < length &&
                 isdigit((unsigned char)text[position]));
    }
    return position == length;
}

static bool recovery_json_primitive_is_valid(
    const char *text,
    size_t length) {
    return (length == 4U && memcmp(text, "true", 4U) == 0) ||
           (length == 5U && memcmp(text, "false", 5U) == 0) ||
           (length == 4U && memcmp(text, "null", 4U) == 0) ||
           recovery_json_number_is_valid(text, length);
}

static bool recovery_validate_json_value(
    RecoveryParseContext *context,
    int token_index,
    int expected_parent,
    unsigned int depth,
    size_t *position,
    int *next_token) {
    const jsmntok_t *token;
    int child_index;
    int child;

    if (depth > RECOVERY_JSON_MAX_DEPTH) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "project JSON is nested too deeply");
    }
    if (token_index < 0 || token_index >= context->token_count) {
        return recovery_report_offset(
            context, *position, "project JSON is missing a value");
    }
    token = &context->tokens[token_index];
    if (token->parent != expected_parent || token->start < 0 ||
        token->end < token->start) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "project has an invalid JSON token relationship");
    }

    recovery_skip_whitespace(context->json, context->json_length, position);
    if (token->type == JSMN_STRING) {
        if (token->size != 0 || *position >= context->json_length ||
            context->json[*position] != '"' ||
            token->start != (int)(*position + 1U) ||
            (size_t)token->end >= context->json_length ||
            context->json[token->end] != '"') {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "project is not strict JSON near a string");
        }
        *position = (size_t)token->end + 1U;
        *next_token = token_index + 1;
        return true;
    }
    if (token->type == JSMN_PRIMITIVE) {
        size_t primitive_length;
        if (token->size != 0 || token->start != (int)*position ||
            token->end <= token->start) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "project is not strict JSON near a primitive");
        }
        primitive_length = (size_t)(token->end - token->start);
        if (!recovery_json_primitive_is_valid(
                context->json + token->start, primitive_length)) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "project contains an invalid JSON primitive");
        }
        *position = (size_t)token->end;
        *next_token = token_index + 1;
        return true;
    }
    if (token->type == JSMN_ARRAY) {
        if (token->start != (int)*position ||
            *position >= context->json_length ||
            context->json[*position] != '[') {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "project is not strict JSON near an array");
        }
        (*position)++;
        child_index = token_index + 1;
        for (child = 0; child < token->size; child++) {
            recovery_skip_whitespace(
                context->json, context->json_length, position);
            if (!recovery_validate_json_value(
                    context, child_index, token_index, depth + 1U,
                    position, &child_index)) return false;
            recovery_skip_whitespace(
                context->json, context->json_length, position);
            if (child + 1 < token->size) {
                if (*position >= context->json_length ||
                    context->json[*position] != ',') {
                    return recovery_report_offset(
                        context, *position,
                        "project array values must be comma separated");
                }
                (*position)++;
            }
        }
        recovery_skip_whitespace(context->json, context->json_length, position);
        if (*position >= context->json_length ||
            context->json[*position] != ']') {
            return recovery_report_offset(
                context, *position, "project JSON array is not closed");
        }
        (*position)++;
        *next_token = child_index;
        return true;
    }
    if (token->type == JSMN_OBJECT) {
        if (token->start != (int)*position ||
            *position >= context->json_length ||
            context->json[*position] != '{') {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "project is not strict JSON near an object");
        }
        (*position)++;
        child_index = token_index + 1;
        for (child = 0; child < token->size; child++) {
            const jsmntok_t *key;
            recovery_skip_whitespace(
                context->json, context->json_length, position);
            if (child_index < 0 || child_index >= context->token_count) {
                return recovery_report_offset(
                    context, *position, "project JSON object is incomplete");
            }
            key = &context->tokens[child_index];
            if (key->parent != token_index || key->type != JSMN_STRING ||
                key->size != 1 || *position >= context->json_length ||
                context->json[*position] != '"' ||
                key->start != (int)(*position + 1U) ||
                (size_t)key->end >= context->json_length ||
                context->json[key->end] != '"') {
                return recovery_report(
                    context, PORPOISE_EXIT_USAGE, child_index,
                    "project object keys must be strict JSON strings");
            }
            *position = (size_t)key->end + 1U;
            recovery_skip_whitespace(
                context->json, context->json_length, position);
            if (*position >= context->json_length ||
                context->json[*position] != ':') {
                return recovery_report_offset(
                    context, *position,
                    "project object key must be followed by a colon");
            }
            (*position)++;
            recovery_skip_whitespace(
                context->json, context->json_length, position);
            if (!recovery_validate_json_value(
                    context, child_index + 1, child_index,
                    depth + 1U, position, &child_index)) return false;
            recovery_skip_whitespace(
                context->json, context->json_length, position);
            if (child + 1 < token->size) {
                if (*position >= context->json_length ||
                    context->json[*position] != ',') {
                    return recovery_report_offset(
                        context, *position,
                        "project object members must be comma separated");
                }
                (*position)++;
            }
        }
        recovery_skip_whitespace(context->json, context->json_length, position);
        if (*position >= context->json_length ||
            context->json[*position] != '}') {
            return recovery_report_offset(
                context, *position, "project JSON object is not closed");
        }
        (*position)++;
        *next_token = child_index;
        return true;
    }
    return recovery_report(
        context, PORPOISE_EXIT_USAGE, token_index,
        "project contains an invalid JSON token");
}

static bool recovery_validate_json_document(RecoveryParseContext *context) {
    size_t position = 0U;
    int next_token = 0;
    recovery_skip_whitespace(context->json, context->json_length, &position);
    if (context->token_count == 0) {
        return recovery_report_offset(
            context, position, "project JSON document is empty");
    }
    if (!recovery_validate_json_value(
            context, 0, -1, 0U, &position, &next_token)) return false;
    recovery_skip_whitespace(context->json, context->json_length, &position);
    if (position != context->json_length || next_token != context->token_count) {
        return recovery_report_offset(
            context, position,
            "project must contain exactly one strict JSON value");
    }
    return true;
}

static int recovery_hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool recovery_append_utf8(
    char *output,
    size_t capacity,
    size_t *length,
    uint32_t codepoint) {
    unsigned char encoded[4];
    size_t count;
    if (codepoint == 0U || codepoint > UINT32_C(0x10ffff) ||
        (codepoint >= UINT32_C(0xd800) &&
         codepoint <= UINT32_C(0xdfff))) return false;
    if (codepoint <= UINT32_C(0x7f)) {
        encoded[0] = (unsigned char)codepoint;
        count = 1U;
    } else if (codepoint <= UINT32_C(0x7ff)) {
        encoded[0] = (unsigned char)(0xc0U | (codepoint >> 6U));
        encoded[1] = (unsigned char)(0x80U | (codepoint & 0x3fU));
        count = 2U;
    } else if (codepoint <= UINT32_C(0xffff)) {
        encoded[0] = (unsigned char)(0xe0U | (codepoint >> 12U));
        encoded[1] = (unsigned char)(0x80U | ((codepoint >> 6U) & 0x3fU));
        encoded[2] = (unsigned char)(0x80U | (codepoint & 0x3fU));
        count = 3U;
    } else {
        encoded[0] = (unsigned char)(0xf0U | (codepoint >> 18U));
        encoded[1] = (unsigned char)(0x80U | ((codepoint >> 12U) & 0x3fU));
        encoded[2] = (unsigned char)(0x80U | ((codepoint >> 6U) & 0x3fU));
        encoded[3] = (unsigned char)(0x80U | (codepoint & 0x3fU));
        count = 4U;
    }
    if (count + 1U > capacity - *length) return false;
    memcpy(output + *length, encoded, count);
    *length += count;
    return true;
}

static bool recovery_valid_utf8(const char *text, size_t length) {
    size_t position = 0U;
    while (position < length) {
        unsigned char first = (unsigned char)text[position++];
        uint32_t codepoint;
        uint32_t minimum;
        unsigned int remaining;
        if (first < 0x80U) {
            if (first == 0U) return false;
            continue;
        }
        if ((first & 0xe0U) == 0xc0U) {
            codepoint = first & 0x1fU;
            minimum = 0x80U;
            remaining = 1U;
        } else if ((first & 0xf0U) == 0xe0U) {
            codepoint = first & 0x0fU;
            minimum = 0x800U;
            remaining = 2U;
        } else if ((first & 0xf8U) == 0xf0U) {
            codepoint = first & 0x07U;
            minimum = 0x10000U;
            remaining = 3U;
        } else return false;
        if (remaining > length - position) return false;
        while (remaining != 0U) {
            unsigned char next = (unsigned char)text[position++];
            if ((next & 0xc0U) != 0x80U) return false;
            codepoint = (codepoint << 6U) | (uint32_t)(next & 0x3fU);
            remaining--;
        }
        if (codepoint < minimum || codepoint > UINT32_C(0x10ffff) ||
            (codepoint >= UINT32_C(0xd800) &&
             codepoint <= UINT32_C(0xdfff))) return false;
    }
    return true;
}

static bool recovery_decode_json_string(
    const char *json,
    const jsmntok_t *token,
    char *output,
    size_t capacity) {
    size_t input_position;
    size_t input_end;
    size_t output_length = 0U;
    if (token->type != JSMN_STRING || token->start < 0 ||
        token->end < token->start || capacity == 0U) return false;
    input_position = (size_t)token->start;
    input_end = (size_t)token->end;
    while (input_position < input_end) {
        unsigned char value = (unsigned char)json[input_position++];
        if (value != '\\') {
            if (value == 0U || output_length + 1U >= capacity) return false;
            output[output_length++] = (char)value;
            continue;
        }
        if (input_position >= input_end) return false;
        value = (unsigned char)json[input_position++];
        if (value == '"' || value == '\\' || value == '/') {
            if (output_length + 1U >= capacity) return false;
            output[output_length++] = (char)value;
        } else if (value == 'b' || value == 'f' || value == 'n' ||
                   value == 'r' || value == 't') {
            char decoded = value == 'b' ? '\b' : value == 'f' ? '\f' :
                value == 'n' ? '\n' : value == 'r' ? '\r' : '\t';
            if (output_length + 1U >= capacity) return false;
            output[output_length++] = decoded;
        } else if (value == 'u') {
            uint32_t codepoint = 0U;
            unsigned int digit;
            if (input_position + 4U > input_end) return false;
            for (digit = 0U; digit < 4U; digit++) {
                int hex = recovery_hex_value(json[input_position++]);
                if (hex < 0) return false;
                codepoint = (codepoint << 4U) | (uint32_t)hex;
            }
            if (codepoint >= UINT32_C(0xd800) &&
                codepoint <= UINT32_C(0xdbff)) {
                uint32_t low = 0U;
                if (input_position + 6U > input_end ||
                    json[input_position] != '\\' ||
                    json[input_position + 1U] != 'u') return false;
                input_position += 2U;
                for (digit = 0U; digit < 4U; digit++) {
                    int hex = recovery_hex_value(json[input_position++]);
                    if (hex < 0) return false;
                    low = (low << 4U) | (uint32_t)hex;
                }
                if (low < UINT32_C(0xdc00) ||
                    low > UINT32_C(0xdfff)) return false;
                codepoint = UINT32_C(0x10000) +
                    ((codepoint - UINT32_C(0xd800)) << 10U) +
                    (low - UINT32_C(0xdc00));
            }
            if (!recovery_append_utf8(
                    output, capacity, &output_length, codepoint)) return false;
        } else return false;
    }
    if (!recovery_valid_utf8(output, output_length)) return false;
    output[output_length] = '\0';
    return true;
}

static bool recovery_decode_owned_string(
    RecoveryParseContext *context,
    int token_index,
    const char *description,
    bool nonempty,
    char **output) {
    const jsmntok_t *token;
    size_t length;
    char *decoded;
    if (token_index < 0 || token_index >= context->token_count) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "%s is missing", description);
    }
    token = &context->tokens[token_index];
    if (token->type != JSMN_STRING) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "%s must be a JSON string", description);
    }
    length = (size_t)(token->end - token->start);
    decoded = (char *)malloc(length + 1U);
    if (decoded == NULL) {
        return recovery_report(
            context, PORPOISE_EXIT_INTERNAL, token_index,
            "out of memory while decoding %s", description);
    }
    if (!recovery_decode_json_string(
            context->json, token, decoded, length + 1U)) {
        free(decoded);
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "%s contains invalid Unicode or an embedded null", description);
    }
    if (nonempty && decoded[0] == '\0') {
        free(decoded);
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "%s must not be empty", description);
    }
    *output = decoded;
    return true;
}

static bool recovery_token_is(
    const RecoveryParseContext *context,
    int token_index,
    const char *expected) {
    const jsmntok_t *token;
    size_t length;
    if (token_index < 0 || token_index >= context->token_count) return false;
    token = &context->tokens[token_index];
    if (token->type != JSMN_PRIMITIVE) return false;
    length = (size_t)(token->end - token->start);
    return strlen(expected) == length &&
           memcmp(context->json + token->start, expected, length) == 0;
}

static int recovery_token_after(
    const RecoveryParseContext *context,
    int token_index) {
    const jsmntok_t *token;
    int next;
    int child;
    if (token_index < 0 || token_index >= context->token_count) return -1;
    token = &context->tokens[token_index];
    next = token_index + 1;
    for (child = 0; child < token->size; child++) {
        next = recovery_token_after(context, next);
        if (next < 0) return -1;
    }
    return next;
}

static bool recovery_parse_bool(
    RecoveryParseContext *context,
    int token_index,
    const char *description,
    bool *value_out) {
    if (recovery_token_is(context, token_index, "true")) {
        *value_out = true;
        return true;
    }
    if (recovery_token_is(context, token_index, "false")) {
        *value_out = false;
        return true;
    }
    return recovery_report(
        context, PORPOISE_EXIT_USAGE, token_index,
        "%s must be a boolean", description);
}

static bool recovery_parse_uint64(
    RecoveryParseContext *context,
    int token_index,
    const char *description,
    uint64_t maximum,
    uint64_t *value_out) {
    const jsmntok_t *token;
    size_t length;
    size_t index;
    uint64_t value = 0U;
    if (token_index < 0 || token_index >= context->token_count) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "%s is missing", description);
    }
    token = &context->tokens[token_index];
    if (token->type != JSMN_PRIMITIVE || token->end <= token->start) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "%s must be an unsigned integer", description);
    }
    length = (size_t)(token->end - token->start);
    if (length > 1U && context->json[token->start] == '0') {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "%s must be an unsigned integer", description);
    }
    for (index = 0U; index < length; index++) {
        unsigned int digit;
        char character = context->json[token->start + (int)index];
        if (character < '0' || character > '9') {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "%s must be an unsigned integer", description);
        }
        digit = (unsigned int)(character - '0');
        if (value > (maximum - digit) / UINT64_C(10)) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "%s is outside its supported range", description);
        }
        value = value * UINT64_C(10) + digit;
    }
    *value_out = value;
    return true;
}

static bool recovery_parse_uint32(
    RecoveryParseContext *context,
    int token_index,
    const char *description,
    uint32_t *value_out) {
    uint64_t value;
    if (!recovery_parse_uint64(
            context, token_index, description, UINT32_MAX, &value)) return false;
    *value_out = (uint32_t)value;
    return true;
}

static bool recovery_parse_nullable_string(
    RecoveryParseContext *context,
    int token_index,
    const char *description,
    char **value_out) {
    if (recovery_token_is(context, token_index, "null")) {
        *value_out = NULL;
        return true;
    }
    return recovery_decode_owned_string(
        context, token_index, description, true, value_out);
}

static bool recovery_parse_sha256(
    RecoveryParseContext *context,
    int token_index,
    const char *description,
    char **value_out) {
    char *value = NULL;
    size_t index;
    if (!recovery_decode_owned_string(
            context, token_index, description, true, &value)) return false;
    if (strlen(value) != 64U) {
        free(value);
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "%s must contain 64 lowercase hexadecimal digits", description);
    }
    for (index = 0U; index < 64U; index++) {
        if (!((value[index] >= '0' && value[index] <= '9') ||
              (value[index] >= 'a' && value[index] <= 'f'))) {
            free(value);
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, token_index,
                "%s must contain 64 lowercase hexadecimal digits", description);
        }
    }
    *value_out = value;
    return true;
}

static bool recovery_parse_path(
    RecoveryParseContext *context,
    int token_index,
    const char *description,
    PorpoiseRecoveryPath *path_out) {
    char resolved[PORPOISE_PATH_CAPACITY];
    char *value = NULL;
    if (!recovery_decode_owned_string(
            context, token_index, description, true, &value)) return false;
    if (recovery_path_has_expansion(value)) {
        free(value);
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "%s must not contain environment-variable or tilde expansion",
            description);
    }
    if (!recovery_path_resolve(
            context->directory, value, resolved, sizeof(resolved))) {
        free(value);
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "%s is not a valid portable path", description);
    }
    path_out->value = value;
    path_out->resolved = porpoise_strdup(resolved);
    if (path_out->resolved == NULL) {
        recovery_path_free(path_out);
        return recovery_report(
            context, PORPOISE_EXIT_INTERNAL, token_index,
            "out of memory while resolving %s", description);
    }
    return true;
}

static bool recovery_key(
    RecoveryParseContext *context,
    int token_index,
    char **key_out) {
    return recovery_decode_owned_string(
        context, token_index, "project object key", false, key_out);
}

static bool recovery_append_item(
    RecoveryParseContext *context,
    void **items,
    size_t *count,
    size_t item_size,
    int token_index,
    const char *description) {
    size_t capacity = *count;
    void *grown = *items;
    if (!porpoise_grow_array(&grown, &capacity, item_size, *count + 1U)) {
        return recovery_report(
            context, PORPOISE_EXIT_INTERNAL, token_index,
            "out of memory while loading %s", description);
    }
    *items = grown;
    memset((char *)grown + (*count * item_size), 0, item_size);
    (*count)++;
    return true;
}

static bool recovery_parse_path_array(
    RecoveryParseContext *context,
    int array_index,
    const char *description,
    PorpoiseRecoveryPath **items_out,
    size_t *count_out) {
    const jsmntok_t *array;
    int item_index;
    int item;
    if (array_index < 0 || array_index >= context->token_count ||
        context->tokens[array_index].type != JSMN_ARRAY) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, array_index,
            "%s must be an array", description);
    }
    array = &context->tokens[array_index];
    item_index = array_index + 1;
    for (item = 0; item < array->size; item++) {
        size_t added_index = *count_out;
        if (!recovery_append_item(
                context, (void **)items_out, count_out,
                sizeof(**items_out), item_index, description)) return false;
        if (!recovery_parse_path(
                context, item_index, description,
                &(*items_out)[added_index])) return false;
        item_index = recovery_token_after(context, item_index);
        if (item_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, array_index,
                "%s has invalid token structure", description);
        }
    }
    return true;
}

static bool recovery_parse_symbol_kind(
    RecoveryParseContext *context,
    int token_index,
    PorpoiseSymbolSourceKind *kind_out) {
    char *name = NULL;
    PorpoiseSymbolSourceKind kind;
    if (!recovery_decode_owned_string(
            context, token_index, "symbol source kind", true, &name)) {
        return false;
    }
    if (strcmp(name, "codewarrior_map") == 0)
        kind = PORPOISE_SYMBOL_SOURCE_CODEWARRIOR_MAP;
    else if (strcmp(name, "dtk_symbols") == 0)
        kind = PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS;
    else {
        bool result = recovery_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "unknown symbol source kind '%s'", name);
        free(name);
        return result;
    }
    free(name);
    *kind_out = kind;
    return true;
}

static bool recovery_parse_symbol_source(
    RecoveryParseContext *context,
    int object_index,
    PorpoiseRecoverySymbolSource *source) {
    enum {
        FIELD_KIND = 1U << 0,
        FIELD_PATH = 1U << 1,
        FIELD_AUXILIARY_PATH = 1U << 2,
        FIELD_MODULE = 1U << 3,
        FIELD_PERMISSIVE = 1U << 4
    };
    const jsmntok_t *object;
    int member_index;
    int member;
    unsigned int seen = 0U;

    if (object_index < 0 || object_index >= context->token_count ||
        context->tokens[object_index].type != JSMN_OBJECT) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "symbol sources must be objects");
    }
    object = &context->tokens[object_index];
    source->module = porpoise_strdup("");
    if (source->module == NULL) {
        return recovery_report(
            context, PORPOISE_EXIT_INTERNAL, object_index,
            "out of memory while loading symbol source");
    }
    member_index = object_index + 1;
    for (member = 0; member < object->size; member++) {
        int key_index = member_index;
        int value_index = key_index + 1;
        char *key = NULL;
        unsigned int bit;
        if (!recovery_key(context, key_index, &key)) return false;
        if (strcmp(key, "kind") == 0) bit = FIELD_KIND;
        else if (strcmp(key, "path") == 0) bit = FIELD_PATH;
        else if (strcmp(key, "auxiliary_path") == 0)
            bit = FIELD_AUXILIARY_PATH;
        else if (strcmp(key, "module") == 0) bit = FIELD_MODULE;
        else if (strcmp(key, "permissive") == 0) bit = FIELD_PERMISSIVE;
        else {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "unknown symbol source key '%s'", key);
            free(key);
            return result;
        }
        if ((seen & bit) != 0U) {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "duplicate symbol source key '%s'", key);
            free(key);
            return result;
        }
        seen |= bit;
        free(key);

        if (bit == FIELD_KIND) {
            if (!recovery_parse_symbol_kind(
                    context, value_index, &source->kind)) return false;
        } else if (bit == FIELD_PATH) {
            if (!recovery_parse_path(
                    context, value_index, "symbol source path",
                    &source->path)) return false;
        } else if (bit == FIELD_AUXILIARY_PATH) {
            if (recovery_token_is(context, value_index, "null")) {
                source->has_auxiliary_path = false;
            } else {
                if (!recovery_parse_path(
                        context, value_index, "symbol source auxiliary_path",
                        &source->auxiliary_path)) return false;
                source->has_auxiliary_path = true;
            }
        } else if (bit == FIELD_MODULE) {
            char *module = NULL;
            if (!recovery_decode_owned_string(
                    context, value_index, "symbol source module", false,
                    &module)) return false;
            free(source->module);
            source->module = module;
        } else if (!recovery_parse_bool(
                       context, value_index, "symbol source permissive",
                       &source->permissive)) return false;
        member_index = recovery_token_after(context, value_index);
        if (member_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, value_index,
                "symbol source has invalid token structure");
        }
    }
    if ((seen & (FIELD_KIND | FIELD_PATH)) !=
        (FIELD_KIND | FIELD_PATH)) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "symbol source is missing kind or path");
    }
    if (source->kind == PORPOISE_SYMBOL_SOURCE_CODEWARRIOR_MAP &&
        source->has_auxiliary_path) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "CodeWarrior map sources cannot have auxiliary_path");
    }
    return true;
}

static bool recovery_parse_symbol_sources(
    RecoveryParseContext *context,
    int array_index,
    PorpoiseRecoveryTarget *target) {
    const jsmntok_t *array;
    int item_index;
    int item;
    if (array_index < 0 || array_index >= context->token_count ||
        context->tokens[array_index].type != JSMN_ARRAY) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, array_index,
            "target symbol_sources must be an array");
    }
    array = &context->tokens[array_index];
    item_index = array_index + 1;
    for (item = 0; item < array->size; item++) {
        size_t added_index = target->symbol_source_count;
        if (!recovery_append_item(
                context, (void **)&target->symbol_sources,
                &target->symbol_source_count,
                sizeof(*target->symbol_sources), item_index,
                "target symbol_sources")) return false;
        if (!recovery_parse_symbol_source(
                context, item_index,
                &target->symbol_sources[added_index])) return false;
        item_index = recovery_token_after(context, item_index);
        if (item_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, array_index,
                "target symbol_sources have invalid token structure");
        }
    }
    return true;
}

static bool recovery_parse_override_action(
    RecoveryParseContext *context,
    int token_index,
    PorpoiseOverrideAction *action_out) {
    char *name = NULL;
    PorpoiseOverrideAction action;
    if (!recovery_decode_owned_string(
            context, token_index, "override action", true, &name)) return false;
    if (strcmp(name, "auto") == 0) action = PORPOISE_OVERRIDE_AUTO;
    else if (strcmp(name, "lift") == 0) action = PORPOISE_OVERRIDE_LIFT;
    else if (strcmp(name, "import") == 0) action = PORPOISE_OVERRIDE_IMPORT;
    else if (strcmp(name, "omit") == 0) action = PORPOISE_OVERRIDE_OMIT;
    else if (strcmp(name, "treat_as_data") == 0)
        action = PORPOISE_OVERRIDE_TREAT_AS_DATA;
    else {
        bool result = recovery_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "unknown override action '%s'", name);
        free(name);
        return result;
    }
    free(name);
    *action_out = action;
    return true;
}

static bool recovery_parse_override(
    RecoveryParseContext *context,
    int object_index,
    const char *target_id,
    PorpoiseRecoveryOverride *override) {
    enum {
        FIELD_TARGET = 1U << 0,
        FIELD_MODULE = 1U << 1,
        FIELD_ADDRESS = 1U << 2,
        FIELD_SIZE = 1U << 3,
        FIELD_FINGERPRINT = 1U << 4,
        FIELD_ACTION = 1U << 5,
        FIELD_CONTRACT = 1U << 6,
        FIELD_ACKNOWLEDGE = 1U << 7
    };
    const unsigned int required = FIELD_TARGET | FIELD_MODULE |
        FIELD_ADDRESS | FIELD_SIZE | FIELD_FINGERPRINT | FIELD_ACTION |
        FIELD_CONTRACT | FIELD_ACKNOWLEDGE;
    const jsmntok_t *object;
    int member_index;
    int member;
    unsigned int seen = 0U;
    if (object_index < 0 || object_index >= context->token_count ||
        context->tokens[object_index].type != JSMN_OBJECT) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "target overrides must be objects");
    }
    object = &context->tokens[object_index];
    member_index = object_index + 1;
    for (member = 0; member < object->size; member++) {
        int key_index = member_index;
        int value_index = key_index + 1;
        char *key = NULL;
        unsigned int bit;
        if (!recovery_key(context, key_index, &key)) return false;
        if (strcmp(key, "target") == 0) bit = FIELD_TARGET;
        else if (strcmp(key, "module") == 0) bit = FIELD_MODULE;
        else if (strcmp(key, "address") == 0) bit = FIELD_ADDRESS;
        else if (strcmp(key, "size") == 0) bit = FIELD_SIZE;
        else if (strcmp(key, "fingerprint") == 0) bit = FIELD_FINGERPRINT;
        else if (strcmp(key, "action") == 0) bit = FIELD_ACTION;
        else if (strcmp(key, "contract") == 0) bit = FIELD_CONTRACT;
        else if (strcmp(key, "acknowledge_conflict") == 0)
            bit = FIELD_ACKNOWLEDGE;
        else {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "unknown override key '%s'", key);
            free(key);
            return result;
        }
        if ((seen & bit) != 0U) {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "duplicate override key '%s'", key);
            free(key);
            return result;
        }
        seen |= bit;
        free(key);
        if (bit == FIELD_TARGET) {
            if (!recovery_decode_owned_string(
                    context, value_index, "override target", true,
                    &override->target)) return false;
        } else if (bit == FIELD_MODULE) {
            if (!recovery_decode_owned_string(
                    context, value_index, "override module", false,
                    &override->module)) return false;
        } else if (bit == FIELD_ADDRESS) {
            if (!recovery_parse_uint32(
                    context, value_index, "override address",
                    &override->address)) return false;
        } else if (bit == FIELD_SIZE) {
            if (!recovery_parse_uint32(
                    context, value_index, "override size",
                    &override->size)) return false;
        } else if (bit == FIELD_FINGERPRINT) {
            if (!recovery_parse_sha256(
                    context, value_index, "override fingerprint",
                    &override->normalized_fingerprint)) return false;
        } else if (bit == FIELD_ACTION) {
            if (!recovery_parse_override_action(
                    context, value_index, &override->action)) return false;
        } else if (bit == FIELD_CONTRACT) {
            if (!recovery_parse_nullable_string(
                    context, value_index, "override contract",
                    &override->contract_name)) return false;
        } else if (!recovery_parse_bool(
                       context, value_index,
                       "override acknowledge_conflict",
                       &override->acknowledge_conflict)) return false;
        member_index = recovery_token_after(context, value_index);
        if (member_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, value_index,
                "override has invalid token structure");
        }
    }
    if ((seen & required) != required) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "override is missing required locator or action fields");
    }
    if (override->size == 0U) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "override size must be greater than zero");
    }
    if (strcmp(override->target, target_id) != 0) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "override target '%s' does not match owning target '%s'",
            override->target, target_id);
    }
    if (override->action == PORPOISE_OVERRIDE_IMPORT) {
        if (override->contract_name == NULL) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, object_index,
                "import override requires a contract");
        }
    } else if (override->contract_name != NULL) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "only import overrides may specify a contract");
    }
    return true;
}

static bool recovery_parse_overrides(
    RecoveryParseContext *context,
    int array_index,
    PorpoiseRecoveryTarget *target) {
    const jsmntok_t *array;
    int item_index;
    int item;
    if (array_index < 0 || array_index >= context->token_count ||
        context->tokens[array_index].type != JSMN_ARRAY) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, array_index,
            "target overrides must be an array");
    }
    array = &context->tokens[array_index];
    item_index = array_index + 1;
    for (item = 0; item < array->size; item++) {
        size_t added_index = target->override_count;
        if (!recovery_append_item(
                context, (void **)&target->overrides,
                &target->override_count, sizeof(*target->overrides),
                item_index, "target overrides")) return false;
        if (!recovery_parse_override(
                context, item_index, target->id,
                &target->overrides[added_index])) return false;
        item_index = recovery_token_after(context, item_index);
        if (item_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, array_index,
                "target overrides have invalid token structure");
        }
    }
    return true;
}

static bool recovery_parse_annotation(
    RecoveryParseContext *context,
    int object_index,
    const char *target_id,
    PorpoiseRecoveryAnnotation *annotation) {
    enum {
        FIELD_TARGET = 1U << 0,
        FIELD_MODULE = 1U << 1,
        FIELD_ADDRESS = 1U << 2,
        FIELD_SIZE = 1U << 3,
        FIELD_FINGERPRINT = 1U << 4,
        FIELD_EXACT_BYTES = 1U << 5,
        FIELD_INTERPRETATION = 1U << 6,
        FIELD_COUNT = 1U << 7,
        FIELD_ENCODING = 1U << 8
    };
    const unsigned int required = FIELD_TARGET | FIELD_MODULE |
        FIELD_ADDRESS | FIELD_SIZE | FIELD_FINGERPRINT |
        FIELD_EXACT_BYTES | FIELD_INTERPRETATION | FIELD_COUNT |
        FIELD_ENCODING;
    const jsmntok_t *object;
    int member_index;
    int member;
    unsigned int seen = 0U;
    if (object_index < 0 || object_index >= context->token_count ||
        context->tokens[object_index].type != JSMN_OBJECT) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "target annotations must be objects");
    }
    object = &context->tokens[object_index];
    member_index = object_index + 1;
    for (member = 0; member < object->size; member++) {
        int key_index = member_index;
        int value_index = key_index + 1;
        char *key = NULL;
        unsigned int bit;
        if (!recovery_key(context, key_index, &key)) return false;
        if (strcmp(key, "target") == 0) bit = FIELD_TARGET;
        else if (strcmp(key, "module") == 0) bit = FIELD_MODULE;
        else if (strcmp(key, "address") == 0) bit = FIELD_ADDRESS;
        else if (strcmp(key, "size") == 0) bit = FIELD_SIZE;
        else if (strcmp(key, "fingerprint") == 0) bit = FIELD_FINGERPRINT;
        else if (strcmp(key, "exact_bytes_sha256") == 0)
            bit = FIELD_EXACT_BYTES;
        else if (strcmp(key, "interpretation") == 0)
            bit = FIELD_INTERPRETATION;
        else if (strcmp(key, "count") == 0) bit = FIELD_COUNT;
        else if (strcmp(key, "encoding") == 0) bit = FIELD_ENCODING;
        else {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "unknown annotation key '%s'", key);
            free(key);
            return result;
        }
        if ((seen & bit) != 0U) {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "duplicate annotation key '%s'", key);
            free(key);
            return result;
        }
        seen |= bit;
        free(key);
        if (bit == FIELD_TARGET) {
            if (!recovery_decode_owned_string(
                    context, value_index, "annotation target", true,
                    &annotation->target)) return false;
        } else if (bit == FIELD_MODULE) {
            if (!recovery_decode_owned_string(
                    context, value_index, "annotation module", false,
                    &annotation->module)) return false;
        } else if (bit == FIELD_ADDRESS) {
            if (!recovery_parse_uint32(
                    context, value_index, "annotation address",
                    &annotation->address)) return false;
        } else if (bit == FIELD_SIZE) {
            if (!recovery_parse_uint32(
                    context, value_index, "annotation size",
                    &annotation->size)) return false;
        } else if (bit == FIELD_FINGERPRINT) {
            if (!recovery_parse_sha256(
                    context, value_index, "annotation fingerprint",
                    &annotation->normalized_fingerprint)) return false;
        } else if (bit == FIELD_EXACT_BYTES) {
            if (!recovery_parse_sha256(
                    context, value_index, "annotation exact_bytes_sha256",
                    &annotation->exact_bytes_sha256)) return false;
        } else if (bit == FIELD_INTERPRETATION) {
            char *name = NULL;
            bool valid;
            if (!recovery_decode_owned_string(
                    context, value_index, "annotation interpretation", true,
                    &name)) return false;
            valid = porpoise_recovery_annotation_interpretation_from_name(
                name, &annotation->interpretation);
            if (!valid) {
                bool result = recovery_report(
                    context, PORPOISE_EXIT_USAGE, value_index,
                    "unknown annotation interpretation '%s'", name);
                free(name);
                return result;
            }
            free(name);
        } else if (bit == FIELD_COUNT) {
            if (!recovery_parse_uint32(
                    context, value_index, "annotation count",
                    &annotation->element_count)) return false;
        } else if (!recovery_parse_nullable_string(
                       context, value_index, "annotation encoding",
                       &annotation->encoding)) return false;
        member_index = recovery_token_after(context, value_index);
        if (member_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, value_index,
                "annotation has invalid token structure");
        }
    }
    if ((seen & required) != required) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "annotation is missing required locator, byte hash, or interpretation fields");
    }
    if (annotation->size == 0U) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "annotation size must be greater than zero");
    }
    if (annotation->element_count == 0U) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "annotation count must be greater than zero");
    }
    if (strcmp(annotation->target, target_id) != 0) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "annotation target '%s' does not match owning target '%s'",
            annotation->target, target_id);
    }
    return true;
}

static bool recovery_parse_annotations(
    RecoveryParseContext *context,
    int array_index,
    PorpoiseRecoveryTarget *target) {
    const jsmntok_t *array;
    int item_index;
    int item;
    if (array_index < 0 || array_index >= context->token_count ||
        context->tokens[array_index].type != JSMN_ARRAY) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, array_index,
            "target annotations must be an array");
    }
    array = &context->tokens[array_index];
    item_index = array_index + 1;
    for (item = 0; item < array->size; item++) {
        size_t added_index = target->annotation_count;
        if (!recovery_append_item(
                context, (void **)&target->annotations,
                &target->annotation_count, sizeof(*target->annotations),
                item_index, "target annotations")) return false;
        if (!recovery_parse_annotation(
                context, item_index, target->id,
                &target->annotations[added_index])) return false;
        item_index = recovery_token_after(context, item_index);
        if (item_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, array_index,
                "target annotations have invalid token structure");
        }
    }
    return true;
}

static bool recovery_parse_dependency(
    RecoveryParseContext *context,
    int object_index,
    PorpoiseRecoveryDependencyCacheEntry *dependency) {
    enum {
        FIELD_PATH = 1U << 0,
        FIELD_SHA256 = 1U << 1,
        FIELD_SIZE = 1U << 2,
        FIELD_MTIME_NS = 1U << 3
    };
    const unsigned int required = FIELD_PATH | FIELD_SHA256 |
        FIELD_SIZE | FIELD_MTIME_NS;
    const jsmntok_t *object;
    int member_index;
    int member;
    unsigned int seen = 0U;
    if (object_index < 0 || object_index >= context->token_count ||
        context->tokens[object_index].type != JSMN_OBJECT) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "cache dependencies must be objects");
    }
    object = &context->tokens[object_index];
    member_index = object_index + 1;
    for (member = 0; member < object->size; member++) {
        int key_index = member_index;
        int value_index = key_index + 1;
        char *key = NULL;
        unsigned int bit;
        if (!recovery_key(context, key_index, &key)) return false;
        if (strcmp(key, "path") == 0) bit = FIELD_PATH;
        else if (strcmp(key, "sha256") == 0) bit = FIELD_SHA256;
        else if (strcmp(key, "size") == 0) bit = FIELD_SIZE;
        else if (strcmp(key, "mtime_ns") == 0) bit = FIELD_MTIME_NS;
        else {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "unknown cache dependency key '%s'", key);
            free(key);
            return result;
        }
        if ((seen & bit) != 0U) {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "duplicate cache dependency key '%s'", key);
            free(key);
            return result;
        }
        seen |= bit;
        free(key);
        if (bit == FIELD_PATH) {
            if (!recovery_parse_path(
                    context, value_index, "cache dependency path",
                    &dependency->path)) return false;
        } else if (bit == FIELD_SHA256) {
            if (!recovery_parse_sha256(
                    context, value_index, "cache dependency sha256",
                    &dependency->sha256)) return false;
        } else if (bit == FIELD_SIZE) {
            if (!recovery_parse_uint64(
                    context, value_index, "cache dependency size",
                    UINT64_MAX, &dependency->size)) return false;
        } else if (!recovery_parse_uint64(
                       context, value_index, "cache dependency mtime_ns",
                       UINT64_MAX, &dependency->mtime_ns)) return false;
        member_index = recovery_token_after(context, value_index);
        if (member_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, value_index,
                "cache dependency has invalid token structure");
        }
    }
    if ((seen & required) != required) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "cache dependency is missing required fields");
    }
    return true;
}

static bool recovery_parse_dependencies(
    RecoveryParseContext *context,
    int array_index,
    PorpoiseRecoveryTargetCache *cache) {
    const jsmntok_t *array;
    int item_index;
    int item;
    if (array_index < 0 || array_index >= context->token_count ||
        context->tokens[array_index].type != JSMN_ARRAY) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, array_index,
            "cache dependencies must be an array");
    }
    array = &context->tokens[array_index];
    item_index = array_index + 1;
    for (item = 0; item < array->size; item++) {
        size_t added_index = cache->dependency_count;
        if (!recovery_append_item(
                context, (void **)&cache->dependencies,
                &cache->dependency_count, sizeof(*cache->dependencies),
                item_index, "cache dependencies")) return false;
        if (!recovery_parse_dependency(
                context, item_index,
                &cache->dependencies[added_index])) return false;
        item_index = recovery_token_after(context, item_index);
        if (item_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, array_index,
                "cache dependencies have invalid token structure");
        }
    }
    return true;
}

static bool recovery_parse_match(
    RecoveryParseContext *context,
    int object_index,
    PorpoiseRecoveryMatchCacheEntry *match) {
    enum {
        FIELD_MODULE = 1U << 0,
        FIELD_ADDRESS = 1U << 1,
        FIELD_SIZE = 1U << 2,
        FIELD_FINGERPRINT = 1U << 3,
        FIELD_IDENTITY = 1U << 4,
        FIELD_CONTRACT = 1U << 5
    };
    const unsigned int required = FIELD_MODULE | FIELD_ADDRESS | FIELD_SIZE |
        FIELD_FINGERPRINT | FIELD_IDENTITY | FIELD_CONTRACT;
    const jsmntok_t *object;
    int member_index;
    int member;
    unsigned int seen = 0U;
    if (object_index < 0 || object_index >= context->token_count ||
        context->tokens[object_index].type != JSMN_OBJECT) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "cache matches must be objects");
    }
    object = &context->tokens[object_index];
    member_index = object_index + 1;
    for (member = 0; member < object->size; member++) {
        int key_index = member_index;
        int value_index = key_index + 1;
        char *key = NULL;
        unsigned int bit;
        if (!recovery_key(context, key_index, &key)) return false;
        if (strcmp(key, "module") == 0) bit = FIELD_MODULE;
        else if (strcmp(key, "address") == 0) bit = FIELD_ADDRESS;
        else if (strcmp(key, "size") == 0) bit = FIELD_SIZE;
        else if (strcmp(key, "fingerprint") == 0) bit = FIELD_FINGERPRINT;
        else if (strcmp(key, "canonical_identity") == 0)
            bit = FIELD_IDENTITY;
        else if (strcmp(key, "contract") == 0) bit = FIELD_CONTRACT;
        else {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "unknown cache match key '%s'", key);
            free(key);
            return result;
        }
        if ((seen & bit) != 0U) {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "duplicate cache match key '%s'", key);
            free(key);
            return result;
        }
        seen |= bit;
        free(key);
        if (bit == FIELD_MODULE) {
            if (!recovery_decode_owned_string(
                    context, value_index, "cache match module", false,
                    &match->module)) return false;
        } else if (bit == FIELD_ADDRESS) {
            if (!recovery_parse_uint32(
                    context, value_index, "cache match address",
                    &match->address)) return false;
        } else if (bit == FIELD_SIZE) {
            if (!recovery_parse_uint32(
                    context, value_index, "cache match size",
                    &match->size)) return false;
        } else if (bit == FIELD_FINGERPRINT) {
            if (!recovery_parse_sha256(
                    context, value_index, "cache match fingerprint",
                    &match->normalized_fingerprint)) return false;
        } else if (bit == FIELD_IDENTITY) {
            if (!recovery_decode_owned_string(
                    context, value_index, "cache match canonical_identity",
                    true, &match->canonical_identity)) return false;
        } else if (!recovery_parse_nullable_string(
                       context, value_index, "cache match contract",
                       &match->contract_name)) return false;
        member_index = recovery_token_after(context, value_index);
        if (member_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, value_index,
                "cache match has invalid token structure");
        }
    }
    if ((seen & required) != required) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "cache match is missing required fields");
    }
    if (match->size == 0U) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "cache match size must be greater than zero");
    }
    return true;
}

static bool recovery_parse_matches(
    RecoveryParseContext *context,
    int array_index,
    PorpoiseRecoveryTargetCache *cache) {
    const jsmntok_t *array;
    int item_index;
    int item;
    if (array_index < 0 || array_index >= context->token_count ||
        context->tokens[array_index].type != JSMN_ARRAY) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, array_index,
            "cache matches must be an array");
    }
    array = &context->tokens[array_index];
    item_index = array_index + 1;
    for (item = 0; item < array->size; item++) {
        size_t added_index = cache->match_count;
        if (!recovery_append_item(
                context, (void **)&cache->matches, &cache->match_count,
                sizeof(*cache->matches), item_index,
                "cache matches")) return false;
        if (!recovery_parse_match(
                context, item_index, &cache->matches[added_index])) return false;
        item_index = recovery_token_after(context, item_index);
        if (item_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, array_index,
                "cache matches have invalid token structure");
        }
    }
    return true;
}

static bool recovery_parse_cache(
    RecoveryParseContext *context,
    int object_index,
    PorpoiseRecoveryTargetCache *cache) {
    enum {
        FIELD_INPUT_SHA256 = 1U << 0,
        FIELD_SETTINGS_SHA256 = 1U << 1,
        FIELD_DTK_VERSION = 1U << 2,
        FIELD_DEPENDENCIES = 1U << 3,
        FIELD_MATCHES = 1U << 4
    };
    const unsigned int required = FIELD_INPUT_SHA256 | FIELD_SETTINGS_SHA256 |
        FIELD_DTK_VERSION | FIELD_DEPENDENCIES | FIELD_MATCHES;
    const jsmntok_t *object;
    int member_index;
    int member;
    unsigned int seen = 0U;
    if (recovery_token_is(context, object_index, "null")) return true;
    if (object_index < 0 || object_index >= context->token_count ||
        context->tokens[object_index].type != JSMN_OBJECT) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "target cache must be an object or null");
    }
    object = &context->tokens[object_index];
    member_index = object_index + 1;
    for (member = 0; member < object->size; member++) {
        int key_index = member_index;
        int value_index = key_index + 1;
        char *key = NULL;
        unsigned int bit;
        if (!recovery_key(context, key_index, &key)) return false;
        if (strcmp(key, "input_sha256") == 0) bit = FIELD_INPUT_SHA256;
        else if (strcmp(key, "settings_sha256") == 0)
            bit = FIELD_SETTINGS_SHA256;
        else if (strcmp(key, "dtk_version") == 0) bit = FIELD_DTK_VERSION;
        else if (strcmp(key, "dependencies") == 0) bit = FIELD_DEPENDENCIES;
        else if (strcmp(key, "matches") == 0) bit = FIELD_MATCHES;
        else {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "unknown target cache key '%s'", key);
            free(key);
            return result;
        }
        if ((seen & bit) != 0U) {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "duplicate target cache key '%s'", key);
            free(key);
            return result;
        }
        seen |= bit;
        free(key);
        if (bit == FIELD_INPUT_SHA256) {
            if (!recovery_parse_sha256(
                    context, value_index, "cache input_sha256",
                    &cache->input_sha256)) return false;
        } else if (bit == FIELD_SETTINGS_SHA256) {
            if (!recovery_parse_sha256(
                    context, value_index, "cache settings_sha256",
                    &cache->settings_sha256)) return false;
        } else if (bit == FIELD_DTK_VERSION) {
            if (!recovery_parse_nullable_string(
                    context, value_index, "cache dtk_version",
                    &cache->dtk_version)) return false;
        } else if (bit == FIELD_DEPENDENCIES) {
            if (!recovery_parse_dependencies(
                    context, value_index, cache)) return false;
        } else if (!recovery_parse_matches(
                       context, value_index, cache)) return false;
        member_index = recovery_token_after(context, value_index);
        if (member_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, value_index,
                "target cache has invalid token structure");
        }
    }
    if ((seen & required) != required) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "target cache is missing required fields");
    }
    return true;
}

static bool recovery_parse_nullable_sha256(
    RecoveryParseContext *context,
    int token_index,
    const char *description,
    char **value_out) {
    if (recovery_token_is(context, token_index, "null")) {
        *value_out = NULL;
        return true;
    }
    return recovery_parse_sha256(
        context, token_index, description, value_out);
}

static bool recovery_parse_title_gpr(
    RecoveryParseContext *context,
    int array_index,
    PorpoiseRecoveryTitleHostProfile *profile) {
    const jsmntok_t *array;
    int item_index;
    size_t index;
    if (array_index < 0 || array_index >= context->token_count ||
        context->tokens[array_index].type != JSMN_ARRAY) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, array_index,
            "title_host gpr must be an array");
    }
    array = &context->tokens[array_index];
    if ((size_t)array->size != PORPOISE_RECOVERY_TITLE_HOST_GPR_COUNT) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, array_index,
            "title_host gpr must contain exactly %u registers",
            PORPOISE_RECOVERY_TITLE_HOST_GPR_COUNT);
    }
    item_index = array_index + 1;
    for (index = 0U; index < PORPOISE_RECOVERY_TITLE_HOST_GPR_COUNT;
         index++) {
        if (!recovery_parse_uint32(
                context, item_index, "title_host gpr value",
                &profile->gpr[index])) return false;
        item_index = recovery_token_after(context, item_index);
        if (item_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, array_index,
                "title_host gpr has invalid token structure");
        }
    }
    return true;
}

static bool recovery_parse_title_startup_function(
    RecoveryParseContext *context,
    int object_index,
    PorpoiseRecoveryTitleStartupFunction *function) {
    enum {
        FIELD_MODULE = 1U << 0,
        FIELD_ADDRESS = 1U << 1,
        FIELD_SIZE = 1U << 2,
        FIELD_FINGERPRINT = 1U << 3,
        FIELD_FLAGS = 1U << 4
    };
    const unsigned int required = FIELD_MODULE | FIELD_ADDRESS | FIELD_SIZE |
        FIELD_FINGERPRINT | FIELD_FLAGS;
    const jsmntok_t *object;
    int member_index;
    int member;
    unsigned int seen = 0U;
    if (object_index < 0 || object_index >= context->token_count ||
        context->tokens[object_index].type != JSMN_OBJECT) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "title_host startup functions must be objects");
    }
    object = &context->tokens[object_index];
    member_index = object_index + 1;
    for (member = 0; member < object->size; member++) {
        int key_index = member_index;
        int value_index = key_index + 1;
        char *key = NULL;
        unsigned int bit;
        if (!recovery_key(context, key_index, &key)) return false;
        if (strcmp(key, "module") == 0) bit = FIELD_MODULE;
        else if (strcmp(key, "address") == 0) bit = FIELD_ADDRESS;
        else if (strcmp(key, "size") == 0) bit = FIELD_SIZE;
        else if (strcmp(key, "fingerprint") == 0) bit = FIELD_FINGERPRINT;
        else if (strcmp(key, "flags") == 0) bit = FIELD_FLAGS;
        else {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "unknown title_host startup function key '%s'", key);
            free(key);
            return result;
        }
        if ((seen & bit) != 0U) {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "duplicate title_host startup function key '%s'", key);
            free(key);
            return result;
        }
        seen |= bit;
        free(key);
        if (bit == FIELD_MODULE) {
            if (!recovery_decode_owned_string(
                    context, value_index,
                    "title_host startup function module", false,
                    &function->module)) return false;
        } else if (bit == FIELD_ADDRESS) {
            if (!recovery_parse_uint32(
                    context, value_index,
                    "title_host startup function address",
                    &function->address)) return false;
        } else if (bit == FIELD_SIZE) {
            if (!recovery_parse_uint32(
                    context, value_index,
                    "title_host startup function size",
                    &function->size)) return false;
        } else if (bit == FIELD_FINGERPRINT) {
            if (!recovery_parse_sha256(
                    context, value_index,
                    "title_host startup function fingerprint",
                    &function->normalized_fingerprint)) return false;
        } else if (!recovery_parse_uint32(
                       context, value_index,
                       "title_host startup function flags",
                       &function->flags)) return false;
        member_index = recovery_token_after(context, value_index);
        if (member_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, value_index,
                "title_host startup function has invalid token structure");
        }
    }
    if ((seen & required) != required) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "title_host startup function is missing required fields");
    }
    return true;
}

static bool recovery_parse_title_startup_functions(
    RecoveryParseContext *context,
    int array_index,
    PorpoiseRecoveryTitleHostProfile *profile) {
    const jsmntok_t *array;
    int item_index;
    int item;
    if (array_index < 0 || array_index >= context->token_count ||
        context->tokens[array_index].type != JSMN_ARRAY) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, array_index,
            "title_host startup_functions must be an array");
    }
    array = &context->tokens[array_index];
    if ((size_t)array->size >
        PORPOISE_RECOVERY_TITLE_HOST_STARTUP_FUNCTION_CAPACITY) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, array_index,
            "title_host has more than %u startup functions",
            PORPOISE_RECOVERY_TITLE_HOST_STARTUP_FUNCTION_CAPACITY);
    }
    item_index = array_index + 1;
    for (item = 0; item < array->size; item++) {
        size_t index = profile->startup_function_count++;
        if (!recovery_parse_title_startup_function(
                context, item_index,
                &profile->startup_functions[index])) return false;
        item_index = recovery_token_after(context, item_index);
        if (item_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, array_index,
                "title_host startup_functions have invalid token structure");
        }
    }
    return true;
}

static bool recovery_parse_title_initial_word(
    RecoveryParseContext *context,
    int object_index,
    PorpoiseRecoveryTitleInitialWord *word) {
    enum {
        FIELD_ADDRESS = 1U << 0,
        FIELD_VALUE = 1U << 1
    };
    const unsigned int required = FIELD_ADDRESS | FIELD_VALUE;
    const jsmntok_t *object;
    int member_index;
    int member;
    unsigned int seen = 0U;
    if (object_index < 0 || object_index >= context->token_count ||
        context->tokens[object_index].type != JSMN_OBJECT) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "title_host initial words must be objects");
    }
    object = &context->tokens[object_index];
    member_index = object_index + 1;
    for (member = 0; member < object->size; member++) {
        int key_index = member_index;
        int value_index = key_index + 1;
        char *key = NULL;
        unsigned int bit;
        if (!recovery_key(context, key_index, &key)) return false;
        if (strcmp(key, "address") == 0) bit = FIELD_ADDRESS;
        else if (strcmp(key, "value") == 0) bit = FIELD_VALUE;
        else {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "unknown title_host initial word key '%s'", key);
            free(key);
            return result;
        }
        if ((seen & bit) != 0U) {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "duplicate title_host initial word key '%s'", key);
            free(key);
            return result;
        }
        seen |= bit;
        free(key);
        if (bit == FIELD_ADDRESS) {
            if (!recovery_parse_uint32(
                    context, value_index,
                    "title_host initial word address",
                    &word->address)) return false;
        } else if (!recovery_parse_uint32(
                       context, value_index,
                       "title_host initial word value",
                       &word->value)) return false;
        member_index = recovery_token_after(context, value_index);
        if (member_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, value_index,
                "title_host initial word has invalid token structure");
        }
    }
    if ((seen & required) != required) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "title_host initial word is missing required fields");
    }
    return true;
}

static bool recovery_parse_title_initial_words(
    RecoveryParseContext *context,
    int array_index,
    PorpoiseRecoveryTitleHostProfile *profile) {
    const jsmntok_t *array;
    int item_index;
    int item;
    if (array_index < 0 || array_index >= context->token_count ||
        context->tokens[array_index].type != JSMN_ARRAY) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, array_index,
            "title_host initial_words must be an array");
    }
    array = &context->tokens[array_index];
    if ((size_t)array->size >
        PORPOISE_RECOVERY_TITLE_HOST_INITIAL_WORD_CAPACITY) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, array_index,
            "title_host has more than %u initial words",
            PORPOISE_RECOVERY_TITLE_HOST_INITIAL_WORD_CAPACITY);
    }
    item_index = array_index + 1;
    for (item = 0; item < array->size; item++) {
        size_t index = profile->initial_word_count++;
        if (!recovery_parse_title_initial_word(
                context, item_index,
                &profile->initial_words[index])) return false;
        item_index = recovery_token_after(context, item_index);
        if (item_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, array_index,
                "title_host initial_words have invalid token structure");
        }
    }
    return true;
}

static bool recovery_parse_title_host(
    RecoveryParseContext *context,
    int object_index,
    PorpoiseRecoveryTarget *target) {
    enum {
        FIELD_ENTRY = 1U << 0,
        FIELD_GPR = 1U << 1,
        FIELD_ARENA_LO = 1U << 2,
        FIELD_ARENA_HI = 1U << 3,
        FIELD_STARTUP = 1U << 4,
        FIELD_WORDS = 1U << 5,
        FIELD_DVD = 1U << 6,
        FIELD_INPUT = 1U << 7,
        FIELD_SYMBOLS = 1U << 8,
        FIELD_CATALOGS = 1U << 9
    };
    const unsigned int required = FIELD_ENTRY | FIELD_GPR | FIELD_ARENA_LO |
        FIELD_ARENA_HI | FIELD_STARTUP | FIELD_WORDS | FIELD_DVD |
        FIELD_INPUT | FIELD_SYMBOLS | FIELD_CATALOGS;
    PorpoiseRecoveryTitleHostProfile *profile = &target->title_host;
    const jsmntok_t *object;
    int member_index;
    int member;
    unsigned int seen = 0U;
    if (recovery_token_is(context, object_index, "null")) {
        target->has_title_host = false;
        return true;
    }
    if (object_index < 0 || object_index >= context->token_count ||
        context->tokens[object_index].type != JSMN_OBJECT) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "target title_host must be an object or null");
    }
    target->has_title_host = true;
    object = &context->tokens[object_index];
    member_index = object_index + 1;
    for (member = 0; member < object->size; member++) {
        int key_index = member_index;
        int value_index = key_index + 1;
        char *key = NULL;
        unsigned int bit;
        if (!recovery_key(context, key_index, &key)) return false;
        if (strcmp(key, "entry_address") == 0) bit = FIELD_ENTRY;
        else if (strcmp(key, "gpr") == 0) bit = FIELD_GPR;
        else if (strcmp(key, "arena_lo") == 0) bit = FIELD_ARENA_LO;
        else if (strcmp(key, "arena_hi") == 0) bit = FIELD_ARENA_HI;
        else if (strcmp(key, "startup_functions") == 0) bit = FIELD_STARTUP;
        else if (strcmp(key, "initial_words") == 0) bit = FIELD_WORDS;
        else if (strcmp(key, "initialize_dvd") == 0) bit = FIELD_DVD;
        else if (strcmp(key, "input_sha256") == 0) bit = FIELD_INPUT;
        else if (strcmp(key, "symbol_sources_sha256") == 0)
            bit = FIELD_SYMBOLS;
        else if (strcmp(key, "sdk_catalogs_sha256") == 0)
            bit = FIELD_CATALOGS;
        else {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "unknown target title_host key '%s'", key);
            free(key);
            return result;
        }
        if ((seen & bit) != 0U) {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "duplicate target title_host key '%s'", key);
            free(key);
            return result;
        }
        seen |= bit;
        free(key);
        if (bit == FIELD_ENTRY) {
            if (!recovery_parse_uint32(
                    context, value_index, "title_host entry_address",
                    &profile->entry_address)) return false;
        } else if (bit == FIELD_GPR) {
            if (!recovery_parse_title_gpr(
                    context, value_index, profile)) return false;
        } else if (bit == FIELD_ARENA_LO) {
            if (!recovery_parse_uint32(
                    context, value_index, "title_host arena_lo",
                    &profile->arena_lo)) return false;
        } else if (bit == FIELD_ARENA_HI) {
            if (!recovery_parse_uint32(
                    context, value_index, "title_host arena_hi",
                    &profile->arena_hi)) return false;
        } else if (bit == FIELD_STARTUP) {
            if (!recovery_parse_title_startup_functions(
                    context, value_index, profile)) return false;
        } else if (bit == FIELD_WORDS) {
            if (!recovery_parse_title_initial_words(
                    context, value_index, profile)) return false;
        } else if (bit == FIELD_DVD) {
            if (!recovery_parse_bool(
                    context, value_index, "title_host initialize_dvd",
                    &profile->initialize_dvd)) return false;
        } else if (bit == FIELD_INPUT) {
            if (!recovery_parse_sha256(
                    context, value_index, "title_host input_sha256",
                    &profile->input_sha256)) return false;
        } else if (bit == FIELD_SYMBOLS) {
            if (!recovery_parse_nullable_sha256(
                    context, value_index,
                    "title_host symbol_sources_sha256",
                    &profile->symbol_sources_sha256)) return false;
        } else if (!recovery_parse_nullable_sha256(
                       context, value_index,
                       "title_host sdk_catalogs_sha256",
                       &profile->sdk_catalogs_sha256)) return false;
        member_index = recovery_token_after(context, value_index);
        if (member_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, value_index,
                "target title_host has invalid token structure");
        }
    }
    if ((seen & required) != required) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "target title_host is missing required fields");
    }
    return true;
}

static bool recovery_parse_source_kind(
    RecoveryParseContext *context,
    int token_index,
    PorpoiseRecoverySourceKind *kind_out) {
    char *name = NULL;
    bool valid;
    if (!recovery_decode_owned_string(
            context, token_index, "target source_kind", true, &name)) {
        return false;
    }
    valid = porpoise_recovery_source_kind_from_name(name, kind_out);
    if (!valid) {
        bool result = recovery_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "unknown target source_kind '%s'", name);
        free(name);
        return result;
    }
    free(name);
    return true;
}

static bool recovery_parse_sdk_policy(
    RecoveryParseContext *context,
    int token_index,
    PorpoiseSdkPolicy *policy_out) {
    char *name = NULL;
    PorpoiseSdkPolicy policy;
    if (!recovery_decode_owned_string(
            context, token_index, "target sdk_policy", true, &name)) {
        return false;
    }
    if (strcmp(name, "keep") == 0) policy = PORPOISE_SDK_POLICY_KEEP;
    else if (strcmp(name, "imported") == 0)
        policy = PORPOISE_SDK_POLICY_IMPORTED;
    else if (strcmp(name, "omit") == 0) policy = PORPOISE_SDK_POLICY_OMIT;
    else {
        bool result = recovery_report(
            context, PORPOISE_EXIT_USAGE, token_index,
            "unknown target sdk_policy '%s'", name);
        free(name);
        return result;
    }
    free(name);
    *policy_out = policy;
    return true;
}

static bool recovery_locators_equal(
    const char *left_module,
    uint32_t left_address,
    uint32_t left_size,
    const char *left_fingerprint,
    const char *right_module,
    uint32_t right_address,
    uint32_t right_size,
    const char *right_fingerprint) {
    return left_address == right_address && left_size == right_size &&
           strcmp(left_module, right_module) == 0 &&
           strcmp(left_fingerprint, right_fingerprint) == 0;
}

static bool recovery_validate_target_records(
    RecoveryParseContext *context,
    int object_index,
    const PorpoiseRecoveryTarget *target) {
    size_t left;
    size_t right;
    for (left = 0U; left < target->override_count; left++) {
        const PorpoiseRecoveryOverride *first = &target->overrides[left];
        for (right = left + 1U; right < target->override_count; right++) {
            const PorpoiseRecoveryOverride *second = &target->overrides[right];
            if (recovery_locators_equal(
                    first->module, first->address, first->size,
                    first->normalized_fingerprint,
                    second->module, second->address, second->size,
                    second->normalized_fingerprint)) {
                return recovery_report(
                    context, PORPOISE_EXIT_USAGE, object_index,
                    "target '%s' contains duplicate override locators",
                    target->id);
            }
        }
    }
    for (left = 0U; left < target->annotation_count; left++) {
        const PorpoiseRecoveryAnnotation *first = &target->annotations[left];
        for (right = left + 1U; right < target->annotation_count; right++) {
            const PorpoiseRecoveryAnnotation *second =
                &target->annotations[right];
            if (recovery_locators_equal(
                    first->module, first->address, first->size,
                    first->normalized_fingerprint,
                    second->module, second->address, second->size,
                    second->normalized_fingerprint)) {
                return recovery_report(
                    context, PORPOISE_EXIT_USAGE, object_index,
                    "target '%s' contains duplicate annotation locators",
                    target->id);
            }
        }
    }
    return true;
}

static bool recovery_find_target_id(
    RecoveryParseContext *context,
    int object_index,
    PorpoiseRecoveryTarget *target) {
    const jsmntok_t *object = &context->tokens[object_index];
    int member_index = object_index + 1;
    int member;
    for (member = 0; member < object->size; member++) {
        int key_index = member_index;
        int value_index = key_index + 1;
        char *key = NULL;
        if (!recovery_key(context, key_index, &key)) return false;
        if (strcmp(key, "id") == 0 && target->id == NULL) {
            bool decoded;
            free(key);
            decoded = recovery_decode_owned_string(
                context, value_index, "target id", true, &target->id);
            if (!decoded) return false;
            if (!porpoise_recovery_target_id_is_valid(target->id)) {
                return recovery_report(
                    context, PORPOISE_EXIT_USAGE, value_index,
                    "target id '%s' must not be empty",
                    target->id);
            }
            return true;
        }
        free(key);
        member_index = recovery_token_after(context, value_index);
        if (member_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, value_index,
                "target has invalid token structure");
        }
    }
    return recovery_report(
        context, PORPOISE_EXIT_USAGE, object_index,
        "target is missing required id");
}

static bool recovery_parse_target(
    RecoveryParseContext *context,
    int object_index,
    PorpoiseRecoveryTarget *target) {
    enum {
        FIELD_ID = 1U << 0,
        FIELD_ENABLED = 1U << 1,
        FIELD_SOURCE_KIND = 1U << 2,
        FIELD_INPUT = 1U << 3,
        FIELD_OUTPUT = 1U << 4,
        FIELD_ENTRY = 1U << 5,
        FIELD_STRICT = 1U << 6,
        FIELD_SDK_POLICY = 1U << 7,
        FIELD_SYMBOL_SOURCES = 1U << 8,
        FIELD_SKIP_LIST = 1U << 9,
        FIELD_OVERRIDES = 1U << 10,
        FIELD_ANNOTATIONS = 1U << 11,
        FIELD_CACHE = 1U << 12,
        FIELD_TITLE_HOST = 1U << 13
    };
    unsigned int required = FIELD_ID | FIELD_ENABLED |
        FIELD_SOURCE_KIND | FIELD_INPUT | FIELD_OUTPUT | FIELD_ENTRY |
        FIELD_STRICT | FIELD_SDK_POLICY | FIELD_SYMBOL_SOURCES |
        FIELD_SKIP_LIST | FIELD_OVERRIDES | FIELD_ANNOTATIONS | FIELD_CACHE;
    const jsmntok_t *object;
    int member_index;
    int member;
    unsigned int seen = 0U;

    if (object_index < 0 || object_index >= context->token_count ||
        context->tokens[object_index].type != JSMN_OBJECT) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "project targets must be objects");
    }
    object = &context->tokens[object_index];
    target->enabled = true;
    target->sdk_policy = PORPOISE_SDK_POLICY_KEEP;
    if (context->source_schema_version >=
        PORPOISE_RECOVERY_PROJECT_SCHEMA_VERSION) {
        required |= FIELD_TITLE_HOST;
    }
    if (!recovery_find_target_id(context, object_index, target)) return false;
    member_index = object_index + 1;
    for (member = 0; member < object->size; member++) {
        int key_index = member_index;
        int value_index = key_index + 1;
        char *key = NULL;
        unsigned int bit;
        if (!recovery_key(context, key_index, &key)) return false;
        if (strcmp(key, "id") == 0) bit = FIELD_ID;
        else if (strcmp(key, "enabled") == 0) bit = FIELD_ENABLED;
        else if (strcmp(key, "source_kind") == 0) bit = FIELD_SOURCE_KIND;
        else if (strcmp(key, "input") == 0) bit = FIELD_INPUT;
        else if (strcmp(key, "output") == 0) bit = FIELD_OUTPUT;
        else if (strcmp(key, "entry") == 0) bit = FIELD_ENTRY;
        else if (strcmp(key, "strict") == 0) bit = FIELD_STRICT;
        else if (strcmp(key, "sdk_policy") == 0) bit = FIELD_SDK_POLICY;
        else if (strcmp(key, "symbol_sources") == 0)
            bit = FIELD_SYMBOL_SOURCES;
        else if (strcmp(key, "skip_list") == 0) bit = FIELD_SKIP_LIST;
        else if (strcmp(key, "overrides") == 0) bit = FIELD_OVERRIDES;
        else if (strcmp(key, "annotations") == 0) bit = FIELD_ANNOTATIONS;
        else if (strcmp(key, "cache") == 0) bit = FIELD_CACHE;
        else if (strcmp(key, "title_host") == 0 &&
                 context->source_schema_version >=
                     PORPOISE_RECOVERY_PROJECT_SCHEMA_VERSION)
            bit = FIELD_TITLE_HOST;
        else {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "unknown target key '%s'", key);
            free(key);
            return result;
        }
        if ((seen & bit) != 0U) {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "duplicate target key '%s'", key);
            free(key);
            return result;
        }
        seen |= bit;
        free(key);

        if (bit == FIELD_ID) {
            char *id = NULL;
            if (!recovery_decode_owned_string(
                    context, value_index, "target id", true, &id)) return false;
            if (strcmp(id, target->id) != 0) {
                free(id);
                return recovery_report(
                    context, PORPOISE_EXIT_INTERNAL, value_index,
                    "target id changed while parsing");
            }
            free(id);
        } else if (bit == FIELD_ENABLED) {
            if (!recovery_parse_bool(
                    context, value_index, "target enabled",
                    &target->enabled)) return false;
        } else if (bit == FIELD_SOURCE_KIND) {
            if (!recovery_parse_source_kind(
                    context, value_index, &target->source_kind)) return false;
        } else if (bit == FIELD_INPUT) {
            if (!recovery_parse_path(
                    context, value_index, "target input",
                    &target->input)) return false;
        } else if (bit == FIELD_OUTPUT) {
            if (!recovery_parse_path(
                    context, value_index, "target output",
                    &target->output)) return false;
        } else if (bit == FIELD_ENTRY) {
            if (!recovery_parse_nullable_string(
                    context, value_index, "target entry",
                    &target->entry)) return false;
        } else if (bit == FIELD_STRICT) {
            if (!recovery_parse_bool(
                    context, value_index, "target strict",
                    &target->strict)) return false;
        } else if (bit == FIELD_SDK_POLICY) {
            if (!recovery_parse_sdk_policy(
                    context, value_index, &target->sdk_policy)) return false;
        } else if (bit == FIELD_SYMBOL_SOURCES) {
            if (!recovery_parse_symbol_sources(
                    context, value_index, target)) return false;
        } else if (bit == FIELD_SKIP_LIST) {
            if (recovery_token_is(context, value_index, "null")) {
                target->has_skip_list = false;
            } else {
                if (!recovery_parse_path(
                        context, value_index, "target skip_list",
                        &target->skip_list)) return false;
                target->has_skip_list = true;
            }
        } else if (bit == FIELD_OVERRIDES) {
            if (!recovery_parse_overrides(
                    context, value_index, target)) return false;
        } else if (bit == FIELD_ANNOTATIONS) {
            if (!recovery_parse_annotations(
                    context, value_index, target)) return false;
        } else if (bit == FIELD_CACHE) {
            if (!recovery_parse_cache(
                    context, value_index, &target->cache)) return false;
        } else if (!recovery_parse_title_host(
                       context, value_index, target)) return false;

        member_index = recovery_token_after(context, value_index);
        if (member_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, value_index,
                "target has invalid token structure");
        }
    }
    if ((seen & required) != required) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, object_index,
            "target '%s' is missing required schema-v%u fields", target->id,
            context->source_schema_version);
    }
    return recovery_validate_target_records(context, object_index, target);
}

static bool recovery_parse_targets(
    RecoveryParseContext *context,
    int array_index,
    PorpoiseRecoveryProject *project) {
    const jsmntok_t *array;
    int item_index;
    int item;
    if (array_index < 0 || array_index >= context->token_count ||
        context->tokens[array_index].type != JSMN_ARRAY) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, array_index,
            "project targets must be an array");
    }
    array = &context->tokens[array_index];
    item_index = array_index + 1;
    for (item = 0; item < array->size; item++) {
        size_t added_index = project->target_count;
        size_t previous;
        if (!recovery_append_item(
                context, (void **)&project->targets, &project->target_count,
                sizeof(*project->targets), item_index,
                "project targets")) return false;
        if (!recovery_parse_target(
                context, item_index,
                &project->targets[added_index])) return false;
        for (previous = 0U; previous < added_index; previous++) {
            if (strcmp(project->targets[previous].id,
                       project->targets[added_index].id) == 0) {
                return recovery_report(
                    context, PORPOISE_EXIT_USAGE, item_index,
                    "duplicate project target id '%s'",
                    project->targets[added_index].id);
            }
        }
        item_index = recovery_token_after(context, item_index);
        if (item_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, array_index,
                "project targets have invalid token structure");
        }
    }
    return true;
}

static bool recovery_discover_schema_version(
    RecoveryParseContext *context) {
    const jsmntok_t *root;
    int member_index;
    int member;
    bool found = false;
    if (context->token_count == 0 ||
        context->tokens[0].type != JSMN_OBJECT) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, 0,
            "project root must be an object");
    }
    root = &context->tokens[0];
    member_index = 1;
    for (member = 0; member < root->size; member++) {
        int key_index = member_index;
        int value_index = key_index + 1;
        char *key = NULL;
        if (!recovery_key(context, key_index, &key)) return false;
        if (strcmp(key, "schema_version") == 0) {
            uint32_t version;
            free(key);
            if (found) {
                return recovery_report(
                    context, PORPOISE_EXIT_USAGE, key_index,
                    "duplicate project root key 'schema_version'");
            }
            if (!recovery_parse_uint32(
                    context, value_index, "schema_version", &version)) {
                return false;
            }
            if (version != PORPOISE_RECOVERY_PROJECT_LEGACY_SCHEMA_VERSION &&
                version != PORPOISE_RECOVERY_PROJECT_SCHEMA_VERSION) {
                return recovery_report(
                    context, PORPOISE_EXIT_USAGE, value_index,
                    "schema_version must be %u or %u",
                    PORPOISE_RECOVERY_PROJECT_LEGACY_SCHEMA_VERSION,
                    PORPOISE_RECOVERY_PROJECT_SCHEMA_VERSION);
            }
            context->source_schema_version = version;
            found = true;
        } else {
            free(key);
        }
        member_index = recovery_token_after(context, value_index);
        if (member_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, value_index,
                "project root has invalid token structure");
        }
    }
    if (!found) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, 0,
            "project root is missing required schema_version");
    }
    return true;
}

static bool recovery_parse_root(
    RecoveryParseContext *context,
    PorpoiseRecoveryProject *project) {
    enum {
        FIELD_SCHEMA_VERSION = 1U << 0,
        FIELD_SDK_CATALOGS = 1U << 1,
        FIELD_ABI_CONTRACTS = 1U << 2,
        FIELD_TARGETS = 1U << 3
    };
    const unsigned int required = FIELD_SCHEMA_VERSION | FIELD_SDK_CATALOGS |
        FIELD_ABI_CONTRACTS | FIELD_TARGETS;
    const jsmntok_t *root;
    int member_index;
    int member;
    unsigned int seen = 0U;
    if (context->token_count == 0 ||
        context->tokens[0].type != JSMN_OBJECT) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, 0,
            "project root must be an object");
    }
    root = &context->tokens[0];
    member_index = 1;
    for (member = 0; member < root->size; member++) {
        int key_index = member_index;
        int value_index = key_index + 1;
        char *key = NULL;
        unsigned int bit;
        if (!recovery_key(context, key_index, &key)) return false;
        if (strcmp(key, "schema_version") == 0)
            bit = FIELD_SCHEMA_VERSION;
        else if (strcmp(key, "sdk_catalogs") == 0)
            bit = FIELD_SDK_CATALOGS;
        else if (strcmp(key, "abi_contracts") == 0)
            bit = FIELD_ABI_CONTRACTS;
        else if (strcmp(key, "targets") == 0) bit = FIELD_TARGETS;
        else {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "unknown project root key '%s'", key);
            free(key);
            return result;
        }
        if ((seen & bit) != 0U) {
            bool result = recovery_report(
                context, PORPOISE_EXIT_USAGE, key_index,
                "duplicate project root key '%s'", key);
            free(key);
            return result;
        }
        seen |= bit;
        free(key);
        if (bit == FIELD_SCHEMA_VERSION) {
            uint32_t version;
            if (!recovery_parse_uint32(
                    context, value_index, "schema_version", &version)) {
                return false;
            }
            if (version != context->source_schema_version) {
                return recovery_report(
                    context, PORPOISE_EXIT_INTERNAL, value_index,
                    "schema_version changed while parsing");
            }
            project->schema_version =
                PORPOISE_RECOVERY_PROJECT_SCHEMA_VERSION;
        } else if (bit == FIELD_SDK_CATALOGS) {
            if (!recovery_parse_path_array(
                    context, value_index, "sdk_catalogs",
                    &project->sdk_catalogs,
                    &project->sdk_catalog_count)) return false;
        } else if (bit == FIELD_ABI_CONTRACTS) {
            if (!recovery_parse_path_array(
                    context, value_index, "abi_contracts",
                    &project->abi_contracts,
                    &project->abi_contract_count)) return false;
        } else if (!recovery_parse_targets(
                       context, value_index, project)) return false;
        member_index = recovery_token_after(context, value_index);
        if (member_index < 0) {
            return recovery_report(
                context, PORPOISE_EXIT_USAGE, value_index,
                "project root has invalid token structure");
        }
    }
    if ((seen & required) != required) {
        return recovery_report(
            context, PORPOISE_EXIT_USAGE, 0,
            "project root is missing required schema-v%u fields",
            context->source_schema_version);
    }
    return true;
}

static int recovery_read_file(
    const char *path,
    char **text_out,
    size_t *length_out,
    PorpoiseDiagnostics *diagnostics) {
    FILE *file;
    char *text = NULL;
    size_t capacity = 0U;
    size_t length = 0U;
    file = fopen(path, "rb");
    if (file == NULL) {
        return recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_IO, path, 0U,
            "failed to open recovery project: %s", strerror(errno));
    }
    for (;;) {
        size_t available;
        size_t count;
        if (capacity - length < RECOVERY_READ_CHUNK) {
            size_t next = capacity == 0U ? RECOVERY_READ_CHUNK : capacity * 2U;
            char *replacement;
            if (next < capacity || next == SIZE_MAX) {
                fclose(file);
                free(text);
                return recovery_add_diagnostic(
                    diagnostics, PORPOISE_EXIT_INTERNAL, path, 0U,
                    "recovery project is too large");
            }
            replacement = (char *)realloc(text, next + 1U);
            if (replacement == NULL) {
                fclose(file);
                free(text);
                return recovery_add_diagnostic(
                    diagnostics, PORPOISE_EXIT_INTERNAL, path, 0U,
                    "out of memory while reading recovery project");
            }
            text = replacement;
            capacity = next;
        }
        available = capacity - length;
        count = fread(text + length, 1U, available, file);
        length += count;
        if (count < available) {
            if (ferror(file)) {
                int saved_errno = errno;
                fclose(file);
                free(text);
                return recovery_add_diagnostic(
                    diagnostics, PORPOISE_EXIT_IO, path, 0U,
                    "failed to read recovery project: %s",
                    strerror(saved_errno));
            }
            break;
        }
    }
    if (fclose(file) != 0) {
        free(text);
        return recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_IO, path, 0U,
            "failed to close recovery project after reading");
    }
    if (text == NULL) {
        text = (char *)malloc(1U);
        if (text == NULL) {
            return recovery_add_diagnostic(
                diagnostics, PORPOISE_EXIT_INTERNAL, path, 0U,
                "out of memory while reading recovery project");
        }
    }
    text[length] = '\0';
    *text_out = text;
    *length_out = length;
    return PORPOISE_EXIT_OK;
}

int porpoise_recovery_project_load(
    PorpoiseRecoveryProject *project,
    const char *path,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseRecoveryProject parsed;
    char normalized_host[PORPOISE_PATH_CAPACITY];
    char normalized[PORPOISE_PATH_CAPACITY];
    char directory[PORPOISE_PATH_CAPACITY];
    char *json = NULL;
    size_t json_length = 0U;
    jsmn_parser parser;
    jsmntok_t *tokens = NULL;
    int token_count;
    int parsed_count;
    int result;
    RecoveryParseContext context;

    if (project == NULL || path == NULL || path[0] == '\0') {
        return recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_USAGE, path, 0U,
            "recovery project and path are required");
    }
    if (recovery_path_has_expansion(path)) {
        return recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_USAGE, path, 0U,
            "project path must not contain environment-variable or tilde expansion");
    }
    if (!porpoise_path_normalize_lexical(
            normalized_host, sizeof(normalized_host), path) ||
        !recovery_path_normalize_generic(
            normalized_host, normalized, sizeof(normalized)) ||
        !porpoise_path_parent(directory, sizeof(directory), normalized)) {
        return recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_USAGE, path, 0U,
            "project path is invalid or too long");
    }

    porpoise_recovery_project_init(&parsed);
    parsed.path = porpoise_strdup(normalized);
    parsed.directory = porpoise_strdup(directory);
    if (parsed.path == NULL || parsed.directory == NULL) {
        porpoise_recovery_project_free(&parsed);
        return recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_INTERNAL, path, 0U,
            "out of memory while opening recovery project");
    }
    result = recovery_read_file(path, &json, &json_length, diagnostics);
    if (result != PORPOISE_EXIT_OK) {
        porpoise_recovery_project_free(&parsed);
        return result;
    }
    jsmn_init(&parser);
    token_count = jsmn_parse(&parser, json, json_length, NULL, 0U);
    if (token_count <= 0) {
        free(json);
        porpoise_recovery_project_free(&parsed);
        return recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_USAGE, path, 1U,
            token_count == 0 ? "recovery project is empty" :
            "recovery project contains malformed JSON");
    }
    tokens = (jsmntok_t *)calloc((size_t)token_count, sizeof(*tokens));
    if (tokens == NULL) {
        free(json);
        porpoise_recovery_project_free(&parsed);
        return recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_INTERNAL, path, 0U,
            "out of memory while parsing recovery project");
    }
    jsmn_init(&parser);
    parsed_count = jsmn_parse(
        &parser, json, json_length, tokens, (unsigned int)token_count);
    if (parsed_count != token_count) {
        free(tokens);
        free(json);
        porpoise_recovery_project_free(&parsed);
        return recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_USAGE, path, 1U,
            "recovery project contains malformed JSON");
    }
    memset(&context, 0, sizeof(context));
    context.path = path;
    context.directory = parsed.directory;
    context.json = json;
    context.json_length = json_length;
    context.tokens = tokens;
    context.token_count = token_count;
    context.diagnostics = diagnostics;
    context.result = PORPOISE_EXIT_USAGE;
    if (!recovery_validate_json_document(&context) ||
        !recovery_discover_schema_version(&context) ||
        !recovery_parse_root(&context, &parsed)) {
        result = context.result;
        free(tokens);
        free(json);
        porpoise_recovery_project_free(&parsed);
        return result;
    }
    free(tokens);
    free(json);
    porpoise_recovery_project_free(project);
    *project = parsed;
    return PORPOISE_EXIT_OK;
}

static const char *recovery_symbol_source_kind_json_name(
    PorpoiseSymbolSourceKind kind) {
    switch (kind) {
    case PORPOISE_SYMBOL_SOURCE_CODEWARRIOR_MAP: return "codewarrior_map";
    case PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS: return "dtk_symbols";
    default: return "unknown";
    }
}

static const char *recovery_override_action_json_name(
    PorpoiseOverrideAction action) {
    switch (action) {
    case PORPOISE_OVERRIDE_AUTO: return "auto";
    case PORPOISE_OVERRIDE_LIFT: return "lift";
    case PORPOISE_OVERRIDE_IMPORT: return "import";
    case PORPOISE_OVERRIDE_OMIT: return "omit";
    case PORPOISE_OVERRIDE_TREAT_AS_DATA: return "treat_as_data";
    default: return "unknown";
    }
}

static const char *recovery_sdk_policy_json_name(PorpoiseSdkPolicy policy) {
    switch (policy) {
    case PORPOISE_SDK_POLICY_KEEP: return "keep";
    case PORPOISE_SDK_POLICY_IMPORTED: return "imported";
    case PORPOISE_SDK_POLICY_OMIT: return "omit";
    default: return "unknown";
    }
}

static bool recovery_cache_is_empty(
    const PorpoiseRecoveryTargetCache *cache) {
    return cache->input_sha256 == NULL && cache->settings_sha256 == NULL &&
           cache->dtk_version == NULL && cache->dependency_count == 0U &&
           cache->match_count == 0U;
}

static bool recovery_sha256_string_valid(const char *value) {
    size_t index;
    if (value == NULL || strlen(value) != 64U) return false;
    for (index = 0U; index < 64U; index++) {
        if (!((value[index] >= '0' && value[index] <= '9') ||
              (value[index] >= 'a' && value[index] <= 'f'))) return false;
    }
    return true;
}

static int recovery_validate_for_save(
    const PorpoiseRecoveryProject *project,
    const char *path,
    PorpoiseDiagnostics *diagnostics) {
    size_t target_index;
    if (project == NULL ||
        project->schema_version != PORPOISE_RECOVERY_PROJECT_SCHEMA_VERSION) {
        return recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_USAGE, path, 0U,
            "cannot save a null or non-v2 recovery project");
    }
    if ((project->sdk_catalog_count != 0U && project->sdk_catalogs == NULL) ||
        (project->abi_contract_count != 0U && project->abi_contracts == NULL) ||
        (project->target_count != 0U && project->targets == NULL)) {
        return recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_USAGE, path, 0U,
            "recovery project contains an invalid array");
    }
    for (target_index = 0U; target_index < project->target_count;
         target_index++) {
        const PorpoiseRecoveryTarget *target =
            &project->targets[target_index];
        size_t other;
        if (!porpoise_recovery_target_id_is_valid(target->id) ||
            target->input.resolved == NULL || target->output.resolved == NULL ||
            strcmp(porpoise_recovery_source_kind_name(target->source_kind),
                   "unknown") == 0 ||
            strcmp(recovery_sdk_policy_json_name(target->sdk_policy),
                   "unknown") == 0) {
            return recovery_add_diagnostic(
                diagnostics, PORPOISE_EXIT_USAGE, path, 0U,
                "target %zu is incomplete or invalid", target_index);
        }
        for (other = 0U; other < target_index; other++) {
            if (strcmp(project->targets[other].id, target->id) == 0) {
                return recovery_add_diagnostic(
                    diagnostics, PORPOISE_EXIT_USAGE, path, 0U,
                    "duplicate project target id '%s'", target->id);
            }
        }
        if (!recovery_cache_is_empty(&target->cache) &&
            (target->cache.input_sha256 == NULL ||
             target->cache.settings_sha256 == NULL)) {
            return recovery_add_diagnostic(
                diagnostics, PORPOISE_EXIT_USAGE, path, 0U,
                "target '%s' has an incomplete cache", target->id);
        }
        if (target->has_title_host) {
            const PorpoiseRecoveryTitleHostProfile *profile =
                &target->title_host;
            size_t index;
            if (!recovery_sha256_string_valid(profile->input_sha256) ||
                (profile->symbol_sources_sha256 != NULL &&
                 !recovery_sha256_string_valid(
                     profile->symbol_sources_sha256)) ||
                (profile->sdk_catalogs_sha256 != NULL &&
                 !recovery_sha256_string_valid(
                     profile->sdk_catalogs_sha256)) ||
                profile->startup_function_count >
                    PORPOISE_RECOVERY_TITLE_HOST_STARTUP_FUNCTION_CAPACITY ||
                profile->initial_word_count >
                    PORPOISE_RECOVERY_TITLE_HOST_INITIAL_WORD_CAPACITY) {
                return recovery_add_diagnostic(
                    diagnostics, PORPOISE_EXIT_USAGE, path, 0U,
                    "target '%s' has an incomplete title_host profile",
                    target->id);
            }
            for (index = 0U; index < profile->startup_function_count;
                 index++) {
                const PorpoiseRecoveryTitleStartupFunction *function =
                    &profile->startup_functions[index];
                if (function->module == NULL ||
                    !recovery_sha256_string_valid(
                        function->normalized_fingerprint)) {
                    return recovery_add_diagnostic(
                        diagnostics, PORPOISE_EXIT_USAGE, path, 0U,
                        "target '%s' has an incomplete title_host startup function",
                        target->id);
                }
            }
        }
    }
    return PORPOISE_EXIT_OK;
}

static bool recovery_write_nullable_string(FILE *file, const char *value) {
    if (value == NULL) return fputs("null", file) >= 0;
    porpoise_json_write_string(file, value);
    return !ferror(file);
}

static bool recovery_write_path(
    FILE *file,
    const PorpoiseRecoveryPath *path,
    const char *destination_directory) {
    char rebased[PORPOISE_PATH_CAPACITY];
    const char *value;
    if (path == NULL) return false;
    value = path->resolved;
    if (value != NULL && recovery_path_rebase(
            value, destination_directory, rebased, sizeof(rebased))) {
        value = rebased;
    } else if (value == NULL) {
        value = path->value;
    }
    if (value == NULL || value[0] == '\0') return false;
    porpoise_json_write_string(file, value);
    return !ferror(file);
}

static bool recovery_write_path_array(
    FILE *file,
    const PorpoiseRecoveryPath *paths,
    size_t count,
    const char *destination_directory,
    unsigned int indent) {
    size_t index;
    if (count == 0U) return fputs("[]", file) >= 0;
    if (fputs("[\n", file) < 0) return false;
    for (index = 0U; index < count; index++) {
        unsigned int space;
        for (space = 0U; space < indent; space++) fputc(' ', file);
        if (!recovery_write_path(
                file, &paths[index], destination_directory)) return false;
        if (index + 1U < count) fputc(',', file);
        fputc('\n', file);
    }
    {
        unsigned int space;
        for (space = 0U; space + 2U < indent; space++) fputc(' ', file);
    }
    fputc(']', file);
    return !ferror(file);
}

static bool recovery_write_symbol_sources(
    FILE *file,
    const PorpoiseRecoveryTarget *target,
    const char *destination_directory) {
    size_t index;
    if (target->symbol_source_count == 0U) return fputs("[]", file) >= 0;
    if (fputs("[\n", file) < 0) return false;
    for (index = 0U; index < target->symbol_source_count; index++) {
        const PorpoiseRecoverySymbolSource *source =
            &target->symbol_sources[index];
        fputs("        {\"kind\": ", file);
        porpoise_json_write_string(
            file, recovery_symbol_source_kind_json_name(source->kind));
        fputs(", \"path\": ", file);
        if (!recovery_write_path(
                file, &source->path, destination_directory)) return false;
        fputs(", \"auxiliary_path\": ", file);
        if (source->has_auxiliary_path) {
            if (!recovery_write_path(
                    file, &source->auxiliary_path,
                    destination_directory)) return false;
        } else fputs("null", file);
        fputs(", \"module\": ", file);
        porpoise_json_write_string(
            file, source->module == NULL ? "" : source->module);
        fprintf(file, ", \"permissive\": %s}",
                source->permissive ? "true" : "false");
        if (index + 1U < target->symbol_source_count) fputc(',', file);
        fputc('\n', file);
    }
    fputs("      ]", file);
    return !ferror(file);
}

static bool recovery_write_overrides(
    FILE *file,
    const PorpoiseRecoveryTarget *target) {
    size_t index;
    if (target->override_count == 0U) return fputs("[]", file) >= 0;
    if (fputs("[\n", file) < 0) return false;
    for (index = 0U; index < target->override_count; index++) {
        const PorpoiseRecoveryOverride *override = &target->overrides[index];
        fputs("        {\"target\": ", file);
        porpoise_json_write_string(file, override->target);
        fputs(", \"module\": ", file);
        porpoise_json_write_string(file, override->module);
        fprintf(file, ", \"address\": %" PRIu32
                ", \"size\": %" PRIu32 ", \"fingerprint\": ",
                override->address, override->size);
        porpoise_json_write_string(file, override->normalized_fingerprint);
        fputs(", \"action\": ", file);
        porpoise_json_write_string(
            file, recovery_override_action_json_name(override->action));
        fputs(", \"contract\": ", file);
        if (!recovery_write_nullable_string(
                file, override->contract_name)) return false;
        fprintf(file, ", \"acknowledge_conflict\": %s}",
                override->acknowledge_conflict ? "true" : "false");
        if (index + 1U < target->override_count) fputc(',', file);
        fputc('\n', file);
    }
    fputs("      ]", file);
    return !ferror(file);
}

static bool recovery_write_annotations(
    FILE *file,
    const PorpoiseRecoveryTarget *target) {
    size_t index;
    if (target->annotation_count == 0U) return fputs("[]", file) >= 0;
    if (fputs("[\n", file) < 0) return false;
    for (index = 0U; index < target->annotation_count; index++) {
        const PorpoiseRecoveryAnnotation *annotation =
            &target->annotations[index];
        fputs("        {\"target\": ", file);
        porpoise_json_write_string(file, annotation->target);
        fputs(", \"module\": ", file);
        porpoise_json_write_string(file, annotation->module);
        fprintf(file, ", \"address\": %" PRIu32
                ", \"size\": %" PRIu32 ", \"fingerprint\": ",
                annotation->address, annotation->size);
        porpoise_json_write_string(file, annotation->normalized_fingerprint);
        fputs(", \"exact_bytes_sha256\": ", file);
        porpoise_json_write_string(file, annotation->exact_bytes_sha256);
        fputs(", \"interpretation\": ", file);
        porpoise_json_write_string(
            file, porpoise_recovery_annotation_interpretation_name(
                      annotation->interpretation));
        fprintf(file, ", \"count\": %" PRIu32 ", \"encoding\": ",
                annotation->element_count);
        if (!recovery_write_nullable_string(file, annotation->encoding))
            return false;
        fputc('}', file);
        if (index + 1U < target->annotation_count) fputc(',', file);
        fputc('\n', file);
    }
    fputs("      ]", file);
    return !ferror(file);
}

static bool recovery_write_cache(
    FILE *file,
    const PorpoiseRecoveryTargetCache *cache,
    const char *destination_directory) {
    size_t index;
    if (recovery_cache_is_empty(cache)) return fputs("null", file) >= 0;
    fputs("{\n        \"input_sha256\": ", file);
    porpoise_json_write_string(file, cache->input_sha256);
    fputs(",\n        \"settings_sha256\": ", file);
    porpoise_json_write_string(file, cache->settings_sha256);
    fputs(",\n        \"dtk_version\": ", file);
    if (!recovery_write_nullable_string(file, cache->dtk_version)) return false;
    fputs(",\n        \"dependencies\": ", file);
    if (cache->dependency_count == 0U) {
        fputs("[]", file);
    } else {
        fputs("[\n", file);
        for (index = 0U; index < cache->dependency_count; index++) {
            const PorpoiseRecoveryDependencyCacheEntry *dependency =
                &cache->dependencies[index];
            fputs("          {\"path\": ", file);
            if (!recovery_write_path(
                    file, &dependency->path,
                    destination_directory)) return false;
            fputs(", \"sha256\": ", file);
            porpoise_json_write_string(file, dependency->sha256);
            fprintf(file, ", \"size\": %" PRIu64
                    ", \"mtime_ns\": %" PRIu64 "}",
                    dependency->size, dependency->mtime_ns);
            if (index + 1U < cache->dependency_count) fputc(',', file);
            fputc('\n', file);
        }
        fputs("        ]", file);
    }
    fputs(",\n        \"matches\": ", file);
    if (cache->match_count == 0U) {
        fputs("[]", file);
    } else {
        fputs("[\n", file);
        for (index = 0U; index < cache->match_count; index++) {
            const PorpoiseRecoveryMatchCacheEntry *match =
                &cache->matches[index];
            fputs("          {\"module\": ", file);
            porpoise_json_write_string(file, match->module);
            fprintf(file, ", \"address\": %" PRIu32
                    ", \"size\": %" PRIu32 ", \"fingerprint\": ",
                    match->address, match->size);
            porpoise_json_write_string(file, match->normalized_fingerprint);
            fputs(", \"canonical_identity\": ", file);
            porpoise_json_write_string(file, match->canonical_identity);
            fputs(", \"contract\": ", file);
            if (!recovery_write_nullable_string(
                    file, match->contract_name)) return false;
            fputc('}', file);
            if (index + 1U < cache->match_count) fputc(',', file);
            fputc('\n', file);
        }
        fputs("        ]", file);
    }
    fputs("\n      }", file);
    return !ferror(file);
}

static bool recovery_write_title_host(
    FILE *file,
    const PorpoiseRecoveryTarget *target) {
    const PorpoiseRecoveryTitleHostProfile *profile = &target->title_host;
    size_t index;
    if (!target->has_title_host) return fputs("null", file) >= 0;
    fprintf(file, "{\n        \"entry_address\": %" PRIu32
            ",\n        \"gpr\": [",
            profile->entry_address);
    for (index = 0U; index < PORPOISE_RECOVERY_TITLE_HOST_GPR_COUNT;
         index++) {
        if (index != 0U) fputs(", ", file);
        fprintf(file, "%" PRIu32, profile->gpr[index]);
    }
    fprintf(file, "],\n        \"arena_lo\": %" PRIu32
            ",\n        \"arena_hi\": %" PRIu32
            ",\n        \"startup_functions\": ",
            profile->arena_lo, profile->arena_hi);
    if (profile->startup_function_count == 0U) {
        fputs("[]", file);
    } else {
        fputs("[\n", file);
        for (index = 0U; index < profile->startup_function_count; index++) {
            const PorpoiseRecoveryTitleStartupFunction *function =
                &profile->startup_functions[index];
            fputs("          {\"module\": ", file);
            porpoise_json_write_string(file, function->module);
            fprintf(file, ", \"address\": %" PRIu32
                    ", \"size\": %" PRIu32 ", \"fingerprint\": ",
                    function->address, function->size);
            porpoise_json_write_string(
                file, function->normalized_fingerprint);
            fprintf(file, ", \"flags\": %" PRIu32 "}", function->flags);
            if (index + 1U < profile->startup_function_count) fputc(',', file);
            fputc('\n', file);
        }
        fputs("        ]", file);
    }
    fputs(",\n        \"initial_words\": ", file);
    if (profile->initial_word_count == 0U) {
        fputs("[]", file);
    } else {
        fputs("[\n", file);
        for (index = 0U; index < profile->initial_word_count; index++) {
            const PorpoiseRecoveryTitleInitialWord *word =
                &profile->initial_words[index];
            fprintf(file, "          {\"address\": %" PRIu32
                    ", \"value\": %" PRIu32 "}",
                    word->address, word->value);
            if (index + 1U < profile->initial_word_count) fputc(',', file);
            fputc('\n', file);
        }
        fputs("        ]", file);
    }
    fprintf(file, ",\n        \"initialize_dvd\": %s"
            ",\n        \"input_sha256\": ",
            profile->initialize_dvd ? "true" : "false");
    porpoise_json_write_string(file, profile->input_sha256);
    fputs(",\n        \"symbol_sources_sha256\": ", file);
    if (!recovery_write_nullable_string(
            file, profile->symbol_sources_sha256)) return false;
    fputs(",\n        \"sdk_catalogs_sha256\": ", file);
    if (!recovery_write_nullable_string(
            file, profile->sdk_catalogs_sha256)) return false;
    fputs("\n      }", file);
    return !ferror(file);
}

static bool recovery_write_target(
    FILE *file,
    const PorpoiseRecoveryTarget *target,
    const char *destination_directory) {
    fputs("    {\n      \"id\": ", file);
    porpoise_json_write_string(file, target->id);
    fprintf(file, ",\n      \"enabled\": %s,\n      \"source_kind\": ",
            target->enabled ? "true" : "false");
    porpoise_json_write_string(
        file, porpoise_recovery_source_kind_name(target->source_kind));
    fputs(",\n      \"input\": ", file);
    if (!recovery_write_path(
            file, &target->input, destination_directory)) return false;
    fputs(",\n      \"output\": ", file);
    if (!recovery_write_path(
            file, &target->output, destination_directory)) return false;
    fputs(",\n      \"entry\": ", file);
    if (!recovery_write_nullable_string(file, target->entry)) return false;
    fprintf(file, ",\n      \"strict\": %s,\n      \"sdk_policy\": ",
            target->strict ? "true" : "false");
    porpoise_json_write_string(
        file, recovery_sdk_policy_json_name(target->sdk_policy));
    fputs(",\n      \"symbol_sources\": ", file);
    if (!recovery_write_symbol_sources(
            file, target, destination_directory)) return false;
    fputs(",\n      \"skip_list\": ", file);
    if (target->has_skip_list) {
        if (!recovery_write_path(
                file, &target->skip_list,
                destination_directory)) return false;
    } else fputs("null", file);
    fputs(",\n      \"overrides\": ", file);
    if (!recovery_write_overrides(file, target)) return false;
    fputs(",\n      \"annotations\": ", file);
    if (!recovery_write_annotations(file, target)) return false;
    fputs(",\n      \"title_host\": ", file);
    if (!recovery_write_title_host(file, target)) return false;
    fputs(",\n      \"cache\": ", file);
    if (!recovery_write_cache(
            file, &target->cache, destination_directory)) return false;
    fputs("\n    }", file);
    return !ferror(file);
}

static bool recovery_write_document(
    FILE *file,
    const PorpoiseRecoveryProject *project,
    const char *destination_directory) {
    size_t target_index;
    fprintf(file, "{\n  \"schema_version\": %u,\n  \"sdk_catalogs\": ",
            PORPOISE_RECOVERY_PROJECT_SCHEMA_VERSION);
    if (!recovery_write_path_array(
            file, project->sdk_catalogs, project->sdk_catalog_count,
            destination_directory, 4U)) return false;
    fputs(",\n  \"abi_contracts\": ", file);
    if (!recovery_write_path_array(
            file, project->abi_contracts, project->abi_contract_count,
            destination_directory, 4U)) return false;
    fputs(",\n  \"targets\": ", file);
    if (project->target_count == 0U) {
        fputs("[]", file);
    } else {
        fputs("[\n", file);
        for (target_index = 0U; target_index < project->target_count;
             target_index++) {
            if (!recovery_write_target(
                    file, &project->targets[target_index],
                    destination_directory)) return false;
            if (target_index + 1U < project->target_count) fputc(',', file);
            fputc('\n', file);
        }
        fputs("  ]", file);
    }
    fputs("\n}\n", file);
    return !ferror(file);
}

static bool recovery_sibling_path(
    const char *destination,
    const char *tag,
    unsigned int attempt,
    char path[PORPOISE_PATH_CAPACITY]) {
    char parent[PORPOISE_PATH_CAPACITY];
    char base[PORPOISE_PATH_CAPACITY];
    char name[PORPOISE_PATH_CAPACITY];
    unsigned long seed =
        (unsigned long)time(NULL) ^ (unsigned long)RECOVERY_GETPID();
    return porpoise_path_parent(parent, sizeof(parent), destination) &&
           porpoise_path_basename(base, sizeof(base), destination) &&
           porpoise_format(
               name, sizeof(name), ".%s.porpoise-%s-%08lx-%u",
               base, tag, seed, attempt) &&
           porpoise_path_join(
               path, PORPOISE_PATH_CAPACITY, parent, name);
}

static FILE *recovery_create_stage_file(
    const char *destination,
    char stage_path[PORPOISE_PATH_CAPACITY],
    PorpoiseDiagnostics *diagnostics) {
    unsigned int attempt;
    for (attempt = 0U; attempt < 1000U; attempt++) {
        int descriptor;
        FILE *file;
        if (!recovery_sibling_path(
                destination, "save", attempt, stage_path)) {
            recovery_add_diagnostic(
                diagnostics, PORPOISE_EXIT_USAGE, destination, 0U,
                "save staging path is too long");
            return NULL;
        }
#ifdef _WIN32
        descriptor = RECOVERY_OPEN(
            stage_path,
            _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
            _S_IREAD | _S_IWRITE);
#else
        descriptor = RECOVERY_OPEN(
            stage_path, O_WRONLY | O_CREAT | O_EXCL,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP |
                S_IROTH | S_IWOTH);
#endif
        if (descriptor < 0) {
            if (errno == EEXIST) continue;
            recovery_add_diagnostic(
                diagnostics, PORPOISE_EXIT_IO, destination, 0U,
                "failed to create adjacent recovery project stage: %s",
                strerror(errno));
            return NULL;
        }
        file = RECOVERY_FDOPEN(descriptor, "wb");
        if (file != NULL) return file;
        {
            int saved_errno = errno;
            (void)RECOVERY_CLOSE(descriptor);
            (void)remove(stage_path);
            errno = saved_errno;
        }
        recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_IO, destination, 0U,
            "failed to open adjacent recovery project stage: %s",
            strerror(errno));
        return NULL;
    }
    recovery_add_diagnostic(
        diagnostics, PORPOISE_EXIT_IO, destination, 0U,
        "cannot allocate a unique adjacent recovery project stage");
    return NULL;
}

static bool recovery_close_stage(FILE *file) {
    bool success = ferror(file) == 0;
    int descriptor = RECOVERY_FILENO(file);
    if (success && fflush(file) != 0) success = false;
    if (success &&
        (descriptor < 0 || RECOVERY_COMMIT(descriptor) != 0)) {
        success = false;
    }
    if (fclose(file) != 0) success = false;
    return success;
}

static void recovery_cleanup_save_file(
    const char *path,
    PorpoiseDiagnostics *diagnostics) {
    if (path == NULL || path[0] == '\0' || !porpoise_path_exists(path)) {
        return;
    }
    if (remove(path) != 0) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_WARNING, path, 0U, 0U,
            "could not remove recovery save temporary file: %s",
            strerror(errno));
    }
}

#ifdef _WIN32
static bool recovery_unique_backup_path(
    const char *destination,
    char backup_path[PORPOISE_PATH_CAPACITY],
    PorpoiseDiagnostics *diagnostics) {
    unsigned int attempt;
    for (attempt = 0U; attempt < 1000U; attempt++) {
        if (!recovery_sibling_path(
                destination, "backup", attempt, backup_path)) {
            recovery_add_diagnostic(
                diagnostics, PORPOISE_EXIT_USAGE, destination, 0U,
                "save rollback path is too long");
            return false;
        }
        if (!porpoise_path_exists(backup_path)) return true;
    }
    recovery_add_diagnostic(
        diagnostics, PORPOISE_EXIT_IO, destination, 0U,
        "cannot allocate a recovery save rollback path");
    return false;
}
#endif

static bool recovery_publish_save_file(
    const char *stage_path,
    const char *destination,
    bool destination_existed,
    PorpoiseDiagnostics *diagnostics) {
#ifdef _WIN32
    if (!destination_existed) {
        if (MoveFileExA(stage_path, destination, MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
        recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_IO, destination, 0U,
            "failed to publish recovery project atomically "
            "(Windows error %lu)",
            (unsigned long)GetLastError());
        return false;
    }
    {
        char backup_path[PORPOISE_PATH_CAPACITY];
        DWORD publish_error;
        if (!recovery_unique_backup_path(
                destination, backup_path, diagnostics)) return false;
        if (ReplaceFileA(
                destination, stage_path, backup_path,
                REPLACEFILE_WRITE_THROUGH, NULL, NULL)) {
            recovery_cleanup_save_file(backup_path, diagnostics);
            return true;
        }
        publish_error = GetLastError();
        if (porpoise_path_exists(backup_path)) {
            if (!MoveFileExA(
                    backup_path, destination,
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                recovery_add_diagnostic(
                    diagnostics, PORPOISE_EXIT_IO, destination, 0U,
                    "recovery project publication failed and rollback from "
                    "%s also failed (Windows error %lu)",
                    backup_path, (unsigned long)GetLastError());
            }
        } else if (!porpoise_path_exists(destination)) {
            recovery_add_diagnostic(
                diagnostics, PORPOISE_EXIT_IO, destination, 0U,
                "recovery project publication failed and the prior project "
                "could not be located for rollback");
        }
        recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_IO, destination, 0U,
            "failed to replace recovery project atomically "
            "(Windows error %lu)",
            (unsigned long)publish_error);
        return false;
    }
#else
    (void)destination_existed;
    return porpoise_move_path(stage_path, destination, diagnostics);
#endif
}

int porpoise_recovery_project_save(
    const PorpoiseRecoveryProject *project,
    const char *path,
    PorpoiseDiagnostics *diagnostics) {
    char normalized_host[PORPOISE_PATH_CAPACITY];
    char normalized[PORPOISE_PATH_CAPACITY];
    char destination_directory[PORPOISE_PATH_CAPACITY];
    char stage_path[PORPOISE_PATH_CAPACITY];
    FILE *file = NULL;
    bool destination_existed;
    bool destination_exists_now;
    bool serialized;
    int result;

    if (path == NULL || path[0] == '\0' ||
        recovery_path_has_expansion(path)) {
        return recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_USAGE, path, 0U,
            "save path is required and must not use environment or tilde expansion");
    }
    result = recovery_validate_for_save(project, path, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    if (!porpoise_path_normalize_lexical(
            normalized_host, sizeof(normalized_host), path) ||
        !recovery_path_normalize_generic(
            normalized_host, normalized, sizeof(normalized)) ||
        !porpoise_path_parent(
            destination_directory, sizeof(destination_directory),
            normalized)) {
        return recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_USAGE, path, 0U,
            "save path is invalid or too long");
    }
    destination_existed = porpoise_path_exists(normalized_host);
    if (destination_existed &&
        porpoise_path_is_directory(normalized_host)) {
        return recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_IO, path, 0U,
            "recovery project destination is a directory");
    }
    stage_path[0] = '\0';
    file = recovery_create_stage_file(
        normalized_host, stage_path, diagnostics);
    if (file == NULL) {
        return PORPOISE_EXIT_IO;
    }
    serialized = recovery_write_document(
        file, project, destination_directory);
    if (!recovery_close_stage(file)) serialized = false;
    file = NULL;
    if (!serialized) {
        recovery_cleanup_save_file(stage_path, diagnostics);
        return recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_IO, path, 0U,
            "failed to serialize recovery project; prior project was not changed");
    }
    destination_exists_now = porpoise_path_exists(normalized_host);
    if (destination_exists_now != destination_existed ||
        (destination_exists_now &&
         porpoise_path_is_directory(normalized_host))) {
        recovery_cleanup_save_file(stage_path, diagnostics);
        return recovery_add_diagnostic(
            diagnostics, PORPOISE_EXIT_IO, path, 0U,
            "recovery project destination changed while the save was staged; "
            "prior project was not changed");
    }
    if (!recovery_publish_save_file(
            stage_path, normalized_host, destination_existed,
            diagnostics)) {
        recovery_cleanup_save_file(stage_path, diagnostics);
        return PORPOISE_EXIT_IO;
    }
    return PORPOISE_EXIT_OK;
}
