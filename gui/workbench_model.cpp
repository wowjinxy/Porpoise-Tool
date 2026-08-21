#include "workbench_model.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

extern "C" {
#include "porpoise/sha256.h"
}

namespace porpoise::gui {
namespace {

char *Duplicate(const std::string &value) {
    auto *copy = static_cast<char *>(std::malloc(value.size() + 1));
    if (copy != nullptr) std::memcpy(copy, value.c_str(), value.size() + 1);
    return copy;
}

bool Replace(char *&destination, const std::string &value,
             bool empty_is_null = false) {
    char *copy = nullptr;
    if (!(empty_is_null && value.empty())) {
        copy = Duplicate(value);
        if (copy == nullptr) return false;
    }
    std::free(destination);
    destination = copy;
    return true;
}

std::string NullToEmpty(const char *value) {
    return value == nullptr ? std::string() : std::string(value);
}

void FreePath(PorpoiseRecoveryPath &path) {
    std::free(path.value);
    std::free(path.resolved);
    path = {};
}

void FreeOverride(PorpoiseRecoveryOverride &value) {
    std::free(value.target);
    std::free(value.module);
    std::free(value.normalized_fingerprint);
    std::free(value.contract_name);
    value = {};
}

void FreeAnnotation(PorpoiseRecoveryAnnotation &value) {
    std::free(value.target);
    std::free(value.module);
    std::free(value.normalized_fingerprint);
    std::free(value.exact_bytes_sha256);
    std::free(value.encoding);
    value = {};
}

void FreeCache(PorpoiseRecoveryTargetCache &cache) {
    std::free(cache.input_sha256);
    std::free(cache.settings_sha256);
    std::free(cache.dtk_version);
    for (std::size_t index = 0; index < cache.dependency_count; ++index) {
        auto &dependency = cache.dependencies[index];
        FreePath(dependency.path);
        std::free(dependency.sha256);
    }
    std::free(cache.dependencies);
    for (std::size_t index = 0; index < cache.match_count; ++index) {
        auto &match = cache.matches[index];
        std::free(match.module);
        std::free(match.normalized_fingerprint);
        std::free(match.canonical_identity);
        std::free(match.contract_name);
    }
    std::free(cache.matches);
    cache = {};
}

void FreeSymbolSource(PorpoiseRecoverySymbolSource &source) {
    FreePath(source.path);
    FreePath(source.auxiliary_path);
    std::free(source.module);
    source = {};
}

void FreeTarget(PorpoiseRecoveryTarget &target) {
    std::free(target.id);
    FreePath(target.input);
    FreePath(target.output);
    std::free(target.entry);
    for (std::size_t index = 0; index < target.symbol_source_count; ++index)
        FreeSymbolSource(target.symbol_sources[index]);
    std::free(target.symbol_sources);
    FreePath(target.skip_list);
    for (std::size_t index = 0; index < target.override_count; ++index)
        FreeOverride(target.overrides[index]);
    std::free(target.overrides);
    for (std::size_t index = 0; index < target.annotation_count; ++index)
        FreeAnnotation(target.annotations[index]);
    std::free(target.annotations);
    FreeCache(target.cache);
    target = {};
}

bool IsAbsolutePortable(const std::string &value) {
    if (value.empty()) return false;
    if (value[0] == '/' || value[0] == '\\') return true;
    return value.size() >= 3 &&
           ((value[0] >= 'A' && value[0] <= 'Z') ||
            (value[0] >= 'a' && value[0] <= 'z')) &&
           value[1] == ':' && (value[2] == '/' || value[2] == '\\');
}

std::string GenericPath(const std::filesystem::path &path) {
    return path.lexically_normal().generic_string();
}

std::string NormalizedDocumentIdentity(const std::filesystem::path &path) {
    std::error_code error;
    auto normalized = std::filesystem::absolute(path, error);
    if (error) {
        error.clear();
        normalized = path;
    }
    const auto canonical = std::filesystem::weakly_canonical(normalized, error);
    if (!error) normalized = canonical;
    std::string identity = GenericPath(normalized);
#if defined(_WIN32)
    /* Windows path identity is case-insensitive for supported project files. */
    std::transform(
        identity.begin(), identity.end(), identity.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
#endif
    return identity;
}

std::string AutosaveStem(const std::filesystem::path &document) {
    std::string filename = document.filename().string();
    std::transform(
        filename.begin(), filename.end(), filename.begin(),
        [](unsigned char character) {
            if (std::isalnum(character) || character == '-' ||
                character == '_' || character == '.') {
                return static_cast<char>(character);
            }
            return '_';
        });
    if (filename.empty()) filename = "project";
    constexpr std::size_t kMaximumFilenameLength = 64;
    if (filename.size() > kMaximumFilenameLength)
        filename.resize(kMaximumFilenameLength);

    const std::string identity = NormalizedDocumentIdentity(document);
    std::uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    char digest_hex[PORPOISE_SHA256_HEX_SIZE];
    porpoise_sha256(identity.data(), identity.size(), digest);
    porpoise_sha256_hex(digest, digest_hex);
    return ".porpoise-autosave-" + filename + "-" + digest_hex + ".json";
}

std::string ModuleForTarget(const PorpoiseRecoveryTarget &target) {
    if (target.symbol_source_count != 0 &&
        target.symbol_sources[0].module != nullptr) {
        return target.symbol_sources[0].module;
    }
    if (target.override_count != 0 && target.overrides[0].module != nullptr) {
        return target.overrides[0].module;
    }
    return {};
}

std::string AnnotationEncoding(
    PorpoiseRecoveryAnnotationInterpretation interpretation,
    const std::string &requested) {
    if (!requested.empty()) return requested;
    switch (interpretation) {
    case PORPOISE_RECOVERY_ANNOTATION_ASCII: return "ascii";
    case PORPOISE_RECOVERY_ANNOTATION_UTF8: return "utf-8";
    case PORPOISE_RECOVERY_ANNOTATION_SHIFT_JIS: return "shift-jis";
    case PORPOISE_RECOVERY_ANNOTATION_UTF16: return "utf-16be";
    case PORPOISE_RECOVERY_ANNOTATION_S16_ARRAY:
    case PORPOISE_RECOVERY_ANNOTATION_U16_ARRAY:
    case PORPOISE_RECOVERY_ANNOTATION_S32_ARRAY:
    case PORPOISE_RECOVERY_ANNOTATION_U32_ARRAY:
    case PORPOISE_RECOVERY_ANNOTATION_F32_ARRAY:
    case PORPOISE_RECOVERY_ANNOTATION_F64_ARRAY:
    case PORPOISE_RECOVERY_ANNOTATION_POINTER32_ARRAY:
        return "big-endian";
    default: return {};
    }
}

bool RangesOverlap(std::uint32_t left_address, std::uint32_t left_size,
                   std::uint32_t right_address, std::uint32_t right_size) {
    const std::uint64_t left_end =
        static_cast<std::uint64_t>(left_address) + left_size;
    const std::uint64_t right_end =
        static_cast<std::uint64_t>(right_address) + right_size;
    return static_cast<std::uint64_t>(left_address) < right_end &&
           static_cast<std::uint64_t>(right_address) < left_end;
}

bool LowercaseFingerprint(const std::string &value) {
    return value.size() == PORPOISE_SHA256_HEX_SIZE - 1 &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

std::string JsonEscape(const std::string &value) {
    std::ostringstream output;
    output << '"';
    for (unsigned char character : value) {
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20) {
                output << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0')
                       << static_cast<unsigned int>(character)
                       << std::dec << std::setfill(' ');
            } else {
                output << static_cast<char>(character);
            }
            break;
        }
    }
    output << '"';
    return output.str();
}

std::string RegisterName(PorpoiseAbiRegisterClass register_class,
                         unsigned int register_index) {
    if (register_class == PORPOISE_ABI_REGISTER_GPR)
        return "r" + std::to_string(register_index);
    if (register_class == PORPOISE_ABI_REGISTER_FPR)
        return "f" + std::to_string(register_index);
    return "none";
}

bool SyncFile(const std::filesystem::path &path, std::string *error_out);

bool WriteAbiJson(const std::filesystem::path &path,
                   const std::vector<DirectAbiDraft> &functions) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << "{\n  \"schema_version\": 1,\n  \"functions\": [";
    if (!functions.empty()) output << '\n';
    for (std::size_t index = 0; index < functions.size(); ++index) {
        const auto &function = functions[index];
        output << "    {\n      \"kind\": "
               << JsonEscape(function.kind == PORPOISE_ABI_IMPORT
                                 ? "import" : "export")
               << ",\n      \"symbol\": " << JsonEscape(function.symbol);
        if (!function.wrapper.empty())
            output << ",\n      \"wrapper\": "
                   << JsonEscape(function.wrapper);
        output << ",\n      \"header\": " << JsonEscape(function.header)
               << ",\n      \"return\": {\"type\": "
               << JsonEscape(porpoise_abi_type_name(function.result_type));
        if (function.result_type != PORPOISE_ABI_VOID) {
            output << ", \"register\": "
                   << JsonEscape(RegisterName(
                          function.result_register_class,
                          function.result_register_index));
        }
        output << "},\n      \"arguments\": [";
        if (!function.arguments.empty()) output << '\n';
        for (std::size_t argument_index = 0;
             argument_index < function.arguments.size(); ++argument_index) {
            const auto &argument = function.arguments[argument_index];
            output << "        {\"name\": " << JsonEscape(argument.name)
                   << ", \"type\": "
                   << JsonEscape(porpoise_abi_type_name(argument.type))
                   << ", \"register\": "
                   << JsonEscape(RegisterName(argument.register_class,
                                               argument.register_index))
                   << "}";
            if (argument_index + 1 < function.arguments.size()) output << ',';
            output << '\n';
        }
        if (!function.arguments.empty()) output << "      ";
        output << "]\n    }";
        if (index + 1 < functions.size()) output << ',';
        output << '\n';
    }
    output << "  ]\n}\n";
    output.flush();
    if (!output.good()) return false;
    output.close();
    if (output.fail()) return false;
    std::string sync_error;
    return SyncFile(path, &sync_error);
}

constexpr const char *kAbiDraftRecoveryMagic =
    "PORPOISE_ABI_DRAFT_RECOVERY_V1";
constexpr std::size_t kMaximumRecoveredDrafts = 65536;
constexpr std::size_t kMaximumRecoveredArguments = 65536;
constexpr std::size_t kMaximumRecoveredString = 1024 * 1024;

bool WriteAbiDraftRecovery(
    const std::filesystem::path &path,
    const std::vector<DirectAbiDraft> &functions) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << kAbiDraftRecoveryMagic << '\n' << functions.size() << '\n';
    for (const auto &function : functions) {
        output << "F " << static_cast<int>(function.kind) << ' '
               << static_cast<int>(function.result_type) << ' '
               << static_cast<int>(function.result_register_class) << ' '
               << function.result_register_index << ' '
               << function.arguments.size() << ' '
               << std::quoted(function.symbol) << ' '
               << std::quoted(function.wrapper) << ' '
               << std::quoted(function.header) << '\n';
        for (const auto &argument : function.arguments) {
            output << "A " << static_cast<int>(argument.type) << ' '
                   << static_cast<int>(argument.register_class) << ' '
                   << argument.register_index << ' '
                   << std::quoted(argument.name) << '\n';
        }
    }
    output.flush();
    if (!output.good()) return false;
    output.close();
    if (output.fail()) return false;
    std::string sync_error;
    return SyncFile(path, &sync_error);
}

