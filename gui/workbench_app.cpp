#include "workbench_app.h"

#include "workbench_model.h"

#include <SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>
#include <portable-file-dialogs.h>

extern "C" {
#include "porpoise/sha256.h"
#include "porpoise/util.h"
}

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace porpoise::gui {
namespace {

constexpr const char *kProjectFilters[] = {
    "Porpoise projects", "*.porpoise.json",
    "JSON files", "*.json",
    "All files", "*"
};

std::string Text(const char *value) {
    return value == nullptr ? std::string() : std::string(value);
}

bool InputTextString(const char *label, std::string &value,
                     ImGuiInputTextFlags flags = 0) {
    std::array<char, PORPOISE_PATH_CAPACITY> buffer{};
    const auto length = std::min(value.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), value.data(), length);
    if (!ImGui::InputText(label, buffer.data(), buffer.size(), flags))
        return false;
    value = buffer.data();
    return true;
}

bool InputOptionalPath(const char *label, std::string &value,
                       bool allow_folder, const char *file_filter = "*") {
    bool changed = InputTextString(label, value);
    ImGui::SameLine();
    ImGui::PushID(label);
    if (ImGui::Button("File...")) {
        auto selected = pfd::open_file(
            "Select file", value,
            {"Matching files", file_filter, "All files", "*"},
            pfd::opt::none).result();
        if (!selected.empty()) {
            value = selected.front();
            changed = true;
        }
    }
    if (allow_folder) {
        ImGui::SameLine();
        if (ImGui::Button("Folder...")) {
            const auto selected = pfd::select_folder(
                "Select folder", value).result();
            if (!selected.empty()) {
                value = selected;
                changed = true;
            }
        }
    }
    ImGui::PopID();
    return changed;
}

bool InputFolderPath(const char *label, std::string &value) {
    bool changed = InputTextString(label, value);
    ImGui::SameLine();
    ImGui::PushID(label);
    if (ImGui::Button("Folder...")) {
        const auto selected = pfd::select_folder(
            "Select folder", value).result();
        if (!selected.empty()) {
            value = selected;
            changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

std::string Hex(std::uint32_t value, unsigned int width = 8) {
    std::ostringstream output;
    output << "0x" << std::hex << std::uppercase << std::setw(width)
           << std::setfill('0') << value;
    return output.str();
}

std::string ReadTextFile(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

std::string ResolveExecutable(const std::string &value) {
    if (value.empty()) return {};
    std::error_code error;
    const std::filesystem::path requested(value);
    if (requested.has_parent_path() || requested.is_absolute()) {
#if defined(_WIN32)
        const auto extension = Lower(requested.extension().string());
        if (extension == ".cmd" || extension == ".bat") return {};
#endif
        const auto absolute = std::filesystem::absolute(requested, error);
        if (!error && std::filesystem::is_regular_file(absolute, error) &&
            !error) return absolute.lexically_normal().string();
        return {};
    }
    const char *path_value = std::getenv("PATH");
    if (path_value == nullptr) return {};
#if defined(_WIN32)
    constexpr char separator = ';';
    const std::array<const char *, 2> suffixes = {"", ".exe"};
#else
    constexpr char separator = ':';
    const std::array<const char *, 1> suffixes = {""};
#endif
    std::istringstream path_stream(path_value);
    std::string directory;
    while (std::getline(path_stream, directory, separator)) {
        if (directory.empty()) continue;
        for (const char *suffix : suffixes) {
            const auto candidate = std::filesystem::path(directory) /
                (value + suffix);
            error.clear();
            if (std::filesystem::is_regular_file(candidate, error) && !error)
                return std::filesystem::absolute(candidate).lexically_normal()
                    .string();
        }
    }
    return {};
}

bool IsLibPorpoiseCheckout(const std::filesystem::path &directory) {
    std::error_code error;
    return std::filesystem::is_directory(directory, error) && !error &&
           std::filesystem::is_regular_file(directory / "meson.build", error) &&
           !error;
}

struct LibPorpoiseCapabilities {
    bool checkout = false;
    bool canonical_fifo = false;
    bool queued_canonical_fifo = false;
    bool host_xfb_observation = false;
    bool gpu_resident_xfb = false;
    unsigned int canonical_fifo_version = 0;
};

unsigned int DefinedUnsignedVersion(const std::string &source,
                                    const std::string &name) {
    const auto definition = source.find("#define " + name);
    if (definition == std::string::npos) return 0;
    auto cursor = definition + std::string("#define ").size() + name.size();
    while (cursor < source.size() &&
           std::isspace(static_cast<unsigned char>(source[cursor])) != 0)
        ++cursor;
    unsigned int value = 0;
    bool found_digit = false;
    while (cursor < source.size() &&
           std::isdigit(static_cast<unsigned char>(source[cursor])) != 0) {
        found_digit = true;
        const unsigned int digit =
            static_cast<unsigned int>(source[cursor] - '0');
        if (value > (std::numeric_limits<unsigned int>::max() - digit) / 10U)
            return 0;
        value = value * 10U + digit;
        ++cursor;
    }
    return found_digit ? value : 0;
}

LibPorpoiseCapabilities InspectLibPorpoiseCapabilities(
    const std::filesystem::path &directory) {
    LibPorpoiseCapabilities capabilities;
    capabilities.checkout = IsLibPorpoiseCheckout(directory);
    if (!capabilities.checkout) return capabilities;

    const auto command_processor = ReadTextFile(
        (directory / "include" / "simulator" /
         "sim_gx_CommandProcessor.h").string());
    capabilities.canonical_fifo_version = DefinedUnsignedVersion(
        command_processor,
        "SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION");
    capabilities.queued_canonical_fifo =
        capabilities.canonical_fifo_version >= 2U &&
        command_processor.find(
            "SIM_GX_CommandProcessor_QueueCanonicalBytes") !=
            std::string::npos;
    capabilities.canonical_fifo =
        capabilities.queued_canonical_fifo ||
        (capabilities.canonical_fifo_version >= 1U &&
         command_processor.find(
             "SIM_GX_CommandProcessor_SendCanonicalBytes") !=
             std::string::npos);

    const auto host_xfb = ReadTextFile(
        (directory / "include" / "simulator" / "sim_gx_Xfb.hpp")
            .string());
    capabilities.host_xfb_observation =
        host_xfb.find("HostXfbExecutionStats") != std::string::npos &&
        host_xfb.find("GetHostXfbExecutionStats") != std::string::npos &&
        host_xfb.find("lastPresentedGuestAddress") != std::string::npos &&
        host_xfb.find("presentations") != std::string::npos;
    capabilities.gpu_resident_xfb =
        capabilities.host_xfb_observation &&
        host_xfb.find("gpuResidentDisplayCopies") != std::string::npos &&
        host_xfb.find("gpuResidentPresentations") != std::string::npos;
    return capabilities;
}

std::uintmax_t DirectoryAllocatedSize(const std::filesystem::path &root) {
    std::error_code error;
    if (!std::filesystem::is_directory(root, error) || error) return 0;
    std::uintmax_t total = 0;
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error) && !error) {
            const auto size = iterator->file_size(error);
            if (!error && total <= std::numeric_limits<std::uintmax_t>::max() -
                                      size) total += size;
        }
        error.clear();
        iterator.increment(error);
    }
    return total;
}

std::string HumanSize(std::uintmax_t bytes) {
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units)) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream output;
    output << std::fixed << std::setprecision(unit == 0 ? 0 : 1)
           << value << ' ' << units[unit];
    return output.str();
}

const char *SeverityName(PorpoiseSeverity severity) {
    switch (severity) {
    case PORPOISE_SEVERITY_INFO: return "info";
    case PORPOISE_SEVERITY_WARNING: return "warning";
    case PORPOISE_SEVERITY_ERROR: return "error";
    }
    return "unknown";
}

std::string DiagnosticHint(const PorpoiseDiagnostic *diagnostic) {
    if (diagnostic == nullptr || diagnostic->message == nullptr) return {};
    const std::string message = diagnostic->message;
    if (message.find("outside of section .init") != std::string::npos ||
        message.find("Range 0x00000000") != std::string::npos) {
        return "The selected DTK build cannot disassemble this executable "
               "ELF. Select the Porpoise-patched DTK executable, then "
               "Analyze again.";
    }
    if (message.find("overlaps the Porpoise project file") !=
            std::string::npos ||
        (message.find("output") != std::string::npos &&
         message.find("overlaps") != std::string::npos)) {
        return "Choose an output folder that is separate from the project, "
               "input, cache, maps, catalogs, and runtime directory.";
    }
    if (message.find("approximate host semantics") != std::string::npos) {
        return "Strict mode rejected an approximate lowering. Review the "
               "diagnostic, then turn off Strict in Advanced settings if "
               "that approximation is acceptable for this recovery pass.";
    }
    if (message.find("compiler") != std::string::npos ||
        message.find("Meson") != std::string::npos ||
        message.find("meson") != std::string::npos) {
        return "Open Runtime configuration and select one matching x64 C/C++ "
               "toolchain plus Meson 1.2 or newer.";
    }
    if (message.find("DLL") != std::string::npos ||
        message.find("runtime import") != std::string::npos) {
        return "Open Runtime configuration and correct the reported runtime "
               "or compiler path before running again.";
    }
    if (message.find("title_host") != std::string::npos ||
        message.find("title-host") != std::string::npos) {
        return "Review the target title-host profile; stale locators and "
               "startup functions that are not Lift block Build and Run.";
    }
    if (message.find("does not exist") != std::string::npos ||
        message.find("could not open") != std::string::npos) {
        return "Check the selected source and tool paths, then retry.";
    }
    return "Open Diagnostics for the complete context, correct the setting "
           "shown there, then Analyze again.";
}

std::string RegisterText(const PorpoiseAbiValue &value) {
    if (value.register_class == PORPOISE_ABI_REGISTER_GPR)
        return "r" + std::to_string(value.register_index);
    if (value.register_class == PORPOISE_ABI_REGISTER_FPR)
        return "f" + std::to_string(value.register_index);
    return "none";
}

std::string RowKey(const FunctionLocator &locator) {
    return locator.target + "\x1f" + locator.module + "\x1f" +
           std::to_string(locator.address) + "\x1f" +
           std::to_string(locator.size) + "\x1f" +
           locator.normalized_fingerprint;
}

std::size_t InterpretationWidth(
    PorpoiseRecoveryAnnotationInterpretation interpretation) {
    switch (interpretation) {
    case PORPOISE_RECOVERY_ANNOTATION_S16_ARRAY:
    case PORPOISE_RECOVERY_ANNOTATION_U16_ARRAY:
    case PORPOISE_RECOVERY_ANNOTATION_UTF16: return 2;
    case PORPOISE_RECOVERY_ANNOTATION_S32_ARRAY:
    case PORPOISE_RECOVERY_ANNOTATION_U32_ARRAY:
    case PORPOISE_RECOVERY_ANNOTATION_F32_ARRAY:
    case PORPOISE_RECOVERY_ANNOTATION_POINTER32_ARRAY: return 4;
    case PORPOISE_RECOVERY_ANNOTATION_F64_ARRAY: return 8;
    default: return 1;
    }
}

std::uint64_t LoadInteger(const std::uint8_t *bytes, std::size_t width,
                          bool little_endian) {
    std::uint64_t value = 0;
    if (little_endian) {
        for (std::size_t index = 0; index < width; ++index)
            value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8);
    } else {
        for (std::size_t index = 0; index < width; ++index)
            value = (value << 8) | bytes[index];
    }
    return value;
}

std::string BytePreview(
    const PorpoiseRecoveryByteView &view,
    PorpoiseRecoveryAnnotationInterpretation interpretation,
    const std::string &encoding) {
    std::ostringstream output;
    const std::size_t shown = std::min<std::size_t>(view.size, 1024);
    if (interpretation == PORPOISE_RECOVERY_ANNOTATION_RAW_BYTES ||
        interpretation == PORPOISE_RECOVERY_ANNOTATION_ZERO_FILL) {
        bool all_zero = true;
        for (std::size_t row = 0; row < shown; row += 16) {
            output << Hex(view.address + static_cast<std::uint32_t>(row))
                   << "  ";
            for (std::size_t column = 0; column < 16; ++column) {
                if (row + column < shown) {
                    const auto value = view.bytes[row + column];
                    all_zero = all_zero && value == 0;
                    output << std::hex << std::setw(2) << std::setfill('0')
                           << static_cast<unsigned int>(value) << ' ';
                } else output << "   ";
            }
            output << " ";
            for (std::size_t column = 0;
                 column < 16 && row + column < shown; ++column) {
                const unsigned char value = view.bytes[row + column];
                output << (value >= 0x20 && value < 0x7f
                               ? static_cast<char>(value) : '.');
            }
            output << '\n';
        }
        if (interpretation == PORPOISE_RECOVERY_ANNOTATION_ZERO_FILL)
            output << (all_zero && shown == view.size
                           ? "verified zero-fill" : "contains nonzero bytes")
                   << '\n';
    } else if (interpretation == PORPOISE_RECOVERY_ANNOTATION_ASCII ||
               interpretation == PORPOISE_RECOVERY_ANNOTATION_UTF8 ||
               interpretation == PORPOISE_RECOVERY_ANNOTATION_SHIFT_JIS) {
        output << "encoding: " << encoding << "\n\"";
        for (std::size_t index = 0; index < shown; ++index) {
            const unsigned char value = view.bytes[index];
            if (value == '\n') output << "\\n";
            else if (value == '\r') output << "\\r";
            else if (value == '\t') output << "\\t";
            else if (value >= 0x20 && value < 0x7f)
                output << static_cast<char>(value);
            else output << "\\x" << std::hex << std::setw(2)
                        << std::setfill('0')
                        << static_cast<unsigned int>(value);
        }
        output << "\"\n";
    } else {
        const std::size_t width = InterpretationWidth(interpretation);
        const bool little = encoding == "little-endian" ||
                            encoding == "utf-16le";
        const std::size_t count = std::min<std::size_t>(shown / width, 256);
        for (std::size_t index = 0; index < count; ++index) {
            const auto bits = LoadInteger(view.bytes + index * width,
                                          width, little);
            output << '[' << index << "] ";
            if (interpretation == PORPOISE_RECOVERY_ANNOTATION_F32_ARRAY) {
                const std::uint32_t word = static_cast<std::uint32_t>(bits);
                float value;
                std::memcpy(&value, &word, sizeof(value));
                output << value;
            } else if (interpretation ==
                       PORPOISE_RECOVERY_ANNOTATION_F64_ARRAY) {
                double value;
                std::memcpy(&value, &bits, sizeof(value));
                output << value;
            } else if (interpretation ==
                           PORPOISE_RECOVERY_ANNOTATION_POINTER32_ARRAY ||
                       interpretation ==
                           PORPOISE_RECOVERY_ANNOTATION_UTF16) {
                output << Hex(static_cast<std::uint32_t>(bits),
                              static_cast<unsigned int>(width * 2));
            } else if (interpretation ==
                           PORPOISE_RECOVERY_ANNOTATION_S8_ARRAY ||
                       interpretation ==
                           PORPOISE_RECOVERY_ANNOTATION_S16_ARRAY ||
                       interpretation ==
                           PORPOISE_RECOVERY_ANNOTATION_S32_ARRAY) {
                std::int64_t signed_value;
                if (width == 1) signed_value = static_cast<std::int8_t>(bits);
                else if (width == 2)
                    signed_value = static_cast<std::int16_t>(bits);
                else signed_value = static_cast<std::int32_t>(bits);
                output << signed_value;
            } else output << bits;
            output << '\n';
        }
    }
    if (shown != view.size)
        output << "... " << (view.size - shown) << " more bytes\n";
    return output.str();
}

struct TargetDraft {
    std::string id;
    std::string input;
    std::string output;
    std::string entry;
    std::string skip_list;
};

struct OverrideLocatorDraft {
    std::string module;
    std::string fingerprint;
    std::uint32_t address = 0;
    std::uint32_t size = 0;
};

enum class PendingFileAction {
    None,
    NewProject,
    OpenProject,
    Close
};

enum class GuidedAction {
    Analyze,
    Generate,
    ConfigureRuntime,
    Build,
    Run
};

const char *GuidedActionName(GuidedAction action) {
    switch (action) {
    case GuidedAction::Analyze: return "Analyze";
    case GuidedAction::Generate: return "Generate";
    case GuidedAction::ConfigureRuntime: return "Configure Runtime";
    case GuidedAction::Build: return "Build";
    case GuidedAction::Run: return "Run";
    }
    return "Continue";
}

struct RowRecord {
    std::size_t run_target_index = 0;
    std::size_t function_index = 0;
    const PorpoiseRecoveryRunTarget *run_target = nullptr;
    const PorpoiseFunctionPlanView *view = nullptr;
    FunctionLocator locator;
    std::string key;
    std::string source_name;
    std::string canonical_name;
    std::string unit;
    std::string section;
    std::string category;
    std::string confidence;
    std::string requested_action;
    std::string action;
    std::string binding;
    std::string provenance;
    std::string origin;
};

class WorkbenchApp {
public:
    explicit WorkbenchApp(std::string preference_directory)
        : preference_directory_(std::move(preference_directory)) {
        machine_state_path_ =
            (std::filesystem::path(preference_directory_) /
             "workbench-state.ini").string();
        imgui_ini_path_ =
            (std::filesystem::path(preference_directory_) /
             "imgui-layout.ini").string();
        std::error_code error;
        std::filesystem::create_directories(preference_directory_, error);
        if (!error)
            model_.SetUntitledRecoveryDirectory(preference_directory_);
        LoadMachineState();
        DiscoverRuntimeSettings();
    }

    ~WorkbenchApp() { SaveMachineState(); }

    const std::string &ImGuiIniPath() const { return imgui_ini_path_; }
    bool ShouldClose() const { return close_; }

