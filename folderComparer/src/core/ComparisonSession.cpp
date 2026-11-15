#include "core/ComparisonSession.hpp"

#include "core/FileManager.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace fs = std::filesystem;

namespace fc {

bool PathLess::operator()(const fs::path& left, const fs::path& right) const noexcept
{
    return pathLess(left, right);
}

void ComparisonSession::setResult(fs::path folderA, fs::path folderB, ComparisonResult result)
{
    reset();
    folderA_ = std::move(folderA);
    folderB_ = std::move(folderB);
    result_ = std::move(result);
    hasResult_ = true;

    for (const FileEntry& entry : result_.allDifferences()) {
        statuses_.emplace(entry.path, entry.status);
    }
}

void ComparisonSession::reset()
{
    folderA_.clear();
    folderB_.clear();
    result_ = ComparisonResult{};
    hasResult_ = false;
    statuses_.clear();
    actions_.clear();
}

std::vector<RowButton> ComparisonSession::buttonsFor(FileStatus status)
{
    switch (status) {
    case FileStatus::OnlyA:
        return {RowButton{"Copy → B", CopyDirection::AToB}};
    case FileStatus::OnlyB:
        return {RowButton{"Copy → A", CopyDirection::BToA}};
    case FileStatus::Different:
        // Same visual order as the Python version: "B → A" then "A → B".
        return {RowButton{"B → A", CopyDirection::BToA}, RowButton{"A → B", CopyDirection::AToB}};
    case FileStatus::Identical:
        return {};
    }
    return {};
}

Row ComparisonSession::makeRow(const FileEntry& entry) const
{
    Row row;
    row.path = entry.path;
    row.status = entry.status;
    row.action = actionOf(entry.path);

    if (row.action) {
        row.style = actionStyle(*row.action);
    }
    else {
        row.style = statusStyle(entry.status);
        row.buttons = buttonsFor(entry.status);
    }
    row.statusText = upperLabel(row.style.label);
    return row;
}

std::vector<Row> ComparisonSession::rows(Filter filter) const
{
    const std::optional<FileStatus> status = filterStatus(filter);
    const std::vector<FileEntry> entries = status ? result_.entries(*status) : result_.allDifferences();

    std::vector<Row> result;
    result.reserve(entries.size());
    for (const FileEntry& entry : entries) {
        result.push_back(makeRow(entry));
    }
    return result;
}

std::optional<Row> ComparisonSession::row(const fs::path& relativePath) const
{
    const std::optional<FileStatus> status = statusOf(relativePath);
    if (!status) {
        return std::nullopt;
    }
    return makeRow(FileEntry{relativePath, *status});
}

std::optional<FileStatus> ComparisonSession::statusOf(const fs::path& relativePath) const
{
    if (const auto found = statuses_.find(relativePath); found != statuses_.end()) {
        return found->second;
    }
    return std::nullopt;
}

std::optional<FileAction> ComparisonSession::actionOf(const fs::path& relativePath) const
{
    if (const auto found = actions_.find(relativePath); found != actions_.end()) {
        return found->second;
    }
    return std::nullopt;
}

void ComparisonSession::copy(const fs::path& relativePath, CopyDirection direction)
{
    const std::optional<FileStatus> status = statusOf(relativePath);
    if (!status) {
        throw std::invalid_argument("Not a difference of the current comparison: " + toUtf8(relativePath));
    }
    if (actionOf(relativePath)) {
        throw std::logic_error("An action was already applied to: " + toUtf8(relativePath));
    }

    const std::vector<RowButton> allowed = buttonsFor(*status);
    const bool directionAllowed = std::ranges::any_of(
        allowed, [direction](const RowButton& button) { return button.direction == direction; });
    if (!directionAllowed) {
        throw std::logic_error("Copy direction not allowed for: " + toUtf8(relativePath));
    }

    const bool aToB = direction == CopyDirection::AToB;
    const fs::path& sourceFolder = aToB ? folderA_ : folderB_;
    const fs::path& destinationFolder = aToB ? folderB_ : folderA_;

    FileManager::copyFile(sourceFolder / relativePath, destinationFolder / relativePath);
    actions_.emplace(relativePath, FileAction::Copy);
}

} // namespace fc
