#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#endif

#include "porpoise/recovery_cache.h"

#include "porpoise/sha256.h"
#include "porpoise/signature.h"
#include "porpoise/util.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define RECOVERY_CACHE_TREE_MAX_DEPTH 64U

typedef struct RecoveryCacheTreeEntry {
    char *relative_path;
    char *full_path;
    bool directory;
} RecoveryCacheTreeEntry;

typedef struct RecoveryCacheTree {
    RecoveryCacheTreeEntry *entries;
    size_t count;
    size_t capacity;
} RecoveryCacheTree;

typedef struct RecoveryCacheDependencySnapshot {
    char *path;
    char sha256[PORPOISE_SHA256_HEX_SIZE];
    uint64_t size;
    uint64_t mtime_ns;
    size_t input_index;
    bool available;
} RecoveryCacheDependencySnapshot;

static int recovery_cache_error(
    PorpoiseDiagnostics *diagnostics,
    int result,
    const char *path,
    const char *format,
    ...) {
    char message[PORPOISE_MESSAGE_CAPACITY];
    va_list arguments;
    int written;

    va_start(arguments, format);
    written = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (written < 0) return PORPOISE_EXIT_INTERNAL;
    if (diagnostics != NULL &&
        !porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            path == NULL ? "" : path, 0U, 0U, "%s", message)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    return result;
}

static bool recovery_cache_cancelled(
    const PorpoiseOperationCallbacks *operation) {
    return porpoise_operation_cancelled(operation);
}

static char *recovery_cache_strdup(const char *value) {
    return value == NULL ? NULL : porpoise_strdup(value);
}

static bool recovery_cache_optional_equal(
    const char *left,
    const char *right) {
    if (left == NULL || right == NULL) return left == right;
    return strcmp(left, right) == 0;
}

static bool recovery_cache_hex_is_normalized(const char *value) {
    size_t index;
    if (value == NULL || strlen(value) != PORPOISE_SHA256_DIGEST_SIZE * 2U)
        return false;
    for (index = 0U; index < PORPOISE_SHA256_DIGEST_SIZE * 2U; index++) {
        if (!((value[index] >= '0' && value[index] <= '9') ||
              (value[index] >= 'a' && value[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

static void recovery_cache_hash_u64(
    PorpoiseSha256Context *hash,
    uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0U; index < sizeof(bytes); index++)
        bytes[sizeof(bytes) - 1U - index] =
            (uint8_t)(value >> (index * 8U));
    porpoise_sha256_update(hash, bytes, sizeof(bytes));
}

static void recovery_cache_hash_string(
    PorpoiseSha256Context *hash,
    const char *value) {
    size_t length = value == NULL ? 0U : strlen(value);
    recovery_cache_hash_u64(hash, (uint64_t)length);
    if (length != 0U) porpoise_sha256_update(hash, value, length);
}

static void recovery_cache_settings_hash(
    const char *identity,
    const PorpoiseDtkImportMetadata *dtk_metadata,
    char output[PORPOISE_SHA256_HEX_SIZE]) {
    PorpoiseSha256Context hash;
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    static const char domain[] = "porpoise-recovery-settings-v1";

    porpoise_sha256_init(&hash);
    recovery_cache_hash_string(&hash, domain);
    recovery_cache_hash_string(&hash, identity == NULL ? "" : identity);
    recovery_cache_hash_string(
        &hash, dtk_metadata == NULL ? "" : dtk_metadata->settings_sha256);
    porpoise_sha256_final(&hash, digest);
    porpoise_sha256_hex(digest, output);
}

void porpoise_recovery_cache_inputs_init(
    PorpoiseRecoveryCacheInputs *inputs) {
    if (inputs == NULL) return;
    memset(inputs, 0, sizeof(*inputs));
    inputs->settings_identity = "";
}

void porpoise_recovery_cache_validation_init(
    PorpoiseRecoveryCacheValidation *validation) {
    if (validation == NULL) return;
    memset(validation, 0, sizeof(*validation));
    validation->state = PORPOISE_RECOVERY_CACHE_MISS;
    validation->dependency_index = SIZE_MAX;
}

void porpoise_recovery_target_cache_clear(
    PorpoiseRecoveryTargetCache *cache) {
    size_t index;
    if (cache == NULL) return;
    free(cache->input_sha256);
    free(cache->settings_sha256);
    free(cache->dtk_version);
    for (index = 0U; index < cache->dependency_count; index++) {
        free(cache->dependencies[index].path.value);
        free(cache->dependencies[index].path.resolved);
        free(cache->dependencies[index].sha256);
    }
    for (index = 0U; index < cache->match_count; index++) {
        free(cache->matches[index].module);
        free(cache->matches[index].normalized_fingerprint);
        free(cache->matches[index].canonical_identity);
        free(cache->matches[index].contract_name);
    }
    free(cache->dependencies);
    free(cache->matches);
    memset(cache, 0, sizeof(*cache));
}

static bool recovery_cache_dtk_metadata_valid(
    const PorpoiseDtkImportMetadata *metadata) {
    if (metadata == NULL) return true;
    if (metadata->schema_version !=
            PORPOISE_DTK_IMPORT_METADATA_SCHEMA_VERSION ||
        (metadata->source_kind != PORPOISE_DTK_SOURCE_MANAGED_ELF &&
         metadata->source_kind != PORPOISE_DTK_SOURCE_PREPARED_ASM) ||
        memchr(metadata->dtk_version, '\0',
               sizeof(metadata->dtk_version)) == NULL) {
        return false;
    }
    return recovery_cache_hex_is_normalized(metadata->input_sha256) &&
           recovery_cache_hex_is_normalized(metadata->tool_sha256) &&
           recovery_cache_hex_is_normalized(metadata->settings_sha256) &&
           recovery_cache_hex_is_normalized(metadata->dependency_sha256) &&
           recovery_cache_hex_is_normalized(metadata->content_sha256);
}

static int recovery_cache_path_info(
    const char *path,
    bool *directory_out,
    uint64_t *size_out,
    uint64_t *mtime_ns_out,
    PorpoiseDiagnostics *diagnostics) {
#ifdef _WIN32
    struct _stat64 status;
    DWORD attributes;
    __time64_t modified;
    __int64 length;
    attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
        _stat64(path, &status) != 0) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_IO, path,
            "cannot inspect recovery-cache dependency: %s", strerror(errno));
    }
    if ((status.st_mode & _S_IFMT) != _S_IFDIR &&
        (status.st_mode & _S_IFMT) != _S_IFREG) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_USAGE, path,
            "recovery cache accepts only regular files and directories");
    }
    *directory_out = (status.st_mode & _S_IFMT) == _S_IFDIR;
    modified = status.st_mtime;
    length = status.st_size;
#else
    struct stat status;
    time_t modified;
    off_t length;
    if (lstat(path, &status) != 0) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_IO, path,
            "cannot inspect recovery-cache dependency: %s", strerror(errno));
    }
    if (!S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode)) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_USAGE, path,
            "recovery cache rejects links and non-regular filesystem objects");
    }
    *directory_out = S_ISDIR(status.st_mode);
    modified = status.st_mtime;
    length = status.st_size;
