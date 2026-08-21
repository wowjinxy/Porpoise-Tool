#include "porpoise/options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int failures = 0U;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n",                     \
                    __FILE__, __LINE__, #condition);                            \
            failures++;                                                        \
        }                                                                       \
    } while (0)

static PorpoiseExitCode parse(
    PorpoiseOptions *options,
    int argc,
    char **argv) {
    return porpoise_options_parse(options, argc, argv, NULL);
}

static char *read_stream(FILE *stream) {
    long size;
    char *text;
    if (fflush(stream) != 0 || fseek(stream, 0L, SEEK_END) != 0 ||
        (size = ftell(stream)) < 0L || fseek(stream, 0L, SEEK_SET) != 0) {
        return NULL;
    }
    text = (char *)malloc((size_t)size + 1U);
    if (text == NULL) return NULL;
    if (fread(text, 1U, (size_t)size, stream) != (size_t)size) {
        free(text);
        return NULL;
    }
    text[size] = '\0';
    return text;
}

static void test_defaults_and_minimal_project(void) {
    PorpoiseOptions options;
    char *argv[] = { "porpoise", "--project", "game.porpoise.json" };
    porpoise_options_init(&options);
    CHECK(options.project_path[0] == '\0');
    CHECK(options.target_id_count == 0U);
    CHECK(options.report_path[0] == '\0');
    CHECK(!options.analyze_only);
    CHECK(!options.force);
    CHECK(parse(&options, 3, argv) == PORPOISE_EXIT_SUCCESS);
    CHECK(strcmp(options.project_path, "game.porpoise.json") == 0);
    CHECK(options.target_id_count == 0U);
    CHECK(options.input_path[0] == '\0');
    CHECK(options.output_path[0] == '\0');
    CHECK(options.config_path[0] == '\0');
    CHECK(options.report_path[0] == '\0');
    CHECK(!options.analyze_only);
    CHECK(!options.force);
    CHECK(options.verbosity == PORPOISE_VERBOSITY_NORMAL);
}

static void test_repeatable_selection_and_operational_flags(void) {
    PorpoiseOptions options;
    char *argv[] = {
        "porpoise",
        "--target", "main-dol",
        "--project", "game.porpoise.json",
        "--dtk", "tools/dtk.exe",
        "--target", "overlay-rel",
        "--analyze-only",
        "--report", "reports/analysis.json",
        "--force",
        "--verbose"
    };
    CHECK(parse(&options, (int)(sizeof(argv) / sizeof(argv[0])), argv) ==
          PORPOISE_EXIT_SUCCESS);
    CHECK(strcmp(options.project_path, "game.porpoise.json") == 0);
    CHECK(options.target_id_count == 2U);
    CHECK(strcmp(options.target_ids[0], "main-dol") == 0);
    CHECK(strcmp(options.target_ids[1], "overlay-rel") == 0);
    CHECK(options.analyze_only);
    CHECK(strcmp(options.dtk_path, "tools/dtk.exe") == 0);
    CHECK(strcmp(options.report_path, "reports/analysis.json") == 0);
    CHECK(options.force);
    CHECK(options.verbosity == PORPOISE_VERBOSITY_VERBOSE);
    /* force is operational state: absent CLI input always resets it. */
    {
        char *without_force[] = {
            "porpoise", "--project", "game.porpoise.json", "--quiet"
        };
        CHECK(parse(&options, 4, without_force) == PORPOISE_EXIT_SUCCESS);
        CHECK(!options.force);
        CHECK(options.verbosity == PORPOISE_VERBOSITY_QUIET);
    }
}

static void test_project_classic_exclusivity(void) {
    static const struct ConflictCase {
        const char *first;
        const char *second;
    } conflicts[] = {
        { "input.s", NULL },
        { "--output", "out" },
        { "--config", "classic.json" },
        { "--abi", "abi.json" },
        { "--skip-list", "skip.txt" },
        { "--map", "game.map" },
        { "--dtk-symbols", "symbols.txt" },
        { "--dtk-splits", "splits.txt" },
        { "--sdk-catalog", "sdk.json" },
        { "--sdk-policy", "omit" },
        { "--module", "main" },
        { "--entry", "_start" },
        { "--strict", NULL }
    };
    size_t index;
    for (index = 0U; index < sizeof(conflicts) / sizeof(conflicts[0]); index++) {
        PorpoiseOptions options;
        char *argv[5];
        int argc = 4;
        argv[0] = "porpoise";
        argv[1] = "--project";
        argv[2] = "game.porpoise.json";
        argv[3] = (char *)conflicts[index].first;
        if (conflicts[index].second != NULL) {
            argv[4] = (char *)conflicts[index].second;
            argc = 5;
        }
        CHECK(parse(&options, argc, argv) == PORPOISE_EXIT_USAGE);
    }
    /* The decision is independent of argument order. */
    {
        PorpoiseOptions options;
        char *argv[] = {
            "porpoise", "input.s", "--output", "out",
            "--project", "game.porpoise.json"
        };
        CHECK(parse(&options, 6, argv) == PORPOISE_EXIT_USAGE);
    }
}

static void test_project_only_options_require_project(void) {
    static const struct ProjectOnlyCase {
        const char *first;
        const char *second;
    } cases[] = {
        { "--dtk", "dtk" },
        { "--target", "main" },
        { "--analyze-only", NULL },
        { "--report", "report.json" }
    };
    size_t index;
    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
        PorpoiseOptions options;
        char *argv[7] = {
            "porpoise", "input.s", "--output", "out",
            (char *)cases[index].first, (char *)cases[index].second, NULL
        };
        int argc = cases[index].second == NULL ? 5 : 6;
        CHECK(parse(&options, argc, argv) == PORPOISE_EXIT_USAGE);
    }
}

