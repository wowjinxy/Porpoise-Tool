/**
 * @file options.c
 * @brief Command-line and strict JSON configuration parsing.
 */

#include "porpoise/options.h"

#include "jsmn.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PORPOISE_CONFIG_MAX_BYTES (1024U * 1024U)

typedef struct CliValues {
    PorpoiseOptions values;
    bool input_seen;
    bool output_seen;
    bool config_seen;
    bool abi_seen;
    bool skip_list_seen;
    bool map_seen;
    bool dtk_symbols_seen;
    bool dtk_splits_seen;
    bool sdk_catalog_seen;
    bool sdk_policy_seen;
    bool module_seen;
    bool entry_seen;
    bool force_seen;
    bool strict_seen;
    bool verbosity_seen;
} CliValues;

typedef enum ConfigKeyFlag {
    CONFIG_KEY_SCHEMA = 1U << 0,
    CONFIG_KEY_ABI = 1U << 1,
    CONFIG_KEY_SKIP_LIST = 1U << 2,
    CONFIG_KEY_ENTRY = 1U << 3,
    CONFIG_KEY_STRICT = 1U << 4,
    CONFIG_KEY_VERBOSITY = 1U << 5,
    CONFIG_KEY_MAP = 1U << 6,
    CONFIG_KEY_DTK_SYMBOLS = 1U << 7,
    CONFIG_KEY_DTK_SPLITS = 1U << 8,
    CONFIG_KEY_SDK_CATALOG = 1U << 9,
    CONFIG_KEY_SDK_POLICY = 1U << 10,
    CONFIG_KEY_MODULE = 1U << 11
} ConfigKeyFlag;

static PorpoiseExitCode option_error(FILE *stream, const char *format, ...) {
    if (stream != NULL) {
        va_list arguments;

        fputs("porpoise: ", stream);
        va_start(arguments, format);
        vfprintf(stream, format, arguments);
        va_end(arguments);
        fputc('\n', stream);
        fputs("Try 'porpoise --help' for usage.\n", stream);
    }
    return PORPOISE_EXIT_USAGE;
}

static PorpoiseExitCode io_error(FILE *stream, const char *format, ...) {
    if (stream != NULL) {
        va_list arguments;

        fputs("porpoise: ", stream);
        va_start(arguments, format);
        vfprintf(stream, format, arguments);
        va_end(arguments);
        fputc('\n', stream);
    }
    return PORPOISE_EXIT_IO;
}

static bool copy_checked(char *destination,
                         size_t capacity,
                         const char *source,
                         const char *description,
                         FILE *error_stream) {
    size_t length;

    if (source == NULL || source[0] == '\0') {
        option_error(error_stream, "%s must not be empty", description);
        return false;
    }
    length = strlen(source);
    if (length >= capacity) {
        option_error(error_stream, "%s is too long (maximum %lu bytes)",
                     description, (unsigned long)(capacity - 1U));
        return false;
    }
    memcpy(destination, source, length + 1U);
    return true;
}

static bool is_help_option(const char *argument) {
    return strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0;
}

static bool is_version_option(const char *argument) {
    return strcmp(argument, "--version") == 0 || strcmp(argument, "-V") == 0;
}

void porpoise_options_init(PorpoiseOptions *options) {
    if (options == NULL) {
        return;
    }
    memset(options, 0, sizeof(*options));
    options->verbosity = PORPOISE_VERBOSITY_NORMAL;
    options->sdk_policy = PORPOISE_SDK_POLICY_KEEP;
}

static bool parse_sdk_policy(
    const char *value,
    PorpoiseSdkPolicy *policy_out) {
    if (strcmp(value, "keep") == 0) {
        *policy_out = PORPOISE_SDK_POLICY_KEEP;
    } else if (strcmp(value, "imported") == 0) {
        *policy_out = PORPOISE_SDK_POLICY_IMPORTED;
    } else if (strcmp(value, "omit") == 0) {
        *policy_out = PORPOISE_SDK_POLICY_OMIT;
    } else {
        return false;
    }
    return true;
}

