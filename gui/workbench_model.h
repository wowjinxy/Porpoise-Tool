#ifndef PORPOISE_GUI_WORKBENCH_MODEL_H
#define PORPOISE_GUI_WORKBENCH_MODEL_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include "porpoise/build.h"
#include "porpoise/recovery_annotation.h"
#include "porpoise/recovery_runner.h"
#include "porpoise/recovery_title_host.h"
#include "porpoise/util.h"
}

namespace porpoise::gui {

/* Safely remove only the derived cache component for a logical target id. */
bool RemoveTargetBuildCache(const std::string &project_file,
                            const std::string &target_id,
                            std::string *error_out);

enum class WorkerState {
    Idle,
    Running,
    Cancelling,
    Succeeded,
    Failed,
    Cancelled
};

enum class WorkerOperation {
    None,
    Recovery,
    Replan,
    RuntimePreflight,
    Build,
    Run
};

struct ProgressSnapshot {
    PorpoiseOperationPhase phase = PORPOISE_PHASE_LOAD;
    std::size_t completed = 0;
    std::size_t total = 0;
    std::string detail;
};

struct RunRequest {
    std::vector<std::string> target_ids;
    bool analyze_only = true;
    bool force = false;
    std::string report_path;
    std::string runtime_directory;
    std::string dtk_path;
};

struct BuildProgressSnapshot {
    PorpoiseBuildPhase phase = PORPOISE_BUILD_PHASE_PREFLIGHT;
    std::size_t completed = 0;
    std::size_t total = 0;
    std::string detail;
};

/*
 * Owns every string referenced by the C build request for the lifetime of the
 * background worker. Machine-specific paths deliberately live here instead
 * of in the portable recovery project.
 */
struct BuildRunRequest {
    std::string target_id;
    std::string generated_directory;
    std::string libporpoise_directory;
    std::string title_host_directory;
    std::string meson_executable;
    std::string c_compiler;
    std::string cpp_compiler;
    std::string objdump_executable;
    std::string build_type = "debugoptimized";
    std::string generated_plan_digest;
    std::string run_working_directory;
    std::string dvd_root;
    std::string trace_file;
    std::size_t frame_limit = 0;
    std::vector<std::string> runtime_search_directories;
    std::vector<std::string> run_arguments;
    bool allow_copy_fallback = false;
    bool force_reconfigure = false;
    bool run = false;
};

/* A copied locator remains valid after the immutable run result is released. */
struct FunctionLocator {
    std::string target;
    std::string module;
    std::uint32_t address = 0;
    std::uint32_t size = 0;
    std::string normalized_fingerprint;
};

/*
 * A copied description of an ordinary named Program data object. The range
 * metadata is populated, but callers use MakeDataLocator() to validate and
 * fingerprint the exact full object or subrange they intend to annotate.
 */
struct DataObjectRecord {
    FunctionLocator locator;
    std::string name;
    std::string translation_unit;
    std::string section;
};

struct OverrideEdit {
    FunctionLocator locator;
    PorpoiseOverrideAction action = PORPOISE_OVERRIDE_AUTO;
    std::string contract_name;
    bool acknowledge_conflict = false;
};

struct AbiArgumentDraft {
    std::string name;
    PorpoiseAbiType type = PORPOISE_ABI_U32;
    PorpoiseAbiRegisterClass register_class = PORPOISE_ABI_REGISTER_GPR;
    unsigned int register_index = 3;
};

/*
 * The first release intentionally edits ordinary direct-call mappings only.
 * Stateful adapter contracts are visible through LoadedAbiManifest(), but are
 * never rewritten by this generic editor.
 */
struct DirectAbiDraft {
    PorpoiseAbiKind kind = PORPOISE_ABI_IMPORT;
    std::string symbol;
    std::string wrapper;
    std::string header;
    PorpoiseAbiType result_type = PORPOISE_ABI_VOID;
    PorpoiseAbiRegisterClass result_register_class =
        PORPOISE_ABI_REGISTER_NONE;
    unsigned int result_register_index = 0;
    std::vector<AbiArgumentDraft> arguments;
};

/*
 * Shared by the desktop table and headless smoke tests so case-insensitive,
 * whitespace-tokenized filtering does not depend on a Dear ImGui context.
 */
bool FunctionFilterMatches(
    const std::string &filter,
    const std::vector<std::string> &searchable_fields);

class WorkbenchModel {
public:
    WorkbenchModel();
    ~WorkbenchModel();

