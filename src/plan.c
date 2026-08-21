#include "plan_internal.h"

#include "porpoise/sdk_contract.h"
#include "porpoise/util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const BLOCK_CONFLICT =
    "map and exact signature evidence conflict";
static const char *const BLOCK_OVERRIDE_CONFLICT =
    "manual action must explicitly acknowledge conflicting evidence";
static const char *const BLOCK_IMPORT_CONTRACT =
    "selected import has no valid host contract";
static const char *const BLOCK_INPUT_DATA =
    "an assembly data region cannot be reclassified as code";
static const char *const BLOCK_SKIPPED_LIFT =
    "a skip-list function cannot be lifted by an override";

void porpoise_plan_options_init(PorpoisePlanOptions *options) {
    if (options == NULL) return;
    memset(options, 0, sizeof(*options));
    options->sdk_policy = PORPOISE_SDK_POLICY_KEEP;
}

static bool sdk_policy_valid(PorpoiseSdkPolicy policy) {
    return policy == PORPOISE_SDK_POLICY_KEEP ||
           policy == PORPOISE_SDK_POLICY_IMPORTED ||
           policy == PORPOISE_SDK_POLICY_OMIT;
}

static bool override_action_valid(PorpoiseOverrideAction action) {
    return action == PORPOISE_OVERRIDE_AUTO ||
           action == PORPOISE_OVERRIDE_LIFT ||
           action == PORPOISE_OVERRIDE_IMPORT ||
           action == PORPOISE_OVERRIDE_OMIT ||
           action == PORPOISE_OVERRIDE_TREAT_AS_DATA;
}

static int plan_error(
    PorpoiseDiagnostics *diagnostics,
    int result,
    const char *message) {
    if (diagnostics != NULL) {
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            NULL,
            0U,
            0U,
            "%s",
            message);
    }
    return result;
}

static int plan_cancelled(PorpoiseDiagnostics *diagnostics) {
    porpoise_diagnostics_add(
        diagnostics,
        PORPOISE_SEVERITY_INFO,
        NULL,
        0U,
        0U,
        "translation planning was cancelled");
    return PORPOISE_EXIT_CANCELLED;
}

static const PorpoiseImportBinding *find_import_binding(
    const PorpoiseAnalysis *analysis,
    const PorpoiseFunction *function) {
    size_t index;
    for (index = 0U; index < analysis->import_binding_count; index++) {
        if (analysis->import_bindings[index].owner == function) {
            return &analysis->import_bindings[index];
        }
    }
    return NULL;
}

static bool count_program_functions(
    const PorpoiseProgram *program,
    size_t *count_out) {
    size_t file_index;
    size_t count = 0U;
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        size_t file_count = program->files[file_index].function_count;
        if (file_count > SIZE_MAX - count) return false;
        count += file_count;
    }
    *count_out = count;
    return true;
}

static void free_abi_value(PorpoiseAbiValue *value) {
    if (value == NULL) return;
    free(value->name);
    memset(value, 0, sizeof(*value));
}

static void free_abi_function(PorpoiseAbiFunction *function) {
    size_t index;
    if (function == NULL) return;
    free(function->symbol);
    free(function->wrapper);
    free(function->header);
    free(function->adapter);
    free_abi_value(&function->result);
    for (index = 0U; index < function->argument_count; index++) {
        free_abi_value(&function->arguments[index]);
    }
    free(function->arguments);
    memset(function, 0, sizeof(*function));
}

static bool clone_optional_string(char **output, const char *value) {
    *output = value == NULL ? NULL : porpoise_strdup(value);
    return value == NULL || *output != NULL;
}

static bool clone_abi_value(
    PorpoiseAbiValue *output,
    const PorpoiseAbiValue *input) {
    memset(output, 0, sizeof(*output));
    output->type = input->type;
    output->register_class = input->register_class;
    output->register_index = input->register_index;
    return clone_optional_string(&output->name, input->name);
}

static bool clone_abi_function(
    PorpoiseAbiFunction *output,
    const PorpoiseAbiFunction *input) {
    size_t index;
    memset(output, 0, sizeof(*output));
    output->kind = input->kind;
    if (!clone_optional_string(&output->symbol, input->symbol) ||
        !clone_optional_string(&output->wrapper, input->wrapper) ||
        !clone_optional_string(&output->header, input->header) ||
        !clone_optional_string(&output->adapter, input->adapter) ||
        !clone_abi_value(&output->result, &input->result)) {
        free_abi_function(output);
        return false;
    }
    if (input->argument_count != 0U) {
        if (input->argument_count > SIZE_MAX / sizeof(*output->arguments)) {
            free_abi_function(output);
            return false;
        }
        output->arguments = (PorpoiseAbiValue *)calloc(
            input->argument_count, sizeof(*output->arguments));
        if (output->arguments == NULL) {
            free_abi_function(output);
            return false;
        }
    }
    for (index = 0U; index < input->argument_count; index++) {
        if (!clone_abi_value(
                &output->arguments[index], &input->arguments[index])) {
            output->argument_count = index + 1U;
            free_abi_function(output);
            return false;
        }
        output->argument_count++;
    }
    return true;
}

static bool initialize_effective_abi(
    PorpoiseTranslationPlan *plan,
    const PorpoiseAbiManifest *source) {
    size_t index;
    size_t capacity;
    porpoise_abi_init(&plan->effective_abi);
    if (source->function_count > SIZE_MAX - plan->function_count) {
        return false;
    }
    capacity = source->function_count + plan->function_count;
    if (capacity != 0U) {
        plan->effective_abi.functions = (PorpoiseAbiFunction *)calloc(
            capacity, sizeof(*plan->effective_abi.functions));
        if (plan->effective_abi.functions == NULL) return false;
    }
    plan->effective_abi_capacity = capacity;
    for (index = 0U; index < source->function_count; index++) {
        PorpoiseAbiFunction *target =
            &plan->effective_abi.functions[plan->effective_abi.function_count];
        if (!clone_abi_function(target, &source->functions[index])) {
            return false;
        }
        plan->effective_abi.function_count++;
    }
    return true;
}

static const PorpoiseAbiFunction *find_effective_import(
    const PorpoiseTranslationPlan *plan,
    const char *symbol) {
    return porpoise_abi_find_import(&plan->effective_abi, symbol);
}

static const PorpoiseAbiFunction *find_effective_direct_contract(
    const PorpoiseTranslationPlan *plan,
    const char *contract_name) {
    const PorpoiseAbiFunction *function =
        find_effective_import(plan, contract_name);
    return function != NULL && function->adapter == NULL
               ? function
               : NULL;
}

