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
#include "porpoise/recovery_annotation.h"
#include "porpoise/recovery_runner.h"
}

namespace porpoise::gui {

enum class WorkerState {
    Idle,
    Running,
    Cancelling,
    Succeeded,
    Failed,
    Cancelled
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
    void Cancel();
    bool PollWorker();
    void Wait();
    WorkerState State() const { return worker_state_.load(); }
    int LastExitCode() const { return last_exit_code_.load(); }
    ProgressSnapshot Progress() const;
    std::vector<std::string> Logs() const;

    /* Mirrors the target-selection rules used by a project run. */
    bool SelectedOutputsExist(
        const std::vector<std::string> &target_ids) const;

    const PorpoiseRecoveryRunResult *RunResult() const;
    const PorpoiseDiagnostics &Diagnostics() const { return diagnostics_; }

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
    void OnProgress(PorpoiseOperationPhase phase, std::size_t completed,
                    std::size_t total, const char *detail);
    void WorkerMain(RunRequest request);
    bool StartReplan(const std::vector<std::string> &target_ids);
    void ReplanWorkerMain(std::vector<std::string> target_ids);
    void AdoptWorkerProject();
    void ClearRunResult();
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

    mutable std::mutex progress_mutex_;
    ProgressSnapshot progress_{};
    mutable std::mutex log_mutex_;
    std::vector<std::string> logs_;
};

const char *WorkerStateName(WorkerState state);

}  // namespace porpoise::gui

#endif
