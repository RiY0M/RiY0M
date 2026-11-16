#include "core/ComparisonSession.hpp"

#include <algorithm>
#include <functional>
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
    selection_.clear();
    typeFilter_.reset();
    filteredContent_.clear();
    nextSequence_ = 1;
    nextGroup_ = 1;
    liveCounts_.reset(result_);
    counts_ = CategoryCounts{};
}

std::vector<RowCommand> RowButton::commands() const
{
    if (choices.empty()) {
        return {command};
    }
    std::vector<RowCommand> result;
    result.reserve(choices.size());
    for (const RowChoice& choice : choices) {
        result.push_back(choice.command);
    }
    return result;
}

std::vector<RowButton> ComparisonSession::buttonsFor(FileStatus status)
{
    const auto button = [](RowCommand command) {
        return RowButton{std::string(commandLabel(command)), command, {}};
    };
    const auto choice = [](RowCommand command) { return RowChoice{std::string(commandLabel(command)), command}; };

    switch (status) {
    case FileStatus::Different:
        // Menus keep the row compact: the precise copy / deletion is picked in them.
        return {RowButton{"Copy", RowCommand::CopyAToB, {choice(RowCommand::CopyAToB), choice(RowCommand::CopyBToA)}},
                RowButton{"Delete", RowCommand::DeleteA,
                          {choice(RowCommand::DeleteA), choice(RowCommand::DeleteB), choice(RowCommand::DeleteBoth)}},
                button(RowCommand::Skip)};
    case FileStatus::OnlyA:
        return {RowButton{"Copy → B", RowCommand::CopyAToB, {}}, button(RowCommand::DeleteA),
                button(RowCommand::Skip)};
    case FileStatus::OnlyB:
        return {RowButton{"Copy → A", RowCommand::CopyBToA, {}}, button(RowCommand::DeleteB),
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
        row.expandable = !sortedChildren(path).empty(); // shown content only
        row.expanded = row.expandable && expanded_.contains(path);
        row.detail = formatFolderContent(shownContent(path));
    }
    else {
        row.extension = extensionLabel(path);
    }

    if (const auto own = actions_.find(path); own != actions_.end()) {
        row.action = own->second.command;
        row.style = commandStyle(own->second.command);
        row.statusText = actionStatusText(own->second.command);
        row.buttons = {RowButton{std::string(commandLabel(RowCommand::Undo)), RowCommand::Undo, {}}};
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
        if (typeFilter_ && entry.kind == EntryKind::Folder) {
            applyFilteredFolderState(path, entry, row);
        }
        else if (!hasDescendantAction(path)) {
            row.buttons = buttonsFor(entry.status);
        }
    }
    row.selectable = !row.buttons.empty();
    row.selected = row.selectable && selection_.contains(path);
    return row;
}

void ComparisonSession::applyFilteredFolderState(const fs::path& path, const Entry& entry, Row& row) const
{
    // With a type filter, a folder row stands for its matching files: it offers
    // the actions while none of them has one, and Undo once all of them have one.
    if (hasDescendantFolderAction(path)) {
        return; // a sub-folder carries a whole-folder action: nothing to offer here
    }
    const std::vector<fs::path> targets = actionTargets(path);
    std::optional<RowCommand> common;
    bool sameAction = true;
    std::size_t acted = 0;
    for (const fs::path& target : targets) {
        if (const auto action = actionOf(target)) {
            ++acted;
            sameAction = sameAction && (!common || *common == *action);
            common = action;
        }
    }

    if (acted == 0) {
        row.buttons = buttonsFor(entry.status);
    }
    else if (acted == targets.size()) {
        row.buttons = {RowButton{std::string(commandLabel(RowCommand::Undo)), RowCommand::Undo, {}}};
        if (sameAction && common) {
            row.action = common;
            row.style = commandStyle(*common);
            row.statusText = actionStatusText(*common);
        }
    }
    // Some done, some not: no folder-wide button (each file keeps its own).
}

bool ComparisonSession::hasDescendantFolderAction(const fs::path& path) const
{
    fs::path::string_type prefix = path.native();
    prefix.push_back(fs::path::preferred_separator);
    for (auto action = actions_.lower_bound(fs::path(prefix));
         action != actions_.end() && action->first.native().starts_with(prefix); ++action) {
        if (entries_.at(action->first).kind == EntryKind::Folder) {
            return true;
        }
    }
    return false;
}

std::vector<fs::path> ComparisonSession::undoTargets(const fs::path& path) const
{
    if (actions_.contains(path)) {
        return {path};
    }
    std::vector<fs::path> targets; // a filtered folder: its matching files that have an action
    if (typeFilter_ && entryOf(path).kind == EntryKind::Folder) {
        for (fs::path& target : actionTargets(path)) {
            if (actions_.contains(target)) {
                targets.push_back(std::move(target));
            }
        }
    }
    if (targets.empty()) {
        throw std::logic_error("No action to undo for " + toUtf8(path));
    }
    return targets;
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

bool ComparisonSession::isShown(const fs::path& path, EntryKind kind) const
{
    if (!typeFilter_) {
        return true;
    }
    if (kind == EntryKind::Folder) {
        return filteredContent_.contains(path); // holds at least one matching file
    }
    return typeFilter_->contains(extensionLabel(path));
}

const EntryCounts& ComparisonSession::shownContent(const fs::path& folder) const
{
    if (!typeFilter_) {
        return folderContent_.at(folder);
    }
    static const EntryCounts nothing;
    const auto found = filteredContent_.find(folder);
    return found != filteredContent_.end() ? found->second : nothing;
}

void ComparisonSession::refreshFilteredContent()
{
    filteredContent_.clear();
    if (!typeFilter_) {
        return;
    }
    const auto isFolder = [this](const fs::path& path) {
        const auto found = entries_.find(path);
        return found != entries_.end() && found->second.kind == EntryKind::Folder;
    };

    // Matching files, counted in every folder above them...
    for (const auto& [path, entry] : entries_) {
        if (entry.kind == EntryKind::File && typeFilter_->contains(extensionLabel(path))) {
            for (fs::path parent = path.parent_path(); isFolder(parent); parent = parent.parent_path()) {
                ++filteredContent_[parent].files;
            }
        }
    }
    // ...then the folders holding them, counted in the folders above them.
    std::vector<fs::path> shownFolders;
    shownFolders.reserve(filteredContent_.size());
    for (const auto& [folder, content] : filteredContent_) {
        shownFolders.push_back(folder);
    }
    for (const fs::path& folder : shownFolders) {
        for (fs::path parent = folder.parent_path(); isFolder(parent); parent = parent.parent_path()) {
            ++filteredContent_[parent].folders;
        }
    }
}

bool ComparisonSession::rowBefore(const fs::path& left, const fs::path& right) const
{
    if (sortOrder_ == SortOrder::Ascending) {
        return caseInsensitiveLess(left, right);
    }
    // Descending: `left` comes first when it would come last in ascending order.
    const fs::path& later = left;
    const fs::path& earlier = right;
    return caseInsensitiveLess(earlier, later);
}

std::vector<const fs::path*> ComparisonSession::sortedChildren(const fs::path& folder) const
{
    std::vector<const fs::path*> children;
    if (const auto found = children_.find(folder); found != children_.end()) {
        for (const fs::path& child : found->second) {
            if (isShown(child, entries_.at(child).kind)) {
                children.push_back(&child);
            }
        }
    }
    // Sub-folders first, then files, like a file explorer; by name in each group.
    std::ranges::sort(children, [this](const fs::path* left, const fs::path* right) {
        const bool leftIsFolder = entries_.at(*left).kind == EntryKind::Folder;
        const bool rightIsFolder = entries_.at(*right).kind == EntryKind::Folder;
        if (leftIsFolder != rightIsFolder) {
            return leftIsFolder;
        }
        return rowBefore(left->filename(), right->filename());
    });
    return children;
}

std::vector<Row> ComparisonSession::rows(Filter filter) const
{
    // The top-level rows of the filter.
    std::vector<const fs::path*> topLevelRows;
    if (filter == Filter::Skipped) {
        for (const auto& [path, action] : actions_) {
            if (action.command == RowCommand::Skip && isShown(path, entries_.at(path).kind)) {
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
                    if (isShown(path, entries_.at(path).kind)) {
                        topLevelRows.push_back(&path);
                    }
                }
            }
        }
    }
    std::ranges::sort(topLevelRows, [this](const fs::path* left, const fs::path* right) {
        return rowBefore(*left, *right);
    });

    // Depth-first walk with an explicit stack: expanded folders are followed by their content.
    std::vector<Row> rows;
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
            const std::vector<const fs::path*> children = sortedChildren(*path); // a named range: GCC 11
            for (const fs::path* child : std::views::reverse(children)) {
                pending.emplace_back(child, depth + 1);
            }
        }
    }
    return rows;
}

