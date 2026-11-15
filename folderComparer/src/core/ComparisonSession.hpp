#pragma once

// UI independent state of the application: the last comparison, the expanded
// folders, the actions applied (and how to undo them), and the rows to display
// for a filter. The GUI only renders what this class describes.
//
// Rows:
//  - Different files are always listed one by one.
//  - A folder that exists on one side only is a single row (with its content
//    counted); expanding it lists its direct content as sub-rows, recursively.
//
// Actions (copy / delete / skip) are applied to a row, i.e. to a file or a whole
// folder. A row with an action shows it with an Undo button; the content of an
// acted-on folder shows the same action, without buttons. To keep every
// action independently undoable, a row cannot be acted on while one of its
// parent folders or one of its descendants has an action.

#include "core/ActionHistory.hpp"
#include "core/ComparisonResult.hpp"
#include "core/FileManager.hpp"
#include "core/LiveCounts.hpp"
#include "core/Presentation.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fc {

enum class EntryKind : std::uint8_t {
    File,
    Folder,
};

/// One entry of a menu button.
struct RowChoice {
    std::string label;
    RowCommand command = RowCommand::Undo;

    friend bool operator==(const RowChoice&, const RowChoice&) = default;
};

/// A row button: a plain button runs `command`; a menu button (non-empty
/// `choices`) lets the user pick the precise command, and its `command` only
/// tells the kind of commands it offers (and so its color).
struct RowButton {
    std::string label;
    RowCommand command = RowCommand::Undo;
    std::vector<RowChoice> choices;

    /// Every command the button can run.
    [[nodiscard]] std::vector<RowCommand> commands() const;

    friend bool operator==(const RowButton&, const RowButton&) = default;
};

/// Where an entry currently exists on disk.
struct Location {
    Side side = Side::A;
    std::filesystem::path path; ///< absolute
};

/// Everything needed to draw one row.
struct Row {
    std::filesystem::path path;        ///< relative path, the row identifier
    std::filesystem::path displayName; ///< full path at depth 0, name below; folders end with a separator
    EntryKind kind = EntryKind::File;
    FileStatus status = FileStatus::Different;
    std::size_t depth = 0;
    bool expandable = false;
    bool expanded = false;
    std::optional<RowCommand> action; ///< applied to this row or (inherited) to a parent folder
    bool inherited = false;
    std::string statusText; ///< "DIFFERENT", "ONLY A", "COPIED → B", "DELETED A"...
    std::string detail;     ///< folder content, "D(2) F(10)"
    std::string extension;  ///< files: upper-case extension without the dot ("JPG"), may be empty
    Style style;
    std::vector<RowButton> buttons; ///< left to right

    friend bool operator==(const Row&, const Row&) = default;
};

class ComparisonSession {
public:
    /// Store a new comparison. The actions of the previous one are discarded:
    /// they become permanent (see hasUndoableBackups()).
    void setResult(std::filesystem::path folderA, std::filesystem::path folderB, ComparisonResult result);
    void reset();

    [[nodiscard]] bool hasResult() const noexcept { return hasResult_; }
    [[nodiscard]] const ComparisonResult& result() const noexcept { return result_; }
    [[nodiscard]] const std::filesystem::path& folderA() const noexcept { return folderA_; }
    [[nodiscard]] const std::filesystem::path& folderB() const noexcept { return folderB_; }

    /// Counts shown by a summary item. They follow the copies and deletions
    /// applied by the application (see LiveCounts; changes made outside the
    /// application are not tracked). Skipped counts the skipped rows and
    /// everything inside the skipped folders.
    [[nodiscard]] EntryCounts summaryCounts(SummaryItem item) const;

    /// Rows to display for `filter`, in display order: Different, then Only A,
    /// then Only B for "All differences"; for "Skipped", every skipped row (full
    /// path, sorted); expanded folders followed by their content.
    [[nodiscard]] std::vector<Row> rows(Filter filter) const;

