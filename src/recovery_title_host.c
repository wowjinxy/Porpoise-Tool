#include "porpoise/recovery_title_host.h"

#include "porpoise/sha256.h"
#include "porpoise/util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int title_host_error(
    PorpoiseDiagnostics *diagnostics,
    int result,
    const char *file,
    uint32_t address,
    const char *format,
    ...) {
    char message[PORPOISE_MESSAGE_CAPACITY];
    va_list arguments;
    int written;
    if (diagnostics == NULL) return PORPOISE_EXIT_INTERNAL;
    va_start(arguments, format);
    written = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (written < 0 || !porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            file == NULL ? "" : file, 0U, address, "%s",
            written < 0 ? "failed to format title-host diagnostic" :
                          message)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    return result;
}

static bool title_host_sha256_valid(const char *value) {
    size_t index;
    if (value == NULL || strlen(value) != 64U) return false;
    for (index = 0U; index < 64U; index++) {
        if (!((value[index] >= '0' && value[index] <= '9') ||
              (value[index] >= 'a' && value[index] <= 'f'))) return false;
    }
    return true;
}

static void title_host_hash_u32(
    PorpoiseSha256Context *hash,
    uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
    porpoise_sha256_update(hash, bytes, sizeof(bytes));
}

static void title_host_hash_size(
    PorpoiseSha256Context *hash,
    size_t value) {
    uint8_t bytes[8];
    uint64_t portable = (uint64_t)value;
    size_t index;
    for (index = 0U; index < sizeof(bytes); index++) {
        bytes[sizeof(bytes) - index - 1U] = (uint8_t)portable;
        portable >>= 8U;
    }
    porpoise_sha256_update(hash, bytes, sizeof(bytes));
}

static void title_host_hash_string(
    PorpoiseSha256Context *hash,
    const char *value) {
    size_t length = value == NULL ? 0U : strlen(value);
    title_host_hash_size(hash, length);
    if (length != 0U) porpoise_sha256_update(hash, value, length);
}

static void title_host_hash_signature(
    PorpoiseSha256Context *hash,
    const PorpoiseFunctionSignature *signature) {
    title_host_hash_u32(hash, signature->algorithm_version);
    title_host_hash_u32(hash, signature->function_size);
    title_host_hash_u32(hash, signature->instruction_count);
    title_host_hash_u32(hash, signature->fixed_instruction_count);
    title_host_hash_u32(
        hash, signature->meaningful_fixed_instruction_count);
    title_host_hash_u32(hash, signature->relocation_count);
    title_host_hash_u32(hash, signature->internal_branch_count);
    title_host_hash_u32(hash, signature->external_branch_count);
    title_host_hash_u32(hash, signature->external_target_count);
    title_host_hash_u32(hash, signature->issue_flags);
    porpoise_sha256_update(
        hash, signature->digest, sizeof(signature->digest));
}

/*
 * These identities intentionally hash parsed evidence rather than resolved
 * host paths.  A reviewed profile therefore remains portable when a project
 * moves, while any semantic map/catalog change invalidates the review.
 */
static bool title_host_symbol_evidence_identity(
    const PorpoiseRecoveryTarget *target,
    const PorpoiseTranslationPlan *plan,
    bool *present_out,
    char digest_hex[PORPOISE_SHA256_HEX_SIZE]) {
    const PorpoiseSession *session = porpoise_plan_session(plan);
    const PorpoiseSymbolCatalog *catalog =
        porpoise_session_symbols(session);
    PorpoiseSha256Context hash;
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    size_t index;
    bool present;
    if (target == NULL || session == NULL || catalog == NULL ||
        present_out == NULL || digest_hex == NULL ||
        (target->symbol_source_count != 0U &&
         target->symbol_sources == NULL) ||
        (catalog->symbol_count != 0U && catalog->symbols == NULL)) {
        return false;
    }
    present = target->symbol_source_count != 0U ||
              catalog->symbol_count != 0U;
    *present_out = present;
    digest_hex[0] = '\0';
    if (!present) return true;

    porpoise_sha256_init(&hash);
    title_host_hash_string(
        &hash, "porpoise-title-host-symbol-evidence-v1");
    title_host_hash_size(&hash, target->symbol_source_count);
    for (index = 0U; index < target->symbol_source_count; index++) {
        const PorpoiseRecoverySymbolSource *source =
            &target->symbol_sources[index];
        title_host_hash_u32(&hash, (uint32_t)source->kind);
        title_host_hash_string(&hash, source->module);
        title_host_hash_u32(&hash, source->permissive ? 1U : 0U);
        title_host_hash_u32(
            &hash, source->has_auxiliary_path ? 1U : 0U);
    }
    title_host_hash_size(&hash, catalog->symbol_count);
    for (index = 0U; index < catalog->symbol_count; index++) {
        const PorpoiseSymbol *symbol = &catalog->symbols[index];
        title_host_hash_string(&hash, symbol->name);
        title_host_hash_string(&hash, symbol->section);
        title_host_hash_string(&hash, symbol->module);
        title_host_hash_string(&hash, symbol->object);
        title_host_hash_string(&hash, symbol->library);
        title_host_hash_u32(&hash, symbol->address);
        title_host_hash_u32(&hash, symbol->size);
        title_host_hash_u32(&hash, symbol->has_address ? 1U : 0U);
        title_host_hash_u32(&hash, symbol->has_size ? 1U : 0U);
        title_host_hash_u32(&hash, symbol->used ? 1U : 0U);
        title_host_hash_u32(&hash, (uint32_t)symbol->kind);
        title_host_hash_u32(&hash, (uint32_t)symbol->scope);
        title_host_hash_u32(
            &hash, (uint32_t)symbol->provenance.kind);
    }
    porpoise_sha256_final(&hash, digest);
    porpoise_sha256_hex(digest, digest_hex);
    return true;
}