std::map<std::string, std::size_t> ComparisonSession::fileTypes(Filter filter) const
{
    std::map<std::string, std::size_t> types;
    if (filter == Filter::Skipped) {
        // The skipped rows and, for folders, every file inside them.
        for (const auto& [path, action] : actions_) {
            if (action.command != RowCommand::Skip) {
                continue;
            }
            if (entries_.at(path).kind == EntryKind::File) {
                ++types[extensionLabel(path)];
                continue;
            }
            fs::path::string_type prefix = path.native();
            prefix.push_back(fs::path::preferred_separator);
            for (auto entry = entries_.lower_bound(fs::path(prefix));
                 entry != entries_.end() && entry->first.native().starts_with(prefix); ++entry) {
                if (entry->second.kind == EntryKind::File) {
                    ++types[extensionLabel(entry->first)];
                }
            }
        }
        return types;
    }

    const std::optional<FileStatus> only = filterStatus(filter); // nullopt: every difference
    for (const auto& [path, entry] : entries_) {
        if (entry.kind == EntryKind::File && (!only || entry.status == *only)) {
            ++types[extensionLabel(path)];
        }
    }
    return types;
}

void ComparisonSession::setTypeFilter(std::optional<std::set<std::string>> types)
{
    typeFilter_ = std::move(types);
    refreshFilteredContent();
    selection_.clear();
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
        Redo undone; // a filtered folder undoes its files' actions, as one
        for (const fs::path& target : undoTargets(relativePath)) {
            undone.emplace_back(target, undoAction(target));
        }
        redo_.push_back(std::move(undone));
    }
    else {
        (void)entryOf(relativePath); // throws for an unlisted path
        const std::vector<fs::path> targets = actionTargets(relativePath);
        if (targets.size() == 1 && targets.front() == relativePath) {
            applyAction(relativePath, command, nextGroup_++); // throws: nothing recorded
            redo_.clear(); // a new action: what was undone cannot be redone anymore
        }
        else {
            applyToTargets(targets, command); // the matching files of a filtered folder
        }
    }
    pruneSelection();
}

