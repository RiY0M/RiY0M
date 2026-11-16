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

enum class SortOrder : std::uint8_t {
    Ascending,
    Descending,
};

/// State of the "select all" box of a view.
enum class SelectionState : std::uint8_t {
    None,
    Some,
    All,
};

/// An action of a mass action that could not be applied.
struct ActionFailure {
    std::filesystem::path path;
    std::string message;
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
    bool selectable = false;        ///< has a check box: it offers at least one command
    bool selected = false;

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

    /// Rows to display for `filter` (Skipped: every skipped row, full path),
    /// sorted by name ignoring the case (caseInsensitiveLess) in the sort order,
    /// restricted by the type filter; expanded folders are followed by their
    /// content, sub-folders first.
    [[nodiscard]] std::vector<Row> rows(Filter filter) const;

    /// Kept across comparisons (a display preference).
    void setSortOrder(SortOrder order) noexcept { sortOrder_ = order; }
    [[nodiscard]] SortOrder sortOrder() const noexcept { return sortOrder_; }

    /// File types (extensionLabel, "" without extension) of the files of the
    /// view `filter` (Skipped: the files of the skipped rows, folder content
    /// included), with their number of files. Ignores the type filter.
    [[nodiscard]] std::map<std::string, std::size_t> fileTypes(Filter filter) const;

    /// Show only the files of `types` (nullopt: every type). Folders stay, but
    /// only those holding at least one matching file somewhere inside; their
    /// "D() F()" then counts what matches. The selection is cleared (it could
    /// hold hidden rows). Reset by a new comparison.
    void setTypeFilter(std::optional<std::set<std::string>> types);
    [[nodiscard]] const std::optional<std::set<std::string>>& typeFilter() const noexcept { return typeFilter_; }

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
    /// With a type filter, an action on a folder row applies to the matching
    /// files inside it only (one action for undo / redo); if some of them fail,
    /// the others are applied and std::runtime_error lists the failures.
    void apply(const std::filesystem::path& relativePath, RowCommand command);

    /// Undo the most recent action still applied, or every action of the most
    /// recent mass action. Returns false if there is none.
    bool undoLast();
    [[nodiscard]] bool canUndo() const noexcept { return !actions_.empty(); }

    /// Apply again what was undone most recently (a row Undo, an undoLast, a mass
    /// Undo), as one action. Returns false if there is none. A new action empties
    /// the redo list. On failure (the files changed meanwhile) what could not be
    /// applied stays redoable.
    bool redoLast();
    [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] std::size_t actionCount() const noexcept { return actions_.size(); }

    /// True when some deleted / overwritten data can only be recovered by an
    /// undo: setResult(), reset() and the destructor would make it permanent.
    [[nodiscard]] bool hasUndoableBackups() const noexcept { return history_.holdsBackups(); }

    /// Commands offered for an entry of the given category without action.
    [[nodiscard]] static std::vector<RowButton> buttonsFor(FileStatus status);

    // ----- Selection and mass actions -----------------------------------------
    // Only rows offering at least one command can be selected. The selection
    // follows the actions: a row that no longer offers anything (content of an
    // acted-on folder...) leaves it.

    /// Like a tree of check boxes: selecting a folder selects its shown content,
    /// unselecting a row unselects its content and every folder above it, and a
    /// folder whose shown content is entirely selected gets selected.
    void setSelected(const std::filesystem::path& relativePath, bool selected);
    /// Select / unselect every selectable row shown for `filter`.
    void setAllSelected(Filter filter, bool selected);
    void clearSelection() noexcept { selection_.clear(); }
    [[nodiscard]] bool isSelected(const std::filesystem::path& relativePath) const;
    [[nodiscard]] std::size_t selectionCount() const noexcept { return selection_.size(); }
    /// What the selection covers: a selected folder counts with its content
    /// (with a type filter: its matching content, what the actions touch), and
    /// the missing folders above selected rows count as folders, selected or not.
    [[nodiscard]] EntryCounts selectionCounts() const;
    /// For the "select all" box of `filter`: how many of its selectable rows are selected.
    [[nodiscard]] SelectionState selectionState(Filter filter) const;