    WorkbenchModel(const WorkbenchModel &) = delete;
    WorkbenchModel &operator=(const WorkbenchModel &) = delete;

    bool NewProject();
    bool LoadProject(const std::string &path);
    bool RecoverAutosave(const std::string &document_path);
    bool Save();
    bool SaveAs(const std::string &path);
    bool Autosave();
    bool HasNewerAutosave() const;
    bool SetUntitledRecoveryDirectory(const std::string &path);

    const std::string &DocumentPath() const { return document_path_; }
    std::string AutosavePath() const;
    bool Dirty() const { return dirty_; }
    void MarkDirty();

    PorpoiseRecoveryProject &Project() { return project_; }
    const PorpoiseRecoveryProject &Project() const { return project_; }

    bool AddTarget(const std::string &preferred_id);
    bool RemoveTarget(std::size_t target_index);
    bool SetTargetId(std::size_t target_index, const std::string &id);
    bool SetTargetEntry(std::size_t target_index, const std::string &entry);
    bool SetTargetPath(std::size_t target_index, bool output,
                       const std::string &value);
    bool SetTargetSkipList(std::size_t target_index,
                           const std::string &value);

    bool AddSharedPath(bool abi, const std::string &value);
    bool SetSharedPath(bool abi, std::size_t index,
                       const std::string &value);
    bool RemoveSharedPath(bool abi, std::size_t index);

    bool AddSymbolSource(std::size_t target_index,
                         PorpoiseSymbolSourceKind kind,
                         const std::string &path);
    bool RemoveSymbolSource(std::size_t target_index, std::size_t index);
    bool SetSymbolSourcePath(std::size_t target_index, std::size_t index,
                             bool auxiliary, const std::string &value);
    bool SetSymbolSourceModule(std::size_t target_index, std::size_t index,
                               const std::string &module);

    bool Start(const RunRequest &request);
    bool StartRuntimePreflight(const BuildRunRequest &request);
    bool StartBuild(const BuildRunRequest &request);
    void Cancel();
    bool PollWorker();
    void Wait();
    WorkerState State() const { return worker_state_.load(); }
    WorkerOperation Operation() const { return worker_operation_; }
    int LastExitCode() const { return last_exit_code_.load(); }
    ProgressSnapshot Progress() const;
    BuildProgressSnapshot BuildProgress() const;
    std::vector<std::string> Logs() const;
    const PorpoiseBuildResult *BuildResult() const;
    void ClearBuildResult();
    const PorpoiseBuildPreflight *RuntimePreflightResult() const;
    bool RuntimePreflightRequestMatches(
        const BuildRunRequest &request) const;
    void ClearRuntimePreflight();

    /* Inference is a review draft only; it never changes the project. */
    bool InferTitleHostProfile(const std::string &target_id);
    const PorpoiseRecoveryTitleHostProfile *InferredTitleHostProfile(
        const std::string &target_id) const;
    const std::string &TitleHostInferenceIssue() const {
        return title_host_inference_issue_;
    }
    bool AcceptInferredTitleHostProfile(const std::string &target_id,
                                        bool initialize_dvd);
    void DiscardInferredTitleHostProfile();

    /* Mirrors the target-selection rules used by a project run. */
    bool SelectedOutputsExist(
        const std::vector<std::string> &target_ids) const;

    const PorpoiseRecoveryRunResult *RunResult() const;
    const PorpoiseDiagnostics &Diagnostics() const { return diagnostics_; }
    const PorpoiseDiagnostic *PrimaryDiagnostic() const;

    FunctionLocator MakeLocator(
        const PorpoiseRecoveryRunTarget &target,
        const PorpoiseFunctionPlanView &view) const;
    std::vector<DataObjectRecord> DataObjects(
        const PorpoiseRecoveryRunTarget &target) const;
    bool MakeDataLocator(
        const PorpoiseRecoveryRunTarget &target,
        std::uint32_t address,
        std::uint32_t size,
        FunctionLocator *locator_out) const;
    bool ApplyOverrides(const std::vector<OverrideEdit> &edits);
    bool RebindOverride(std::size_t target_index, std::size_t override_index,
                        const std::string &module, std::uint32_t address,
                        std::uint32_t size,
                        const std::string &normalized_fingerprint);
    bool RemoveOverride(std::size_t target_index,
                        std::size_t override_index);

