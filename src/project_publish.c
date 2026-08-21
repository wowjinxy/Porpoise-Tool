#include "project_internal.h"

#include "plan_internal.h"

#include "porpoise/util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <process.h>
#define PORPOISE_GETPID() _getpid()
#else
#include <unistd.h>
#define PORPOISE_GETPID() getpid()
#endif

typedef struct ProjectPublishEntry {
    PorpoiseStagedProject *staged;
    char backup_path[PORPOISE_PATH_CAPACITY];
    bool had_output;
    bool backup_moved;
    bool output_published;
} ProjectPublishEntry;

static bool make_unique_sibling(
    const char *output,
    const char *tag,
    char *path,
    PorpoiseDiagnostics *diagnostics) {
    char parent[PORPOISE_PATH_CAPACITY];
    char base[PORPOISE_PATH_CAPACITY];
    unsigned int attempt;
    unsigned long seed = (unsigned long)time(NULL) ^ (unsigned long)PORPOISE_GETPID();
    if (!porpoise_path_parent(parent, sizeof(parent), output) ||
        !porpoise_path_basename(base, sizeof(base), output) ||
        !porpoise_make_directories(parent, diagnostics)) return false;
    for (attempt = 0U; attempt < 1000U; attempt++) {
        char name[PORPOISE_PATH_CAPACITY];
        if (!porpoise_format(name, sizeof(name), ".%s.porpoise-%s-%08lx-%u",
                             base, tag, seed, attempt) ||
            !porpoise_path_join(path, PORPOISE_PATH_CAPACITY, parent, name)) return false;
        if (!porpoise_path_exists(path)) return true;
    }
    porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, output, 0U, 0U,
                             "cannot allocate a sibling staging path");
    return false;
}

static bool write_publish_journal(
    const char *journal_path,
    const char *state,
    const ProjectPublishEntry *entries,
    size_t entry_count,
    PorpoiseDiagnostics *diagnostics) {
    FILE *journal = fopen(journal_path, "wb");
    size_t index;
    if (journal == NULL) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, journal_path, 0U, 0U,
            "cannot create publication rollback journal: %s",
            strerror(errno));
        return false;
    }
    fputs("{\n  \"schema_version\": 1,\n  \"state\": ", journal);
    porpoise_json_write_string(journal, state);
    fputs(",\n  \"targets\": [", journal);
    for (index = 0U; index < entry_count; index++) {
        const ProjectPublishEntry *entry = &entries[index];
        fputs(index == 0U ? "\n    {\"output\": " :
                            ",\n    {\"output\": ", journal);
        porpoise_json_write_string(
            journal, entry->staged->output_path);
        fputs(", \"stage\": ", journal);
        porpoise_json_write_string(
            journal, entry->staged->stage_path);
        fputs(", \"backup\": ", journal);
        if (entry->had_output) {
            porpoise_json_write_string(journal, entry->backup_path);
        } else {
            fputs("null", journal);
        }
        fprintf(
            journal,
            ", \"backup_moved\": %s, \"output_published\": %s}",
            entry->backup_moved ? "true" : "false",
            entry->output_published ? "true" : "false");
    }
    fputs(entry_count == 0U ? "]\n}\n" : "\n  ]\n}\n", journal);
    return porpoise_project_checked_close(journal, journal_path, diagnostics);
}

static bool publish_batch_cancelled(
    const ProjectPublishEntry *entries,
    size_t entry_count) {
    size_t index;
    for (index = 0U; index < entry_count; index++) {
        if (porpoise_operation_cancelled(
                &entries[index].staged->operation)) {
            return true;
        }
    }
    return false;
}

static bool inspect_output_destination(
    const PorpoiseStagedProject *staged,
    bool *exists_out,
    bool *empty_out,
    PorpoiseDiagnostics *diagnostics) {
    bool exists = porpoise_path_exists(staged->output_path);
    bool empty = false;
    if (exists) {
        if (!porpoise_path_is_directory(staged->output_path)) {
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR,
                staged->output_path, 0U, 0U,
                "output path changed after staging and is not a directory");
            return false;
        }
        empty = porpoise_directory_is_empty(staged->output_path);
    }
    *exists_out = exists;
    *empty_out = empty;
    return true;
}