#endif
    if (length < 0) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_IO, path,
            "recovery-cache dependency has an invalid negative size");
    }
    *size_out = (uint64_t)length;
    if (modified < 0 ||
        (uint64_t)modified > UINT64_MAX / UINT64_C(1000000000)) {
        *mtime_ns_out = 0U;
    } else {
        *mtime_ns_out = (uint64_t)modified * UINT64_C(1000000000);
    }
    return PORPOISE_EXIT_OK;
}

static int recovery_cache_hash_file(
    const char *path,
    const PorpoiseOperationCallbacks *operation,
    char output[PORPOISE_SHA256_HEX_SIZE],
    PorpoiseDiagnostics *diagnostics) {
    FILE *file = fopen(path, "rb");
    PorpoiseSha256Context hash;
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    uint8_t bytes[16384];
    size_t count;
    bool failed = false;

    if (file == NULL) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_IO, path,
            "cannot open recovery-cache dependency: %s", strerror(errno));
    }
    porpoise_sha256_init(&hash);
    for (;;) {
        if (recovery_cache_cancelled(operation)) {
            fclose(file);
            return PORPOISE_EXIT_CANCELLED;
        }
        count = fread(bytes, 1U, sizeof(bytes), file);
        if (count != 0U) porpoise_sha256_update(&hash, bytes, count);
        if (count < sizeof(bytes)) {
            failed = ferror(file) != 0;
            break;
        }
    }
    if (fclose(file) != 0) failed = true;
    if (failed) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_IO, path,
            "cannot read recovery-cache dependency");
    }
    porpoise_sha256_final(&hash, digest);
    porpoise_sha256_hex(digest, output);
    return PORPOISE_EXIT_OK;
}

static void recovery_cache_tree_free(RecoveryCacheTree *tree) {
    size_t index;
    if (tree == NULL) return;
    for (index = 0U; index < tree->count; index++) {
        free(tree->entries[index].relative_path);
        free(tree->entries[index].full_path);
    }
    free(tree->entries);
    memset(tree, 0, sizeof(*tree));
}

static int recovery_cache_tree_add(
    RecoveryCacheTree *tree,
    const char *relative_path,
    const char *full_path,
    bool directory,
    PorpoiseDiagnostics *diagnostics) {
    RecoveryCacheTreeEntry *entry;
    if (!porpoise_grow_array(
            (void **)&tree->entries, &tree->capacity,
            sizeof(*tree->entries), tree->count + 1U)) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_INTERNAL, full_path,
            "out of memory while hashing recovery input");
    }
    entry = &tree->entries[tree->count];
    memset(entry, 0, sizeof(*entry));
    entry->relative_path = recovery_cache_strdup(relative_path);
    entry->full_path = recovery_cache_strdup(full_path);
    entry->directory = directory;
    if (entry->relative_path == NULL || entry->full_path == NULL) {
        free(entry->relative_path);
        free(entry->full_path);
        memset(entry, 0, sizeof(*entry));
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_INTERNAL, full_path,
            "out of memory while hashing recovery input");
    }
    tree->count++;
    return PORPOISE_EXIT_OK;
}

static int recovery_cache_tree_collect(
    const char *root,
    const char *relative_parent,
    size_t depth,
    RecoveryCacheTree *tree,
    const PorpoiseOperationCallbacks *operation,
    PorpoiseDiagnostics *diagnostics) {
    char directory_path[PORPOISE_PATH_CAPACITY];
    DIR *directory;
    struct dirent *entry;

    if (depth > RECOVERY_CACHE_TREE_MAX_DEPTH) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_USAGE, root,
            "recovery input directory nesting exceeds the supported limit");
    }
    if (relative_parent[0] == '\0') {
        if (!porpoise_copy_string(directory_path, sizeof(directory_path), root))
            return PORPOISE_EXIT_USAGE;
    } else if (!porpoise_path_join(
                   directory_path, sizeof(directory_path),
                   root, relative_parent)) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_USAGE, root,
            "recovery input path exceeds the supported length");
    }
    directory = opendir(directory_path);
    if (directory == NULL) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_IO, directory_path,
            "cannot open recovery input directory: %s", strerror(errno));
    }
    while ((entry = readdir(directory)) != NULL) {
        char relative_path[PORPOISE_PATH_CAPACITY];
        char full_path[PORPOISE_PATH_CAPACITY];
        bool is_directory;
        uint64_t ignored_size;
        uint64_t ignored_mtime;
        int result;
        size_t index;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) continue;
        if (recovery_cache_cancelled(operation)) {
            closedir(directory);
            return PORPOISE_EXIT_CANCELLED;
        }
        if (relative_parent[0] == '\0') {
            if (!porpoise_copy_string(
                    relative_path, sizeof(relative_path), entry->d_name)) {
                closedir(directory);
                return PORPOISE_EXIT_USAGE;
            }
        } else if (!porpoise_path_join(
                       relative_path, sizeof(relative_path),
                       relative_parent, entry->d_name)) {
            closedir(directory);
            return PORPOISE_EXIT_USAGE;
        }
        for (index = 0U; relative_path[index] != '\0'; index++) {
            if (relative_path[index] == '\\') relative_path[index] = '/';
        }
        if (!porpoise_path_join(
                full_path, sizeof(full_path), root, relative_path)) {
            closedir(directory);
            return PORPOISE_EXIT_USAGE;
        }
        result = recovery_cache_path_info(
            full_path, &is_directory, &ignored_size, &ignored_mtime,
            diagnostics);
        if (result != PORPOISE_EXIT_OK) {
            closedir(directory);
            return result;
        }
        result = recovery_cache_tree_add(
            tree, relative_path, full_path, is_directory, diagnostics);
        if (result != PORPOISE_EXIT_OK) {
            closedir(directory);
            return result;
        }
        if (is_directory) {
            result = recovery_cache_tree_collect(
                root, relative_path, depth + 1U, tree,
                operation, diagnostics);
            if (result != PORPOISE_EXIT_OK) {
                closedir(directory);
                return result;
            }
        }
    }
    if (closedir(directory) != 0) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_IO, directory_path,
            "cannot close recovery input directory");
    }
    return PORPOISE_EXIT_OK;
}

