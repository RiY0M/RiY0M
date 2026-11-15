#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

namespace fc {

/// Raised when an operation is interrupted through its std::stop_token.
class OperationCancelled final : public std::runtime_error {
public:
    OperationCancelled() : std::runtime_error("Operation cancelled") {}
};

/// Raised by FileManager::copyFile when the source vanished (Python: NameError).
class SourceMissingError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// UTF-8 representation of a path, safe on every platform (path::string() may
/// throw on Windows for characters outside the ANSI code page).
[[nodiscard]] std::string toUtf8(const std::filesystem::path& path);

/// Ordering used for every sorted list of paths: comparison of the native
/// strings, which is what Python's sorted() does on os.path strings
/// (std::filesystem::path::operator< compares element by element instead).
[[nodiscard]] bool pathLess(const std::filesystem::path& left,
                            const std::filesystem::path& right) noexcept;

namespace FileManager {

/// All regular files of `folder`, as sorted paths relative to `folder`.
/// recursive == true : files of every sub-folder are included (symlinked
///                     folders are not followed, like os.walk).
/// recursive == false: only files directly inside `folder`.
/// Throws std::filesystem::filesystem_error if `folder` itself cannot be listed;
/// unreadable sub-folders are skipped.
[[nodiscard]] std::vector<std::filesystem::path> getFiles(const std::filesystem::path& folder,
                                                          bool recursive);

/// True if both files have the same size and, when `deepComparison` is set,
/// the same bytes. Throws std::filesystem::filesystem_error / std::runtime_error
/// on I/O errors and OperationCancelled when `stopToken` is triggered.
[[nodiscard]] bool filesAreIdentical(const std::filesystem::path& fileA,
                                     const std::filesystem::path& fileB,
                                     bool deepComparison = true,
                                     const std::stop_token& stopToken = {});

/// Copy `source` over `destination`, creating missing parent folders and
/// preserving the modification time (Python: shutil.copy2). The copy goes
/// through a temporary file so an interrupted copy never leaves a truncated
/// destination behind.
void copyFile(const std::filesystem::path& source, const std::filesystem::path& destination);

/// Human readable size, identical to the Python output ("1.5 KB", "117.7 MB").
[[nodiscard]] std::string formatSize(std::uintmax_t size);

struct FileInfo {
    std::uintmax_t size = 0;
    std::string sizeText;
    std::chrono::system_clock::time_point modified;
    std::string modifiedText; ///< local time, "%Y-%m-%d %H:%M:%S"
};

[[nodiscard]] FileInfo getFileInfo(const std::filesystem::path& file);

/// Local time formatted as "%Y-%m-%d %H:%M:%S" (seconds are truncated).
[[nodiscard]] std::string formatLocalTime(std::chrono::system_clock::time_point time);

} // namespace FileManager
} // namespace fc
