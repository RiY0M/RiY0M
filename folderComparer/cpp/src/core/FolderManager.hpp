#pragma once

#include "core/ComparisonResult.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <variant>

namespace fc {

struct CompareOptions {
    /// true : files are compared by name, size and content (more accurate).
    /// false: files are compared by name and size (faster).
    bool deepComparison = false;
    /// true : files of every sub-folder are included.
    bool recursive = true;
};

/// Called before each common file is compared: (current, total, relativePath),
/// `current` going from 1 to `total`. Invoked on the comparing thread.
using ProgressCallback =
    std::function<void(std::size_t current, std::size_t total, const std::filesystem::path& path)>;

/// User facing error produced by FolderManager::validateFolders.
struct FolderValidationError {
    std::string title;
    std::string message;
};

/// Two existing, distinct, normalized folders ready to be compared.
struct ValidatedFolders {
    std::filesystem::path folderA;
    std::filesystem::path folderB;
};

using FolderValidation = std::variant<ValidatedFolders, FolderValidationError>;

namespace FolderManager {

/// Compare two folders. Throws OperationCancelled when `stopToken` is
/// triggered and std::filesystem::filesystem_error when a folder cannot be
/// listed. Files that cannot be read are reported as different and listed in
/// ComparisonResult::errors.
[[nodiscard]] ComparisonResult compare(const std::filesystem::path& folderA,
                                       const std::filesystem::path& folderB,
                                       const CompareOptions& options,
                                       const ProgressCallback& progress = {},
                                       const std::stop_token& stopToken = {});

/// Absolute, lexically normalized path without trailing separator and with
/// native separators (Python: normcase(normpath(abspath(path))), minus the
/// lower-casing which only matters for display on Windows).
[[nodiscard]] std::filesystem::path normalizeFolderPath(const std::filesystem::path& folderPath);

/// False for an empty path or anything that is not an existing folder.
[[nodiscard]] bool isFolderPathValid(const std::filesystem::path& folderPath);

/// True when both paths designate the same existing folder. Never throws:
/// a missing folder is simply not the same folder.
[[nodiscard]] bool checkSameFolders(const std::filesystem::path& folderA,
                                    const std::filesystem::path& folderB);

/// Leading and trailing whitespace removed (Python: str.strip()).
[[nodiscard]] std::filesystem::path trimmed(const std::filesystem::path& path);

/// Checks done by the UI before comparing, with the Python error messages.
/// Unlike the Python version, the first error stops the validation.
[[nodiscard]] FolderValidation validateFolders(const std::filesystem::path& folderA,
                                               const std::filesystem::path& folderB);

} // namespace FolderManager
} // namespace fc