static int recovery_cache_tree_compare(const void *left, const void *right) {
    const RecoveryCacheTreeEntry *a =
        (const RecoveryCacheTreeEntry *)left;
    const RecoveryCacheTreeEntry *b =
        (const RecoveryCacheTreeEntry *)right;
    return strcmp(a->relative_path, b->relative_path);
}

static int recovery_cache_hash_tree(
    const char *root,
    const PorpoiseOperationCallbacks *operation,
    char output[PORPOISE_SHA256_HEX_SIZE],
    PorpoiseDiagnostics *diagnostics) {
    RecoveryCacheTree tree;
    PorpoiseSha256Context hash;
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    size_t index;
    int result;
    static const char domain[] = "porpoise-recovery-input-tree-v1";

    memset(&tree, 0, sizeof(tree));
    result = recovery_cache_tree_collect(
        root, "", 0U, &tree, operation, diagnostics);
    if (result != PORPOISE_EXIT_OK) {
        recovery_cache_tree_free(&tree);
        return result;
    }
    if (tree.count > 1U) {
        qsort(tree.entries, tree.count, sizeof(*tree.entries),
              recovery_cache_tree_compare);
    }
    porpoise_sha256_init(&hash);
    recovery_cache_hash_string(&hash, domain);
    recovery_cache_hash_u64(&hash, (uint64_t)tree.count);
    for (index = 0U; index < tree.count; index++) {
        RecoveryCacheTreeEntry *entry = &tree.entries[index];
        char file_hash[PORPOISE_SHA256_HEX_SIZE];
        uint8_t type = entry->directory ? 1U : 0U;
        if (recovery_cache_cancelled(operation)) {
            recovery_cache_tree_free(&tree);
            return PORPOISE_EXIT_CANCELLED;
        }
        porpoise_sha256_update(&hash, &type, sizeof(type));
        recovery_cache_hash_string(&hash, entry->relative_path);
        if (!entry->directory) {
            result = recovery_cache_hash_file(
                entry->full_path, operation, file_hash, diagnostics);
            if (result != PORPOISE_EXIT_OK) {
                recovery_cache_tree_free(&tree);
                return result;
            }
            recovery_cache_hash_string(&hash, file_hash);
        }
    }
    porpoise_sha256_final(&hash, digest);
    porpoise_sha256_hex(digest, output);
    recovery_cache_tree_free(&tree);
    return PORPOISE_EXIT_OK;
}

static int recovery_cache_hash_source(
    const char *path,
    const PorpoiseOperationCallbacks *operation,
    char output[PORPOISE_SHA256_HEX_SIZE],
    PorpoiseDiagnostics *diagnostics) {
    bool directory;
    uint64_t ignored_size;
    uint64_t ignored_mtime;
    int result;
    if (path == NULL || path[0] == '\0') {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_USAGE, "",
            "recovery cache requires a current source path");
    }
    result = recovery_cache_path_info(
        path, &directory, &ignored_size, &ignored_mtime, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    return directory
        ? recovery_cache_hash_tree(path, operation, output, diagnostics)
        : recovery_cache_hash_file(path, operation, output, diagnostics);
}

static int recovery_cache_dependency_compare(
    const void *left,
    const void *right) {
    const RecoveryCacheDependencySnapshot *a =
        (const RecoveryCacheDependencySnapshot *)left;
    const RecoveryCacheDependencySnapshot *b =
        (const RecoveryCacheDependencySnapshot *)right;
    return strcmp(a->path, b->path);
}

static void recovery_cache_dependency_snapshots_free(
    RecoveryCacheDependencySnapshot *dependencies,
    size_t count) {
    size_t index;
    for (index = 0U; index < count; index++) free(dependencies[index].path);
    free(dependencies);
}