static const PorpoiseAbiFunction *append_contract_import(
    PorpoiseTranslationPlan *plan,
    const PorpoiseSdkContract *contract,
    const char *symbol) {
    PorpoiseAbiFunction *function;
    const PorpoiseSdkAbiValue *sdk_value;
    const PorpoiseAbiFunction *existing;
    size_t index;
    existing = find_effective_import(plan, symbol);
    if (existing != NULL) {
        return porpoise_sdk_contract_binding_matches(contract, existing)
                   ? existing
                   : NULL;
    }
    if (contract == NULL || symbol == NULL || symbol[0] == '\0' ||
        plan->effective_abi.function_count >= plan->effective_abi_capacity) {
        return NULL;
    }
    function = &plan->effective_abi.functions[
        plan->effective_abi.function_count];
    memset(function, 0, sizeof(*function));
    function->kind = PORPOISE_ABI_IMPORT;
    function->symbol = porpoise_strdup(symbol);
    function->wrapper = porpoise_strdup(symbol);
    function->header = porpoise_strdup(
        porpoise_sdk_contract_host_header(contract));
    if (porpoise_sdk_contract_host_binding_kind(contract) ==
        PORPOISE_SDK_HOST_BINDING_SPECIALIZED_ADAPTER) {
        function->adapter = porpoise_strdup(
            porpoise_sdk_contract_host_callable(contract));
    } else if (porpoise_sdk_contract_host_binding_kind(contract) ==
               PORPOISE_SDK_HOST_BINDING_DIRECT_CALL) {
        free(function->wrapper);
        function->wrapper = porpoise_strdup(
            porpoise_sdk_contract_host_callable(contract));
    }
    if (function->symbol == NULL || function->wrapper == NULL ||
        (porpoise_sdk_contract_host_header(contract) != NULL &&
         function->header == NULL) ||
        (porpoise_sdk_contract_host_binding_kind(contract) ==
             PORPOISE_SDK_HOST_BINDING_SPECIALIZED_ADAPTER &&
         function->adapter == NULL)) {
        free_abi_function(function);
        return NULL;
    }
    sdk_value = porpoise_sdk_contract_result(contract);
    if (sdk_value == NULL) {
        free_abi_function(function);
        return NULL;
    }
    function->result.type = sdk_value->type;
    function->result.register_class = sdk_value->register_class;
    function->result.register_index = sdk_value->register_index;
    function->argument_count = porpoise_sdk_contract_argument_count(contract);
    if (function->argument_count != 0U) {
        function->arguments = (PorpoiseAbiValue *)calloc(
            function->argument_count, sizeof(*function->arguments));
        if (function->arguments == NULL) {
            free_abi_function(function);
            return NULL;
        }
    }
    for (index = 0U; index < function->argument_count; index++) {
        char name[32];
        sdk_value = porpoise_sdk_contract_argument_at(contract, index);
        if (sdk_value == NULL ||
            !porpoise_format(
                name, sizeof(name), "argument_%lu", (unsigned long)index)) {
            free_abi_function(function);
            return NULL;
        }
        function->arguments[index].type = sdk_value->type;
        function->arguments[index].register_class = sdk_value->register_class;
        function->arguments[index].register_index = sdk_value->register_index;
        function->arguments[index].name = porpoise_strdup(name);
        if (function->arguments[index].name == NULL) {
            free_abi_function(function);
            return NULL;
        }
    }
    plan->effective_abi.function_count++;
    return function;
}

