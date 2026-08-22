#include "workbench_model.h"

extern "C" {
#include "porpoise/util.h"
}

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using porpoise::gui::DirectAbiDraft;
using porpoise::gui::BuildRunRequest;
using porpoise::gui::FunctionLocator;
using porpoise::gui::FunctionFilterMatches;
using porpoise::gui::RemoveTargetBuildCache;
using porpoise::gui::OverrideEdit;
using porpoise::gui::RunRequest;
using porpoise::gui::WorkerState;
using porpoise::gui::WorkerOperation;
using porpoise::gui::WorkbenchModel;

namespace {

unsigned int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition "\n";                 \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

std::filesystem::path Fixture(const std::filesystem::path &source,
                              const char *leaf) {
    return source / "tests" / "fixtures" / "session_plan" / leaf;
}

const PorpoiseFunctionPlanView *FindFunction(
    const PorpoiseRecoveryRunResult *result, const char *name,
    const PorpoiseRecoveryRunTarget **target_out = nullptr) {
    if (result == nullptr) return nullptr;
    for (std::size_t target_index = 0; target_index < result->target_count;
         ++target_index) {
        const auto &target = result->targets[target_index];
        if (target.plan == nullptr) continue;
        for (std::size_t index = 0;
             index < porpoise_plan_function_count(target.plan); ++index) {
            const auto *view = porpoise_plan_function_at(target.plan, index);
            if (std::string(view->function->name) == name) {
                if (target_out != nullptr) *target_out = &target;
                return view;
            }
        }
    }
    return nullptr;
}

bool DiagnosticsContain(const WorkbenchModel &model, const char *text) {
    for (std::size_t index = 0; index < model.Diagnostics().count; ++index) {
        const auto *message = model.Diagnostics().items[index].message;
        if (message != nullptr && std::strstr(message, text) != nullptr)
            return true;
    }
    return false;
}

void WriteText(const std::filesystem::path &path, const std::string &text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    CHECK(output.good());
    output << text;
    output.flush();
    CHECK(output.good());
}

std::string ReadText(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

int RunFakePreflightTool(int argc, char **argv) {
    const std::string executable = argc > 0 && argv[0] != nullptr
        ? std::filesystem::path(argv[0]).filename().string() : std::string();
    if (executable.find("porpoise-fake-") == std::string::npos) return -1;
    const bool meson = executable.find("meson") != std::string::npos;
    const bool mismatch = executable.find("mismatch") != std::string::npos;
    const bool slow = executable.find("slow") != std::string::npos;
    if (meson) {
        if (slow) std::this_thread::sleep_for(std::chrono::seconds(20));
        if (argc == 2 && std::string(argv[1]) == "--version") {
            std::cout << "1.3.0\n";
            return 0;
        }
        return 1;
    }
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "gcc (GCC) " << (mismatch ? "14.1.0" : "13.2.0")
                  << '\n';
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "-dumpmachine") {
        std::cout << "x86_64-porpoise-test\n";
        return 0;
    }
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string(argv[index]) != "-o") continue;
        std::error_code error;
        const std::filesystem::path output(argv[index + 1]);
        std::filesystem::create_directories(output.parent_path(), error);
        if (error) return 1;
        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        file << "synthetic compiler output\n";
        return file.good() ? 0 : 1;
    }
    return 1;
}

void Analyze(WorkbenchModel &model) {
    RunRequest request;
    request.analyze_only = true;
    CHECK(model.Start(request));
    CHECK(model.Operation() == WorkerOperation::Recovery);
    model.Wait();
    if (model.State() != WorkerState::Succeeded) {
        for (std::size_t index = 0; index < model.Diagnostics().count; ++index)
            std::cerr << "diagnostic: "
                      << model.Diagnostics().items[index].message << '\n';
    }
    CHECK(model.State() == WorkerState::Succeeded);
    CHECK(model.Operation() == WorkerOperation::Recovery);
    CHECK(model.LastExitCode() == PORPOISE_EXIT_OK);
}