static bool destination_matches_staged_expectation(
    const PorpoiseStagedProject *staged,
    bool *exists_out,
    PorpoiseDiagnostics *diagnostics) {
    bool exists;
    bool empty;
    if (!inspect_output_destination(
            staged, &exists, &empty, diagnostics)) {
        return false;
    }
    if (!staged->force &&
        (exists != staged->output_expected_exists ||
         (exists && empty != staged->output_expected_empty))) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            staged->output_path, 0U, 0U,
            "output destination changed after staging; refusing to replace it without force");
        return false;
    }
    *exists_out = exists;
    return true;
}

static bool destination_matches_publish_snapshot(
    const ProjectPublishEntry *entry,
    PorpoiseDiagnostics *diagnostics) {
    bool exists;
    if (!destination_matches_staged_expectation(
            entry->staged, &exists, diagnostics)) {
        return false;
    }
    if (exists != entry->had_output) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            entry->staged->output_path, 0U, 0U,
            "output destination changed while publication was being prepared; retry the operation");
        return false;
    }
    return true;
}

static bool destination_is_absent_for_publish(
    const ProjectPublishEntry *entry,
    PorpoiseDiagnostics *diagnostics) {
    if (porpoise_path_exists(entry->staged->output_path)) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR,
            entry->staged->output_path, 0U, 0U,
            "output destination appeared before publication; refusing to overwrite it");
        return false;
    }
    return true;
}

static bool validate_publish_batch(
    PorpoiseStagedProject *const *staged,
    size_t staged_count,
    PorpoiseDiagnostics *diagnostics) {
    size_t left;
    size_t right;
    if (staged == NULL || staged_count == 0U) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, NULL, 0U, 0U,
            "publication batch contains no staged targets");
        return false;
    }
    for (left = 0U; left < staged_count; left++) {
        if (staged[left] == NULL || staged[left]->published ||
            staged[left]->stage_path[0] == '\0' ||
            staged[left]->output_path[0] == '\0' ||
            !porpoise_path_is_directory(staged[left]->stage_path)) {
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_ERROR,
                staged[left] == NULL ? NULL : staged[left]->stage_path,
                0U, 0U,
                "publication batch contains an invalid staged target");
            return false;
        }
        for (right = 0U; right < left; right++) {
            bool overlap = false;
            if (!porpoise_path_trees_overlap(
                    staged[left]->output_path,
                    staged[right]->output_path,
                    &overlap)) {
                porpoise_diagnostics_add(
                    diagnostics, PORPOISE_SEVERITY_ERROR,
                    staged[left]->output_path, 0U, 0U,
                    "cannot compare project output paths safely");
                return false;
            }
            if (overlap) {
                porpoise_diagnostics_add(
                    diagnostics, PORPOISE_SEVERITY_ERROR,
                    staged[left]->output_path, 0U, 0U,
                    "project output paths overlap: %s",
                    staged[right]->output_path);
                return false;
            }
        }
    }
    return true;
}

static bool rollback_publish_batch(
    ProjectPublishEntry *entries,
    size_t entry_count,
    PorpoiseDiagnostics *diagnostics) {
    bool restored = true;
    size_t remaining = entry_count;
    while (remaining != 0U) {
        ProjectPublishEntry *entry = &entries[--remaining];
        if (entry->output_published &&
            porpoise_path_exists(entry->staged->output_path)) {
            if (!porpoise_remove_tree(
                    entry->staged->output_path, diagnostics)) {
                restored = false;
                continue;
            }
            entry->output_published = false;
        }
        if (entry->backup_moved) {
            if (!porpoise_move_path(
                    entry->backup_path,
                    entry->staged->output_path,
                    diagnostics)) {
                porpoise_diagnostics_add(
                    diagnostics, PORPOISE_SEVERITY_ERROR,
                    entry->staged->output_path, 0U, 0U,
                    "publication rollback could not restore the previous output from %s",
                    entry->backup_path);
                restored = false;
            } else {
                entry->backup_moved = false;
            }
        }
    }
    return restored;
}

