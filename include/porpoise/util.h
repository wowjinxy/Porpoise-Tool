#ifndef PORPOISE_UTIL_H
#define PORPOISE_UTIL_H

#include "porpoise/common.h"

#include <stdio.h>

char *porpoise_strdup(const char *value);
bool porpoise_grow_array(void **items, size_t *capacity, size_t item_size, size_t minimum);
void porpoise_trim(char *text);
bool porpoise_copy_string(char *destination, size_t capacity, const char *source);
bool porpoise_format(char *destination, size_t capacity, const char *format, ...);
bool porpoise_path_join(char *destination, size_t capacity, const char *left, const char *right);
bool porpoise_path_parent(char *destination, size_t capacity, const char *path);
bool porpoise_path_basename(char *destination, size_t capacity, const char *path);
bool porpoise_path_without_extension(char *destination, size_t capacity, const char *path);
bool porpoise_path_is_absolute(const char *path);
bool porpoise_path_exists(const char *path);
bool porpoise_path_is_directory(const char *path);
bool porpoise_path_normalize_lexical(char *destination, size_t capacity, const char *path);
bool porpoise_path_contains_path(const char *parent, const char *child, bool *contains);
bool porpoise_path_trees_overlap(const char *left, const char *right, bool *overlap);
bool porpoise_directory_is_empty(const char *path);
bool porpoise_make_directories(const char *path, PorpoiseDiagnostics *diagnostics);
bool porpoise_copy_file(const char *source, const char *destination, PorpoiseDiagnostics *diagnostics);
bool porpoise_remove_tree(const char *path, PorpoiseDiagnostics *diagnostics);
bool porpoise_move_path(const char *source, const char *destination, PorpoiseDiagnostics *diagnostics);
bool porpoise_write_all(FILE *file, const char *text);
void porpoise_sanitize_identifier(const char *input, char *output, size_t capacity);
void porpoise_json_write_string(FILE *file, const char *value);

#endif