void porpoise_options_print_help(FILE *stream, const char *program_name) {
    const char *name = program_name;

    if (stream == NULL) {
        return;
    }
    if (name == NULL || name[0] == '\0') {
        name = "porpoise";
    }

    fprintf(stream,
            "Usage: %s INPUT --output DIR [OPTIONS]\n"
            "\n"
            "Translate one annotated PowerPC assembly file, or all .s files\n"
            "beneath an input directory, into a libPorpoise-backed C project.\n"
            "\n"
            "Required:\n"
            "  INPUT                 Assembly file or directory to translate\n"
            "  --output DIR          Destination directory (must be empty)\n"
            "\n"
            "Options:\n"
            "  --config FILE         Load an explicit schema-version 1 JSON config\n"
            "  --abi FILE            ABI manifest for typed external calls\n"
            "  --skip-list FILE      List of functions not to translate\n"
            "  --map FILE            Optional CodeWarrior DOL/REL map\n"
            "  --dtk-symbols FILE    Optional DTK symbols.txt\n"
            "  --dtk-splits FILE     DTK splits.txt paired with --dtk-symbols\n"
            "  --sdk-catalog FILE    Add an exact local SDK signature catalog\n"
            "  --sdk-policy POLICY   keep (default), imported, or omit\n"
            "  --module NAME         Module identity used for map evidence\n"
            "  --entry SYMBOL        Lifted entry point used by DolphinMain\n"
            "  --force               Replace a nonempty destination atomically\n"
            "  --strict              Reject approximate instruction lowerings\n"
            "  --quiet               Suppress non-error output\n"
            "  --verbose             Enable detailed progress output\n"
            "  -h, --help            Show this help and exit\n"
            "  -V, --version         Show version information and exit\n"
            "\n"
            "Config keys: schema_version (required and equal to 1), abi,\n"
            "skip_list, map, dtk_symbols, dtk_splits, sdk_catalog,\n"
            "sdk_policy, module, entry, strict, and verbosity. Config-relative file\n"
            "paths are resolved beside the config file; CLI values override\n"
            "config values. --force is accepted only on the command line.\n",
            name);
}

void porpoise_options_print_version(FILE *stream) {
    if (stream != NULL) {
        fprintf(stream, "porpoise %s\n", PORPOISE_TOOL_VERSION);
    }
}

