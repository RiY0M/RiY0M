// Command line front-end: compares two folders without any GUI.
//
//   folder_comparer_cli [--deep] [--no-recursive] [--summary] <folderA> <folderB>
//
// Output: one "<status>\t<relative/path>" line per file (status being
// identical, different, only_a or only_b, paths with '/' separators), then
// the summary when --summary is given. Exit code: 0 when every file is
// identical, 1 when there are differences, 2 on error.

#include "core/ComparisonResult.hpp"
#include "core/FileManager.hpp"
#include "core/FolderManager.hpp"
#include "core/Presentation.hpp"

#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kExitDifferences = 1;
constexpr int kExitError = 2;

struct Arguments {
    fc::CompareOptions options;
    bool summary = false;
    std::vector<std::string> folders;
};

void printUsage(std::ostream& stream)
{
    stream << "Usage: folder_comparer_cli [--deep] [--no-recursive] [--summary] <folderA> <folderB>\n"
              "  --deep          compare file contents, not only sizes\n"
              "  --no-recursive  only compare the files directly inside the folders\n"
              "  --summary       print the totals after the file list\n";
}

void printFiles(std::string_view status, const std::vector<fs::path>& files)
{
    for (const fs::path& file : files) {
        std::cout << status << '\t' << fc::toUtf8(file.generic_u8string()) << '\n';
    }
}

int run(std::span<char*> argv)
{
    Arguments arguments;
    for (std::size_t index = 1; index < argv.size(); ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--deep") {
            arguments.options.deepComparison = true;
        }
        else if (argument == "--no-recursive") {
            arguments.options.recursive = false;
        }
        else if (argument == "--summary") {
            arguments.summary = true;
        }
        else if (argument == "--help" || argument == "-h") {
            printUsage(std::cout);
            return EXIT_SUCCESS;
        }
        else if (argument.starts_with("--")) {
            std::cerr << "Unknown option: " << argument << '\n';
            printUsage(std::cerr);
            return kExitError;
        }
        else {
            arguments.folders.emplace_back(argument);
        }
    }

    if (arguments.folders.size() != 2) {
        printUsage(std::cerr);
        return kExitError;
    }

    const fc::FolderValidation validation =
        fc::FolderManager::validateFolders(fs::path(arguments.folders[0]), fs::path(arguments.folders[1]));
    if (const auto* error = std::get_if<fc::FolderValidationError>(&validation)) {
        std::cerr << error->title << ": " << error->message << '\n';
        return kExitError;
    }
    const auto& folders = std::get<fc::ValidatedFolders>(validation);

    const fc::ComparisonResult result =
        fc::FolderManager::compare(folders.folderA, folders.folderB, arguments.options);

    printFiles("identical", result.identical);
    printFiles("different", result.different);
    printFiles("only_a", result.onlyA);
    printFiles("only_b", result.onlyB);

    for (const fc::ComparisonError& error : result.errors) {
        std::cerr << "Could not compare " << fc::toUtf8(error.path) << ": " << error.message << '\n';
    }

    if (arguments.summary) {
        std::string separator;
        for (const fc::SummaryItem item : fc::kSummaryItems) {
            if (item == fc::SummaryItem::Skipped) {
                continue; // no action in the command line tool
            }
            std::cout << separator << fc::summaryText(item, result);
            separator = "  ";
        }
        std::cout << '\n';
    }

    return result.allDifferencesCount() == 0 ? EXIT_SUCCESS : kExitDifferences;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        return run(std::span(argv, static_cast<std::size_t>(argc)));
    }
    catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return kExitError;
    }
}
