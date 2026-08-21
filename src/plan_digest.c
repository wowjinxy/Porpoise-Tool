#include "plan_internal.h"

#include "porpoise/sha256.h"

#include <string.h>

static void hash_u8(PorpoiseSha256Context *hash, uint8_t value) {
    porpoise_sha256_update(hash, &value, sizeof(value));
}

static void hash_bool(PorpoiseSha256Context *hash, bool value) {
    hash_u8(hash, value ? UINT8_C(1) : UINT8_C(0));
}

static void hash_u32(PorpoiseSha256Context *hash, uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value >> 24U);
    bytes[1] = (uint8_t)(value >> 16U);
    bytes[2] = (uint8_t)(value >> 8U);
    bytes[3] = (uint8_t)value;
    porpoise_sha256_update(hash, bytes, sizeof(bytes));
}

static void hash_u64(PorpoiseSha256Context *hash, uint64_t value) {
    uint8_t bytes[8];
    bytes[0] = (uint8_t)(value >> 56U);
    bytes[1] = (uint8_t)(value >> 48U);
    bytes[2] = (uint8_t)(value >> 40U);
    bytes[3] = (uint8_t)(value >> 32U);
    bytes[4] = (uint8_t)(value >> 24U);
    bytes[5] = (uint8_t)(value >> 16U);
    bytes[6] = (uint8_t)(value >> 8U);
    bytes[7] = (uint8_t)value;
    porpoise_sha256_update(hash, bytes, sizeof(bytes));
}

static void hash_size(PorpoiseSha256Context *hash, size_t value) {
    hash_u64(hash, (uint64_t)value);
}

static void hash_bytes(
    PorpoiseSha256Context *hash,
    const void *bytes,
    size_t size) {
    hash_size(hash, size);
    hash_bool(hash, bytes != NULL);
    if (bytes != NULL && size != 0U) {
        porpoise_sha256_update(hash, bytes, size);
    }
}

static void hash_string(PorpoiseSha256Context *hash, const char *value) {
    hash_bool(hash, value != NULL);
    if (value != NULL) hash_bytes(hash, value, strlen(value));
}

static void hash_abi_value(
    PorpoiseSha256Context *hash,
    const PorpoiseAbiValue *value) {
    hash_u32(hash, (uint32_t)value->type);
    hash_u32(hash, (uint32_t)value->register_class);
    hash_u32(hash, value->register_index);
    hash_string(hash, value->name);
}

static void hash_abi_function(
    PorpoiseSha256Context *hash,
    const PorpoiseAbiFunction *function) {
    size_t index;
    hash_bool(hash, function != NULL);
    if (function == NULL) return;
    hash_u32(hash, (uint32_t)function->kind);
    hash_string(hash, function->symbol);
    hash_string(hash, function->wrapper);
    hash_string(hash, function->header);
    hash_string(hash, function->adapter);
    hash_abi_value(hash, &function->result);
    hash_size(hash, function->argument_count);
    hash_bool(
        hash,
        function->argument_count == 0U || function->arguments != NULL);
    if (function->arguments == NULL) return;
    for (index = 0U; index < function->argument_count; index++) {
        hash_abi_value(hash, &function->arguments[index]);
    }
}

static void hash_abi_manifest(
    PorpoiseSha256Context *hash,
    const PorpoiseAbiManifest *manifest) {
    size_t index;
    hash_bool(hash, manifest != NULL);
    if (manifest == NULL) return;
    hash_size(hash, manifest->function_count);
    hash_bool(
        hash,
        manifest->function_count == 0U || manifest->functions != NULL);
    if (manifest->functions == NULL) return;
    for (index = 0U; index < manifest->function_count; index++) {
        hash_abi_function(hash, &manifest->functions[index]);
    }
}

