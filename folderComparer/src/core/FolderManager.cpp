#include "core/FolderManager.hpp"

#include "core/FileManager.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace fc::FolderManager {

namespace {

template <typename CharT>
std::basic_string<CharT> trimmedString(const std::basic_string<CharT>& text)
{
    constexpr std::array<CharT, 6> whitespace{
        static_cast<CharT>(' '),  static_cast<CharT>('\t'), static_cast<CharT>('\n'),
        static_cast<CharT>('\r'), static_cast<CharT>('\f'), static_cast<CharT>('\v'),
    };
    const auto isSpace = [&whitespace](CharT character) {
        return std::ranges::find(whitespace, character) != whitespace.end();
    };

    const auto first = std::ranges::find_if_not(text, isSpace);
    const auto last = std::find_if_not(text.rbegin(), text.rend(), isSpace).base();
    if (first >= last) {
        return {};
    }
    return std::basic_string<CharT>(first, last);
}

/// Folders present on both sides are "different" when anything inside them
/// differs, "identical" otherwise.
void classifyCommonFolders(ComparisonResult& result, const std::vector<fs::path>& commonFolders)
{
    std::set<fs::path, PathLess> foldersWithDifferences;
    const auto markParents = [&foldersWithDifferences](const fs::path& path) {
        for (fs::path parent = path.parent_path(); !parent.empty(); parent = parent.parent_path()) {
            if (!foldersWithDifferences.insert(parent).second) {
                break; // its own parents are already marked
            }
        }
    };

    for (const auto* paths : {&result.different, &result.onlyA, &result.onlyB, &result.onlyAFolders,
                              &result.onlyBFolders}) {
        for (const fs::path& path : *paths) {
            markParents(path);
        }
    }

    for (const fs::path& folder : commonFolders) {
        if (foldersWithDifferences.contains(folder)) {
            result.differentFolders.push_back(folder);
        }
        else {
            result.identicalFolders.push_back(folder);
        }
    }
}

constexpr const char* kInvalidFolderTitle = "Invalid folder";

std::optional<FolderValidationError> validateFolder(const fs::path& rawPath, fs::path& normalized)
{
    const fs::path folderPath = trimmed(rawPath);
    if (folderPath.empty()) {
        return FolderValidationError{kInvalidFolderTitle, "Folder must not be empty."};
    }

    try {
        normalized = normalizeFolderPath(folderPath);
    }
    catch (const fs::filesystem_error&) {
        return FolderValidationError{kInvalidFolderTitle, "Folder does not exist."};
    }

    if (!isFolderPathValid(normalized)) {
        return FolderValidationError{kInvalidFolderTitle, "Folder does not exist."};
    }
    return std::nullopt;
}

} // namespace

ComparisonResult compare(const fs::path& folderA, const fs::path& folderB,
                         const CompareOptions& options, const ProgressCallback& progress,
                         const std::stop_token& stopToken)
{
    ComparisonResult result;

    const FolderListing listingA = FileManager::listEntries(folderA, options.recursive);
    if (stopToken.stop_requested()) {
        throw OperationCancelled{};
    }
    const FolderListing listingB = FileManager::listEntries(folderB, options.recursive);

    std::vector<fs::path> common;
    std::ranges::set_difference(listingA.files, listingB.files, std::back_inserter(result.onlyA), pathLess);
    std::ranges::set_difference(listingB.files, listingA.files, std::back_inserter(result.onlyB), pathLess);
    std::ranges::set_intersection(listingA.files, listingB.files, std::back_inserter(common), pathLess);

    std::vector<fs::path> commonFolders;
    std::ranges::set_difference(listingA.folders, listingB.folders, std::back_inserter(result.onlyAFolders),
                                pathLess);
    std::ranges::set_difference(listingB.folders, listingA.folders, std::back_inserter(result.onlyBFolders),
                                pathLess);
    std::ranges::set_intersection(listingA.folders, listingB.folders, std::back_inserter(commonFolders),
                                  pathLess);

    const std::size_t total = common.size();
    for (std::size_t index = 0; index < total; ++index) {
        if (stopToken.stop_requested()) {
            throw OperationCancelled{};
        }

        const fs::path& path = common[index];
        if (progress) {
            progress(index + 1, total, path);
        }

        try {
            if (FileManager::filesAreIdentical(folderA / path, folderB / path,
                                               options.deepComparison, stopToken)) {
                result.identical.push_back(path);
            }
            else {
                result.different.push_back(path);
            }
        }
        catch (const OperationCancelled&) {
            throw;
        }
        catch (const std::exception& error) {
            result.different.push_back(path);
            result.errors.push_back(ComparisonError{path, error.what()});
        }
    }

    classifyCommonFolders(result, commonFolders);
    return result;
}

fs::path normalizeFolderPath(const fs::path& folderPath)
{
    if (folderPath.empty()) {
        return {};
    }

    fs::path normalized = fs::absolute(folderPath).lexically_normal();
    if (!normalized.has_filename() && normalized.has_relative_path()) {
        normalized = normalized.parent_path(); // "/a/b/" -> "/a/b", but "/" stays "/"
    }
    normalized.make_preferred();
    return normalized;
}

bool isFolderPathValid(const fs::path& folderPath)
{
    if (folderPath.empty()) {
        return false;
    }

    try {
        std::error_code error;
        return fs::is_directory(normalizeFolderPath(folderPath), error);
    }
    catch (const fs::filesystem_error&) {
        return false;
    }
}

bool checkSameFolders(const fs::path& folderA, const fs::path& folderB)
{
    if (folderA.empty() || folderB.empty()) {
        return false;
    }

    try {
        std::error_code error;
        const bool same =
            fs::equivalent(normalizeFolderPath(folderA), normalizeFolderPath(folderB), error);
        return !error && same;
    }
    catch (const fs::filesystem_error&) {
        return false;
    }
}

fs::path trimmed(const fs::path& path)
{
    return {trimmedString(path.native())};
}

FolderValidation validateFolders(const fs::path& folderA, const fs::path& folderB)
{
    ValidatedFolders folders;

    if (auto error = validateFolder(folderA, folders.folderA)) {
        return *std::move(error);
    }
    if (auto error = validateFolder(folderB, folders.folderB)) {
        return *std::move(error);
    }
    if (checkSameFolders(folders.folderA, folders.folderB)) {
        return FolderValidationError{"Invalid folders", "Folder A and Folder B are the same folder."};
    }
    return folders;
}

} // namespace fc::FolderManager