bool RecoveredStringValid(const std::string &value) {
    return value.size() <= kMaximumRecoveredString;
}

bool ReadAbiDraftRecovery(
    const std::filesystem::path &path,
    std::vector<DirectAbiDraft> *functions_out) {
    if (functions_out == nullptr) return false;
    std::ifstream input(path, std::ios::binary);
    std::string magic;
    if (!std::getline(input, magic) || magic != kAbiDraftRecoveryMagic)
        return false;
    std::size_t function_count = 0;
    if (!(input >> function_count) ||
        function_count > kMaximumRecoveredDrafts) return false;
    std::vector<DirectAbiDraft> loaded;
    loaded.reserve(function_count);
    for (std::size_t index = 0; index < function_count; ++index) {
        char record = '\0';
        int kind = 0;
        int result_type = 0;
        int result_register_class = 0;
        unsigned int result_register_index = 0;
        std::size_t argument_count = 0;
        DirectAbiDraft function;
        if (!(input >> record >> kind >> result_type >>
              result_register_class >> result_register_index >>
              argument_count >> std::quoted(function.symbol) >>
              std::quoted(function.wrapper) >>
              std::quoted(function.header)) ||
            record != 'F' ||
            kind < static_cast<int>(PORPOISE_ABI_IMPORT) ||
            kind > static_cast<int>(PORPOISE_ABI_EXPORT) ||
            result_type < static_cast<int>(PORPOISE_ABI_VOID) ||
            result_type > static_cast<int>(PORPOISE_ABI_POINTER) ||
            result_register_class <
                static_cast<int>(PORPOISE_ABI_REGISTER_NONE) ||
            result_register_class >
                static_cast<int>(PORPOISE_ABI_REGISTER_FPR) ||
            argument_count > kMaximumRecoveredArguments ||
            !RecoveredStringValid(function.symbol) ||
            !RecoveredStringValid(function.wrapper) ||
            !RecoveredStringValid(function.header)) return false;
        function.kind = static_cast<PorpoiseAbiKind>(kind);
        function.result_type = static_cast<PorpoiseAbiType>(result_type);
        function.result_register_class =
            static_cast<PorpoiseAbiRegisterClass>(result_register_class);
        function.result_register_index = result_register_index;
        function.arguments.reserve(argument_count);
        for (std::size_t argument_index = 0;
             argument_index < argument_count; ++argument_index) {
            int type = 0;
            int register_class = 0;
            AbiArgumentDraft argument;
            if (!(input >> record >> type >> register_class >>
                  argument.register_index >> std::quoted(argument.name)) ||
                record != 'A' ||
                type < static_cast<int>(PORPOISE_ABI_U8) ||
                type > static_cast<int>(PORPOISE_ABI_POINTER) ||
                register_class <
                    static_cast<int>(PORPOISE_ABI_REGISTER_NONE) ||
                register_class >
                    static_cast<int>(PORPOISE_ABI_REGISTER_FPR) ||
                !RecoveredStringValid(argument.name)) return false;
            argument.type = static_cast<PorpoiseAbiType>(type);
            argument.register_class =
                static_cast<PorpoiseAbiRegisterClass>(register_class);
            function.arguments.push_back(std::move(argument));
        }
        loaded.push_back(std::move(function));
    }
    input >> std::ws;
    if (!input.eof()) return false;
    *functions_out = std::move(loaded);
    return true;
}

#if defined(_WIN32)
std::string NativeErrorMessage(unsigned long value) {
    return std::error_code(
        static_cast<int>(value), std::system_category()).message();
}
#endif

unsigned long ProcessIdentifier() {
#if defined(_WIN32)
    return static_cast<unsigned long>(GetCurrentProcessId());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

bool ReserveUniqueAdjacentFile(
    const std::filesystem::path &destination,
    const char *tag,
    std::filesystem::path *path_out,
    std::string *error_out) {
    if (path_out == nullptr || error_out == nullptr) return false;
    const auto nonce = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    for (unsigned int attempt = 0; attempt < 1000; ++attempt) {
        std::ostringstream name;
        name << '.' << destination.filename().string() << ".porpoise-" << tag
             << '-' << std::hex << nonce << '-' << ProcessIdentifier() << '-'
             << std::dec << attempt;
        const auto candidate = destination.parent_path() / name.str();
#if defined(_WIN32)
        HANDLE handle = CreateFileW(
            candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            *path_out = candidate;
            return true;
        }
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
            continue;
        *error_out = NativeErrorMessage(error);
        return false;
#else
        const int descriptor = open(
            candidate.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (descriptor >= 0) {
            if (close(descriptor) != 0) {
                const int error = errno;
                std::error_code ignored;
                std::filesystem::remove(candidate, ignored);
                *error_out = std::strerror(error);
                return false;
            }
            *path_out = candidate;
            return true;
        }
        if (errno == EEXIST) continue;
        *error_out = std::strerror(errno);
        return false;
#endif
    }
    *error_out = "could not allocate a unique adjacent staging file";
    return false;
}

bool SyncFile(const std::filesystem::path &path, std::string *error_out) {
#if defined(_WIN32)
    HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        *error_out = NativeErrorMessage(GetLastError());
        return false;
    }
    const BOOL flushed = FlushFileBuffers(handle);
    const DWORD flush_error = flushed ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (!flushed) {
        *error_out = NativeErrorMessage(flush_error);
        return false;
    }
    return true;
#else
    const int descriptor = open(path.c_str(), O_RDONLY);
    if (descriptor < 0) {
        *error_out = std::strerror(errno);
        return false;
    }
    const bool flushed = fsync(descriptor) == 0;
    const int flush_error = errno;
    const bool closed = close(descriptor) == 0;
    if (!flushed || !closed) {
        *error_out = std::strerror(flushed ? errno : flush_error);
        return false;
    }
    return true;
#endif
}

#if defined(_WIN32)
bool UniqueAdjacentBackupPath(
    const std::filesystem::path &destination,
    std::filesystem::path *backup_out,
    std::string *error_out) {
    const auto nonce = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    for (unsigned int attempt = 0; attempt < 1000; ++attempt) {
        std::ostringstream name;
        name << '.' << destination.filename().string()
             << ".porpoise-backup-" << std::hex << nonce << '-'
             << ProcessIdentifier() << '-' << std::dec << attempt;
        const auto candidate = destination.parent_path() / name.str();
        const DWORD attributes = GetFileAttributesW(candidate.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                *backup_out = candidate;
                return true;
            }
            *error_out = NativeErrorMessage(error);
            return false;
        }
    }
    *error_out = "could not allocate a unique adjacent rollback file";
    return false;
}
#endif