static void hash_signature(
    PorpoiseSha256Context *hash,
    const PorpoiseFunctionSignature *signature) {
    hash_u32(hash, signature->algorithm_version);
    hash_u32(hash, signature->function_size);
    hash_u32(hash, signature->instruction_count);
    hash_u32(hash, signature->fixed_instruction_count);
    hash_u32(hash, signature->meaningful_fixed_instruction_count);
    hash_u32(hash, signature->relocation_count);
    hash_u32(hash, signature->internal_branch_count);
    hash_u32(hash, signature->external_branch_count);
    hash_u32(hash, signature->external_target_count);
    hash_u32(hash, signature->issue_flags);
    hash_bytes(hash, signature->digest, sizeof(signature->digest));
    hash_string(hash, signature->digest_hex);
}

static void hash_asm_item(
    PorpoiseSha256Context *hash,
    const PorpoiseAsmItem *item) {
    hash_u32(hash, (uint32_t)item->kind);
    hash_size(hash, item->source_line);
    hash_u32(hash, item->address);
    hash_u32(hash, item->word);
    hash_string(hash, item->mnemonic);
    hash_string(hash, item->operands);
    hash_string(hash, item->label);
}

static void hash_address_alias(
    PorpoiseSha256Context *hash,
    const PorpoiseAddressAlias *alias) {
    size_t index;
    hash_bool(hash, alias != NULL);
    if (alias == NULL) return;
    hash_string(hash, alias->name);
    hash_string(hash, alias->c_name);
    hash_string(hash, alias->section);
    hash_bool(hash, alias->is_global);
    hash_bool(hash, alias->is_function_name);
    hash_size(hash, alias->source_line);
    hash_string(hash, alias->source_path);
    hash_u32(hash, alias->address);
    index = alias->instruction_item_index;
    hash_size(hash, index);
}

static void hash_function(
    PorpoiseSha256Context *hash,
    const PorpoiseFunction *function) {
    size_t index;
    hash_string(hash, function->name);
    hash_string(hash, function->c_name);
    hash_string(hash, function->section);
    hash_bool(hash, function->is_global);
    hash_bool(hash, function->skipped);
    hash_bool(hash, function->data_region);
    hash_u32(hash, function->start_address);
    hash_u32(hash, function->size);
    hash_size(hash, function->instruction_count);
    hash_size(hash, function->item_count);
    hash_bool(
        hash, function->item_count == 0U || function->items != NULL);
    if (function->items != NULL) {
        for (index = 0U; index < function->item_count; index++) {
            hash_asm_item(hash, &function->items[index]);
        }
    }
    hash_size(hash, function->alias_count);
    hash_bool(
        hash, function->alias_count == 0U || function->aliases != NULL);
    if (function->aliases != NULL) {
        for (index = 0U; index < function->alias_count; index++) {
            hash_address_alias(hash, &function->aliases[index]);
        }
    }
}

static void hash_data_word(
    PorpoiseSha256Context *hash,
    const PorpoiseDataWord *word) {
    hash_size(hash, word->source_line);
    hash_u32(hash, word->address);
    hash_u32(hash, word->word);
    hash_string(hash, word->directive);
}

static void hash_data_alias(
    PorpoiseSha256Context *hash,
    const PorpoiseDataAlias *alias) {
    hash_bool(hash, alias != NULL);
    if (alias == NULL) return;
    hash_string(hash, alias->name);
    hash_string(hash, alias->section);
    hash_bool(hash, alias->is_global);
    hash_size(hash, alias->source_line);
    hash_u32(hash, alias->address);
}

static void hash_data_fixup(
    PorpoiseSha256Context *hash,
    const PorpoiseDataFixup *fixup) {
    hash_u32(hash, (uint32_t)fixup->kind);
    hash_size(hash, fixup->source_line);
    hash_u32(hash, fixup->offset);
    hash_u8(hash, fixup->width);
    hash_string(hash, fixup->target_symbol);
    hash_u64(hash, (uint64_t)fixup->target_addend);
    hash_string(hash, fixup->base_symbol);
    hash_u64(hash, (uint64_t)fixup->base_addend);
}