std::vector<fs::path> ComparisonSession::actionTargets(const fs::path& path) const
{
    if (!typeFilter_ || entryOf(path).kind != EntryKind::Folder) {
        return {path};
    }
    std::vector<fs::path> files;
    fs::path::string_type prefix = path.native();
    prefix.push_back(fs::path::preferred_separator);
    for (auto entry = entries_.lower_bound(fs::path(prefix));
         entry != entries_.end() && entry->first.native().starts_with(prefix); ++entry) {
        if (entry->second.kind == EntryKind::File && isShown(entry->first, EntryKind::File)) {
            files.push_back(entry->first);
        }
    }
    return files;
}

void ComparisonSession::applyToTargets(const std::vector<fs::path>& targets, RowCommand command)
{
    if (targets.empty()) {
        throw std::logic_error("No matching file to act on");
    }
    const std::uint64_t group = nextGroup_++;
    std::vector<ActionFailure> failures;
    for (const fs::path& target : targets) {
        try {
            applyAction(target, command, group);
        }
        catch (const std::exception& error) {
            failures.push_back(ActionFailure{target, error.what()});
        }
    }
    if (failures.size() < targets.size()) {
        redo_.clear(); // a new action
    }
    if (!failures.empty()) {
        std::string message = std::to_string(failures.size()) + " of " + std::to_string(targets.size()) +
                              " file(s) could not be processed:";
        constexpr std::size_t kListed = 5;
        for (std::size_t index = 0; index < failures.size() && index < kListed; ++index) {
            message += "\n\n" + toUtf8(failures.at(index).path) + "\n" + failures.at(index).message;
        }
        throw std::runtime_error(message);
    }
}