void TestModel(const std::filesystem::path &source,
               const std::filesystem::path &build) {
    const auto default_runtime =
        std::filesystem::path(porpoise_default_runtime_directory());
    CHECK(std::filesystem::is_regular_file(
        default_runtime / "include" / "porpoise_lifted.h"));
    CHECK(std::filesystem::is_regular_file(
        default_runtime / "src" / "porpoise_lifted.c"));

    const auto temporary = build / "workbench-model-test";
    std::error_code error;
    const auto resolved_temporary = std::filesystem::weakly_canonical(
        temporary.parent_path(), error) / temporary.filename();
    CHECK(!error);
    CHECK(resolved_temporary.string().find(
              std::filesystem::weakly_canonical(build).string()) == 0);
    std::filesystem::remove_all(resolved_temporary, error);
    error.clear();
    std::filesystem::create_directories(resolved_temporary, error);
    CHECK(!error);

    const auto model_input = temporary / "input-with-data";
    std::filesystem::create_directories(model_input, error);
    CHECK(!error);
    for (const auto &entry : std::filesystem::directory_iterator(
             Fixture(source, "input"))) {
        std::filesystem::copy_file(
            entry.path(), model_input / entry.path().filename(),
            std::filesystem::copy_options::overwrite_existing, error);
        CHECK(!error);
        error.clear();
    }
    WriteText(
        model_input / "data.s",
        "# 0x80005000..0x80005010 | size: 0x10\n"
        ".data\n"
        "# .data:0x0 | 0x80005000 | size: 0x10\n"
        ".obj editable_blob, global\n"
        "  .byte 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17\n"
        "  .byte 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f\n"
        ".endobj editable_blob\n");

    {
        WorkbenchModel diagnostics;
        CHECK(diagnostics.PrimaryDiagnostic() == nullptr);
        CHECK(!diagnostics.LoadProject(
            (temporary / "missing-project.porpoise.json").string()));
        const auto *primary = diagnostics.PrimaryDiagnostic();
        CHECK(primary != nullptr);
        if (primary != nullptr)
            CHECK(primary->severity == PORPOISE_SEVERITY_ERROR);
    }

    /* New targets begin with safe, distinct project-relative locations and
     * permissive analysis so the first run does not require expert setup. */
    {
        WorkbenchModel defaults;
        CHECK(defaults.NewProject());
        CHECK(defaults.AddTarget("sample"));
        CHECK(defaults.AddTarget("sample"));
        CHECK(defaults.Project().target_count == 2);
        if (defaults.Project().target_count == 2) {
            const auto &first = defaults.Project().targets[0];
            const auto &second = defaults.Project().targets[1];
            CHECK(std::string(first.id) == "sample");
            CHECK(std::string(first.input.value) == "input");
            CHECK(std::string(first.output.value) ==
                  "porpoise-output/sample");
            CHECK(!first.strict);
            CHECK(std::string(second.id) == "sample-2");
            CHECK(std::string(second.input.value) == "input");
            CHECK(std::string(second.output.value) ==
                  "porpoise-output/sample-2");
            CHECK(!second.strict);
        }
        CHECK(defaults.AddTarget("legacy.overlay"));
        CHECK(defaults.Project().target_count == 3);
        if (defaults.Project().target_count == 3) {
            const auto &legacy = defaults.Project().targets[2];
            CHECK(std::string(legacy.output.value).rfind(
                      "porpoise-output/target-", 0U) == 0U);
        }
        CHECK(defaults.SetTargetId(2, "legacy/overlay"));
        CHECK(!defaults.SetTargetId(2, ""));
    }

    /* Saving a never-published project interprets its relative defaults beside
     * the selected project, rather than beside the process working directory. */
    {
        const auto destination = temporary / "direct-save-as";
        std::filesystem::create_directories(destination, error);
        CHECK(!error);
        WorkbenchModel untitled;
        CHECK(untitled.NewProject());
        CHECK(untitled.AddTarget("direct"));
        const auto document = destination / "direct.porpoise.json";
        CHECK(untitled.SaveAs(document.string()));
        const auto &target = untitled.Project().targets[0];
        CHECK(std::string(target.input.value) == "input");
        CHECK(std::string(target.output.value) ==
              "porpoise-output/direct");
        CHECK(std::filesystem::path(target.input.resolved).lexically_normal() ==
              (destination / "input").lexically_normal());
        CHECK(std::filesystem::path(target.output.resolved).lexically_normal() ==
              (destination / "porpoise-output" / "direct").lexically_normal());
    }

    /* An untitled recovery autosave is stored under machine state, but Save As
     * still binds its relative paths to the user-selected project directory. */
    {
        const auto recovery_directory = temporary / "rebase-recovery";
        const auto destination = temporary / "recovered-save-as";
        std::filesystem::create_directories(recovery_directory, error);
        CHECK(!error);
        std::filesystem::create_directories(destination, error);
        CHECK(!error);
        WorkbenchModel draft;
        CHECK(draft.SetUntitledRecoveryDirectory(recovery_directory.string()));
        CHECK(draft.NewProject());
        CHECK(draft.AddTarget("recovered"));
        CHECK(draft.Autosave());

        WorkbenchModel recovered;
        CHECK(recovered.SetUntitledRecoveryDirectory(
            recovery_directory.string()));
        CHECK(recovered.NewProject());
        CHECK(recovered.RecoverAutosave(""));
        CHECK(recovered.DocumentPath().empty());
        const auto document = destination / "recovered.porpoise.json";
        CHECK(recovered.SaveAs(document.string()));
        const auto &target = recovered.Project().targets[0];
        CHECK(std::string(target.input.value) == "input");
        CHECK(std::string(target.output.value) ==
              "porpoise-output/recovered");
        CHECK(std::filesystem::path(target.input.resolved).lexically_normal() ==
              (destination / "input").lexically_normal());
        CHECK(std::filesystem::path(target.output.resolved).lexically_normal() ==
              (destination / "porpoise-output" /
               "recovered").lexically_normal());
    }

    /* Never-saved documents use a discoverable recovery identity. Loading the
     * recovery keeps the document untitled and does not bind the in-memory
     * project to the hidden autosave filename. */
    const auto untitled_directory = temporary / "untitled-recovery";
    std::filesystem::create_directories(untitled_directory, error);
    CHECK(!error);
    std::string untitled_autosave;
    {
        WorkbenchModel untitled;
        CHECK(untitled.SetUntitledRecoveryDirectory(
            untitled_directory.string()));
        CHECK(untitled.NewProject());
        CHECK(untitled.AddTarget("untitled"));
        CHECK(untitled.SetTargetPath(
            0, false, Fixture(source, "input").string()));
        CHECK(untitled.SetTargetPath(
            0, true, (untitled_directory / "output").string()));
        CHECK(untitled.SetTargetEntry(0, "lift_me"));
        CHECK(untitled.SetTargetSkipList(
            0, Fixture(source, "skip.txt").string()));
        untitled_autosave = untitled.AutosavePath();
        CHECK(untitled.Autosave());
        CHECK(std::filesystem::is_regular_file(untitled_autosave));

        WorkbenchModel recovered_untitled;
        CHECK(recovered_untitled.SetUntitledRecoveryDirectory(
            untitled_directory.string()));
        CHECK(recovered_untitled.NewProject());
        CHECK(recovered_untitled.HasNewerAutosave());
        CHECK(recovered_untitled.RecoverAutosave(""));
        CHECK(recovered_untitled.DocumentPath().empty());
        CHECK(recovered_untitled.Project().path == nullptr);
        CHECK(recovered_untitled.Project().target_count == 1);
        if (recovered_untitled.Project().target_count == 1) {
            CHECK(std::string(recovered_untitled.Project().targets[0].id) ==
                  "untitled");
        }
    }
    std::filesystem::remove(untitled_autosave, error);
    CHECK(!error);

    const auto project_path = temporary / "recovery.porpoise.json";
    WorkbenchModel model;
    CHECK(model.NewProject());
    CHECK(model.AddTarget("main"));
    CHECK(model.SetTargetPath(0, false, model_input.string()));
    CHECK(model.SetTargetPath(0, true, (temporary / "output").string()));
    CHECK(model.SetTargetEntry(0, "lift_me"));
    CHECK(model.SetTargetSkipList(0, Fixture(source, "skip.txt").string()));
    CHECK(model.AddSharedPath(true, Fixture(source, "abi.json").string()));
    CHECK(model.SaveAs(project_path.string()));
    CHECK(!model.Dirty());
    CHECK(model.Project().target_count == 1);

    {
        WorkbenchModel reopened;
        CHECK(reopened.LoadProject(project_path.string()));
        CHECK(reopened.Project().target_count == 1);
        CHECK(std::string(reopened.Project().targets[0].id) == "main");
        CHECK(std::string(reopened.Project().targets[0].entry) == "lift_me");
        CHECK(!reopened.Dirty());
    }

    /* Autosaves are keyed by the normalized full document path, not merely
     * the containing directory. Two projects beside each other must never
     * overwrite or recover each other's work. */
    const auto neighbor_path = temporary / "neighbor.porpoise.json";
    std::filesystem::copy_file(
        project_path, neighbor_path,
        std::filesystem::copy_options::overwrite_existing, error);
    CHECK(!error);
    {
        WorkbenchModel first;
        WorkbenchModel neighbor;
        WorkbenchModel alternate_spelling;
        CHECK(first.LoadProject(project_path.string()));
        CHECK(neighbor.LoadProject(neighbor_path.string()));
        CHECK(alternate_spelling.LoadProject(
            (temporary / "." / project_path.filename()).string()));
        CHECK(first.AutosavePath() == alternate_spelling.AutosavePath());
        CHECK(first.AutosavePath() != neighbor.AutosavePath());
        CHECK(std::filesystem::path(first.AutosavePath())
                  .filename().string().find(project_path.filename().string()) !=
              std::string::npos);
        CHECK(std::filesystem::path(neighbor.AutosavePath())
                  .filename().string().find(neighbor_path.filename().string()) !=
              std::string::npos);

        first.Project().targets[0].strict = false;
        first.MarkDirty();
        CHECK(first.Autosave());
        neighbor.Project().targets[0].enabled = false;
        neighbor.MarkDirty();
        CHECK(neighbor.Autosave());
        CHECK(std::filesystem::exists(first.AutosavePath()));
        CHECK(std::filesystem::exists(neighbor.AutosavePath()));
        CHECK(ReadText(first.AutosavePath()) !=
              ReadText(neighbor.AutosavePath()));
    }

    CHECK(FunctionFilterMatches("", {}));
    CHECK(FunctionFilterMatches(
        "LiFt", {"main", "lift_me", ".text", "title"}));
    CHECK(FunctionFilterMatches(
        "SDK/LIB", {"sdk/lib", "canonical-name"}));
    CHECK(FunctionFilterMatches(
        "MAIN .TeXt", {"main", "lift_me", ".text", "title"}));
    CHECK(!FunctionFilterMatches(
        "main missing", {"main", "lift_me", ".text", "title"}));
    CHECK(!FunctionFilterMatches(
        "missing", {"main", "lift_me", ".text", "title"}));

    const std::string recovery_sentinel = "recovery-autosave-sentinel\n";
    WriteText(model.AutosavePath(), recovery_sentinel);
    Analyze(model);
    CHECK(ReadText(model.AutosavePath()) == recovery_sentinel);
    CHECK(model.Project().path != nullptr);
    if (model.Project().path != nullptr) {
        CHECK(std::filesystem::path(model.Project().path).lexically_normal() ==
              std::filesystem::absolute(project_path).lexically_normal());
    }
    bool worker_snapshot_left = false;
    for (const auto &entry : std::filesystem::directory_iterator(temporary)) {
        if (entry.path().filename().string().find(".porpoise-run-") == 0)
            worker_snapshot_left = true;
    }
    CHECK(!worker_snapshot_left);
    const PorpoiseRecoveryRunTarget *run_target = nullptr;
    const auto *omit = FindFunction(model.RunResult(), "omit_me", &run_target);
    CHECK(omit != nullptr);
    CHECK(run_target != nullptr);
    CHECK(omit != nullptr && omit->action == PORPOISE_PLAN_ACTION_OMIT);

    const auto *lift = FindFunction(model.RunResult(), "lift_me", &run_target);
    CHECK(lift != nullptr);
    FunctionLocator lift_locator;
    if (lift != nullptr && run_target != nullptr)
        lift_locator = model.MakeLocator(*run_target, *lift);
    const auto *initial_import = FindFunction(
        model.RunResult(), "import_me", &run_target);
    FunctionLocator import_locator;
    if (initial_import != nullptr && run_target != nullptr)
        import_locator = model.MakeLocator(*run_target, *initial_import);
    CHECK(initial_import != nullptr);
    const PorpoiseSession *loaded_session =
        run_target == nullptr ? nullptr : run_target->session;

    OverrideEdit lift_edit;
    lift_edit.locator = lift_locator;
    lift_edit.action = PORPOISE_OVERRIDE_LIFT;
    OverrideEdit import_edit;
    import_edit.locator = import_locator;
    import_edit.action = PORPOISE_OVERRIDE_OMIT;
    CHECK(model.ApplyOverrides({lift_edit, import_edit}));
    CHECK(model.State() == WorkerState::Running);
    model.Wait();
    CHECK(model.Project().targets[0].override_count == 2);
    const auto *immediate_bulk = FindFunction(
        model.RunResult(), "import_me");
    CHECK(immediate_bulk != nullptr &&
          immediate_bulk->action == PORPOISE_PLAN_ACTION_OMIT);
    CHECK(model.State() == WorkerState::Succeeded);
    CHECK(model.RunResult() != nullptr &&
          model.RunResult()->targets[0].session == loaded_session);

    OverrideEdit invalid_import = import_edit;
    invalid_import.action = PORPOISE_OVERRIDE_IMPORT;
    invalid_import.contract_name.clear();
    const auto override_count_before_invalid =
        model.Project().targets[0].override_count;
    CHECK(!model.ApplyOverrides({lift_edit, invalid_import}));
    CHECK(model.Project().targets[0].override_count ==
          override_count_before_invalid);
    CHECK(DiagnosticsContain(model, "requires a contract name"));

    CHECK(model.Save());
    Analyze(model);
    lift = FindFunction(model.RunResult(), "lift_me", &run_target);
    CHECK(lift != nullptr && lift->action == PORPOISE_PLAN_ACTION_LIFT);
    CHECK(lift != nullptr && lift->overridden);
    const auto *bulk_import = FindFunction(model.RunResult(), "import_me");
    CHECK(bulk_import != nullptr &&
          bulk_import->action == PORPOISE_PLAN_ACTION_OMIT);
    CHECK(bulk_import != nullptr && bulk_import->overridden);

    OverrideEdit reset_import = import_edit;
    reset_import.action = PORPOISE_OVERRIDE_AUTO;
    CHECK(model.ApplyOverrides({reset_import}));
    CHECK(model.State() == WorkerState::Running);
    model.Wait();
    CHECK(model.Project().targets[0].override_count == 1);
    const auto *immediate_automatic = FindFunction(
        model.RunResult(), "import_me");
    CHECK(immediate_automatic != nullptr &&
          immediate_automatic->action == PORPOISE_PLAN_ACTION_IMPORT);
    Analyze(model);
    const auto *automatic_import = FindFunction(
        model.RunResult(), "import_me");
    CHECK(automatic_import != nullptr &&
          automatic_import->action == PORPOISE_PLAN_ACTION_IMPORT);
    lift = FindFunction(model.RunResult(), "lift_me", &run_target);
    CHECK(lift != nullptr);

    FunctionLocator data_locator;
    std::vector<porpoise::gui::DataObjectRecord> data_objects;
    if (run_target != nullptr) data_objects = model.DataObjects(*run_target);
    const auto editable_data = std::find_if(
        data_objects.begin(), data_objects.end(),
        [](const porpoise::gui::DataObjectRecord &record) {
            return record.name == "editable_blob";
        });
    CHECK(editable_data != data_objects.end());
    if (editable_data != data_objects.end() && run_target != nullptr) {
        CHECK(model.MakeDataLocator(
            *run_target, editable_data->locator.address + 4U, 4U,
            &data_locator));
        FunctionLocator outside_data;
        CHECK(!model.MakeDataLocator(
            *run_target, editable_data->locator.address + 15U, 2U,
            &outside_data));
    }
    CHECK(data_locator.address == UINT32_C(0x80005004));
    CHECK(data_locator.size == 4U);
    CHECK(data_locator.normalized_fingerprint.size() == 64U);
    CHECK(model.UpsertAnnotation(
        data_locator, PORPOISE_RECOVERY_ANNOTATION_U16_ARRAY,
        2U, "big-endian"));
    CHECK(model.State() == WorkerState::Running);
    model.Wait();
    CHECK(model.Project().targets[0].annotation_count == 1);
    if (model.Project().targets[0].annotation_count == 1) {
        const auto &saved = model.Project().targets[0].annotations[0];
        CHECK(saved.address == UINT32_C(0x80005004));
        CHECK(saved.size == 4U);
        CHECK(std::string(saved.normalized_fingerprint) ==
              data_locator.normalized_fingerprint);
        CHECK(std::string(saved.normalized_fingerprint) ==
              std::string(saved.exact_bytes_sha256));
    }
    CHECK(model.RunResult() != nullptr);
    CHECK(model.RunResult()->targets[0].session == run_target->session);

    FunctionLocator overlapping = data_locator;
    ++overlapping.address;
    overlapping.size = 2;
    CHECK(!model.UpsertAnnotation(
        overlapping, PORPOISE_RECOVERY_ANNOTATION_RAW_BYTES,
        overlapping.size, ""));
    CHECK(model.Project().targets[0].annotation_count == 1);
    CHECK(DiagnosticsContain(model, "overlaps"));

    DirectAbiDraft contract;
    contract.symbol = "LocalHostCall";
    contract.wrapper = "host_local_call";
    contract.header = "porpoise/local_contracts.h";
    contract.result_type = PORPOISE_ABI_U32;
    contract.result_register_class = PORPOISE_ABI_REGISTER_GPR;
    contract.result_register_index = 3;
    porpoise::gui::AbiArgumentDraft integer_argument;
    integer_argument.name = "value";
    integer_argument.type = PORPOISE_ABI_S32;
    integer_argument.register_class = PORPOISE_ABI_REGISTER_GPR;
    integer_argument.register_index = 3;
    porpoise::gui::AbiArgumentDraft float_argument;
    float_argument.name = "scale";
    float_argument.type = PORPOISE_ABI_F32;
    float_argument.register_class = PORPOISE_ABI_REGISTER_FPR;
    float_argument.register_index = 1;
    contract.arguments = {integer_argument, float_argument};
    DirectAbiDraft partial_contract;
    partial_contract.symbol = "unfinished";
    partial_contract.header.clear();
    partial_contract.result_type = PORPOISE_ABI_U32;
    partial_contract.result_register_class = PORPOISE_ABI_REGISTER_GPR;
    partial_contract.result_register_index = 7;
    const auto abi_recovery = temporary / "abi-drafts.recovery";
    CHECK(model.WriteDirectAbiDraftRecovery(
        abi_recovery.string(), {contract, partial_contract}));
    std::vector<DirectAbiDraft> recovered_contracts;
    CHECK(model.LoadDirectAbiDraftRecovery(
        abi_recovery.string(), &recovered_contracts));
    CHECK(recovered_contracts.size() == 2);
    if (recovered_contracts.size() == 2) {
        CHECK(recovered_contracts[0].symbol == contract.symbol);
        CHECK(recovered_contracts[0].arguments.size() == 2);
        CHECK(recovered_contracts[1].symbol == "unfinished");
        CHECK(recovered_contracts[1].header.empty());
        CHECK(recovered_contracts[1].result_type == PORPOISE_ABI_U32);
        CHECK(recovered_contracts[1].result_register_class ==
              PORPOISE_ABI_REGISTER_GPR);
        CHECK(recovered_contracts[1].result_register_index == 7);
    }
    CHECK(model.WriteDirectAbiDraftRecovery(
        abi_recovery.string(), {partial_contract}));
    recovered_contracts.clear();
    CHECK(model.LoadDirectAbiDraftRecovery(
        abi_recovery.string(), &recovered_contracts));
    CHECK(recovered_contracts.size() == 1);
    if (recovered_contracts.size() == 1) {
        CHECK(recovered_contracts[0].symbol == "unfinished");
        CHECK(recovered_contracts[0].result_register_index == 7);
    }

    const auto abi_recovery_directory = temporary / "abi-recovery-directory";
    std::filesystem::create_directories(abi_recovery_directory, error);
    CHECK(!error);
    const auto abi_recovery_sentinel = abi_recovery_directory / "sentinel.bin";
    WriteText(abi_recovery_sentinel, "keep-recovery-directory");
    CHECK(!model.WriteDirectAbiDraftRecovery(
        abi_recovery_directory.string(), {contract}));
    CHECK(ReadText(abi_recovery_sentinel) == "keep-recovery-directory");

    DirectAbiDraft invalid_return = contract;
    invalid_return.result_register_index = 4;
    const auto invalid_abi = temporary / "invalid-return-abi.json";
    CHECK(!model.WriteDirectAbiManifest(
        invalid_abi.string(), {invalid_return}, false));
    CHECK(DiagnosticsContain(model, "returns to r3"));
    CHECK(!std::filesystem::exists(invalid_abi));

    const auto local_abi = temporary / "local-abi.json";
    CHECK(model.WriteDirectAbiManifest(local_abi.string(), {contract}, true));
    CHECK(model.WriteDirectAbiManifest(local_abi.string(), {contract}, true));
    CHECK(model.Project().abi_contract_count == 2);
    const auto abi_manifest_directory = temporary / "abi-manifest-directory";
    std::filesystem::create_directories(abi_manifest_directory, error);
    CHECK(!error);
    const auto abi_manifest_sentinel = abi_manifest_directory / "sentinel.bin";
    WriteText(abi_manifest_sentinel, "keep-manifest-directory");
    CHECK(!model.WriteDirectAbiManifest(
        abi_manifest_directory.string(), {contract}, false));
    CHECK(ReadText(abi_manifest_sentinel) == "keep-manifest-directory");
    CHECK(model.Save());

    Analyze(model);
    const auto *loaded_abi = model.LoadedAbiManifest(0);
    const auto *loaded_contract = loaded_abi == nullptr
        ? nullptr : porpoise_abi_find_import(loaded_abi, "LocalHostCall");
    CHECK(loaded_contract != nullptr);
    if (loaded_contract != nullptr) {
        CHECK(loaded_contract->result.type == PORPOISE_ABI_U32);
        CHECK(loaded_contract->result.register_class ==
              PORPOISE_ABI_REGISTER_GPR);
        CHECK(loaded_contract->result.register_index == 3);
        CHECK(loaded_contract->argument_count == 2);
        const auto round_trip =
            WorkbenchModel::DraftFromContract(*loaded_contract);
        CHECK(round_trip.symbol == contract.symbol);
        CHECK(round_trip.wrapper == contract.wrapper);
        CHECK(round_trip.result_type == contract.result_type);
        CHECK(round_trip.arguments.size() == 2);
        CHECK(round_trip.arguments[1].register_class ==
              PORPOISE_ABI_REGISTER_FPR);
        CHECK(round_trip.arguments[1].register_index == 1);
    }

    {
        WorkbenchModel reopened;
        CHECK(reopened.LoadProject(project_path.string()));
        CHECK(reopened.Project().abi_contract_count == 2);
        CHECK(reopened.Project().targets[0].override_count == 1);
        CHECK(reopened.Project().targets[0].annotation_count == 1);
        const auto *exact_hash = reopened.Project().targets[0]
                                     .annotations[0].exact_bytes_sha256;
        CHECK(exact_hash != nullptr);
        if (exact_hash != nullptr) CHECK(std::strlen(exact_hash) == 64);
    }

    const auto output = temporary / "output";
    const auto sentinel = output / "sentinel.txt";
    std::filesystem::create_directories(output, error);
    CHECK(!error);
    WriteText(sentinel, "preserve-me");
    CHECK(model.SelectedOutputsExist({}));
    model.Project().targets[0].enabled = false;
    model.MarkDirty();
    CHECK(!model.SelectedOutputsExist({}));
    CHECK(model.SelectedOutputsExist({"main"}));
    model.Project().targets[0].enabled = true;
    model.MarkDirty();

    Analyze(model);
    lift = FindFunction(model.RunResult(), "lift_me", &run_target);
    CHECK(lift != nullptr);
    if (lift != nullptr && run_target != nullptr)
        lift_locator = model.MakeLocator(*run_target, *lift);

    CHECK(!model.Project().targets[0].has_title_host);
    CHECK(!model.InferTitleHostProfile("main"));
    CHECK(!model.Project().targets[0].has_title_host);
    CHECK(model.InferredTitleHostProfile("main") == nullptr);
    CHECK(!model.TitleHostInferenceIssue().empty());

    BuildRunRequest premature_build;
    premature_build.target_id = "main";
    CHECK(!model.StartBuild(premature_build));
    CHECK(model.State() == WorkerState::Failed);
    CHECK(model.Operation() == WorkerOperation::Build);
    CHECK(DiagnosticsContain(model, "generate the selected target"));
    Analyze(model);

    RunRequest no_overwrite;
    no_overwrite.analyze_only = false;
    no_overwrite.force = false;
    no_overwrite.runtime_directory = (source / "runtime").string();
    CHECK(model.Start(no_overwrite));
    model.Wait();
    CHECK(model.State() == WorkerState::Failed);
    CHECK(std::filesystem::exists(sentinel));
    CHECK(ReadText(sentinel) == "preserve-me");

    OverrideEdit block_entry;
    block_entry.locator = lift_locator;
    block_entry.action = PORPOISE_OVERRIDE_OMIT;
    CHECK(model.ApplyOverrides({block_entry}));
    CHECK(model.State() == WorkerState::Running);
    model.Wait();
    const auto *blocked_entry = FindFunction(
        model.RunResult(), "lift_me");
    CHECK(blocked_entry != nullptr &&
          blocked_entry->action == PORPOISE_PLAN_ACTION_OMIT);
    CHECK(model.State() == WorkerState::Failed);
    RunRequest blocked_generation = no_overwrite;
    blocked_generation.force = true;
    CHECK(model.Start(blocked_generation));
    model.Wait();
    CHECK(model.State() == WorkerState::Failed);
    CHECK(std::filesystem::exists(sentinel));
    CHECK(ReadText(sentinel) == "preserve-me");
    CHECK(DiagnosticsContain(model, "entry"));

    block_entry.action = PORPOISE_OVERRIDE_AUTO;
    CHECK(model.ApplyOverrides({block_entry}));
    model.Wait();
    Analyze(model);

    model.Project().targets[0].strict = false;
    model.MarkDirty();
    CHECK(model.Autosave());
    CHECK(std::filesystem::exists(model.AutosavePath()));
    const auto document_time =
        std::filesystem::last_write_time(project_path, error);
    CHECK(!error);
    std::filesystem::last_write_time(
        model.AutosavePath(), document_time + std::chrono::seconds(2), error);
    CHECK(!error);

    WorkbenchModel recovered;
    CHECK(recovered.LoadProject(project_path.string()));
    CHECK(recovered.HasNewerAutosave());
    CHECK(recovered.RecoverAutosave(project_path.string()));
    CHECK(recovered.Dirty());
    CHECK(!recovered.Project().targets[0].strict);
    CHECK(recovered.Project().path != nullptr);
    if (recovered.Project().path != nullptr) {
        CHECK(std::filesystem::path(recovered.Project().path)
                  .lexically_normal() ==
              std::filesystem::absolute(project_path).lexically_normal());
    }
    CHECK(recovered.Save());
    CHECK(!recovered.Dirty());
    CHECK(recovered.Project().path != nullptr);
    if (recovered.Project().path != nullptr) {
        CHECK(std::filesystem::path(recovered.Project().path)
                  .lexically_normal() ==
              std::filesystem::absolute(project_path).lexically_normal());
    }

    for (unsigned int index = 0; index < 48; ++index) {
        const std::string id = "cancel-" + std::to_string(index);
        CHECK(model.AddTarget(id));
        const auto target_index = model.Project().target_count - 1;
        CHECK(model.SetTargetPath(
            target_index, false, Fixture(source, "input").string()));
        CHECK(model.SetTargetPath(
            target_index, true,
            (temporary / ("cancel-output-" + std::to_string(index))).string()));
        CHECK(model.SetTargetEntry(target_index, "lift_me"));
        CHECK(model.SetTargetSkipList(
            target_index, Fixture(source, "skip.txt").string()));
    }
    RunRequest cancellable;
    cancellable.analyze_only = true;
    CHECK(model.Start(cancellable));
    model.Cancel();
    CHECK(model.State() == WorkerState::Cancelling);
    model.Wait();
    CHECK(model.State() == WorkerState::Cancelled);
    CHECK(model.LastExitCode() == PORPOISE_EXIT_CANCELLED);

    std::filesystem::remove_all(resolved_temporary, error);
}