static int ascii_case_compare(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        int a = tolower((unsigned char)*left++);
        int b = tolower((unsigned char)*right++);
        if (a != b) return a - b;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

static bool string_in_list(
    const char *value,
    const char *const *values,
    size_t count) {
    size_t index;
    if (value == NULL) return false;
    for (index = 0U; index < count; index++) {
        if (ascii_case_compare(value, values[index]) == 0) return true;
    }
    return false;
}

static const char *ownership_archive_basename(const char *library) {
    const char *basename = library;
    const char *cursor;
    if (library == NULL) return NULL;
    for (cursor = library; *cursor != '\0'; cursor++) {
        if (*cursor == '/' || *cursor == '\\') basename = cursor + 1U;
    }
    return basename;
}

static bool map_library_category(
    const char *library,
    PorpoiseSdkCategory *category_out) {
    static const char *const nintendo[] = {
        "ai.a", "base.a", "dvd.a", "exi.a", "gx.a", "mtx.a",
        "os.a", "pad.a", "si.a", "vi.a", "ar.a", "card.a",
        "dsp.a", "thp.a", "db.a"
    };
    static const char *const crt_msl[] = {
        "MSL_C.PPCEABI.bare.H.a", "MSL_C.PPCEABI.H.a"
    };
    static const char *const runtime[] = {
        "Runtime.PPCEABI.H.a", "Runtime.PPCEABI.a"
    };
    static const char *const trk[] = {
        "TRK_MINNOW_DOLPHIN.a", "MetroTRK.a"
    };
    static const char *const stubs[] = {"amcstubs.a", "odemustubs.a"};
    library = ownership_archive_basename(library);
    if (library == NULL || library[0] == '\0' || category_out == NULL) {
        return false;
    }
    if (ascii_case_compare(library, "demo.a") == 0) {
        *category_out = PORPOISE_SDK_CATEGORY_DEMO;
    } else if (string_in_list(
                   library, nintendo,
                   sizeof(nintendo) / sizeof(nintendo[0]))) {
        *category_out = PORPOISE_SDK_CATEGORY_NINTENDO_DOLPHIN;
    } else if (string_in_list(
                   library, crt_msl, sizeof(crt_msl) / sizeof(crt_msl[0]))) {
        *category_out = PORPOISE_SDK_CATEGORY_CRT_MSL;
    } else if (string_in_list(
                   library, runtime,
                   sizeof(runtime) / sizeof(runtime[0]))) {
        *category_out = PORPOISE_SDK_CATEGORY_RUNTIME;
    } else if (string_in_list(
                   library, trk, sizeof(trk) / sizeof(trk[0]))) {
        *category_out = PORPOISE_SDK_CATEGORY_METROTRK;
    } else if (string_in_list(
                   library, stubs, sizeof(stubs) / sizeof(stubs[0]))) {
        *category_out = PORPOISE_SDK_CATEGORY_STUB;
    } else {
        return false;
    }
    return true;
}

static bool optional_string_equal(const char *left, const char *right) {
    if (left == NULL || left[0] == '\0') left = NULL;
    if (right == NULL || right[0] == '\0') right = NULL;
    return left == right ||
           (left != NULL && right != NULL && strcmp(left, right) == 0);
}

static bool symbol_matches_function_address(
    const PorpoiseSymbol *symbol,
    const char *module,
    const PorpoiseFunction *function) {
    return symbol->used && symbol->has_address &&
           symbol->address == function->start_address &&
           optional_string_equal(symbol->section, function->section) &&
           (module == NULL || module[0] == '\0' ||
            optional_string_equal(symbol->module, module)) &&
           (symbol->kind == PORPOISE_SYMBOL_KIND_FUNCTION ||
            symbol->kind == PORPOISE_SYMBOL_KIND_UNKNOWN ||
            symbol->kind == PORPOISE_SYMBOL_KIND_LABEL);
}

static bool map_symbol_matches_canonical_identity(
    const PorpoiseSymbol *symbol,
    const char *identity) {
    const char *archive;
    const char *first_separator;
    const char *last_separator;
    const char *cursor;
    size_t archive_length;
    size_t identity_archive_length;
    size_t object_length;
    size_t identity_object_length;
    if (symbol == NULL || identity == NULL) return false;
    if (strcmp(symbol->name, identity) == 0) return true;
    if (symbol->library == NULL || symbol->object == NULL) return false;
    archive = ownership_archive_basename(symbol->library);
    if (archive == NULL || archive[0] == '\0') return false;
    first_separator = strchr(identity, '/');
    last_separator = strrchr(identity, '/');
    if (first_separator == NULL || last_separator == first_separator) {
        return false;
    }
    if (strcmp(last_separator + 1U, symbol->name) != 0) return false;
    archive_length = strlen(archive);
    identity_archive_length = (size_t)(first_separator - identity);
    if (archive_length != identity_archive_length) return false;
    for (cursor = archive; *cursor != '\0'; cursor++) {
        size_t offset = (size_t)(cursor - archive);
        if (tolower((unsigned char)*cursor) !=
            tolower((unsigned char)identity[offset])) {
            return false;
        }
    }
    object_length = strlen(symbol->object);
    identity_object_length =
        (size_t)(last_separator - first_separator - 1U);
    if (object_length != identity_object_length) return false;
    for (cursor = symbol->object; *cursor != '\0'; cursor++) {
        size_t offset = (size_t)(cursor - symbol->object);
        char map_character = *cursor == '\\' ? '/' : *cursor;
        if (map_character != first_separator[1U + offset]) return false;
    }
    return true;
}

static bool assign_map_canonical_identity(
    PorpoiseTranslationPlan *plan,
    PorpoiseFunctionPlanView *view) {
    const PorpoiseSymbol *symbol = view->map_symbol;
    const char *archive;
    size_t archive_length;
    size_t object_length;
    size_t name_length;
    size_t total_length;
    size_t index;
    char *identity;
    char *cursor;
    const char *source;
    if (symbol == NULL || symbol->library == NULL ||
        symbol->object == NULL) {
        view->canonical_sdk_identity =
            symbol == NULL ? NULL : symbol->name;
        return true;
    }
    archive = ownership_archive_basename(symbol->library);
    archive_length = strlen(archive);
    object_length = strlen(symbol->object);
    name_length = strlen(symbol->name);
    if (archive_length > SIZE_MAX - object_length ||
        archive_length + object_length > SIZE_MAX - name_length ||
        archive_length + object_length + name_length > SIZE_MAX - 3U) {
        return false;
    }
    total_length = archive_length + object_length + name_length + 3U;
    identity = (char *)malloc(total_length);
    if (identity == NULL) return false;
    cursor = identity;
    memcpy(cursor, archive, archive_length);
    cursor += archive_length;
    *cursor++ = '/';
    for (source = symbol->object; *source != '\0'; source++) {
        *cursor++ = *source == '\\' ? '/' : *source;
    }
    *cursor++ = '/';
    memcpy(cursor, symbol->name, name_length + 1U);
    index = (size_t)(view - plan->functions);
    if (index >= plan->function_count ||
        plan->owned_sdk_identities[index] != NULL) {
        free(identity);
        return false;
    }
    plan->owned_sdk_identities[index] = identity;
    view->canonical_sdk_identity = identity;
    return true;
}

static const PorpoiseSymbol *select_map_symbol(
    const PorpoiseSymbolCatalog *catalog,
    const char *module,
    const PorpoiseFunction *function,
    const PorpoiseSdkCatalogEntry *sdk_entry,
    bool *has_map_evidence,
    bool *canonical_name_seen) {
    const PorpoiseSymbol *best = NULL;
    unsigned int best_score = 0U;
    size_t index;
    *has_map_evidence = false;
    *canonical_name_seen = false;
    if (catalog == NULL) return NULL;
    for (index = 0U; index < catalog->symbol_count; index++) {
        const PorpoiseSymbol *symbol = &catalog->symbols[index];
        unsigned int score = 1U;
        if (!symbol_matches_function_address(symbol, module, function)) {
            continue;
        }
        *has_map_evidence = true;
        if (sdk_entry != NULL &&
            map_symbol_matches_canonical_identity(
                symbol, sdk_entry->canonical_identity)) {
            *canonical_name_seen = true;
            score += 100U;
        }
        if (strcmp(symbol->name, function->name) == 0) score += 80U;
        if (symbol->kind == PORPOISE_SYMBOL_KIND_FUNCTION) score += 20U;
        if (symbol->has_size && symbol->size == function->size) score += 10U;
        if (symbol->library != NULL) score += 5U;
        if (best == NULL || score > best_score) {
            best = symbol;
            best_score = score;
        }
    }
    return best;
}

static const char *contract_name_for_entry(
    const PorpoiseSdkCatalogEntry *entry,
    const char *override_name) {
    const char *name = override_name;
    if ((name == NULL || name[0] == '\0') && entry != NULL) {
        name = entry->contract_name != NULL
                   ? entry->contract_name
                   : entry->canonical_identity;
    }
    return name;
}

static const PorpoiseSdkContract *builtin_contract_for_entry(
    const PorpoiseSdkCatalogEntry *entry,
    const char *override_name) {
    return porpoise_sdk_contract_find_by_canonical_name(
        contract_name_for_entry(entry, override_name));
}

static void populate_legacy_action(
    PorpoiseFunctionPlanView *view,
    const PorpoiseSourceFile *source,
    const PorpoiseFunction *function,
    const PorpoiseAnalysis *analysis,
    const PorpoiseTranslationPlan *plan) {
    const PorpoiseImportBinding *binding =
        find_import_binding(analysis, function);
    memset(view, 0, sizeof(*view));
    view->source = source;
    view->function = function;
    view->binding_address = function->start_address;
    view->override_action = PORPOISE_OVERRIDE_AUTO;
    if (function->data_region) {
        view->action = PORPOISE_PLAN_ACTION_DATA;
        view->origin = PORPOISE_PLAN_ORIGIN_INPUT_DATA;
    } else if (!function->skipped) {
        view->action = PORPOISE_PLAN_ACTION_LIFT;
        view->origin = PORPOISE_PLAN_ORIGIN_DEFAULT;
    } else if (binding != NULL) {
        view->action = PORPOISE_PLAN_ACTION_IMPORT;
        view->origin = PORPOISE_PLAN_ORIGIN_ABI_IMPORT;
        view->binding = find_effective_import(plan, binding->import->symbol);
        view->binding_alias = binding->alias;
        view->binding_address = binding->guest_address;
        view->contract_name = binding->import->symbol;
    } else {
        view->action = PORPOISE_PLAN_ACTION_OMIT;
        view->origin = PORPOISE_PLAN_ORIGIN_SKIP_LIST;
    }
    view->requested_action = view->action;
}

static void mark_view_blocked(
    PorpoiseTranslationPlan *plan,
    PorpoiseFunctionPlanView *view,
    const char *reason) {
    view->blocked = true;
    view->blocking_reason = reason;
    plan->blocked = true;
    if (plan->blocking_reason == NULL) plan->blocking_reason = reason;
}

static const char *normalized_hint_identity(const char *value) {
    return value == NULL ? "" : value;
}

static bool exact_optional_string_equal(
    const char *left,
    const char *right) {
    if (left == NULL || right == NULL) return left == right;
    return strcmp(left, right) == 0;
}

static bool lookup_verified_match_hint(
    const PorpoiseSdkCatalog *catalog,
    const PorpoisePlanOptions *options,
    const PorpoiseFunctionPlanView *view,
    PorpoiseSdkCatalogMatch *match_out) {
    size_t index;
    if (catalog == NULL || options == NULL || view == NULL ||
        match_out == NULL || view->function == NULL ||
        !porpoise_signature_is_automatic_match_eligible(&view->signature)) {
        return false;
    }
    for (index = 0U; index < options->match_hint_count; index++) {
        const PorpoisePlanMatchHint *hint = &options->match_hints[index];
        PorpoiseSdkCatalogMatch match;
        if (hint->target_id == NULL || hint->module == NULL ||
            hint->normalized_fingerprint == NULL ||
            hint->canonical_identity == NULL ||
            strcmp(hint->target_id,
                   normalized_hint_identity(options->target_id)) != 0 ||
            strcmp(hint->module,
                   normalized_hint_identity(options->module)) != 0 ||
            hint->address != view->function->start_address ||
            hint->size != view->function->size ||
            strcmp(hint->normalized_fingerprint,
                   view->signature.digest_hex) != 0) {
            continue;
        }
        match = porpoise_sdk_catalog_lookup_identity_exact(
            catalog, hint->canonical_identity, &view->signature);
        if (match.status != PORPOISE_SDK_CATALOG_MATCH_UNIQUE ||
            match.entry == NULL ||
            !exact_optional_string_equal(
                match.entry->contract_name, hint->contract_name)) {
            continue;
        }
        *match_out = match;
        return true;
    }
    return false;
}

static bool populate_sdk_evidence(
    PorpoiseTranslationPlan *plan,
    PorpoiseFunctionPlanView *view,
    const PorpoisePlanOptions *options,
    PorpoiseDiagnostics *diagnostics) {
    const PorpoiseSdkCatalog *sdk_catalog =
        porpoise_session_sdk_catalog(plan->session);
    const PorpoiseSymbolCatalog *symbols =
        porpoise_session_symbols(plan->session);
    PorpoiseSdkCatalogMatch match;
    const PorpoiseSdkContract *contract = NULL;
    const PorpoiseAbiFunction *direct_contract = NULL;
    const char *contract_name = NULL;
    bool match_hint_used;
    bool signature_eligible;
    bool has_map_evidence;
    bool canonical_name_seen;
    bool conflict = false;
    PorpoiseSdkCategory map_category = PORPOISE_SDK_CATEGORY_NINTENDO_DOLPHIN;
    bool has_map_category = false;

    if (!porpoise_signature_compute(
            porpoise_session_program(plan->session),
            view->function,
            &view->signature)) {
        return true;
    }
    signature_eligible =
        porpoise_signature_is_automatic_match_eligible(&view->signature);
    match_hint_used = lookup_verified_match_hint(
        sdk_catalog, options, view, &match);
    if (!match_hint_used) {
        match = porpoise_sdk_catalog_lookup_exact(
            sdk_catalog, &view->signature);
    } else if (options->match_hint_used_count_out != NULL) {
        (*options->match_hint_used_count_out)++;
    }
    if (match.status == PORPOISE_SDK_CATALOG_MATCH_UNIQUE) {
        view->sdk_entry = match.entry;
        view->canonical_sdk_identity = match.entry->canonical_identity;
        view->sdk_category = match.entry->category;
        view->has_sdk_category = true;
        view->evidence_flags |= PORPOISE_PLAN_EVIDENCE_SIGNATURE;
        if (signature_eligible) {
            view->confidence = PORPOISE_MATCH_CONFIDENCE_EXACT;
        }
    } else if (match.status == PORPOISE_SDK_CATALOG_MATCH_AMBIGUOUS) {
        view->evidence_flags |=
            PORPOISE_PLAN_EVIDENCE_AMBIGUOUS_SIGNATURE;
    }
    view->map_symbol = select_map_symbol(
        symbols,
        options->module,
        view->function,
        view->sdk_entry,
        &has_map_evidence,
        &canonical_name_seen);
    if (has_map_evidence) {
        view->evidence_flags |= PORPOISE_PLAN_EVIDENCE_MAP;
        if (view->confidence == PORPOISE_MATCH_CONFIDENCE_NONE) {
            view->confidence = PORPOISE_MATCH_CONFIDENCE_MAP_ONLY;
        }
    }
    if (view->map_symbol != NULL) {
        has_map_category = map_library_category(
            view->map_symbol->library, &map_category);
        if (!view->has_sdk_category && has_map_category) {
            view->sdk_category = map_category;
            view->has_sdk_category = true;
            if (!assign_map_canonical_identity(plan, view)) return false;
        }
        if (view->map_symbol->has_size && view->map_symbol->size != 0U &&
            view->map_symbol->size != view->function->size) {
            conflict = true;
        }
    }
    if (view->sdk_entry != NULL && has_map_evidence &&
        !canonical_name_seen) {
        conflict = true;
    }
    if (view->sdk_entry != NULL && has_map_category &&
        map_category != view->sdk_entry->category) {
        conflict = true;
    }
    if (conflict) {
        view->evidence_flags |= PORPOISE_PLAN_EVIDENCE_CONFLICT;
        if (options->sdk_policy == PORPOISE_SDK_POLICY_KEEP) {
            porpoise_diagnostics_add(
                diagnostics,
                PORPOISE_SEVERITY_WARNING,
                view->source->path,
                0U,
                view->function->start_address,
                "map and exact signature evidence disagree for %s; keeping its body",
                view->function->name);
        }
    }

    if (view->action != PORPOISE_PLAN_ACTION_LIFT ||
        view->sdk_entry == NULL || !signature_eligible ||
        !porpoise_sdk_category_is_automatic(view->sdk_entry->category)) {
        return true;
    }
    contract_name = contract_name_for_entry(view->sdk_entry, NULL);
    direct_contract = find_effective_direct_contract(plan, contract_name);
    if (direct_contract == NULL) {
        contract = builtin_contract_for_entry(view->sdk_entry, NULL);
    }
    if (direct_contract == NULL && contract != NULL &&
        !porpoise_sdk_contract_allows_automatic_import(contract)) {
        contract = NULL;
    }
    if (options->sdk_policy == PORPOISE_SDK_POLICY_IMPORTED &&
        (direct_contract != NULL || contract != NULL) && !conflict) {
        view->requested_action = PORPOISE_PLAN_ACTION_IMPORT;
    } else if (options->sdk_policy == PORPOISE_SDK_POLICY_OMIT &&
               !conflict) {
        view->requested_action =
            direct_contract != NULL || contract != NULL
                                     ? PORPOISE_PLAN_ACTION_IMPORT
                                     : PORPOISE_PLAN_ACTION_OMIT;
    }
    if (conflict && options->sdk_policy != PORPOISE_SDK_POLICY_KEEP) {
        mark_view_blocked(plan, view, BLOCK_CONFLICT);
        return true;
    }
    if (view->requested_action == PORPOISE_PLAN_ACTION_IMPORT) {
        const PorpoiseAbiFunction *binding = direct_contract;
        if (binding == NULL) {
            binding = append_contract_import(
                plan, contract, view->function->name);
        }
        if (binding == NULL) {
            mark_view_blocked(plan, view, BLOCK_IMPORT_CONTRACT);
            return true;
        }
        view->action = PORPOISE_PLAN_ACTION_IMPORT;
        view->origin = PORPOISE_PLAN_ORIGIN_SDK_POLICY;
        view->binding = binding;
        view->contract_name = direct_contract != NULL
                                  ? direct_contract->symbol
                                  : porpoise_sdk_contract_canonical_name(
                                        contract);
    } else if (view->requested_action == PORPOISE_PLAN_ACTION_OMIT) {
        view->action = PORPOISE_PLAN_ACTION_OMIT;
        view->origin = PORPOISE_PLAN_ORIGIN_SDK_POLICY;
    }
    return true;
}

static bool override_module_matches(
    const PorpoiseFunctionOverride *override,
    const char *module) {
    return override->module == NULL || override->module[0] == '\0' ||
           optional_string_equal(override->module, module);
}

static bool override_fingerprint_valid(const char *fingerprint) {
    size_t index;
    if (fingerprint == NULL ||
        strlen(fingerprint) != PORPOISE_SHA256_DIGEST_SIZE * 2U) {
        return false;
    }
    for (index = 0U; fingerprint[index] != '\0'; index++) {
        if (!isdigit((unsigned char)fingerprint[index]) &&
            !(fingerprint[index] >= 'a' && fingerprint[index] <= 'f')) {
            return false;
        }
    }
    return true;
}

static bool override_locator_matches(
    const PorpoiseFunctionOverride *override,
    const char *module,
    const PorpoiseFunctionPlanView *view) {
    return override_module_matches(override, module) &&
           override->address == view->function->start_address &&
           override->size == view->function->size &&
           override_fingerprint_valid(override->normalized_fingerprint) &&
           strcmp(
               override->normalized_fingerprint,
               view->signature.digest_hex) == 0;
}

static void apply_override(
    PorpoiseTranslationPlan *plan,
    PorpoiseFunctionPlanView *view,
    const PorpoiseFunctionOverride *override) {
    const PorpoiseSdkContract *contract;
    const PorpoiseAbiFunction *binding;
    const char *contract_name;
    view->overridden = true;
    view->override_action = override->action;
    view->evidence_flags |= PORPOISE_PLAN_EVIDENCE_OVERRIDE;
    if ((view->evidence_flags & PORPOISE_PLAN_EVIDENCE_CONFLICT) != 0U &&
        override->action != PORPOISE_OVERRIDE_AUTO &&
        !override->acknowledge_conflict) {
        mark_view_blocked(plan, view, BLOCK_OVERRIDE_CONFLICT);
        return;
    }
    if ((view->evidence_flags & PORPOISE_PLAN_EVIDENCE_CONFLICT) != 0U &&
        override->action != PORPOISE_OVERRIDE_AUTO &&
        override->acknowledge_conflict &&
        view->blocking_reason == BLOCK_CONFLICT) {
        view->blocked = false;
        view->blocking_reason = NULL;
    }
    if (override->action == PORPOISE_OVERRIDE_AUTO) return;
    view->binding = NULL;
    view->binding_alias = NULL;
    view->contract_name = NULL;
    view->origin = PORPOISE_PLAN_ORIGIN_MANUAL_OVERRIDE;
    switch (override->action) {
        case PORPOISE_OVERRIDE_LIFT:
            if (view->function->data_region) {
                mark_view_blocked(plan, view, BLOCK_INPUT_DATA);
            } else if (view->function->skipped) {
                mark_view_blocked(plan, view, BLOCK_SKIPPED_LIFT);
            } else {
                view->action = PORPOISE_PLAN_ACTION_LIFT;
            }
            break;
        case PORPOISE_OVERRIDE_IMPORT:
            contract_name = contract_name_for_entry(
                view->sdk_entry, override->contract_name);
            binding = find_effective_direct_contract(
                plan, contract_name);
            contract = NULL;
            if (binding == NULL) {
                contract = builtin_contract_for_entry(
                    view->sdk_entry, override->contract_name);
                binding = append_contract_import(
                    plan, contract, view->function->name);
            }
            if (binding == NULL) {
                mark_view_blocked(plan, view, BLOCK_IMPORT_CONTRACT);
            } else {
                view->action = PORPOISE_PLAN_ACTION_IMPORT;
                view->binding = binding;
                view->contract_name = contract != NULL
                                          ? porpoise_sdk_contract_canonical_name(
                                                contract)
                                          : binding->symbol;
            }
            break;
        case PORPOISE_OVERRIDE_OMIT:
            if (view->function->data_region) {
                mark_view_blocked(plan, view, BLOCK_INPUT_DATA);
            } else {
                view->action = PORPOISE_PLAN_ACTION_OMIT;
            }
            break;
        case PORPOISE_OVERRIDE_TREAT_AS_DATA:
            if (view->function->data_region) {
                view->action = PORPOISE_PLAN_ACTION_DATA;
            } else {
                view->action = PORPOISE_PLAN_ACTION_DATA;
            }
            break;
        case PORPOISE_OVERRIDE_AUTO:
        default:
            break;
    }
}

int porpoise_plan_build(
    const PorpoiseSession *session,
    const PorpoisePlanOptions *options,
    PorpoiseTranslationPlan **plan_out,
    PorpoiseDiagnostics *diagnostics) {
    PorpoisePlanOptions defaults;
    PorpoiseTranslationPlan *plan;
    const PorpoiseProgram *program;
    const PorpoiseAbiManifest *abi;
    bool *matched_overrides = NULL;
    size_t file_index;
    size_t view_index = 0U;
    size_t override_index;
    int result;

    if (plan_out == NULL || diagnostics == NULL) return PORPOISE_EXIT_INTERNAL;
    *plan_out = NULL;
    if (session == NULL) {
        return plan_error(
            diagnostics, PORPOISE_EXIT_INTERNAL,
            "recovery session is required to build a translation plan");
    }
    if (options == NULL) {
        porpoise_plan_options_init(&defaults);
        options = &defaults;
    }
    if (options->match_hint_used_count_out != NULL) {
        *options->match_hint_used_count_out = 0U;
    }
    if (!sdk_policy_valid(options->sdk_policy)) {
        return plan_error(
            diagnostics, PORPOISE_EXIT_USAGE,
            "translation plan contains an invalid SDK policy");
    }
    if (options->override_count != 0U && options->overrides == NULL) {
        return plan_error(
            diagnostics, PORPOISE_EXIT_USAGE,
            "translation plan override array is inconsistent");
    }
    if (options->match_hint_count != 0U && options->match_hints == NULL) {
        return plan_error(
            diagnostics, PORPOISE_EXIT_USAGE,
            "translation plan match-hint array is inconsistent");
    }
    for (override_index = 0U;
         override_index < options->override_count;
         override_index++) {
        if (!override_action_valid(options->overrides[override_index].action) ||
            !override_fingerprint_valid(
                options->overrides[override_index].normalized_fingerprint)) {
            return plan_error(
                diagnostics, PORPOISE_EXIT_USAGE,
                "translation plan contains an invalid override record");
        }
    }
    if (porpoise_operation_cancelled(options->operation)) {
        return plan_cancelled(diagnostics);
    }
    program = porpoise_session_program(session);
    abi = porpoise_session_abi(session);
    if (program == NULL || abi == NULL) {
        return plan_error(
            diagnostics, PORPOISE_EXIT_INTERNAL,
            "recovery session is incomplete");
    }
    plan = (PorpoiseTranslationPlan *)calloc(1U, sizeof(*plan));
    if (plan == NULL) {
        return plan_error(
            diagnostics, PORPOISE_EXIT_INTERNAL,
            "out of memory while creating translation plan");
    }
    plan->session = session;
    plan->sdk_policy = options->sdk_policy;
    plan->target_id = options->target_id == NULL
                          ? NULL
                          : porpoise_strdup(options->target_id);
    plan->module = options->module == NULL
                       ? NULL
                       : porpoise_strdup(options->module);
    if ((options->target_id != NULL && plan->target_id == NULL) ||
        (options->module != NULL && plan->module == NULL)) {
        porpoise_plan_free(plan);
        return plan_error(
            diagnostics, PORPOISE_EXIT_INTERNAL,
            "out of memory while copying plan identity");
    }
    porpoise_analysis_init(&plan->analysis);
    porpoise_abi_init(&plan->effective_abi);
    if (!count_program_functions(program, &plan->function_count) ||
        (plan->function_count != 0U &&
         plan->function_count > SIZE_MAX / sizeof(*plan->functions))) {
        porpoise_plan_free(plan);
        return plan_error(
            diagnostics, PORPOISE_EXIT_INTERNAL,
            "too many functions in translation plan");
    }
    if (!initialize_effective_abi(plan, abi)) {
        porpoise_plan_free(plan);
        return plan_error(
            diagnostics, PORPOISE_EXIT_INTERNAL,
            "out of memory while snapshotting the effective ABI");
    }
    if (plan->function_count != 0U) {
        plan->functions = (PorpoiseFunctionPlanView *)calloc(
            plan->function_count, sizeof(*plan->functions));
        plan->owned_sdk_identities = (char **)calloc(
            plan->function_count, sizeof(*plan->owned_sdk_identities));
        if (plan->functions == NULL ||
            plan->owned_sdk_identities == NULL) {
            porpoise_plan_free(plan);
            return plan_error(
                diagnostics, PORPOISE_EXIT_INTERNAL,
                "out of memory while snapshotting translation plan");
        }
    }
    if (options->override_count != 0U) {
        matched_overrides = (bool *)calloc(
            options->override_count, sizeof(*matched_overrides));
        if (matched_overrides == NULL) {
            porpoise_plan_free(plan);
            return plan_error(
                diagnostics, PORPOISE_EXIT_INTERNAL,
                "out of memory while matching overrides");
        }
    }

    porpoise_operation_progress(
        options->operation, PORPOISE_PHASE_PLAN, 0U,
        plan->function_count, "analyzing recovery input");
    result = porpoise_analyze_program(
        program, abi, options->entry_symbol, &plan->analysis, diagnostics);
    if (result != PORPOISE_EXIT_OK) {
        free(matched_overrides);
        porpoise_plan_free(plan);
        return result;
    }
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *source = &program->files[file_index];
        size_t function_index;
        for (function_index = 0U;
             function_index < source->function_count;
             function_index++) {
            const PorpoiseFunction *function =
                &source->functions[function_index];
            PorpoiseFunctionPlanView *view = &plan->functions[view_index];
            populate_legacy_action(
                view, source, function, &plan->analysis, plan);
            if (!populate_sdk_evidence(
                    plan, view, options, diagnostics)) {
                free(matched_overrides);
                porpoise_plan_free(plan);
                return plan_error(
                    diagnostics, PORPOISE_EXIT_INTERNAL,
                    "out of memory while recording SDK ownership identity");
            }
            for (override_index = 0U;
                 override_index < options->override_count;
                 override_index++) {
                if (!override_locator_matches(
                        &options->overrides[override_index],
                        options->module,
                        view)) {
                    continue;
                }
                if (matched_overrides[override_index]) {
                    mark_view_blocked(
                        plan, view,
                        "override locator matches more than one function");
                    continue;
                }
                matched_overrides[override_index] = true;
                apply_override(
                    plan, view, &options->overrides[override_index]);
            }
            if (function == plan->analysis.entry) plan->entry = view;
            view_index++;
            porpoise_operation_progress(
                options->operation,
                PORPOISE_PHASE_SIGNATURES,
                view_index,
                plan->function_count,
                function->name);
            if (porpoise_operation_cancelled(options->operation)) {
                free(matched_overrides);
                porpoise_plan_free(plan);
                return plan_cancelled(diagnostics);
            }
        }
    }
    plan->blocked = false;
    plan->blocking_reason = NULL;
    for (view_index = 0U; view_index < plan->function_count; view_index++) {
        if (plan->functions[view_index].blocked) {
            plan->blocked = true;
            if (plan->blocking_reason == NULL) {
                plan->blocking_reason =
                    plan->functions[view_index].blocking_reason;
            }
        }
    }
    for (override_index = 0U;
         override_index < options->override_count;
         override_index++) {
        if (!matched_overrides[override_index]) {
            plan->blocked = true;
            plan->blocking_reason =
                "one or more override locators are stale";
            porpoise_diagnostics_add(
                diagnostics,
                PORPOISE_SEVERITY_WARNING,
                NULL,
                0U,
                options->overrides[override_index].address,
                "override locator is stale or no longer identifies an exact function");
        }
    }
    free(matched_overrides);
    if (!porpoise_plan_compute_binding_digest(
            plan, plan->digest_hex)) {
        porpoise_plan_free(plan);
        return plan_error(
            diagnostics, PORPOISE_EXIT_INTERNAL,
            "failed to bind translation plan to its recovery session");
    }
    porpoise_operation_progress(
        options->operation, PORPOISE_PHASE_PLAN,
        plan->function_count, plan->function_count,
        "translation plan ready");
    *plan_out = plan;
    return PORPOISE_EXIT_OK;
}