static void hash_data_object(
    PorpoiseSha256Context *hash,
    const PorpoiseDataObject *object) {
    size_t index;
    hash_string(hash, object->name);
    hash_string(hash, object->section);
    hash_bool(hash, object->is_global);
    hash_size(hash, object->metadata_line);
    hash_size(hash, object->source_line);
    hash_size(hash, object->end_source_line);
    hash_u32(hash, object->section_offset);
    hash_u32(hash, object->address);
    hash_u32(hash, object->size);
    hash_bytes(hash, object->bytes, object->size);
    hash_bytes(hash, object->initialized, object->size);
    hash_size(hash, object->label_count);
    hash_bool(hash, object->label_count == 0U || object->labels != NULL);
    if (object->labels != NULL) {
        for (index = 0U; index < object->label_count; index++) {
            hash_string(hash, object->labels[index].name);
            hash_size(hash, object->labels[index].source_line);
            hash_u32(hash, object->labels[index].offset);
        }
    }
    hash_size(hash, object->fixup_count);
    hash_bool(hash, object->fixup_count == 0U || object->fixups != NULL);
    if (object->fixups != NULL) {
        for (index = 0U; index < object->fixup_count; index++) {
            hash_data_fixup(hash, &object->fixups[index]);
        }
    }
}

static void hash_source_file(
    PorpoiseSha256Context *hash,
    const PorpoiseSourceFile *source) {
    size_t index;
    hash_string(hash, source->path);
    hash_string(hash, source->relative_path);
    hash_string(hash, source->output_stem);
    hash_size(hash, source->function_count);
    hash_bool(
        hash, source->function_count == 0U || source->functions != NULL);
    if (source->functions != NULL) {
        for (index = 0U; index < source->function_count; index++) {
            hash_function(hash, &source->functions[index]);
        }
    }
    hash_size(hash, source->data_word_count);
    hash_bool(
        hash, source->data_word_count == 0U || source->data_words != NULL);
    if (source->data_words != NULL) {
        for (index = 0U; index < source->data_word_count; index++) {
            hash_data_word(hash, &source->data_words[index]);
        }
    }
    hash_size(hash, source->data_alias_count);
    hash_bool(
        hash,
        source->data_alias_count == 0U || source->data_aliases != NULL);
    if (source->data_aliases != NULL) {
        for (index = 0U; index < source->data_alias_count; index++) {
            hash_data_alias(hash, &source->data_aliases[index]);
        }
    }
    hash_size(hash, source->data_object_count);
    hash_bool(
        hash,
        source->data_object_count == 0U || source->data_objects != NULL);
    if (source->data_objects != NULL) {
        for (index = 0U; index < source->data_object_count; index++) {
            hash_data_object(hash, &source->data_objects[index]);
        }
    }
    hash_size(hash, source->anonymous_data_count);
    hash_bool(
        hash,
        source->anonymous_data_count == 0U ||
            source->anonymous_data != NULL);
    if (source->anonymous_data != NULL) {
        for (index = 0U; index < source->anonymous_data_count; index++) {
            const PorpoiseAnonymousData *anonymous =
                &source->anonymous_data[index];
            hash_data_object(hash, &anonymous->storage);
            hash_bytes(
                hash, anonymous->present, anonymous->storage.size);
        }
    }
}

static bool find_source_reference(
    const PorpoiseProgram *program,
    const PorpoiseSourceFile *source,
    size_t *file_index_out) {
    size_t file_index;
    if (program == NULL || program->files == NULL || source == NULL) {
        return false;
    }
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        if (&program->files[file_index] == source) {
            *file_index_out = file_index;
            return true;
        }
    }
    return false;
}

static bool find_function_reference(
    const PorpoiseProgram *program,
    const PorpoiseFunction *function,
    size_t *file_index_out,
    size_t *function_index_out) {
    size_t file_index;
    if (program == NULL || program->files == NULL || function == NULL) {
        return false;
    }
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *source = &program->files[file_index];
        size_t function_index;
        if (source->functions == NULL) continue;
        for (function_index = 0U;
             function_index < source->function_count;
             function_index++) {
            if (&source->functions[function_index] == function) {
                *file_index_out = file_index;
                *function_index_out = function_index;
                return true;
            }
        }
    }
    return false;
}