static void test_duplicate_and_missing_project_options(void) {
    PorpoiseOptions options;
    char *duplicate_project[] = {
        "porpoise", "--project", "a.json", "--project", "b.json"
    };
    char *duplicate_target[] = {
        "porpoise", "--project", "a.json",
        "--target", "main", "--target", "main"
    };
    char *duplicate_analyze[] = {
        "porpoise", "--project", "a.json",
        "--analyze-only", "--analyze-only"
    };
    char *duplicate_report[] = {
        "porpoise", "--project", "a.json",
        "--report", "a.json", "--report", "b.json"
    };
    char *missing_project[] = { "porpoise", "--project" };
    char *missing_target[] = {
        "porpoise", "--project", "a.json", "--target"
    };
    char *missing_report[] = {
        "porpoise", "--project", "a.json", "--report"
    };
    char *duplicate_force[] = {
        "porpoise", "--project", "a.json", "--force", "--force"
    };
    char *verbosity_conflict[] = {
        "porpoise", "--project", "a.json", "--quiet", "--verbose"
    };
    char *help_combined[] = {
        "porpoise", "--project", "a.json", "--help"
    };
    CHECK(parse(&options, 5, duplicate_project) == PORPOISE_EXIT_USAGE);
    CHECK(parse(&options, 7, duplicate_target) == PORPOISE_EXIT_USAGE);
    CHECK(parse(&options, 5, duplicate_analyze) == PORPOISE_EXIT_USAGE);
    CHECK(parse(&options, 7, duplicate_report) == PORPOISE_EXIT_USAGE);
    CHECK(parse(&options, 2, missing_project) == PORPOISE_EXIT_USAGE);
    CHECK(parse(&options, 4, missing_target) == PORPOISE_EXIT_USAGE);
    CHECK(parse(&options, 4, missing_report) == PORPOISE_EXIT_USAGE);
    CHECK(parse(&options, 5, duplicate_force) == PORPOISE_EXIT_USAGE);
    CHECK(parse(&options, 5, verbosity_conflict) == PORPOISE_EXIT_USAGE);
    CHECK(parse(&options, 4, help_combined) == PORPOISE_EXIT_USAGE);
}

static void test_selector_limit(void) {
    PorpoiseOptions options;
    char *argv[3U + (PORPOISE_TARGET_SELECTOR_LIMIT + 1U) * 2U];
    char ids[PORPOISE_TARGET_SELECTOR_LIMIT + 1U][32];
    size_t index;
    int argc;
    argv[0] = "porpoise";
    argv[1] = "--project";
    argv[2] = "game.porpoise.json";
    argc = 3;
    for (index = 0U; index < PORPOISE_TARGET_SELECTOR_LIMIT; index++) {
        snprintf(ids[index], sizeof(ids[index]), "target-%lu",
                 (unsigned long)index);
        argv[argc++] = "--target";
        argv[argc++] = ids[index];
    }
    CHECK(parse(&options, argc, argv) == PORPOISE_EXIT_SUCCESS);
    CHECK(options.target_id_count == PORPOISE_TARGET_SELECTOR_LIMIT);
    snprintf(ids[PORPOISE_TARGET_SELECTOR_LIMIT],
             sizeof(ids[PORPOISE_TARGET_SELECTOR_LIMIT]), "target-overflow");
    argv[argc++] = "--target";
    argv[argc++] = ids[PORPOISE_TARGET_SELECTOR_LIMIT];
    CHECK(parse(&options, argc, argv) == PORPOISE_EXIT_USAGE);
}

static void test_help_and_classic_compatibility(void) {
    PorpoiseOptions options;
    char *help_argv[] = { "porpoise", "--help" };
    char *classic_argv[] = {
        "porpoise", "input.s", "--output", "out",
        "--abi", "abi.json", "--force", "--quiet"
    };
    FILE *stream = tmpfile();
    char *help;
    CHECK(stream != NULL);
    if (stream != NULL) {
        porpoise_options_print_help(stream, "porpoise-test");
        help = read_stream(stream);
        CHECK(help != NULL);
        if (help != NULL) {
            CHECK(strstr(help,
                         "porpoise-test --project FILE") != NULL);
            CHECK(strstr(help, "--dtk FILE") != NULL);
            CHECK(strstr(help, "--target ID") != NULL);
            CHECK(strstr(help, "--analyze-only") != NULL);
            CHECK(strstr(help, "--report FILE") != NULL);
            free(help);
        }
        fclose(stream);
    }
    CHECK(parse(&options, 2, help_argv) == PORPOISE_EXIT_SUCCESS);
    CHECK(options.show_help);
    CHECK(parse(&options, 8, classic_argv) == PORPOISE_EXIT_SUCCESS);
    CHECK(strcmp(options.input_path, "input.s") == 0);
    CHECK(strcmp(options.output_path, "out") == 0);
    CHECK(strcmp(options.abi_path, "abi.json") == 0);
    CHECK(options.force);
    CHECK(options.verbosity == PORPOISE_VERBOSITY_QUIET);
    CHECK(options.project_path[0] == '\0');
    CHECK(options.target_id_count == 0U);
}

int main(void) {
    test_defaults_and_minimal_project();
    test_repeatable_selection_and_operational_flags();
    test_project_classic_exclusivity();
    test_project_only_options_require_project();
    test_duplicate_and_missing_project_options();
    test_selector_limit();
    test_help_and_classic_compatibility();
    if (failures != 0U) {
        fprintf(stderr, "%u project option test(s) failed\n", failures);
        return 1;
    }
    return 0;
}