void porpoise_plan_free(PorpoiseTranslationPlan *plan) {
    size_t index;
    if (plan == NULL) return;
    free(plan->target_id);
    free(plan->module);
    for (index = 0U; index < plan->function_count; index++) {
        free(plan->owned_sdk_identities == NULL
                 ? NULL
                 : plan->owned_sdk_identities[index]);
    }
    free(plan->owned_sdk_identities);
    free(plan->functions);
    porpoise_abi_free(&plan->effective_abi);
    porpoise_analysis_free(&plan->analysis);
    free(plan);
}

static bool plan_function_has_contiguous_bytes(
    const PorpoiseFunction *function) {
    uint64_t expected = function->start_address;
    uint64_t end = expected + function->size;
    size_t item_index;
    if (function->size == 0U || (function->size & UINT32_C(3)) != 0U ||
        end > (UINT64_C(1) << 32)) {
        return false;
    }
    for (item_index = 0U; item_index < function->item_count; item_index++) {
        const PorpoiseAsmItem *item = &function->items[item_index];
        if (item->kind != PORPOISE_ASM_INSTRUCTION) continue;
        if ((uint64_t)item->address != expected || expected + 4U > end) {
            return false;
        }
        expected += 4U;
    }
    return expected == end;
}

static bool plan_data_action_overlaps_existing_data(
    const PorpoiseProgram *program,
    const PorpoiseFunction *function) {
    uint64_t function_start = function->start_address;
    uint64_t function_end = function_start + function->size;
    size_t span_index;
    for (span_index = 0U;
         span_index < program->data_span_count;
         span_index++) {
        const PorpoiseDataSpan *span = &program->data_spans[span_index];
        uint64_t span_start = span->address;
        uint64_t span_end = span_start + span->size;
        if (function_start < span_end && span_start < function_end) {
            return true;
        }
    }
    return false;
}