static bool title_host_sdk_evidence_identity(
    const PorpoiseTranslationPlan *plan,
    bool *present_out,
    char digest_hex[PORPOISE_SHA256_HEX_SIZE]) {
    const PorpoiseSession *session = porpoise_plan_session(plan);
    const PorpoiseSdkCatalog *catalog =
        porpoise_session_sdk_catalog(session);
    PorpoiseSha256Context hash;
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    size_t index;
    if (session == NULL || catalog == NULL || present_out == NULL ||
        digest_hex == NULL ||
        (catalog->entry_count != 0U && catalog->entries == NULL)) {
        return false;
    }
    *present_out = catalog->entry_count != 0U;
    digest_hex[0] = '\0';
    if (catalog->entry_count == 0U) return true;

    porpoise_sha256_init(&hash);
    title_host_hash_string(
        &hash, "porpoise-title-host-sdk-evidence-v1");
    title_host_hash_size(&hash, catalog->entry_count);
    for (index = 0U; index < catalog->entry_count; index++) {
        const PorpoiseSdkCatalogEntry *entry = &catalog->entries[index];
        title_host_hash_string(&hash, entry->canonical_identity);
        title_host_hash_u32(&hash, (uint32_t)entry->category);
        title_host_hash_string(&hash, entry->contract_name);
        title_host_hash_signature(&hash, &entry->signature);
        title_host_hash_u32(
            &hash, (uint32_t)entry->provenance.source_kind);
    }
    porpoise_sha256_final(&hash, digest);
    porpoise_sha256_hex(digest, digest_hex);
    return true;
}

static int title_host_capture_evidence(
    const PorpoiseRecoveryTarget *target,
    const PorpoiseTranslationPlan *plan,
    PorpoiseRecoveryTitleHostProfile *profile,
    PorpoiseDiagnostics *diagnostics) {
    char symbol_digest[PORPOISE_SHA256_HEX_SIZE];
    char sdk_digest[PORPOISE_SHA256_HEX_SIZE];
    bool have_symbols;
    bool have_sdk;
    if (!title_host_symbol_evidence_identity(
            target, plan, &have_symbols, symbol_digest) ||
        !title_host_sdk_evidence_identity(
            plan, &have_sdk, sdk_digest)) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_INTERNAL,
            target == NULL ? NULL : target->input.resolved, 0U,
            "cannot bind title_host to malformed session evidence");
    }
    if (have_symbols) {
        profile->symbol_sources_sha256 = porpoise_strdup(symbol_digest);
        if (profile->symbol_sources_sha256 == NULL) {
            return title_host_error(
                diagnostics, PORPOISE_EXIT_INTERNAL,
                target->input.resolved, 0U,
                "out of memory while binding title_host symbol evidence");
        }
    }
    if (have_sdk) {
        profile->sdk_catalogs_sha256 = porpoise_strdup(sdk_digest);
        if (profile->sdk_catalogs_sha256 == NULL) {
            return title_host_error(
                diagnostics, PORPOISE_EXIT_INTERNAL,
                target->input.resolved, 0U,
                "out of memory while binding title_host SDK evidence");
        }
    }
    return PORPOISE_EXIT_OK;
}

