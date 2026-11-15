#include "core/ComparisonSession.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace fc {

void ComparisonSession::setResult(fs::path folderA, fs::path folderB, ComparisonResult result)
{
    reset();
    folderA_ = std::move(folderA);
    folderB_ = std::move(folderB);
    result_ = std::move(result);
    hasResult_ = true;

    for (const fs::path& path : result_.different) {
        entries_.insert_or_assign(path, Entry{FileStatus::Different, EntryKind::File});
    }
    topLevel_[FileStatus::Different] = result_.different;
    indexSide(FileStatus::OnlyA);
    indexSide(FileStatus::OnlyB);

    liveCounts_.reset(result_);
    refreshCounts();
}

void ComparisonSession::refreshCounts()
{
    std::vector<LiveCounts::Action> applied;
    applied.reserve(actions_.size());
    for (const auto& [path, action] : actions_) {
        applied.emplace_back(path, action.command);
    }
    counts_ = liveCounts_.compute(applied);
}

void ComparisonSession::indexSide(FileStatus status)
{
    const std::vector<fs::path>& files = result_.files(status);
    const std::vector<fs::path>& folders = result_.folders(status);
    const std::set<fs::path, PathLess> folderSet(folders.begin(), folders.end());
    std::vector<fs::path>& topLevel = topLevel_[status];

    const auto add = [&](const fs::path& path, EntryKind kind) {
        entries_.insert_or_assign(path, Entry{status, kind});
        if (const fs::path parent = path.parent_path(); folderSet.contains(parent)) {
            children_[parent].push_back(path);
        }
        else {
            topLevel.push_back(path); // the topmost missing folder, or a file in a common folder
        }
    };

    for (const fs::path& folder : folders) {
        add(folder, EntryKind::Folder);
        folderContent_[folder]; // empty folders too
    }
    for (const fs::path& file : files) {
        add(file, EntryKind::File);
    }

    // Content counts of every folder, its sub-folders included.
    for (const fs::path& folder : folders) {
        for (fs::path parent = folder.parent_path(); folderSet.contains(parent); parent = parent.parent_path()) {
            ++folderContent_[parent].folders;
        }
    }
    for (const fs::path& file : files) {
        for (fs::path parent = file.parent_path(); folderSet.contains(parent); parent = parent.parent_path()) {
            ++folderContent_[parent].files;
        }
    }

    std::ranges::sort(topLevel, pathLess);
    for (const fs::path& folder : folders) {
        const auto found = children_.find(folder);
        if (found == children_.end()) {
            continue;
        }
        // Folders first, then files, like a file explorer.
        std::ranges::sort(found->second, [this](const fs::path& left, const fs::path& right) {
            const bool leftIsFolder = entries_.at(left).kind == EntryKind::Folder;
            const bool rightIsFolder = entries_.at(right).kind == EntryKind::Folder;
            if (leftIsFolder != rightIsFolder) {
                return leftIsFolder;
            }
            return pathLess(left, right);
        });
    }
}

void ComparisonSession::reset()
{
    history_.discard();
    folderA_.clear();
    folderB_.clear();
    result_ = ComparisonResult{};
    hasResult_ = false;
    entries_.clear();
    topLevel_.clear();
    children_.clear();
    folderContent_.clear();
    expanded_.clear();
    actions_.clear();
    redo_.clear();
    nextSequence_ = 1;
    liveCounts_.reset(result_);
    counts_ = CategoryCounts{};
}

std::vector<RowButton> ComparisonSession::buttonsFor(FileStatus status)
{
    const auto button = [](RowCommand command) {
        return RowButton{std::string(commandLabel(command)), command};
    };

    switch (status) {
    case FileStatus::Different:
        return {button(RowCommand::CopyBToA), button(RowCommand::CopyAToB), button(RowCommand::DeleteA),
                button(RowCommand::DeleteB), button(RowCommand::DeleteBoth), button(RowCommand::Skip)};
    case FileStatus::OnlyA:
        return {RowButton{"Copy → B", RowCommand::CopyAToB}, button(RowCommand::DeleteA),
                button(RowCommand::Skip)};
    case FileStatus::OnlyB:
        return {RowButton{"Copy → A", RowCommand::CopyBToA}, button(RowCommand::DeleteB),
                button(RowCommand::Skip)};
    case FileStatus::Identical:
        return {};
    }
    return {};
}

const ComparisonSession::Entry& ComparisonSession::entryOf(const fs::path& path) const
{
    const auto found = entries_.find(path);
    if (found == entries_.end()) {
        throw std::invalid_argument("Not a difference of the current comparison: " + toUtf8(path));
    }
    return found->second;
}

std::optional<RowCommand> ComparisonSession::parentAction(const fs::path& path) const
{
    for (fs::path parent = path.parent_path(); !parent.empty(); parent = parent.parent_path()) {
        if (const auto found = actions_.find(parent); found != actions_.end()) {
            return found->second.command;
        }
    }
    return std::nullopt;
}