static PorpoiseExitCode parse_cli(CliValues *cli,
                                  int argc,
                                  char *const argv[],
                                  FILE *error_stream) {
    int index;
    bool positional_only = false;

    porpoise_options_init(&cli->values);

    if (argc == 2 && is_help_option(argv[1])) {
        cli->values.show_help = true;
        return PORPOISE_EXIT_SUCCESS;
    }
    if (argc == 2 && is_version_option(argv[1])) {
        cli->values.show_version = true;
        return PORPOISE_EXIT_SUCCESS;
    }

    for (index = 1; index < argc; index++) {
        const char *argument = argv[index];
        char *target = NULL;
        size_t target_capacity = 0U;
        const char *description = NULL;
        bool *seen = NULL;

        if (!positional_only && strcmp(argument, "--") == 0) {
            positional_only = true;
            continue;
        }

        if (!positional_only && (is_help_option(argument) ||
                                 is_version_option(argument))) {
            return option_error(error_stream,
                                "%s must be used by itself", argument);
        }

        if (!positional_only && strcmp(argument, "--output") == 0) {
            target = cli->values.output_path;
            target_capacity = sizeof(cli->values.output_path);
            description = "output directory";
            seen = &cli->output_seen;
        } else if (!positional_only && strcmp(argument, "--config") == 0) {
            target = cli->values.config_path;
            target_capacity = sizeof(cli->values.config_path);
            description = "config path";
            seen = &cli->config_seen;
        } else if (!positional_only && strcmp(argument, "--abi") == 0) {
            target = cli->values.abi_path;
            target_capacity = sizeof(cli->values.abi_path);
            description = "ABI manifest path";
            seen = &cli->abi_seen;
        } else if (!positional_only && strcmp(argument, "--skip-list") == 0) {
            target = cli->values.skip_list_path;
            target_capacity = sizeof(cli->values.skip_list_path);
            description = "skip-list path";
            seen = &cli->skip_list_seen;
        } else if (!positional_only && strcmp(argument, "--map") == 0) {
            target = cli->values.map_path;
            target_capacity = sizeof(cli->values.map_path);
            description = "CodeWarrior map path";
            seen = &cli->map_seen;
        } else if (!positional_only &&
                   strcmp(argument, "--dtk-symbols") == 0) {
            target = cli->values.dtk_symbols_path;
            target_capacity = sizeof(cli->values.dtk_symbols_path);
            description = "DTK symbols path";
            seen = &cli->dtk_symbols_seen;
        } else if (!positional_only &&
                   strcmp(argument, "--dtk-splits") == 0) {
            target = cli->values.dtk_splits_path;
            target_capacity = sizeof(cli->values.dtk_splits_path);
            description = "DTK splits path";
            seen = &cli->dtk_splits_seen;
        } else if (!positional_only &&
                   strcmp(argument, "--sdk-catalog") == 0) {
            target = cli->values.sdk_catalog_path;
            target_capacity = sizeof(cli->values.sdk_catalog_path);
            description = "SDK catalog path";
            seen = &cli->sdk_catalog_seen;
        } else if (!positional_only && strcmp(argument, "--module") == 0) {
            target = cli->values.module;
            target_capacity = sizeof(cli->values.module);
            description = "module name";
            seen = &cli->module_seen;
        } else if (!positional_only &&
                   strcmp(argument, "--sdk-policy") == 0) {
            if (cli->sdk_policy_seen) {
                return option_error(
                    error_stream,
                    "option --sdk-policy was specified more than once");
            }
            if (index + 1 >= argc) {
                return option_error(
                    error_stream, "option --sdk-policy requires a value");
            }
            cli->sdk_policy_seen = true;
            index++;
            if (!parse_sdk_policy(
                    argv[index], &cli->values.sdk_policy)) {
                return option_error(
                    error_stream,
                    "--sdk-policy must be 'keep', 'imported', or 'omit'");
            }
            continue;
        } else if (!positional_only && strcmp(argument, "--entry") == 0) {
            target = cli->values.entry_symbol;
            target_capacity = sizeof(cli->values.entry_symbol);
            description = "entry symbol";
            seen = &cli->entry_seen;
        } else if (!positional_only && strcmp(argument, "--force") == 0) {
            if (cli->force_seen) {
                return option_error(error_stream,
                                    "option --force was specified more than once");
            }
            cli->force_seen = true;
            cli->values.force = true;
            continue;
        } else if (!positional_only && strcmp(argument, "--strict") == 0) {
            if (cli->strict_seen) {
                return option_error(error_stream,
                                    "option --strict was specified more than once");
            }
            cli->strict_seen = true;
            cli->values.strict = true;
            continue;
        } else if (!positional_only &&
                   (strcmp(argument, "--quiet") == 0 ||
                    strcmp(argument, "--verbose") == 0)) {
            if (cli->verbosity_seen) {
                return option_error(error_stream,
                                    "--quiet and --verbose are mutually exclusive");
            }
            cli->verbosity_seen = true;
            cli->values.verbosity = (strcmp(argument, "--quiet") == 0)
                                        ? PORPOISE_VERBOSITY_QUIET
                                        : PORPOISE_VERBOSITY_VERBOSE;
            continue;
        } else if (!positional_only && argument[0] == '-' && argument[1] != '\0') {
            return option_error(error_stream, "unknown option '%s'", argument);
        } else {
            if (cli->input_seen) {
                return option_error(error_stream,
                                    "unexpected extra argument '%s'", argument);
            }
            if (!copy_checked(cli->values.input_path,
                              sizeof(cli->values.input_path), argument,
                              "input path", error_stream)) {
                return PORPOISE_EXIT_USAGE;
            }
            cli->input_seen = true;
            continue;
        }

        if (*seen) {
            return option_error(error_stream,
                                "option %s was specified more than once", argument);
        }
        if (index + 1 >= argc) {
            return option_error(error_stream,
                                "option %s requires a value", argument);
        }
        *seen = true;
        index++;
        if (!copy_checked(target, target_capacity, argv[index], description,
                          error_stream)) {
            return PORPOISE_EXIT_USAGE;
        }
    }

    if (!cli->input_seen) {
        return option_error(error_stream, "missing INPUT");
    }
    if (!cli->output_seen) {
        return option_error(error_stream, "missing required option --output DIR");
    }
    return PORPOISE_EXIT_SUCCESS;
}

static PorpoiseExitCode read_config(const char *path,
                                    char **contents_out,
                                    size_t *length_out,
                                    FILE *error_stream) {
    FILE *file;
    char *contents;
    size_t length = 0U;
    size_t capacity = 4096U;

    file = fopen(path, "rb");
    if (file == NULL) {
        return io_error(error_stream, "cannot open config '%s': %s",
                        path, strerror(errno));
    }

    contents = (char *)malloc(capacity + 1U);
    if (contents == NULL) {
        fclose(file);
        if (error_stream != NULL) {
            fprintf(error_stream,
                    "porpoise: out of memory while reading config '%s'\n", path);
        }
        return PORPOISE_EXIT_INTERNAL;
    }

    for (;;) {
        size_t amount;

        if (length == capacity) {
            char *larger;
            size_t next_capacity;

            if (capacity == PORPOISE_CONFIG_MAX_BYTES) {
                int extra = fgetc(file);
                if (extra != EOF) {
                    free(contents);
                    fclose(file);
                    return option_error(error_stream,
                                        "config '%s' exceeds the %lu-byte limit",
                                        path,
                                        (unsigned long)PORPOISE_CONFIG_MAX_BYTES);
                }
                if (ferror(file)) {
                    free(contents);
                    fclose(file);
                    return io_error(error_stream,
                                    "failed to read config '%s': %s",
                                    path, strerror(errno));
                }
                break;
            }

            next_capacity = capacity * 2U;
            if (next_capacity > PORPOISE_CONFIG_MAX_BYTES) {
                next_capacity = PORPOISE_CONFIG_MAX_BYTES;
            }
            larger = (char *)realloc(contents, next_capacity + 1U);
            if (larger == NULL) {
                free(contents);
                fclose(file);
                if (error_stream != NULL) {
                    fprintf(error_stream,
                            "porpoise: out of memory while reading config '%s'\n",
                            path);
                }
                return PORPOISE_EXIT_INTERNAL;
            }
            contents = larger;
            capacity = next_capacity;
        }

        amount = fread(contents + length, 1U, capacity - length, file);
        length += amount;
        if (amount == 0U) {
            if (ferror(file)) {
                int saved_errno = errno;
                free(contents);
                fclose(file);
                return io_error(error_stream,
                                "failed to read config '%s': %s",
                                path, strerror(saved_errno));
            }
            break;
        }
    }

    if (fclose(file) != 0) {
        int saved_errno = errno;
        free(contents);
        return io_error(error_stream, "failed to close config '%s': %s",
                        path, strerror(saved_errno));
    }

    contents[length] = '\0';
    *contents_out = contents;
    *length_out = length;
    return PORPOISE_EXIT_SUCCESS;
}