void ComparisonSession::applyAction(const fs::path& relativePath, RowCommand command, std::uint64_t group)
{
    const Entry& entry = entryOf(relativePath);

    if (actions_.contains(relativePath) || parentAction(relativePath) || hasDescendantAction(relativePath)) {
        throw std::logic_error("An action is already applied to (or inside) " + toUtf8(relativePath));
    }

    const std::vector<RowButton> allowed = buttonsFor(entry.status);
    if (std::ranges::none_of(allowed, [command](const RowButton& button) {
            const std::vector<RowCommand> commands = button.commands();
            return std::ranges::find(commands, command) != commands.end();
        })) {
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
        return; // handled by the callers
    }
    actions_.emplace(relativePath, AppliedAction{command, nextSequence_++, group, std::move(ids)});
    refreshCounts();
}

RowCommand ComparisonSession::undoAction(const fs::path& path)
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
    refreshCounts();
    return command;
}

bool ComparisonSession::undoLast()
{
    if (actions_.empty()) {
        return false;
    }
    const auto latest = std::ranges::max_element(
        actions_, [](const auto& left, const auto& right) { return left.second.sequence < right.second.sequence; });
    const std::uint64_t group = latest->second.group;

    // Every action of that group, most recent first.
    std::vector<std::pair<std::uint64_t, fs::path>> members;
    for (const auto& [path, action] : actions_) {
        if (action.group == group) {
            members.emplace_back(action.sequence, path);
        }
    }
    std::ranges::sort(members, std::greater<>{});

    Redo redo;
    try {
        for (const auto& [sequence, path] : members) {
            redo.emplace_back(path, undoAction(path));
        }
    }
    catch (...) {
        if (!redo.empty()) {
            std::ranges::reverse(redo);
            redo_.push_back(std::move(redo)); // what was undone stays redoable
        }
        pruneSelection();
        throw;
    }
    std::ranges::reverse(redo); // redo in the original order
    redo_.push_back(std::move(redo));
    pruneSelection();
    return true;
}

bool ComparisonSession::redoLast()
{
    if (redo_.empty()) {
        return false;
    }
    Redo& pending = redo_.back();
    const std::uint64_t group = nextGroup_++;
    while (!pending.empty()) {
        const auto [path, command] = pending.front();
        applyAction(path, command, group); // throws on failure: the rest stays redoable
        pending.erase(pending.begin());
    }
    redo_.pop_back();
    pruneSelection();
    return true;
}

// ---------------------------------------------------------------------------
// Selection and mass actions
// ---------------------------------------------------------------------------