bool PublishAdjacentFileAtomically(
    const std::filesystem::path &stage,
    const std::filesystem::path &destination,
    std::string *error_out,
    std::string *warning_out) {
    std::error_code status_error;
    const bool existed = std::filesystem::exists(destination, status_error);
    if (status_error) {
        *error_out = "cannot inspect destination: " + status_error.message();
        return false;
    }
    if (existed &&
        !std::filesystem::is_regular_file(destination, status_error)) {
        *error_out = status_error
            ? "cannot inspect destination: " + status_error.message()
            : "destination is not a regular file";
        return false;
    }

#if defined(_WIN32)
    if (!existed) {
        if (MoveFileExW(stage.c_str(), destination.c_str(),
                        MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
        *error_out = "atomic move failed: " +
            NativeErrorMessage(GetLastError());
        return false;
    }

    std::filesystem::path backup;
    if (!UniqueAdjacentBackupPath(destination, &backup, error_out))
        return false;
    if (ReplaceFileW(
            destination.c_str(), stage.c_str(), backup.c_str(),
            REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) {
        std::error_code cleanup_error;
        std::filesystem::remove(backup, cleanup_error);
        if (cleanup_error) {
            *warning_out = "published file, but could not remove rollback "
                "backup " + backup.string() + ": " +
                cleanup_error.message();
        }
        return true;
    }

    const DWORD publish_error = GetLastError();
    std::error_code exists_error;
    if (std::filesystem::exists(backup, exists_error) && !exists_error) {
        if (!MoveFileExW(
                backup.c_str(), destination.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            *error_out = "atomic replacement failed and rollback from " +
                backup.string() + " also failed: " +
                NativeErrorMessage(GetLastError());
            return false;
        }
    }
    *error_out = "atomic replacement failed: " +
        NativeErrorMessage(publish_error);
    return false;
#else
    if (rename(stage.c_str(), destination.c_str()) != 0) {
        *error_out = "atomic rename failed: " + std::string(std::strerror(errno));
        return false;
    }
    const auto parent = destination.parent_path();
#if defined(O_DIRECTORY)
    const int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
#else
    const int directory = open(parent.c_str(), O_RDONLY);
#endif
    if (directory >= 0) {
        if (fsync(directory) != 0) {
            *warning_out = "published file, but could not synchronize its "
                "directory: " + std::string(std::strerror(errno));
        }
        (void)close(directory);
    }
    return true;
#endif
}

}  // namespace

bool FunctionFilterMatches(
    const std::string &filter,
    const std::vector<std::string> &searchable_fields) {
    if (filter.empty()) return true;
    auto lower = [](const std::string &value) {
        std::string result = value;
        std::transform(
            result.begin(), result.end(), result.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
        return result;
        };
    std::vector<std::string> fields;
    fields.reserve(searchable_fields.size());
    for (const auto &field : searchable_fields) fields.push_back(lower(field));
    std::istringstream query(lower(filter));
    std::string token;
    while (query >> token) {
        if (!std::any_of(
                fields.begin(), fields.end(),
                [&](const std::string &field) {
                    return field.find(token) != std::string::npos;
                })) {
            return false;
        }
    }
    return true;
}

WorkbenchModel::WorkbenchModel() {
    porpoise_recovery_project_init(&project_);
    porpoise_recovery_project_init(&worker_project_);
    porpoise_recovery_run_result_init(&run_result_);
    porpoise_diagnostics_init(&diagnostics_);
}

WorkbenchModel::~WorkbenchModel() {
    Cancel();
    Wait();
    porpoise_recovery_run_result_free(&run_result_);
    porpoise_diagnostics_free(&diagnostics_);
    porpoise_recovery_project_free(&worker_project_);
    porpoise_recovery_project_free(&project_);
}

void WorkbenchModel::ClearRunResult() {
    if (worker_state_.load() == WorkerState::Running ||
        worker_state_.load() == WorkerState::Cancelling) return;
    porpoise_recovery_run_result_free(&run_result_);
    porpoise_recovery_run_result_init(&run_result_);
}

void WorkbenchModel::ResetDiagnostics() {
    porpoise_diagnostics_free(&diagnostics_);
    porpoise_diagnostics_init(&diagnostics_);
}

void WorkbenchModel::AddLocalDiagnostic(PorpoiseSeverity severity,
                                        const std::string &message) {
    porpoise_diagnostics_add(&diagnostics_, severity,
                             document_path_.empty()
                                 ? nullptr : document_path_.c_str(),
                             0, 0, "%s", message.c_str());
}

bool WorkbenchModel::NewProject() {
    if (State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    ClearRunResult();
    ResetDiagnostics();
    porpoise_recovery_project_free(&project_);
    porpoise_recovery_project_init(&project_);
    std::error_code error;
    const auto current = std::filesystem::current_path(error);
    if (!error) project_.directory = Duplicate(GenericPath(current));
    document_path_.clear();
    dirty_ = false;
    worker_state_ = WorkerState::Idle;
    return error.value() == 0;
}

bool WorkbenchModel::LoadProject(const std::string &path) {
    if (path.empty() || State() == WorkerState::Running ||
        State() == WorkerState::Cancelling) return false;
    ClearRunResult();
    ResetDiagnostics();
    if (porpoise_recovery_project_load(
            &project_, path.c_str(), &diagnostics_) != PORPOISE_EXIT_OK) {
        return false;
    }
    document_path_ = GenericPath(std::filesystem::absolute(path));
    dirty_ = false;
    worker_state_ = WorkerState::Idle;
    return true;
}

bool WorkbenchModel::RecoverAutosave(const std::string &document_path) {
    if (State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    std::error_code error;
    std::filesystem::path rebound_document;
    std::filesystem::path autosave;
    if (document_path.empty()) {
        autosave = std::filesystem::path(AutosavePath());
    } else {
        rebound_document = std::filesystem::absolute(document_path, error);
        if (error) return false;
        autosave = rebound_document.parent_path() /
            AutosaveStem(rebound_document);
    }
    if (autosave.empty()) return false;

    const std::string rebound_path = document_path.empty()
        ? std::string() : GenericPath(rebound_document);
    const std::string rebound_directory = GenericPath(
        document_path.empty() ? autosave.parent_path()
                              : rebound_document.parent_path());
    char *owned_path = rebound_path.empty() ? nullptr : Duplicate(rebound_path);
    char *owned_directory = Duplicate(rebound_directory);
    if ((!rebound_path.empty() && owned_path == nullptr) ||
        owned_directory == nullptr) {
        std::free(owned_path);
        std::free(owned_directory);
        AddLocalDiagnostic(PORPOISE_SEVERITY_ERROR,
                           "out of memory while preparing autosave recovery");
        return false;
    }
    if (!LoadProject(GenericPath(autosave))) {
        std::free(owned_path);
        std::free(owned_directory);
        return false;
    }
    std::free(project_.path);
    std::free(project_.directory);
    project_.path = owned_path;
    project_.directory = owned_directory;
    document_path_ = rebound_path;
    dirty_ = true;
    AddLocalDiagnostic(PORPOISE_SEVERITY_INFO,
                       document_path.empty()
                           ? "recovered the untitled workbench autosave; use "
                             "Save As to publish it to a project file"
                           : "recovered the newer workbench autosave; use Save "
                             "to publish it to the project file");
    return true;
}

bool WorkbenchModel::Save() {
    if (document_path_.empty() || State() == WorkerState::Running ||
        State() == WorkerState::Cancelling) return false;
    ResetDiagnostics();
    if (porpoise_recovery_project_save(
            &project_, document_path_.c_str(), &diagnostics_) !=
        PORPOISE_EXIT_OK) return false;
    dirty_ = false;
    return true;
}

bool WorkbenchModel::SaveAs(const std::string &path) {
    if (path.empty() || State() == WorkerState::Running ||
        State() == WorkerState::Cancelling) return false;
    ResetDiagnostics();
    if (porpoise_recovery_project_save(
            &project_, path.c_str(), &diagnostics_) != PORPOISE_EXIT_OK) {
        return false;
    }
    /* Reload to bind all relative paths to the new project location. */
    PorpoiseRecoveryProject rebound;
    porpoise_recovery_project_init(&rebound);
    if (porpoise_recovery_project_load(
            &rebound, path.c_str(), &diagnostics_) != PORPOISE_EXIT_OK) {
        porpoise_recovery_project_free(&rebound);
        return false;
    }
    ClearRunResult();
    porpoise_recovery_project_free(&project_);
    project_ = rebound;
    document_path_ = GenericPath(std::filesystem::absolute(path));
    dirty_ = false;
    return true;
}

std::string WorkbenchModel::AutosavePath() const {
    std::filesystem::path base;
    if (!document_path_.empty()) {
        const auto document = std::filesystem::path(document_path_);
        base = document.parent_path();
        return GenericPath(base / AutosaveStem(document));
    } else if (!untitled_recovery_directory_.empty()) {
        base = untitled_recovery_directory_;
    } else if (project_.directory != nullptr) {
        base = project_.directory;
    } else {
        std::error_code error;
        base = std::filesystem::current_path(error);
        if (error) return {};
    }
    return GenericPath(base / ".porpoise-autosave-untitled.json");
}

bool WorkbenchModel::SetUntitledRecoveryDirectory(const std::string &path) {
    if (path.empty() || State() == WorkerState::Running ||
        State() == WorkerState::Cancelling) return false;
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error) return false;
    untitled_recovery_directory_ = GenericPath(absolute);
    return true;
}

bool WorkbenchModel::Autosave() {
    if (!dirty_ || State() == WorkerState::Running ||
        State() == WorkerState::Cancelling) return false;
    const auto path = AutosavePath();
    if (path.empty()) return false;
    PorpoiseDiagnostics autosave_diagnostics;
    porpoise_diagnostics_init(&autosave_diagnostics);
    const int result = porpoise_recovery_project_save(
        &project_, path.c_str(), &autosave_diagnostics);
    if (result != PORPOISE_EXIT_OK) {
        for (std::size_t index = 0; index < autosave_diagnostics.count;
             ++index) {
            AddLocalDiagnostic(autosave_diagnostics.items[index].severity,
                               autosave_diagnostics.items[index].message);
        }
    }
    porpoise_diagnostics_free(&autosave_diagnostics);
    return result == PORPOISE_EXIT_OK;
}

bool WorkbenchModel::HasNewerAutosave() const {
    std::error_code error;
    const auto autosave = std::filesystem::path(AutosavePath());
    if (!std::filesystem::is_regular_file(autosave, error) || error)
        return false;
    if (document_path_.empty()) return true;
    const auto document = std::filesystem::path(document_path_);
    const auto autosave_time = std::filesystem::last_write_time(autosave, error);
    if (error) return false;
    if (!std::filesystem::exists(document, error) || error) return true;
    const auto document_time = std::filesystem::last_write_time(document, error);
    return !error && autosave_time > document_time;
}

void WorkbenchModel::MarkDirty() {
    if (State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return;
    dirty_ = true;
    ClearRunResult();
}

std::string WorkbenchModel::ResolveProjectPath(
    const std::string &value) const {
    if (value.empty()) return {};
    if (IsAbsolutePortable(value)) return GenericPath(value);
    std::filesystem::path base;
    if (project_.directory != nullptr) base = project_.directory;
    else {
        std::error_code error;
        base = std::filesystem::current_path(error);
        if (error) return value;
    }
    return GenericPath(base / std::filesystem::path(value));
}

bool WorkbenchModel::SetPath(PorpoiseRecoveryPath &path,
                             const std::string &value) {
    if (value.empty()) return false;
    char *spelling = Duplicate(value);
    char *resolved = Duplicate(ResolveProjectPath(value));
    if (spelling == nullptr || resolved == nullptr) {
        std::free(spelling);
        std::free(resolved);
        return false;
    }
    FreePath(path);
    path.value = spelling;
    path.resolved = resolved;
    return true;
}

bool WorkbenchModel::AddTarget(const std::string &preferred_id) {
    if (State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    std::string id = preferred_id.empty() ? "target" : preferred_id;
    auto exists = [&](const std::string &candidate) {
        return porpoise_recovery_project_find_target(&project_,
                                                     candidate.c_str()) != nullptr;
    };
    if (exists(id)) {
        const std::string base = id;
        for (unsigned int suffix = 2; exists(id); ++suffix)
            id = base + "-" + std::to_string(suffix);
    }
    auto *targets = static_cast<PorpoiseRecoveryTarget *>(std::realloc(
        project_.targets,
        (project_.target_count + 1) * sizeof(*project_.targets)));
    if (targets == nullptr) return false;
    project_.targets = targets;
    auto &target = project_.targets[project_.target_count];
    target = {};
    target.id = Duplicate(id);
    target.enabled = true;
    target.source_kind = PORPOISE_RECOVERY_SOURCE_ASSEMBLY;
    target.strict = true;
    target.sdk_policy = PORPOISE_SDK_POLICY_KEEP;
    if (target.id == nullptr || !SetPath(target.input, ".") ||
        !SetPath(target.output, "porpoise-output")) {
        FreeTarget(target);
        return false;
    }
    ++project_.target_count;
    dirty_ = true;
    ClearRunResult();
    return true;
}

bool WorkbenchModel::RemoveTarget(std::size_t target_index) {
    if (target_index >= project_.target_count ||
        State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    FreeTarget(project_.targets[target_index]);
    if (target_index + 1 < project_.target_count) {
        std::memmove(&project_.targets[target_index],
                     &project_.targets[target_index + 1],
                     (project_.target_count - target_index - 1) *
                         sizeof(*project_.targets));
    }
    --project_.target_count;
    dirty_ = true;
    ClearRunResult();
    return true;
}

bool WorkbenchModel::SetTargetId(std::size_t target_index,
                                 const std::string &id) {
    if (target_index >= project_.target_count || id.empty() ||
        State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    for (std::size_t index = 0; index < project_.target_count; ++index) {
        if (index != target_index && project_.targets[index].id != nullptr &&
            id == project_.targets[index].id) return false;
    }
    auto &target = project_.targets[target_index];
    char *copy = Duplicate(id);
    if (copy == nullptr) return false;
    for (std::size_t index = 0; index < target.override_count; ++index) {
        if (!Replace(target.overrides[index].target, id)) {
            std::free(copy);
            return false;
        }
    }
    for (std::size_t index = 0; index < target.annotation_count; ++index) {
        if (!Replace(target.annotations[index].target, id)) {
            std::free(copy);
            return false;
        }
    }
    std::free(target.id);
    target.id = copy;
    dirty_ = true;
    ClearRunResult();
    return true;
}

bool WorkbenchModel::SetTargetEntry(std::size_t target_index,
                                    const std::string &entry) {
    if (target_index >= project_.target_count ||
        State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    if (!Replace(project_.targets[target_index].entry, entry, true)) return false;
    dirty_ = true;
    ClearRunResult();
    return true;
}

bool WorkbenchModel::SetTargetPath(std::size_t target_index, bool output,
                                   const std::string &value) {
    if (target_index >= project_.target_count ||
        State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    auto &target = project_.targets[target_index];
    if (!SetPath(output ? target.output : target.input, value)) return false;
    dirty_ = true;
    ClearRunResult();
    return true;
}

bool WorkbenchModel::SetTargetSkipList(std::size_t target_index,
                                       const std::string &value) {
    if (target_index >= project_.target_count ||
        State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    auto &target = project_.targets[target_index];
    if (value.empty()) {
        FreePath(target.skip_list);
        target.has_skip_list = false;
    } else {
        if (!SetPath(target.skip_list, value)) return false;
        target.has_skip_list = true;
    }
    dirty_ = true;
    ClearRunResult();
    return true;
}

bool WorkbenchModel::AddSharedPath(bool abi, const std::string &value) {
    if (value.empty() || State() == WorkerState::Running ||
        State() == WorkerState::Cancelling) return false;
    auto *&paths = abi ? project_.abi_contracts : project_.sdk_catalogs;
    auto &count = abi ? project_.abi_contract_count : project_.sdk_catalog_count;
    auto *grown = static_cast<PorpoiseRecoveryPath *>(
        std::realloc(paths, (count + 1) * sizeof(*paths)));
    if (grown == nullptr) return false;
    paths = grown;
    paths[count] = {};
    if (!SetPath(paths[count], value)) return false;
    ++count;
    dirty_ = true;
    ClearRunResult();
    return true;
}

bool WorkbenchModel::SetSharedPath(bool abi, std::size_t index,
                                   const std::string &value) {
    auto *paths = abi ? project_.abi_contracts : project_.sdk_catalogs;
    const auto count = abi ? project_.abi_contract_count
                           : project_.sdk_catalog_count;
    if (index >= count || State() == WorkerState::Running ||
        State() == WorkerState::Cancelling || !SetPath(paths[index], value))
        return false;
    dirty_ = true;
    ClearRunResult();
    return true;
}

bool WorkbenchModel::RemoveSharedPath(bool abi, std::size_t index) {
    auto *&paths = abi ? project_.abi_contracts : project_.sdk_catalogs;
    auto &count = abi ? project_.abi_contract_count : project_.sdk_catalog_count;
    if (index >= count || State() == WorkerState::Running ||
        State() == WorkerState::Cancelling) return false;
    FreePath(paths[index]);
    if (index + 1 < count)
        std::memmove(&paths[index], &paths[index + 1],
                     (count - index - 1) * sizeof(*paths));
    --count;
    dirty_ = true;
    ClearRunResult();
    return true;
}

bool WorkbenchModel::AddSymbolSource(std::size_t target_index,
                                     PorpoiseSymbolSourceKind kind,
                                     const std::string &path) {
    if (target_index >= project_.target_count || path.empty() ||
        State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    auto &target = project_.targets[target_index];
    auto *grown = static_cast<PorpoiseRecoverySymbolSource *>(std::realloc(
        target.symbol_sources,
        (target.symbol_source_count + 1) * sizeof(*target.symbol_sources)));
    if (grown == nullptr) return false;
    target.symbol_sources = grown;
    auto &source = target.symbol_sources[target.symbol_source_count];
    source = {};
    source.kind = kind;
    source.module = Duplicate("");
    if (source.module == nullptr || !SetPath(source.path, path)) {
        FreeSymbolSource(source);
        return false;
    }
    ++target.symbol_source_count;
    dirty_ = true;
    ClearRunResult();
    return true;
}

bool WorkbenchModel::RemoveSymbolSource(std::size_t target_index,
                                        std::size_t index) {
    if (target_index >= project_.target_count ||
        State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    auto &target = project_.targets[target_index];
    if (index >= target.symbol_source_count) return false;
    FreeSymbolSource(target.symbol_sources[index]);
    if (index + 1 < target.symbol_source_count)
        std::memmove(&target.symbol_sources[index],
                     &target.symbol_sources[index + 1],
                     (target.symbol_source_count - index - 1) *
                         sizeof(*target.symbol_sources));
    --target.symbol_source_count;
    dirty_ = true;
    ClearRunResult();
    return true;
}

bool WorkbenchModel::SetSymbolSourcePath(std::size_t target_index,
                                         std::size_t index, bool auxiliary,
                                         const std::string &value) {
    if (target_index >= project_.target_count ||
        State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    auto &target = project_.targets[target_index];
    if (index >= target.symbol_source_count) return false;
    auto &source = target.symbol_sources[index];
    if (auxiliary && value.empty()) {
        FreePath(source.auxiliary_path);
        source.has_auxiliary_path = false;
    } else {
        if (!SetPath(auxiliary ? source.auxiliary_path : source.path, value))
            return false;
        if (auxiliary) source.has_auxiliary_path = true;
    }
    dirty_ = true;
    ClearRunResult();
    return true;
}

bool WorkbenchModel::SetSymbolSourceModule(std::size_t target_index,
                                           std::size_t index,
                                           const std::string &module) {
    if (target_index >= project_.target_count ||
        State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    auto &target = project_.targets[target_index];
    if (index >= target.symbol_source_count ||
        !Replace(target.symbol_sources[index].module, module)) return false;
    dirty_ = true;
    ClearRunResult();
    return true;
}

void WorkbenchModel::ProgressThunk(void *user_data,
                                   PorpoiseOperationPhase phase,
                                   std::size_t completed, std::size_t total,
                                   const char *detail) {
    static_cast<WorkbenchModel *>(user_data)->OnProgress(
        phase, completed, total, detail);
}

bool WorkbenchModel::CancelThunk(void *user_data) {
    return static_cast<WorkbenchModel *>(user_data)
        ->cancel_requested_.load();
}

void WorkbenchModel::OnProgress(PorpoiseOperationPhase phase,
                                std::size_t completed, std::size_t total,
                                const char *detail) {
    {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        progress_.phase = phase;
        progress_.completed = completed;
        progress_.total = total;
        progress_.detail = detail == nullptr ? "" : detail;
    }
    std::ostringstream line;
    line << porpoise_operation_phase_name(phase) << ' ' << completed;
    if (total != 0) line << '/' << total;
    if (detail != nullptr && detail[0] != '\0') line << ": " << detail;
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (logs_.empty() || logs_.back() != line.str()) logs_.push_back(line.str());
}

bool WorkbenchModel::Start(const RunRequest &request) {
    PollWorker();
    if (worker_.joinable() || document_path_.empty() ||
        State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    ClearRunResult();
    ResetDiagnostics();
    porpoise_recovery_project_free(&worker_project_);
    porpoise_recovery_project_init(&worker_project_);
    worker_project_ready_ = false;
    replan_worker_ = false;
    const auto document = std::filesystem::path(document_path_);
    const auto snapshot_path = document.parent_path() /
        (".porpoise-run-" +
         std::to_string(std::chrono::steady_clock::now()
                            .time_since_epoch().count()) +
         ".json");
    const bool snapshot_saved = porpoise_recovery_project_save(
        &project_, snapshot_path.string().c_str(), &diagnostics_) ==
        PORPOISE_EXIT_OK;
    const bool snapshot_loaded = snapshot_saved &&
        porpoise_recovery_project_load(
            &worker_project_, snapshot_path.string().c_str(),
            &diagnostics_) == PORPOISE_EXIT_OK;
    std::error_code snapshot_error;
    std::filesystem::remove(snapshot_path, snapshot_error);
    if (snapshot_error) {
        AddLocalDiagnostic(
            PORPOISE_SEVERITY_WARNING,
            "failed to remove the temporary worker project snapshot");
    }
    if (!snapshot_loaded ||
        !Replace(worker_project_.path, document_path_)) {
        worker_state_ = WorkerState::Failed;
        last_exit_code_ = PORPOISE_EXIT_USAGE;
        return false;
    }
    worker_project_ready_ = true;
    {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        progress_ = {};
    }
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        logs_.clear();
    }
    cancel_requested_ = false;
    worker_finished_ = false;
    last_exit_code_ = PORPOISE_EXIT_OK;
    worker_state_ = WorkerState::Running;
    worker_ = std::thread(&WorkbenchModel::WorkerMain, this, request);
    return true;
}

bool WorkbenchModel::StartReplan(
    const std::vector<std::string> &target_ids) {
    PollWorker();
    if (target_ids.empty() || worker_.joinable() ||
        State() == WorkerState::Running ||
        State() == WorkerState::Cancelling) {
        return false;
    }
    ResetDiagnostics();
    {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        progress_ = {};
    }
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        logs_.clear();
    }
    cancel_requested_ = false;
    worker_finished_ = false;
    last_exit_code_ = PORPOISE_EXIT_OK;
    replan_worker_ = true;
    worker_state_ = WorkerState::Running;
    worker_ = std::thread(
        &WorkbenchModel::ReplanWorkerMain, this, target_ids);
    return true;
}

void WorkbenchModel::WorkerMain(RunRequest request) {
    std::vector<const char *> selectors;
    selectors.reserve(request.target_ids.size());
    for (const auto &target : request.target_ids)
        selectors.push_back(target.c_str());
    PorpoiseOperationCallbacks callbacks;
    porpoise_operation_callbacks_init(&callbacks);
    callbacks.progress = &WorkbenchModel::ProgressThunk;
    callbacks.cancelled = &WorkbenchModel::CancelThunk;
    callbacks.user_data = this;
    PorpoiseRecoveryRunOptions options;
    porpoise_recovery_run_options_init(&options);
    options.target_ids = selectors.empty() ? nullptr : selectors.data();
    options.target_id_count = selectors.size();
    options.analyze_only = request.analyze_only;
    options.force = request.force;
    options.report_path = request.report_path.empty()
        ? nullptr : request.report_path.c_str();
    options.runtime_directory = request.runtime_directory.empty()
        ? nullptr : request.runtime_directory.c_str();
    options.dtk_path = request.dtk_path.empty()
        ? nullptr : request.dtk_path.c_str();
    options.operation = &callbacks;
    const int result = porpoise_recovery_project_run(
        &worker_project_, &options, &run_result_, &diagnostics_);
    last_exit_code_ = result;
    /* PollWorker/Wait publishes the terminal state after joining. */
    worker_finished_ = true;
}

void WorkbenchModel::ReplanWorkerMain(
    std::vector<std::string> target_ids) {
    PorpoiseOperationCallbacks callbacks;
    porpoise_operation_callbacks_init(&callbacks);
    callbacks.progress = &WorkbenchModel::ProgressThunk;
    callbacks.cancelled = &WorkbenchModel::CancelThunk;
    callbacks.user_data = this;
    int aggregate = PORPOISE_EXIT_OK;
    for (const auto &target_id : target_ids) {
        if (cancel_requested_.load()) {
            aggregate = PORPOISE_EXIT_CANCELLED;
            break;
        }
        const int result = ReplanLoadedTarget(target_id, &callbacks);
        if (result != PORPOISE_EXIT_OK) aggregate = result;
        if (result == PORPOISE_EXIT_INTERNAL ||
            result == PORPOISE_EXIT_CANCELLED) {
            break;
        }
    }
    last_exit_code_ = aggregate;
    worker_finished_ = true;
}

void WorkbenchModel::Cancel() {
    if (State() == WorkerState::Running) {
        cancel_requested_ = true;
        worker_state_ = WorkerState::Cancelling;
    }
}

bool WorkbenchModel::PollWorker() {
    if (!worker_.joinable() || !worker_finished_.load()) return false;
    worker_.join();
    AdoptWorkerProject();
    return true;
}

void WorkbenchModel::Wait() {
    if (worker_.joinable()) {
        worker_.join();
        AdoptWorkerProject();
    }
}

void WorkbenchModel::AdoptWorkerProject() {
    if (replan_worker_) {
        replan_worker_ = false;
        const int result = last_exit_code_.load();
        if (result == PORPOISE_EXIT_OK) worker_state_ = WorkerState::Succeeded;
        else if (result == PORPOISE_EXIT_CANCELLED ||
                 cancel_requested_.load()) {
            worker_state_ = WorkerState::Cancelled;
        } else worker_state_ = WorkerState::Failed;
        return;
    }
    if (!worker_project_ready_) return;
    /*
     * The runner may refresh compact dependency/match caches. Moving the
     * snapshot (rather than cloning it again) preserves every run-result
     * target pointer while publishing those cache changes to the editor.
     */
    porpoise_recovery_project_free(&project_);
    project_ = worker_project_;
    porpoise_recovery_project_init(&worker_project_);
    worker_project_ready_ = false;
    dirty_ = true;
    const int result = last_exit_code_.load();
    if (result == PORPOISE_EXIT_OK) worker_state_ = WorkerState::Succeeded;
    else if (result == PORPOISE_EXIT_CANCELLED || cancel_requested_.load())
        worker_state_ = WorkerState::Cancelled;
    else worker_state_ = WorkerState::Failed;
}

ProgressSnapshot WorkbenchModel::Progress() const {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    return progress_;
}

std::vector<std::string> WorkbenchModel::Logs() const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    return logs_;
}

bool WorkbenchModel::SelectedOutputsExist(
    const std::vector<std::string> &target_ids) const {
    auto selected = [&](const PorpoiseRecoveryTarget &target) {
        if (target_ids.empty()) return target.enabled;
        return std::find(target_ids.begin(), target_ids.end(),
                         NullToEmpty(target.id)) != target_ids.end();
    };
    std::error_code error;
    for (std::size_t index = 0; index < project_.target_count; ++index) {
        const auto &target = project_.targets[index];
        if (selected(target) && target.output.resolved != nullptr &&
            std::filesystem::exists(target.output.resolved, error) &&
            !error) {
            return true;
        }
        error.clear();
    }
    return false;
}

const PorpoiseRecoveryRunResult *WorkbenchModel::RunResult() const {
    if (State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return nullptr;
    if (worker_.joinable()) return nullptr;
    return &run_result_;
}

PorpoiseRecoveryRunTarget *WorkbenchModel::FindRunTarget(
    const std::string &target) {
    for (std::size_t index = 0; index < run_result_.target_count; ++index) {
        auto &candidate = run_result_.targets[index];
        if (candidate.target != nullptr && candidate.target->id != nullptr &&
            target == candidate.target->id) return &candidate;
    }
    return nullptr;
}

const PorpoiseRecoveryRunTarget *WorkbenchModel::FindRunTarget(
    const std::string &target) const {
    for (std::size_t index = 0; index < run_result_.target_count; ++index) {
        const auto &candidate = run_result_.targets[index];
        if (candidate.target != nullptr && candidate.target->id != nullptr &&
            target == candidate.target->id) return &candidate;
    }
    return nullptr;
}

FunctionLocator WorkbenchModel::MakeLocator(
    const PorpoiseRecoveryRunTarget &target,
    const PorpoiseFunctionPlanView &view) const {
    FunctionLocator locator;
    locator.target = target.target == nullptr || target.target->id == nullptr
        ? "" : target.target->id;
    locator.module = target.plan == nullptr
        ? ModuleForTarget(*target.target)
        : NullToEmpty(porpoise_plan_module(target.plan));
    locator.address = view.function->start_address;
    locator.size = view.function->size;
    locator.normalized_fingerprint = view.signature.digest_hex;
    return locator;
}

bool WorkbenchModel::MakeDataLocator(
    const PorpoiseRecoveryRunTarget &target,
    std::uint32_t address,
    std::uint32_t size,
    FunctionLocator *locator_out) const {
    if (locator_out == nullptr || target.target == nullptr ||
        target.target->id == nullptr || target.session == nullptr ||
        size == 0U ||
        static_cast<std::uint64_t>(address) + size >
            UINT64_C(0x100000000)) {
        return false;
    }
    const auto *program = porpoise_session_program(target.session);
    if (program == nullptr) return false;

    const std::uint64_t range_end =
        static_cast<std::uint64_t>(address) + size;
    bool contained = false;
    for (std::size_t file_index = 0;
         file_index < program->file_count && !contained; ++file_index) {
        const auto &file = program->files[file_index];
        for (std::size_t object_index = 0;
             object_index < file.data_object_count; ++object_index) {
            const auto &object = file.data_objects[object_index];
            const std::uint64_t object_end =
                static_cast<std::uint64_t>(object.address) + object.size;
            if (object.size != 0U && address >= object.address &&
                range_end <= object_end) {
                contained = true;
                break;
            }
        }
    }
    if (!contained) return false;

    char fingerprint[PORPOISE_SHA256_HEX_SIZE];
    PorpoiseDiagnostics diagnostics;
    porpoise_diagnostics_init(&diagnostics);
    const int result = porpoise_recovery_normalized_fingerprint_compute(
        program, address, size, fingerprint, &diagnostics);
    porpoise_diagnostics_free(&diagnostics);
    if (result != PORPOISE_EXIT_OK) return false;

    FunctionLocator locator;
    locator.target = target.target->id;
    locator.module = target.plan == nullptr
        ? ModuleForTarget(*target.target)
        : NullToEmpty(porpoise_plan_module(target.plan));
    locator.address = address;
    locator.size = size;
    locator.normalized_fingerprint = fingerprint;
    *locator_out = std::move(locator);
    return true;
}

std::vector<DataObjectRecord> WorkbenchModel::DataObjects(
    const PorpoiseRecoveryRunTarget &target) const {
    std::vector<DataObjectRecord> records;
    if (target.session == nullptr || target.target == nullptr ||
        target.target->id == nullptr) return records;
    const auto *program = porpoise_session_program(target.session);
    if (program == nullptr) return records;
    const std::string module = target.plan == nullptr
        ? ModuleForTarget(*target.target)
        : NullToEmpty(porpoise_plan_module(target.plan));
    for (std::size_t file_index = 0; file_index < program->file_count;
         ++file_index) {
        const auto &file = program->files[file_index];
        for (std::size_t object_index = 0;
             object_index < file.data_object_count; ++object_index) {
            const auto &object = file.data_objects[object_index];
            if (object.size == 0U) continue;
            DataObjectRecord record;
            record.locator.target = target.target->id;
            record.locator.module = module;
            record.locator.address = object.address;
            record.locator.size = object.size;
            record.name = NullToEmpty(object.name);
            record.translation_unit = file.relative_path != nullptr
                ? file.relative_path : NullToEmpty(file.path);
            record.section = NullToEmpty(object.section);
            records.push_back(std::move(record));
        }
    }
    return records;
}

bool WorkbenchModel::ApplyOverrides(const std::vector<OverrideEdit> &edits) {
    if (edits.empty() || State() == WorkerState::Running ||
        State() == WorkerState::Cancelling) return false;
    /* Reject the complete bulk edit before changing any project records. */
    for (const auto &edit : edits) {
        if (porpoise_recovery_project_find_target(
                &project_, edit.locator.target.c_str()) == nullptr ||
            edit.locator.size == 0 ||
            !LowercaseFingerprint(edit.locator.normalized_fingerprint) ||
            static_cast<unsigned int>(edit.action) >
                static_cast<unsigned int>(
                    PORPOISE_OVERRIDE_TREAT_AS_DATA) ||
            (edit.action == PORPOISE_OVERRIDE_IMPORT &&
             edit.contract_name.empty())) {
            AddLocalDiagnostic(
                PORPOISE_SEVERITY_ERROR,
                edit.action == PORPOISE_OVERRIDE_IMPORT &&
                        edit.contract_name.empty()
                    ? "an import override requires a contract name"
                    : "cannot apply an override with a stale or incomplete "
                      "function locator");
            return false;
        }
    }
    for (const auto &edit : edits) {
        auto *target = porpoise_recovery_project_find_target_mutable(
            &project_, edit.locator.target.c_str());
        if (target == nullptr) return false;
        std::size_t found = target->override_count;
        for (std::size_t index = 0; index < target->override_count; ++index) {
            const auto &candidate = target->overrides[index];
            if (candidate.address == edit.locator.address &&
                candidate.size == edit.locator.size &&
                NullToEmpty(candidate.module) == edit.locator.module &&
                NullToEmpty(candidate.normalized_fingerprint) ==
                    edit.locator.normalized_fingerprint) {
                found = index;
                break;
            }
        }
        if (edit.action == PORPOISE_OVERRIDE_AUTO) {
            if (found != target->override_count) {
                FreeOverride(target->overrides[found]);
                if (found + 1 < target->override_count)
                    std::memmove(&target->overrides[found],
                                 &target->overrides[found + 1],
                                 (target->override_count - found - 1) *
                                     sizeof(*target->overrides));
                --target->override_count;
            }
            continue;
        }
        PorpoiseRecoveryOverride replacement{};
        replacement.target = Duplicate(edit.locator.target);
        replacement.module = Duplicate(edit.locator.module);
        replacement.address = edit.locator.address;
        replacement.size = edit.locator.size;
        replacement.normalized_fingerprint =
            Duplicate(edit.locator.normalized_fingerprint);
        replacement.action = edit.action;
        replacement.contract_name =
            edit.action == PORPOISE_OVERRIDE_IMPORT
                ? Duplicate(edit.contract_name) : nullptr;
        replacement.acknowledge_conflict = edit.acknowledge_conflict;
        if (replacement.target == nullptr || replacement.module == nullptr ||
            replacement.normalized_fingerprint == nullptr ||
            (edit.action == PORPOISE_OVERRIDE_IMPORT &&
             (replacement.contract_name == nullptr ||
              edit.contract_name.empty()))) {
            FreeOverride(replacement);
            AddLocalDiagnostic(PORPOISE_SEVERITY_ERROR,
                               "an import override requires a contract name");
            return false;
        }
        if (found != target->override_count) {
            FreeOverride(target->overrides[found]);
            target->overrides[found] = replacement;
        } else {
            auto *grown = static_cast<PorpoiseRecoveryOverride *>(std::realloc(
                target->overrides,
                (target->override_count + 1) * sizeof(*target->overrides)));
            if (grown == nullptr) {
                FreeOverride(replacement);
                return false;
            }
            target->overrides = grown;
            target->overrides[target->override_count++] = replacement;
        }
    }
    dirty_ = true;
    std::vector<std::string> targets;
    for (const auto &edit : edits) {
        if (std::find(targets.begin(), targets.end(), edit.locator.target) !=
            targets.end()) {
            continue;
        }
        targets.push_back(edit.locator.target);
        if (FindRunTarget(edit.locator.target) == nullptr)
            targets.pop_back();
    }
    if (!targets.empty() && !StartReplan(targets)) {
        ClearRunResult();
        AddLocalDiagnostic(
            PORPOISE_SEVERITY_ERROR,
            "failed to start background replanning for the edited targets");
    }
    return true;
}

int WorkbenchModel::ReplanLoadedTarget(
    const std::string &target_id,
    const PorpoiseOperationCallbacks *operation) {
    auto *run_target = FindRunTarget(target_id);
    auto *target = porpoise_recovery_project_find_target_mutable(
        &project_, target_id.c_str());
    if (run_target == nullptr || run_target->session == nullptr ||
        target == nullptr) {
        return PORPOISE_EXIT_OK;
    }

    std::vector<PorpoiseFunctionOverride> overrides(target->override_count);
    for (std::size_t index = 0; index < target->override_count; ++index) {
        const auto &source = target->overrides[index];
        auto &destination = overrides[index];
        destination.module = source.module;
        destination.address = source.address;
        destination.size = source.size;
        destination.normalized_fingerprint = source.normalized_fingerprint;
        destination.action = source.action;
        destination.contract_name = source.contract_name;
        destination.acknowledge_conflict = source.acknowledge_conflict;
    }
    const std::string module = ModuleForTarget(*target);
    PorpoisePlanOptions options;
    porpoise_plan_options_init(&options);
    options.entry_symbol = target->entry;
    options.target_id = target->id;
    options.module = module.c_str();
    options.sdk_policy = target->sdk_policy;
    options.overrides = overrides.empty() ? nullptr : overrides.data();
    options.override_count = overrides.size();
    options.operation = operation;

    PorpoiseTranslationPlan *candidate = nullptr;
    int result = porpoise_plan_build(
        run_target->session, &options, &candidate, &diagnostics_);
    if (result != PORPOISE_EXIT_OK) return result;
    porpoise_plan_free(run_target->plan);
    run_target->plan = candidate;
    result = porpoise_plan_validate(candidate, &diagnostics_);
    if (result == PORPOISE_EXIT_OK) {
        result = porpoise_recovery_annotations_validate(
            porpoise_session_program(run_target->session),
            target->annotations, target->annotation_count,
            target->id, module.c_str(), &diagnostics_);
    }
    return result;
}

bool WorkbenchModel::RebindOverride(
    std::size_t target_index, std::size_t override_index,
    const std::string &module, std::uint32_t address, std::uint32_t size,
    const std::string &normalized_fingerprint) {
    if (target_index >= project_.target_count || size == 0 ||
        normalized_fingerprint.size() != PORPOISE_SHA256_HEX_SIZE - 1 ||
        State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    if (!LowercaseFingerprint(normalized_fingerprint)) return false;
    auto &target = project_.targets[target_index];
    if (override_index >= target.override_count) return false;
    char *module_copy = Duplicate(module);
    char *fingerprint_copy = Duplicate(normalized_fingerprint);
    if (module_copy == nullptr || fingerprint_copy == nullptr) {
        std::free(module_copy);
        std::free(fingerprint_copy);
        return false;
    }
    auto &value = target.overrides[override_index];
    std::free(value.module);
    std::free(value.normalized_fingerprint);
    value.module = module_copy;
    value.address = address;
    value.size = size;
    value.normalized_fingerprint = fingerprint_copy;
    dirty_ = true;
    const std::string target_id = NullToEmpty(target.id);
    if (FindRunTarget(target_id) != nullptr) {
        if (!StartReplan({target_id})) ClearRunResult();
    }
    return true;
}

bool WorkbenchModel::RemoveOverride(std::size_t target_index,
                                    std::size_t override_index) {
    if (target_index >= project_.target_count ||
        State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    auto &target = project_.targets[target_index];
    if (override_index >= target.override_count) return false;
    FreeOverride(target.overrides[override_index]);
    if (override_index + 1 < target.override_count) {
        std::memmove(&target.overrides[override_index],
                     &target.overrides[override_index + 1],
                     (target.override_count - override_index - 1) *
                         sizeof(*target.overrides));
    }
    --target.override_count;
    dirty_ = true;
    const std::string target_id = NullToEmpty(target.id);
    if (FindRunTarget(target_id) != nullptr) {
        if (!StartReplan({target_id})) ClearRunResult();
    }
    return true;
}

bool WorkbenchModel::UpsertAnnotation(
    const FunctionLocator &locator,
    PorpoiseRecoveryAnnotationInterpretation interpretation,
    std::uint32_t element_count, const std::string &encoding) {
    if (State() == WorkerState::Running || State() == WorkerState::Cancelling ||
        locator.size == 0 || element_count == 0) return false;
    auto *run_target = FindRunTarget(locator.target);
    auto *target = porpoise_recovery_project_find_target_mutable(
        &project_, locator.target.c_str());
    if (run_target == nullptr || run_target->session == nullptr ||
        target == nullptr) return false;

    PorpoiseRecoveryByteView bytes;
    porpoise_recovery_byte_view_init(&bytes);
    const auto *program = porpoise_session_program(run_target->session);
    if (porpoise_recovery_byte_view_extract(
            program, locator.address, locator.size, &bytes,
            &diagnostics_) != PORPOISE_EXIT_OK) return false;
    std::uint8_t digest[PORPOISE_SHA256_DIGEST_SIZE];
    char exact_hash[PORPOISE_SHA256_HEX_SIZE];
    porpoise_sha256(bytes.bytes, bytes.size, digest);
    porpoise_sha256_hex(digest, exact_hash);
    porpoise_recovery_byte_view_free(&bytes);

    const std::string fingerprint = locator.normalized_fingerprint.empty()
        ? std::string(exact_hash) : locator.normalized_fingerprint;
    const std::string canonical_encoding =
        AnnotationEncoding(interpretation, encoding);
    PorpoiseRecoveryAnnotation candidate{};
    candidate.target = Duplicate(locator.target);
    candidate.module = Duplicate(locator.module);
    candidate.address = locator.address;
    candidate.size = locator.size;
    candidate.normalized_fingerprint = Duplicate(fingerprint);
    candidate.exact_bytes_sha256 = Duplicate(exact_hash);
    candidate.interpretation = interpretation;
    candidate.element_count = element_count;
    candidate.encoding = canonical_encoding.empty()
        ? nullptr : Duplicate(canonical_encoding);
    if (candidate.target == nullptr || candidate.module == nullptr ||
        candidate.normalized_fingerprint == nullptr ||
        candidate.exact_bytes_sha256 == nullptr ||
        (!canonical_encoding.empty() && candidate.encoding == nullptr)) {
        FreeAnnotation(candidate);
        return false;
    }

    std::size_t replace_index = target->annotation_count;
    for (std::size_t index = 0; index < target->annotation_count; ++index) {
        const auto &existing = target->annotations[index];
        if (existing.address == locator.address &&
            existing.size == locator.size &&
            NullToEmpty(existing.module) == locator.module) {
            replace_index = index;
        } else if (RangesOverlap(existing.address, existing.size,
                                 locator.address, locator.size)) {
            FreeAnnotation(candidate);
            AddLocalDiagnostic(PORPOISE_SEVERITY_ERROR,
                               "the proposed data annotation overlaps an "
                               "existing annotation");
            return false;
        }
    }
    PorpoiseRecoveryAnnotationView view;
    porpoise_recovery_annotation_view_init(&view);
    const int validation = porpoise_recovery_annotation_view_open(
        program, &candidate, locator.target.c_str(), locator.module.c_str(),
        &view, &diagnostics_);
    porpoise_recovery_annotation_view_free(&view);
    if (validation != PORPOISE_EXIT_OK) {
        FreeAnnotation(candidate);
        return false;
    }
    if (replace_index != target->annotation_count) {
        FreeAnnotation(target->annotations[replace_index]);
        target->annotations[replace_index] = candidate;
    } else {
        auto *grown = static_cast<PorpoiseRecoveryAnnotation *>(std::realloc(
            target->annotations,
            (target->annotation_count + 1) * sizeof(*target->annotations)));
        if (grown == nullptr) {
            FreeAnnotation(candidate);
            return false;
        }
        target->annotations = grown;
        target->annotations[target->annotation_count++] = candidate;
    }
    dirty_ = true;
    if (!StartReplan({locator.target})) ClearRunResult();
    return true;
}

bool WorkbenchModel::RemoveAnnotation(const FunctionLocator &locator) {
    if (State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    auto *target = porpoise_recovery_project_find_target_mutable(
        &project_, locator.target.c_str());
    if (target == nullptr) return false;
    for (std::size_t index = 0; index < target->annotation_count; ++index) {
        const auto &candidate = target->annotations[index];
        if (candidate.address == locator.address &&
            candidate.size == locator.size &&
            NullToEmpty(candidate.module) == locator.module) {
            FreeAnnotation(target->annotations[index]);
            if (index + 1 < target->annotation_count)
                std::memmove(&target->annotations[index],
                             &target->annotations[index + 1],
                             (target->annotation_count - index - 1) *
                                 sizeof(*target->annotations));
            --target->annotation_count;
            dirty_ = true;
            if (FindRunTarget(locator.target) != nullptr &&
                !StartReplan({locator.target})) {
                ClearRunResult();
            }
            return true;
        }
    }
    return false;
}

bool WorkbenchModel::RemoveAnnotationAt(std::size_t target_index,
                                        std::size_t annotation_index) {
    if (target_index >= project_.target_count ||
        State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    auto &target = project_.targets[target_index];
    if (annotation_index >= target.annotation_count) return false;
    FreeAnnotation(target.annotations[annotation_index]);
    if (annotation_index + 1 < target.annotation_count) {
        std::memmove(&target.annotations[annotation_index],
                     &target.annotations[annotation_index + 1],
                     (target.annotation_count - annotation_index - 1) *
                         sizeof(*target.annotations));
    }
    --target.annotation_count;
    dirty_ = true;
    const std::string target_id = NullToEmpty(target.id);
    if (FindRunTarget(target_id) != nullptr &&
        !StartReplan({target_id})) {
        ClearRunResult();
    }
    return true;
}

const PorpoiseAbiManifest *WorkbenchModel::LoadedAbiManifest(
    std::size_t run_target_index) const {
    if (run_target_index >= run_result_.target_count ||
        run_result_.targets[run_target_index].session == nullptr ||
        State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return nullptr;
    return porpoise_session_abi(run_result_.targets[run_target_index].session);
}

DirectAbiDraft WorkbenchModel::DraftFromContract(
    const PorpoiseAbiFunction &value) {
    DirectAbiDraft draft;
    draft.kind = value.kind;
    draft.symbol = NullToEmpty(value.symbol);
    draft.wrapper = NullToEmpty(value.wrapper);
    draft.header = NullToEmpty(value.header);
    draft.result_type = value.result.type;
    draft.result_register_class = value.result.register_class;
    draft.result_register_index = value.result.register_index;
    for (std::size_t index = 0; index < value.argument_count; ++index) {
        const auto &source = value.arguments[index];
        AbiArgumentDraft argument;
        argument.name = NullToEmpty(source.name);
        argument.type = source.type;
        argument.register_class = source.register_class;
        argument.register_index = source.register_index;
        draft.arguments.push_back(std::move(argument));
    }
    return draft;
}

bool WorkbenchModel::LoadDirectAbiDraftRecovery(
    const std::string &path, std::vector<DirectAbiDraft> *functions_out) {
    if (path.empty() || functions_out == nullptr ||
        State() == WorkerState::Running || State() == WorkerState::Cancelling)
        return false;
    if (!ReadAbiDraftRecovery(path, functions_out)) {
        AddLocalDiagnostic(PORPOISE_SEVERITY_ERROR,
                           "failed to read the ABI draft recovery autosave");
        return false;
    }
    return true;
}

bool WorkbenchModel::WriteDirectAbiDraftRecovery(
    const std::string &path,
    const std::vector<DirectAbiDraft> &functions) {
    if (path.empty() || State() == WorkerState::Running ||
        State() == WorkerState::Cancelling) return false;
    std::error_code filesystem_error;
    const auto destination = std::filesystem::absolute(path, filesystem_error);
    if (filesystem_error) {
        AddLocalDiagnostic(PORPOISE_SEVERITY_ERROR,
                           "cannot resolve ABI draft recovery destination: " +
                               filesystem_error.message());
        return false;
    }
    std::filesystem::path stage;
    std::string publish_error;
    std::string publish_warning;
    if (!ReserveUniqueAdjacentFile(
            destination, "stage", &stage, &publish_error)) {
        AddLocalDiagnostic(PORPOISE_SEVERITY_ERROR,
                           "failed to reserve the ABI draft recovery stage: " +
                               publish_error);
        return false;
    }
    if (!WriteAbiDraftRecovery(stage, functions)) {
        std::error_code ignored;
        std::filesystem::remove(stage, ignored);
        AddLocalDiagnostic(PORPOISE_SEVERITY_ERROR,
                           "failed to write and synchronize the staged ABI "
                           "draft recovery autosave");
        return false;
    }
    if (!PublishAdjacentFileAtomically(
            stage, destination, &publish_error, &publish_warning)) {
        std::error_code ignored;
        std::filesystem::remove(stage, ignored);
        AddLocalDiagnostic(PORPOISE_SEVERITY_ERROR,
                           "failed to publish the ABI draft recovery autosave "
                           "without replacing the prior file: " + publish_error);
        return false;
    }
    if (!publish_warning.empty())
        AddLocalDiagnostic(PORPOISE_SEVERITY_WARNING, publish_warning);
    return true;
}

bool WorkbenchModel::WriteDirectAbiManifest(
    const std::string &path, const std::vector<DirectAbiDraft> &functions,
    bool add_to_project) {
    if (path.empty() || State() == WorkerState::Running ||
        State() == WorkerState::Cancelling) return false;
    ResetDiagnostics();
    std::error_code filesystem_error;
    const auto destination = std::filesystem::absolute(path, filesystem_error);
    if (filesystem_error) {
        AddLocalDiagnostic(PORPOISE_SEVERITY_ERROR,
                           "cannot resolve ABI manifest destination: " +
                               filesystem_error.message());
        return false;
    }
    std::filesystem::path stage;
    std::string publish_error;
    std::string publish_warning;
    if (!ReserveUniqueAdjacentFile(
            destination, "stage", &stage, &publish_error)) {
        AddLocalDiagnostic(PORPOISE_SEVERITY_ERROR,
                           "failed to reserve the ABI manifest stage: " +
                               publish_error);
        return false;
    }
    if (!WriteAbiJson(stage, functions)) {
        std::error_code ignored;
        std::filesystem::remove(stage, ignored);
        AddLocalDiagnostic(PORPOISE_SEVERITY_ERROR,
                           "failed to write and synchronize the staged ABI "
                           "manifest");
        return false;
    }
    PorpoiseAbiManifest validation;
    porpoise_abi_init(&validation);
    const int result = porpoise_abi_load(
        &validation, stage.string().c_str(), &diagnostics_);
    porpoise_abi_free(&validation);
    if (result != PORPOISE_EXIT_OK) {
        std::error_code ignored;
        std::filesystem::remove(stage, ignored);
        return false;
    }
    if (!PublishAdjacentFileAtomically(
            stage, destination, &publish_error, &publish_warning)) {
        std::error_code ignored;
        std::filesystem::remove(stage, ignored);
        AddLocalDiagnostic(PORPOISE_SEVERITY_ERROR,
                           "failed to publish the ABI manifest without "
                           "replacing the prior file: " + publish_error);
        return false;
    }
    if (!publish_warning.empty())
        AddLocalDiagnostic(PORPOISE_SEVERITY_WARNING, publish_warning);
    if (add_to_project) {
        const std::string normalized = GenericPath(destination);
        bool already_present = false;
        for (std::size_t index = 0; index < project_.abi_contract_count;
             ++index) {
            if (NullToEmpty(project_.abi_contracts[index].resolved) ==
                normalized) already_present = true;
        }
        if (!already_present && !AddSharedPath(true, normalized)) return false;
    }
    dirty_ = true;
    ClearRunResult();
    return true;
}

const char *WorkerStateName(WorkerState state) {
    switch (state) {
    case WorkerState::Idle: return "idle";
    case WorkerState::Running: return "running";
    case WorkerState::Cancelling: return "cancelling";
    case WorkerState::Succeeded: return "succeeded";
    case WorkerState::Failed: return "failed";
    case WorkerState::Cancelled: return "cancelled";
    }
    return "unknown";
}

}  // namespace porpoise::gui