static void skip_json_whitespace(const char *json,
                                 size_t length,
                                 size_t *position) {
    while (*position < length) {
        char c = json[*position];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        (*position)++;
    }
}

static bool validate_flat_object_syntax(const char *json,
                                        size_t length,
                                        const jsmntok_t *tokens,
                                        int token_count) {
    const jsmntok_t *root;
    size_t position = 0U;
    int pair;
    int token_index = 1;

    if (token_count < 1) {
        return false;
    }
    root = &tokens[0];
    if (root->type != JSMN_OBJECT || root->start < 0 || root->end < 0 ||
        root->size < 0 || token_count != 1 + (root->size * 2)) {
        return false;
    }

    skip_json_whitespace(json, length, &position);
    if (position != (size_t)root->start || position >= length ||
        json[position] != '{') {
        return false;
    }
    position++;

    for (pair = 0; pair < root->size; pair++) {
        const jsmntok_t *key = &tokens[token_index];
        const jsmntok_t *value = &tokens[token_index + 1];

        skip_json_whitespace(json, length, &position);
        if (position >= length || json[position] != '\"' ||
            key->type != JSMN_STRING || key->parent != 0 || key->size != 1 ||
            key->start != (int)position + 1 || key->end < key->start ||
            (size_t)key->end >= length || json[key->end] != '\"') {
            return false;
        }
        position = (size_t)key->end + 1U;
        skip_json_whitespace(json, length, &position);
        if (position >= length || json[position] != ':') {
            return false;
        }
        position++;
        skip_json_whitespace(json, length, &position);

        if (value->parent != token_index || value->size != 0) {
            return false;
        }
        if (value->type == JSMN_STRING) {
            if (position >= length || json[position] != '\"' ||
                value->start != (int)position + 1 || value->end < value->start ||
                (size_t)value->end >= length || json[value->end] != '\"') {
                return false;
            }
            position = (size_t)value->end + 1U;
        } else if (value->type == JSMN_PRIMITIVE) {
            if (value->start != (int)position || value->end <= value->start) {
                return false;
            }
            position = (size_t)value->end;
        } else {
            return false;
        }

        skip_json_whitespace(json, length, &position);
        if (pair + 1 < root->size) {
            if (position >= length || json[position] != ',') {
                return false;
            }
            position++;
        } else if (position >= length || json[position] != '}') {
            return false;
        }
        token_index += 2;
    }

    if (root->size == 0) {
        skip_json_whitespace(json, length, &position);
        if (position >= length || json[position] != '}') {
            return false;
        }
    }
    position++;
    if (position != (size_t)root->end) {
        return false;
    }
    skip_json_whitespace(json, length, &position);
    return position == length;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool append_utf8(char *output,
                        size_t capacity,
                        size_t *output_length,
                        uint32_t codepoint) {
    unsigned int count;

    if (codepoint == 0U || codepoint > 0x10ffffU ||
        (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
        return false;
    }
    if (codepoint <= 0x7fU) {
        count = 1U;
    } else if (codepoint <= 0x7ffU) {
        count = 2U;
    } else if (codepoint <= 0xffffU) {
        count = 3U;
    } else {
        count = 4U;
    }
    if (*output_length + count >= capacity) {
        return false;
    }

    if (count == 1U) {
        output[(*output_length)++] = (char)codepoint;
    } else if (count == 2U) {
        output[(*output_length)++] = (char)(0xc0U | (codepoint >> 6));
        output[(*output_length)++] = (char)(0x80U | (codepoint & 0x3fU));
    } else if (count == 3U) {
        output[(*output_length)++] = (char)(0xe0U | (codepoint >> 12));
        output[(*output_length)++] =
            (char)(0x80U | ((codepoint >> 6) & 0x3fU));
        output[(*output_length)++] = (char)(0x80U | (codepoint & 0x3fU));
    } else {
        output[(*output_length)++] = (char)(0xf0U | (codepoint >> 18));
        output[(*output_length)++] =
            (char)(0x80U | ((codepoint >> 12) & 0x3fU));
        output[(*output_length)++] =
            (char)(0x80U | ((codepoint >> 6) & 0x3fU));
        output[(*output_length)++] = (char)(0x80U | (codepoint & 0x3fU));
    }
    return true;
}

static bool valid_utf8(const char *text, size_t length) {
    size_t position = 0U;

    while (position < length) {
        unsigned char first = (unsigned char)text[position++];
        uint32_t codepoint;
        unsigned int remaining;
        uint32_t minimum;

        if (first <= 0x7fU) {
            continue;
        }
        if (first >= 0xc2U && first <= 0xdfU) {
            codepoint = first & 0x1fU;
            remaining = 1U;
            minimum = 0x80U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            codepoint = first & 0x0fU;
            remaining = 2U;
            minimum = 0x800U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            codepoint = first & 0x07U;
            remaining = 3U;
            minimum = 0x10000U;
        } else {
            return false;
        }

        if (position + remaining > length) {
            return false;
        }
        while (remaining-- > 0U) {
            unsigned char next = (unsigned char)text[position++];
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6) | (uint32_t)(next & 0x3fU);
        }
        if (codepoint < minimum || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            return false;
        }
    }
    return true;
}

static bool decode_json_string(const char *json,
                               const jsmntok_t *token,
                               char *output,
                               size_t capacity) {
    size_t input_position = (size_t)token->start;
    size_t input_end = (size_t)token->end;
    size_t output_length = 0U;

    if (capacity == 0U) {
        return false;
    }
    output[0] = '\0';

    while (input_position < input_end) {
        unsigned char c = (unsigned char)json[input_position++];

        if (c != '\\') {
            if (c == 0U || output_length + 1U >= capacity) {
                return false;
            }
            output[output_length++] = (char)c;
            continue;
        }

        if (input_position >= input_end) {
            return false;
        }
        c = (unsigned char)json[input_position++];
        if (c == '\"' || c == '\\' || c == '/') {
            if (output_length + 1U >= capacity) {
                return false;
            }
            output[output_length++] = (char)c;
        } else if (c == 'b' || c == 'f' || c == 'n' || c == 'r' || c == 't') {
            char decoded;
            if (c == 'b') {
                decoded = '\b';
            } else if (c == 'f') {
                decoded = '\f';
            } else if (c == 'n') {
                decoded = '\n';
            } else if (c == 'r') {
                decoded = '\r';
            } else {
                decoded = '\t';
            }
            if (output_length + 1U >= capacity) {
                return false;
            }
            output[output_length++] = decoded;
        } else if (c == 'u') {
            uint32_t codepoint = 0U;
            unsigned int digit;

            if (input_position + 4U > input_end) {
                return false;
            }
            for (digit = 0U; digit < 4U; digit++) {
                int value = hex_value(json[input_position++]);
                if (value < 0) {
                    return false;
                }
                codepoint = (codepoint << 4) | (uint32_t)value;
            }

            if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
                uint32_t low = 0U;
                if (input_position + 6U > input_end ||
                    json[input_position] != '\\' ||
                    json[input_position + 1U] != 'u') {
                    return false;
                }
                input_position += 2U;
                for (digit = 0U; digit < 4U; digit++) {
                    int value = hex_value(json[input_position++]);
                    if (value < 0) {
                        return false;
                    }
                    low = (low << 4) | (uint32_t)value;
                }
                if (low < 0xdc00U || low > 0xdfffU) {
                    return false;
                }
                codepoint = 0x10000U +
                            ((codepoint - 0xd800U) << 10) +
                            (low - 0xdc00U);
            }
            if (!append_utf8(output, capacity, &output_length, codepoint)) {
                return false;
            }
        } else {
            return false;
        }
    }

    if (!valid_utf8(output, output_length)) {
        return false;
    }
    output[output_length] = '\0';
    return true;
}