void TestRuntimePreflight(
    const std::filesystem::path &source,
    const std::filesystem::path &build,
    const std::filesystem::path &self) {
    const auto temporary = build / "workbench-runtime-preflight-test";
    std::error_code error;
    std::filesystem::remove_all(temporary, error);
    error.clear();
    std::filesystem::create_directories(temporary, error);
    CHECK(!error);
#if defined(_WIN32)
    constexpr const char *executable_suffix = ".exe";
#else
    constexpr const char *executable_suffix = "";
#endif
    const auto tool_tag = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto copy_tool = [&](const std::string &name) {
        /* Keep Windows compiler-runtime DLL discovery identical to the real
         * model-test executable by placing its synthetic tool aliases beside
         * it. The unique suffix keeps parallel build directories isolated. */
        const auto path = self.parent_path() /
            ("porpoise-fake-" + name + "-" + tool_tag + executable_suffix);
        error.clear();
        std::filesystem::copy_file(
            self, path, std::filesystem::copy_options::overwrite_existing,
            error);
        CHECK(!error);
        if (!error) {
            const auto permissions = std::filesystem::status(self, error)
                                         .permissions();
            CHECK(!error);
            if (!error) std::filesystem::permissions(path, permissions, error);
            CHECK(!error);
        }
        return path;
    };
    const auto meson = copy_tool("meson");
    const auto slow_meson = copy_tool("slow-meson");
    const auto c_compiler = copy_tool("cc");
    const auto cpp_compiler = copy_tool("cxx");
    const auto mismatched_cpp = copy_tool("mismatch-cxx");

    WorkbenchModel model;
    CHECK(model.NewProject());
    CHECK(model.AddTarget("runtime"));
    const auto project = temporary / "runtime.porpoise.json";
    CHECK(model.SaveAs(project.string()));

    BuildRunRequest request;
    request.target_id = "runtime";
    request.generated_directory =
        (source / "tests" / "fixtures" / "build_core" / "generated")
            .string();
    request.libporpoise_directory =
        (source / "tests" / "fixtures" / "build_core" / "libPorpoise")
            .string();
    request.title_host_directory =
        (source / "tests" / "fixtures" / "build_core" / "title-host")
            .string();
    request.meson_executable = meson.string();
    request.c_compiler = c_compiler.string();
    request.cpp_compiler = cpp_compiler.string();
    request.generated_plan_digest = "synthetic-plan";
    request.build_type = "debugoptimized";

    CHECK(!model.RuntimePreflightRequestMatches(request));
    CHECK(model.RuntimePreflightResult() == nullptr);
    CHECK(model.StartRuntimePreflight(request));
    CHECK(model.Operation() == WorkerOperation::RuntimePreflight);
    CHECK(model.State() == WorkerState::Running);
    model.Wait();
    CHECK(model.State() == WorkerState::Succeeded);
    CHECK(model.LastExitCode() == PORPOISE_EXIT_OK);
    CHECK(model.RuntimePreflightRequestMatches(request));
    const auto *preflight = model.RuntimePreflightResult();
    CHECK(preflight != nullptr);
    if (preflight != nullptr) {
        CHECK(std::string(preflight->meson_version) == "1.3.0");
        CHECK(std::string(preflight->compiler_family) == "gcc");
        CHECK(std::string(preflight->compiler_version) == "13.2.0");
        CHECK(std::string(preflight->compiler_target) ==
              "x86_64-porpoise-test");
    }

    BuildRunRequest changed = request;
    changed.cpp_compiler = mismatched_cpp.string();
    CHECK(!model.RuntimePreflightRequestMatches(changed));
    CHECK(model.StartRuntimePreflight(changed));
    model.Wait();
    CHECK(model.State() == WorkerState::Failed);
    CHECK(model.RuntimePreflightRequestMatches(changed));
    CHECK(model.RuntimePreflightResult() == nullptr);
    CHECK(DiagnosticsContain(model, "matched toolchain"));

    CHECK(model.StartRuntimePreflight(request));
    model.Wait();
    CHECK(model.State() == WorkerState::Succeeded);
    CHECK(model.RuntimePreflightResult() != nullptr);
    model.ClearRuntimePreflight();
    CHECK(!model.RuntimePreflightRequestMatches(request));
    CHECK(model.RuntimePreflightResult() == nullptr);

    BuildRunRequest cancelled = request;
    cancelled.meson_executable = slow_meson.string();
    CHECK(model.StartRuntimePreflight(cancelled));
    model.Cancel();
    CHECK(model.State() == WorkerState::Cancelling);
    model.Wait();
    CHECK(model.State() == WorkerState::Cancelled);
    CHECK(model.LastExitCode() == PORPOISE_EXIT_CANCELLED);
    CHECK(model.RuntimePreflightResult() == nullptr);

    for (const auto &tool : {meson, slow_meson, c_compiler, cpp_compiler,
                             mismatched_cpp}) {
        error.clear();
        std::filesystem::remove(tool, error);
        CHECK(!error);
    }
    std::filesystem::remove_all(temporary, error);
}