    /// Row of a listed entry (any depth), or nullopt.
    [[nodiscard]] std::optional<Row> row(const std::filesystem::path& relativePath) const;

    [[nodiscard]] std::optional<FileStatus> statusOf(const std::filesystem::path& relativePath) const;
    /// Action applied to this very entry (not inherited).
    [[nodiscard]] std::optional<RowCommand> actionOf(const std::filesystem::path& relativePath) const;

    /// The sides where the entry exists right now (A first), checked on disk:
    /// used to open it or show it in the file manager.
    [[nodiscard]] std::vector<Location> existingLocations(const std::filesystem::path& relativePath) const;

    /// Show / hide the content of a folder row. Throws std::invalid_argument
    /// for anything else.
    void toggleExpanded(const std::filesystem::path& relativePath);
    [[nodiscard]] bool isExpanded(const std::filesystem::path& relativePath) const;

    /// Run a row button. Throws std::invalid_argument for an unlisted path,
    /// std::logic_error when the command is not offered for the row, and the
    /// file system errors of the action (nothing is recorded then).
    void apply(const std::filesystem::path& relativePath, RowCommand command);

    /// Undo the most recent action still applied. Returns false if there is none.
    bool undoLast();
    [[nodiscard]] bool canUndo() const noexcept { return !actions_.empty(); }

    /// Apply again the most recently undone action (row Undo or undoLast).
    /// Returns false if there is none. A new action empties the redo list.
    /// On failure (the files changed meanwhile) the action stays redoable.
    bool redoLast();
    [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] std::size_t actionCount() const noexcept { return actions_.size(); }

    /// True when some deleted / overwritten data can only be recovered by an
    /// undo: setResult(), reset() and the destructor would make it permanent.
    [[nodiscard]] bool hasUndoableBackups() const noexcept { return history_.holdsBackups(); }

    /// Commands offered for an entry of the given category without action.
    [[nodiscard]] static std::vector<RowButton> buttonsFor(FileStatus status);

private:
    struct Entry {
        FileStatus status = FileStatus::Different;
        EntryKind kind = EntryKind::File;
    };

    struct AppliedAction {
        RowCommand command = RowCommand::Undo;
        std::uint64_t sequence = 0;                  ///< grows: the most recent action is the largest
        std::vector<ActionHistory::Id> historyIds; ///< file system changes to revert, in order (none for Skip)
    };

    void indexSide(FileStatus status);
    [[nodiscard]] Row makeRow(const std::filesystem::path& path, const Entry& entry, std::size_t depth) const;
    [[nodiscard]] std::optional<RowCommand> parentAction(const std::filesystem::path& path) const;
    [[nodiscard]] bool hasDescendantAction(const std::filesystem::path& path) const;
    [[nodiscard]] const Entry& entryOf(const std::filesystem::path& path) const;
    void applyAction(const std::filesystem::path& path, RowCommand command);
    void undo(const std::filesystem::path& path);
    void refreshCounts();

    std::filesystem::path folderA_;
    std::filesystem::path folderB_;
    ComparisonResult result_;
    bool hasResult_ = false;

    std::map<std::filesystem::path, Entry, PathLess> entries_;
    std::map<FileStatus, std::vector<std::filesystem::path>> topLevel_;
    std::map<std::filesystem::path, std::vector<std::filesystem::path>, PathLess> children_;
    std::map<std::filesystem::path, EntryCounts, PathLess> folderContent_;
    std::set<std::filesystem::path, PathLess> expanded_;
    std::map<std::filesystem::path, AppliedAction, PathLess> actions_;
    std::uint64_t nextSequence_ = 1;
    std::vector<std::pair<std::filesystem::path, RowCommand>> redo_; ///< most recently undone last
    LiveCounts liveCounts_;
    CategoryCounts counts_;

    ActionHistory history_; // last: its destructor empties the trash
};

} // namespace fc