static int recovery_cache_snapshot_dependencies(
    const PorpoiseRecoveryCacheInputs *inputs,
    bool soft_unavailable,
    RecoveryCacheDependencySnapshot **dependencies_out,
    size_t *count_out,
    PorpoiseDiagnostics *diagnostics) {
    RecoveryCacheDependencySnapshot *dependencies;
    size_t count;
    size_t index;
    size_t write_index;

    *dependencies_out = NULL;
    *count_out = 0U;
    if (inputs == NULL || inputs->dependency_count == 0U) return PORPOISE_EXIT_OK;
    if (inputs->dependencies == NULL ||
        inputs->dependency_count > SIZE_MAX / sizeof(*dependencies)) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_USAGE, "",
            "recovery-cache dependency input array is inconsistent");
    }
    count = inputs->dependency_count;
    dependencies = (RecoveryCacheDependencySnapshot *)calloc(
        count, sizeof(*dependencies));
    if (dependencies == NULL) return PORPOISE_EXIT_INTERNAL;
    for (index = 0U; index < count; index++) {
        const char *path = inputs->dependencies[index].path;
        char normalized[PORPOISE_PATH_CAPACITY];
        bool directory = false;
        int result;
        dependencies[index].input_index = index;
        if (path == NULL || path[0] == '\0' ||
            !porpoise_path_normalize_lexical(
                normalized, sizeof(normalized), path)) {
            if (!soft_unavailable) {
                recovery_cache_dependency_snapshots_free(dependencies, count);
                return recovery_cache_error(
                    diagnostics, PORPOISE_EXIT_USAGE,
                    path == NULL ? "" : path,
                    "recovery-cache dependency path is missing or invalid");
            }
            dependencies[index].path = recovery_cache_strdup(
                path == NULL ? "" : path);
            if (dependencies[index].path == NULL) {
                recovery_cache_dependency_snapshots_free(dependencies, count);
                return PORPOISE_EXIT_INTERNAL;
            }
            continue;
        }
        dependencies[index].path = recovery_cache_strdup(normalized);
        if (dependencies[index].path == NULL) {
            recovery_cache_dependency_snapshots_free(dependencies, count);
            return PORPOISE_EXIT_INTERNAL;
        }
        result = recovery_cache_path_info(
            normalized, &directory, &dependencies[index].size,
            &dependencies[index].mtime_ns,
            soft_unavailable ? NULL : diagnostics);
        if (result == PORPOISE_EXIT_OK && directory) {
            result = PORPOISE_EXIT_USAGE;
            if (!soft_unavailable) {
                recovery_cache_error(
                    diagnostics, result, normalized,
                    "recovery-cache dependencies must be regular files");
            }
        }
        if (result == PORPOISE_EXIT_OK) {
            result = recovery_cache_hash_file(
                normalized, inputs->operation, dependencies[index].sha256,
                soft_unavailable ? NULL : diagnostics);
        }
        if (result == PORPOISE_EXIT_CANCELLED) {
            recovery_cache_dependency_snapshots_free(dependencies, count);
            return result;
        }
        if (result != PORPOISE_EXIT_OK) {
            if (!soft_unavailable) {
                recovery_cache_dependency_snapshots_free(dependencies, count);
                return result;
            }
            continue;
        }
        dependencies[index].available = true;
    }
    if (count > 1U) {
        qsort(dependencies, count, sizeof(*dependencies),
              recovery_cache_dependency_compare);
    }
    write_index = 0U;
    for (index = 0U; index < count; index++) {
        if (write_index != 0U &&
            strcmp(dependencies[write_index - 1U].path,
                   dependencies[index].path) == 0) {
            RecoveryCacheDependencySnapshot *prior =
                &dependencies[write_index - 1U];
            if (!prior->available && dependencies[index].available) {
                size_t original_index = prior->input_index;
                free(prior->path);
                *prior = dependencies[index];
                prior->input_index = original_index;
                memset(&dependencies[index], 0, sizeof(dependencies[index]));
            } else {
                if (dependencies[index].input_index < prior->input_index)
                    prior->input_index = dependencies[index].input_index;
                free(dependencies[index].path);
                memset(&dependencies[index], 0, sizeof(dependencies[index]));
            }
            continue;
        }
        if (write_index != index) {
            dependencies[write_index] = dependencies[index];
            memset(&dependencies[index], 0, sizeof(dependencies[index]));
        }
        write_index++;
    }
    *dependencies_out = dependencies;
    *count_out = write_index;
    return PORPOISE_EXIT_OK;
}

static bool recovery_cache_is_absent(
    const PorpoiseRecoveryTargetCache *cache) {
    return cache->input_sha256 == NULL && cache->settings_sha256 == NULL &&
           cache->dtk_version == NULL && cache->dependency_count == 0U &&
           cache->match_count == 0U && cache->dependencies == NULL &&
           cache->matches == NULL;
}

static bool recovery_cache_is_partial(
    const PorpoiseRecoveryTargetCache *cache) {
    size_t index;
    if (cache->input_sha256 == NULL || cache->settings_sha256 == NULL ||
        (cache->dependency_count != 0U && cache->dependencies == NULL) ||
        (cache->match_count != 0U && cache->matches == NULL)) {
        return true;
    }
    for (index = 0U; index < cache->dependency_count; index++) {
        const PorpoiseRecoveryDependencyCacheEntry *dependency =
            &cache->dependencies[index];
        if ((dependency->path.resolved == NULL &&
             dependency->path.value == NULL) ||
            dependency->sha256 == NULL) return true;
    }
    for (index = 0U; index < cache->match_count; index++) {
        const PorpoiseRecoveryMatchCacheEntry *match = &cache->matches[index];
        if (match->module == NULL ||
            match->normalized_fingerprint == NULL ||
            match->canonical_identity == NULL) return true;
    }
    return false;
}

static bool recovery_cache_match_equal(
    const PorpoiseRecoveryMatchCacheEntry *left,
    const PorpoiseRecoveryMatchCacheEntry *right) {
    return left->address == right->address && left->size == right->size &&
           strcmp(left->module, right->module) == 0 &&
           strcmp(left->normalized_fingerprint,
                  right->normalized_fingerprint) == 0 &&
           strcmp(left->canonical_identity,
                  right->canonical_identity) == 0 &&
           recovery_cache_optional_equal(
               left->contract_name, right->contract_name);
}