int porpoise_project_prepare_output(ProjectContext *context) {
    char base[PORPOISE_PATH_CAPACITY];
    const PorpoiseProjectOptions *options = context->options;
    PorpoiseDiagnostics *diagnostics = context->diagnostics;
    bool output_exists = porpoise_path_exists(options->output_path);

    context->output_expected_exists = output_exists;
    context->output_expected_empty = false;
    if (output_exists) {
        if (!porpoise_path_is_directory(options->output_path)) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, options->output_path, 0U, 0U,
                                     "output path exists and is not a directory");
            return PORPOISE_EXIT_IO;
        }
        context->output_expected_empty =
            porpoise_directory_is_empty(options->output_path);
        if (!context->output_expected_empty && !options->force) {
            porpoise_diagnostics_add(diagnostics, PORPOISE_SEVERITY_ERROR, options->output_path, 0U, 0U,
                                     "output directory is not empty; use --force to replace it");
            return PORPOISE_EXIT_USAGE;
        }
    }
    if (!porpoise_path_basename(base, sizeof(base), options->output_path)) {
        return PORPOISE_EXIT_INTERNAL;
    }
    porpoise_sanitize_identifier(
        base, context->project_name, sizeof(context->project_name));
    return PORPOISE_EXIT_OK;
}

int porpoise_project_stage_context(
    ProjectContext *context,
    PorpoiseStagedProject **staged_out) {
    PorpoiseStagedProject *staged;
    PorpoiseOperationCallbacks operation;
    bool force;
    bool generated;
    if (staged_out == NULL) return PORPOISE_EXIT_INTERNAL;
    *staged_out = NULL;
    porpoise_operation_callbacks_init(&operation);
    if (context->options->operation != NULL) {
        operation = *context->options->operation;
    }
    force = context->options->force;
    if (porpoise_project_generation_cancelled(context)) return PORPOISE_EXIT_CANCELLED;
    if (!porpoise_project_prepare_generation(context)) {
        int result = context->failure_code != PORPOISE_EXIT_OK
                         ? context->failure_code
                         : PORPOISE_EXIT_INTERNAL;
        porpoise_project_release_generation(context);
        return result;
    }
    if (!make_unique_sibling(
            context->options->output_path,
            "stage",
            context->stage,
            context->diagnostics)) {
        porpoise_project_release_generation(context);
        return PORPOISE_EXIT_IO;
    }
    generated = porpoise_project_generate_artifacts(context);
    if (!generated) {
        porpoise_project_release_generation(context);
        if (!porpoise_remove_tree(context->stage, context->diagnostics)) {
            return PORPOISE_EXIT_IO;
        }
        return context->failure_code != PORPOISE_EXIT_OK
                   ? context->failure_code
                   : PORPOISE_EXIT_IO;
    }
    if (porpoise_project_generation_cancelled(context)) {
        porpoise_project_release_generation(context);
        if (!porpoise_remove_tree(context->stage, context->diagnostics)) {
            return PORPOISE_EXIT_IO;
        }
        return PORPOISE_EXIT_CANCELLED;
    }
    porpoise_project_release_generation(context);
    staged = (PorpoiseStagedProject *)calloc(1U, sizeof(*staged));
    if (staged == NULL ||
        !porpoise_copy_string(
            staged->output_path, sizeof(staged->output_path),
            context->options->output_path) ||
        !porpoise_copy_string(
            staged->stage_path, sizeof(staged->stage_path),
            context->stage)) {
        free(staged);
        (void)porpoise_remove_tree(context->stage, context->diagnostics);
        return PORPOISE_EXIT_INTERNAL;
    }
    staged->operation = operation;
    staged->force = force;
    staged->output_expected_exists = context->output_expected_exists;
    staged->output_expected_empty = context->output_expected_empty;
    *staged_out = staged;
    return PORPOISE_EXIT_OK;
}