static bool token_is(const char *json,
                     const jsmntok_t *token,
                     const char *expected) {
    size_t expected_length = strlen(expected);
    size_t token_length;

    if (token->start < 0 || token->end < token->start) {
        return false;
    }
    token_length = (size_t)(token->end - token->start);
    return token_length == expected_length &&
           memcmp(json + token->start, expected, token_length) == 0;
}

static bool path_is_absolute(const char *path) {
#ifdef _WIN32
    unsigned char first = (unsigned char)path[0];

    if (path[0] == '/' || path[0] == '\\') {
        return true;
    }
    return isalpha(first) != 0 && path[1] == ':' &&
           (path[2] == '/' || path[2] == '\\');
#else
    return path[0] == '/';
#endif
}

static bool resolve_config_relative_path(const char *config_path,
                                         const char *value,
                                         char *output,
                                         size_t capacity,
                                         const char *description,
                                         FILE *error_stream) {
    const char *slash;
    const char *separator;
    size_t directory_length;
    size_t value_length = strlen(value);
    bool needs_separator;

    if (path_is_absolute(value)) {
        return copy_checked(output, capacity, value, description, error_stream);
    }

    slash = strrchr(config_path, '/');
#ifdef _WIN32
    {
        const char *backslash = strrchr(config_path, '\\');
        if (slash == NULL) {
            separator = backslash;
        } else if (backslash == NULL || slash > backslash) {
            separator = slash;
        } else {
            separator = backslash;
        }
    }
#else
    separator = slash;
#endif

    if (separator == NULL) {
        directory_length = 1U;
        needs_separator = true;
    } else {
        directory_length = (size_t)(separator - config_path) + 1U;
        needs_separator = false;
    }

    if (directory_length + (needs_separator ? 1U : 0U) + value_length >=
        capacity) {
        option_error(error_stream,
                     "%s resolved relative to the config file is too long",
                     description);
        return false;
    }

    if (separator == NULL) {
        output[0] = '.';
#ifdef _WIN32
        output[1] = '\\';
#else
        output[1] = '/';
#endif
        memcpy(output + 2U, value, value_length + 1U);
    } else {
        memcpy(output, config_path, directory_length);
        memcpy(output + directory_length, value, value_length + 1U);
    }
    return true;
}