static bool recovery_cache_matches_conflict(
    const PorpoiseRecoveryMatchCacheEntry *left,
    const PorpoiseRecoveryMatchCacheEntry *right) {
    bool same_locator = left->address == right->address &&
        left->size == right->size &&
        strcmp(left->module, right->module) == 0 &&
        strcmp(left->normalized_fingerprint,
               right->normalized_fingerprint) == 0;
    bool same_fingerprint = strcmp(
        left->normalized_fingerprint,
        right->normalized_fingerprint) == 0;
    bool same_identity = strcmp(
        left->canonical_identity, right->canonical_identity) == 0;
    bool identity_contract_equal =
        strcmp(left->canonical_identity, right->canonical_identity) == 0 &&
        recovery_cache_optional_equal(
            left->contract_name, right->contract_name);

    if (recovery_cache_match_equal(left, right)) return false;
    if (same_locator) return true;
    if (same_fingerprint && !identity_contract_equal) return true;
    if (same_identity &&
        (strcmp(left->normalized_fingerprint,
                right->normalized_fingerprint) != 0 ||
         !recovery_cache_optional_equal(
             left->contract_name, right->contract_name))) return true;
    return false;
}

static int recovery_cache_validate_match_records(
    const PorpoiseRecoveryTargetCache *cache,
    PorpoiseRecoveryCacheValidation *validation,
    PorpoiseDiagnostics *diagnostics) {
    size_t left;
    for (left = 0U; left < cache->match_count; left++) {
        const PorpoiseRecoveryMatchCacheEntry *match = &cache->matches[left];
        size_t right;
        if (match->size == 0U || match->canonical_identity[0] == '\0' ||
            !recovery_cache_hex_is_normalized(
                match->normalized_fingerprint)) {
            validation->state = PORPOISE_RECOVERY_CACHE_INVALID;
            return recovery_cache_error(
                diagnostics, PORPOISE_EXIT_USAGE, "",
                "recovery cache contains a malformed exact SDK match locator");
        }
        for (right = left + 1U; right < cache->match_count; right++) {
            if (recovery_cache_matches_conflict(
                    match, &cache->matches[right])) {
                validation->state = PORPOISE_RECOVERY_CACHE_INVALID;
                validation->reason_flags |=
                    PORPOISE_RECOVERY_CACHE_REASON_CONFLICTING_MATCH;
                return recovery_cache_error(
                    diagnostics, PORPOISE_EXIT_USAGE, "",
                    "recovery cache maps one exact SDK fingerprint or identity to conflicting records");
            }
        }
    }
    return PORPOISE_EXIT_OK;
}

static void recovery_cache_note_dependency(
    PorpoiseRecoveryCacheValidation *validation,
    const RecoveryCacheDependencySnapshot *dependency) {
    if (validation->dependency_index != SIZE_MAX) return;
    validation->dependency_index = dependency->input_index;
    porpoise_copy_string(
        validation->dependency_path,
        sizeof(validation->dependency_path), dependency->path);
}

static const char *recovery_cache_dependency_path(
    const PorpoiseRecoveryDependencyCacheEntry *dependency) {
    return dependency->path.resolved != NULL
        ? dependency->path.resolved : dependency->path.value;
}

static int recovery_cache_find_cached_dependency(
    const PorpoiseRecoveryTargetCache *cache,
    const char *normalized_path,
    size_t *index_out) {
    size_t index;
    size_t found = SIZE_MAX;
    for (index = 0U; index < cache->dependency_count; index++) {
        char normalized[PORPOISE_PATH_CAPACITY];
        const char *path = recovery_cache_dependency_path(
            &cache->dependencies[index]);
        if (!porpoise_path_normalize_lexical(
                normalized, sizeof(normalized), path)) return -1;
        if (strcmp(normalized, normalized_path) != 0) continue;
        if (found != SIZE_MAX) {
            const PorpoiseRecoveryDependencyCacheEntry *first =
                &cache->dependencies[found];
            const PorpoiseRecoveryDependencyCacheEntry *second =
                &cache->dependencies[index];
            if (first->size != second->size ||
                first->mtime_ns != second->mtime_ns ||
                strcmp(first->sha256, second->sha256) != 0) return -1;
        } else {
            found = index;
        }
    }
    if (found == SIZE_MAX) return 0;
    *index_out = found;
    return 1;
}

static int recovery_cache_current_input_hash(
    const PorpoiseRecoveryTarget *target,
    const PorpoiseRecoveryCacheInputs *inputs,
    const PorpoiseDtkImportMetadata *dtk_metadata,
    bool soft_unavailable,
    char output[PORPOISE_SHA256_HEX_SIZE],
    bool *available_out,
    PorpoiseDiagnostics *diagnostics) {
    const char *path;
    int result;
    *available_out = false;
    if (dtk_metadata != NULL) {
        if (!porpoise_copy_string(
                output, PORPOISE_SHA256_HEX_SIZE,
                dtk_metadata->input_sha256)) return PORPOISE_EXIT_INTERNAL;
        *available_out = true;
        return PORPOISE_EXIT_OK;
    }
    path = inputs != NULL && inputs->source_path != NULL
        ? inputs->source_path
        : target->input.resolved;
    result = recovery_cache_hash_source(
        path, inputs == NULL ? NULL : inputs->operation,
        output, soft_unavailable ? NULL : diagnostics);
    if (result == PORPOISE_EXIT_CANCELLED || result == PORPOISE_EXIT_INTERNAL)
        return result;
    if (result != PORPOISE_EXIT_OK) {
        return soft_unavailable ? PORPOISE_EXIT_OK : result;
    }
    *available_out = true;
    return PORPOISE_EXIT_OK;
}

