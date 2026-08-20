#include "porpoise/recovery_runner.h"

#include "porpoise/recovery_annotation.h"
#include "porpoise/recovery_cache.h"
#include "porpoise/util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <process.h>
#define PORPOISE_RUNNER_GETPID() _getpid()
#else
#include <unistd.h>
#define PORPOISE_RUNNER_GETPID() getpid()
#endif

static int recovery_runner_error(
    PorpoiseDiagnostics *diagnostics,
    int result,
    const char *path,
    const char *message) {
    porpoise_diagnostics_add(
        diagnostics, PORPOISE_SEVERITY_ERROR, path, 0U, 0U,
        "%s", message);
    return result;
}

void porpoise_recovery_run_options_init(
    PorpoiseRecoveryRunOptions *options) {
    if (options != NULL) memset(options, 0, sizeof(*options));
}

void porpoise_recovery_run_result_init(
    PorpoiseRecoveryRunResult *result) {
    if (result != NULL) memset(result, 0, sizeof(*result));
}

void porpoise_recovery_run_result_free(
    PorpoiseRecoveryRunResult *result) {
    size_t index;
    if (result == NULL) return;
    for (index = 0U; index < result->target_count; index++) {
        PorpoiseRecoveryRunTarget *target = &result->targets[index];
        porpoise_staged_project_free(target->staged);
        porpoise_report_free(&target->report);
        porpoise_plan_free(target->plan);
        porpoise_session_close(target->session);
    }
    free(result->targets);
    memset(result, 0, sizeof(*result));
}

static uint32_t recovery_runner_name_hash(const char *text) {
    uint32_t hash = UINT32_C(2166136261);
    size_t index;
    for (index = 0U; text[index] != '\0'; index++) {
        hash ^= (unsigned char)text[index];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static bool recovery_runner_reject_output_overlap(
    const char *output,
    const char *dependency,
    const char *description,
    PorpoiseDiagnostics *diagnostics) {
    bool overlap = false;
    if (dependency == NULL || dependency[0] == '\0') return true;
    if (!porpoise_path_trees_overlap(output, dependency, &overlap)) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, output, 0U, 0U,
            "cannot compare output with %s safely", description);
        return false;
    }
    if (overlap) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, output, 0U, 0U,
            "output overlaps %s: %s", description, dependency);
        return false;
    }
    return true;
}

static int recovery_runner_validate_target_paths(
    const PorpoiseRecoveryProject *project,
    const PorpoiseRecoveryTarget *target,
    PorpoiseDiagnostics *diagnostics) {
    size_t index;
    bool output_contains_project = false;
    if (!porpoise_path_contains_path(
            target->output.resolved, project->path,
            &output_contains_project)) {
        return recovery_runner_error(
            diagnostics, PORPOISE_EXIT_IO, target->output.resolved,
            "cannot compare project and output paths safely");
    }
    if (output_contains_project) {
        return recovery_runner_error(
            diagnostics, PORPOISE_EXIT_USAGE, target->output.resolved,
            "output must not contain the Porpoise project file");
    }
    if (!recovery_runner_reject_output_overlap(
            target->output.resolved, target->input.resolved,
            "target input", diagnostics) ||
        (target->has_skip_list &&
         !recovery_runner_reject_output_overlap(
             target->output.resolved, target->skip_list.resolved,
             "skip list", diagnostics))) {
        return PORPOISE_EXIT_USAGE;
    }
    for (index = 0U; index < project->sdk_catalog_count; index++) {
        if (!recovery_runner_reject_output_overlap(
                target->output.resolved,
                project->sdk_catalogs[index].resolved,
                "SDK catalog", diagnostics)) {
            return PORPOISE_EXIT_USAGE;
        }
    }
    for (index = 0U; index < project->abi_contract_count; index++) {
        if (!recovery_runner_reject_output_overlap(
                target->output.resolved,
                project->abi_contracts[index].resolved,
                "ABI contract", diagnostics)) {
            return PORPOISE_EXIT_USAGE;
        }
    }
    for (index = 0U; index < target->symbol_source_count; index++) {
        const PorpoiseRecoverySymbolSource *source =
            &target->symbol_sources[index];
        if (!recovery_runner_reject_output_overlap(
                target->output.resolved, source->path.resolved,
                "symbol source", diagnostics) ||
            (source->has_auxiliary_path &&
             !recovery_runner_reject_output_overlap(
                 target->output.resolved,
                 source->auxiliary_path.resolved,
                 "symbol-source auxiliary file", diagnostics))) {
            return PORPOISE_EXIT_USAGE;
        }
    }
    return PORPOISE_EXIT_OK;
}