void TestCacheCleanup(const std::filesystem::path &build) {
    const auto nonce = std::chrono::high_resolution_clock::now()
                           .time_since_epoch().count();
    const auto temporary = build /
        ("workbench-cache-cleanup-" + std::to_string(nonce));
    const auto project_directory = temporary / "project";
    const auto project = project_directory / "recovery.porpoise.json";
    const auto cache_parent = project_directory / ".porpoise-build";
    const auto outside = temporary / "outside";
    const auto sentinel = project_directory / "keep.txt";
    const auto outside_sentinel = outside / "keep.txt";
    std::error_code error;
    std::filesystem::create_directories(cache_parent, error);
    CHECK(!error);
    std::filesystem::create_directories(outside, error);
    CHECK(!error);
    WriteText(project, "{}\n");
    WriteText(sentinel, "keep\n");
    WriteText(outside_sentinel, "outside\n");

    std::string cleanup_error;
    auto target_cache = [&](const std::string &id) {
        char key[PORPOISE_RECOVERY_TARGET_CACHE_KEY_SIZE];
        CHECK(porpoise_recovery_target_cache_key(id.c_str(), key));
        return cache_parent / key;
    };
    for (const auto *legacy_id : {"..", ".", "target/escape",
                                  "target\\escape", "target.name"}) {
        CHECK(RemoveTargetBuildCache(
            project.string(), legacy_id, &cleanup_error));
        CHECK(cleanup_error.empty());
        CHECK(std::filesystem::is_regular_file(sentinel));
        CHECK(std::filesystem::is_regular_file(outside_sentinel));
    }
    CHECK(!RemoveTargetBuildCache(project.string(), "", &cleanup_error));
    CHECK(!cleanup_error.empty());

    const auto ordinary = target_cache("safe");
    std::filesystem::create_directories(ordinary / "digest", error);
    CHECK(!error);
    WriteText(ordinary / "digest" / "artifact", "generated\n");
    CHECK(RemoveTargetBuildCache(
        project.string(), "safe", &cleanup_error));
    CHECK(cleanup_error.empty());
    CHECK(!std::filesystem::exists(ordinary));
    CHECK(std::filesystem::is_regular_file(sentinel));

    error.clear();
    std::filesystem::create_directory_symlink(outside, ordinary, error);
    if (!error) {
        CHECK(RemoveTargetBuildCache(
            project.string(), "safe", &cleanup_error));
        CHECK(!std::filesystem::exists(ordinary));
        CHECK(std::filesystem::is_regular_file(outside_sentinel));
    }

    error.clear();
    std::filesystem::remove(cache_parent, error);
    CHECK(!error);
    std::filesystem::create_directory_symlink(outside, cache_parent, error);
    if (!error) {
        CHECK(!RemoveTargetBuildCache(
            project.string(), "safe", &cleanup_error));
        CHECK(cleanup_error.find("filesystem link") != std::string::npos);
        CHECK(std::filesystem::is_regular_file(outside_sentinel));
        std::filesystem::remove(cache_parent, error);
        CHECK(!error);
    }

    std::filesystem::remove_all(temporary, error);
    CHECK(!error);
}

}  // namespace

int main(int argc, char **argv) {
    const int fake_tool_result = RunFakePreflightTool(argc, argv);
    if (fake_tool_result >= 0) return fake_tool_result;
    if (argc != 3) {
        std::cerr << "usage: test_workbench_model SOURCE_ROOT BUILD_ROOT\n";
        return 2;
    }
    TestModel(std::filesystem::absolute(argv[1]),
              std::filesystem::absolute(argv[2]));
    TestRuntimePreflight(
        std::filesystem::absolute(argv[1]),
        std::filesystem::absolute(argv[2]),
        std::filesystem::absolute(argv[0]));
    TestCacheCleanup(std::filesystem::absolute(argv[2]));
    if (failures != 0) {
        std::cerr << failures << " workbench model test(s) failed\n";
        return 1;
    }
    return 0;
}