int porpoise_plan_validate(
    const PorpoiseTranslationPlan *plan,
    PorpoiseDiagnostics *diagnostics) {
    const PorpoiseProgram *program;
    const PorpoiseAbiManifest *session_abi;
    char current_digest[PORPOISE_SHA256_HEX_SIZE];
    size_t expected_count;
    size_t file_index;
    size_t view_index = 0U;
    size_t lifted_count = 0U;
    bool valid = true;
    if (diagnostics == NULL) return PORPOISE_EXIT_INTERNAL;
    if (plan == NULL || plan->session == NULL) {
        return plan_error(
            diagnostics, PORPOISE_EXIT_INTERNAL,
            "translation plan is not initialized");
    }
    if (!sdk_policy_valid(plan->sdk_policy)) {
        return plan_error(
            diagnostics, PORPOISE_EXIT_INTERNAL,
            "translation plan SDK policy is corrupt");
    }
    program = porpoise_session_program(plan->session);
    session_abi = porpoise_session_abi(plan->session);
    if (program == NULL || session_abi == NULL ||
        !count_program_functions(program, &expected_count) ||
        expected_count != plan->function_count ||
        (expected_count != 0U && plan->functions == NULL)) {
        return plan_error(
            diagnostics, PORPOISE_EXIT_INTERNAL,
            "translation plan function snapshot is inconsistent");
    }
    if (!porpoise_plan_compute_binding_digest(plan, current_digest) ||
        strcmp(current_digest, plan->digest_hex) != 0) {
        return plan_error(
            diagnostics, PORPOISE_EXIT_TRANSLATION,
            "translation plan binding digest mismatch; the recovery session, selected settings, or plan contents changed after planning");
    }
    if (plan->blocked) {
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            NULL,
            0U,
            0U,
            "%s",
            plan->blocking_reason != NULL
                ? plan->blocking_reason
                : "translation plan contains blocking diagnostics");
        valid = false;
    }
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *source = &program->files[file_index];
        size_t function_index;
        for (function_index = 0U;
             function_index < source->function_count;
             function_index++) {
            const PorpoiseFunction *function =
                &source->functions[function_index];
            const PorpoiseFunctionPlanView *view =
                &plan->functions[view_index++];
            bool action_valid = true;
            if (view->source != source || view->function != function ||
                view->binding_address != function->start_address) {
                return plan_error(
                    diagnostics, PORPOISE_EXIT_INTERNAL,
                    "translation plan function order is inconsistent");
            }
            switch (view->action) {
                case PORPOISE_PLAN_ACTION_LIFT:
                    action_valid = !function->data_region &&
                                   view->binding == NULL;
                    if (action_valid) lifted_count++;
                    break;
                case PORPOISE_PLAN_ACTION_DATA:
                    action_valid = view->binding == NULL &&
                                   (function->data_region ||
                                    (plan_function_has_contiguous_bytes(function) &&
                                     !plan_data_action_overlaps_existing_data(
                                         program, function)));
                    if (!action_valid && !function->data_region) {
                        porpoise_diagnostics_add(
                            diagnostics, PORPOISE_SEVERITY_ERROR,
                            source->path, 0U, function->start_address,
                            "function %s cannot be treated as data because its byte range is incomplete or overlaps existing data",
                            function->name);
                    }
                    break;
                case PORPOISE_PLAN_ACTION_OMIT:
                    action_valid = !function->data_region &&
                                   view->binding == NULL;
                    break;
                case PORPOISE_PLAN_ACTION_IMPORT:
                    action_valid = !function->data_region &&
                                   view->binding != NULL &&
                                   view->binding->kind == PORPOISE_ABI_IMPORT;
                    break;
                default:
                    action_valid = false;
                    break;
            }
            if (view->blocked || !action_valid) {
                porpoise_diagnostics_add(
                    diagnostics,
                    PORPOISE_SEVERITY_ERROR,
                    source->path,
                    0U,
                    function->start_address,
                    "translation plan action for %s is blocked: %s",
                    function->name,
                    view->blocking_reason != NULL
                        ? view->blocking_reason
                        : "structurally inconsistent action");
                valid = false;
            }
        }
    }
    if (lifted_count == 0U) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            NULL, 0U, 0U, "no functions remain to translate");
        valid = false;
    }
    if (plan->entry != NULL &&
        plan->entry->action != PORPOISE_PLAN_ACTION_LIFT) {
        porpoise_diagnostics_add(
            diagnostics,
            PORPOISE_SEVERITY_ERROR,
            plan->entry->source->path,
            0U,
            plan->entry->function->start_address,
            "entry point %s must remain lifted",
            plan->entry->function->name);
        valid = false;
    }
    for (view_index = 0U;
         view_index < session_abi->function_count;
         view_index++) {
        const PorpoiseAbiFunction *abi_function =
            &session_abi->functions[view_index];
        const PorpoiseFunctionPlanView *view;
        if (abi_function->kind != PORPOISE_ABI_EXPORT) continue;
        view = porpoise_plan_find_function(plan, abi_function->symbol);
        if (view == NULL || view->action != PORPOISE_PLAN_ACTION_LIFT) {
            porpoise_diagnostics_add(
                diagnostics,
                PORPOISE_SEVERITY_ERROR,
                NULL,
                0U,
                0U,
                "ABI export %s does not resolve to a lifted function",
                abi_function->symbol);
            valid = false;
        }
    }
    return valid ? PORPOISE_EXIT_OK : PORPOISE_EXIT_TRANSLATION;
}