static bool find_alias_reference(
    const PorpoiseProgram *program,
    const PorpoiseAddressAlias *alias,
    size_t *file_index_out,
    size_t *function_index_out,
    size_t *alias_index_out) {
    size_t file_index;
    if (program == NULL || program->files == NULL || alias == NULL) {
        return false;
    }
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *source = &program->files[file_index];
        size_t function_index;
        if (source->functions == NULL) continue;
        for (function_index = 0U;
             function_index < source->function_count;
             function_index++) {
            const PorpoiseFunction *function =
                &source->functions[function_index];
            size_t alias_index;
            if (function->aliases == NULL) continue;
            for (alias_index = 0U;
                 alias_index < function->alias_count;
                 alias_index++) {
                if (&function->aliases[alias_index] == alias) {
                    *file_index_out = file_index;
                    *function_index_out = function_index;
                    *alias_index_out = alias_index;
                    return true;
                }
            }
        }
    }
    return false;
}

static bool find_data_object_reference(
    const PorpoiseProgram *program,
    const PorpoiseDataObject *object,
    size_t *file_index_out,
    size_t *object_index_out) {
    size_t file_index;
    if (program == NULL || program->files == NULL || object == NULL) {
        return false;
    }
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *source = &program->files[file_index];
        size_t object_index;
        if (source->data_objects == NULL) continue;
        for (object_index = 0U;
             object_index < source->data_object_count;
             object_index++) {
            if (&source->data_objects[object_index] == object) {
                *file_index_out = file_index;
                *object_index_out = object_index;
                return true;
            }
        }
    }
    return false;
}

static bool find_data_alias_reference(
    const PorpoiseProgram *program,
    const PorpoiseDataAlias *alias,
    size_t *file_index_out,
    size_t *alias_index_out) {
    size_t file_index;
    if (program == NULL || program->files == NULL || alias == NULL) {
        return false;
    }
    for (file_index = 0U; file_index < program->file_count; file_index++) {
        const PorpoiseSourceFile *source = &program->files[file_index];
        size_t alias_index;
        if (source->data_aliases == NULL) continue;
        for (alias_index = 0U;
             alias_index < source->data_alias_count;
             alias_index++) {
            if (&source->data_aliases[alias_index] == alias) {
                *file_index_out = file_index;
                *alias_index_out = alias_index;
                return true;
            }
        }
    }
    return false;
}

static void hash_source_reference(
    PorpoiseSha256Context *hash,
    const PorpoiseProgram *program,
    const PorpoiseSourceFile *source) {
    size_t file_index = 0U;
    bool found;
    hash_bool(hash, source != NULL);
    if (source == NULL) return;
    found = find_source_reference(program, source, &file_index);
    hash_bool(hash, found);
    if (found) hash_size(hash, file_index);
}

static void hash_function_reference(
    PorpoiseSha256Context *hash,
    const PorpoiseProgram *program,
    const PorpoiseFunction *function) {
    size_t file_index = 0U;
    size_t function_index = 0U;
    bool found;
    hash_bool(hash, function != NULL);
    if (function == NULL) return;
    found = find_function_reference(
        program, function, &file_index, &function_index);
    hash_bool(hash, found);
    if (found) {
        hash_size(hash, file_index);
        hash_size(hash, function_index);
    }
}

static void hash_alias_reference(
    PorpoiseSha256Context *hash,
    const PorpoiseProgram *program,
    const PorpoiseAddressAlias *alias) {
    size_t file_index = 0U;
    size_t function_index = 0U;
    size_t alias_index = 0U;
    bool found;
    hash_bool(hash, alias != NULL);
    if (alias == NULL) return;
    found = find_alias_reference(
        program, alias, &file_index, &function_index, &alias_index);
    hash_bool(hash, found);
    if (found) {
        hash_size(hash, file_index);
        hash_size(hash, function_index);
        hash_size(hash, alias_index);
    }
}