void porpoise_recovery_title_host_profile_init(
    PorpoiseRecoveryTitleHostProfile *profile) {
    if (profile != NULL) memset(profile, 0, sizeof(*profile));
}

void porpoise_recovery_title_host_profile_free(
    PorpoiseRecoveryTitleHostProfile *profile) {
    size_t index;
    if (profile == NULL) return;
    for (index = 0U;
         index < PORPOISE_RECOVERY_TITLE_HOST_STARTUP_FUNCTION_CAPACITY;
         index++) {
        free(profile->startup_functions[index].module);
        free(profile->startup_functions[index].normalized_fingerprint);
    }
    free(profile->input_sha256);
    free(profile->symbol_sources_sha256);
    free(profile->sdk_catalogs_sha256);
    memset(profile, 0, sizeof(*profile));
}

static const PorpoiseFunctionPlanView *title_host_find_startup(
    const PorpoiseTranslationPlan *plan,
    const PorpoiseRecoveryTitleStartupFunction *startup) {
    const char *plan_module = porpoise_plan_module(plan);
    size_t index;
    if (plan_module == NULL) plan_module = "";
    if (startup->module == NULL ||
        strcmp(startup->module, plan_module) != 0) return NULL;
    for (index = 0U; index < porpoise_plan_function_count(plan); index++) {
        const PorpoiseFunctionPlanView *view =
            porpoise_plan_function_at(plan, index);
        if (view != NULL && view->function != NULL &&
            view->function->start_address == startup->address &&
            view->function->size == startup->size &&
            startup->normalized_fingerprint != NULL &&
            strcmp(view->signature.digest_hex,
                   startup->normalized_fingerprint) == 0) {
            return view;
        }
    }
    return NULL;
}

static bool title_host_find_unique_symbol_address(
    const PorpoiseTranslationPlan *plan,
    const char *name,
    uint32_t *address_out,
    PorpoiseDiagnostics *diagnostics) {
    const PorpoiseSession *session = porpoise_plan_session(plan);
    const PorpoiseSymbolCatalog *catalog =
        porpoise_session_symbols(session);
    const char *module = porpoise_plan_module(plan);
    size_t index = 0U;
    bool found = false;
    uint32_t address = 0U;
    if (module == NULL) module = "";
    if (catalog != NULL) {
        for (;;) {
            const PorpoiseSymbol *symbol;
            index = porpoise_symbol_catalog_find_name(
                catalog, module, name, index);
            if (index == SIZE_MAX) break;
            symbol = &catalog->symbols[index++];
            if (!symbol->has_address) continue;
            if (!found) {
                address = symbol->address;
                found = true;
            } else if (address != symbol->address) {
                title_host_error(
                    diagnostics, PORPOISE_EXIT_USAGE,
                    symbol->provenance.path, symbol->address,
                    "cannot infer title_host: symbol '%s' has ambiguous addresses",
                    name);
                return false;
            }
        }
    }
    if (!found) {
        title_host_error(
            diagnostics, PORPOISE_EXIT_USAGE, NULL, 0U,
            "cannot infer title_host: unique mapped symbol '%s' is required",
            name);
        return false;
    }
    *address_out = address;
    return true;
}

static bool title_host_copy_startup_view(
    PorpoiseRecoveryTitleStartupFunction *startup,
    const PorpoiseFunctionPlanView *view,
    const char *module,
    uint32_t flags) {
    if (view == NULL || view->function == NULL || view->blocked ||
        view->action != PORPOISE_PLAN_ACTION_LIFT ||
        view->function->size == 0U) return false;
    startup->module = porpoise_strdup(module == NULL ? "" : module);
    startup->normalized_fingerprint =
        porpoise_strdup(view->signature.digest_hex);
    if (startup->module == NULL ||
        startup->normalized_fingerprint == NULL) return false;
    startup->address = view->function->start_address;
    startup->size = view->function->size;
    startup->flags = flags;
    return true;
}

