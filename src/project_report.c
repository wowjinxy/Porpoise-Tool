#include "project_internal.h"

#include "porpoise/signature.h"
#include "porpoise/util.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void write_data_fixups(
    FILE *output,
    const PorpoiseDataObject *object) {
    size_t fixup_index;
    fputc('[', output);
    for (fixup_index = 0U;
         fixup_index < object->fixup_count;
         fixup_index++) {
        const PorpoiseDataFixup *fixup = &object->fixups[fixup_index];
        if (fixup_index != 0U) fputc(',', output);
        fputs("{\"kind\": ", output);
        porpoise_json_write_string(
            output,
            fixup->kind == PORPOISE_DATA_FIXUP_ABSOLUTE_32
                ? "absolute_32"
                : "rel_target_32");
        fprintf(
            output,
            ", \"line\": %lu, \"offset\": %lu, "
            "\"width\": %u, \"target\": ",
            (unsigned long)fixup->source_line,
            (unsigned long)fixup->offset,
            (unsigned int)fixup->width);
        if (fixup->target_symbol == NULL) {
            fputs("null", output);
        } else {
            porpoise_json_write_string(output, fixup->target_symbol);
        }
        fprintf(output, ", \"target_addend\": %lld, \"base\": ",
                (long long)fixup->target_addend);
        if (fixup->base_symbol == NULL) {
            fputs("null", output);
        } else {
            porpoise_json_write_string(output, fixup->base_symbol);
        }
        fprintf(output, ", \"base_addend\": %lld}",
                (long long)fixup->base_addend);
    }
    fputc(']', output);
}

static void write_json_nullable_string(FILE *output, const char *value) {
    if (value == NULL || value[0] == '\0') fputs("null", output);
    else porpoise_json_write_string(output, value);
}