static void hash_data_object_reference(
    PorpoiseSha256Context *hash,
    const PorpoiseProgram *program,
    const PorpoiseDataObject *object) {
    size_t file_index = 0U;
    size_t object_index = 0U;
    bool found;
    hash_bool(hash, object != NULL);
    if (object == NULL) return;
    found = find_data_object_reference(
        program, object, &file_index, &object_index);
    hash_bool(hash, found);
    if (found) {
        hash_size(hash, file_index);
        hash_size(hash, object_index);
    }
}

static void hash_data_alias_reference(
    PorpoiseSha256Context *hash,
    const PorpoiseProgram *program,
    const PorpoiseDataAlias *alias) {
    size_t file_index = 0U;
    size_t alias_index = 0U;
    bool found;
    hash_bool(hash, alias != NULL);
    if (alias == NULL) return;
    found = find_data_alias_reference(
        program, alias, &file_index, &alias_index);
    hash_bool(hash, found);
    if (found) {
        hash_size(hash, file_index);
        hash_size(hash, alias_index);
    }
}

static void hash_program(
    PorpoiseSha256Context *hash,
    const PorpoiseProgram *program) {
    size_t index;
    hash_bool(hash, program != NULL);
    if (program == NULL) return;
    hash_size(hash, program->file_count);
    hash_bool(hash, program->file_count == 0U || program->files != NULL);
    if (program->files != NULL) {
        for (index = 0U; index < program->file_count; index++) {
            hash_source_file(hash, &program->files[index]);
        }
    }

    hash_size(hash, program->symbol_index_count);
    hash_bool(
        hash,
        program->symbol_index_count == 0U || program->symbol_index != NULL);
    if (program->symbol_index != NULL) {
        for (index = 0U; index < program->symbol_index_count; index++) {
            const PorpoiseProgramSymbolIndexEntry *entry =
                &program->symbol_index[index];
            hash_string(hash, entry->name);
            hash_source_reference(hash, program, entry->file);
            hash_function_reference(hash, program, entry->function);
            hash_alias_reference(hash, program, entry->alias);
        }
    }

    hash_size(hash, program->label_index_count);
    hash_bool(
        hash,
        program->label_index_count == 0U || program->label_index != NULL);
    if (program->label_index != NULL) {
        for (index = 0U; index < program->label_index_count; index++) {
            const PorpoiseProgramLabelIndexEntry *entry =
                &program->label_index[index];
            hash_string(hash, entry->name);
            hash_source_reference(hash, program, entry->file);
            hash_function_reference(hash, program, entry->function);
            hash_u32(hash, entry->address);
            hash_size(hash, entry->instruction_item_index);
        }
    }

    hash_size(hash, program->data_symbol_index_count);
    hash_bool(
        hash,
        program->data_symbol_index_count == 0U ||
            program->data_symbol_index != NULL);
    if (program->data_symbol_index != NULL) {
        for (index = 0U; index < program->data_symbol_index_count; index++) {
            const PorpoiseProgramDataSymbolIndexEntry *entry =
                &program->data_symbol_index[index];
            hash_string(hash, entry->name);
            hash_source_reference(hash, program, entry->file);
            hash_data_object_reference(hash, program, entry->object);
            hash_data_alias_reference(hash, program, entry->alias);
        }
    }

    hash_size(hash, program->data_span_count);
    hash_bool(
        hash,
        program->data_span_count == 0U || program->data_spans != NULL);
    if (program->data_spans != NULL) {
        for (index = 0U; index < program->data_span_count; index++) {
            const PorpoiseDataSpan *span = &program->data_spans[index];
            hash_u32(hash, (uint32_t)span->kind);
            hash_u32(hash, span->address);
            hash_u32(hash, span->size);
            hash_bytes(hash, span->bytes, span->size);
            hash_size(hash, span->source_file_index);
            hash_size(hash, span->data_object_index);
            hash_size(hash, span->source_line);
            hash_bool(hash, span->contribution_padding);
        }
    }
}