void ComparisonSession::setSelected(const fs::path& relativePath, bool selected)
{
    if (!selected) {
        // The row, everything inside it, and every folder above it (no longer fully selected).
        selection_.erase(relativePath);
        fs::path::string_type prefix = relativePath.native();
        prefix.push_back(fs::path::preferred_separator);
        for (auto inside = selection_.lower_bound(fs::path(prefix));
             inside != selection_.end() && inside->native().starts_with(prefix);) {
            inside = selection_.erase(inside);
        }
        for (fs::path parent = relativePath.parent_path(); !parent.empty(); parent = parent.parent_path()) {
            selection_.erase(parent);
        }
        return;
    }

    const auto found = row(relativePath);
    if (!found || !found->selectable) {
        throw std::invalid_argument("Not selectable: " + toUtf8(relativePath));
    }
    selection_.insert(relativePath);

    // A folder brings its shown content.
    if (found->kind == EntryKind::Folder) {
        std::vector<const fs::path*> pending = sortedChildren(relativePath);
        while (!pending.empty()) {
            const fs::path* child = pending.back();
            pending.pop_back();
            if (const auto childRow = row(*child); childRow && childRow->selectable) {
                selection_.insert(*child);
            }
            if (entries_.at(*child).kind == EntryKind::Folder) {
                for (const fs::path* grandChild : sortedChildren(*child)) {
                    pending.push_back(grandChild);
                }
            }
        }
    }

    // A folder whose shown content is now entirely selected is selected too, up the tree.
    for (fs::path parent = relativePath.parent_path(); entries_.contains(parent); parent = parent.parent_path()) {
        const std::vector<const fs::path*> children = sortedChildren(parent);
        const bool allSelected = std::ranges::all_of(children, [this](const fs::path* child) {
            if (selection_.contains(*child)) {
                return true;
            }
            const auto childRow = row(*child);
            return !childRow || !childRow->selectable; // nothing to select there
        });
        const auto parentRow = row(parent);
        if (!allSelected || !parentRow || !parentRow->selectable) {
            break;
        }
        selection_.insert(parent);
    }
}

void ComparisonSession::setAllSelected(Filter filter, bool selected)
{
    for (const Row& shown : rows(filter)) {
        if (!shown.selectable) {
            continue;
        }
        if (selected) {
            selection_.insert(shown.path);
        }
        else {
            selection_.erase(shown.path);
        }
    }
}

EntryCounts ComparisonSession::selectionCounts() const
{
    EntryCounts counts;
    std::set<fs::path, PathLess> missingParents; // unselected missing folders above selected rows
    for (const fs::path& path : selection_) {
        bool covered = false; // inside a selected folder: already counted with it
        for (fs::path parent = path.parent_path(); !parent.empty() && !covered; parent = parent.parent_path()) {
            covered = selection_.contains(parent);
        }
        if (covered) {
            continue;
        }
        if (entries_.at(path).kind == EntryKind::Folder) {
            ++counts.folders;
            counts += shownContent(path); // with a type filter: what the actions will touch
        }
        else {
            ++counts.files;
        }
        // The missing folders above it differ too (copying it creates them), even unselected.
        for (fs::path parent = path.parent_path(); entries_.contains(parent); parent = parent.parent_path()) {
            missingParents.insert(parent);
        }
    }
    counts.folders += missingParents.size();
    return counts;
}

bool ComparisonSession::isSelected(const fs::path& relativePath) const
{
    return selection_.contains(relativePath);
}

SelectionState ComparisonSession::selectionState(Filter filter) const
{
    std::size_t selectable = 0;
    std::size_t selected = 0;
    for (const Row& shown : rows(filter)) {
        if (shown.selectable) {
            ++selectable;
            selected += shown.selected ? 1U : 0U;
        }
    }
    if (selected == 0) {
        return SelectionState::None;
    }
    return selected == selectable ? SelectionState::All : SelectionState::Some;
}