static PorpoiseExitCode parse_config(PorpoiseOptions *options,
                                     const char *path,
                                     FILE *error_stream) {
    char *json = NULL;
    size_t length = 0U;
    jsmn_parser parser;
    jsmntok_t *tokens = NULL;
    int token_count;
    PorpoiseExitCode result;
    unsigned int seen_keys = 0U;
    int pair;

    result = read_config(path, &json, &length, error_stream);
    if (result != PORPOISE_EXIT_SUCCESS) {
        return result;
    }

    jsmn_init(&parser);
    token_count = jsmn_parse(&parser, json, length, NULL, 0U);
    if (token_count <= 0) {
        free(json);
        return option_error(error_stream, "config '%s' is not valid JSON", path);
    }

    tokens = (jsmntok_t *)calloc((size_t)token_count, sizeof(*tokens));
    if (tokens == NULL) {
        free(json);
        if (error_stream != NULL) {
            fprintf(error_stream,
                    "porpoise: out of memory while parsing config '%s'\n", path);
        }
        return PORPOISE_EXIT_INTERNAL;
    }

    jsmn_init(&parser);
    token_count = jsmn_parse(&parser, json, length, tokens,
                             (unsigned int)token_count);
    if (token_count <= 0 ||
        !validate_flat_object_syntax(json, length, tokens, token_count)) {
        free(tokens);
        free(json);
        return option_error(error_stream,
                            "config '%s' must be one strict, flat JSON object",
                            path);
    }

    for (pair = 0; pair < tokens[0].size; pair++) {
        const jsmntok_t *key_token = &tokens[1 + (pair * 2)];
        const jsmntok_t *value_token = &tokens[2 + (pair * 2)];
        char key[32];
        unsigned int key_flag;

        if (!decode_json_string(json, key_token, key, sizeof(key))) {
            result = option_error(error_stream,
                                  "config '%s' contains an invalid key", path);
            goto finished;
        }

        if (strcmp(key, "schema_version") == 0) {
            key_flag = CONFIG_KEY_SCHEMA;
        } else if (strcmp(key, "abi") == 0) {
            key_flag = CONFIG_KEY_ABI;
        } else if (strcmp(key, "skip_list") == 0) {
            key_flag = CONFIG_KEY_SKIP_LIST;
        } else if (strcmp(key, "entry") == 0) {
            key_flag = CONFIG_KEY_ENTRY;
        } else if (strcmp(key, "strict") == 0) {
            key_flag = CONFIG_KEY_STRICT;
        } else if (strcmp(key, "verbosity") == 0) {
            key_flag = CONFIG_KEY_VERBOSITY;
        } else if (strcmp(key, "map") == 0) {
            key_flag = CONFIG_KEY_MAP;
        } else if (strcmp(key, "dtk_symbols") == 0) {
            key_flag = CONFIG_KEY_DTK_SYMBOLS;
        } else if (strcmp(key, "dtk_splits") == 0) {
            key_flag = CONFIG_KEY_DTK_SPLITS;
        } else if (strcmp(key, "sdk_catalog") == 0) {
            key_flag = CONFIG_KEY_SDK_CATALOG;
        } else if (strcmp(key, "sdk_policy") == 0) {
            key_flag = CONFIG_KEY_SDK_POLICY;
        } else if (strcmp(key, "module") == 0) {
            key_flag = CONFIG_KEY_MODULE;
        } else {
            result = option_error(error_stream,
                                  "unknown key '%s' in config '%s'", key, path);
            goto finished;
        }

        if ((seen_keys & key_flag) != 0U) {
            result = option_error(error_stream,
                                  "duplicate key '%s' in config '%s'", key, path);
            goto finished;
        }
        seen_keys |= key_flag;

        if (key_flag == CONFIG_KEY_SCHEMA) {
            if (value_token->type != JSMN_PRIMITIVE ||
                !token_is(json, value_token, "1")) {
                result = option_error(error_stream,
                                      "config '%s' requires schema_version 1",
                                      path);
                goto finished;
            }
        } else if (key_flag == CONFIG_KEY_STRICT) {
            if (value_token->type != JSMN_PRIMITIVE ||
                (!token_is(json, value_token, "true") &&
                 !token_is(json, value_token, "false"))) {
                result = option_error(error_stream,
                                      "config key 'strict' must be a boolean");
                goto finished;
            }
            options->strict = token_is(json, value_token, "true");
        } else {
            char decoded[PORPOISE_PATH_CAPACITY];

            if (value_token->type != JSMN_STRING ||
                !decode_json_string(json, value_token, decoded,
                                    sizeof(decoded)) || decoded[0] == '\0') {
                result = option_error(error_stream,
                                      "config key '%s' must be a nonempty string",
                                      key);
                goto finished;
            }

            if (key_flag == CONFIG_KEY_ABI) {
                if (!resolve_config_relative_path(
                        path, decoded, options->abi_path,
                        sizeof(options->abi_path), "ABI manifest path",
                        error_stream)) {
                    result = PORPOISE_EXIT_USAGE;
                    goto finished;
                }
            } else if (key_flag == CONFIG_KEY_SKIP_LIST) {
                if (!resolve_config_relative_path(
                        path, decoded, options->skip_list_path,
                        sizeof(options->skip_list_path), "skip-list path",
                        error_stream)) {
                    result = PORPOISE_EXIT_USAGE;
                    goto finished;
                }
            } else if (key_flag == CONFIG_KEY_ENTRY) {
                if (!copy_checked(options->entry_symbol,
                                  sizeof(options->entry_symbol), decoded,
                                  "entry symbol", error_stream)) {
                    result = PORPOISE_EXIT_USAGE;
                    goto finished;
                }
            } else if (key_flag == CONFIG_KEY_MAP) {
                if (!resolve_config_relative_path(
                        path, decoded, options->map_path,
                        sizeof(options->map_path), "CodeWarrior map path",
                        error_stream)) {
                    result = PORPOISE_EXIT_USAGE;
                    goto finished;
                }
            } else if (key_flag == CONFIG_KEY_DTK_SYMBOLS) {
                if (!resolve_config_relative_path(
                        path, decoded, options->dtk_symbols_path,
                        sizeof(options->dtk_symbols_path), "DTK symbols path",
                        error_stream)) {
                    result = PORPOISE_EXIT_USAGE;
                    goto finished;
                }
            } else if (key_flag == CONFIG_KEY_DTK_SPLITS) {
                if (!resolve_config_relative_path(
                        path, decoded, options->dtk_splits_path,
                        sizeof(options->dtk_splits_path), "DTK splits path",
                        error_stream)) {
                    result = PORPOISE_EXIT_USAGE;
                    goto finished;
                }
            } else if (key_flag == CONFIG_KEY_SDK_CATALOG) {
                if (!resolve_config_relative_path(
                        path, decoded, options->sdk_catalog_path,
                        sizeof(options->sdk_catalog_path), "SDK catalog path",
                        error_stream)) {
                    result = PORPOISE_EXIT_USAGE;
                    goto finished;
                }
            } else if (key_flag == CONFIG_KEY_SDK_POLICY) {
                if (!parse_sdk_policy(decoded, &options->sdk_policy)) {
                    result = option_error(
                        error_stream,
                        "config key 'sdk_policy' must be 'keep', 'imported', or 'omit'");
                    goto finished;
                }
            } else if (key_flag == CONFIG_KEY_MODULE) {
                if (!copy_checked(
                        options->module, sizeof(options->module), decoded,
                        "module name", error_stream)) {
                    result = PORPOISE_EXIT_USAGE;
                    goto finished;
                }
            } else {
                if (strcmp(decoded, "quiet") == 0) {
                    options->verbosity = PORPOISE_VERBOSITY_QUIET;
                } else if (strcmp(decoded, "normal") == 0) {
                    options->verbosity = PORPOISE_VERBOSITY_NORMAL;
                } else if (strcmp(decoded, "verbose") == 0) {
                    options->verbosity = PORPOISE_VERBOSITY_VERBOSE;
                } else {
                    result = option_error(
                        error_stream,
                        "config key 'verbosity' must be 'quiet', 'normal', or 'verbose'");
                    goto finished;
                }
            }
        }
    }

    if ((seen_keys & CONFIG_KEY_SCHEMA) == 0U) {
        result = option_error(error_stream,
                              "config '%s' is missing required schema_version",
                              path);
        goto finished;
    }
    if ((seen_keys & CONFIG_KEY_DTK_SPLITS) != 0U &&
        (seen_keys & CONFIG_KEY_DTK_SYMBOLS) == 0U) {
        result = option_error(
            error_stream,
            "config key 'dtk_splits' requires 'dtk_symbols'");
        goto finished;
    }
    result = PORPOISE_EXIT_SUCCESS;

finished:
    free(tokens);
    free(json);
    return result;
}