static void hash_symbol(
    PorpoiseSha256Context *hash,
    const PorpoiseSymbol *symbol) {
    hash_bool(hash, symbol != NULL);
    if (symbol == NULL) return;
    hash_string(hash, symbol->name);
    hash_string(hash, symbol->section);
    hash_string(hash, symbol->module);
    hash_string(hash, symbol->object);
    hash_string(hash, symbol->library);
    hash_u32(hash, symbol->address);
    hash_u32(hash, symbol->size);
    hash_bool(hash, symbol->has_address);
    hash_bool(hash, symbol->has_size);
    hash_bool(hash, symbol->used);
    hash_u32(hash, (uint32_t)symbol->kind);
    hash_u32(hash, (uint32_t)symbol->scope);
    hash_u32(hash, (uint32_t)symbol->provenance.kind);
    hash_string(hash, symbol->provenance.path);
    hash_size(hash, symbol->provenance.line);
    hash_string(hash, symbol->provenance.auxiliary_path);
    hash_size(hash, symbol->provenance.auxiliary_line);
}

static void hash_symbol_catalog(
    PorpoiseSha256Context *hash,
    const PorpoiseSymbolCatalog *catalog) {
    size_t index;
    hash_bool(hash, catalog != NULL);
    if (catalog == NULL) return;
    hash_size(hash, catalog->symbol_count);
    hash_bool(
        hash, catalog->symbol_count == 0U || catalog->symbols != NULL);
    if (catalog->symbols == NULL) return;
    for (index = 0U; index < catalog->symbol_count; index++) {
        hash_symbol(hash, &catalog->symbols[index]);
    }
}

static void hash_sdk_entry(
    PorpoiseSha256Context *hash,
    const PorpoiseSdkCatalogEntry *entry) {
    hash_bool(hash, entry != NULL);
    if (entry == NULL) return;
    hash_string(hash, entry->canonical_identity);
    hash_u32(hash, (uint32_t)entry->category);
    hash_string(hash, entry->contract_name);
    hash_signature(hash, &entry->signature);
    hash_u32(hash, (uint32_t)entry->provenance.source_kind);
    hash_string(hash, entry->provenance.path);
    hash_size(hash, entry->provenance.line);
}

static void hash_sdk_catalog(
    PorpoiseSha256Context *hash,
    const PorpoiseSdkCatalog *catalog) {
    size_t index;
    hash_bool(hash, catalog != NULL);
    if (catalog == NULL) return;
    hash_size(hash, catalog->entry_count);
    hash_bool(
        hash, catalog->entry_count == 0U || catalog->entries != NULL);
    if (catalog->entries == NULL) return;
    for (index = 0U; index < catalog->entry_count; index++) {
        hash_sdk_entry(hash, &catalog->entries[index]);
    }
}

static void hash_analysis(
    PorpoiseSha256Context *hash,
    const PorpoiseProgram *program,
    const PorpoiseAnalysis *analysis) {
    size_t index;
    hash_function_reference(hash, program, analysis->entry);
    hash_size(hash, analysis->translated_function_count);
    hash_size(hash, analysis->import_binding_count);
    hash_bool(
        hash,
        analysis->import_binding_count == 0U ||
            analysis->import_bindings != NULL);
    if (analysis->import_bindings == NULL) return;
    for (index = 0U; index < analysis->import_binding_count; index++) {
        const PorpoiseImportBinding *binding =
            &analysis->import_bindings[index];
        hash_abi_function(hash, binding->import);
        hash_function_reference(hash, program, binding->owner);
        hash_alias_reference(hash, program, binding->alias);
        hash_u32(hash, binding->guest_address);
    }
}