int porpoise_project_stage_plan(
    const PorpoiseTranslationPlan *plan,
    const PorpoiseProjectOptions *options,
    PorpoiseReport *report,
    PorpoiseStagedProject **staged_out,
    PorpoiseDiagnostics *diagnostics) {
    ProjectContext context;
    const PorpoiseSession *session;
    const PorpoiseFunctionPlanView *entry;
    int result;

    if (plan == NULL || options == NULL || options->output_path == NULL ||
        options->runtime_directory == NULL || report == NULL ||
        staged_out == NULL || diagnostics == NULL) {
        return PORPOISE_EXIT_INTERNAL;
    }
    *staged_out = NULL;
    result = porpoise_plan_validate(plan, diagnostics);
    if (result != PORPOISE_EXIT_OK) return result;
    session = porpoise_plan_session(plan);
    if (session == NULL || porpoise_session_program(session) == NULL ||
        porpoise_plan_effective_abi(plan) == NULL ||
        porpoise_plan_analysis_snapshot(plan) == NULL) {
        return PORPOISE_EXIT_INTERNAL;
    }

    memset(&context, 0, sizeof(context));
    context.program = porpoise_session_program(session);
    context.abi = porpoise_plan_effective_abi(plan);
    context.options = options;
    context.report = report;
    context.diagnostics = diagnostics;
    context.plan = plan;
    context.analysis = porpoise_plan_analysis_snapshot(plan);
    entry = porpoise_plan_entry(plan);
    context.entry = entry == NULL ? NULL : entry->function;

    result = porpoise_project_prepare_output(&context);
    if (result != PORPOISE_EXIT_OK) return result;
    return porpoise_project_stage_context(&context, staged_out);
}

const char *porpoise_staged_project_output_path(
    const PorpoiseStagedProject *staged) {
    return staged == NULL ? NULL : staged->output_path;
}

const char *porpoise_staged_project_stage_path(
    const PorpoiseStagedProject *staged) {
    return staged == NULL ? NULL : staged->stage_path;
}

int porpoise_project_publish_batch(
    PorpoiseStagedProject *const *staged,
    size_t staged_count,
    PorpoiseDiagnostics *diagnostics) {
    ProjectPublishEntry *entries;
    char journal_path[PORPOISE_PATH_CAPACITY];
    bool journal_written = false;
    bool rollback_ok;
    size_t index;
    int failure = PORPOISE_EXIT_IO;

    if (diagnostics == NULL) return PORPOISE_EXIT_INTERNAL;
    if (!validate_publish_batch(staged, staged_count, diagnostics)) {
        return PORPOISE_EXIT_USAGE;
    }
    entries = (ProjectPublishEntry *)calloc(staged_count, sizeof(*entries));
    if (entries == NULL) return PORPOISE_EXIT_INTERNAL;
    for (index = 0U; index < staged_count; index++) {
        bool output_exists;
        entries[index].staged = staged[index];
        if (!destination_matches_staged_expectation(
                staged[index], &output_exists, diagnostics)) {
            free(entries);
            return PORPOISE_EXIT_USAGE;
        }
        entries[index].had_output = output_exists;
        if (entries[index].had_output &&
            !make_unique_sibling(
                staged[index]->output_path, "backup",
                entries[index].backup_path, diagnostics)) {
            free(entries);
            return PORPOISE_EXIT_IO;
        }
    }
    if (!make_unique_sibling(
            staged[0]->output_path, "journal", journal_path,
            diagnostics) ||
        !write_publish_journal(
            journal_path, "prepared", entries, staged_count,
            diagnostics)) {
        free(entries);
        return PORPOISE_EXIT_IO;
    }
    journal_written = true;

    for (index = 0U; index < staged_count; index++) {
        if (!destination_matches_publish_snapshot(
                &entries[index], diagnostics)) {
            failure = PORPOISE_EXIT_USAGE;
            goto rollback;
        }
    }
    if (publish_batch_cancelled(entries, staged_count)) {
        failure = PORPOISE_EXIT_CANCELLED;
        goto rollback;
    }
    porpoise_operation_progress(
        &entries[0].staged->operation,
        PORPOISE_PHASE_PUBLISH, 0U, staged_count,
        entries[0].staged->output_path);
    for (index = 0U; index < staged_count; index++) {
        if (entries[index].had_output) {
            if (!destination_matches_publish_snapshot(
                    &entries[index], diagnostics)) {
                failure = PORPOISE_EXIT_USAGE;
                goto rollback;
            }
            if (!porpoise_move_path(
                    entries[index].staged->output_path,
                    entries[index].backup_path, diagnostics)) {
                goto rollback;
            }
            entries[index].backup_moved = true;
            if (!write_publish_journal(
                    journal_path, "backing_up", entries,
                    staged_count, diagnostics)) {
                goto rollback;
            }
        }
        if (publish_batch_cancelled(entries, staged_count)) {
            failure = PORPOISE_EXIT_CANCELLED;
            goto rollback;
        }
    }

    for (index = 0U; index < staged_count; index++) {
        if (!destination_is_absent_for_publish(
                &entries[index], diagnostics)) {
            failure = PORPOISE_EXIT_USAGE;
            goto rollback;
        }
        if (!porpoise_move_path(
                entries[index].staged->stage_path,
                entries[index].staged->output_path, diagnostics)) {
            goto rollback;
        }
        entries[index].output_published = true;
        if (!write_publish_journal(
                journal_path, "publishing", entries, staged_count,
                diagnostics)) {
            goto rollback;
        }
        porpoise_operation_progress(
            &entries[0].staged->operation,
            PORPOISE_PHASE_PUBLISH, index + 1U, staged_count,
            entries[index].staged->output_path);
        if (publish_batch_cancelled(entries, staged_count)) {
            failure = PORPOISE_EXIT_CANCELLED;
            goto rollback;
        }
    }

    for (index = 0U; index < staged_count; index++) {
        entries[index].staged->published = true;
        porpoise_operation_progress(
            &entries[index].staged->operation,
            PORPOISE_PHASE_PUBLISH, staged_count, staged_count,
            entries[index].staged->output_path);
    }
    (void)write_publish_journal(
        journal_path, "published", entries, staged_count, diagnostics);
    for (index = 0U; index < staged_count; index++) {
        if (entries[index].backup_moved) {
            PorpoiseDiagnostics cleanup_diagnostics;
            porpoise_diagnostics_init(&cleanup_diagnostics);
            if (!porpoise_remove_tree(
                    entries[index].backup_path,
                    &cleanup_diagnostics)) {
                porpoise_diagnostics_add(
                    diagnostics, PORPOISE_SEVERITY_WARNING,
                    entries[index].backup_path, 0U, 0U,
                    "generated output was published, but its recoverable backup could not be removed");
            }
            porpoise_diagnostics_free(&cleanup_diagnostics);
        }
    }
    if (remove(journal_path) != 0) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_WARNING, journal_path,
            0U, 0U,
            "outputs were published, but the completed rollback journal could not be removed");
    }
    free(entries);
    return PORPOISE_EXIT_OK;