bool ComparisonSession::hasDescendantAction(const fs::path& path) const
{
    // Descendants are contiguous in the native-string ordering, right after "path/".
    fs::path::string_type prefix = path.native();
    prefix.push_back(fs::path::preferred_separator);
    const auto found = actions_.lower_bound(fs::path(prefix));
    return found != actions_.end() && found->first.native().starts_with(prefix);
}

Row ComparisonSession::makeRow(const fs::path& path, const Entry& entry, std::size_t depth) const
{
    Row row;
    row.path = path;
    row.kind = entry.kind;
    row.status = entry.status;
    row.depth = depth;
    row.displayName = depth == 0 ? path : path.filename();

    if (entry.kind == EntryKind::Folder) {
        row.displayName += fs::path::preferred_separator;
        const auto children = children_.find(path);
        row.expandable = children != children_.end() && !children->second.empty();
        row.expanded = row.expandable && expanded_.contains(path);
        row.detail = formatFolderContent(folderContent_.at(path));
    }

    if (const auto own = actions_.find(path); own != actions_.end()) {
        row.action = own->second.command;
        row.style = commandStyle(own->second.command);
        row.statusText = actionStatusText(own->second.command);
        row.buttons = {RowButton{std::string(commandLabel(RowCommand::Undo)), RowCommand::Undo}};
    }
    else if (const auto inherited = parentAction(path)) {
        row.action = inherited;
        row.inherited = true;
        row.style = commandStyle(*inherited);
        row.statusText = actionStatusText(*inherited);
    }
    else {
        row.style = statusStyle(entry.status);
        row.statusText = upperLabel(row.style.label);
        if (!hasDescendantAction(path)) {
            row.buttons = buttonsFor(entry.status);
        }
    }
    return row;
}

EntryCounts ComparisonSession::summaryCounts(SummaryItem item) const
{
    switch (item) {
    case SummaryItem::Total:
        return counts_.total();
    case SummaryItem::Identical:
        return counts_.identical;
    case SummaryItem::AllDifferences:
        return counts_.allDifferences();
    case SummaryItem::Different:
        return counts_.different;
    case SummaryItem::OnlyA:
        return counts_.onlyA;
    case SummaryItem::OnlyB:
        return counts_.onlyB;
    case SummaryItem::Skipped:
        break;
    }

    // Skipped rows never overlap (no action inside an acted-on folder).
    EntryCounts counts;
    for (const auto& [path, action] : actions_) {
        if (action.command != RowCommand::Skip) {
            continue;
        }
        if (entries_.at(path).kind == EntryKind::Folder) {
            ++counts.folders;
            counts += folderContent_.at(path);
        }
        else {
            ++counts.files;
        }
    }
    return counts;
}

std::vector<Row> ComparisonSession::rows(Filter filter) const
{
    // The top-level rows of the filter, in display order.
    std::vector<const fs::path*> topLevelRows;
    if (filter == Filter::Skipped) {
        for (const auto& [path, action] : actions_) { // sorted with pathLess
            if (action.command == RowCommand::Skip) {
                topLevelRows.push_back(&path);
            }
        }
    }
    else {
        std::vector<FileStatus> statuses;
        if (const auto status = filterStatus(filter)) {
            statuses.push_back(*status);
        }
        else {
            statuses = {FileStatus::Different, FileStatus::OnlyA, FileStatus::OnlyB};
        }
        for (const FileStatus status : statuses) {
            if (const auto topLevel = topLevel_.find(status); topLevel != topLevel_.end()) {
                for (const fs::path& path : topLevel->second) {
                    topLevelRows.push_back(&path);
                }
            }
        }
    }

    std::vector<Row> rows;
    {
        // Depth-first walk with an explicit stack: expanded folders are
        // followed by their content.
        std::vector<std::pair<const fs::path*, std::size_t>> pending;
        for (const fs::path* path : std::views::reverse(topLevelRows)) {
            pending.emplace_back(path, 0);
        }

        while (!pending.empty()) {
            const auto [path, depth] = pending.back();
            pending.pop_back();

            const Entry& entry = entries_.at(*path);
            rows.push_back(makeRow(*path, entry, depth));

            if (entry.kind == EntryKind::Folder && expanded_.contains(*path)) {
                if (const auto children = children_.find(*path); children != children_.end()) {
                    for (const fs::path& child : std::views::reverse(children->second)) {
                        pending.emplace_back(&child, depth + 1);
                    }
                }
            }
        }
    }
    return rows;
}

std::optional<Row> ComparisonSession::row(const fs::path& relativePath) const
{
    const auto found = entries_.find(relativePath);
    if (found == entries_.end()) {
        return std::nullopt;
    }

    std::size_t depth = 0;
    for (fs::path parent = relativePath.parent_path(); entries_.contains(parent); parent = parent.parent_path()) {
        ++depth;
    }
    return makeRow(relativePath, found->second, depth);
}