PorpoiseExitCode porpoise_options_parse(PorpoiseOptions *options,
                                        int argc,
                                        char *const argv[],
                                        FILE *error_stream) {
    CliValues cli;
    PorpoiseOptions parsed;
    PorpoiseExitCode result;

    if (options == NULL) {
        if (error_stream != NULL) {
            fputs("porpoise: internal error: options pointer is NULL\n",
                  error_stream);
        }
        return PORPOISE_EXIT_INTERNAL;
    }
    porpoise_options_init(options);
    memset(&cli, 0, sizeof(cli));

    if (argc < 1 || argv == NULL || argv[0] == NULL) {
        if (error_stream != NULL) {
            fputs("porpoise: internal error: invalid argv\n", error_stream);
        }
        return PORPOISE_EXIT_INTERNAL;
    }

    result = parse_cli(&cli, argc, argv, error_stream);
    if (result != PORPOISE_EXIT_SUCCESS) {
        return result;
    }
    if (cli.values.show_help || cli.values.show_version) {
        *options = cli.values;
        return PORPOISE_EXIT_SUCCESS;
    }

    porpoise_options_init(&parsed);
    if (cli.config_seen) {
        if (!copy_checked(parsed.config_path, sizeof(parsed.config_path),
                          cli.values.config_path, "config path", error_stream)) {
            return PORPOISE_EXIT_USAGE;
        }
        result = parse_config(&parsed, parsed.config_path, error_stream);
        if (result != PORPOISE_EXIT_SUCCESS) {
            return result;
        }
    }

    memcpy(parsed.input_path, cli.values.input_path,
           sizeof(parsed.input_path));
    memcpy(parsed.output_path, cli.values.output_path,
           sizeof(parsed.output_path));
    if (cli.abi_seen) {
        memcpy(parsed.abi_path, cli.values.abi_path, sizeof(parsed.abi_path));
    }
    if (cli.skip_list_seen) {
        memcpy(parsed.skip_list_path, cli.values.skip_list_path,
               sizeof(parsed.skip_list_path));
    }
    if (cli.map_seen) {
        memcpy(parsed.map_path, cli.values.map_path,
               sizeof(parsed.map_path));
    }
    if (cli.dtk_symbols_seen) {
        memcpy(parsed.dtk_symbols_path, cli.values.dtk_symbols_path,
               sizeof(parsed.dtk_symbols_path));
    }
    if (cli.dtk_splits_seen) {
        memcpy(parsed.dtk_splits_path, cli.values.dtk_splits_path,
               sizeof(parsed.dtk_splits_path));
    }
    if (cli.sdk_catalog_seen) {
        memcpy(parsed.sdk_catalog_path, cli.values.sdk_catalog_path,
               sizeof(parsed.sdk_catalog_path));
    }
    if (cli.module_seen) {
        memcpy(parsed.module, cli.values.module, sizeof(parsed.module));
    }
    if (cli.sdk_policy_seen) parsed.sdk_policy = cli.values.sdk_policy;
    if (cli.entry_seen) {
        memcpy(parsed.entry_symbol, cli.values.entry_symbol,
               sizeof(parsed.entry_symbol));
    }
    if (cli.strict_seen) {
        parsed.strict = true;
    }
    if (cli.verbosity_seen) {
        parsed.verbosity = cli.values.verbosity;
    }
    parsed.force = cli.values.force;

    if (parsed.dtk_splits_path[0] != '\0' &&
        parsed.dtk_symbols_path[0] == '\0') {
        return option_error(
            error_stream,
            "--dtk-splits requires --dtk-symbols");
    }

    *options = parsed;
    return PORPOISE_EXIT_SUCCESS;
}