const PorpoiseSession *porpoise_plan_session(
    const PorpoiseTranslationPlan *plan) {
    return plan == NULL ? NULL : plan->session;
}

PorpoiseSdkPolicy porpoise_plan_sdk_policy(
    const PorpoiseTranslationPlan *plan) {
    return plan == NULL ? PORPOISE_SDK_POLICY_KEEP : plan->sdk_policy;
}

size_t porpoise_plan_function_count(
    const PorpoiseTranslationPlan *plan) {
    return plan == NULL ? 0U : plan->function_count;
}

const PorpoiseFunctionPlanView *porpoise_plan_function_at(
    const PorpoiseTranslationPlan *plan,
    size_t index) {
    if (plan == NULL || index >= plan->function_count) return NULL;
    return &plan->functions[index];
}

const PorpoiseFunctionPlanView *porpoise_plan_find_function(
    const PorpoiseTranslationPlan *plan,
    const char *name) {
    size_t index;
    if (plan == NULL || name == NULL) return NULL;
    for (index = 0U; index < plan->function_count; index++) {
        if (strcmp(plan->functions[index].function->name, name) == 0) {
            return &plan->functions[index];
        }
    }
    return NULL;
}

const PorpoiseFunctionPlanView *porpoise_plan_entry(
    const PorpoiseTranslationPlan *plan) {
    return plan == NULL ? NULL : plan->entry;
}

