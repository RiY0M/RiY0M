#pragma once

// UI independent state of the application: the last comparison, the actions
// applied to its files, and the rows to display for a given filter. The GUI
// only renders what this class describes, so the behavior is unit tested.

#include "core/ComparisonResult.hpp"
#include "core/Presentation.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fc {

struct RowButton {
    std::string label;
    CopyDirection direction = CopyDirection::AToB;

    friend bool operator==(const RowButton&, const RowButton&) = default;
};

/// Everything needed to draw one file row.
struct Row {
    std::filesystem::path path;
    FileStatus status = FileStatus::Different;
    std::optional<FileAction> action;
    std::string statusText; ///< "DIFFERENT", "ONLY A", "COPIED"...
    Style style;
    std::vector<RowButton> buttons; ///< left to right; empty once an action was applied
};

/// Ordered by native string, like every list of the application.
struct PathLess {
    bool operator()(const std::filesystem::path& left, const std::filesystem::path& right) const noexcept;
};

class ComparisonSession {
public:
    /// Store a new comparison and forget every action of the previous one
    /// (the Python version kept stale filter caches between comparisons).
    void setResult(std::filesystem::path folderA, std::filesystem::path folderB,
                   ComparisonResult result);
    void reset();

    [[nodiscard]] bool hasResult() const noexcept { return hasResult_; }
    [[nodiscard]] const ComparisonResult& result() const noexcept { return result_; }
    [[nodiscard]] const std::filesystem::path& folderA() const noexcept { return folderA_; }
    [[nodiscard]] const std::filesystem::path& folderB() const noexcept { return folderB_; }

    /// Rows to display for `filter`, in display order. "All" lists Different,
    /// then Only A, then Only B, each row keeping its own category.
    [[nodiscard]] std::vector<Row> rows(Filter filter) const;

    /// Row of a single file, or nullopt if the path is not a difference.
    [[nodiscard]] std::optional<Row> row(const std::filesystem::path& relativePath) const;

    [[nodiscard]] std::optional<FileStatus> statusOf(const std::filesystem::path& relativePath) const;
    [[nodiscard]] std::optional<FileAction> actionOf(const std::filesystem::path& relativePath) const;

    /// Copy a listed file in the given direction and remember the action.
    /// Throws std::invalid_argument if the path is not a difference of the
    /// current comparison, std::logic_error if the direction is not allowed for
    /// it or an action was already applied, and the FileManager::copyFile
    /// exceptions on I/O failure (in which case nothing is recorded).
    void copy(const std::filesystem::path& relativePath, CopyDirection direction);

    /// Buttons offered for a file of the given category (no action applied).
    [[nodiscard]] static std::vector<RowButton> buttonsFor(FileStatus status);

private:
    [[nodiscard]] Row makeRow(const FileEntry& entry) const;

    std::filesystem::path folderA_;
    std::filesystem::path folderB_;
    ComparisonResult result_;
    bool hasResult_ = false;
    std::map<std::filesystem::path, FileStatus, PathLess> statuses_;
    std::map<std::filesystem::path, FileAction, PathLess> actions_;
};

} // namespace fc
