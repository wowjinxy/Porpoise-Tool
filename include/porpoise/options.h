/**
 * @file options.h
 * @brief Command-line and JSON configuration handling for Porpoise.
 */

#ifndef PORPOISE_OPTIONS_H
#define PORPOISE_OPTIONS_H

#include "porpoise/common.h"
#include "porpoise/plan.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PORPOISE_SYMBOL_CAPACITY
#define PORPOISE_SYMBOL_CAPACITY PORPOISE_NAME_CAPACITY
#endif
#ifndef PORPOISE_TOOL_VERSION
#define PORPOISE_TOOL_VERSION "development"
#endif

/** Options parsing uses the exit values declared in porpoise/common.h. */
typedef int PorpoiseExitCode;
#define PORPOISE_EXIT_SUCCESS PORPOISE_EXIT_OK

typedef enum PorpoiseVerbosity {
    PORPOISE_VERBOSITY_QUIET = -1,
    PORPOISE_VERBOSITY_NORMAL = 0,
    PORPOISE_VERBOSITY_VERBOSE = 1
} PorpoiseVerbosity;

/**
 * Fully owned command-line options. Empty optional strings mean that the
 * corresponding option was not supplied.
 */
typedef struct PorpoiseOptions {
    char input_path[PORPOISE_PATH_CAPACITY];
    char output_path[PORPOISE_PATH_CAPACITY];
    char config_path[PORPOISE_PATH_CAPACITY];
    char abi_path[PORPOISE_PATH_CAPACITY];
    char skip_list_path[PORPOISE_PATH_CAPACITY];
    char map_path[PORPOISE_PATH_CAPACITY];
    char dtk_symbols_path[PORPOISE_PATH_CAPACITY];
    char dtk_splits_path[PORPOISE_PATH_CAPACITY];
    char sdk_catalog_path[PORPOISE_PATH_CAPACITY];
    char module[PORPOISE_NAME_CAPACITY];
    char entry_symbol[PORPOISE_SYMBOL_CAPACITY];
    PorpoiseSdkPolicy sdk_policy;
    PorpoiseVerbosity verbosity;
    bool force;
    bool strict;
    bool show_help;
    bool show_version;
} PorpoiseOptions;

/** Reset an options object to its documented defaults. */
void porpoise_options_init(PorpoiseOptions *options);

/**
 * Parse argv and, when requested, the explicit JSON configuration file.
 *
 * Returns PORPOISE_EXIT_SUCCESS on success (including --help/--version),
 * PORPOISE_EXIT_USAGE for command-line or configuration-schema errors,
 * PORPOISE_EXIT_IO when an explicit config cannot be read, and
 * PORPOISE_EXIT_INTERNAL if parsing memory is unavailable. Diagnostics are
 * written to error_stream when it is non-NULL.
 */
PorpoiseExitCode porpoise_options_parse(PorpoiseOptions *options,
                                        int argc,
                                        char *const argv[],
                                        FILE *error_stream);

void porpoise_options_print_help(FILE *stream, const char *program_name);
void porpoise_options_print_version(FILE *stream);

#ifdef __cplusplus
}
#endif

#endif /* PORPOISE_OPTIONS_H */