static int title_host_find_unique_startup_function(
    const PorpoiseRecoveryTarget *target,
    const PorpoiseTranslationPlan *plan,
    const char *name,
    const PorpoiseFunctionPlanView **view_out,
    PorpoiseDiagnostics *diagnostics) {
    const PorpoiseFunctionPlanView *match = NULL;
    uint32_t first_address = 0U;
    uint32_t second_address = 0U;
    size_t match_count = 0U;
    size_t index;
    if (target == NULL || plan == NULL || name == NULL ||
        view_out == NULL || diagnostics == NULL) {
        return PORPOISE_EXIT_INTERNAL;
    }
    *view_out = NULL;
    for (index = 0U; index < porpoise_plan_function_count(plan); index++) {
        const PorpoiseFunctionPlanView *candidate =
            porpoise_plan_function_at(plan, index);
        if (candidate == NULL || candidate->function == NULL ||
            candidate->function->name == NULL ||
            strcmp(candidate->function->name, name) != 0) {
            continue;
        }
        if (match_count == 0U) {
            match = candidate;
            first_address = candidate->function->start_address;
        } else if (match_count == 1U) {
            second_address = candidate->function->start_address;
        }
        match_count++;
    }
    if (match_count == 0U) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_TRANSLATION,
            target->input.resolved, 0U,
            "cannot infer title_host: startup function '%s' was not resolved",
            name);
    }
    if (match_count != 1U) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_TRANSLATION,
            target->input.resolved, second_address,
            "cannot infer title_host: startup function '%s' is ambiguous; "
            "%lu candidates were resolved (first at 0x%08lX, second at 0x%08lX)",
            name, (unsigned long)match_count,
            (unsigned long)first_address, (unsigned long)second_address);
    }
    if (match->action != PORPOISE_PLAN_ACTION_LIFT || match->blocked) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_TRANSLATION,
            target->input.resolved, first_address,
            "cannot infer title_host: startup function '%s' must resolve to Lift",
            name);
    }
    *view_out = match;
    return PORPOISE_EXIT_OK;
}

int porpoise_recovery_title_host_infer(
    const PorpoiseRecoveryTarget *target,
    const PorpoiseTranslationPlan *plan,
    PorpoiseRecoveryTitleHostProfile *profile_out,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseRecoveryTitleHostProfile candidate;
    PorpoiseRecoveryTarget candidate_target;
    const PorpoiseFunctionPlanView *entry;
    const PorpoiseFunctionPlanView *thread_init;
    const PorpoiseFunctionPlanView *user_init;
    const char *module;
    uint32_t stack_top;
    int result;
    if (target == NULL || plan == NULL || profile_out == NULL ||
        diagnostics == NULL) return PORPOISE_EXIT_INTERNAL;
    result = porpoise_plan_validate(plan, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    if (!title_host_sha256_valid(target->cache.input_sha256)) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_USAGE, target->input.resolved, 0U,
            "cannot infer title_host before the current input digest is cached");
    }
    entry = porpoise_plan_entry(plan);
    if (entry == NULL || entry->function == NULL || entry->blocked ||
        entry->action != PORPOISE_PLAN_ACTION_LIFT) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_TRANSLATION,
            target->input.resolved, 0U,
            "cannot infer title_host without a lifted direct entry");
    }
    result = title_host_find_unique_startup_function(
        target, plan, "__OSThreadInit", &thread_init, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    result = title_host_find_unique_startup_function(
        target, plan, "__init_user", &user_init, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;

    porpoise_recovery_title_host_profile_init(&candidate);
    candidate.entry_address = entry->function->start_address;
    if (!title_host_find_unique_symbol_address(
            plan, "_stack_addr", &stack_top, diagnostics) ||
        !title_host_find_unique_symbol_address(
            plan, "_SDA2_BASE_", &candidate.gpr[2], diagnostics) ||
        !title_host_find_unique_symbol_address(
            plan, "_SDA_BASE_", &candidate.gpr[13], diagnostics) ||
        !title_host_find_unique_symbol_address(
            plan, "__ArenaLo", &candidate.arena_lo, diagnostics) ||
        !title_host_find_unique_symbol_address(
            plan, "__ArenaHi", &candidate.arena_hi, diagnostics)) {
        porpoise_recovery_title_host_profile_free(&candidate);
        return PORPOISE_EXIT_USAGE;
    }
    if (stack_top < 8U) {
        porpoise_recovery_title_host_profile_free(&candidate);
        return title_host_error(
            diagnostics, PORPOISE_EXIT_USAGE,
            target->input.resolved, stack_top,
            "cannot infer title_host: _stack_addr cannot reserve the direct-main frame");
    }
    candidate.gpr[1] = stack_top - UINT32_C(8);
    candidate.initial_word_count = 2U;
    candidate.initial_words[0].address = candidate.gpr[1];
    candidate.initial_words[0].value = UINT32_MAX;
    candidate.initial_words[1].address = candidate.gpr[1] + UINT32_C(4);
    candidate.initial_words[1].value = UINT32_MAX;
    candidate.startup_function_count = 2U;
    module = porpoise_plan_module(plan);
    if (!title_host_copy_startup_view(
            &candidate.startup_functions[0], thread_init, module,
            PORPOISE_RECOVERY_TITLE_STARTUP_ESTABLISH_GUEST_MAIN_THREAD_AFTER) ||
        !title_host_copy_startup_view(
            &candidate.startup_functions[1], user_init, module, 0U)) {
        porpoise_recovery_title_host_profile_free(&candidate);
        return title_host_error(
            diagnostics, PORPOISE_EXIT_INTERNAL,
            target->input.resolved, 0U,
            "out of memory while inferring title_host startup locators");
    }
    candidate.input_sha256 = porpoise_strdup(target->cache.input_sha256);
    if (candidate.input_sha256 == NULL) {
        porpoise_recovery_title_host_profile_free(&candidate);
        return title_host_error(
            diagnostics, PORPOISE_EXIT_INTERNAL,
            target->input.resolved, 0U,
            "out of memory while binding the inferred title_host profile");
    }
    result = title_host_capture_evidence(
        target, plan, &candidate, diagnostics);
    if (result != PORPOISE_EXIT_OK) {
        porpoise_recovery_title_host_profile_free(&candidate);
        return result;
    }
    /* Host-side DVD initialization is policy, not linker evidence. */
    candidate.initialize_dvd = false;

    candidate_target = *target;
    candidate_target.title_host = candidate;
    candidate_target.has_title_host = true;
    result = porpoise_recovery_title_host_validate(
        &candidate_target, plan, diagnostics);
    if (result != PORPOISE_EXIT_OK) {
        porpoise_recovery_title_host_profile_free(&candidate);
        return result;
    }
    if (!porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_INFO,
            target->input.resolved, 0U, candidate.entry_address,
            "inferred CodeWarrior title_host bootstrap; review DVD initialization before Build/Run")) {
        porpoise_recovery_title_host_profile_free(&candidate);
        return PORPOISE_EXIT_INTERNAL;
    }
    porpoise_recovery_title_host_profile_free(profile_out);
    *profile_out = candidate;
    return PORPOISE_EXIT_OK;
}