static bool recovery_runner_cache_path(
    const PorpoiseRecoveryProject *project,
    const PorpoiseRecoveryTarget *target,
    char *path,
    size_t capacity) {
    char target_component[PORPOISE_NAME_CAPACITY];
    char target_directory[PORPOISE_NAME_CAPACITY + 16U];
    char cache_root[PORPOISE_PATH_CAPACITY];
    char target_root[PORPOISE_PATH_CAPACITY];
    porpoise_sanitize_identifier(
        target->id, target_component, sizeof(target_component));
    if (!porpoise_format(
            target_directory, sizeof(target_directory), "%s-%08lX",
            target_component,
            (unsigned long)recovery_runner_name_hash(target->id)) ||
        !porpoise_path_join(
            cache_root, sizeof(cache_root), project->directory,
            ".porpoise-cache") ||
        !porpoise_path_join(
            target_root, sizeof(target_root), cache_root,
            target_directory) ||
        !porpoise_path_join(path, capacity, target_root, "dtk")) {
        return false;
    }
    return true;
}

static int recovery_runner_select_targets(
    const PorpoiseRecoveryProject *project,
    const PorpoiseRecoveryRunOptions *options,
    PorpoiseRecoveryRunResult *result,
    PorpoiseDiagnostics *diagnostics) {
    size_t count = 0U;
    size_t index;
    if (options->target_id_count != 0U && options->target_ids == NULL) {
        return recovery_runner_error(
            diagnostics, PORPOISE_EXIT_INTERNAL, project->path,
            "target selector array is inconsistent");
    }
    if (options->target_id_count == 0U) {
        for (index = 0U; index < project->target_count; index++) {
            if (project->targets[index].enabled) count++;
        }
    } else {
        count = options->target_id_count;
    }
    if (count == 0U) {
        return recovery_runner_error(
            diagnostics, PORPOISE_EXIT_USAGE, project->path,
            "project selection contains no targets");
    }
    if (count > SIZE_MAX / sizeof(*result->targets)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    result->targets = (PorpoiseRecoveryRunTarget *)calloc(
        count, sizeof(*result->targets));
    if (result->targets == NULL) return PORPOISE_EXIT_INTERNAL;
    result->target_count = count;
    for (index = 0U; index < result->target_count; index++) {
        porpoise_report_init(&result->targets[index].report);
        porpoise_dtk_import_result_init(
            &result->targets[index].import_result);
    }

    if (options->target_id_count == 0U) {
        size_t cursor = 0U;
        for (index = 0U; index < project->target_count; index++) {
            if (!project->targets[index].enabled) continue;
            result->targets[cursor++].target = &project->targets[index];
        }
    } else {
        for (index = 0U; index < options->target_id_count; index++) {
            const char *id = options->target_ids[index];
            const PorpoiseRecoveryTarget *target;
            size_t prior;
            if (id == NULL || id[0] == '\0') {
                return recovery_runner_error(
                    diagnostics, PORPOISE_EXIT_USAGE, project->path,
                    "target selector must not be empty");
            }
            for (prior = 0U; prior < index; prior++) {
                if (strcmp(options->target_ids[prior], id) == 0) {
                    porpoise_diagnostics_add(
                        diagnostics, PORPOISE_SEVERITY_ERROR,
                        project->path, 0U, 0U,
                        "target '%s' was selected more than once", id);
                    return PORPOISE_EXIT_USAGE;
                }
            }
            target = porpoise_recovery_project_find_target(project, id);
            if (target == NULL) {
                porpoise_diagnostics_add(
                    diagnostics, PORPOISE_SEVERITY_ERROR,
                    project->path, 0U, 0U,
                    "project has no target named '%s'", id);
                return PORPOISE_EXIT_USAGE;
            }
            result->targets[index].target = target;
        }
    }
    return PORPOISE_EXIT_OK;
}

static const char *recovery_runner_target_module(
    const PorpoiseRecoveryTarget *target) {
    if (target->symbol_source_count != 0U) {
        return target->symbol_sources[0].module == NULL
            ? "" : target->symbol_sources[0].module;
    }
    if (target->override_count != 0U) {
        return target->overrides[0].module == NULL
            ? "" : target->overrides[0].module;
    }
    return "";
}

static void recovery_runner_hash_u32(
    PorpoiseSha256Context *hash,
    uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
    porpoise_sha256_update(hash, bytes, sizeof(bytes));
}

static void recovery_runner_hash_string(
    PorpoiseSha256Context *hash,
    const char *value) {
    size_t length = value == NULL ? 0U : strlen(value);
    recovery_runner_hash_u32(hash, (uint32_t)length);
    if (length != 0U) porpoise_sha256_update(hash, value, length);
}

static void recovery_runner_settings_identity(
    const PorpoiseRecoveryProject *project,
    const PorpoiseRecoveryTarget *target,
    const PorpoiseTranslationPlan *plan,
    char identity[PORPOISE_SHA256_HEX_SIZE]) {
    PorpoiseSha256Context hash;
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    size_t index;
    static const char domain[] = "porpoise-recovery-target-settings-v1";
    porpoise_sha256_init(&hash);
    recovery_runner_hash_string(&hash, domain);
    recovery_runner_hash_u32(&hash, project->schema_version);
    recovery_runner_hash_string(&hash, target->id);
    recovery_runner_hash_u32(&hash, target->enabled ? 1U : 0U);
    recovery_runner_hash_u32(&hash, (uint32_t)target->source_kind);
    recovery_runner_hash_string(&hash, target->input.resolved);
    recovery_runner_hash_string(&hash, target->output.resolved);
    recovery_runner_hash_string(&hash, target->entry);
    recovery_runner_hash_u32(&hash, target->strict ? 1U : 0U);
    recovery_runner_hash_u32(&hash, (uint32_t)target->sdk_policy);
    recovery_runner_hash_u32(
        &hash, (uint32_t)project->sdk_catalog_count);
    for (index = 0U; index < project->sdk_catalog_count; index++) {
        recovery_runner_hash_string(
            &hash, project->sdk_catalogs[index].resolved);
    }
    recovery_runner_hash_u32(
        &hash, (uint32_t)project->abi_contract_count);
    for (index = 0U; index < project->abi_contract_count; index++) {
        recovery_runner_hash_string(
            &hash, project->abi_contracts[index].resolved);
    }
    recovery_runner_hash_u32(
        &hash, (uint32_t)target->symbol_source_count);
    for (index = 0U; index < target->symbol_source_count; index++) {
        const PorpoiseRecoverySymbolSource *source =
            &target->symbol_sources[index];
        recovery_runner_hash_u32(&hash, (uint32_t)source->kind);
        recovery_runner_hash_string(&hash, source->path.resolved);
        recovery_runner_hash_string(
            &hash, source->has_auxiliary_path
                ? source->auxiliary_path.resolved : NULL);
        recovery_runner_hash_string(&hash, source->module);
        recovery_runner_hash_u32(&hash, source->permissive ? 1U : 0U);
    }
    recovery_runner_hash_u32(&hash, target->has_skip_list ? 1U : 0U);
    recovery_runner_hash_string(
        &hash, target->has_skip_list
            ? target->skip_list.resolved : NULL);
    recovery_runner_hash_u32(&hash, (uint32_t)target->override_count);
    for (index = 0U; index < target->override_count; index++) {
        const PorpoiseRecoveryOverride *override = &target->overrides[index];
        recovery_runner_hash_string(&hash, override->target);
        recovery_runner_hash_string(&hash, override->module);
        recovery_runner_hash_u32(&hash, override->address);
        recovery_runner_hash_u32(&hash, override->size);
        recovery_runner_hash_string(
            &hash, override->normalized_fingerprint);
        recovery_runner_hash_u32(&hash, (uint32_t)override->action);
        recovery_runner_hash_string(&hash, override->contract_name);
        recovery_runner_hash_u32(
            &hash, override->acknowledge_conflict ? 1U : 0U);
    }
    recovery_runner_hash_u32(&hash, (uint32_t)target->annotation_count);
    for (index = 0U; index < target->annotation_count; index++) {
        const PorpoiseRecoveryAnnotation *annotation =
            &target->annotations[index];
        recovery_runner_hash_string(&hash, annotation->target);
        recovery_runner_hash_string(&hash, annotation->module);
        recovery_runner_hash_u32(&hash, annotation->address);
        recovery_runner_hash_u32(&hash, annotation->size);
        recovery_runner_hash_string(
            &hash, annotation->normalized_fingerprint);
        recovery_runner_hash_string(
            &hash, annotation->exact_bytes_sha256);
        recovery_runner_hash_u32(
            &hash, (uint32_t)annotation->interpretation);
        recovery_runner_hash_u32(&hash, annotation->element_count);
        recovery_runner_hash_string(&hash, annotation->encoding);
    }
    recovery_runner_hash_string(&hash, porpoise_plan_digest(plan));
    porpoise_sha256_final(&hash, digest);
    porpoise_sha256_hex(digest, identity);
}

static int recovery_runner_update_cache(
    PorpoiseRecoveryProject *project,
    PorpoiseRecoveryRunTarget *run_target,
    const PorpoiseRecoveryRunOptions *options,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseRecoveryTarget *target;
    PorpoiseRecoveryCacheDependencyInput *dependencies = NULL;
    PorpoiseRecoveryCacheInputs inputs;
    PorpoiseRecoveryCacheValidation validation;
    const PorpoiseDtkImportMetadata *metadata = NULL;
    char settings_identity[PORPOISE_SHA256_HEX_SIZE];
    size_t dependency_count = project->sdk_catalog_count +
                              project->abi_contract_count;
    size_t cursor = 0U;
    size_t index;
    int result;

    target = porpoise_recovery_project_find_target_mutable(
        project, run_target->target->id);
    if (target == NULL) return PORPOISE_EXIT_INTERNAL;
    if (target->has_skip_list) dependency_count++;
    for (index = 0U; index < target->symbol_source_count; index++) {
        dependency_count++;
        if (target->symbol_sources[index].has_auxiliary_path)
            dependency_count++;
    }
    if (dependency_count != 0U) {
        dependencies = (PorpoiseRecoveryCacheDependencyInput *)calloc(
            dependency_count, sizeof(*dependencies));
        if (dependencies == NULL) return PORPOISE_EXIT_INTERNAL;
    }
    for (index = 0U; index < project->sdk_catalog_count; index++) {
        dependencies[cursor++].path =
            project->sdk_catalogs[index].resolved;
    }
    for (index = 0U; index < project->abi_contract_count; index++) {
        dependencies[cursor++].path =
            project->abi_contracts[index].resolved;
    }
    if (target->has_skip_list) {
        dependencies[cursor++].path = target->skip_list.resolved;
    }
    for (index = 0U; index < target->symbol_source_count; index++) {
        const PorpoiseRecoverySymbolSource *source =
            &target->symbol_sources[index];
        dependencies[cursor++].path = source->path.resolved;
        if (source->has_auxiliary_path) {
            dependencies[cursor++].path = source->auxiliary_path.resolved;
        }
    }
    if (target->source_kind != PORPOISE_RECOVERY_SOURCE_ASSEMBLY) {
        metadata = &run_target->import_result.metadata;
    }
    recovery_runner_settings_identity(
        project, target, run_target->plan, settings_identity);
    porpoise_recovery_cache_inputs_init(&inputs);
    inputs.source_path = target->input.resolved;
    inputs.settings_identity = settings_identity;
    inputs.dependencies = dependencies;
    inputs.dependency_count = dependency_count;
    inputs.operation = options->operation;
    porpoise_recovery_cache_validation_init(&validation);
    result = porpoise_recovery_target_cache_validate(
        target, &inputs, metadata, &validation, diagnostics);
    if (result == PORPOISE_EXIT_OK &&
        validation.state != PORPOISE_RECOVERY_CACHE_HIT) {
        result = porpoise_recovery_target_cache_rebuild(
            target, &inputs, run_target->session, run_target->plan,
            metadata, diagnostics);
    }
    free(dependencies);
    return result;
}

static int recovery_runner_prepare_source(
    const PorpoiseRecoveryProject *project,
    PorpoiseRecoveryRunTarget *run_target,
    const PorpoiseRecoveryRunOptions *options,
    PorpoiseDiagnostics *diagnostics) {
    const PorpoiseRecoveryTarget *target = run_target->target;
    int result;
    if (target->source_kind == PORPOISE_RECOVERY_SOURCE_ASSEMBLY) {
        if (!porpoise_copy_string(
                run_target->assembly_path,
                sizeof(run_target->assembly_path),
                target->input.resolved)) {
            return PORPOISE_EXIT_INTERNAL;
        }
        return PORPOISE_EXIT_OK;
    }
    if (target->source_kind ==
        PORPOISE_RECOVERY_SOURCE_DTK_PREPARED_ASSEMBLY) {
        result = porpoise_dtk_validate_prepared(
            target->input.resolved, true, target->id,
            options->operation, &run_target->import_result,
            diagnostics);
    } else if (target->source_kind ==
               PORPOISE_RECOVERY_SOURCE_MANAGED_ELF) {
        PorpoiseDtkImportOptions import_options;
        char cache_path[PORPOISE_PATH_CAPACITY];
        char settings[PORPOISE_PATH_CAPACITY];
        if (!recovery_runner_cache_path(
                project, target, cache_path, sizeof(cache_path)) ||
            !porpoise_format(
                settings, sizeof(settings),
                "target=%s\nentry=%s\nstrict=%u\nsdk_policy=%s\n",
                target->id,
                target->entry == NULL ? "" : target->entry,
                target->strict ? 1U : 0U,
                porpoise_sdk_policy_name(target->sdk_policy))) {
            return PORPOISE_EXIT_INTERNAL;
        }
        porpoise_dtk_import_options_init(&import_options);
        import_options.source_kind = PORPOISE_DTK_SOURCE_MANAGED_ELF;
        import_options.input_path = target->input.resolved;
        import_options.cache_path = cache_path;
        import_options.dtk_path =
            options->dtk_path == NULL || options->dtk_path[0] == '\0'
                ? "dtk" : options->dtk_path;
        import_options.settings_identity = settings;
        import_options.allow_cache_reuse = true;
        import_options.operation = options->operation;
        result = porpoise_dtk_import_run(
            &import_options, &run_target->import_result, diagnostics);
    } else {
        return recovery_runner_error(
            diagnostics, PORPOISE_EXIT_USAGE, project->path,
            "target has an unsupported source kind");
    }
    if (result == PORPOISE_EXIT_OK &&
        !porpoise_copy_string(
            run_target->assembly_path,
            sizeof(run_target->assembly_path),
            run_target->import_result.validated_path)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    return result;
}

static int recovery_runner_build_plan(
    PorpoiseRecoveryProject *project,
    PorpoiseRecoveryRunTarget *run_target,
    const PorpoiseRecoveryRunOptions *options,
    PorpoiseDiagnostics *diagnostics) {
    const PorpoiseRecoveryTarget *target = run_target->target;
    PorpoiseSessionOpenOptions session_options;
    PorpoisePlanOptions plan_options;
    PorpoiseSessionSymbolSource *symbol_sources = NULL;
    PorpoiseFunctionOverride *overrides = NULL;
    const char **sdk_catalog_paths = NULL;
    const char **abi_paths = NULL;
    size_t index;
    int result = PORPOISE_EXIT_OK;

    if (target->symbol_source_count != 0U) {
        symbol_sources = (PorpoiseSessionSymbolSource *)calloc(
            target->symbol_source_count, sizeof(*symbol_sources));
        if (symbol_sources == NULL) return PORPOISE_EXIT_INTERNAL;
        for (index = 0U; index < target->symbol_source_count; index++) {
            const PorpoiseRecoverySymbolSource *source =
                &target->symbol_sources[index];
            symbol_sources[index].kind = source->kind;
            symbol_sources[index].path = source->path.resolved;
            symbol_sources[index].auxiliary_path =
                source->has_auxiliary_path
                    ? source->auxiliary_path.resolved : NULL;
            symbol_sources[index].module = source->module;
            symbol_sources[index].permissive = source->permissive;
        }
    }
    if (target->override_count != 0U) {
        overrides = (PorpoiseFunctionOverride *)calloc(
            target->override_count, sizeof(*overrides));
        if (overrides == NULL) {
            free(symbol_sources);
            return PORPOISE_EXIT_INTERNAL;
        }
        for (index = 0U; index < target->override_count; index++) {
            const PorpoiseRecoveryOverride *source =
                &target->overrides[index];
            overrides[index].module = source->module;
            overrides[index].address = source->address;
            overrides[index].size = source->size;
            overrides[index].normalized_fingerprint =
                source->normalized_fingerprint;
            overrides[index].action = source->action;
            overrides[index].contract_name = source->contract_name;
            overrides[index].acknowledge_conflict =
                source->acknowledge_conflict;
        }
    }
    if (project->sdk_catalog_count != 0U) {
        sdk_catalog_paths = (const char **)calloc(
            project->sdk_catalog_count, sizeof(*sdk_catalog_paths));
        if (sdk_catalog_paths == NULL) result = PORPOISE_EXIT_INTERNAL;
        for (index = 0U;
             result == PORPOISE_EXIT_OK &&
             index < project->sdk_catalog_count;
             index++) {
            sdk_catalog_paths[index] =
                project->sdk_catalogs[index].resolved;
        }
    }
    if (result == PORPOISE_EXIT_OK &&
        project->abi_contract_count != 0U) {
        abi_paths = (const char **)calloc(
            project->abi_contract_count, sizeof(*abi_paths));
        if (abi_paths == NULL) result = PORPOISE_EXIT_INTERNAL;
        for (index = 0U;
             result == PORPOISE_EXIT_OK &&
             index < project->abi_contract_count;
             index++) {
            abi_paths[index] = project->abi_contracts[index].resolved;
        }
    }

    if (result == PORPOISE_EXIT_OK) {
        porpoise_session_open_options_init(&session_options);
        session_options.input_path = run_target->assembly_path;
        session_options.abi_paths = abi_paths;
        session_options.abi_path_count = project->abi_contract_count;
        session_options.skip_list_path = target->has_skip_list
            ? target->skip_list.resolved : NULL;
        session_options.symbol_sources = symbol_sources;
        session_options.symbol_source_count = target->symbol_source_count;
        session_options.sdk_catalog_paths = sdk_catalog_paths;
        session_options.sdk_catalog_path_count =
            project->sdk_catalog_count;
        session_options.operation = options->operation;
        result = porpoise_session_open(
            &session_options, &run_target->session, diagnostics);
    }
    if (result == PORPOISE_EXIT_OK) {
        porpoise_plan_options_init(&plan_options);
        plan_options.entry_symbol = target->entry;
        plan_options.target_id = target->id;
        plan_options.module = recovery_runner_target_module(target);
        plan_options.sdk_policy = target->sdk_policy;
        plan_options.overrides = overrides;
        plan_options.override_count = target->override_count;
        plan_options.operation = options->operation;
        result = porpoise_plan_build(
            run_target->session, &plan_options,
            &run_target->plan, diagnostics);
    }
    if (result == PORPOISE_EXIT_OK) {
        result = porpoise_plan_validate(
            run_target->plan, diagnostics);
    }
    if (result == PORPOISE_EXIT_OK) {
        result = porpoise_recovery_annotations_validate(
            porpoise_session_program(run_target->session),
            target->annotations, target->annotation_count,
            target->id, recovery_runner_target_module(target),
            diagnostics);
    }
    if (result == PORPOISE_EXIT_OK) {
        result = recovery_runner_update_cache(
            project, run_target, options, diagnostics);
    }
    free(abi_paths);
    free(sdk_catalog_paths);
    free(overrides);
    free(symbol_sources);
    return result;
}

static bool recovery_runner_write_nullable(FILE *file, const char *value) {
    if (value == NULL) return fputs("null", file) >= 0;
    porpoise_json_write_string(file, value);
    return ferror(file) == 0;
}

static void recovery_runner_write_function(
    FILE *file,
    const PorpoiseFunctionPlanView *view) {
    const char *canonical_name = view->canonical_sdk_identity;
    if (canonical_name == NULL && view->map_symbol != NULL) {
        canonical_name = view->map_symbol->name;
    }
    if (canonical_name == NULL) canonical_name = view->function->name;
    fputs("{\"source_name\": ", file);
    porpoise_json_write_string(file, view->function->name);
    fputs(", \"canonical_name\": ", file);
    porpoise_json_write_string(file, canonical_name);
    fputs(", \"translation_unit\": ", file);
    porpoise_json_write_string(file, view->source->relative_path);
    fputs(", \"section\": ", file);
    recovery_runner_write_nullable(file, view->function->section);
    fprintf(
        file,
        ", \"address\": %lu, \"size\": %lu, \"signature\": ",
        (unsigned long)view->function->start_address,
        (unsigned long)view->function->size);
    porpoise_json_write_string(file, view->signature.digest_hex);
    fputs(", \"category\": ", file);
    if (view->has_sdk_category) {
        porpoise_json_write_string(
            file, porpoise_sdk_category_name(view->sdk_category));
    } else {
        fputs("null", file);
    }
    fputs(", \"confidence\": ", file);
    porpoise_json_write_string(
        file, porpoise_match_confidence_name(view->confidence));
    fputs(", \"requested_action\": ", file);
    porpoise_json_write_string(
        file, porpoise_plan_action_name(view->requested_action));
    fputs(", \"resolved_action\": ", file);
    porpoise_json_write_string(
        file, porpoise_plan_action_name(view->action));
    fputs(", \"binding\": ", file);
    recovery_runner_write_nullable(
        file, view->binding == NULL ? view->contract_name
                                    : view->binding->wrapper);
    fputs(", \"origin\": ", file);
    porpoise_json_write_string(
        file, porpoise_plan_origin_name(view->origin));
    fprintf(
        file,
        ", \"evidence_flags\": %u, \"conflict\": %s, "
        "\"overridden\": %s, \"override\": ",
        view->evidence_flags,
        (view->evidence_flags & PORPOISE_PLAN_EVIDENCE_CONFLICT) != 0U
            ? "true" : "false",
        view->overridden ? "true" : "false");
    porpoise_json_write_string(
        file, porpoise_override_action_name(view->override_action));
    fprintf(file, ", \"blocked\": %s, \"blocking_reason\": ",
            view->blocked ? "true" : "false");
    recovery_runner_write_nullable(file, view->blocking_reason);
    fputs(", \"map_provenance\": ", file);
    if (view->map_symbol == NULL) {
        fputs("null", file);
    } else {
        fputs("{\"path\": ", file);
        recovery_runner_write_nullable(
            file, view->map_symbol->provenance.path);
        fprintf(file, ", \"line\": %lu, \"module\": ",
                (unsigned long)view->map_symbol->provenance.line);
        recovery_runner_write_nullable(file, view->map_symbol->module);
        fputs(", \"object\": ", file);
        recovery_runner_write_nullable(file, view->map_symbol->object);
        fputs(", \"library\": ", file);
        recovery_runner_write_nullable(file, view->map_symbol->library);
        fputc('}', file);
    }
    fputs(", \"catalog_provenance\": ", file);
    if (view->sdk_entry == NULL) {
        fputs("null", file);
    } else {
        fputs("{\"path\": ", file);
        recovery_runner_write_nullable(
            file, view->sdk_entry->provenance.path);
        fprintf(file, ", \"line\": %lu}",
                (unsigned long)view->sdk_entry->provenance.line);
    }
    fputc('}', file);
}

static int recovery_runner_write_report_file(
    FILE *file,
    const PorpoiseRecoveryProject *project,
    const PorpoiseRecoveryRunResult *result,
    const PorpoiseDiagnostics *diagnostics) {
    size_t target_index;
    size_t diagnostic_index;
    fputs("{\n  \"schema_version\": 3,\n  \"project\": ", file);
    recovery_runner_write_nullable(file, project->path);
    fputs(",\n  \"targets\": [", file);
    for (target_index = 0U;
         target_index < result->target_count;
         target_index++) {
        const PorpoiseRecoveryRunTarget *run_target =
            &result->targets[target_index];
        const PorpoiseRecoveryTarget *target = run_target->target;
        size_t function_index;
        fputs(target_index == 0U ? "\n    {\"id\": "
                                 : ",\n    {\"id\": ", file);
        if (target == NULL) {
            fputs("null, \"planned\": false}", file);
            continue;
        }
        porpoise_json_write_string(file, target->id);
        fputs(", \"source_kind\": ", file);
        porpoise_json_write_string(
            file, porpoise_recovery_source_kind_name(target->source_kind));
        fputs(", \"input\": ", file);
        porpoise_json_write_string(file, target->input.resolved);
        fputs(", \"assembly_input\": ", file);
        recovery_runner_write_nullable(
            file, run_target->assembly_path[0] == '\0'
                      ? NULL : run_target->assembly_path);
        fputs(", \"output\": ", file);
        porpoise_json_write_string(file, target->output.resolved);
        fputs(", \"sdk_policy\": ", file);
        porpoise_json_write_string(
            file, porpoise_sdk_policy_name(target->sdk_policy));
        fputs(", \"plan_digest\": ", file);
        recovery_runner_write_nullable(
            file, run_target->plan == NULL
                      ? NULL : porpoise_plan_digest(run_target->plan));
        fprintf(
            file,
            ", \"generated\": %s, \"published\": %s, "
            "\"cache_hit\": %s, \"functions\": [",
            run_target->generated ? "true" : "false",
            run_target->published ? "true" : "false",
            run_target->import_result.cache_hit ? "true" : "false");
        for (function_index = 0U;
             run_target->plan != NULL &&
             function_index <
                 porpoise_plan_function_count(run_target->plan);
             function_index++) {
            fputs(function_index == 0U ? "\n        "
                                       : ",\n        ", file);
            recovery_runner_write_function(
                file, porpoise_plan_function_at(
                          run_target->plan, function_index));
        }
        fputs(run_target->plan != NULL &&
                      porpoise_plan_function_count(run_target->plan) != 0U
                  ? "\n      ]}" : "]}", file);
    }
    fputs(result->target_count == 0U
              ? "],\n  \"diagnostics\": ["
              : "\n  ],\n  \"diagnostics\": [", file);
    for (diagnostic_index = 0U;
         diagnostics != NULL && diagnostic_index < diagnostics->count;
         diagnostic_index++) {
        const PorpoiseDiagnostic *diagnostic =
            &diagnostics->items[diagnostic_index];
        fputs(diagnostic_index == 0U ? "\n    {\"severity\": "
                                     : ",\n    {\"severity\": ", file);
        porpoise_json_write_string(
            file,
            diagnostic->severity == PORPOISE_SEVERITY_ERROR
                ? "error"
                : diagnostic->severity == PORPOISE_SEVERITY_WARNING
                      ? "warning" : "info");
        fputs(", \"file\": ", file);
        recovery_runner_write_nullable(file, diagnostic->file);
        fprintf(
            file,
            ", \"line\": %lu, \"address\": %lu, \"message\": ",
            (unsigned long)diagnostic->line,
            (unsigned long)diagnostic->address);
        recovery_runner_write_nullable(file, diagnostic->message);
        fputc('}', file);
    }
    fputs(diagnostics != NULL && diagnostics->count != 0U
              ? "\n  ]\n}\n" : "]\n}\n", file);
    if (ferror(file) != 0 || fclose(file) != 0) return PORPOISE_EXIT_IO;
    return PORPOISE_EXIT_OK;
}

static bool recovery_runner_unique_sibling(
    const char *path,
    const char *tag,
    char *sibling,
    PorpoiseDiagnostics *diagnostics) {
    char parent[PORPOISE_PATH_CAPACITY];
    char base[PORPOISE_PATH_CAPACITY];
    unsigned long seed = (unsigned long)time(NULL) ^
                         (unsigned long)PORPOISE_RUNNER_GETPID();
    unsigned int attempt;
    if (!porpoise_path_parent(parent, sizeof(parent), path) ||
        !porpoise_path_basename(base, sizeof(base), path) ||
        !porpoise_make_directories(parent, diagnostics)) {
        return false;
    }
    for (attempt = 0U; attempt < 1000U; attempt++) {
        char name[PORPOISE_PATH_CAPACITY];
        if (!porpoise_format(
                name, sizeof(name), ".%s.porpoise-%s-%08lx-%u",
                base, tag, seed, attempt) ||
            !porpoise_path_join(
                sibling, PORPOISE_PATH_CAPACITY, parent, name)) {
            return false;
        }
        if (!porpoise_path_exists(sibling)) return true;
    }
    return false;
}

int porpoise_recovery_run_write_report(
    const PorpoiseRecoveryProject *project,
    const PorpoiseRecoveryRunResult *result,
    const char *path,
    const PorpoiseDiagnostics *diagnostics) {
    PorpoiseDiagnostics write_diagnostics;
    char temporary[PORPOISE_PATH_CAPACITY];
    char backup[PORPOISE_PATH_CAPACITY];
    bool had_output;
    FILE *file;
    int write_result;
    if (project == NULL || result == NULL || path == NULL || path[0] == '\0') {
        return PORPOISE_EXIT_INTERNAL;
    }
    porpoise_diagnostics_init(&write_diagnostics);
    if (!recovery_runner_unique_sibling(
            path, "report", temporary, &write_diagnostics)) {
        porpoise_diagnostics_free(&write_diagnostics);
        return PORPOISE_EXIT_IO;
    }
    file = fopen(temporary, "wb");
    if (file == NULL) {
        porpoise_diagnostics_free(&write_diagnostics);
        return PORPOISE_EXIT_IO;
    }
    write_result = recovery_runner_write_report_file(
        file, project, result, diagnostics);
    if (write_result != PORPOISE_EXIT_OK) {
        (void)remove(temporary);
        porpoise_diagnostics_free(&write_diagnostics);
        return write_result;
    }
    had_output = porpoise_path_exists(path);
    if (had_output &&
        (!recovery_runner_unique_sibling(
             path, "report-backup", backup, &write_diagnostics) ||
         !porpoise_move_path(path, backup, &write_diagnostics))) {
        (void)remove(temporary);
        porpoise_diagnostics_free(&write_diagnostics);
        return PORPOISE_EXIT_IO;
    }
    if (!porpoise_move_path(temporary, path, &write_diagnostics)) {
        if (had_output) {
            (void)porpoise_move_path(
                backup, path, &write_diagnostics);
        }
        porpoise_diagnostics_free(&write_diagnostics);
        return PORPOISE_EXIT_IO;
    }
    if (had_output) (void)porpoise_remove_tree(backup, &write_diagnostics);
    porpoise_diagnostics_free(&write_diagnostics);
    return PORPOISE_EXIT_OK;
}

int porpoise_recovery_project_run(
    PorpoiseRecoveryProject *project,
    const PorpoiseRecoveryRunOptions *options,
    PorpoiseRecoveryRunResult *result,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseRecoveryRunResult candidate;
    PorpoiseStagedProject **staged = NULL;
    size_t index;
    int run_result;
    if (project == NULL || options == NULL || result == NULL ||
        diagnostics == NULL) {
        return PORPOISE_EXIT_INTERNAL;
    }
    if (result->targets != NULL || result->target_count != 0U) {
        return recovery_runner_error(
            diagnostics, PORPOISE_EXIT_INTERNAL, project->path,
            "recovery run result must be initialized and empty");
    }
    porpoise_recovery_run_result_init(&candidate);
    run_result = recovery_runner_select_targets(
        project, options, &candidate, diagnostics);
    if (run_result != PORPOISE_EXIT_OK) goto finished;

    for (index = 0U; index < candidate.target_count; index++) {
        run_result = recovery_runner_validate_target_paths(
            project, candidate.targets[index].target, diagnostics);
        if (run_result != PORPOISE_EXIT_OK) goto finished;
        run_result = recovery_runner_prepare_source(
            project, &candidate.targets[index], options, diagnostics);
        if (run_result != PORPOISE_EXIT_OK) goto finished;
        run_result = recovery_runner_build_plan(
            project, &candidate.targets[index], options, diagnostics);
        if (run_result != PORPOISE_EXIT_OK) goto finished;
    }
    if (options->analyze_only) {
        run_result = PORPOISE_EXIT_OK;
        goto finished;
    }
    if (options->runtime_directory == NULL ||
        options->runtime_directory[0] == '\0') {
        run_result = recovery_runner_error(
            diagnostics, PORPOISE_EXIT_INTERNAL, project->path,
            "runtime directory is required for generation");
        goto finished;
    }
    staged = (PorpoiseStagedProject **)calloc(
        candidate.target_count, sizeof(*staged));
    if (staged == NULL) {
        run_result = PORPOISE_EXIT_INTERNAL;
        goto finished;
    }
    for (index = 0U; index < candidate.target_count; index++) {
        PorpoiseRecoveryRunTarget *target = &candidate.targets[index];
        PorpoiseProjectOptions project_options;
        porpoise_project_options_init(&project_options);
        project_options.output_path = target->target->output.resolved;
        project_options.runtime_directory = options->runtime_directory;
        project_options.entry_symbol = target->target->entry;
        project_options.force = options->force;
        project_options.strict = target->target->strict;
        project_options.operation = options->operation;
        run_result = porpoise_project_stage_plan(
            target->plan, &project_options, &target->report,
            &target->staged, diagnostics);
        if (run_result != PORPOISE_EXIT_OK) goto finished;
        target->generated = true;
        staged[index] = target->staged;
    }
    run_result = porpoise_project_publish_batch(
        staged, candidate.target_count, diagnostics);
    if (run_result == PORPOISE_EXIT_OK) {
        for (index = 0U; index < candidate.target_count; index++) {
            candidate.targets[index].published = true;
        }
    }

finished:
    free(staged);
    *result = candidate;
    if (options->report_path != NULL && options->report_path[0] != '\0') {
        int report_result = porpoise_recovery_run_write_report(
            project, result, options->report_path, diagnostics);
        if (run_result == PORPOISE_EXIT_OK &&
            report_result != PORPOISE_EXIT_OK) {
            run_result = report_result;
        }
    }
    return run_result;
}
