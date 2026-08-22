#include "sdk_guest_layout_internal.h"

#include <ctype.h>
#include <string.h>

#define PORPOISE_EXACT_OS_INIT_IDENTITY "os.a/OS.c/OSInit"
#define PORPOISE_EXACT_OS_INIT_ADAPTER \
    "porpoise_libporpoise_os_init_adapter"

typedef struct PorpoiseSdkGuestSymbolCandidate {
    bool found;
    bool ambiguous;
    bool invalid;
    uint32_t address;
} PorpoiseSdkGuestSymbolCandidate;

typedef struct PorpoiseSdkGuestRequiredSymbol {
    const char *name;
    uint32_t *destination;
} PorpoiseSdkGuestRequiredSymbol;

static bool parse_hex_address_suffix(
    const char *text,
    uint32_t *value_out) {
    uint32_t value = 0U;
    size_t index;

    if (text == NULL || value_out == NULL || strlen(text) != 8U) {
        return false;
    }
    for (index = 0U; index < 8U; index++) {
        unsigned char character = (unsigned char)text[index];
        unsigned int digit;
        if (character >= (unsigned char)'0' &&
            character <= (unsigned char)'9') {
            digit = (unsigned int)(character - (unsigned char)'0');
        } else {
            character = (unsigned char)tolower(character);
            if (character < (unsigned char)'a' ||
                character > (unsigned char)'f') {
                return false;
            }
            digit = 10U +
                    (unsigned int)(character - (unsigned char)'a');
        }
        value = (value << 4U) | (uint32_t)digit;
    }
    *value_out = value;
    return true;
}

static bool data_symbol_name_matches(
    const char *name,
    const char *required_name,
    uint32_t address) {
    size_t required_length;
    uint32_t suffix_address;

    if (name == NULL || required_name == NULL) return false;
    if (strcmp(name, required_name) == 0) return true;
    required_length = strlen(required_name);
    return strncmp(name, required_name, required_length) == 0 &&
           name[required_length] == '_' &&
           parse_hex_address_suffix(
               name + required_length + 1U, &suffix_address) &&
           suffix_address == address;
}

static bool data_symbol_entry_is_global(
    const PorpoiseProgramDataSymbolIndexEntry *entry) {
    return entry->alias != NULL
               ? entry->alias->is_global
               : entry->object->is_global;
}

static uint32_t data_symbol_entry_address(
    const PorpoiseProgramDataSymbolIndexEntry *entry) {
    return entry->alias != NULL
               ? entry->alias->address
               : entry->object->address;
}

static void add_symbol_candidate(
    PorpoiseSdkGuestSymbolCandidate *candidate,
    uint32_t address,
    bool materializes_word) {
    if ((address & UINT32_C(3)) != 0U || !materializes_word) {
        candidate->invalid = true;
        return;
    }
    if (!candidate->found) {
        candidate->found = true;
        candidate->address = address;
    } else if (candidate->address != address) {
        candidate->ambiguous = true;
    }
}

static bool program_materializes_data_word(
    const PorpoiseProgram *program,
    uint32_t address) {
    uint64_t cursor = address;
    const uint64_t end = cursor + sizeof(uint32_t);

    if (end > (UINT64_C(1) << 32)) return false;
    while (cursor < end) {
        uint64_t next = cursor;
        size_t index;
        for (index = 0U; index < program->data_span_count; index++) {
            const PorpoiseDataSpan *span = &program->data_spans[index];
            uint64_t span_start = span->address;
            uint64_t span_end = span_start + span->size;
            if (span_start <= cursor && cursor < span_end &&
                span_end > next) {
                next = span_end < end ? span_end : end;
            }
        }
        if (next == cursor) return false;
        cursor = next;
    }
    return true;
}

static PorpoiseSdkGuestLayoutResolution resolve_required_symbol(
    const PorpoiseProgram *program,
    const PorpoiseSourceFile *preferred_source,
    const char *required_name,
    uint32_t *address_out) {
    PorpoiseSdkGuestSymbolCandidate local;
    PorpoiseSdkGuestSymbolCandidate global;
    const PorpoiseSdkGuestSymbolCandidate *selected;
    size_t index;

    memset(&local, 0, sizeof(local));
    memset(&global, 0, sizeof(global));
    for (index = 0U; index < program->data_symbol_index_count; index++) {
        const PorpoiseProgramDataSymbolIndexEntry *entry =
            &program->data_symbol_index[index];
        uint32_t address = data_symbol_entry_address(entry);

        if (!data_symbol_name_matches(
                entry->name, required_name, address)) {
            continue;
        }
        if (entry->file == preferred_source) {
            add_symbol_candidate(
                &local,
                address,
                program_materializes_data_word(program, address));
        } else if (data_symbol_entry_is_global(entry)) {
            add_symbol_candidate(
                &global,
                address,
                program_materializes_data_word(program, address));
        }
    }

    selected = local.found || local.invalid ? &local : &global;
    if (selected->invalid) return PORPOISE_SDK_GUEST_LAYOUT_INVALID;
    if (selected->ambiguous) {
        return PORPOISE_SDK_GUEST_LAYOUT_AMBIGUOUS;
    }
    if (!selected->found) return PORPOISE_SDK_GUEST_LAYOUT_MISSING;
    *address_out = selected->address;
    return PORPOISE_SDK_GUEST_LAYOUT_RESOLVED;
}