static int title_host_validate_evidence(
    const PorpoiseRecoveryTarget *target,
    const PorpoiseTranslationPlan *plan,
    const PorpoiseRecoveryTitleHostProfile *profile,
    PorpoiseDiagnostics *diagnostics) {
    char symbol_digest[PORPOISE_SHA256_HEX_SIZE];
    char sdk_digest[PORPOISE_SHA256_HEX_SIZE];
    bool have_symbols;
    bool have_sdk;
    if (!title_host_symbol_evidence_identity(
            target, plan, &have_symbols, symbol_digest) ||
        !title_host_sdk_evidence_identity(
            plan, &have_sdk, sdk_digest)) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_INTERNAL,
            target->input.resolved, 0U,
            "cannot validate title_host against malformed session evidence");
    }
    if ((profile->symbol_sources_sha256 != NULL) != have_symbols ||
        (have_symbols &&
         strcmp(profile->symbol_sources_sha256, symbol_digest) != 0)) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_USAGE,
            target->input.resolved, profile->entry_address,
            "title_host profile is stale for the current symbol-source evidence");
    }
    if ((profile->sdk_catalogs_sha256 != NULL) != have_sdk ||
        (have_sdk &&
         strcmp(profile->sdk_catalogs_sha256, sdk_digest) != 0)) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_USAGE,
            target->input.resolved, profile->entry_address,
            "title_host profile is stale for the current SDK-catalog evidence");
    }
    return PORPOISE_EXIT_OK;
}