    /*
     * Exercises the same dirty-document action routing used by the File menu.
     * The executable smoke test still initializes SDL and renders real ImGui
     * frames; this adds deterministic coverage for controls that would
     * otherwise require automating native file dialogs.
     */
    bool RunStartupSmokeScenario() {
        std::error_code error;
        std::filesystem::create_directories(preference_directory_, error);
        if (error) return false;
        ResetProjectMachineState();
        if (!trace_enabled_ || frame_limit_ != 3U) return false;
        trace_enabled_ = false;
        if (EffectiveRunFrameLimit() != 0U) return false;
        trace_enabled_ = true;

        /* Legacy schema-v1 IDs may contain path punctuation. Cache and trace
         * controls must use the same derived key as the C Build/Run core. */
        if (!model_.NewProject() || !model_.AddTarget("..")) return false;
        const auto legacy_id_project =
            std::filesystem::path(preference_directory_) /
            "legacy-id-smoke.porpoise.json";
        if (!model_.SaveAs(legacy_id_project.string())) return false;
        active_target_ = 0;
        char legacy_cache_key[PORPOISE_RECOVERY_TARGET_CACHE_KEY_SIZE] = {};
        if (!porpoise_recovery_target_cache_key("..", legacy_cache_key) ||
            ActiveBuildCacheRoot().filename().string() != legacy_cache_key ||
            ActiveBuildCacheRoot().parent_path().filename().string() !=
                ".porpoise-build") return false;
        if (!model_.NewProject()) return false;

        /* Exercise the actual startup routing for ABI-only work created before
         * an untitled project has ever been saved. */
        DirectAbiDraft startup_recovery_draft;
        startup_recovery_draft.symbol = "UntitledStartupRecovery";
        startup_recovery_draft.result_type = PORPOISE_ABI_U32;
        startup_recovery_draft.result_register_class =
            PORPOISE_ABI_REGISTER_GPR;
        startup_recovery_draft.result_register_index = 7;
        abi_drafts_ = {startup_recovery_draft};
        abi_drafts_dirty_ = true;
        bool startup_recovery_ok = model_.NewProject() && AutosaveAbiDrafts();
        ResetAbiEditor();
        if (startup_recovery_ok) {
            OpenInitial("");
            startup_recovery_ok = open_recovery_popup_ &&
                recovery_document_.empty();
            open_recovery_popup_ = false;
        }
        if (startup_recovery_ok) {
            startup_recovery_ok = RecoverAbiDrafts() &&
                abi_drafts_.size() == 1 &&
                abi_drafts_[0].symbol == "UntitledStartupRecovery" &&
                abi_drafts_[0].result_type == PORPOISE_ABI_U32 &&
                abi_drafts_[0].result_register_class ==
                    PORPOISE_ABI_REGISTER_GPR &&
                abi_drafts_[0].result_register_index == 7;
        }
        RemoveAbiAutosave();
        ResetAbiEditor();
        model_.NewProject();
        if (!startup_recovery_ok) return false;

        if (!model_.NewProject() || !model_.AddTarget("smoke") ||
            !model_.SetTargetPath(
                 0, false,
                 (std::filesystem::path(preference_directory_) /
                 "smoke-input").string()) ||
            !model_.SetTargetPath(
                0, true,
                (std::filesystem::path(preference_directory_) /
                 "smoke-output").string()) ||
            !model_.SetTargetEntry(0, "smoke-entry")) return false;
        const auto smoke_project =
            std::filesystem::path(preference_directory_) /
            "startup-smoke.porpoise.json";
        if (!model_.SaveAs(smoke_project.string()) ||
            !model_.SetTargetEntry(0, "smoke-entry-saved")) return false;

        RequestFileAction(PendingFileAction::OpenProject);
        if (pending_file_action_ != PendingFileAction::OpenProject ||
            !open_unsaved_popup_ || !model_.Dirty()) return false;
        CancelPendingFileAction();
        if (pending_file_action_ != PendingFileAction::None ||
            !model_.Dirty() || model_.Project().target_count != 1) return false;
        RequestFileAction(PendingFileAction::NewProject);
        if (pending_file_action_ != PendingFileAction::NewProject ||
            !FinishPendingFileAction(true) ||
            ReadTextFile(smoke_project.string()).find("smoke-entry-saved") ==
                std::string::npos ||
            model_.Dirty() || model_.Project().target_count != 0) return false;

        DirectAbiDraft recovery_draft;
        recovery_draft.symbol = "SmokeRecoveredContract";
        recovery_draft.wrapper = "PorpoiseSmokeRecoveredContract";
        recovery_draft.header = "porpoise/smoke_recovery.h";
        abi_drafts_.push_back(std::move(recovery_draft));
        /* Partial drafts must survive recovery before manifest validation. */
        abi_drafts_.push_back(DirectAbiDraft{});
        abi_drafts_dirty_ = true;
        if (!AutosaveAbiDrafts()) return false;
        abi_drafts_.clear();
        abi_drafts_dirty_ = false;
        if (!RecoverAbiDrafts() || abi_drafts_.size() != 2 ||
            abi_drafts_[0].symbol != "SmokeRecoveredContract" ||
            !abi_drafts_[1].symbol.empty() ||
            !abi_drafts_dirty_) return false;
        RemoveAbiAutosave();
        RequestFileAction(PendingFileAction::NewProject);
        if (pending_file_action_ != PendingFileAction::NewProject ||
            !DiscardPendingFileAction()) return false;
        if (pending_file_action_ != PendingFileAction::None ||
            model_.Dirty() || abi_drafts_dirty_ || !abi_drafts_.empty() ||
            model_.Project().target_count != 0) return false;

        /* A run persists both dirty project edits and machine-local tool
         * selection before launching. Its real failure diagnostic is routed
         * back to the simple Setup page instead of being hidden in a log. */
        const auto failure_project =
            std::filesystem::path(preference_directory_) /
            "failure-smoke.porpoise.json";
        if (!model_.NewProject() || !model_.AddTarget("failure") ||
            !model_.SetTargetPath(
                0, false,
                (std::filesystem::path(preference_directory_) /
                 "missing-input").string()) ||
            !model_.SetTargetPath(
                0, true,
                (std::filesystem::path(preference_directory_) /
                 "missing-output").string()) ||
            !model_.SetTargetEntry(0, "before-save") ||
            !model_.SaveAs(failure_project.string()) ||
            !model_.SetTargetEntry(0, "saved-before-run")) return false;
        active_target_ = 0;
        SyncTargetDraft();
        dtk_path_ = (std::filesystem::path(preference_directory_) /
                     "smoke-dtk").string();
        const auto smoke_libporpoise =
            std::filesystem::path(preference_directory_) /
            "smoke-libporpoise";
        error.clear();
        std::filesystem::create_directories(
            smoke_libporpoise / "include" / "simulator", error);
        if (error) return false;
        {
            std::ofstream meson(smoke_libporpoise / "meson.build");
            std::ofstream command_processor(
                smoke_libporpoise / "include" / "simulator" /
                "sim_gx_CommandProcessor.h");
            std::ofstream host_xfb(
                smoke_libporpoise / "include" / "simulator" /
                "sim_gx_Xfb.hpp");
            if (!meson || !command_processor || !host_xfb) return false;
            meson << "project('smoke-libporpoise', 'c', 'cpp')\n";
            command_processor
                << "#define SIM_GX_COMMAND_PROCESSOR_CANONICAL_BYTES_API_VERSION 2\n"
                << "int SIM_GX_CommandProcessor_QueueCanonicalBytes;\n";
            host_xfb
                << "struct HostXfbExecutionStats { unsigned long long "
                   "presentations; unsigned long long "
                   "gpuResidentDisplayCopies; unsigned long long "
                   "gpuResidentPresentations; unsigned "
                   "lastPresentedGuestAddress; };\n"
                << "HostXfbExecutionStats GetHostXfbExecutionStats();\n";
        }
        libporpoise_directory_ = smoke_libporpoise.string();
        RefreshLibPorpoiseCapabilities();
        if (!libporpoise_capabilities_.canonical_fifo ||
            !libporpoise_capabilities_.queued_canonical_fifo ||
            !libporpoise_capabilities_.host_xfb_observation ||
            !libporpoise_capabilities_.gpu_resident_xfb) return false;
        meson_path_ = (std::filesystem::path(preference_directory_) /
                       "smoke-meson").string();
        c_compiler_path_ = (std::filesystem::path(preference_directory_) /
                            "smoke-cc").string();
        cpp_compiler_path_ = (std::filesystem::path(preference_directory_) /
                              "smoke-cxx").string();
        dvd_root_ = (std::filesystem::path(preference_directory_) /
                     "smoke-dvd").string();
        trace_path_ = (std::filesystem::path(preference_directory_) /
                       "smoke-trace.jsonl").string();
        frame_limit_ = 300;
        if (NextAction() != GuidedAction::Analyze) return false;
        RequestRun(true);
        const auto project_machine_state =
            ProjectMachineStatePath(failure_project.string());
        if (model_.State() != WorkerState::Running || model_.Dirty() ||
            ReadTextFile(failure_project.string()).find("saved-before-run") ==
                std::string::npos ||
            ReadTextFile(project_machine_state).find("dtk=" + dtk_path_) ==
                std::string::npos ||
            ReadTextFile(project_machine_state).find(
                "libporpoise=" + libporpoise_directory_) ==
                std::string::npos ||
            ReadTextFile(project_machine_state).find("frame_limit=300") ==
                std::string::npos ||
            ReadTextFile(machine_state_path_).find("libporpoise=") !=
                std::string::npos) return false;
        model_.Wait();
        HandleWorkerCompletion();
        const auto *primary = model_.PrimaryDiagnostic();
        const std::string expected_libporpoise = libporpoise_directory_;
        ResetProjectMachineState();
        LoadProjectMachineState(failure_project.string());
        DiscoverRuntimeSettings();
        return libporpoise_directory_ == expected_libporpoise &&
               libporpoise_capabilities_.queued_canonical_fifo &&
               ReadTextFile(failure_project.string()).find(
                   expected_libporpoise) == std::string::npos &&
               frame_limit_ == 300 &&
               model_.State() == WorkerState::Failed && select_setup_tab_ &&
               primary != nullptr &&
               primary->severity == PORPOISE_SEVERITY_ERROR &&
               primary->message != nullptr && primary->message[0] != '\0';
    }

    void OpenInitial(const std::string &path) {
        /* A crash while editing a never-saved project can leave the previous
         * saved-project preference intact. Prefer newly discoverable untitled
         * recovery data when no project was explicitly requested. */
        if (path.empty() &&
            (model_.HasNewerAutosave() || HasNewerAbiAutosave())) {
            model_.NewProject();
            last_project_.clear();
            ResetAbiEditor();
            SyncTargetDraft();
            ResetProjectMachineState();
            DiscoverRuntimeSettings();
            recovery_document_.clear();
            open_recovery_popup_ = true;
            return;
        }
        const std::string selected = path.empty() ? last_project_ : path;
        if (!selected.empty() && std::filesystem::exists(selected) &&
            model_.LoadProject(selected)) {
            last_project_ = selected;
            ResetAbiEditor();
            SyncTargetDraft();
            LoadProjectMachineState(selected);
            DiscoverRuntimeSettings();
            if (model_.HasNewerAutosave() || HasNewerAbiAutosave()) {
                recovery_document_ = selected;
                open_recovery_popup_ = true;
            }
        } else {
            model_.NewProject();
            ResetAbiEditor();
            SyncTargetDraft();
            ResetProjectMachineState();
            DiscoverRuntimeSettings();
        }
    }

    void Frame() {
        if (model_.PollWorker()) HandleWorkerCompletion();
        if (open_recovery_popup_) {
            ImGui::OpenPopup("Recovery autosave");
            open_recovery_popup_ = false;
        }
        RenderRecoveryPopup();
        RenderUnsavedPopup();
        RenderOverwritePopup();
        RenderCopyFallbackPopup();
        RenderCacheCleanupPopup();
        RenderProfileResetPopup();
        RenderMenu();
        RenderToolbar();

        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + 36.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(viewport->WorkSize.x,
                   std::max(100.0f, viewport->WorkSize.y - 36.0f)),
            ImGuiCond_Always);
        ImGui::Begin("Porpoise Recovery Workbench", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoTitleBar);
        const float sidebar = std::min(320.0f,
                                       ImGui::GetContentRegionAvail().x * 0.27f);
        ImGui::BeginChild("targets", ImVec2(sidebar, 0), true);
        RenderProjectSidebar();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("workspace", ImVec2(0, 0), false);
        RenderWorkspace();
        ImGui::EndChild();
        ImGui::End();

        const auto now = std::chrono::steady_clock::now();
        if ((model_.Dirty() || abi_drafts_dirty_) &&
            now - last_autosave_ >= std::chrono::seconds(5)) {
            if (model_.Dirty()) model_.Autosave();
            if (abi_drafts_dirty_) AutosaveAbiDrafts();
            last_autosave_ = now;
        }
    }

    void RequestClose() {
        RequestFileAction(PendingFileAction::Close);
    }