bool porpoise_sdk_guest_os_init_requires_layout(
    const PorpoiseFunctionPlanView *view) {
    return view != NULL &&
           view->action == PORPOISE_PLAN_ACTION_IMPORT &&
           view->binding != NULL &&
           view->binding->adapter != NULL &&
           view->sdk_entry != NULL &&
           view->confidence == PORPOISE_MATCH_CONFIDENCE_EXACT &&
           (view->evidence_flags & PORPOISE_PLAN_EVIDENCE_SIGNATURE) != 0U &&
           view->canonical_sdk_identity != NULL &&
           strcmp(
               view->binding->adapter,
               PORPOISE_EXACT_OS_INIT_ADAPTER) == 0 &&
           strcmp(
               view->canonical_sdk_identity,
               PORPOISE_EXACT_OS_INIT_IDENTITY) == 0;
}

PorpoiseSdkGuestLayoutResolution porpoise_sdk_guest_os_layout_resolve(
    const PorpoiseProgram *program,
    const PorpoiseSourceFile *os_init_source,
    PorpoiseSdkGuestOsLayout *layout_out,
    const char **problem_symbol_out) {
    PorpoiseSdkGuestRequiredSymbol required[6];
    PorpoiseSdkGuestOsLayout candidate;
    size_t index;

    if (layout_out != NULL) memset(layout_out, 0, sizeof(*layout_out));
    if (problem_symbol_out != NULL) *problem_symbol_out = NULL;
    if (program == NULL || os_init_source == NULL || layout_out == NULL) {
        return PORPOISE_SDK_GUEST_LAYOUT_INVALID;
    }
    memset(&candidate, 0, sizeof(candidate));
    required[0].name = "__OSArenaLo";
    required[0].destination = &candidate.arena_lo;
    required[1].name = "__OSArenaHi";
    required[1].destination = &candidate.arena_hi;
    required[2].name = "AreWeInitialized";
    required[2].destination = &candidate.initialized;
    required[3].name = "BootInfo";
    required[3].destination = &candidate.boot_info;
    required[4].name = "BI2DebugFlag";
    required[4].destination = &candidate.bi2_debug_flag;
    required[5].name = "__DVDLongFileNameFlag";
    required[5].destination = &candidate.dvd_long_file_name_flag;

    for (index = 0U; index < sizeof(required) / sizeof(required[0]); index++) {
        PorpoiseSdkGuestLayoutResolution result =
            resolve_required_symbol(
                program,
                os_init_source,
                required[index].name,
                required[index].destination);
        if (result != PORPOISE_SDK_GUEST_LAYOUT_RESOLVED) {
            if (problem_symbol_out != NULL) {
                *problem_symbol_out = required[index].name;
            }
            return result;
        }
    }
    {
        const uint32_t addresses[] = {
            candidate.arena_lo,
            candidate.arena_hi,
            candidate.initialized,
            candidate.boot_info,
            candidate.bi2_debug_flag,
            candidate.dvd_long_file_name_flag
        };
        size_t left;
        for (left = 0U;
             left < sizeof(addresses) / sizeof(addresses[0]);
             left++) {
            size_t right;
            for (right = left + 1U;
                 right < sizeof(addresses) / sizeof(addresses[0]);
                 right++) {
                if (addresses[left] == addresses[right]) {
                    return PORPOISE_SDK_GUEST_LAYOUT_INVALID;
                }
            }
        }
    }
    *layout_out = candidate;
    return PORPOISE_SDK_GUEST_LAYOUT_RESOLVED;
}

PorpoiseSdkGuestLayoutResolution porpoise_sdk_guest_os_layout_for_plan(
    const PorpoiseTranslationPlan *plan,
    PorpoiseSdkGuestOsLayout *layout_out,
    const PorpoiseSourceFile **os_init_source_out,
    const char **problem_symbol_out) {
    const PorpoiseProgram *program;
    const PorpoiseSourceFile *source = NULL;
    PorpoiseSdkGuestOsLayout resolved;
    bool required = false;
    size_t index;

    if (layout_out != NULL) memset(layout_out, 0, sizeof(*layout_out));
    if (os_init_source_out != NULL) *os_init_source_out = NULL;
    if (problem_symbol_out != NULL) *problem_symbol_out = NULL;
    if (plan == NULL || layout_out == NULL) {
        return PORPOISE_SDK_GUEST_LAYOUT_NOT_REQUIRED;
    }
    program = porpoise_session_program(porpoise_plan_session(plan));
    if (program == NULL) return PORPOISE_SDK_GUEST_LAYOUT_INVALID;

    memset(&resolved, 0, sizeof(resolved));
    for (index = 0U; index < porpoise_plan_function_count(plan); index++) {
        const PorpoiseFunctionPlanView *view =
            porpoise_plan_function_at(plan, index);
        PorpoiseSdkGuestOsLayout candidate;
        PorpoiseSdkGuestLayoutResolution result;

        if (!porpoise_sdk_guest_os_init_requires_layout(view)) continue;
        if (os_init_source_out != NULL) {
            *os_init_source_out = view->source;
        }
        result = porpoise_sdk_guest_os_layout_resolve(
            program, view->source, &candidate, problem_symbol_out);
        if (result != PORPOISE_SDK_GUEST_LAYOUT_RESOLVED) return result;
        if (!required) {
            required = true;
            source = view->source;
            resolved = candidate;
        } else if (memcmp(&resolved, &candidate, sizeof(resolved)) != 0) {
            if (problem_symbol_out != NULL) *problem_symbol_out = "OSInit";
            return PORPOISE_SDK_GUEST_LAYOUT_AMBIGUOUS;
        }
    }
    if (!required) return PORPOISE_SDK_GUEST_LAYOUT_NOT_REQUIRED;
    *layout_out = resolved;
    if (os_init_source_out != NULL) *os_init_source_out = source;
    return PORPOISE_SDK_GUEST_LAYOUT_RESOLVED;
}