int porpoise_recovery_title_host_validate(
    const PorpoiseRecoveryTarget *target,
    const PorpoiseTranslationPlan *plan,
    PorpoiseDiagnostics *diagnostics) {
    const PorpoiseRecoveryTitleHostProfile *profile;
    const PorpoiseFunctionPlanView *entry;
    const char *plan_target;
    size_t index;
    size_t main_thread_bind_count = 0U;
    int result;
    if (target == NULL || plan == NULL || diagnostics == NULL) {
        return PORPOISE_EXIT_INTERNAL;
    }
    result = porpoise_plan_validate(plan, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    if (!target->has_title_host) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_USAGE, target->input.resolved, 0U,
            "target '%s' has no reviewed title_host profile", target->id);
    }
    profile = &target->title_host;
    plan_target = porpoise_plan_target_id(plan);
    if (plan_target != NULL &&
        (target->id == NULL || strcmp(plan_target, target->id) != 0)) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_USAGE, target->input.resolved, 0U,
            "title_host profile target does not match the translation plan");
    }
    if (!title_host_sha256_valid(profile->input_sha256) ||
        target->cache.input_sha256 == NULL ||
        strcmp(profile->input_sha256, target->cache.input_sha256) != 0) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_USAGE, target->input.resolved, 0U,
            "title_host profile is stale for the current input digest");
    }
    if ((profile->symbol_sources_sha256 != NULL &&
         !title_host_sha256_valid(profile->symbol_sources_sha256)) ||
        (profile->sdk_catalogs_sha256 != NULL &&
         !title_host_sha256_valid(profile->sdk_catalogs_sha256))) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_USAGE, target->input.resolved, 0U,
            "title_host profile contains malformed evidence provenance");
    }
    result = title_host_validate_evidence(
        target, plan, profile, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    entry = porpoise_plan_entry(plan);
    if (entry == NULL || entry->function == NULL ||
        entry->action != PORPOISE_PLAN_ACTION_LIFT || entry->blocked ||
        entry->function->start_address != profile->entry_address) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_TRANSLATION,
            target->input.resolved, profile->entry_address,
            "title_host entry is absent, stale, or no longer resolved to Lift");
    }
    if (profile->gpr[1] == 0U || profile->gpr[2] == 0U ||
        profile->gpr[13] == 0U || (profile->gpr[1] & UINT32_C(7)) != 0U) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_USAGE,
            target->input.resolved, profile->entry_address,
            "title_host requires 8-byte-aligned r1 and nonzero r2/r13");
    }
    if (!((profile->arena_lo == 0U && profile->arena_hi == 0U) ||
          (profile->arena_lo != 0U &&
           profile->arena_hi > profile->arena_lo))) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_USAGE,
            target->input.resolved, profile->arena_lo,
            "title_host arena bounds must both be zero or form a nonempty range");
    }
    if (profile->startup_function_count >
            PORPOISE_RECOVERY_TITLE_HOST_STARTUP_FUNCTION_CAPACITY ||
        profile->initial_word_count >
            PORPOISE_RECOVERY_TITLE_HOST_INITIAL_WORD_CAPACITY) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_USAGE,
            target->input.resolved, profile->entry_address,
            "title_host profile exceeds the runtime ABI capacities");
    }
    for (index = 0U; index < profile->startup_function_count; index++) {
        const PorpoiseRecoveryTitleStartupFunction *startup =
            &profile->startup_functions[index];
        const PorpoiseFunctionPlanView *view;
        size_t prior;
        if (startup->address == 0U || startup->size == 0U ||
            !title_host_sha256_valid(startup->normalized_fingerprint) ||
            (startup->flags &
             ~PORPOISE_RECOVERY_TITLE_STARTUP_KNOWN_FLAGS) != 0U) {
            return title_host_error(
                diagnostics, PORPOISE_EXIT_USAGE,
                target->input.resolved, startup->address,
                "title_host startup function %zu is malformed", index);
        }
        view = title_host_find_startup(plan, startup);
        if (view == NULL) {
            return title_host_error(
                diagnostics, PORPOISE_EXIT_TRANSLATION,
                target->input.resolved, startup->address,
                "title_host startup function %zu has a stale locator",
                index);
        }
        if (view->action != PORPOISE_PLAN_ACTION_LIFT || view->blocked) {
            return title_host_error(
                diagnostics, PORPOISE_EXIT_TRANSLATION,
                target->input.resolved, startup->address,
                "title_host startup function %s is not resolved to Lift",
                view->function->name);
        }
        for (prior = 0U; prior < index; prior++) {
            if (profile->startup_functions[prior].address ==
                    startup->address &&
                strcmp(profile->startup_functions[prior].module,
                       startup->module) == 0) {
                return title_host_error(
                    diagnostics, PORPOISE_EXIT_USAGE,
                    target->input.resolved, startup->address,
                    "title_host contains a duplicate startup function");
            }
        }
        if ((startup->flags &
             PORPOISE_RECOVERY_TITLE_STARTUP_ESTABLISH_GUEST_MAIN_THREAD_AFTER)
            != 0U) {
            main_thread_bind_count++;
        }
    }
    if (main_thread_bind_count > 1U) {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_USAGE,
            target->input.resolved, profile->entry_address,
            "title_host may establish the guest main thread only once");
    }
    for (index = 0U; index < profile->initial_word_count; index++) {
        size_t prior;
        uint32_t address = profile->initial_words[index].address;
        if (address == 0U || (address & UINT32_C(3)) != 0U) {
            return title_host_error(
                diagnostics, PORPOISE_EXIT_USAGE,
                target->input.resolved, address,
                "title_host initial word %zu is not 4-byte aligned", index);
        }
        for (prior = 0U; prior < index; prior++) {
            if (profile->initial_words[prior].address == address) {
                return title_host_error(
                    diagnostics, PORPOISE_EXIT_USAGE,
                    target->input.resolved, address,
                    "title_host contains duplicate initial-word addresses");
            }
        }
    }
    return PORPOISE_EXIT_OK;
}