const char *porpoise_plan_target_id(
    const PorpoiseTranslationPlan *plan) {
    return plan == NULL ? NULL : plan->target_id;
}

const char *porpoise_plan_module(
    const PorpoiseTranslationPlan *plan) {
    return plan == NULL ? NULL : plan->module;
}

const char *porpoise_plan_digest(
    const PorpoiseTranslationPlan *plan) {
    return plan == NULL ? NULL : plan->digest_hex;
}

const PorpoiseAnalysis *porpoise_plan_analysis_snapshot(
    const PorpoiseTranslationPlan *plan) {
    return plan == NULL ? NULL : &plan->analysis;
}

const PorpoiseAbiManifest *porpoise_plan_effective_abi(
    const PorpoiseTranslationPlan *plan) {
    return plan == NULL ? NULL : &plan->effective_abi;
}

const char *porpoise_sdk_policy_name(PorpoiseSdkPolicy policy) {
    switch (policy) {
        case PORPOISE_SDK_POLICY_KEEP: return "keep";
        case PORPOISE_SDK_POLICY_IMPORTED: return "imported";
        case PORPOISE_SDK_POLICY_OMIT: return "omit";
        default: return "unknown";
    }
}

const char *porpoise_plan_action_name(PorpoisePlanAction action) {
    switch (action) {
        case PORPOISE_PLAN_ACTION_LIFT: return "lift";
        case PORPOISE_PLAN_ACTION_DATA: return "data";
        case PORPOISE_PLAN_ACTION_OMIT: return "omit";
        case PORPOISE_PLAN_ACTION_IMPORT: return "import";
        default: return "unknown";
    }
}

