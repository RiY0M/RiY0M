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

/// Number of files and folders of a category.
struct EntryCounts {
    std::size_t files = 0;
    std::size_t folders = 0;

    /// E = F + D
    [[nodiscard]] std::size_t entries() const noexcept { return files + folders; }

    EntryCounts& operator+=(const EntryCounts& other) noexcept
    {
        files += other.files;
        folders += other.folders;
        return *this;
    }

    friend bool operator==(const EntryCounts&, const EntryCounts&) = default;
};

/// Outcome of FolderManager::compare. Every list is sorted (fc::pathLess) and
/// holds paths relative to the compared folders.
///
/// Every file and every folder of A ∪ B has exactly one category:
///  - a file is identical / different when present on both sides, otherwise only A / only B;
///  - a folder is only A / only B when it exists on one side only; a folder of
///    both sides is "different" when anything inside it differs, "identical" otherwise.
/// Folders are only listed by recursive comparisons.
struct ComparisonResult {
    std::vector<std::filesystem::path> identical;
    std::vector<std::filesystem::path> different;
    std::vector<std::filesystem::path> onlyA;
    std::vector<std::filesystem::path> onlyB;

    std::vector<std::filesystem::path> identicalFolders;
    std::vector<std::filesystem::path> differentFolders;
    std::vector<std::filesystem::path> onlyAFolders;
    std::vector<std::filesystem::path> onlyBFolders;

    std::vector<ComparisonError> errors;

    [[nodiscard]] std::size_t identicalCount() const noexcept { return identical.size(); }
    [[nodiscard]] std::size_t differentCount() const noexcept { return different.size(); }
    [[nodiscard]] std::size_t onlyACount() const noexcept { return onlyA.size(); }
    [[nodiscard]] std::size_t onlyBCount() const noexcept { return onlyB.size(); }

    /// Number of files that differ (different + only A + only B).
    [[nodiscard]] std::size_t allDifferencesCount() const noexcept
    {
        return differentCount() + onlyACount() + onlyBCount();
    }

    /// Number of files.
    [[nodiscard]] std::size_t totalCount() const noexcept { return identicalCount() + allDifferencesCount(); }

    /// The files of the given category.
    [[nodiscard]] const std::vector<std::filesystem::path>& files(FileStatus status) const noexcept;

    /// The folders of the given category.
    [[nodiscard]] const std::vector<std::filesystem::path>& folders(FileStatus status) const noexcept;

    /// Files and folders of a category.
    [[nodiscard]] EntryCounts counts(FileStatus status) const noexcept;
    /// Different + only A + only B.
    [[nodiscard]] EntryCounts allDifferencesCounts() const noexcept;
    /// Every file and folder.
    [[nodiscard]] EntryCounts totalCounts() const noexcept;

    /// Files of one category, tagged.
    [[nodiscard]] std::vector<FileEntry> entries(FileStatus status) const;

    /// Different, then Only A, then Only B files, tagged.
    [[nodiscard]] std::vector<FileEntry> allDifferences() const;
};

} // namespace fc