std::optional<FileStatus> ComparisonSession::statusOf(const fs::path& relativePath) const
{
    if (const auto found = entries_.find(relativePath); found != entries_.end()) {
        return found->second.status;
    }
    return std::nullopt;
}

std::optional<RowCommand> ComparisonSession::actionOf(const fs::path& relativePath) const
{
    if (const auto found = actions_.find(relativePath); found != actions_.end()) {
        return found->second.command;
    }
    return std::nullopt;
}

std::vector<Location> ComparisonSession::existingLocations(const fs::path& relativePath) const
{
    std::vector<Location> locations;
    if (!hasResult_ || relativePath.empty() || !entries_.contains(relativePath)) {
        return locations;
    }
    for (const auto& [side, root] : {std::pair{Side::A, &folderA_}, std::pair{Side::B, &folderB_}}) {
        const fs::path path = *root / relativePath;
        std::error_code error;
        if (fs::exists(path, error)) {
            locations.push_back(Location{side, path});
        }
    }
    return locations;
}

void ComparisonSession::toggleExpanded(const fs::path& relativePath)
{
    if (entryOf(relativePath).kind != EntryKind::Folder) {
        throw std::invalid_argument("Not a folder: " + toUtf8(relativePath));
    }
    if (expanded_.erase(relativePath) == 0) {
        expanded_.insert(relativePath);
    }
}

bool ComparisonSession::isExpanded(const fs::path& relativePath) const
{
    return expanded_.contains(relativePath);
}

void ComparisonSession::apply(const fs::path& relativePath, RowCommand command)
{
    if (command == RowCommand::Undo) {
        (void)entryOf(relativePath); // throws for an unlisted path
        undo(relativePath);
        return;
    }

    applyAction(relativePath, command);
    redo_.clear(); // a new action: what was undone cannot be redone anymore
}

void ComparisonSession::applyAction(const fs::path& relativePath, RowCommand command)
{
    const Entry& entry = entryOf(relativePath);

    if (actions_.contains(relativePath) || parentAction(relativePath) || hasDescendantAction(relativePath)) {
        throw std::logic_error("An action is already applied to (or inside) " + toUtf8(relativePath));
    }

    const std::vector<RowButton> allowed = buttonsFor(entry.status);
    if (std::ranges::none_of(allowed, [command](const RowButton& button) { return button.command == command; })) {
        throw std::logic_error("Command not available for " + toUtf8(relativePath));
    }

    std::vector<ActionHistory::Id> ids;
    switch (command) {
    case RowCommand::CopyAToB:
        ids.push_back(history_.copy(folderA_ / relativePath, folderB_ / relativePath, folderB_));
        break;
    case RowCommand::CopyBToA:
        ids.push_back(history_.copy(folderB_ / relativePath, folderA_ / relativePath, folderA_));
        break;
    case RowCommand::DeleteA:
        ids.push_back(history_.remove(folderA_ / relativePath, folderA_));
        break;
    case RowCommand::DeleteB:
        ids.push_back(history_.remove(folderB_ / relativePath, folderB_));
        break;
    case RowCommand::DeleteBoth: {
        const ActionHistory::Id deletedA = history_.remove(folderA_ / relativePath, folderA_);
        try {
            ids = {deletedA, history_.remove(folderB_ / relativePath, folderB_)};
        }
        catch (...) {
            history_.undo(deletedA); // all or nothing
            throw;
        }
        break;
    }
    case RowCommand::Skip:
        break; // nothing happens on disk
    case RowCommand::Undo:
        return; // handled above
    }
    actions_.emplace(relativePath, AppliedAction{command, nextSequence_++, std::move(ids)});
    refreshCounts();
}

void ComparisonSession::undo(const fs::path& path)
{
    const auto found = actions_.find(path);
    if (found == actions_.end()) {
        throw std::logic_error("No action to undo for " + toUtf8(path));
    }
    // Last change first; an id is dropped once undone, so a failed undo can be retried.
    std::vector<ActionHistory::Id>& ids = found->second.historyIds;
    while (!ids.empty()) {
        history_.undo(ids.back());
        ids.pop_back();
    }
    const RowCommand command = found->second.command;
    actions_.erase(found);
    redo_.emplace_back(path, command);
    refreshCounts();
}

bool ComparisonSession::undoLast()
{
    if (actions_.empty()) {
        return false;
    }
    const auto latest = std::ranges::max_element(
        actions_, [](const auto& left, const auto& right) { return left.second.sequence < right.second.sequence; });
    const fs::path path = latest->first;
    undo(path);
    return true;
}

bool ComparisonSession::redoLast()
{
    if (redo_.empty()) {
        return false;
    }
    const auto [path, command] = redo_.back();
    applyAction(path, command); // throws on failure: kept for a later attempt
    redo_.pop_back();
    return true;
}

} // namespace fc
