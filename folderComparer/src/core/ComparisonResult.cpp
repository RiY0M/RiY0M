#include "core/ComparisonResult.hpp"

namespace fc {

const std::vector<std::filesystem::path>& ComparisonResult::files(FileStatus status) const noexcept
{
    switch (status) {
    case FileStatus::Identical:
        return identical;
    case FileStatus::Different:
        return different;
    case FileStatus::OnlyA:
        return onlyA;
    case FileStatus::OnlyB:
        return onlyB;
    }
    return identical; // unreachable: every enumerator is handled above
}

const std::vector<std::filesystem::path>& ComparisonResult::folders(FileStatus status) const noexcept
{
    switch (status) {
    case FileStatus::Identical:
        return identicalFolders;
    case FileStatus::Different:
        return differentFolders;
    case FileStatus::OnlyA:
        return onlyAFolders;
    case FileStatus::OnlyB:
        return onlyBFolders;
    }
    return identicalFolders; // unreachable: every enumerator is handled above
}

EntryCounts ComparisonResult::counts(FileStatus status) const noexcept
{
    return EntryCounts{.files = files(status).size(), .folders = folders(status).size()};
}

EntryCounts ComparisonResult::allDifferencesCounts() const noexcept
{
    EntryCounts total = counts(FileStatus::Different);
    total += counts(FileStatus::OnlyA);
    total += counts(FileStatus::OnlyB);
    return total;
}

EntryCounts ComparisonResult::totalCounts() const noexcept
{
    EntryCounts total = counts(FileStatus::Identical);
    total += allDifferencesCounts();
    return total;
}

std::vector<FileEntry> ComparisonResult::entries(FileStatus status) const
{
    const auto& paths = files(status);
    std::vector<FileEntry> result;
    result.reserve(paths.size());
    for (const auto& path : paths) {
        result.push_back(FileEntry{path, status});
    }
    return result;
}

std::vector<FileEntry> ComparisonResult::allDifferences() const
{
    std::vector<FileEntry> result;
    result.reserve(allDifferencesCount());
    for (const auto status : {FileStatus::Different, FileStatus::OnlyA, FileStatus::OnlyB}) {
        for (const auto& path : files(status)) {
            result.push_back(FileEntry{path, status});
        }
    }
    return result;
}

} // namespace fc
