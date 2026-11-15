#include "core/FolderManager.hpp"

#include "core/FileManager.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <iterator>
#include <optional>
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

    const std::vector<fs::path> filesA = FileManager::getFiles(folderA, options.recursive);
    if (stopToken.stop_requested()) {
        throw OperationCancelled{};
    }
    const std::vector<fs::path> filesB = FileManager::getFiles(folderB, options.recursive);

    std::vector<fs::path> common;
    std::ranges::set_difference(filesA, filesB, std::back_inserter(result.onlyA), pathLess);
    std::ranges::set_difference(filesB, filesA, std::back_inserter(result.onlyB), pathLess);
    std::ranges::set_intersection(filesA, filesB, std::back_inserter(common), pathLess);

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