static bool title_host_close(
    FILE *file,
    const char *path,
    PorpoiseDiagnostics *diagnostics) {
    if (ferror(file) || fclose(file) != 0) {
        title_host_error(
            diagnostics, PORPOISE_EXIT_IO, path, 0U,
            "failed to write generated title-host file");
        return false;
    }
    return true;
}

static bool title_host_write_meson(
    const char *directory,
    PorpoiseDiagnostics *diagnostics) {
    char path[PORPOISE_PATH_CAPACITY];
    FILE *file;
    if (!porpoise_path_join(path, sizeof(path), directory, "meson.build")) {
        title_host_error(
            diagnostics, PORPOISE_EXIT_USAGE, directory, 0U,
            "title-host Meson path is too long");
        return false;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        title_host_error(
            diagnostics, PORPOISE_EXIT_IO, path, 0U,
            "failed to create generated title-host Meson project");
        return false;
    }
    fputs(
        "project(\n"
        "  'porpoise-recovery-title-host',\n"
        "  'c',\n"
        "  version: '1.0.0',\n"
        "  default_options: ['c_std=c99', 'warning_level=3', 'werror=true'],\n"
        ")\n\n"
        "porpoise_title_contract_dep = dependency('porpoise-title-contract')\n\n"
        "porpoise_recovery_title_host_library = static_library(\n"
        "  'porpoise_recovery_title_host',\n"
        "  'porpoise_recovery_title_host.c',\n"
        "  dependencies: porpoise_title_contract_dep,\n"
        "  install: false,\n"
        ")\n\n"
        "porpoise_title_host_dep = declare_dependency(\n"
        "  link_with: porpoise_recovery_title_host_library,\n"
        "  dependencies: porpoise_title_contract_dep,\n"
        ")\n",
        file);
    return title_host_close(file, path, diagnostics);
}