static void hash_plan_entry(
    PorpoiseSha256Context *hash,
    const PorpoiseTranslationPlan *plan) {
    size_t index;
    hash_bool(hash, plan->entry != NULL);
    if (plan->entry == NULL) return;
    for (index = 0U; index < plan->function_count; index++) {
        if (&plan->functions[index] == plan->entry) {
            hash_bool(hash, true);
            hash_size(hash, index);
            return;
        }
    }
    hash_bool(hash, false);
}

static void hash_plan_view(
    PorpoiseSha256Context *hash,
    const PorpoiseProgram *program,
    const PorpoiseFunctionPlanView *view) {
    hash_source_reference(hash, program, view->source);
    hash_function_reference(hash, program, view->function);
    hash_u32(hash, (uint32_t)view->action);
    hash_u32(hash, (uint32_t)view->requested_action);
    hash_u32(hash, (uint32_t)view->origin);
    hash_abi_function(hash, view->binding);
    hash_alias_reference(hash, program, view->binding_alias);
    hash_u32(hash, view->binding_address);
    hash_signature(hash, &view->signature);
    hash_symbol(hash, view->map_symbol);
    hash_sdk_entry(hash, view->sdk_entry);
    hash_string(hash, view->canonical_sdk_identity);
    hash_string(hash, view->contract_name);
    hash_u32(hash, (uint32_t)view->sdk_category);
    hash_u32(hash, (uint32_t)view->confidence);
    hash_u32(hash, view->evidence_flags);
    hash_u32(hash, (uint32_t)view->override_action);
    hash_bool(hash, view->has_sdk_category);
    hash_bool(hash, view->overridden);
    hash_bool(hash, view->blocked);
    hash_string(hash, view->blocking_reason);
}

bool porpoise_plan_compute_binding_digest(
    const PorpoiseTranslationPlan *plan,
    char digest_hex[PORPOISE_SHA256_HEX_SIZE]) {
    const PorpoiseProgram *program;
    const PorpoiseAbiManifest *session_abi;
    const PorpoiseSymbolCatalog *symbols;
    const PorpoiseSdkCatalog *sdk_catalog;
    PorpoiseSha256Context hash;
    uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    size_t index;
    if (plan == NULL || plan->session == NULL || digest_hex == NULL) {
        return false;
    }
    program = porpoise_session_program(plan->session);
    session_abi = porpoise_session_abi(plan->session);
    symbols = porpoise_session_symbols(plan->session);
    sdk_catalog = porpoise_session_sdk_catalog(plan->session);
    if (program == NULL || session_abi == NULL || symbols == NULL ||
        sdk_catalog == NULL) {
        return false;
    }

    porpoise_sha256_init(&hash);
    hash_string(&hash, "porpoise-translation-plan-binding-v2");

    /* Session identity: every semantic field exposed to planning/generation. */
    hash_program(&hash, program);
    hash_abi_manifest(&hash, session_abi);
    hash_symbol_catalog(&hash, symbols);
    hash_sdk_catalog(&hash, sdk_catalog);

    /* Selected settings and the resulting immutable plan snapshot. */
    hash_string(&hash, plan->target_id);
    hash_string(&hash, plan->module);
    hash_u32(&hash, (uint32_t)plan->sdk_policy);
    hash_abi_manifest(&hash, &plan->effective_abi);
    hash_analysis(&hash, program, &plan->analysis);
    hash_size(&hash, plan->function_count);
    hash_bool(
        &hash, plan->function_count == 0U || plan->functions != NULL);
    if (plan->functions != NULL) {
        for (index = 0U; index < plan->function_count; index++) {
            hash_plan_view(&hash, program, &plan->functions[index]);
        }
    }
    hash_plan_entry(&hash, plan);
    hash_bool(&hash, plan->blocked);
    hash_string(&hash, plan->blocking_reason);

    porpoise_sha256_final(&hash, digest);
    porpoise_sha256_hex(digest, digest_hex);
    return true;
}
