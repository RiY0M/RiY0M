#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fc {

/// Category of a relative path after comparing folder A with folder B.
enum class FileStatus : std::uint8_t {
    Identical,
    Different,
    OnlyA,
    OnlyB,
};

/// A relative path tagged with its category.
/// (Fixes the Python TODO: merged lists used to lose the category of each file.)
struct FileEntry {
    std::filesystem::path path;
    FileStatus status = FileStatus::Identical;

    friend bool operator==(const FileEntry&, const FileEntry&) = default;
};

/// A file present on both sides that could not be compared (I/O error).
/// Such files are also listed in `different`, since they are not proven identical.
struct ComparisonError {
    std::filesystem::path path;
    std::string message;
};

/// Outcome of FolderManager::compare. Every list is sorted and holds paths
/// relative to the compared folders.
struct ComparisonResult {
    std::vector<std::filesystem::path> identical;
    std::vector<std::filesystem::path> different;
    std::vector<std::filesystem::path> onlyA;
    std::vector<std::filesystem::path> onlyB;
    std::vector<ComparisonError> errors;

    [[nodiscard]] std::size_t identicalCount() const noexcept { return identical.size(); }
    [[nodiscard]] std::size_t differentCount() const noexcept { return different.size(); }
    [[nodiscard]] std::size_t onlyACount() const noexcept { return onlyA.size(); }
    [[nodiscard]] std::size_t onlyBCount() const noexcept { return onlyB.size(); }

    [[nodiscard]] std::size_t allDifferencesCount() const noexcept
    {
        return differentCount() + onlyACount() + onlyBCount();
    }

    [[nodiscard]] std::size_t totalCount() const noexcept
    {
        return identicalCount() + allDifferencesCount();
    }

    /// The list holding the files of the given category.
    [[nodiscard]] const std::vector<std::filesystem::path>& files(FileStatus status) const noexcept;

    /// Files of one category, tagged.
    [[nodiscard]] std::vector<FileEntry> entries(FileStatus status) const;

    /// Different, then Only A, then Only B (same order as the Python version), tagged.
    [[nodiscard]] std::vector<FileEntry> allDifferences() const;
};

} // namespace fc
