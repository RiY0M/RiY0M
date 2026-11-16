#pragma once

#include "core/ScanControl.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fc {


/// Raised when the source of a copy vanished since the comparison.
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

/// fc::pathLess as a comparator, for std::map / std::set.
struct PathLess {
    bool operator()(const std::filesystem::path& left, const std::filesystem::path& right) const noexcept
    {
        return pathLess(left, right);
    }
};

/// Hidden folder created at the root of a compared folder to keep what was
/// deleted or overwritten, so that actions can be undone. Never listed.
inline constexpr std::string_view kTrashFolderName = ".folderComparer-trash";

/// Files and folders of a folder, relative to it, each list sorted with pathLess.
struct FolderListing {
    std::vector<std::filesystem::path> files;
    std::vector<std::filesystem::path> folders;
};

namespace FileManager {

/// Regular files and folders of `folder`, relative to it.
/// recursive == true : every sub-folder is walked and listed (symlinked folders
///                     are neither listed nor followed, like os.walk).
/// recursive == false: only the files directly inside `folder`, no folder.
/// The trash folder (kTrashFolderName) at the root is ignored.
/// Throws std::filesystem::filesystem_error if `folder` itself cannot be listed;
/// unreadable sub-folders are skipped. `control` is checked for each folder.
[[nodiscard]] FolderListing listEntries(const std::filesystem::path& folder, bool recursive,
                                        const ScanControl& control = {});

/// listEntries(folder, recursive).files
[[nodiscard]] std::vector<std::filesystem::path> getFiles(const std::filesystem::path& folder,
                                                          bool recursive);

/// True if both files have the same size and, when `deepComparison` is set,
/// the same bytes. Throws std::filesystem::filesystem_error / std::runtime_error
/// on I/O errors; `control` is checked for each 1 MiB chunk.
[[nodiscard]] bool filesAreIdentical(const std::filesystem::path& fileA,
                                     const std::filesystem::path& fileB,
                                     bool deepComparison = true,
                                     const ScanControl& control = {});

/// Copy a file, or a folder with everything inside it, to `destination`, which
/// must not exist (its parent must). File modification times are preserved.
/// Symlinked folders inside a copied folder are skipped, like when listing.
void copyEntry(const std::filesystem::path& source, const std::filesystem::path& destination);

/// Move a file or a folder, creating the missing parents of `destination`.
/// Falls back to copy + remove when both paths are on different volumes.
void moveEntry(const std::filesystem::path& source, const std::filesystem::path& destination);

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