const char *porpoise_plan_origin_name(PorpoisePlanOrigin origin) {
    switch (origin) {
        case PORPOISE_PLAN_ORIGIN_DEFAULT: return "default";
        case PORPOISE_PLAN_ORIGIN_INPUT_DATA: return "input-data";
        case PORPOISE_PLAN_ORIGIN_SKIP_LIST: return "skip-list";
        case PORPOISE_PLAN_ORIGIN_ABI_IMPORT: return "abi-import";
        case PORPOISE_PLAN_ORIGIN_SDK_POLICY: return "sdk-policy";
        case PORPOISE_PLAN_ORIGIN_MANUAL_OVERRIDE: return "manual-override";
        default: return "unknown";
    }
}

const char *porpoise_override_action_name(PorpoiseOverrideAction action) {
    switch (action) {
        case PORPOISE_OVERRIDE_AUTO: return "auto";
        case PORPOISE_OVERRIDE_LIFT: return "lift";
        case PORPOISE_OVERRIDE_IMPORT: return "import";
        case PORPOISE_OVERRIDE_OMIT: return "omit";
        case PORPOISE_OVERRIDE_TREAT_AS_DATA: return "data";
        default: return "unknown";
    }
}

const char *porpoise_match_confidence_name(
    PorpoiseMatchConfidence confidence) {
    switch (confidence) {
        case PORPOISE_MATCH_CONFIDENCE_NONE: return "none";
        case PORPOISE_MATCH_CONFIDENCE_MAP_ONLY: return "map-only";
        case PORPOISE_MATCH_CONFIDENCE_EXACT: return "exact";
        default: return "unknown";
    }
}