static bool title_host_write_source(
    const PorpoiseRecoveryTitleHostProfile *profile,
    const char *directory,
    PorpoiseDiagnostics *diagnostics) {
    char path[PORPOISE_PATH_CAPACITY];
    FILE *file;
    size_t index;
    if (!porpoise_path_join(
            path, sizeof(path), directory,
            "porpoise_recovery_title_host.c")) {
        title_host_error(
            diagnostics, PORPOISE_EXIT_USAGE, directory, 0U,
            "title-host source path is too long");
        return false;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        title_host_error(
            diagnostics, PORPOISE_EXIT_IO, path, 0U,
            "failed to create generated title-host source");
        return false;
    }
    fputs(
        "#include \"porpoise_title_host.h\"\n\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <sys/stat.h>\n\n"
        "typedef void (*PorpoiseTitleConstructor)(void);\n\n"
        "PorpoiseTitleConstructor _ctors[] = {NULL};\n"
        "PorpoiseTitleConstructor _dtors[] = {NULL};\n\n",
        file);
    fputs(
        "int PorpoiseHostPrepareRuntimeV1(\n"
        "    uint32_t entry_address,\n"
        "    PorpoiseTitleRuntimeConfigV1 *config_out)\n"
        "{\n",
        file);
    if (profile->initialize_dvd) {
        fputs("    const char *dvd_root;\n    struct stat root_status;\n\n", file);
    }
    fprintf(file,
            "    if (entry_address != UINT32_C(0x%08lX) || "
            "config_out == NULL) {\n"
            "        return PORPOISE_TITLE_HOST_INVALID_STATE;\n"
            "    }\n\n"
            "    memset(config_out, 0, sizeof(*config_out));\n",
            (unsigned long)profile->entry_address);
    if (profile->initialize_dvd) {
        fprintf(file,
                "    dvd_root = getenv(\"%s\");\n"
                "    if (dvd_root == NULL || dvd_root[0] == '\\0' ||\n"
                "        stat(dvd_root, &root_status) != 0 ||\n"
                "        (root_status.st_mode & S_IFDIR) == 0) {\n"
                "        return PORPOISE_TITLE_HOST_UNAVAILABLE;\n"
                "    }\n"
                "    config_out->flags = PORPOISE_TITLE_RUNTIME_INITIALIZE_DVD;\n"
                "    config_out->dvd_root_directory = dvd_root;\n",
                PORPOISE_RECOVERY_TITLE_HOST_DVD_ROOT_ENV);
    }
    fputs("    return PORPOISE_TITLE_HOST_OK;\n}\n\n", file);

    fputs(
        "int PorpoiseHostPrepareTitleEntryV3(\n"
        "    uint32_t entry_address,\n"
        "    PorpoiseTitleEntryStateV3 *state_out)\n"
        "{\n",
        file);
    fprintf(file,
            "    if (entry_address != UINT32_C(0x%08lX) || "
            "state_out == NULL) {\n"
            "        return PORPOISE_TITLE_HOST_INVALID_STATE;\n"
            "    }\n\n"
            "    memset(state_out, 0, sizeof(*state_out));\n",
            (unsigned long)profile->entry_address);
    for (index = 0U; index < PORPOISE_RECOVERY_TITLE_HOST_GPR_COUNT;
         index++) {
        if (profile->gpr[index] != 0U) {
            fprintf(file,
                    "    state_out->gpr[%lu] = UINT32_C(0x%08lX);\n",
                    (unsigned long)index,
                    (unsigned long)profile->gpr[index]);
        }
    }
    fprintf(file,
            "    state_out->arena_lo = UINT32_C(0x%08lX);\n"
            "    state_out->arena_hi = UINT32_C(0x%08lX);\n"
            "    state_out->startup_function_count = %luU;\n",
            (unsigned long)profile->arena_lo,
            (unsigned long)profile->arena_hi,
            (unsigned long)profile->startup_function_count);
    for (index = 0U; index < profile->startup_function_count; index++) {
        const PorpoiseRecoveryTitleStartupFunction *startup =
            &profile->startup_functions[index];
        fprintf(file,
                "    state_out->startup_functions[%lu].guest_address = "
                "UINT32_C(0x%08lX);\n",
                (unsigned long)index, (unsigned long)startup->address);
        if ((startup->flags &
             PORPOISE_RECOVERY_TITLE_STARTUP_ESTABLISH_GUEST_MAIN_THREAD_AFTER)
            != 0U) {
            fprintf(file,
                    "    state_out->startup_functions[%lu].flags =\n"
                    "        PORPOISE_TITLE_STARTUP_ESTABLISH_GUEST_MAIN_THREAD_AFTER;\n",
                    (unsigned long)index);
        }
    }
    fprintf(file,
            "    state_out->initial_word_count = %luU;\n",
            (unsigned long)profile->initial_word_count);
    for (index = 0U; index < profile->initial_word_count; index++) {
        const PorpoiseRecoveryTitleInitialWord *word =
            &profile->initial_words[index];
        fprintf(file,
                "    state_out->initial_words[%lu].guest_address = "
                "UINT32_C(0x%08lX);\n"
                "    state_out->initial_words[%lu].value = "
                "UINT32_C(0x%08lX);\n",
                (unsigned long)index, (unsigned long)word->address,
                (unsigned long)index, (unsigned long)word->value);
    }
    fputs("    return PORPOISE_TITLE_HOST_OK;\n}\n", file);
    return title_host_close(file, path, diagnostics);
}

int porpoise_recovery_title_host_generate(
    const PorpoiseRecoveryTarget *target,
    const PorpoiseTranslationPlan *plan,
    const char *output_directory,
    PorpoiseDiagnostics *diagnostics) {
    int result;
    if (output_directory == NULL || output_directory[0] == '\0') {
        return title_host_error(
            diagnostics, PORPOISE_EXIT_USAGE, output_directory, 0U,
            "title-host output directory is required");
    }
    result = porpoise_recovery_title_host_validate(
        target, plan, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    if (!porpoise_make_directories(output_directory, diagnostics) ||
        !title_host_write_meson(output_directory, diagnostics) ||
        !title_host_write_source(
            &target->title_host, output_directory, diagnostics)) {
        return PORPOISE_EXIT_IO;
    }
    return PORPOISE_EXIT_OK;
}