int porpoise_recovery_target_cache_validate(
    const PorpoiseRecoveryTarget *target,
    const PorpoiseRecoveryCacheInputs *inputs,
    const PorpoiseDtkImportMetadata *dtk_metadata,
    PorpoiseRecoveryCacheValidation *validation,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseRecoveryCacheInputs defaults;
    RecoveryCacheDependencySnapshot *dependencies = NULL;
    size_t dependency_count = 0U;
    char input_sha256[PORPOISE_SHA256_HEX_SIZE];
    char settings_sha256[PORPOISE_SHA256_HEX_SIZE];
    const char *expected_dtk_version;
    bool input_available = false;
    size_t index;
    int result;

    if (validation != NULL) porpoise_recovery_cache_validation_init(validation);
    if (target == NULL || validation == NULL) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_INTERNAL, "",
            "recovery-cache validation arguments are invalid");
    }
    if (inputs == NULL) {
        porpoise_recovery_cache_inputs_init(&defaults);
        defaults.source_path = target->input.resolved;
        inputs = &defaults;
    }
    if (inputs->dependency_count != 0U && inputs->dependencies == NULL) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_USAGE, "",
            "recovery-cache dependency input array is inconsistent");
    }
    if (!recovery_cache_dtk_metadata_valid(dtk_metadata)) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_USAGE, "",
            "validated DTK import metadata is malformed or incompatible");
    }
    if (recovery_cache_is_absent(&target->cache)) {
        validation->state = PORPOISE_RECOVERY_CACHE_MISS;
        validation->reason_flags = PORPOISE_RECOVERY_CACHE_REASON_ABSENT;
        return PORPOISE_EXIT_OK;
    }
    if (recovery_cache_is_partial(&target->cache)) {
        validation->state = PORPOISE_RECOVERY_CACHE_MISS;
        validation->reason_flags = PORPOISE_RECOVERY_CACHE_REASON_INCOMPLETE;
        return PORPOISE_EXIT_OK;
    }
    if (!recovery_cache_hex_is_normalized(target->cache.input_sha256) ||
        !recovery_cache_hex_is_normalized(target->cache.settings_sha256)) {
        validation->state = PORPOISE_RECOVERY_CACHE_INVALID;
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_USAGE, "",
            "recovery cache contains malformed input or settings hashes");
    }
    for (index = 0U; index < target->cache.dependency_count; index++) {
        if (!recovery_cache_hex_is_normalized(
                target->cache.dependencies[index].sha256)) {
            validation->state = PORPOISE_RECOVERY_CACHE_INVALID;
            return recovery_cache_error(
                diagnostics, PORPOISE_EXIT_USAGE,
                recovery_cache_dependency_path(
                    &target->cache.dependencies[index]),
                "recovery cache contains a malformed dependency hash");
        }
    }
    result = recovery_cache_validate_match_records(
        &target->cache, validation, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    if (recovery_cache_cancelled(inputs->operation))
        return PORPOISE_EXIT_CANCELLED;

    validation->state = PORPOISE_RECOVERY_CACHE_HIT;
    result = recovery_cache_current_input_hash(
        target, inputs, dtk_metadata, true, input_sha256,
        &input_available, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    if (!input_available) {
        validation->reason_flags |=
            PORPOISE_RECOVERY_CACHE_REASON_INPUT_UNAVAILABLE;
    } else if (strcmp(input_sha256, target->cache.input_sha256) != 0) {
        validation->reason_flags |=
            PORPOISE_RECOVERY_CACHE_REASON_INPUT_CHANGED;
    }
    recovery_cache_settings_hash(
        inputs->settings_identity, dtk_metadata, settings_sha256);
    if (strcmp(settings_sha256, target->cache.settings_sha256) != 0) {
        validation->reason_flags |=
            PORPOISE_RECOVERY_CACHE_REASON_SETTINGS_CHANGED;
    }
    expected_dtk_version = dtk_metadata == NULL ||
                           dtk_metadata->dtk_version[0] == '\0'
        ? NULL : dtk_metadata->dtk_version;
    if (!recovery_cache_optional_equal(
            expected_dtk_version, target->cache.dtk_version)) {
        validation->reason_flags |=
            PORPOISE_RECOVERY_CACHE_REASON_DTK_CHANGED;
    }

    result = recovery_cache_snapshot_dependencies(
        inputs, true, &dependencies, &dependency_count, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    if (dependency_count != target->cache.dependency_count) {
        validation->reason_flags |=
            PORPOISE_RECOVERY_CACHE_REASON_DEPENDENCY_SET_CHANGED;
    }
    for (index = 0U; index < dependency_count; index++) {
        RecoveryCacheDependencySnapshot *current = &dependencies[index];
        size_t cached_index = SIZE_MAX;
        int found = recovery_cache_find_cached_dependency(
            &target->cache, current->path, &cached_index);
        if (found < 0) {
            recovery_cache_dependency_snapshots_free(
                dependencies, dependency_count);
            validation->state = PORPOISE_RECOVERY_CACHE_INVALID;
            return recovery_cache_error(
                diagnostics, PORPOISE_EXIT_USAGE, current->path,
                "recovery cache contains conflicting duplicate dependencies");
        }
        if (found == 0) {
            validation->reason_flags |=
                PORPOISE_RECOVERY_CACHE_REASON_DEPENDENCY_SET_CHANGED;
            recovery_cache_note_dependency(validation, current);
            continue;
        }
        if (!current->available) {
            validation->reason_flags |=
                PORPOISE_RECOVERY_CACHE_REASON_DEPENDENCY_MISSING;
            recovery_cache_note_dependency(validation, current);
            continue;
        }
        if (current->size != target->cache.dependencies[cached_index].size ||
            current->mtime_ns !=
                target->cache.dependencies[cached_index].mtime_ns) {
            validation->reason_flags |=
                PORPOISE_RECOVERY_CACHE_REASON_DEPENDENCY_METADATA_CHANGED;
            recovery_cache_note_dependency(validation, current);
        }
        if (strcmp(current->sha256,
                   target->cache.dependencies[cached_index].sha256) != 0) {
            validation->reason_flags |=
                PORPOISE_RECOVERY_CACHE_REASON_DEPENDENCY_CONTENT_CHANGED;
            recovery_cache_note_dependency(validation, current);
        }
    }
    recovery_cache_dependency_snapshots_free(dependencies, dependency_count);
    if (validation->reason_flags != PORPOISE_RECOVERY_CACHE_REASON_NONE)
        validation->state = PORPOISE_RECOVERY_CACHE_STALE;
    porpoise_operation_progress(
        inputs->operation, PORPOISE_PHASE_VALIDATE, 1U, 1U,
        validation->state == PORPOISE_RECOVERY_CACHE_HIT
            ? "recovery cache is current" : "recovery cache is stale");
    return PORPOISE_EXIT_OK;
}

static int recovery_cache_store_dependencies(
    PorpoiseRecoveryTargetCache *cache,
    const RecoveryCacheDependencySnapshot *dependencies,
    size_t count,
    PorpoiseDiagnostics *diagnostics) {
    size_t index;
    if (count == 0U) return PORPOISE_EXIT_OK;
    if (count > SIZE_MAX / sizeof(*cache->dependencies))
        return PORPOISE_EXIT_INTERNAL;
    cache->dependencies = (PorpoiseRecoveryDependencyCacheEntry *)calloc(
        count, sizeof(*cache->dependencies));
    if (cache->dependencies == NULL) return PORPOISE_EXIT_INTERNAL;
    cache->dependency_count = count;
    for (index = 0U; index < count; index++) {
        PorpoiseRecoveryDependencyCacheEntry *stored =
            &cache->dependencies[index];
        if (!dependencies[index].available) {
            return recovery_cache_error(
                diagnostics, PORPOISE_EXIT_IO, dependencies[index].path,
                "cannot cache an unavailable dependency");
        }
        stored->path.value = recovery_cache_strdup(dependencies[index].path);
        stored->path.resolved = recovery_cache_strdup(dependencies[index].path);
        stored->sha256 = recovery_cache_strdup(dependencies[index].sha256);
        stored->size = dependencies[index].size;
        stored->mtime_ns = dependencies[index].mtime_ns;
        if (stored->path.value == NULL || stored->path.resolved == NULL ||
            stored->sha256 == NULL) return PORPOISE_EXIT_INTERNAL;
    }
    return PORPOISE_EXIT_OK;
}

static int recovery_cache_match_compare(const void *left, const void *right) {
    const PorpoiseRecoveryMatchCacheEntry *a =
        (const PorpoiseRecoveryMatchCacheEntry *)left;
    const PorpoiseRecoveryMatchCacheEntry *b =
        (const PorpoiseRecoveryMatchCacheEntry *)right;
    int order = strcmp(a->module, b->module);
    if (order != 0) return order;
    if (a->address < b->address) return -1;
    if (a->address > b->address) return 1;
    if (a->size < b->size) return -1;
    if (a->size > b->size) return 1;
    order = strcmp(a->normalized_fingerprint, b->normalized_fingerprint);
    if (order != 0) return order;
    order = strcmp(a->canonical_identity, b->canonical_identity);
    if (order != 0) return order;
    if (a->contract_name == NULL || b->contract_name == NULL)
        return a->contract_name == b->contract_name
            ? 0 : (a->contract_name == NULL ? -1 : 1);
    return strcmp(a->contract_name, b->contract_name);
}

static void recovery_cache_match_free(
    PorpoiseRecoveryMatchCacheEntry *match) {
    free(match->module);
    free(match->normalized_fingerprint);
    free(match->canonical_identity);
    free(match->contract_name);
    memset(match, 0, sizeof(*match));
}

static int recovery_cache_append_match(
    PorpoiseRecoveryTargetCache *cache,
    size_t *capacity,
    const PorpoiseFunctionPlanView *view,
    const char *module) {
    PorpoiseRecoveryMatchCacheEntry *match;
    if (!porpoise_grow_array(
            (void **)&cache->matches, capacity, sizeof(*cache->matches),
            cache->match_count + 1U)) return PORPOISE_EXIT_INTERNAL;
    match = &cache->matches[cache->match_count];
    memset(match, 0, sizeof(*match));
    match->module = recovery_cache_strdup(module);
    match->address = view->function->start_address;
    match->size = view->function->size;
    match->normalized_fingerprint = recovery_cache_strdup(
        view->signature.digest_hex);
    match->canonical_identity = recovery_cache_strdup(
        view->sdk_entry->canonical_identity);
    match->contract_name = recovery_cache_strdup(
        view->sdk_entry->contract_name);
    if (match->module == NULL || match->normalized_fingerprint == NULL ||
        match->canonical_identity == NULL ||
        (view->sdk_entry->contract_name != NULL &&
         match->contract_name == NULL)) {
        recovery_cache_match_free(match);
        return PORPOISE_EXIT_INTERNAL;
    }
    cache->match_count++;
    return PORPOISE_EXIT_OK;
}

static int recovery_cache_store_matches(
    PorpoiseRecoveryTargetCache *cache,
    const PorpoiseTranslationPlan *plan,
    PorpoiseDiagnostics *diagnostics) {
    const char *module = porpoise_plan_module(plan);
    size_t capacity = 0U;
    size_t index;
    size_t write_index;

    if (module == NULL) module = "";
    for (index = 0U; index < porpoise_plan_function_count(plan); index++) {
        const PorpoiseFunctionPlanView *view =
            porpoise_plan_function_at(plan, index);
        int result;
        if (view == NULL || view->sdk_entry == NULL ||
            view->confidence != PORPOISE_MATCH_CONFIDENCE_EXACT ||
            (view->evidence_flags & PORPOISE_PLAN_EVIDENCE_SIGNATURE) == 0U ||
            !porpoise_signature_is_automatic_match_eligible(&view->signature)) {
            continue;
        }
        if (view->function == NULL || view->function->size == 0U ||
            view->sdk_entry->canonical_identity == NULL ||
            view->sdk_entry->canonical_identity[0] == '\0' ||
            !recovery_cache_hex_is_normalized(view->signature.digest_hex)) {
            return recovery_cache_error(
                diagnostics, PORPOISE_EXIT_INTERNAL, "",
                "validated plan exposes an invalid exact SDK match");
        }
        result = recovery_cache_append_match(cache, &capacity, view, module);
        if (result != PORPOISE_EXIT_OK) return result;
    }
    if (cache->match_count > 1U) {
        qsort(cache->matches, cache->match_count, sizeof(*cache->matches),
              recovery_cache_match_compare);
    }
    for (index = 0U; index < cache->match_count; index++) {
        size_t other;
        for (other = index + 1U; other < cache->match_count; other++) {
            if (recovery_cache_matches_conflict(
                    &cache->matches[index], &cache->matches[other])) {
                return recovery_cache_error(
                    diagnostics, PORPOISE_EXIT_USAGE, "",
                    "validated plan contains conflicting exact SDK identities");
            }
        }
    }
    write_index = 0U;
    for (index = 0U; index < cache->match_count; index++) {
        if (write_index != 0U && recovery_cache_match_equal(
                &cache->matches[write_index - 1U],
                &cache->matches[index])) {
            recovery_cache_match_free(&cache->matches[index]);
            continue;
        }
        if (write_index != index) {
            cache->matches[write_index] = cache->matches[index];
            memset(&cache->matches[index], 0, sizeof(cache->matches[index]));
        }
        write_index++;
    }
    cache->match_count = write_index;
    return PORPOISE_EXIT_OK;
}

int porpoise_recovery_target_cache_rebuild(
    PorpoiseRecoveryTarget *target,
    const PorpoiseRecoveryCacheInputs *inputs,
    const PorpoiseSession *session,
    const PorpoiseTranslationPlan *plan,
    const PorpoiseDtkImportMetadata *dtk_metadata,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseRecoveryCacheInputs defaults;
    PorpoiseRecoveryTargetCache replacement;
    RecoveryCacheDependencySnapshot *dependencies = NULL;
    size_t dependency_count = 0U;
    char input_sha256[PORPOISE_SHA256_HEX_SIZE];
    char settings_sha256[PORPOISE_SHA256_HEX_SIZE];
    const char *plan_target;
    bool input_available = false;
    int result;

    memset(&replacement, 0, sizeof(replacement));
    if (target == NULL || session == NULL || plan == NULL ||
        porpoise_plan_session(plan) != session) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_USAGE, "",
            "recovery-cache rebuild requires a coherent session and plan");
    }
    plan_target = porpoise_plan_target_id(plan);
    if (target->id == NULL || plan_target == NULL ||
        strcmp(target->id, plan_target) != 0) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_USAGE, "",
            "recovery-cache target does not match the validated plan");
    }
    if (!recovery_cache_dtk_metadata_valid(dtk_metadata)) {
        return recovery_cache_error(
            diagnostics, PORPOISE_EXIT_USAGE, "",
            "validated DTK import metadata is malformed or incompatible");
    }
    if (inputs == NULL) {
        porpoise_recovery_cache_inputs_init(&defaults);
        defaults.source_path = target->input.resolved;
        inputs = &defaults;
    }
    if (recovery_cache_cancelled(inputs->operation))
        return PORPOISE_EXIT_CANCELLED;
    result = porpoise_plan_validate(plan, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    porpoise_operation_progress(
        inputs->operation, PORPOISE_PHASE_VALIDATE, 0U, 3U,
        "measure recovery-cache inputs");
    result = recovery_cache_current_input_hash(
        target, inputs, dtk_metadata, false, input_sha256,
        &input_available, diagnostics);
    if (result != PORPOISE_EXIT_OK || !input_available) return result;
    recovery_cache_settings_hash(
        inputs->settings_identity, dtk_metadata, settings_sha256);
    result = recovery_cache_snapshot_dependencies(
        inputs, false, &dependencies, &dependency_count, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    if (recovery_cache_cancelled(inputs->operation)) {
        recovery_cache_dependency_snapshots_free(
            dependencies, dependency_count);
        return PORPOISE_EXIT_CANCELLED;
    }

    replacement.input_sha256 = recovery_cache_strdup(input_sha256);
    replacement.settings_sha256 = recovery_cache_strdup(settings_sha256);
    if (dtk_metadata != NULL && dtk_metadata->dtk_version[0] != '\0') {
        replacement.dtk_version = recovery_cache_strdup(
            dtk_metadata->dtk_version);
    }
    if (replacement.input_sha256 == NULL ||
        replacement.settings_sha256 == NULL ||
        (dtk_metadata != NULL && dtk_metadata->dtk_version[0] != '\0' &&
         replacement.dtk_version == NULL)) {
        result = PORPOISE_EXIT_INTERNAL;
        goto fail;
    }
    result = recovery_cache_store_dependencies(
        &replacement, dependencies, dependency_count, diagnostics);
    if (result != PORPOISE_EXIT_OK) goto fail;
    porpoise_operation_progress(
        inputs->operation, PORPOISE_PHASE_VALIDATE, 1U, 3U,
        "collect exact SDK cache matches");
    result = recovery_cache_store_matches(&replacement, plan, diagnostics);
    if (result != PORPOISE_EXIT_OK) goto fail;
    if (recovery_cache_cancelled(inputs->operation)) {
        result = PORPOISE_EXIT_CANCELLED;
        goto fail;
    }
    recovery_cache_dependency_snapshots_free(dependencies, dependency_count);
    porpoise_operation_progress(
        inputs->operation, PORPOISE_PHASE_VALIDATE, 2U, 3U,
        "replace target recovery cache");
    porpoise_recovery_target_cache_clear(&target->cache);
    target->cache = replacement;
    porpoise_operation_progress(
        inputs->operation, PORPOISE_PHASE_VALIDATE, 3U, 3U,
        "target recovery cache rebuilt");
    return PORPOISE_EXIT_OK;

fail:
    recovery_cache_dependency_snapshots_free(dependencies, dependency_count);
    porpoise_recovery_target_cache_clear(&replacement);
    return result;
}

const char *porpoise_recovery_cache_state_name(
    PorpoiseRecoveryCacheState state) {
    switch (state) {
        case PORPOISE_RECOVERY_CACHE_HIT: return "hit";
        case PORPOISE_RECOVERY_CACHE_MISS: return "miss";
        case PORPOISE_RECOVERY_CACHE_STALE: return "stale";
        case PORPOISE_RECOVERY_CACHE_INVALID: return "invalid";
        default: return "unknown";
    }
}