private:
    void HandleWorkerCompletion() {
        last_operation_ = model_.Operation();
        bool inferred_review_ready = false;
        if (model_.State() == WorkerState::Succeeded &&
            last_operation_ == WorkerOperation::Recovery &&
            active_target_ < model_.Project().target_count &&
            !model_.Project().targets[active_target_].has_title_host) {
            inferred_initialize_dvd_ = false;
            inferred_review_ready = model_.InferTitleHostProfile(
                Text(model_.Project().targets[active_target_].id));
        }
        if (model_.State() == WorkerState::Succeeded &&
            (last_operation_ == WorkerOperation::Build ||
             last_operation_ == WorkerOperation::Run) &&
            active_target_ < model_.Project().target_count) {
            built_target_id_ = Text(
                model_.Project().targets[active_target_].id);
            cache_size_known_ = false;
        }
        if (model_.State() == WorkerState::Failed) {
            select_setup_tab_ = true;
            select_functions_tab_ = false;
            select_diagnostics_tab_ = false;
        } else if (model_.State() == WorkerState::Succeeded &&
                   last_operation_ == WorkerOperation::Recovery &&
                   last_run_analyze_only_) {
            if (inferred_review_ready) {
                select_setup_tab_ = true;
                select_functions_tab_ = false;
            } else {
                select_setup_tab_ = false;
                select_functions_tab_ = true;
            }
        } else if (model_.State() == WorkerState::Succeeded) {
            select_setup_tab_ = true;
            select_functions_tab_ = false;
        }
    }

    std::string AbiAutosavePath() const {
        const auto project_autosave = model_.AutosavePath();
        return project_autosave.empty() ? std::string()
                                        : project_autosave + ".abi-drafts";
    }

    bool HasNewerAbiAutosave() const {
        const auto path = AbiAutosavePath();
        if (path.empty()) return false;
        std::error_code error;
        const auto autosave = std::filesystem::path(path);
        if (!std::filesystem::is_regular_file(autosave, error) || error)
            return false;
        if (model_.DocumentPath().empty()) return true;
        const auto document = std::filesystem::path(model_.DocumentPath());
        const auto autosave_time =
            std::filesystem::last_write_time(autosave, error);
        if (error) return false;
        if (!std::filesystem::exists(document, error) || error) return true;
        const auto document_time =
            std::filesystem::last_write_time(document, error);
        return !error && autosave_time > document_time;
    }

    bool AutosaveAbiDrafts() {
        const auto path = AbiAutosavePath();
        return !path.empty() && model_.WriteDirectAbiDraftRecovery(
            path, abi_drafts_);
    }

    bool RecoverAbiDrafts() {
        const auto path = AbiAutosavePath();
        std::vector<DirectAbiDraft> recovered;
        if (path.empty() ||
            !model_.LoadDirectAbiDraftRecovery(path, &recovered)) return false;
        abi_drafts_ = std::move(recovered);
        active_abi_draft_ = 0;
        abi_drafts_dirty_ = true;
        return true;
    }

    void RemoveAbiAutosave() const {
        const auto path = AbiAutosavePath();
        if (path.empty()) return;
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    void ResetAbiEditor() {
        abi_drafts_.clear();
        abi_manifest_path_.clear();
        active_abi_draft_ = 0;
        selected_contract_ = 0;
        abi_drafts_dirty_ = false;
    }

    void MarkAbiDraftsDirty() { abi_drafts_dirty_ = true; }

    void SyncTargetDraft() {
        target_draft_error_.clear();
        if (model_.Project().target_count == 0) {
            active_target_ = 0;
            target_draft_ = {};
            return;
        }
        active_target_ = std::min(active_target_,
                                  model_.Project().target_count - 1);
        const auto &target = model_.Project().targets[active_target_];
        target_draft_.id = Text(target.id);
        target_draft_.input = Text(target.input.value);
        target_draft_.output = Text(target.output.value);
        target_draft_.entry = Text(target.entry);
        target_draft_.skip_list = target.has_skip_list
            ? Text(target.skip_list.value) : std::string();
        target_draft_dirty_ = false;
    }

    void RefreshLibPorpoiseCapabilities() {
        libporpoise_capability_path_ = libporpoise_directory_;
        libporpoise_capabilities_ = InspectLibPorpoiseCapabilities(
            std::filesystem::path(libporpoise_directory_));
    }

    void DiscoverRuntimeSettings() {
        auto first_executable = [](std::initializer_list<const char *> names) {
            for (const char *name : names) {
                const auto resolved = ResolveExecutable(name);
                if (!resolved.empty()) return resolved;
            }
            return std::string();
        };
        if (ResolveExecutable(meson_path_).empty())
            meson_path_ = first_executable({"meson"});
        if (ResolveExecutable(c_compiler_path_).empty()) {
            c_compiler_path_.clear();
            const char *configured = std::getenv("CC");
            if (configured != nullptr)
                c_compiler_path_ = ResolveExecutable(configured);
            if (c_compiler_path_.empty())
                c_compiler_path_ = first_executable({"clang", "gcc", "cc"});
        }
        if (ResolveExecutable(cpp_compiler_path_).empty()) {
            cpp_compiler_path_.clear();
            const char *configured = std::getenv("CXX");
            if (configured != nullptr)
                cpp_compiler_path_ = ResolveExecutable(configured);
            if (cpp_compiler_path_.empty() && !c_compiler_path_.empty()) {
                auto candidate = std::filesystem::path(c_compiler_path_);
                std::string stem = candidate.stem().string();
                if (stem.size() >= 3 &&
                    stem.substr(stem.size() - 3) == "gcc") {
                    stem.replace(stem.size() - 3, 3, "g++");
                } else if (stem.size() >= 5 &&
                           stem.substr(stem.size() - 5) == "clang") {
                    stem += "++";
                } else stem.clear();
                if (!stem.empty()) {
                    candidate = candidate.parent_path() /
                        (stem + candidate.extension().string());
                    cpp_compiler_path_ = ResolveExecutable(candidate.string());
                }
            }
            if (cpp_compiler_path_.empty())
                cpp_compiler_path_ = first_executable(
                    {"clang++", "g++", "c++"});
        }
        if (!IsLibPorpoiseCheckout(libporpoise_directory_)) {
            libporpoise_directory_.clear();
            std::vector<std::filesystem::path> candidates;
            std::error_code error;
            const auto current = std::filesystem::current_path(error);
            if (!error) {
                candidates.push_back(current / "libPorpoise");
                candidates.push_back(current.parent_path() / "libPorpoise");
            }
            if (!model_.DocumentPath().empty()) {
                const auto project_directory =
                    std::filesystem::path(model_.DocumentPath()).parent_path();
                candidates.push_back(project_directory / "libPorpoise");
                candidates.push_back(
                    project_directory.parent_path() / "libPorpoise");
            }
            for (const auto &candidate : candidates) {
                if (IsLibPorpoiseCheckout(candidate)) {
                    libporpoise_directory_ =
                        std::filesystem::absolute(candidate).lexically_normal()
                            .string();
                    break;
                }
            }
        }
        if (build_type_.empty()) build_type_ = "debugoptimized";
        RefreshLibPorpoiseCapabilities();
    }

    std::string ProjectMachineStatePath(const std::string &document) const {
        if (document.empty()) return {};
        std::error_code error;
        auto normalized = std::filesystem::absolute(document, error);
        if (error) normalized = document;
        error.clear();
        const auto canonical = std::filesystem::weakly_canonical(
            normalized, error);
        if (!error) normalized = canonical;
        std::string identity = normalized.lexically_normal().generic_string();
#if defined(_WIN32)
        std::transform(identity.begin(), identity.end(), identity.begin(),
                       [](unsigned char character) {
                           return static_cast<char>(std::tolower(character));
                       });
#endif
        std::uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
        char digest_hex[PORPOISE_SHA256_HEX_SIZE];
        porpoise_sha256(identity.data(), identity.size(), digest);
        porpoise_sha256_hex(digest, digest_hex);
        return (std::filesystem::path(preference_directory_) /
                ("project-state-" + std::string(digest_hex) + ".ini"))
            .string();
    }

    void ResetProjectMachineState() {
        dtk_path_.clear();
        runtime_directory_ = porpoise_default_runtime_directory();
        report_path_.clear();
        libporpoise_directory_.clear();
        libporpoise_capability_path_.clear();
        libporpoise_capabilities_ = {};
        meson_path_.clear();
        c_compiler_path_.clear();
        cpp_compiler_path_.clear();
        dvd_root_.clear();
        run_working_directory_.clear();
        build_type_ = "debugoptimized";
        trace_path_.clear();
        trace_enabled_ = true;
        allow_copy_fallback_ = false;
        frame_limit_ = 3;
        built_target_id_.clear();
        cache_size_known_ = false;
        cache_cleanup_error_.clear();
    }

    void LoadMachineState() {
        std::ifstream input(machine_state_path_);
        std::string line;
        while (std::getline(input, line)) {
            const auto separator = line.find('=');
            if (separator == std::string::npos) continue;
            const auto key = line.substr(0, separator);
            const auto value = line.substr(separator + 1);
            if (key == "last_project") last_project_ = value;
            else if (key == "filter") filter_ = value;
        }
    }

    void LoadProjectMachineState(const std::string &document) {
        ResetProjectMachineState();
        const auto state_path = ProjectMachineStatePath(document);
        if (state_path.empty()) return;
        std::ifstream input(state_path);
        std::string line;
        while (std::getline(input, line)) {
            const auto separator = line.find('=');
            if (separator == std::string::npos) continue;
            const auto key = line.substr(0, separator);
            const auto value = line.substr(separator + 1);
            if (key == "dtk") dtk_path_ = value;
            else if (key == "runtime") runtime_directory_ = value;
            else if (key == "report") report_path_ = value;
            else if (key == "libporpoise") libporpoise_directory_ = value;
            else if (key == "meson") meson_path_ = value;
            else if (key == "cc") c_compiler_path_ = value;
            else if (key == "cxx") cpp_compiler_path_ = value;
            else if (key == "dvd_root") dvd_root_ = value;
            else if (key == "working_directory")
                run_working_directory_ = value;
            else if (key == "build_type") build_type_ = value;
            else if (key == "trace") trace_path_ = value;
            else if (key == "trace_enabled") trace_enabled_ = value != "0";
            else if (key == "allow_copy_fallback")
                allow_copy_fallback_ = value == "1";
            else if (key == "frame_limit") {
                try {
                    frame_limit_ = std::stoull(value);
                } catch (...) {
                    frame_limit_ = 0;
                }
            }
        }
    }

    void SaveMachineState() const {
        std::error_code error;
        std::filesystem::create_directories(preference_directory_, error);
        auto safe = [](std::string value) {
            std::replace(value.begin(), value.end(), '\n', ' ');
            std::replace(value.begin(), value.end(), '\r', ' ');
            return value;
        };
        {
            std::ofstream output(machine_state_path_,
                                 std::ios::binary | std::ios::trunc);
            output << "last_project=" << safe(last_project_) << '\n'
                   << "filter=" << safe(filter_) << '\n';
        }
        const auto project_state =
            ProjectMachineStatePath(model_.DocumentPath());
        if (project_state.empty()) return;
        std::ofstream output(project_state,
                             std::ios::binary | std::ios::trunc);
        output << "dtk=" << safe(dtk_path_) << '\n'
               << "runtime=" << safe(runtime_directory_) << '\n'
               << "report=" << safe(report_path_) << '\n'
               << "libporpoise=" << safe(libporpoise_directory_) << '\n'
               << "meson=" << safe(meson_path_) << '\n'
               << "cc=" << safe(c_compiler_path_) << '\n'
               << "cxx=" << safe(cpp_compiler_path_) << '\n'
               << "dvd_root=" << safe(dvd_root_) << '\n'
               << "working_directory=" << safe(run_working_directory_) << '\n'
               << "build_type=" << safe(build_type_) << '\n'
               << "trace=" << safe(trace_path_) << '\n'
               << "trace_enabled=" << (trace_enabled_ ? "1" : "0") << '\n'
               << "allow_copy_fallback="
               << (allow_copy_fallback_ ? "1" : "0") << '\n'
               << "frame_limit=" << frame_limit_ << '\n';
    }

    void RenderRecoveryPopup() {
        if (!ImGui::BeginPopupModal("Recovery autosave", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::TextWrapped(
            "Newer recovery changes were found for this project. These may "
            "include the source and output selected during the last session. "
            "Recover them unless you intentionally want the older saved "
            "settings.");
        if (ImGui::Button("Recover newer changes (recommended)")) {
            if (model_.HasNewerAutosave())
                model_.RecoverAutosave(recovery_document_);
            if (HasNewerAbiAutosave()) RecoverAbiDrafts();
            SyncTargetDraft();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Use older saved project"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    const char *PendingFileActionDescription() const {
        switch (pending_file_action_) {
        case PendingFileAction::NewProject: return "creating a new project";
        case PendingFileAction::OpenProject: return "opening another project";
        case PendingFileAction::Close: return "closing the workbench";
        case PendingFileAction::None: break;
        }
        return "continuing";
    }

    void PerformFileAction(PendingFileAction action) {
        switch (action) {
        case PendingFileAction::NewProject:
            if (model_.NewProject()) {
                selected_rows_.clear();
                last_project_.clear();
                ResetAbiEditor();
                SyncTargetDraft();
                ResetProjectMachineState();
                DiscoverRuntimeSettings();
            }
            break;
        case PendingFileAction::OpenProject:
            OpenProjectDialog();
            break;
        case PendingFileAction::Close:
            close_ = true;
            break;
        case PendingFileAction::None:
            break;
        }
    }

    void RequestFileAction(PendingFileAction action) {
        ApplyTargetDraft();
        if (model_.Dirty() || target_draft_dirty_ || abi_drafts_dirty_) {
            pending_file_action_ = action;
            open_unsaved_popup_ = true;
            return;
        }
        PerformFileAction(action);
    }

    bool FinishPendingFileAction(bool save) {
        if (pending_file_action_ == PendingFileAction::None) return false;
        if (save && !SaveProject(false)) return false;
        const auto action = pending_file_action_;
        pending_file_action_ = PendingFileAction::None;
        open_unsaved_popup_ = false;
        PerformFileAction(action);
        return true;
    }

    bool DiscardPendingFileAction() {
        return FinishPendingFileAction(false);
    }

    void CancelPendingFileAction() {
        pending_file_action_ = PendingFileAction::None;
        open_unsaved_popup_ = false;
    }

    void RenderUnsavedPopup() {
        if (open_unsaved_popup_) {
            ImGui::OpenPopup("Unsaved project");
            open_unsaved_popup_ = false;
        }
        if (!ImGui::BeginPopupModal("Unsaved project", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::TextWrapped("Save project changes before %s?",
                           PendingFileActionDescription());
        if (ImGui::Button("Save")) {
            if (FinishPendingFileAction(true)) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")) {
            DiscardPendingFileAction();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            CancelPendingFileAction();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void RenderOverwritePopup() {
        if (open_overwrite_popup_) {
            ImGui::OpenPopup("Confirm output replacement");
            open_overwrite_popup_ = false;
        }
        if (!ImGui::BeginPopupModal("Confirm output replacement", nullptr,
                                    ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::TextWrapped(
            "One or more selected output folders already exist. Generation "
            "is transactional, but publishing will replace those outputs. "
            "Continue with force enabled?");
        if (ImGui::Button("Replace outputs")) {
            pending_run_.force = true;
            model_.Start(pending_run_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void RenderMenu() {
        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("File")) {
            const bool idle = model_.State() != WorkerState::Running &&
                              model_.State() != WorkerState::Cancelling;
            if (ImGui::MenuItem("New", nullptr, false, idle))
                RequestFileAction(PendingFileAction::NewProject);
            if (ImGui::MenuItem("Open...", nullptr, false, idle))
                RequestFileAction(PendingFileAction::OpenProject);
            if (ImGui::MenuItem("Save", "Ctrl+S",
                                false, !model_.DocumentPath().empty()))
                SaveProject(false);
            if (ImGui::MenuItem("Save As...")) SaveProject(true);
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) RequestClose();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Run")) {
            const auto state = model_.State();
            const bool idle = state != WorkerState::Running &&
                              state != WorkerState::Cancelling;
            const auto action = NextAction();
            const bool primary_enabled = idle &&
                (action == GuidedAction::ConfigureRuntime ||
                 SetupIssue().empty());
            if (ImGui::MenuItem(GuidedActionName(action), nullptr, false,
                                primary_enabled))
                RequestPrimaryAction();
            if (ImGui::MenuItem("Cancel", nullptr, false, !idle))
                model_.Cancel();
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    void RenderToolbar() {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->WorkPos,
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(ImGui::GetMainViewport()->WorkSize.x, 36.0f),
            ImGuiCond_Always);
        ImGui::Begin("toolbar", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);
        const auto state = model_.State();
        const bool busy = state == WorkerState::Running ||
                          state == WorkerState::Cancelling;
        const auto action = NextAction();
        const bool primary_enabled = !busy &&
            (action == GuidedAction::ConfigureRuntime ||
             SetupIssue().empty());
        ImGui::BeginDisabled(!primary_enabled);
        if (ImGui::Button(GuidedActionName(action))) RequestPrimaryAction();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!busy);
        if (ImGui::Button("Cancel")) model_.Cancel();
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool build_operation =
            model_.Operation() == WorkerOperation::RuntimePreflight ||
            model_.Operation() == WorkerOperation::Build ||
            model_.Operation() == WorkerOperation::Run;
        const auto progress = model_.Progress();
        const auto build_progress = model_.BuildProgress();
        const std::size_t completed = build_operation
            ? build_progress.completed : progress.completed;
        const std::size_t total = build_operation
            ? build_progress.total : progress.total;
        float fraction = total == 0
            ? (busy ? -1.0f : 0.0f)
            : static_cast<float>(completed) / static_cast<float>(total);
        const auto *primary = state == WorkerState::Failed
            ? model_.PrimaryDiagnostic() : nullptr;
        std::string overlay;
        if (state == WorkerState::Failed && primary != nullptr) {
            overlay = "Failed: " + Text(primary->message);
        } else {
            overlay = std::string(WorkerStateName(state)) + " - ";
            if (build_operation) {
                overlay += porpoise_build_phase_name(build_progress.phase);
                if (!build_progress.detail.empty())
                    overlay += " - " + build_progress.detail;
            } else {
                overlay += porpoise_operation_phase_name(progress.phase);
                if (!progress.detail.empty())
                    overlay += " - " + progress.detail;
            }
        }
        if (state == WorkerState::Failed)
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                  ImVec4(.75f, .18f, .16f, 1.0f));
        ImGui::ProgressBar(fraction, ImVec2(-1, 0), overlay.c_str());
        if (state == WorkerState::Failed) {
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && primary != nullptr)
                ImGui::SetTooltip("%s", Text(primary->message).c_str());
        }
        ImGui::End();
    }

    void OpenProjectDialog() {
        auto selected = pfd::open_file(
            "Open Porpoise project", last_project_,
            {kProjectFilters[0], kProjectFilters[1], kProjectFilters[2],
             kProjectFilters[3], kProjectFilters[4], kProjectFilters[5]},
            pfd::opt::none).result();
        if (selected.empty()) return;
        if (model_.LoadProject(selected.front())) {
            last_project_ = selected.front();
            SaveMachineState();
            selected_rows_.clear();
            ResetAbiEditor();
            SyncTargetDraft();
            LoadProjectMachineState(selected.front());
            DiscoverRuntimeSettings();
            if (model_.HasNewerAutosave() || HasNewerAbiAutosave()) {
                recovery_document_ = selected.front();
                open_recovery_popup_ = true;
            }
        }
    }

    bool SaveProject(bool force_dialog) {
        ApplyTargetDraft();
        if (target_draft_dirty_) {
            select_setup_tab_ = true;
            return false;
        }
        std::string path = model_.DocumentPath();
        if (force_dialog || path.empty()) {
            path = pfd::save_file(
                "Save Porpoise project",
                path.empty() ? "recovery.porpoise.json" : path,
                {"Porpoise projects", "*.porpoise.json",
                 "JSON files", "*.json"},
                pfd::opt::force_overwrite).result();
            if (path.empty()) return false;
            static const std::string suffix = ".porpoise.json";
            if (path.size() < suffix.size() ||
                path.substr(path.size() - suffix.size()) != suffix)
                path += ".porpoise.json";
        }
        if (abi_drafts_dirty_) {
            if (abi_manifest_path_.empty())
                abi_manifest_path_ = path + ".local-abi.json";
            if (!model_.WriteDirectAbiManifest(
                    abi_manifest_path_, abi_drafts_, true)) return false;
            abi_drafts_dirty_ = false;
            RemoveAbiAutosave();
        }
        if (force_dialog || model_.DocumentPath().empty() ||
            path != model_.DocumentPath()) {
            if (!model_.SaveAs(path)) return false;
        } else if (!model_.Save()) return false;
        last_project_ = path;
        SaveMachineState();
        SyncTargetDraft();
        return true;
    }

    void RequestRun(bool analyze_only) {
        ApplyTargetDraft();
        if (target_draft_dirty_) {
            select_setup_tab_ = true;
            return;
        }
        if (model_.DocumentPath().empty()) {
            if (!SaveProject(true)) return;
        } else if ((model_.Dirty() || abi_drafts_dirty_) &&
                   !SaveProject(false)) {
            return;
        }
        SaveMachineState();
        last_run_analyze_only_ = analyze_only;
        last_operation_ = WorkerOperation::Recovery;
        built_target_id_.clear();
        RunRequest request;
        request.analyze_only = analyze_only;
        request.report_path = report_path_;
        request.runtime_directory = runtime_directory_;
        request.dtk_path = dtk_path_;
        if (active_target_only_ &&
            active_target_ < model_.Project().target_count) {
            request.target_ids.push_back(
                Text(model_.Project().targets[active_target_].id));
        }
        if (!analyze_only && OutputsExist(request)) {
            pending_run_ = request;
            open_overwrite_popup_ = true;
        } else model_.Start(request);
    }

    void RequestBuild(bool run) {
        ApplyTargetDraft();
        if (target_draft_dirty_ || active_target_ >=
                model_.Project().target_count) {
            select_setup_tab_ = true;
            return;
        }
        if (model_.DocumentPath().empty()) {
            if (!SaveProject(true)) return;
        } else if ((model_.Dirty() || abi_drafts_dirty_) &&
                   !SaveProject(false)) {
            return;
        }
        const auto configuration_issue = RuntimeConfigurationIssue();
        if (!configuration_issue.empty()) {
            runtime_config_expanded_ = true;
            select_setup_tab_ = true;
            return;
        }
        const auto *run_target = ActiveRunTarget();
        if (run_target == nullptr || run_target->plan == nullptr) return;

        SaveMachineState();
        BuildRunRequest request;
        if (!BuildRequestForActiveTarget(run, &request)) return;
        last_operation_ = run ? WorkerOperation::Run : WorkerOperation::Build;
        if (!model_.StartBuild(request)) {
            select_setup_tab_ = true;
            select_diagnostics_tab_ = false;
        }
    }

    bool BuildRequestForActiveTarget(
        bool run, BuildRunRequest *request) const {
        if (request == nullptr || model_.DocumentPath().empty() ||
            active_target_ >= model_.Project().target_count) return false;
        const auto *run_target = ActiveRunTarget();
        if (run_target == nullptr || run_target->plan == nullptr) return false;
        const auto &target = model_.Project().targets[active_target_];
        *request = {};
        request->target_id = Text(target.id);
        request->generated_directory = Text(target.output.resolved);
        request->libporpoise_directory = libporpoise_directory_;
        request->meson_executable = ResolveExecutable(meson_path_);
        request->c_compiler = ResolveExecutable(c_compiler_path_);
        request->cpp_compiler = ResolveExecutable(cpp_compiler_path_);
        request->build_type = build_type_;
        request->generated_plan_digest = Text(
            porpoise_plan_digest(run_target->plan));
        request->dvd_root = dvd_root_;
        request->run_working_directory = !run_working_directory_.empty()
            ? run_working_directory_
            : dvd_root_.empty()
                ? std::filesystem::path(model_.DocumentPath()).parent_path()
                      .string()
                : dvd_root_;
        request->trace_file = trace_enabled_
            ? (trace_path_.empty() ? DefaultTracePath() : trace_path_)
            : std::string();
        request->frame_limit = EffectiveRunFrameLimit();
        request->allow_copy_fallback = allow_copy_fallback_;
        request->run = run;
        const auto add_runtime_directory = [&](const std::string &path) {
            if (path.empty()) return;
            const auto directory = std::filesystem::path(path).parent_path()
                .string();
            if (!directory.empty() && std::find(
                    request->runtime_search_directories.begin(),
                    request->runtime_search_directories.end(), directory) ==
                    request->runtime_search_directories.end()) {
                request->runtime_search_directories.push_back(directory);
            }
        };
        add_runtime_directory(request->c_compiler);
        add_runtime_directory(request->cpp_compiler);
        return true;
    }

    void RequestRuntimePreflight() {
        ApplyTargetDraft();
        RefreshLibPorpoiseCapabilities();
        if (target_draft_dirty_ || !RuntimeInputIssue().empty()) {
            runtime_config_expanded_ = true;
            select_setup_tab_ = true;
            return;
        }
        if (model_.DocumentPath().empty()) {
            if (!SaveProject(true)) return;
        } else if ((model_.Dirty() || abi_drafts_dirty_) &&
                   !SaveProject(false)) {
            return;
        }
        BuildRunRequest request;
        if (!BuildRequestForActiveTarget(false, &request)) return;
        SaveMachineState();
        runtime_config_expanded_ = true;
        select_setup_tab_ = true;
        last_operation_ = WorkerOperation::RuntimePreflight;
        model_.StartRuntimePreflight(request);
    }

    bool OutputsExist(const RunRequest &request) const {
        return model_.SelectedOutputsExist(request.target_ids);
    }

    void RenderProjectSidebar() {
        ImGui::TextUnformatted("Project");
        ImGui::TextWrapped("%s%s",
            model_.DocumentPath().empty()
                ? "Unsaved project" : model_.DocumentPath().c_str(),
            (model_.Dirty() || abi_drafts_dirty_) ? "  *" : "");
        ImGui::Separator();
        ImGui::TextUnformatted("Targets");
        const auto &project = model_.Project();
        for (std::size_t index = 0; index < project.target_count; ++index) {
            const auto &target = project.targets[index];
            std::string label = target.enabled ? "● " : "○ ";
            label += Text(target.id);
            if (ImGui::Selectable(label.c_str(), active_target_ == index)) {
                ApplyTargetDraft();
                active_target_ = index;
                cache_size_known_ = false;
                SyncTargetDraft();
            }
        }
        if (ImGui::Button("Add target")) {
            if (model_.AddTarget("target")) {
                active_target_ = model_.Project().target_count - 1;
                cache_size_known_ = false;
                SyncTargetDraft();
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(project.target_count == 0);
        if (ImGui::Button("Remove")) {
            model_.RemoveTarget(active_target_);
            selected_rows_.clear();
            cache_size_known_ = false;
            built_target_id_.clear();
            SyncTargetDraft();
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        if (project.target_count > 1) {
            ImGui::Checkbox("Run selected target only", &active_target_only_);
            ImGui::TextWrapped(
                "Turn this off to run every enabled target as one "
                "transaction.");
        } else {
            active_target_only_ = true;
            ImGui::TextDisabled("Running the selected target.");
        }
        ImGui::Separator();
        ImGui::Text("Plan rows: %zu", BuildRows().size());
        ImGui::Text("Selected: %zu", selected_rows_.size());
        const bool busy = model_.State() == WorkerState::Running ||
                          model_.State() == WorkerState::Cancelling;
        if (busy) ImGui::TextDisabled("Diagnostics update when the run stops");
        else ImGui::Text("Diagnostics: %zu", model_.Diagnostics().count);
    }

    std::filesystem::path ResolveSetupPath(const std::string &value) const {
        if (value.empty()) return {};
        std::filesystem::path path(value);
        if (path.is_absolute()) return path.lexically_normal();
        if (!model_.DocumentPath().empty()) {
            return (std::filesystem::path(model_.DocumentPath()).parent_path() /
                    path).lexically_normal();
        }
        if (model_.Project().directory != nullptr) {
            return (std::filesystem::path(model_.Project().directory) /
                    path).lexically_normal();
        }
        std::error_code error;
        return (std::filesystem::current_path(error) / path).lexically_normal();
    }

    std::string SetupIssue() const {
        if (!target_draft_error_.empty()) return target_draft_error_;
        if (active_target_ >= model_.Project().target_count)
            return "Create a target to begin.";
        if (target_draft_.input.empty()) return "Choose a source input.";
        const auto input = ResolveSetupPath(target_draft_.input);
        std::error_code error;
        if (!std::filesystem::exists(input, error) || error)
            return "The selected source input does not exist.";
        const auto &target = model_.Project().targets[active_target_];
        if (target.source_kind == PORPOISE_RECOVERY_SOURCE_MANAGED_ELF) {
            if (dtk_path_.empty()) return "Choose a DTK executable.";
            error.clear();
            if (!std::filesystem::is_regular_file(dtk_path_, error) || error)
                return "The selected DTK executable does not exist.";
        }
        if (target_draft_.output.empty() || target_draft_.output == ".")
            return "Choose a dedicated output folder, not the project folder.";
        if (!model_.DocumentPath().empty()) {
            const auto project_directory =
                std::filesystem::path(model_.DocumentPath()).parent_path()
                    .lexically_normal();
            if (ResolveSetupPath(target_draft_.output) == project_directory)
                return "The output folder cannot be the project folder.";
        }
        return {};
    }

    const PorpoiseRecoveryRunTarget *ActiveRunTarget() const {
        const auto *result = model_.RunResult();
        if (result == nullptr ||
            active_target_ >= model_.Project().target_count) return nullptr;
        const std::string id = Text(model_.Project().targets[active_target_].id);
        for (std::size_t index = 0; index < result->target_count; ++index) {
            const auto &candidate = result->targets[index];
            if (candidate.target != nullptr &&
                Text(candidate.target->id) == id) return &candidate;
        }
        return nullptr;
    }

    bool ActiveTargetAnalyzed() const {
        const auto *target = ActiveRunTarget();
        return target != nullptr && target->plan != nullptr;
    }

    bool ActiveTargetGenerated() const {
        const auto *target = ActiveRunTarget();
        return target != nullptr && target->plan != nullptr &&
               target->generated && target->published;
    }

    std::string RuntimeInputIssue() const {
        if (active_target_ >= model_.Project().target_count)
            return "Create a target first.";
        const auto &target = model_.Project().targets[active_target_];
        if (!target.has_title_host)
            return "Review and save a title-host profile for this target.";
        if (libporpoise_directory_.empty())
            return "Select the libPorpoise checkout.";
        if (!IsLibPorpoiseCheckout(libporpoise_directory_))
            return "The libPorpoise folder must contain meson.build.";
        if (libporpoise_capability_path_ != libporpoise_directory_ ||
            !libporpoise_capabilities_.canonical_fifo)
            return "Select a libPorpoise checkout with canonical GX FIFO byte ingress API v1 or newer.";
        if (ResolveExecutable(meson_path_).empty())
            return "Select a Meson 1.2+ executable.";
        if (ResolveExecutable(c_compiler_path_).empty())
            return "Select the x64 C compiler.";
        if (ResolveExecutable(cpp_compiler_path_).empty())
            return "Select the matching x64 C++ compiler.";
        if (target.title_host.initialize_dvd) {
            std::error_code error;
            if (dvd_root_.empty() ||
                !std::filesystem::is_directory(dvd_root_, error) || error)
                return "Select the extracted DVD root used by this target.";
        }
        if (!run_working_directory_.empty()) {
            std::error_code error;
            if (!std::filesystem::is_directory(
                    run_working_directory_, error) || error)
                return "Select an existing Run working directory or leave it blank.";
        }
        return {};
    }

    std::string RuntimeConfigurationIssue() const {
        const auto input_issue = RuntimeInputIssue();
        if (!input_issue.empty()) return input_issue;
        BuildRunRequest request;
        if (!BuildRequestForActiveTarget(false, &request))
            return "Generate this target before validating its runtime.";
        if (!model_.RuntimePreflightRequestMatches(request))
            return "Validate Meson and the selected x64 C/C++ toolchain.";
        if (model_.Operation() == WorkerOperation::RuntimePreflight &&
            (model_.State() == WorkerState::Running ||
             model_.State() == WorkerState::Cancelling))
            return "Runtime validation is still running.";
        if (model_.RuntimePreflightResult() == nullptr) {
            const auto *diagnostic = model_.PrimaryDiagnostic();
            if (diagnostic != nullptr && diagnostic->message != nullptr &&
                diagnostic->message[0] != '\0') return diagnostic->message;
            return model_.State() == WorkerState::Cancelled
                ? "Runtime validation was cancelled."
                : "Runtime validation failed; review the selected tools.";
        }
        return {};
    }

    bool ActiveTargetBuilt() const {
        return model_.BuildResult() != nullptr &&
               active_target_ < model_.Project().target_count &&
               built_target_id_ == Text(model_.Project().targets[active_target_].id);
    }

    GuidedAction NextAction() const {
        if (!SetupIssue().empty() || !ActiveTargetAnalyzed())
            return GuidedAction::Analyze;
        if (!ActiveTargetGenerated()) return GuidedAction::Generate;
        if (!RuntimeConfigurationIssue().empty())
            return GuidedAction::ConfigureRuntime;
        if (!ActiveTargetBuilt()) return GuidedAction::Build;
        return GuidedAction::Run;
    }

    std::filesystem::path ActiveBuildCacheRoot() const {
        char target_cache_key[PORPOISE_RECOVERY_TARGET_CACHE_KEY_SIZE] = {};
        if (model_.DocumentPath().empty() ||
            active_target_ >= model_.Project().target_count) return {};
        if (!porpoise_recovery_target_cache_key(
                model_.Project().targets[active_target_].id,
                target_cache_key)) return {};
        return std::filesystem::path(model_.DocumentPath()).parent_path() /
            ".porpoise-build" /
            target_cache_key;
    }

    std::string DefaultTracePath() const {
        const auto root = ActiveBuildCacheRoot();
        return root.empty() ? std::string()
                            : (root / "boot-trace.jsonl").string();
    }

    std::size_t EffectiveRunFrameLimit() const {
        if (!trace_enabled_) return 0U;
        return static_cast<std::size_t>(std::min<std::uint64_t>(
            frame_limit_, std::numeric_limits<std::size_t>::max()));
    }

    void RuntimeSettingsChanged(bool invalidates_build = true) {
        SaveMachineState();
        if (invalidates_build) {
            model_.ClearBuildResult();
            model_.ClearRuntimePreflight();
            built_target_id_.clear();
        }
    }

    void RequestPrimaryAction() {
        switch (NextAction()) {
        case GuidedAction::Analyze: RequestRun(true); break;
        case GuidedAction::Generate: RequestRun(false); break;
        case GuidedAction::ConfigureRuntime:
            runtime_config_expanded_ = true;
            select_setup_tab_ = true;
            if (RuntimeInputIssue().empty()) RequestRuntimePreflight();
            break;
        case GuidedAction::Build: RequestBuild(false); break;
        case GuidedAction::Run: RequestBuild(true); break;
        }
    }

    void RenderRunStatusBanner() {
        if (model_.State() != WorkerState::Failed) return;
        const auto *primary = model_.PrimaryDiagnostic();
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
                              ImVec4(.24f, .055f, .045f, 1.0f));
        ImGui::BeginChild("run-failure", ImVec2(0, 145), true,
                          ImGuiWindowFlags_NoScrollbar);
        const char *operation =
            last_operation_ == WorkerOperation::RuntimePreflight
            ? "Runtime validation"
            : last_operation_ == WorkerOperation::Build
            ? "Build"
            : last_operation_ == WorkerOperation::Run
                ? "Run"
                : last_run_analyze_only_ ? "Analysis" : "Generation";
        ImGui::TextColored(ImVec4(1.0f, .42f, .35f, 1.0f),
                           "%s failed", operation);
        ImGui::TextWrapped("%s", primary == nullptr
            ? "The operation failed without a diagnostic."
            : Text(primary->message).c_str());
        const auto hint = DiagnosticHint(primary);
        if (!hint.empty()) {
            ImGui::TextColored(ImVec4(1.0f, .78f, .42f, 1.0f),
                               "Next: %s", hint.c_str());
        }
        if (ImGui::Button("Back to Setup")) select_setup_tab_ = true;
        ImGui::SameLine();
        if (ImGui::Button("Show diagnostics"))
            select_diagnostics_tab_ = true;
        ImGui::SameLine();
        if (last_operation_ == WorkerOperation::RuntimePreflight ||
            last_operation_ == WorkerOperation::Build ||
            last_operation_ == WorkerOperation::Run) {
            if (ImGui::Button("Fix runtime settings")) {
                runtime_config_expanded_ = true;
                select_setup_tab_ = true;
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Retry")) {
            if (last_operation_ == WorkerOperation::RuntimePreflight)
                RequestRuntimePreflight();
            else if (last_operation_ == WorkerOperation::Build)
                RequestBuild(false);
            else if (last_operation_ == WorkerOperation::Run)
                RequestBuild(true);
            else RequestRun(last_run_analyze_only_);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void RenderTitleHostReview() {
        if (active_target_ >= model_.Project().target_count ||
            !ActiveTargetAnalyzed()) return;
        const auto &target = model_.Project().targets[active_target_];
        ImGui::SeparatorText("Title host");
        if (!target.has_title_host) {
            const std::string target_id = Text(target.id);
            const auto *inferred =
                model_.InferredTitleHostProfile(target_id);
            if (inferred == nullptr) {
                ImGui::TextColored(
                    ImVec4(1.0f, .68f, .25f, 1.0f),
                    "A reviewed title-host profile is required before Build.");
                if (!model_.TitleHostInferenceIssue().empty()) {
                    ImGui::TextWrapped("Automatic inference stopped: %s",
                        model_.TitleHostInferenceIssue().c_str());
                    ImGui::TextDisabled(
                        "Add the missing map evidence, then Analyze again.");
                } else {
                    ImGui::TextWrapped(
                        "Analyze with CodeWarrior map evidence to infer the "
                        "entry, registers, arena, and startup locators.");
                }
                if (ImGui::Button("Try inference again")) {
                    inferred_initialize_dvd_ = false;
                    model_.InferTitleHostProfile(target_id);
                }
                return;
            }

            ImGui::TextColored(ImVec4(.5f, .78f, 1.0f, 1.0f),
                               "Review inferred CodeWarrior bootstrap");
            const auto *run_target = ActiveRunTarget();
            const auto *entry = run_target == nullptr ||
                                      run_target->plan == nullptr
                ? nullptr : porpoise_plan_entry(run_target->plan);
            ImGui::Text("Entry: %s (Lift)",
                        entry == nullptr || entry->function == nullptr
                            ? "resolved direct entry"
                            : Text(entry->function->name).c_str());
            ImGui::TextUnformatted(
                "Stack/SDA registers and arena bounds are resolved.");
            ImGui::Text("Startup functions: %zu  |  Initial words: %zu",
                        inferred->startup_function_count,
                        inferred->initial_word_count);
            ImGui::Checkbox("Initialize DVD through libPorpoise",
                            &inferred_initialize_dvd_);
            ImGui::TextDisabled(
                "Verify the values and DVD policy; inference never saves "
                "or overwrites a profile by itself.");
            if (ImGui::Button("Accept and save profile")) {
                if (model_.AcceptInferredTitleHostProfile(
                        target_id, inferred_initialize_dvd_)) {
                    SaveProject(false);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard suggestion"))
                model_.DiscardInferredTitleHostProfile();
            return;
        }
        const auto &profile = target.title_host;
        const auto *run_target = ActiveRunTarget();
        const auto *entry = run_target == nullptr || run_target->plan == nullptr
            ? nullptr : porpoise_plan_entry(run_target->plan);
        ImGui::Text("Reviewed entry: %s (Lift)",
                    entry == nullptr || entry->function == nullptr
                        ? "resolved direct entry"
                        : Text(entry->function->name).c_str());
        ImGui::TextUnformatted(
            "Stack/SDA registers and arena bounds are configured.");
        ImGui::Text("Startup functions: %zu  |  Initial words: %zu  |  DVD: %s",
                    profile.startup_function_count,
                    profile.initial_word_count,
                    profile.initialize_dvd ? "native initialization" : "off");
        ImGui::TextDisabled(
            "Portable profile saved in the project; machine paths are below.");
        if (ImGui::Button("Re-infer profile...")) {
            pending_profile_reset_target_ = Text(target.id);
            replacement_initialize_dvd_ = profile.initialize_dvd;
            open_profile_reset_popup_ = true;
        }
    }

    void RenderInlineSettingIssue(const std::string &message) {
        if (message.empty()) return;
        ImGui::TextColored(ImVec4(1.0f, .52f, .3f, 1.0f),
                           "Fix: %s", message.c_str());
    }

    void RenderRuntimeConfiguration() {
        const auto issue = RuntimeConfigurationIssue();
        const auto input_issue = RuntimeInputIssue();
        BuildRunRequest preflight_request;
        const bool have_preflight_request =
            BuildRequestForActiveTarget(false, &preflight_request);
        const bool preflight_matches = have_preflight_request &&
            model_.RuntimePreflightRequestMatches(preflight_request);
        const auto *preflight = preflight_matches
            ? model_.RuntimePreflightResult() : nullptr;
        const bool should_open = runtime_config_expanded_ ||
            NextAction() == GuidedAction::ConfigureRuntime;
        if (should_open)
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        runtime_config_expanded_ = false;
        if (!ImGui::CollapsingHeader("Runtime configuration")) {
            if (issue.empty())
                ImGui::TextColored(ImVec4(.35f, .9f, .5f, 1.0f),
                                   "Runtime configuration is ready.");
            else ImGui::TextDisabled("Open to finish first-use setup.");
            return;
        }

        ImGui::TextWrapped(
            "These paths are stored only on this computer. Porpoise validates "
            "Meson and the mixed C/C++ x64 toolchain before configuring the "
            "title build.");
        const bool busy = model_.State() == WorkerState::Running ||
                          model_.State() == WorkerState::Cancelling;
        ImGui::BeginDisabled(busy);
        if (ImGui::Button("Auto-detect tools")) {
            DiscoverRuntimeSettings();
            RuntimeSettingsChanged();
        }

        bool changed = InputFolderPath(
            "libPorpoise renderer checkout", libporpoise_directory_);
        if (changed) {
            RefreshLibPorpoiseCapabilities();
            RuntimeSettingsChanged();
        }
        if (libporpoise_directory_.empty())
            RenderInlineSettingIssue("select the libPorpoise source folder");
        else if (!IsLibPorpoiseCheckout(libporpoise_directory_))
            RenderInlineSettingIssue("this folder does not contain meson.build");
        else {
            if (!libporpoise_capabilities_.canonical_fifo) {
                RenderInlineSettingIssue(
                    "this checkout lacks canonical GX FIFO byte ingress v1+");
            } else {
                ImGui::TextColored(
                    libporpoise_capabilities_.gpu_resident_xfb
                        ? ImVec4(.35f, .9f, .5f, 1.0f)
                        : ImVec4(1.0f, .76f, .32f, 1.0f),
                    "Renderer: %s",
                    libporpoise_capabilities_.gpu_resident_xfb
                        ? "GPU-resident host-XFB capable"
                        : libporpoise_capabilities_.host_xfb_observation
                            ? "observable host-XFB (GPU residency not advertised)"
                            : "host-XFB capability not advertised");
                if (libporpoise_capabilities_.queued_canonical_fifo) {
                    ImGui::TextColored(
                        ImVec4(.35f, .9f, .5f, 1.0f),
                        "GX FIFO: queued canonical bytes v%u",
                        libporpoise_capabilities_.canonical_fifo_version);
                } else {
                    ImGui::TextColored(
                        ImVec4(1.0f, .76f, .32f, 1.0f),
                        "GX FIFO: synchronous canonical bytes v%u",
                        libporpoise_capabilities_.canonical_fifo_version);
                    ImGui::TextWrapped(
                        "Compatible, but lifted code with frequent WGPIPE "
                        "stores can run slowly. A checkout exposing queued "
                        "canonical FIFO v2 is preferred.");
                }
                if (!libporpoise_capabilities_.host_xfb_observation) {
                    ImGui::TextWrapped(
                        "This checkout cannot report successful host-XFB "
                        "presentations; a reached VIWaitForRetrace contract "
                        "will fail closed.");
                }
            }
            std::error_code carrier_error;
            const auto carrier_header =
                std::filesystem::path(libporpoise_directory_) / "include" /
                "porpoise" / "host_thread_carrier.h";
            if (!std::filesystem::is_regular_file(
                    carrier_header, carrier_error) || carrier_error) {
                ImGui::TextColored(
                    ImVec4(1.0f, .76f, .32f, 1.0f),
                    "single-thread compatibility only");
                ImGui::TextDisabled(
                    "Build/Run remain available; a reached carrier-dependent "
                    "sleep, wake, suspend, resume, or exit faults precisely.");
            }
        }
        ImGui::TextDisabled(
            "This checkout/worktree path is remembered only for this project "
            "on this computer.");

        changed = InputOptionalPath("Meson executable", meson_path_, false,
#if defined(_WIN32)
                                    "*.exe"
#else
                                    "*"
#endif
        );
        if (ResolveExecutable(meson_path_).empty())
            RenderInlineSettingIssue("select Meson 1.2 or newer");
        if (changed) RuntimeSettingsChanged();

        changed = InputOptionalPath("C compiler", c_compiler_path_, false,
#if defined(_WIN32)
                                    "*.exe"
#else
                                    "*"
#endif
        );
        if (ResolveExecutable(c_compiler_path_).empty())
            RenderInlineSettingIssue("select the x64 C compiler");
        if (changed) RuntimeSettingsChanged();

        changed = InputOptionalPath("C++ compiler", cpp_compiler_path_, false,
#if defined(_WIN32)
                                    "*.exe"
#else
                                    "*"
#endif
        );
        if (ResolveExecutable(cpp_compiler_path_).empty())
            RenderInlineSettingIssue(
                "select the matching compiler family, target, and version");
        if (changed) RuntimeSettingsChanged();

        ImGui::BeginDisabled(!input_issue.empty());
        if (ImGui::Button(
                preflight == nullptr ? "Validate runtime now"
                                     : "Revalidate runtime")) {
            RequestRuntimePreflight();
        }
        ImGui::EndDisabled();
        if (model_.Operation() == WorkerOperation::RuntimePreflight &&
            (model_.State() == WorkerState::Running ||
             model_.State() == WorkerState::Cancelling)) {
            const auto progress = model_.BuildProgress();
            ImGui::TextColored(
                ImVec4(.5f, .78f, 1.0f, 1.0f), "%s",
                progress.detail.empty() ? "Validating runtime tools..."
                                        : progress.detail.c_str());
        } else if (preflight != nullptr) {
            ImGui::TextColored(
                ImVec4(.35f, .9f, .5f, 1.0f),
                "Validated: Meson %s | %s %s | %s",
                preflight->meson_version, preflight->compiler_family,
                preflight->compiler_version, preflight->compiler_target);
        } else if (preflight_matches && model_.PrimaryDiagnostic() != nullptr) {
            ImGui::TextColored(
                ImVec4(1.0f, .52f, .3f, 1.0f), "Runtime validation: %s",
                Text(model_.PrimaryDiagnostic()->message).c_str());
        } else if (input_issue.empty()) {
            ImGui::TextColored(
                ImVec4(1.0f, .68f, .25f, 1.0f),
                "Validate these tools before Build becomes available.");
        }

        if (active_target_ < model_.Project().target_count &&
            model_.Project().targets[active_target_].has_title_host &&
            model_.Project().targets[active_target_]
                .title_host.initialize_dvd) {
            changed = InputFolderPath("DVD root", dvd_root_);
            std::error_code error;
            if (dvd_root_.empty() ||
                !std::filesystem::is_directory(dvd_root_, error) || error)
                RenderInlineSettingIssue("select the extracted game DVD root");
            if (changed) RuntimeSettingsChanged(false);
        }

        changed = InputFolderPath(
            "Run working directory (optional)", run_working_directory_);
        if (!run_working_directory_.empty()) {
            std::error_code error;
            if (!std::filesystem::is_directory(
                    run_working_directory_, error) || error)
                RenderInlineSettingIssue(
                    "select an existing folder or clear this field");
        }
        if (changed) RuntimeSettingsChanged(false);

        const char *build_types[] = {"debugoptimized", "release", "debug"};
        int build_type_index = build_type_ == "release" ? 1
            : build_type_ == "debug" ? 2 : 0;
        if (ImGui::Combo("Build type", &build_type_index, build_types, 3)) {
            build_type_ = build_types[build_type_index];
            RuntimeSettingsChanged();
        }

        bool trace_changed = ImGui::Checkbox(
            "Write a boot trace on Run", &trace_enabled_);
        if (trace_enabled_) {
            trace_changed |= InputTextString(
                "Trace file (blank = build cache)", trace_path_);
            trace_changed |= ImGui::InputScalar(
                "Boot trace frame limit (0 = unlimited)", ImGuiDataType_U64,
                &frame_limit_);
            if (frame_limit_ == 0U) {
                ImGui::TextColored(
                    ImVec4(1.0f, .68f, .25f, 1.0f),
                    "Unlimited JSONL tracing can consume substantial disk "
                    "space; disable tracing for normal play.");
            }
        }
        if (trace_changed) RuntimeSettingsChanged(false);

        bool copy_requested = allow_copy_fallback_;
        if (ImGui::Checkbox(
                "Allow confirmed dependency-copy fallback", &copy_requested)) {
            if (copy_requested && !allow_copy_fallback_) {
                open_copy_fallback_popup_ = true;
            } else {
                allow_copy_fallback_ = false;
                RuntimeSettingsChanged();
            }
        }
        ImGui::TextDisabled(
            "Normally generated output and dependencies are linked into the "
            "cache. Copying is used only after confirmation and a space check.");

        if (!issue.empty())
            ImGui::TextColored(ImVec4(1.0f, .68f, .25f, 1.0f),
                               "Next fix: %s", issue.c_str());
        else ImGui::TextColored(ImVec4(.35f, .9f, .5f, 1.0f),
                                "Runtime preflight passed; Build is ready.");
        ImGui::EndDisabled();
    }

    void RenderBuildCacheControls() {
        if (model_.DocumentPath().empty() ||
            active_target_ >= model_.Project().target_count) return;
        ImGui::SeparatorText("Build cache");
        const auto root = ActiveBuildCacheRoot();
        ImGui::TextWrapped("%s", root.string().c_str());
        ImGui::Text("Size: %s", cache_size_known_
            ? HumanSize(cache_size_).c_str() : "not measured");
        const bool busy = model_.State() == WorkerState::Running ||
                          model_.State() == WorkerState::Cancelling;
        ImGui::BeginDisabled(busy);
        if (ImGui::Button("Measure size")) {
            cache_size_ = DirectoryAllocatedSize(root);
            cache_size_known_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reveal folder")) {
            std::string url = "file:///" + root.lexically_normal()
                .generic_string();
            std::string escaped;
            for (char character : url) {
                if (character == ' ') escaped += "%20";
                else escaped += character;
            }
            SDL_OpenURL(escaped.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Clean cache..."))
            open_cache_cleanup_popup_ = true;
        ImGui::EndDisabled();

        if (const auto *build = model_.BuildResult(); build != nullptr) {
            ImGui::Text("Last build: %s%s", build->cache_reused
                ? "reused cache" : "compiled", build->executable_available
                ? " | executable ready" : "");
        }
        if (!cache_cleanup_error_.empty())
            ImGui::TextColored(ImVec4(1.0f, .52f, .3f, 1.0f),
                               "Cleanup failed: %s",
                               cache_cleanup_error_.c_str());
    }

    void RenderCopyFallbackPopup() {
        if (open_copy_fallback_popup_) {
            ImGui::OpenPopup("Allow dependency copies");
            open_copy_fallback_popup_ = false;
        }
        if (!ImGui::BeginPopupModal(
                "Allow dependency copies", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::TextWrapped(
            "If junctions or symlinks are unavailable, Porpoise may copy the "
            "generated project and libPorpoise into the build cache after "
            "checking free space. This can use substantial disk space.");
        if (ImGui::Button("Allow for this computer")) {
            allow_copy_fallback_ = true;
            RuntimeSettingsChanged();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep links only")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void RenderCacheCleanupPopup() {
        if (open_cache_cleanup_popup_) {
            ImGui::OpenPopup("Clean target build cache");
            open_cache_cleanup_popup_ = false;
        }
        if (!ImGui::BeginPopupModal(
                "Clean target build cache", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) return;
        const auto root = ActiveBuildCacheRoot();
        ImGui::TextWrapped(
            "Remove this target's generated build cache?\n%s\n\nThe recovery "
            "project, source input, generated output, and libPorpoise checkout "
            "are not removed.", root.string().c_str());
        if (ImGui::Button("Remove cache")) {
            if (RemoveTargetBuildCache(
                    model_.DocumentPath(),
                    Text(model_.Project().targets[active_target_].id),
                    &cache_cleanup_error_)) {
                model_.ClearBuildResult();
                built_target_id_.clear();
                cache_size_ = 0;
                cache_size_known_ = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void RenderProfileResetPopup() {
        if (open_profile_reset_popup_) {
            model_.InferTitleHostProfile(pending_profile_reset_target_);
            ImGui::OpenPopup("Review replacement title host");
            open_profile_reset_popup_ = false;
        }
        if (!ImGui::BeginPopupModal(
                "Review replacement title host", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) return;
        const auto *inferred = model_.InferredTitleHostProfile(
            pending_profile_reset_target_);
        if (inferred == nullptr) {
            ImGui::TextWrapped(
                "The saved profile was left unchanged. Inference could not "
                "produce a replacement: %s",
                model_.TitleHostInferenceIssue().empty()
                    ? "the required map evidence is unavailable"
                    : model_.TitleHostInferenceIssue().c_str());
            if (ImGui::Button("Keep saved profile")) {
                pending_profile_reset_target_.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return;
        }

        ImGui::TextWrapped(
            "Compare this newly inferred profile with the saved profile. "
            "Nothing is replaced until you explicitly accept and save it.");
        ImGui::TextUnformatted(
            "Direct entry, stack/SDA registers, and arena bounds resolved.");
        ImGui::Text("Startup functions: %zu  |  Initial words: %zu",
                    inferred->startup_function_count,
                    inferred->initial_word_count);
        ImGui::Checkbox("Initialize DVD through libPorpoise",
                        &replacement_initialize_dvd_);
        if (ImGui::Button("Replace and save profile")) {
            if (model_.AcceptInferredTitleHostProfile(
                    pending_profile_reset_target_,
                    replacement_initialize_dvd_)) {
                SaveProject(false);
                pending_profile_reset_target_.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep saved profile")) {
            model_.DiscardInferredTitleHostProfile();
            pending_profile_reset_target_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void RenderSetup() {
        const bool busy = model_.State() == WorkerState::Running ||
                          model_.State() == WorkerState::Cancelling;
        ImGui::TextWrapped(
            "Follow the single highlighted action below. Porpoise saves the "
            "project before each step and never rewrites the source.");
        if (active_target_ >= model_.Project().target_count) {
            if (ImGui::Button("Create recovery target")) {
                if (model_.AddTarget("target")) {
                    active_target_ = model_.Project().target_count - 1;
                    SyncTargetDraft();
                }
            }
            return;
        }

        auto &target = model_.Project().targets[active_target_];
        ImGui::BeginDisabled(busy);
        ImGui::SeparatorText("1. Choose the source");
        int source_kind = static_cast<int>(target.source_kind);
        const char *source_kinds[] = {
            "Assembly file or folder", "Executable ELF (managed DTK)",
            "Prepared DTK assembly"
        };
        if (ImGui::Combo("Source type", &source_kind, source_kinds, 3)) {
            target.source_kind =
                static_cast<PorpoiseRecoverySourceKind>(source_kind);
            model_.MarkDirty();
        }
        target_draft_dirty_ |= InputOptionalPath(
            target.source_kind == PORPOISE_RECOVERY_SOURCE_MANAGED_ELF
                ? "Game ELF" : "Assembly input",
            target_draft_.input,
            target.source_kind != PORPOISE_RECOVERY_SOURCE_MANAGED_ELF,
            target.source_kind == PORPOISE_RECOVERY_SOURCE_MANAGED_ELF
                ? "*.elf" : "*.s;*.asm");
        {
            std::error_code error;
            if (target_draft_.input.empty())
                RenderInlineSettingIssue("choose the source input");
            else if (!std::filesystem::exists(
                         ResolveSetupPath(target_draft_.input), error) || error)
                RenderInlineSettingIssue("the selected source does not exist");
        }

        if (target.source_kind == PORPOISE_RECOVERY_SOURCE_MANAGED_ELF) {
            ImGui::SeparatorText("2. Choose DTK");
            if (InputOptionalPath("DTK executable", dtk_path_, false,
#ifdef _WIN32
                                  "*.exe"
#else
                                  "*"
#endif
            )) SaveMachineState();
            std::error_code error;
            if (dtk_path_.empty())
                RenderInlineSettingIssue("choose the DTK executable");
            else if (!std::filesystem::is_regular_file(dtk_path_, error) ||
                     error)
                RenderInlineSettingIssue("the selected DTK does not exist");
            ImGui::TextDisabled(
                "This computer-specific tool path is remembered immediately.");
        }

        ImGui::SeparatorText(
            target.source_kind == PORPOISE_RECOVERY_SOURCE_MANAGED_ELF
                ? "3. Choose the output" : "2. Choose the output");
        target_draft_dirty_ |=
            InputFolderPath("Generated project folder", target_draft_.output);
        if (target_draft_.output.empty() || target_draft_.output == ".")
            RenderInlineSettingIssue("choose a dedicated output folder");
        ImGui::TextDisabled(
            "Use a new folder separate from the source and project file.");

        ImGui::SeparatorText("Recovery options");
        if (ImGui::Checkbox("Strict lowerings", &target.strict))
            model_.MarkDirty();
        ImGui::SameLine();
        ImGui::TextDisabled(
            target.strict ? "approximate instructions block generation"
                          : "approximate instructions are reported as warnings");
        ImGui::Text("SDK handling: %s",
                    porpoise_sdk_policy_name(target.sdk_policy));
        ImGui::TextDisabled(
            "The safe default is keep. Maps, catalogs, policies, and entry "
            "selection are under Advanced.");
        ImGui::EndDisabled();

        const auto issue = SetupIssue();
        if (!issue.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, .68f, .25f, 1.0f),
                               "Before Analyze: %s", issue.c_str());
        }

        if (ActiveTargetAnalyzed()) {
            ImGui::TextColored(ImVec4(.35f, .9f, .5f, 1.0f),
                               "Analysis ready: %zu functions.",
                               BuildRows().size());
        }
        RenderTitleHostReview();
        RenderRuntimeConfiguration();
        RenderBuildCacheControls();

        const auto action = NextAction();
        const bool primary_enabled = !busy &&
            (action == GuidedAction::ConfigureRuntime || issue.empty());
        ImGui::Spacing();
        ImGui::SeparatorText("Next step");
        ImGui::BeginDisabled(!primary_enabled);
        if (ImGui::Button(GuidedActionName(action), ImVec2(210, 42)))
            RequestPrimaryAction();
        ImGui::EndDisabled();
        ImGui::SameLine();
        switch (action) {
        case GuidedAction::Analyze:
            ImGui::TextDisabled("inspect input and build a reviewable plan");
            break;
        case GuidedAction::Generate:
            ImGui::TextDisabled("publish the reviewed generated project");
            break;
        case GuidedAction::ConfigureRuntime:
            ImGui::TextDisabled("finish the highlighted machine settings");
            break;
        case GuidedAction::Build:
            ImGui::TextDisabled("preflight, configure, compile, and stage DLLs");
            break;
        case GuidedAction::Run:
            ImGui::TextDisabled("launch the staged title host");
            break;
        }
    }

    void RenderWorkspace() {
        RenderRunStatusBanner();
        if (ImGui::BeginTabBar("workbench-tabs")) {
            const ImGuiTabItemFlags setup_flags = select_setup_tab_
                ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem("Setup", nullptr, setup_flags)) {
                select_setup_tab_ = false;
                RenderSetup();
                ImGui::EndTabItem();
            }
            const ImGuiTabItemFlags function_flags = select_functions_tab_
                ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(
                    "Functions", nullptr, function_flags)) {
                select_functions_tab_ = false;
                RenderFunctionTable();
                ImGui::EndTabItem();
            }
            const ImGuiTabItemFlags diagnostics_flags =
                select_diagnostics_tab_ ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(
                    "Diagnostics", nullptr, diagnostics_flags)) {
                select_diagnostics_tab_ = false;
                RenderDiagnostics();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Advanced")) {
                if (ImGui::BeginTabBar("advanced-tabs")) {
                    if (ImGui::BeginTabItem("Project & Maps")) {
                        RenderTargetSettings();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Data")) {
                        RenderDataEditor();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("ABI Contracts")) {
                        RenderAbiEditor();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Title Host")) {
                        RenderTitleHostAdvanced();
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Report")) {
                        RenderReport();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    std::vector<RowRecord> BuildRows() const {
        std::vector<RowRecord> rows;
        const auto *result = model_.RunResult();
        if (result == nullptr) return rows;
        for (std::size_t target_index = 0;
             target_index < result->target_count; ++target_index) {
            const auto &target = result->targets[target_index];
            if (target.plan == nullptr) continue;
            for (std::size_t function_index = 0;
                 function_index < porpoise_plan_function_count(target.plan);
                 ++function_index) {
                const auto *view = porpoise_plan_function_at(
                    target.plan, function_index);
                RowRecord row;
                row.run_target_index = target_index;
                row.function_index = function_index;
                row.run_target = &target;
                row.view = view;
                row.locator = model_.MakeLocator(target, *view);
                row.key = RowKey(row.locator);
                row.source_name = Text(view->function->name);
                if (view->canonical_sdk_identity != nullptr)
                    row.canonical_name = view->canonical_sdk_identity;
                else if (view->map_symbol != nullptr)
                    row.canonical_name = Text(view->map_symbol->name);
                else row.canonical_name = row.source_name;
                row.unit = view->source == nullptr
                    ? "" : Text(view->source->relative_path);
                row.section = Text(view->function->section);
                row.category = view->has_sdk_category
                    ? porpoise_sdk_category_name(view->sdk_category) : "title";
                row.confidence = porpoise_match_confidence_name(view->confidence);
                row.requested_action =
                    porpoise_plan_action_name(view->requested_action);
                row.action = porpoise_plan_action_name(view->action);
                row.origin = porpoise_plan_origin_name(view->origin);
                if (view->contract_name != nullptr)
                    row.binding = view->contract_name;
                else if (view->binding != nullptr)
                    row.binding = Text(view->binding->symbol);
                if (view->map_symbol != nullptr) {
                    row.provenance = Text(view->map_symbol->provenance.path) +
                        ":" + std::to_string(view->map_symbol->provenance.line);
                } else if (view->sdk_entry != nullptr) {
                    row.provenance =
                        view->sdk_entry->provenance.source_kind ==
                                PORPOISE_SDK_CATALOG_SOURCE_BUILTIN
                            ? "built-in catalog"
                            : Text(view->sdk_entry->provenance.path) + ":" +
                                  std::to_string(
                                      view->sdk_entry->provenance.line);
                }
                rows.push_back(std::move(row));
            }
        }
        return rows;
    }

    std::optional<RowRecord> SelectedRow() const {
        const auto rows = BuildRows();
        for (const auto &row : rows)
            if (selected_rows_.find(row.key) != selected_rows_.end()) return row;
        return std::nullopt;
    }

    void RenderFunctionTable() {
        InputTextString("Filter", filter_);
        ImGui::SameLine();
        if (ImGui::Button("Clear selection")) selected_rows_.clear();
        ImGui::SameLine();
        ImGui::TextDisabled(
            "Map evidence is optional; exact local catalogs work mapless.");
        RenderBulkActions();

        auto rows = BuildRows();
        rows.erase(std::remove_if(rows.begin(), rows.end(),
            [&](const RowRecord &row) {
                return !FunctionFilterMatches(
                    filter_,
                    {row.locator.target, row.source_name,
                     row.canonical_name, row.unit, row.section,
                     row.category, row.action, row.binding,
                     row.provenance});
            }), rows.end());

        const ImGuiTableFlags flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable |
            ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("functions", 17, flags, ImVec2(0, 390))) {
            enum Column : ImGuiID {
                Select, Target, Source, Canonical, Unit, Section, Address,
                Size, Category, Confidence, Requested, Resolved, Binding,
                Origin, Provenance, Conflict, Override
            };
            ImGui::TableSetupScrollFreeze(3, 1);
            ImGui::TableSetupColumn("##select", ImGuiTableColumnFlags_NoSort |
                ImGuiTableColumnFlags_WidthFixed, 28, Select);
            ImGui::TableSetupColumn("Target", 0, 80, Target);
            ImGui::TableSetupColumn("Source name", 0, 150, Source);
            ImGui::TableSetupColumn("Canonical", 0, 150, Canonical);
            ImGui::TableSetupColumn("Translation unit", 0, 160, Unit);
            ImGui::TableSetupColumn("Section", 0, 70, Section);
            ImGui::TableSetupColumn("Address", 0, 92, Address);
            ImGui::TableSetupColumn("Size", 0, 55, Size);
            ImGui::TableSetupColumn("Category", 0, 95, Category);
            ImGui::TableSetupColumn("Confidence", 0, 85, Confidence);
            ImGui::TableSetupColumn("Requested", 0, 75, Requested);
            ImGui::TableSetupColumn("Resolved", 0, 75, Resolved);
            ImGui::TableSetupColumn("Binding", 0, 120, Binding);
            ImGui::TableSetupColumn("Origin", 0, 100, Origin);
            ImGui::TableSetupColumn("Provenance", 0, 180, Provenance);
            ImGui::TableSetupColumn("Conflict", 0, 65, Conflict);
            ImGui::TableSetupColumn("Override", 0, 75, Override);
            ImGui::TableHeadersRow();
            if (auto *sort = ImGui::TableGetSortSpecs();
                sort != nullptr && sort->SpecsCount > 0) {
                const auto spec = sort->Specs[0];
                auto compare = [&](const RowRecord &left,
                                   const RowRecord &right) {
                    int order = 0;
                    switch (spec.ColumnUserID) {
                    case Target: order = left.locator.target.compare(
                                                right.locator.target); break;
                    case Source: order = left.source_name.compare(
                                                right.source_name); break;
                    case Canonical: order = left.canonical_name.compare(
                                                   right.canonical_name); break;
                    case Unit: order = left.unit.compare(right.unit); break;
                    case Section: order = left.section.compare(
                                                 right.section); break;
                    case Address:
                        order = left.locator.address < right.locator.address
                            ? -1 : left.locator.address > right.locator.address;
                        break;
                    case Size:
                        order = left.locator.size < right.locator.size
                            ? -1 : left.locator.size > right.locator.size;
                        break;
                    case Category: order = left.category.compare(
                                                  right.category); break;
                    case Confidence: order = left.confidence.compare(
                                                    right.confidence); break;
                    case Requested: order = left.requested_action.compare(
                                                    right.requested_action); break;
                    case Resolved: order = left.action.compare(
                                                   right.action); break;
                    case Binding: order = left.binding.compare(
                                                  right.binding); break;
                    case Origin: order = left.origin.compare(right.origin); break;
                    case Provenance: order = left.provenance.compare(
                                                     right.provenance); break;
                    case Conflict:
                        if (!!(left.view->evidence_flags &
                              PORPOISE_PLAN_EVIDENCE_CONFLICT) !=
                            !!(right.view->evidence_flags &
                              PORPOISE_PLAN_EVIDENCE_CONFLICT)) {
                            order = !!(left.view->evidence_flags &
                                      PORPOISE_PLAN_EVIDENCE_CONFLICT)
                                ? 1 : -1;
                        }
                        break;
                    case Override:
                        if (left.view->overridden != right.view->overridden)
                            order = left.view->overridden ? 1 : -1;
                        break;
                    default: order = left.key.compare(right.key); break;
                    }
                    if (order == 0) order = left.key.compare(right.key);
                    return spec.SortDirection == ImGuiSortDirection_Ascending
                        ? order < 0 : order > 0;
                };
                std::stable_sort(rows.begin(), rows.end(), compare);
                sort->SpecsDirty = false;
            }
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()));
            while (clipper.Step()) {
                for (int row_index = clipper.DisplayStart;
                     row_index < clipper.DisplayEnd; ++row_index) {
                    const auto &row = rows[static_cast<std::size_t>(row_index)];
                    ImGui::TableNextRow();
                    ImGui::PushID(row.key.c_str());
                    ImGui::TableSetColumnIndex(0);
                    bool selected = selected_rows_.find(row.key) !=
                                    selected_rows_.end();
                    if (ImGui::Checkbox("##selected", &selected)) {
                        if (selected) selected_rows_.insert(row.key);
                        else selected_rows_.erase(row.key);
                    }
                    auto cell = [&](int column, const std::string &value,
                                    bool selectable = false) {
                        ImGui::TableSetColumnIndex(column);
                        if (selectable) {
                            if (ImGui::Selectable(value.c_str(), selected,
                                ImGuiSelectableFlags_SpanAllColumns |
                                ImGuiSelectableFlags_AllowOverlap)) {
                                if (!ImGui::GetIO().KeyCtrl)
                                    selected_rows_.clear();
                                selected_rows_.insert(row.key);
                            }
                        } else ImGui::TextUnformatted(value.c_str());
                    };
                    cell(1, row.locator.target);
                    cell(2, row.source_name, true);
                    cell(3, row.canonical_name);
                    cell(4, row.unit);
                    cell(5, row.section);
                    cell(6, Hex(row.locator.address));
                    cell(7, std::to_string(row.locator.size));
                    cell(8, row.category);
                    cell(9, row.confidence);
                    cell(10, row.requested_action);
                    cell(11, row.action);
                    cell(12, row.binding);
                    cell(13, row.origin);
                    cell(14, row.provenance);
                    cell(15, (row.view->evidence_flags &
                              PORPOISE_PLAN_EVIDENCE_CONFLICT)
                                 ? "conflict" : "");
                    cell(16, row.view->overridden
                                 ? porpoise_override_action_name(
                                       row.view->override_action) : "auto");
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        RenderFunctionDetails();
    }

    void RenderBulkActions() {
        static int action = PORPOISE_OVERRIDE_AUTO;
        const char *actions[] = {"Auto", "Lift", "Import(contract)",
                                 "Omit", "Treat as Data"};
        ImGui::SetNextItemWidth(170);
        ImGui::Combo("Bulk action", &action, actions,
                     static_cast<int>(std::size(actions)));
        ImGui::SameLine();
        InputTextString("Contract", override_contract_);
        ImGui::SameLine();
        ImGui::Checkbox("Acknowledge conflict", &acknowledge_conflict_);
        ImGui::SameLine();
        ImGui::BeginDisabled(selected_rows_.empty());
        if (ImGui::Button("Apply to selected")) {
            std::vector<OverrideEdit> edits;
            for (const auto &row : BuildRows()) {
                if (selected_rows_.find(row.key) == selected_rows_.end())
                    continue;
                OverrideEdit edit;
                edit.locator = row.locator;
                edit.action = static_cast<PorpoiseOverrideAction>(action);
                edit.contract_name = override_contract_;
                edit.acknowledge_conflict = acknowledge_conflict_;
                edits.push_back(std::move(edit));
            }
            if (model_.ApplyOverrides(edits)) selected_rows_.clear();
        }
        ImGui::EndDisabled();
    }

    void RenderFunctionDetails() {
        const auto selected = SelectedRow();
        if (!selected) {
            ImGui::TextDisabled(
                "Select a function to inspect evidence and disassembly.");
            return;
        }
        const auto &row = *selected;
        const auto &view = *row.view;
        if (ImGui::CollapsingHeader("Evidence and disassembly",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("%s / %s", row.source_name.c_str(),
                        row.canonical_name.c_str());
            ImGui::Text("Locator: %s · %u bytes · %s",
                        Hex(row.locator.address).c_str(), row.locator.size,
                        row.locator.normalized_fingerprint.c_str());
            ImGui::Text("Evidence: map=%s signature=%s ambiguous=%s conflict=%s",
                (view.evidence_flags & PORPOISE_PLAN_EVIDENCE_MAP) ? "yes" : "no",
                (view.evidence_flags & PORPOISE_PLAN_EVIDENCE_SIGNATURE) ? "yes" : "no",
                (view.evidence_flags & PORPOISE_PLAN_EVIDENCE_AMBIGUOUS_SIGNATURE) ? "yes" : "no",
                (view.evidence_flags & PORPOISE_PLAN_EVIDENCE_CONFLICT) ? "yes" : "no");
            ImGui::Text("Structural signature: %u instructions, %u fixed, "
                        "%u meaningful, %u relocations, issues 0x%X",
                        view.signature.instruction_count,
                        view.signature.fixed_instruction_count,
                        view.signature.meaningful_fixed_instruction_count,
                        view.signature.relocation_count,
                        view.signature.issue_flags);
            if (view.blocked)
                ImGui::TextColored(ImVec4(1, .35f, .25f, 1), "Blocked: %s",
                                   Text(view.blocking_reason).c_str());
            if (view.map_symbol != nullptr) {
                ImGui::TextWrapped("Map: %s · module %s · object %s · library %s",
                    Text(view.map_symbol->name).c_str(),
                    Text(view.map_symbol->module).c_str(),
                    Text(view.map_symbol->object).c_str(),
                    Text(view.map_symbol->library).c_str());
            }
            ImGui::BeginChild("disassembly", ImVec2(0, 180), true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            for (std::size_t index = 0; index < view.function->item_count;
                 ++index) {
                const auto &item = view.function->items[index];
                if (item.kind == PORPOISE_ASM_LABEL) {
                    ImGui::TextColored(ImVec4(.5f, .8f, 1, 1), "%s:",
                                       Text(item.label).c_str());
                } else {
                    ImGui::Text("%s  %08X  %-10s %s",
                        Hex(item.address).c_str(), item.word,
                        Text(item.mnemonic).c_str(),
                        Text(item.operands).c_str());
                }
            }
            ImGui::EndChild();
        }
    }

    void ApplyTargetDraft() {
        if (!target_draft_dirty_ ||
            active_target_ >= model_.Project().target_count) return;
        const bool okay = model_.SetTargetId(active_target_, target_draft_.id) &&
            model_.SetTargetPath(active_target_, false, target_draft_.input) &&
            model_.SetTargetPath(active_target_, true, target_draft_.output) &&
            model_.SetTargetEntry(active_target_, target_draft_.entry) &&
            model_.SetTargetSkipList(active_target_, target_draft_.skip_list);
        if (okay) {
            target_draft_dirty_ = false;
            target_draft_error_.clear();
        } else {
            target_draft_error_ =
                "Target settings could not be applied. Use a unique, nonempty "
                "target ID and nonempty source/output paths.";
        }
    }

    void RenderTargetSettings() {
        const bool busy = model_.State() == WorkerState::Running ||
                          model_.State() == WorkerState::Cancelling;
        ImGui::BeginDisabled(busy);
        if (active_target_ >= model_.Project().target_count) {
            ImGui::TextDisabled("Add or open a target to edit settings.");
            ImGui::EndDisabled();
            return;
        }
        auto &target = model_.Project().targets[active_target_];
        ImGui::TextDisabled(
            "Source and output are configured once in the Setup tab.");
        target_draft_dirty_ |= InputTextString("Target ID", target_draft_.id);
        if (ImGui::Checkbox("Enabled", &target.enabled)) model_.MarkDirty();
        target_draft_dirty_ |= InputTextString("Entry symbol", target_draft_.entry);
        if (ImGui::Checkbox("Strict", &target.strict)) model_.MarkDirty();
        int sdk_policy = static_cast<int>(target.sdk_policy);
        const char *policies[] = {"keep", "imported", "omit"};
        if (ImGui::Combo("SDK policy", &sdk_policy, policies, 3)) {
            target.sdk_policy = static_cast<PorpoiseSdkPolicy>(sdk_policy);
            model_.MarkDirty();
        }
        target_draft_dirty_ |= InputOptionalPath(
            "Skip list (optional)", target_draft_.skip_list, false,
            "*.txt");
        if (target_draft_dirty_ && ImGui::Button("Apply target settings"))
            ApplyTargetDraft();

        ImGui::SeparatorText("Symbol maps (optional)");
        ImGui::TextWrapped(
            "No map is required. Add CodeWarrior maps or paired DTK "
            "symbols/splits evidence when available.");
        for (std::size_t index = 0; index < target.symbol_source_count;
             ++index) {
            ImGui::PushID(static_cast<int>(index));
            auto &source = target.symbol_sources[index];
            ImGui::Separator();
            int kind = static_cast<int>(source.kind);
            const char *kinds[] = {"CodeWarrior DOL/REL map",
                                   "DTK symbols.txt + splits.txt"};
            if (ImGui::Combo("Kind", &kind, kinds, 2)) {
                source.kind = static_cast<PorpoiseSymbolSourceKind>(kind);
                model_.MarkDirty();
            }
            std::string path = Text(source.path.value);
            if (InputOptionalPath("Map / symbols", path, false,
                                  source.kind ==
                                          PORPOISE_SYMBOL_SOURCE_CODEWARRIOR_MAP
                                      ? "*.map" : "*.txt"))
                model_.SetSymbolSourcePath(active_target_, index, false, path);
            if (source.kind == PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS) {
                std::string auxiliary = source.has_auxiliary_path
                    ? Text(source.auxiliary_path.value) : std::string();
                if (InputOptionalPath("splits.txt", auxiliary, false,
                                      "*.txt"))
                    model_.SetSymbolSourcePath(
                        active_target_, index, true, auxiliary);
            }
            std::string module = Text(source.module);
            if (InputTextString("Module", module))
                model_.SetSymbolSourceModule(active_target_, index, module);
            if (ImGui::Checkbox("Permissive partial-map parsing",
                                &source.permissive)) model_.MarkDirty();
            if (ImGui::Button("Remove symbol source")) {
                model_.RemoveSymbolSource(active_target_, index);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (ImGui::Button("Add CodeWarrior map")) {
            const auto selected = pfd::open_file(
                "Select CodeWarrior map", "",
                {"Map files", "*.map", "All files", "*"},
                pfd::opt::none).result();
            if (!selected.empty()) model_.AddSymbolSource(
                active_target_, PORPOISE_SYMBOL_SOURCE_CODEWARRIOR_MAP,
                selected.front());
        }
        ImGui::SameLine();
        if (ImGui::Button("Add DTK symbols")) {
            const auto selected = pfd::open_file(
                "Select symbols.txt", "",
                {"Text files", "*.txt", "All files", "*"},
                pfd::opt::none).result();
            if (!selected.empty()) model_.AddSymbolSource(
                active_target_, PORPOISE_SYMBOL_SOURCE_DTK_SYMBOLS,
                selected.front());
        }

        RenderSavedOverrides(target);
        RenderSavedAnnotations(target);

        RenderSharedPaths(false);
        RenderSharedPaths(true);
        ImGui::EndDisabled();
    }

    void RenderSavedOverrides(PorpoiseRecoveryTarget &target) {
        ImGui::SeparatorText("Saved manual overrides");
        if (target.override_count == 0) {
            ImGui::TextDisabled(
                "Overrides created from the function table appear here.");
            return;
        }
        ImGui::TextWrapped(
            "Stale locators block automatic planning. Rebinding is explicit: "
            "enter the verified module/address/size/fingerprint below, or "
            "remove the record and analyze again.");
        for (std::size_t index = 0; index < target.override_count; ++index) {
            auto &value = target.overrides[index];
            ImGui::PushID(static_cast<int>(index) + 30000);
            ImGui::Separator();
            ImGui::Text("%s · contract %s · conflict acknowledged %s",
                porpoise_override_action_name(value.action),
                Text(value.contract_name).empty()
                    ? "—" : Text(value.contract_name).c_str(),
                value.acknowledge_conflict ? "yes" : "no");
            const std::string draft_key = Text(target.id) + "#" +
                                          std::to_string(index);
            auto [draft_iterator, inserted] =
                override_locator_drafts_.try_emplace(draft_key);
            auto &draft = draft_iterator->second;
            if (inserted) {
                draft.module = Text(value.module);
                draft.fingerprint = Text(value.normalized_fingerprint);
                draft.address = value.address;
                draft.size = value.size;
            }
            InputTextString("Module", draft.module);
            ImGui::InputScalar(
                "Address", ImGuiDataType_U32, &draft.address, nullptr, nullptr,
                "%08X", ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::InputScalar(
                "Size", ImGuiDataType_U32, &draft.size, nullptr, nullptr, "%u");
            InputTextString("Normalized fingerprint", draft.fingerprint,
                            ImGuiInputTextFlags_CharsHexadecimal);
            if (ImGui::Button("Apply explicit locator rebind")) {
                draft.fingerprint = Lower(draft.fingerprint);
                model_.RebindOverride(
                    active_target_, index, draft.module, draft.address,
                    draft.size, draft.fingerprint);
            }
            bool acknowledge_conflict = value.acknowledge_conflict;
            if (ImGui::Checkbox("Acknowledge map/signature conflict",
                                &acknowledge_conflict)) {
                OverrideEdit edit;
                edit.locator.target = Text(value.target);
                edit.locator.module = Text(value.module);
                edit.locator.address = value.address;
                edit.locator.size = value.size;
                edit.locator.normalized_fingerprint =
                    Text(value.normalized_fingerprint);
                edit.action = value.action;
                edit.contract_name = Text(value.contract_name);
                edit.acknowledge_conflict = acknowledge_conflict;
                model_.ApplyOverrides({edit});
            }
            if (ImGui::Button("Remove saved override")) {
                model_.RemoveOverride(active_target_, index);
                override_locator_drafts_.clear();
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }

    void RenderSavedAnnotations(PorpoiseRecoveryTarget &target) {
        ImGui::SeparatorText("Saved data annotations");
        if (target.annotation_count == 0) {
            ImGui::TextDisabled(
                "Validated immutable-byte interpretations appear here.");
            return;
        }
        for (std::size_t index = 0; index < target.annotation_count; ++index) {
            const auto &value = target.annotations[index];
            ImGui::PushID(static_cast<int>(index) + 40000);
            ImGui::TextWrapped("%s + %u · %s · %u elements · %s",
                Hex(value.address).c_str(), value.size,
                porpoise_recovery_annotation_interpretation_name(
                    value.interpretation),
                value.element_count,
                Text(value.encoding).empty() ? "no encoding"
                                             : Text(value.encoding).c_str());
            ImGui::TextDisabled("fingerprint %.16s… · bytes %.16s…",
                Text(value.normalized_fingerprint).c_str(),
                Text(value.exact_bytes_sha256).c_str());
            if (ImGui::Button("Remove annotation")) {
                model_.RemoveAnnotationAt(active_target_, index);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }

    void RenderSharedPaths(bool abi) {
        auto &project = model_.Project();
        auto *paths = abi ? project.abi_contracts : project.sdk_catalogs;
        const std::size_t count = abi ? project.abi_contract_count
                                      : project.sdk_catalog_count;
        ImGui::SeparatorText(abi ? "ABI contract manifests"
                                 : "Exact SDK catalogs");
        for (std::size_t index = 0; index < count; ++index) {
            ImGui::PushID(static_cast<int>(index) + (abi ? 10000 : 20000));
            std::string value = Text(paths[index].value);
            if (InputOptionalPath("##path", value, false,
                                  abi ? "*.json" : "*.json"))
                model_.SetSharedPath(abi, index, value);
            ImGui::SameLine();
            if (ImGui::Button("Remove")) {
                model_.RemoveSharedPath(abi, index);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (ImGui::Button(abi ? "Add ABI manifest..." : "Add SDK catalog...")) {
            const auto selected = pfd::open_file(
                abi ? "Select ABI contract manifest" : "Select SDK catalog",
                "", {"JSON files", "*.json", "All files", "*"},
                pfd::opt::none).result();
            if (!selected.empty()) model_.AddSharedPath(abi, selected.front());
        }
    }

    void RenderDataEditor() {
        const auto selected = SelectedRow();
        const PorpoiseRecoveryRunTarget *editor_target =
            selected ? selected->run_target : nullptr;
        if (editor_target == nullptr) {
            const auto *result = model_.RunResult();
            const auto *project_target =
                active_target_ < model_.Project().target_count
                    ? &model_.Project().targets[active_target_] : nullptr;
            if (result != nullptr && project_target != nullptr) {
                for (std::size_t index = 0; index < result->target_count;
                     ++index) {
                    if (result->targets[index].target == project_target) {
                        editor_target = &result->targets[index];
                        break;
                    }
                }
            }
        }
        const auto data_objects = editor_target == nullptr
            ? std::vector<DataObjectRecord>{}
            : model_.DataObjects(*editor_target);
        if (!selected && data_objects.empty()) {
            ImGui::TextWrapped(
                "Analyze a target containing a named data object, or select "
                "one analyzed function in the Functions tab. Data "
                "annotations reinterpret immutable source bytes; they never "
                "modify the ELF or assembly input.");
            return;
        }
        static int interpretation = PORPOISE_RECOVERY_ANNOTATION_RAW_BYTES;
        static int count = 1;
        static std::string encoding;
        bool range_changed = false;

        if (!selected) data_use_object_ = true;
        if (data_objects.empty()) data_use_object_ = false;
        ImGui::BeginDisabled(!selected);
        if (ImGui::RadioButton("Selected function", !data_use_object_)) {
            data_use_object_ = false;
            range_changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(data_objects.empty());
        if (ImGui::RadioButton("Data object / subrange", data_use_object_)) {
            data_use_object_ = true;
            data_object_range_key_.clear();
            range_changed = true;
        }
        ImGui::EndDisabled();

        FunctionLocator locator;
        bool locator_valid = false;
        if (!data_use_object_ && selected) {
            locator = selected->locator;
            locator_valid = true;
        } else if (!data_objects.empty() && editor_target != nullptr) {
            active_data_object_ = std::min(
                active_data_object_, data_objects.size() - 1U);
            std::vector<std::string> labels;
            std::vector<const char *> label_pointers;
            labels.reserve(data_objects.size());
            label_pointers.reserve(data_objects.size());
            for (const auto &object : data_objects) {
                labels.push_back(
                    (object.name.empty() ? "<unnamed>" : object.name) +
                    " | " + object.translation_unit + " | " +
                    object.section + " | " + Hex(object.locator.address) +
                    " + " + std::to_string(object.locator.size));
            }
            for (const auto &label : labels)
                label_pointers.push_back(label.c_str());
            int object_index = static_cast<int>(active_data_object_);
            if (ImGui::Combo(
                    "Data object", &object_index, label_pointers.data(),
                    static_cast<int>(label_pointers.size()))) {
                active_data_object_ =
                    static_cast<std::size_t>(object_index);
                data_object_range_key_.clear();
                range_changed = true;
            }
            const auto &object = data_objects[active_data_object_];
            const std::string range_key =
                object.locator.target + "\x1f" + object.locator.module +
                "\x1f" + std::to_string(object.locator.address) + "\x1f" +
                std::to_string(object.locator.size) + "\x1f" + object.name;
            if (range_key != data_object_range_key_) {
                data_object_range_key_ = range_key;
                data_range_address_ = object.locator.address;
                data_range_size_ = object.locator.size;
                range_changed = true;
            }
            range_changed |= ImGui::InputScalar(
                "Subrange address", ImGuiDataType_U32,
                &data_range_address_, nullptr, nullptr, "%08X",
                ImGuiInputTextFlags_CharsHexadecimal);
            range_changed |= ImGui::InputScalar(
                "Subrange size", ImGuiDataType_U32, &data_range_size_,
                nullptr, nullptr, "%u");
            const std::uint64_t requested_end =
                static_cast<std::uint64_t>(data_range_address_) +
                data_range_size_;
            const std::uint64_t object_end =
                static_cast<std::uint64_t>(object.locator.address) +
                object.locator.size;
            const bool within_selected_object = data_range_size_ != 0U &&
                data_range_address_ >= object.locator.address &&
                requested_end <= object_end;
            if (range_changed ||
                data_locator_session_ != editor_target->session) {
                data_locator_session_ = editor_target->session;
                data_locator_valid_ = within_selected_object &&
                    model_.MakeDataLocator(
                        *editor_target, data_range_address_, data_range_size_,
                        &data_locator_cache_);
            }
            locator_valid = data_locator_valid_;
            if (locator_valid) locator = data_locator_cache_;
            ImGui::TextDisabled(
                "Object bounds: %s + %u bytes",
                Hex(object.locator.address).c_str(), object.locator.size);
            if (!locator_valid) {
                ImGui::TextColored(
                    ImVec4(1.0f, .35f, .3f, 1.0f),
                    "The requested subrange must be nonempty and remain "
                    "inside the selected data object.");
            }
        }

        if (!locator_valid) return;
        const char *names[] = {
            "Raw bytes", "Verified zero-fill", "ASCII", "UTF-8",
            "Shift-JIS", "UTF-16", "s8 array", "u8 array", "s16 array",
            "u16 array", "s32 array", "u32 array", "f32 array",
            "f64 array", "32-bit pointer table"
        };
        if (ImGui::Combo("Interpretation", &interpretation, names,
                         static_cast<int>(std::size(names)))) {
            const auto width = InterpretationWidth(
                static_cast<PorpoiseRecoveryAnnotationInterpretation>(
                    interpretation));
            count = static_cast<int>(std::max<std::size_t>(
                1, locator.size / width));
            switch (interpretation) {
            case PORPOISE_RECOVERY_ANNOTATION_ASCII: encoding = "ascii"; break;
            case PORPOISE_RECOVERY_ANNOTATION_UTF8: encoding = "utf-8"; break;
            case PORPOISE_RECOVERY_ANNOTATION_SHIFT_JIS:
                encoding = "shift-jis"; break;
            case PORPOISE_RECOVERY_ANNOTATION_UTF16:
                encoding = "utf-16be"; break;
            case PORPOISE_RECOVERY_ANNOTATION_S16_ARRAY:
            case PORPOISE_RECOVERY_ANNOTATION_U16_ARRAY:
            case PORPOISE_RECOVERY_ANNOTATION_S32_ARRAY:
            case PORPOISE_RECOVERY_ANNOTATION_U32_ARRAY:
            case PORPOISE_RECOVERY_ANNOTATION_F32_ARRAY:
            case PORPOISE_RECOVERY_ANNOTATION_F64_ARRAY:
            case PORPOISE_RECOVERY_ANNOTATION_POINTER32_ARRAY:
                encoding = "big-endian"; break;
            default: encoding.clear(); break;
            }
        }
        if (range_changed) {
            const auto width = InterpretationWidth(
                static_cast<PorpoiseRecoveryAnnotationInterpretation>(
                    interpretation));
            count = static_cast<int>(std::max<std::size_t>(
                1, locator.size / width));
        }
        ImGui::InputInt("Element / decoded character count", &count);
        count = std::max(1, count);
        InputTextString("Encoding", encoding);
        ImGui::Text("Range: %s + %u bytes", Hex(locator.address).c_str(),
                    locator.size);
        PorpoiseRecoveryByteView bytes;
        porpoise_recovery_byte_view_init(&bytes);
        PorpoiseDiagnostics preview_diagnostics;
        porpoise_diagnostics_init(&preview_diagnostics);
        if (porpoise_recovery_byte_view_extract(
                porpoise_session_program(editor_target->session),
                locator.address, locator.size, &bytes,
                &preview_diagnostics) ==
            PORPOISE_EXIT_OK) {
            auto preview = BytePreview(
                bytes,
                static_cast<PorpoiseRecoveryAnnotationInterpretation>(
                    interpretation), encoding);
            ImGui::InputTextMultiline(
                "##byte-preview", preview.data(),
                preview.size() + 1, ImVec2(-1, 260),
                ImGuiInputTextFlags_ReadOnly);
        }
        porpoise_recovery_byte_view_free(&bytes);
        porpoise_diagnostics_free(&preview_diagnostics);
        if (ImGui::Button("Save annotation")) {
            model_.UpsertAnnotation(
                locator,
                static_cast<PorpoiseRecoveryAnnotationInterpretation>(
                    interpretation),
                static_cast<std::uint32_t>(count), encoding);
            selected_rows_.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove matching annotation")) {
            model_.RemoveAnnotation(locator);
            selected_rows_.clear();
        }
    }

    void RenderAbiEditor() {
        ImGui::TextWrapped(
            "Ordinary direct-call register mappings can be copied into a "
            "local manifest and edited here. Stateful adapter contracts are "
            "specialized and remain read-only/manifest-backed.");
        const PorpoiseAbiManifest *manifest = nullptr;
        const auto *result = model_.RunResult();
        if (result != nullptr && result->target_count != 0) {
            std::size_t run_index = 0;
            for (; run_index < result->target_count; ++run_index)
                if (result->targets[run_index].target ==
                    (active_target_ < model_.Project().target_count
                         ? &model_.Project().targets[active_target_] : nullptr))
                    break;
            if (run_index < result->target_count)
                manifest = model_.LoadedAbiManifest(run_index);
        }
        if (manifest != nullptr) {
            ImGui::BeginChild("loaded-contracts", ImVec2(280, 260), true);
            for (std::size_t index = 0; index < manifest->function_count;
                 ++index) {
                const auto &contract = manifest->functions[index];
                std::string label = Text(contract.symbol);
                if (contract.adapter != nullptr) label += "  [adapter]";
                if (ImGui::Selectable(label.c_str(), selected_contract_ == index))
                    selected_contract_ = index;
            }
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("contract-detail", ImVec2(0, 260), true);
            if (selected_contract_ < manifest->function_count) {
                const auto &contract = manifest->functions[selected_contract_];
                ImGui::Text("%s %s", contract.kind == PORPOISE_ABI_IMPORT
                                           ? "import" : "export",
                            Text(contract.symbol).c_str());
                ImGui::Text("Header: %s", Text(contract.header).c_str());
                ImGui::Text("Wrapper: %s", Text(contract.wrapper).c_str());
                ImGui::Text("Return: %s in %s",
                            porpoise_abi_type_name(contract.result.type),
                            RegisterText(contract.result).c_str());
                for (std::size_t index = 0; index < contract.argument_count;
                     ++index) {
                    const auto &argument = contract.arguments[index];
                    ImGui::BulletText("%s: %s in %s", Text(argument.name).c_str(),
                        porpoise_abi_type_name(argument.type),
                        RegisterText(argument).c_str());
                }
                if (contract.adapter != nullptr) {
                    ImGui::TextColored(ImVec4(1, .75f, .25f, 1),
                        "Specialized adapter %s (read-only)", contract.adapter);
                } else if (ImGui::Button("Copy into direct editor")) {
                    abi_drafts_.push_back(
                        WorkbenchModel::DraftFromContract(contract));
                    active_abi_draft_ = abi_drafts_.size() - 1;
                    MarkAbiDraftsDirty();
                }
            }
            ImGui::EndChild();
        } else {
            ImGui::TextDisabled("Analyze a target to inspect loaded contracts.");
        }

        ImGui::SeparatorText("Local direct-call manifest");
        if (ImGui::Button("New direct contract")) {
            DirectAbiDraft draft;
            draft.header = "porpoise/local_contracts.h";
            abi_drafts_.push_back(std::move(draft));
            active_abi_draft_ = abi_drafts_.size() - 1;
            MarkAbiDraftsDirty();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(abi_drafts_.empty());
        if (ImGui::Button("Remove draft") &&
            active_abi_draft_ < abi_drafts_.size()) {
            abi_drafts_.erase(abi_drafts_.begin() +
                              static_cast<std::ptrdiff_t>(active_abi_draft_));
            if (!abi_drafts_.empty())
                active_abi_draft_ = std::min(active_abi_draft_,
                                             abi_drafts_.size() - 1);
            MarkAbiDraftsDirty();
        }
        ImGui::EndDisabled();
        if (!abi_drafts_.empty()) {
            active_abi_draft_ = std::min(active_abi_draft_,
                                         abi_drafts_.size() - 1);
            std::vector<const char *> names;
            for (const auto &draft : abi_drafts_)
                names.push_back(draft.symbol.empty()
                                    ? "<new contract>" : draft.symbol.c_str());
            int selected_draft = static_cast<int>(active_abi_draft_);
            if (ImGui::Combo("Draft", &selected_draft, names.data(),
                             static_cast<int>(names.size()))) {
                active_abi_draft_ = static_cast<std::size_t>(selected_draft);
            }
            if (RenderAbiDraft(abi_drafts_[active_abi_draft_]))
                MarkAbiDraftsDirty();
        }
        ImGui::BeginDisabled(abi_drafts_.empty());
        if (ImGui::Button("Save local ABI manifest...")) {
            const auto path = pfd::save_file(
                "Save direct ABI manifest", "local-abi.json",
                {"JSON files", "*.json"},
                pfd::opt::force_overwrite).result();
            if (!path.empty() &&
                model_.WriteDirectAbiManifest(path, abi_drafts_, true)) {
                abi_manifest_path_ = path;
                abi_drafts_dirty_ = false;
                RemoveAbiAutosave();
            }
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped(
            "Changing a contract already supplied by another manifest will "
            "remain a visible catalog conflict until that older manifest is "
            "removed from Project & Target.");
    }

    bool RenderAbiDraft(DirectAbiDraft &draft) {
        bool changed = false;
        int kind = static_cast<int>(draft.kind);
        const char *kinds[] = {"import", "export"};
        if (ImGui::Combo("Kind", &kind, kinds, 2)) {
            draft.kind = static_cast<PorpoiseAbiKind>(kind);
            changed = true;
        }
        changed |= InputTextString("Symbol", draft.symbol);
        changed |= InputTextString("Wrapper", draft.wrapper);
        changed |= InputTextString("Header", draft.header);
        int result_type = static_cast<int>(draft.result_type);
        const char *types[] = {"void", "u8", "u16", "u32", "s8", "s16",
                               "s32", "f32", "f64", "pointer"};
        if (ImGui::Combo("Return type", &result_type, types, 10)) {
            draft.result_type = static_cast<PorpoiseAbiType>(result_type);
            if (draft.result_type == PORPOISE_ABI_VOID) {
                draft.result_register_class = PORPOISE_ABI_REGISTER_NONE;
                draft.result_register_index = 0;
            } else if (draft.result_type == PORPOISE_ABI_F32 ||
                       draft.result_type == PORPOISE_ABI_F64) {
                draft.result_register_class = PORPOISE_ABI_REGISTER_FPR;
                draft.result_register_index = 1;
            } else {
                draft.result_register_class = PORPOISE_ABI_REGISTER_GPR;
                draft.result_register_index = 3;
            }
            changed = true;
        }
        ImGui::BeginDisabled(draft.result_type == PORPOISE_ABI_VOID);
        int result_register_class =
            static_cast<int>(draft.result_register_class);
        const char *classes[] = {"none", "GPR", "FPR"};
        if (ImGui::Combo(
                "Return register class", &result_register_class,
                classes, static_cast<int>(std::size(classes)))) {
            draft.result_register_class =
                static_cast<PorpoiseAbiRegisterClass>(
                    result_register_class);
            changed = true;
        }
        int result_register_index =
            static_cast<int>(draft.result_register_index);
        if (ImGui::InputInt(
                "Return register index", &result_register_index)) {
            draft.result_register_index = static_cast<unsigned int>(
                std::max(0, result_register_index));
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SeparatorText("Arguments");
        for (std::size_t index = 0; index < draft.arguments.size(); ++index) {
            auto &argument = draft.arguments[index];
            ImGui::PushID(static_cast<int>(index));
            changed |= InputTextString("Name", argument.name);
            int type = static_cast<int>(argument.type);
            if (ImGui::Combo("Type", &type, types + 1, 9)) {
                argument.type = static_cast<PorpoiseAbiType>(type + 1);
                argument.register_class =
                    argument.type == PORPOISE_ABI_F32 ||
                            argument.type == PORPOISE_ABI_F64
                        ? PORPOISE_ABI_REGISTER_FPR
                        : PORPOISE_ABI_REGISTER_GPR;
                argument.register_index = argument.register_class ==
                                                   PORPOISE_ABI_REGISTER_FPR
                    ? 1 : 3;
                changed = true;
            }
            int register_class = static_cast<int>(argument.register_class);
            if (ImGui::Combo("Register class", &register_class, classes, 3)) {
                argument.register_class =
                    static_cast<PorpoiseAbiRegisterClass>(register_class);
                changed = true;
            }
            int register_index = static_cast<int>(argument.register_index);
            if (ImGui::InputInt("Register index", &register_index)) {
                argument.register_index = static_cast<unsigned int>(
                    std::max(0, register_index));
                changed = true;
            }
            if (ImGui::Button("Remove argument")) {
                draft.arguments.erase(
                    draft.arguments.begin() + static_cast<std::ptrdiff_t>(index));
                changed = true;
                ImGui::PopID();
                break;
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        if (ImGui::Button("Add argument")) {
            AbiArgumentDraft argument;
            argument.name = "arg" + std::to_string(draft.arguments.size());
            argument.register_index = static_cast<unsigned int>(
                std::min<std::size_t>(10, 3 + draft.arguments.size()));
            draft.arguments.push_back(std::move(argument));
            changed = true;
        }
        return changed;
    }

    void RenderTitleHostAdvanced() {
        if (active_target_ >= model_.Project().target_count) {
            ImGui::TextDisabled("Select a target first.");
            return;
        }
        const auto &target = model_.Project().targets[active_target_];
        const auto *profile = target.has_title_host
            ? &target.title_host
            : model_.InferredTitleHostProfile(Text(target.id));
        if (profile == nullptr) {
            ImGui::TextDisabled(
                "No reviewed or inferred title-host profile is available.");
            return;
        }
        ImGui::TextUnformatted(target.has_title_host
            ? "Reviewed portable profile" : "Unsaved inferred suggestion");
        ImGui::Text("Entry: %s", Hex(profile->entry_address).c_str());
        ImGui::Text("r1: %s   r2: %s   r13: %s",
                    Hex(profile->gpr[1]).c_str(),
                    Hex(profile->gpr[2]).c_str(),
                    Hex(profile->gpr[13]).c_str());
        ImGui::Text("Arena: %s..%s",
                    Hex(profile->arena_lo).c_str(),
                    Hex(profile->arena_hi).c_str());
        ImGui::Text("Native DVD initialization: %s",
                    profile->initialize_dvd ? "enabled" : "disabled");

        ImGui::SeparatorText("Startup locators");
        for (std::size_t index = 0;
             index < profile->startup_function_count; ++index) {
            const auto &function = profile->startup_functions[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::Text("%zu. %s +%u bytes%s", index + 1,
                        Hex(function.address).c_str(), function.size,
                        (function.flags &
                         PORPOISE_RECOVERY_TITLE_STARTUP_ESTABLISH_GUEST_MAIN_THREAD_AFTER)
                            != 0
                            ? " | establish guest main thread" : "");
            ImGui::TextWrapped("Module: %s | Fingerprint: %s",
                               Text(function.module).c_str(),
                               Text(function.normalized_fingerprint).c_str());
            ImGui::PopID();
        }

        ImGui::SeparatorText("Initial memory words");
        for (std::size_t index = 0; index < profile->initial_word_count;
             ++index) {
            const auto &word = profile->initial_words[index];
            ImGui::Text("%s = %s", Hex(word.address).c_str(),
                        Hex(word.value).c_str());
        }
        ImGui::TextDisabled(
            "Return to Setup to accept, replace, or configure DVD policy.");
    }

    void RenderDiagnostics() {
        ImGui::Text("Worker: %s · exit code %d",
                    WorkerStateName(model_.State()), model_.LastExitCode());
        ImGui::Text("Operation: %s",
                    WorkerOperationName(model_.Operation()));
        if (const auto *preflight = model_.RuntimePreflightResult();
            preflight != nullptr) {
            ImGui::Text("Runtime validated: Meson %s | %s %s (%s)",
                        preflight->meson_version,
                        preflight->compiler_family,
                        preflight->compiler_version,
                        preflight->compiler_target);
        }
        if (const auto *build = model_.BuildResult(); build != nullptr) {
            ImGui::TextWrapped("Build cache: %s", build->cache_directory);
            ImGui::TextWrapped("Executable: %s", build->executable_path);
            ImGui::Text("Compiler: %s %s (%s)",
                        build->preflight.compiler_family,
                        build->preflight.compiler_version,
                        build->preflight.compiler_target);
        }
        ImGui::SeparatorText("Diagnostics");
        ImGui::BeginChild("diagnostics", ImVec2(0, 260), true);
        const bool busy = model_.State() == WorkerState::Running ||
                          model_.State() == WorkerState::Cancelling;
        if (busy) {
            ImGui::TextDisabled(
                "Diagnostics are published when the worker stops.");
        } else {
            const auto &diagnostics = model_.Diagnostics();
            for (std::size_t index = 0; index < diagnostics.count; ++index) {
                const auto &diagnostic = diagnostics.items[index];
                const ImVec4 color =
                    diagnostic.severity == PORPOISE_SEVERITY_ERROR
                        ? ImVec4(1, .35f, .3f, 1)
                        : diagnostic.severity == PORPOISE_SEVERITY_WARNING
                            ? ImVec4(1, .75f, .25f, 1)
                            : ImVec4(.65f, .8f, 1, 1);
                ImGui::TextColored(color, "[%s] %s:%zu %s%s%s",
                    SeverityName(diagnostic.severity),
                    Text(diagnostic.file).c_str(), diagnostic.line,
                    diagnostic.address == 0 ? "" :
                        Hex(diagnostic.address).c_str(),
                    diagnostic.address == 0 ? "" : " ",
                    Text(diagnostic.message).c_str());
            }
        }
        ImGui::EndChild();
        ImGui::SeparatorText("Progress log");
        ImGui::BeginChild("logs", ImVec2(0, 260), true);
        for (const auto &line : model_.Logs())
            ImGui::TextUnformatted(line.c_str());
        ImGui::EndChild();
    }

    void RenderReport() {
        InputOptionalPath("Aggregate report", report_path_, false, "*.json");
        InputOptionalPath("Runtime directory", runtime_directory_, true);
        if (ImGui::Button("Refresh report"))
            report_contents_ = ReadTextFile(report_path_);
        ImGui::SameLine();
        ImGui::TextDisabled(
            "Schema v3 includes actions, evidence, provenance and blockers.");
        if (report_contents_.empty() && !report_path_.empty())
            report_contents_ = ReadTextFile(report_path_);
        if (!report_contents_.empty()) {
            ImGui::InputTextMultiline(
                "##report", report_contents_.data(),
                report_contents_.size() + 1, ImVec2(-1, 360),
                ImGuiInputTextFlags_ReadOnly);
        } else ImGui::TextDisabled("No report loaded.");
        ImGui::SeparatorText("Target outputs");
        for (std::size_t index = 0; index < model_.Project().target_count;
             ++index) {
            const auto &target = model_.Project().targets[index];
            ImGui::TextWrapped("%s: %s", Text(target.id).c_str(),
                               Text(target.output.value).c_str());
        }
        ImGui::TextDisabled("Change the active target output in Setup.");
    }

    WorkbenchModel model_;
    std::string preference_directory_;
    std::string machine_state_path_;
    std::string imgui_ini_path_;
    std::string last_project_;
    std::string recovery_document_;
    std::string dtk_path_;
    std::string runtime_directory_ = porpoise_default_runtime_directory();
    std::string libporpoise_directory_;
    std::string libporpoise_capability_path_;
    LibPorpoiseCapabilities libporpoise_capabilities_;
    std::string meson_path_;
    std::string c_compiler_path_;
    std::string cpp_compiler_path_;
    std::string dvd_root_;
    std::string run_working_directory_;
    std::string build_type_ = "debugoptimized";
    std::string trace_path_;
    std::string report_path_;
    std::string report_contents_;
    std::string filter_;
    std::string override_contract_;
    bool acknowledge_conflict_ = false;
    bool active_target_only_ = true;
    bool select_setup_tab_ = true;
    bool select_functions_tab_ = false;
    bool select_diagnostics_tab_ = false;
    bool last_run_analyze_only_ = true;
    bool trace_enabled_ = true;
    bool inferred_initialize_dvd_ = false;
    bool allow_copy_fallback_ = false;
    bool runtime_config_expanded_ = false;
    bool open_copy_fallback_popup_ = false;
    bool open_cache_cleanup_popup_ = false;
    bool open_profile_reset_popup_ = false;
    bool replacement_initialize_dvd_ = false;
    bool cache_size_known_ = false;
    std::uint64_t frame_limit_ = 3;
    std::uintmax_t cache_size_ = 0;
    WorkerOperation last_operation_ = WorkerOperation::None;
    std::string built_target_id_;
    std::string pending_profile_reset_target_;
    std::string cache_cleanup_error_;
    std::size_t active_target_ = 0;
    TargetDraft target_draft_;
    bool target_draft_dirty_ = false;
    std::string target_draft_error_;
    std::unordered_set<std::string> selected_rows_;
    bool data_use_object_ = false;
    std::size_t active_data_object_ = 0;
    std::string data_object_range_key_;
    std::uint32_t data_range_address_ = 0;
    std::uint32_t data_range_size_ = 0;
    const PorpoiseSession *data_locator_session_ = nullptr;
    FunctionLocator data_locator_cache_;
    bool data_locator_valid_ = false;
    std::unordered_map<std::string, OverrideLocatorDraft>
        override_locator_drafts_;
    std::vector<DirectAbiDraft> abi_drafts_;
    std::string abi_manifest_path_;
    bool abi_drafts_dirty_ = false;
    std::size_t active_abi_draft_ = 0;
    std::size_t selected_contract_ = 0;
    RunRequest pending_run_;
    bool open_recovery_popup_ = false;
    bool open_unsaved_popup_ = false;
    bool open_overwrite_popup_ = false;
    PendingFileAction pending_file_action_ = PendingFileAction::None;
    bool close_ = false;
    std::chrono::steady_clock::time_point last_autosave_ =
        std::chrono::steady_clock::now();
};

}  // namespace

int RunWorkbench(int argc, char **argv) {
    bool smoke_test = false;
    std::string initial_project;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] == nullptr ? "" : argv[index];
        if (argument == "--smoke-test") smoke_test = true;
        else if (initial_project.empty()) initial_project = argument;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return PORPOISE_EXIT_INTERNAL;
    }
    if (smoke_test) SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
#if defined(_WIN32)
    else SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
#endif
    SDL_Window *window = SDL_CreateWindow(
        "Porpoise Recovery Workbench", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 1440, 900,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == nullptr) {
        std::fprintf(stderr, "window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return PORPOISE_EXIT_INTERNAL;
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (renderer == nullptr) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC |
                                                   SDL_RENDERER_SOFTWARE);
    }
    if (renderer == nullptr) {
        std::fprintf(stderr, "renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return PORPOISE_EXIT_INTERNAL;
    }
    char *preference_path = SDL_GetPrefPath("Porpoise", "RecoveryWorkbench");
    std::string preference_directory = preference_path == nullptr
        ? "." : preference_path;
    SDL_free(preference_path);
    if (smoke_test) {
        const char *smoke_preference =
            std::getenv("PORPOISE_GUI_SMOKE_STATE_DIR");
        if (smoke_preference != nullptr && smoke_preference[0] != '\0')
            preference_directory = smoke_preference;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    WorkbenchApp app(preference_directory);
    io.IniFilename = app.ImGuiIniPath().c_str();
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.FrameRounding = 3.0f;
    style.WindowRounding = 4.0f;
    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer) ||
        !ImGui_ImplSDLRenderer2_Init(renderer)) {
        std::fprintf(stderr, "Dear ImGui backend initialization failed\n");
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return PORPOISE_EXIT_INTERNAL;
    }
    app.OpenInitial(initial_project);
    int exit_code = PORPOISE_EXIT_OK;
    if (smoke_test && !app.RunStartupSmokeScenario()) {
        std::fprintf(stderr, "workbench startup smoke scenario failed\n");
        exit_code = PORPOISE_EXIT_INTERNAL;
    }

    unsigned int rendered_frames = 0;
    while (exit_code == PORPOISE_EXIT_OK && !app.ShouldClose()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT &&
                 event.window.event == SDL_WINDOWEVENT_CLOSE &&
                 event.window.windowID == SDL_GetWindowID(window))) {
                app.RequestClose();
            }
        }
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        app.Frame();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 24, 27, 31, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        ++rendered_frames;
        if (smoke_test && rendered_frames >= 3) break;
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exit_code;
}

}  // namespace porpoise::gui