    bool UpsertAnnotation(
        const FunctionLocator &locator,
        PorpoiseRecoveryAnnotationInterpretation interpretation,
        std::uint32_t element_count,
        const std::string &encoding);
    bool RemoveAnnotation(const FunctionLocator &locator);
    bool RemoveAnnotationAt(std::size_t target_index,
                            std::size_t annotation_index);

    const PorpoiseAbiManifest *LoadedAbiManifest(
        std::size_t run_target_index) const;
    static DirectAbiDraft DraftFromContract(const PorpoiseAbiFunction &value);
    bool LoadDirectAbiDraftRecovery(
        const std::string &path,
        std::vector<DirectAbiDraft> *functions_out);
    bool WriteDirectAbiDraftRecovery(
        const std::string &path,
        const std::vector<DirectAbiDraft> &functions);
    bool WriteDirectAbiManifest(const std::string &path,
                                const std::vector<DirectAbiDraft> &functions,
                                bool add_to_project);

private:
    static void ProgressThunk(void *user_data, PorpoiseOperationPhase phase,
                              std::size_t completed, std::size_t total,
                              const char *detail);
    static bool CancelThunk(void *user_data);
    static void BuildProgressThunk(void *user_data, PorpoiseBuildPhase phase,
                                   std::size_t completed, std::size_t total,
                                   const char *detail);
    static void BuildLogThunk(void *user_data, PorpoiseBuildPhase phase,
                              bool standard_error, const char *text,
                              std::size_t length);
    static bool BuildCancelThunk(void *user_data);
    void OnProgress(PorpoiseOperationPhase phase, std::size_t completed,
                    std::size_t total, const char *detail);
    void OnBuildProgress(PorpoiseBuildPhase phase, std::size_t completed,
                         std::size_t total, const char *detail);
    void OnBuildLog(PorpoiseBuildPhase phase, bool standard_error,
                    const char *text, std::size_t length);
    void WorkerMain(RunRequest request);
    void RuntimePreflightWorkerMain(BuildRunRequest request);
    void BuildWorkerMain(BuildRunRequest request);
    bool StartReplan(const std::vector<std::string> &target_ids);
    void ReplanWorkerMain(std::vector<std::string> target_ids);
    void AdoptWorkerProject();
    void ClearRunResult();
    void ClearInferredTitleHostProfile();
    void ResetDiagnostics();
    void AddLocalDiagnostic(PorpoiseSeverity severity,
                            const std::string &message);
    int ReplanLoadedTarget(
        const std::string &target_id,
        const PorpoiseOperationCallbacks *operation);
    std::string ResolveProjectPath(const std::string &value) const;
    bool SetPath(PorpoiseRecoveryPath &path, const std::string &value);
    PorpoiseRecoveryRunTarget *FindRunTarget(const std::string &target);
    const PorpoiseRecoveryRunTarget *FindRunTarget(
        const std::string &target) const;

    PorpoiseRecoveryProject project_{};
    PorpoiseRecoveryProject worker_project_{};
    bool worker_project_ready_ = false;
    bool replan_worker_ = false;
    PorpoiseRecoveryRunResult run_result_{};
    PorpoiseDiagnostics diagnostics_{};
    std::string document_path_;
    std::string untitled_recovery_directory_;
    bool dirty_ = false;

    std::thread worker_;
    std::atomic<WorkerState> worker_state_{WorkerState::Idle};
    std::atomic<bool> cancel_requested_{false};
    std::atomic<bool> worker_finished_{false};
    std::atomic<int> last_exit_code_{PORPOISE_EXIT_OK};
    WorkerOperation worker_operation_ = WorkerOperation::None;

    mutable std::mutex progress_mutex_;
    ProgressSnapshot progress_{};
    BuildProgressSnapshot build_progress_{};
    mutable std::mutex log_mutex_;
    std::vector<std::string> logs_;
    PorpoiseBuildResult build_result_{};
    bool build_result_ready_ = false;
    PorpoiseBuildPreflight runtime_preflight_{};
    BuildRunRequest runtime_preflight_request_{};
    bool runtime_preflight_attempted_ = false;
    bool runtime_preflight_ready_ = false;
    PorpoiseRecoveryTitleHostProfile inferred_title_host_{};
    std::string inferred_title_host_target_;
    std::string title_host_inference_issue_;
    bool inferred_title_host_ready_ = false;
};

const char *WorkerStateName(WorkerState state);
const char *WorkerOperationName(WorkerOperation operation);

}  // namespace porpoise::gui

#endif