std::vector<RowCommand> ComparisonSession::commonCommands() const
{
    std::vector<RowCommand> common;
    bool first = true;
    for (const fs::path& path : selection_) {
        const auto selectedRow = row(path);
        if (!selectedRow) {
            continue;
        }
        std::vector<RowCommand> offered;
        for (const RowButton& button : selectedRow->buttons) {
            for (const RowCommand command : button.commands()) {
                offered.push_back(command);
            }
        }
        if (first) {
            common = std::move(offered);
            first = false;
        }
        else {
            std::erase_if(common, [&offered](RowCommand command) {
                return std::ranges::find(offered, command) == offered.end();
            });
        }
    }
    std::ranges::sort(common);
    return common;
}

std::vector<RowButton> ComparisonSession::massButtons(const std::vector<RowCommand>& commands)
{
    std::vector<RowChoice> copies;
    std::vector<RowChoice> deletions;
    std::vector<RowButton> others;
    for (const RowCommand command : commands) {
        const RowChoice choice{std::string(commandLabel(command)), command};
        switch (commandAction(command).value_or(FileAction::Skip)) {
        case FileAction::Copy:
            copies.push_back(choice);
            break;
        case FileAction::Delete:
            deletions.push_back(choice);
            break;
        case FileAction::Skip: // Skip, and Undo (no action)
            others.push_back(RowButton{choice.label, command, {}});
            break;
        }
    }

    std::vector<RowButton> buttons;
    if (copies.size() == 1) {
        // Same label as on a one-sided row.
        const RowCommand command = copies.front().command;
        buttons.push_back(RowButton{command == RowCommand::CopyAToB ? "Copy → B" : "Copy → A", command, {}});
    }
    else if (!copies.empty()) {
        buttons.push_back(RowButton{"Copy", copies.front().command, copies});
    }
    if (deletions.size() == 1) {
        buttons.push_back(RowButton{deletions.front().label, deletions.front().command, {}});
    }
    else if (!deletions.empty()) {
        buttons.push_back(RowButton{"Delete", deletions.front().command, deletions});
    }
    buttons.insert(buttons.end(), others.begin(), others.end());
    return buttons;
}

std::vector<ActionFailure> ComparisonSession::applyToSelection(RowCommand command)
{
    const std::vector<RowCommand> common = commonCommands();
    if (std::ranges::find(common, command) == common.end()) {
        throw std::logic_error("Command not common to the whole selection");
    }

    // A row inside a selected folder is covered by the folder's own action.
    std::vector<fs::path> targets;
    for (const fs::path& path : selection_) {
        bool covered = false;
        for (fs::path parent = path.parent_path(); !parent.empty() && !covered; parent = parent.parent_path()) {
            covered = selection_.contains(parent);
        }
        if (!covered) {
            targets.push_back(path);
        }
    }

    // With a type filter, a selected folder stands for its matching files.
    {
        std::vector<fs::path> expanded;
        for (const fs::path& path : targets) {
            for (fs::path& target : command == RowCommand::Undo ? undoTargets(path) : actionTargets(path)) {
                expanded.push_back(std::move(target));
            }
        }
        targets = std::move(expanded);
    }

    std::vector<ActionFailure> failures;
    const std::uint64_t group = nextGroup_++;
    Redo undone;
    for (const fs::path& path : targets) {
        try {
            if (command == RowCommand::Undo) {
                undone.emplace_back(path, undoAction(path));
            }
            else {
                applyAction(path, command, group);
            }
        }
        catch (const std::exception& error) {
            failures.push_back(ActionFailure{path, error.what()});
        }
    }

    if (command == RowCommand::Undo) {
        if (!undone.empty()) {
            redo_.push_back(std::move(undone));
        }
    }
    else if (failures.size() < targets.size()) {
        redo_.clear(); // a new action
    }
    pruneSelection();
    return failures;
}

void ComparisonSession::pruneSelection()
{
    std::erase_if(selection_, [this](const fs::path& path) {
        const auto found = row(path);
        return !found || !found->selectable;
    });
}

} // namespace fc
