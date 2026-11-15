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