    /// Commands offered by every selected row, in RowCommand order (empty
    /// without selection).
    [[nodiscard]] std::vector<RowCommand> commonCommands() const;

    /// The mass action buttons for `commands`, grouped like the row buttons:
    /// several copies make one "Copy" menu, several deletions one "Delete" menu,
    /// so there are at most a Copy, a Delete and a Skip (or Undo) button.
    [[nodiscard]] static std::vector<RowButton> massButtons(const std::vector<RowCommand>& commands);

    /// Run `command` on every selected row, as a single action for undo / redo.
    /// A row whose parent folder is also selected is covered by the folder's
    /// action. Rows that fail are reported, the others are applied. Throws
    /// std::logic_error if `command` is not common to the whole selection.
    std::vector<ActionFailure> applyToSelection(RowCommand command);

private:
    struct Entry {
        FileStatus status = FileStatus::Different;
        EntryKind kind = EntryKind::File;
    };

    struct AppliedAction {
        RowCommand command = RowCommand::Undo;
        std::uint64_t sequence = 0;                  ///< grows: the most recent action is the largest
        std::uint64_t group = 0;                     ///< actions applied together (mass action) share it
        std::vector<ActionHistory::Id> historyIds; ///< file system changes to revert, in order (none for Skip)
    };

    void indexSide(FileStatus status);
    [[nodiscard]] Row makeRow(const std::filesystem::path& path, const Entry& entry, std::size_t depth) const;
    [[nodiscard]] std::optional<RowCommand> parentAction(const std::filesystem::path& path) const;
    [[nodiscard]] bool hasDescendantAction(const std::filesystem::path& path) const;
    [[nodiscard]] const Entry& entryOf(const std::filesystem::path& path) const;
    using Redo = std::vector<std::pair<std::filesystem::path, RowCommand>>;

    void applyAction(const std::filesystem::path& path, RowCommand command, std::uint64_t group);
    /// Reverts the action of `path`; returns its command.
    RowCommand undoAction(const std::filesystem::path& path);
    void pruneSelection();
    void applyToTargets(const std::vector<std::filesystem::path>& targets, RowCommand command);
    /// Buttons / state of a folder row under a type filter, from its matching files.
    void applyFilteredFolderState(const std::filesystem::path& path, const Entry& entry, Row& row) const;
    [[nodiscard]] bool hasDescendantFolderAction(const std::filesystem::path& path) const;
    /// What an Undo on `path` reverts: its own action, or for a filtered folder its files' actions.
    [[nodiscard]] std::vector<std::filesystem::path> undoTargets(const std::filesystem::path& path) const;
    /// What an action on `path` applies to: the row itself, or with a type
    /// filter and a folder row, the matching files inside it.
    [[nodiscard]] std::vector<std::filesystem::path> actionTargets(const std::filesystem::path& path) const;
    [[nodiscard]] bool isShown(const std::filesystem::path& path, EntryKind kind) const;
    /// Content of a folder as displayed: everything, or what matches the type filter.
    [[nodiscard]] const EntryCounts& shownContent(const std::filesystem::path& folder) const;
    void refreshFilteredContent();
    [[nodiscard]] bool rowBefore(const std::filesystem::path& left, const std::filesystem::path& right) const;
    [[nodiscard]] std::vector<const std::filesystem::path*> sortedChildren(const std::filesystem::path& folder) const;
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
    std::uint64_t nextGroup_ = 1;
    std::vector<Redo> redo_; ///< groups of undone actions, most recently undone last
    std::set<std::filesystem::path, PathLess> selection_;
    SortOrder sortOrder_ = SortOrder::Ascending;
    std::optional<std::set<std::string>> typeFilter_;
    /// With a type filter: the folders holding matching files, with the matching content.
    std::map<std::filesystem::path, EntryCounts, PathLess> filteredContent_;
    LiveCounts liveCounts_;
    CategoryCounts counts_;

    ActionHistory history_; // last: its destructor empties the trash
};

} // namespace fc