bool porpoise_project_write_report(ProjectContext *context) {
    char full[PORPOISE_PATH_CAPACITY];
    FILE *output = porpoise_project_open_generated_file(context, "porpoise-report.json", full);
    size_t file_index;
    size_t function_index;
    size_t object_index;
    size_t anonymous_index;
    size_t span_index;
    size_t instruction_index;
    size_t diagnostic_index;
    size_t data_object_count = 0U;
    size_t anonymous_data_count = 0U;
    size_t data_fixup_count = 0U;
    uint64_t anonymous_explicit_bytes = 0U;
    uint64_t initialized_data_bytes = 0U;
    uint64_t zero_fill_data_bytes = 0U;
    bool first;
    if (output == NULL) return false;
    for (file_index = 0U;
         file_index < context->program->file_count;
         file_index++) {
        const PorpoiseSourceFile *file = &context->program->files[file_index];
        data_object_count += file->data_object_count;
        for (object_index = 0U;
             object_index < file->data_object_count;
             object_index++) {
            data_fixup_count += file->data_objects[object_index].fixup_count;
        }
        anonymous_data_count += file->anonymous_data_count;
        for (anonymous_index = 0U;
             anonymous_index < file->anonymous_data_count;
             anonymous_index++) {
            const PorpoiseAnonymousData *anonymous =
                &file->anonymous_data[anonymous_index];
            size_t byte_index;
            data_fixup_count += anonymous->storage.fixup_count;
            for (byte_index = 0U;
                 byte_index < (size_t)anonymous->storage.size;
                 byte_index++) {
                if (anonymous->present[byte_index] != 0U) {
                    anonymous_explicit_bytes++;
                }
            }
        }
    }
    for (span_index = 0U;
         span_index < context->program->data_span_count;
         span_index++) {
        const PorpoiseDataSpan *span =
            &context->program->data_spans[span_index];
        if (span->kind == PORPOISE_DATA_SPAN_INITIALIZED) {
            initialized_data_bytes += (uint64_t)span->size;
        } else if (span->kind == PORPOISE_DATA_SPAN_ZERO_FILL) {
            zero_fill_data_bytes += (uint64_t)span->size;
        }
    }

    fprintf(
        output,
        "{\n  \"schema_version\": 3,\n"
        "  \"target\": {\"id\": ");
    write_json_nullable_string(
        output,
        context->plan != NULL
            ? porpoise_plan_target_id(context->plan)
            : NULL);
    fputs(", \"module\": ", output);
    write_json_nullable_string(
        output,
        context->plan != NULL ? porpoise_plan_module(context->plan) : NULL);
    fputs(", \"plan_digest\": ", output);
    write_json_nullable_string(
        output,
        context->plan != NULL &&
                (porpoise_plan_target_id(context->plan) != NULL ||
                 porpoise_plan_module(context->plan) != NULL ||
                 porpoise_plan_sdk_policy(context->plan) !=
                     PORPOISE_SDK_POLICY_KEEP)
            ? porpoise_plan_digest(context->plan)
            : NULL);
    fputs(", \"sdk_policy\": ", output);
    porpoise_json_write_string(
        output,
        context->plan != NULL
            ? porpoise_sdk_policy_name(
                  porpoise_plan_sdk_policy(context->plan))
            : "keep");
    fprintf(
        output,
        "},\n"
        "  \"data_model\": {\"source\": \"annotated_assembly\", "
        "\"chunks\": %lu},\n"
        "  \"files\": [\n",
        (unsigned long)context->data_chunk_count);
    for (file_index = 0U; file_index < context->program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &context->program->files[file_index];
        fputs(file_index == 0U ? "    {\"input\": " : ",\n    {\"input\": ", output);
        porpoise_json_write_string(output, file->relative_path);
        fputs(", \"generated\": ", output);
        {
            char path[PORPOISE_PATH_CAPACITY];
            porpoise_format(path, sizeof(path), "src/lifted/%s.c", file->output_stem);
            porpoise_json_write_string(output, path);
        }
        fputc('}', output);
    }
    fputs("\n  ],\n  \"functions\": [\n", output);
    first = true;
    for (file_index = 0U; file_index < context->program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &context->program->files[file_index];
        for (function_index = 0U; function_index < file->function_count; function_index++) {
            const PorpoiseFunction *function = &file->functions[function_index];
            const PorpoiseFunctionPlanView *view =
                porpoise_project_find_function_plan(context, function);
            PorpoisePlanAction action = porpoise_project_function_action(context, function);
            PorpoisePlanAction requested_action =
                view != NULL ? view->requested_action : action;
            PorpoisePlanOrigin origin;
            const PorpoiseAbiFunction *fallback_binding = NULL;
            PorpoiseFunctionSignature fallback_signature;
            const PorpoiseFunctionSignature *signature;
            const char *status;
            memset(&fallback_signature, 0, sizeof(fallback_signature));
            if (view != NULL) {
                origin = view->origin;
                signature = &view->signature;
            } else {
                if (action == PORPOISE_PLAN_ACTION_DATA) {
                    origin = PORPOISE_PLAN_ORIGIN_INPUT_DATA;
                } else if (action == PORPOISE_PLAN_ACTION_IMPORT) {
                    origin = PORPOISE_PLAN_ORIGIN_ABI_IMPORT;
                    fallback_binding =
                        porpoise_project_function_import_binding(context, function);
                } else if (action == PORPOISE_PLAN_ACTION_OMIT) {
                    origin = PORPOISE_PLAN_ORIGIN_SKIP_LIST;
                } else {
                    origin = PORPOISE_PLAN_ORIGIN_DEFAULT;
                }
                (void)porpoise_signature_compute(
                    context->program, function, &fallback_signature);
                signature = &fallback_signature;
            }
            switch (action) {
                case PORPOISE_PLAN_ACTION_LIFT: status = "lifted"; break;
                case PORPOISE_PLAN_ACTION_DATA: status = "data"; break;
                case PORPOISE_PLAN_ACTION_IMPORT: status = "imported"; break;
                case PORPOISE_PLAN_ACTION_OMIT: status = "skipped"; break;
                default: status = "unknown"; break;
            }
            fputs(first ? "    {\"symbol\": " : ",\n    {\"symbol\": ", output); first = false;
            porpoise_json_write_string(output, function->name);
            fputs(", \"c_symbol\": ", output); porpoise_json_write_string(output, function->c_name);
            fputs(", \"file\": ", output); porpoise_json_write_string(output, file->relative_path);
            fprintf(output, ", \"address\": %lu, \"size\": %lu, \"status\": \"%s\"",
                    (unsigned long)function->start_address, (unsigned long)function->size,
                    status);
            fputs(", \"requested_action\": ", output);
            porpoise_json_write_string(
                output, porpoise_plan_action_name(requested_action));
            fputs(", \"resolved_action\": ", output);
            porpoise_json_write_string(
                output, porpoise_plan_action_name(action));
            fputs(", \"origin\": ", output);
            porpoise_json_write_string(
                output,
                porpoise_plan_origin_name(origin));
            fputs(", \"canonical_sdk_identity\": ", output);
            write_json_nullable_string(
                output,
                view != NULL ? view->canonical_sdk_identity : NULL);
            fputs(", \"sdk_category\": ", output);
            if (view == NULL || !view->has_sdk_category) fputs("null", output);
            else porpoise_json_write_string(
                output, porpoise_sdk_category_name(view->sdk_category));
            fputs(", \"confidence\": ", output);
            porpoise_json_write_string(
                output,
                porpoise_match_confidence_name(
                    view != NULL ? view->confidence
                                 : PORPOISE_MATCH_CONFIDENCE_NONE));
            fputs(", \"binding\": ", output);
            write_json_nullable_string(
                output,
                view != NULL
                    ? (view->contract_name != NULL
                           ? view->contract_name
                           : (view->binding != NULL
                                  ? view->binding->symbol
                                  : NULL))
                    : (fallback_binding != NULL
                           ? fallback_binding->symbol
                           : NULL));
            fputs(", \"fingerprint\": ", output);
            write_json_nullable_string(
                output,
                signature->digest_hex);
            fprintf(
                output,
                ", \"evidence_flags\": %u, \"conflict\": %s, "
                "\"overridden\": %s, \"override_action\": ",
                view != NULL ? view->evidence_flags : 0U,
                view != NULL &&
                        (view->evidence_flags &
                         PORPOISE_PLAN_EVIDENCE_CONFLICT) != 0U
                    ? "true" : "false",
                view != NULL && view->overridden ? "true" : "false");
            porpoise_json_write_string(
                output,
                porpoise_override_action_name(
                    view != NULL ? view->override_action
                                 : PORPOISE_OVERRIDE_AUTO));
            fputs(", \"blocking_reason\": ", output);
            write_json_nullable_string(
                output,
                view != NULL && view->blocked
                    ? view->blocking_reason
                    : NULL);
            fputs(", \"provenance\": {\"map\": ", output);
            if (view == NULL || view->map_symbol == NULL) {
                fputs("null", output);
            } else {
                fputs("{\"path\": ", output);
                write_json_nullable_string(
                    output, view->map_symbol->provenance.path);
                fprintf(
                    output,
                    ", \"line\": %lu, \"library\": ",
                    (unsigned long)view->map_symbol->provenance.line);
                write_json_nullable_string(output, view->map_symbol->library);
                fputs(", \"object\": ", output);
                write_json_nullable_string(output, view->map_symbol->object);
                fputc('}', output);
            }
            fputs(", \"catalog\": ", output);
            if (view == NULL || view->sdk_entry == NULL) {
                fputs("null", output);
            } else {
                fputs("{\"source\": ", output);
                porpoise_json_write_string(
                    output,
                    porpoise_sdk_catalog_source_kind_name(
                        view->sdk_entry->provenance.source_kind));
                fputs(", \"path\": ", output);
                write_json_nullable_string(
                    output, view->sdk_entry->provenance.path);
                fprintf(
                    output,
                    ", \"line\": %lu}",
                    (unsigned long)view->sdk_entry->provenance.line);
            }
            fputs("}}", output);
        }
    }
    fputs("\n  ],\n  \"data_objects\": [\n", output);
    first = true;
    for (file_index = 0U; file_index < context->program->file_count; file_index++) {
        const PorpoiseSourceFile *file = &context->program->files[file_index];
        for (object_index = 0U;
             object_index < file->data_object_count;
             object_index++) {
            const PorpoiseDataObject *object =
                &file->data_objects[object_index];
            fputs(first ? "    {\"symbol\": " : ",\n    {\"symbol\": ", output);
            first = false;
            porpoise_json_write_string(output, object->name);
            fputs(", \"file\": ", output);
            porpoise_json_write_string(output, file->relative_path);
            fputs(", \"section\": ", output);
            porpoise_json_write_string(output, object->section);
            fprintf(
                output,
                ", \"binding\": \"%s\", \"metadata_line\": %lu, "
                "\"line\": %lu, \"end_line\": %lu, "
                "\"section_offset\": %lu, \"address\": %lu, "
                "\"size\": %lu, \"fixups\": ",
                object->is_global ? "global" : "local",
                (unsigned long)object->metadata_line,
                (unsigned long)object->source_line,
                (unsigned long)object->end_source_line,
                (unsigned long)object->section_offset,
                (unsigned long)object->address,
                (unsigned long)object->size);
            write_data_fixups(output, object);
            fputc('}', output);
        }
    }
    fputs("\n  ],\n  \"anonymous_data\": [\n", output);
    first = true;
    for (file_index = 0U;
         file_index < context->program->file_count;
         file_index++) {
        const PorpoiseSourceFile *file = &context->program->files[file_index];
        for (anonymous_index = 0U;
             anonymous_index < file->anonymous_data_count;
             anonymous_index++) {
            const PorpoiseAnonymousData *anonymous =
                &file->anonymous_data[anonymous_index];
            const PorpoiseDataObject *storage = &anonymous->storage;
            size_t byte_index;
            uint64_t explicit_bytes = 0U;
            uint64_t initialized_bytes = 0U;
            uint64_t zero_fill_bytes = 0U;
            for (byte_index = 0U;
                 byte_index < (size_t)storage->size;
                 byte_index++) {
                if (anonymous->present[byte_index] == 0U) continue;
                explicit_bytes++;
                if (storage->initialized[byte_index] != 0U) {
                    initialized_bytes++;
                } else {
                    zero_fill_bytes++;
                }
            }
            fputs(first ? "    {\"file\": " : ",\n    {\"file\": ", output);
            first = false;
            porpoise_json_write_string(output, file->relative_path);
            fputs(", \"section\": ", output);
            porpoise_json_write_string(output, storage->section);
            fprintf(
                output,
                ", \"line\": %lu, \"address\": %lu, \"size\": %lu, "
                "\"explicit_bytes\": %llu, \"initialized_bytes\": %llu, "
                "\"zero_fill_bytes\": %llu, \"fixups\": ",
                (unsigned long)storage->source_line,
                (unsigned long)storage->address,
                (unsigned long)storage->size,
                (unsigned long long)explicit_bytes,
                (unsigned long long)initialized_bytes,
                (unsigned long long)zero_fill_bytes);
            write_data_fixups(output, storage);
            fputc('}', output);
        }
    }
    fputs("\n  ],\n  \"data_spans\": [\n", output);
    for (span_index = 0U;
         span_index < context->program->data_span_count;
         span_index++) {
        const PorpoiseDataSpan *span =
            &context->program->data_spans[span_index];
        const PorpoiseSourceFile *source_file = NULL;
        const PorpoiseDataObject *source_object = NULL;
        if (span->source_file_index < context->program->file_count) {
            source_file = &context->program->files[span->source_file_index];
            if (span->data_object_index < source_file->data_object_count) {
                source_object = &source_file->data_objects[span->data_object_index];
            }
        }
        fputs(span_index == 0U ? "    {\"kind\": " : ",\n    {\"kind\": ", output);
        porpoise_json_write_string(
            output,
            span->kind == PORPOISE_DATA_SPAN_INITIALIZED
                ? "initialized"
                : "zero_fill");
        fprintf(
            output,
            ", \"address\": %lu, \"size\": %lu, \"file\": ",
            (unsigned long)span->address,
            (unsigned long)span->size);
        if (source_file == NULL) {
            fputs("null", output);
        } else {
            porpoise_json_write_string(output, source_file->relative_path);
        }
        fputs(", \"object\": ", output);
        if (source_object == NULL) {
            fputs("null", output);
        } else {
            porpoise_json_write_string(output, source_object->name);
        }
        fprintf(
            output,
            ", \"line\": %lu, \"contribution_padding\": %s}",
            (unsigned long)span->source_line,
            span->contribution_padding ? "true" : "false");
    }
    fputs("\n  ],\n  \"instructions\": [\n", output);
    for (instruction_index = 0U; instruction_index < context->report->instruction_count; instruction_index++) {
        const PorpoiseInstructionReport *item = &context->report->instructions[instruction_index];
        fputs(instruction_index == 0U ? "    {\"file\": " : ",\n    {\"file\": ", output);
        porpoise_json_write_string(output, item->file);
        fprintf(output, ", \"line\": %lu, \"address\": %lu, \"mnemonic\": ",
                (unsigned long)item->line, (unsigned long)item->address);
        porpoise_json_write_string(output, item->mnemonic);
        fputs(", \"status\": ", output); porpoise_json_write_string(output, porpoise_lowering_status_name(item->status));
        fprintf(output, ", \"semantic_test\": %s, \"detail\": ", item->semantic_test ? "true" : "false");
        porpoise_json_write_string(output, item->detail == NULL ? "" : item->detail);
        fputc('}', output);
    }
    fputs("\n  ],\n  \"approximations\": [", output);
    first = true;
    for (instruction_index = 0U; instruction_index < context->report->instruction_count; instruction_index++) {
        const PorpoiseInstructionReport *item = &context->report->instructions[instruction_index];
        if (item->status == PORPOISE_APPROXIMATE) {
            if (!first) fputc(',', output);
            fputs("\n    {\"file\": ", output); porpoise_json_write_string(output, item->file);
            fprintf(output, ", \"line\": %lu, \"address\": %lu, \"mnemonic\": ",
                    (unsigned long)item->line, (unsigned long)item->address);
            porpoise_json_write_string(output, item->mnemonic); fputc('}', output); first = false;
        }
    }
    if (!first) fputc('\n', output);
    fputs("  ],\n  \"diagnostics\": [", output);
    for (diagnostic_index = 0U; diagnostic_index < context->diagnostics->count; diagnostic_index++) {
        const PorpoiseDiagnostic *item = &context->diagnostics->items[diagnostic_index];
        if (diagnostic_index != 0U) fputc(',', output);
        fputs("\n    {\"severity\": ", output);
        porpoise_json_write_string(output, item->severity == PORPOISE_SEVERITY_ERROR ? "error" :
                                          item->severity == PORPOISE_SEVERITY_WARNING ? "warning" : "info");
        fputs(", \"file\": ", output); porpoise_json_write_string(output, item->file);
        fprintf(output, ", \"line\": %lu, \"address\": %lu, \"message\": ",
                (unsigned long)item->line, (unsigned long)item->address);
        porpoise_json_write_string(output, item->message); fputc('}', output);
    }
    if (context->diagnostics->count != 0U) fputc('\n', output);
    fputs("  ],\n  \"summary\": {", output);
    fprintf(output, "\"files\": %lu, \"functions\": %lu, \"data_words\": %lu, \"data_objects\": %lu, \"anonymous_contributions\": %lu, \"anonymous_explicit_bytes\": %llu, \"data_fixups\": %lu, \"data_spans\": %lu, \"initialized_data_bytes\": %llu, \"zero_fill_data_bytes\": %llu, \"data_chunks\": %lu, \"lowered\": %lu, \"host_equivalent_noop\": %lu, \"approximate\": %lu, \"unsupported\": %lu}",
            (unsigned long)context->program->file_count,
            (unsigned long)porpoise_project_translated_function_count(context),
            (unsigned long)porpoise_project_data_word_count(context->program),
            (unsigned long)data_object_count,
            (unsigned long)anonymous_data_count,
            (unsigned long long)anonymous_explicit_bytes,
            (unsigned long)data_fixup_count,
            (unsigned long)context->program->data_span_count,
            (unsigned long long)initialized_data_bytes,
            (unsigned long long)zero_fill_data_bytes,
            (unsigned long)context->data_chunk_count,
            (unsigned long)context->report->status_counts[PORPOISE_LOWERED],
            (unsigned long)context->report->status_counts[PORPOISE_HOST_NOOP],
            (unsigned long)context->report->status_counts[PORPOISE_APPROXIMATE],
            (unsigned long)context->report->status_counts[PORPOISE_UNSUPPORTED]);
    fputs("\n}\n", output);
    return porpoise_project_checked_close(output, full, context->diagnostics);
}