rollback:
    if (failure == PORPOISE_EXIT_CANCELLED) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_INFO, journal_path,
            0U, 0U,
            "multi-target publication was cancelled; restoring previous outputs");
    }
    rollback_ok = rollback_publish_batch(
        entries, staged_count, diagnostics);
    if (journal_written && rollback_ok) {
        (void)write_publish_journal(
            journal_path, "rolled_back", entries, staged_count,
            diagnostics);
        if (remove(journal_path) != 0) {
            porpoise_diagnostics_add(
                diagnostics, PORPOISE_SEVERITY_WARNING, journal_path,
                0U, 0U,
                "publication was rolled back, but its completed journal could not be removed");
        }
    } else if (!rollback_ok) {
        porpoise_diagnostics_add(
            diagnostics, PORPOISE_SEVERITY_ERROR, journal_path,
            0U, 0U,
            "publication rollback is incomplete; preserve this journal and its backup paths for recovery");
    }
    free(entries);
    return rollback_ok ? failure : PORPOISE_EXIT_IO;
}

int porpoise_project_publish_staged(
    PorpoiseStagedProject *staged,
    PorpoiseDiagnostics *diagnostics) {
    PorpoiseStagedProject *batch[1];
    batch[0] = staged;
    return porpoise_project_publish_batch(batch, 1U, diagnostics);
}

void porpoise_staged_project_free(PorpoiseStagedProject *staged) {
    if (staged == NULL) return;
    if (!staged->published && staged->stage_path[0] != '\0' &&
        porpoise_path_exists(staged->stage_path)) {
        PorpoiseDiagnostics cleanup_diagnostics;
        porpoise_diagnostics_init(&cleanup_diagnostics);
        (void)porpoise_remove_tree(
            staged->stage_path, &cleanup_diagnostics);
        porpoise_diagnostics_free(&cleanup_diagnostics);
    }
    free(staged);
}
