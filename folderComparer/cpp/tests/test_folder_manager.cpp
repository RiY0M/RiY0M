#include "TempDir.hpp"
#include "TestFramework.hpp"

#include "core/FileManager.hpp"
#include "core/FolderManager.hpp"

#include <cstddef>
#include <filesystem>
#include <stop_token>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

namespace fs = std::filesystem;
using fc::test::TempDir;

namespace {

std::vector<fs::path> paths(std::initializer_list<const char*> items)
{
    std::vector<fs::path> result;
    for (const char* item : items) {
        result.emplace_back(fs::path(item).make_preferred());
    }
    return result;
}

/// A / B fixture covering every category.
struct Fixture {
    TempDir dir;
    fs::path a = dir.makeFolder("A");
    fs::path b = dir.makeFolder("B");

    Fixture()
    {
        dir.write("A/same.txt", "same");
        dir.write("B/same.txt", "same");
        dir.write("A/sub/same.txt", "nested");
        dir.write("B/sub/same.txt", "nested");
        dir.write("A/size.txt", "short");
        dir.write("B/size.txt", "longer content");
        dir.write("A/content.txt", "aaaa"); // same size, different bytes
        dir.write("B/content.txt", "bbbb");
        dir.write("A/only_a.txt", "a");
        dir.write("A/sub/only_a_nested.txt", "a");
        dir.write("B/only_b.txt", "b");
        dir.write("B/sub/deeper/only_b_nested.txt", "b");
    }
};

} // namespace

// ---------------------------------------------------------------------------
// compare
// ---------------------------------------------------------------------------

FC_TEST(compare_recursive_shallow)
{
    const Fixture fixture;
    const auto result = fc::FolderManager::compare(fixture.a, fixture.b, {.deepComparison = false, .recursive = true});

    FC_CHECK_EQ(result.identical, paths({"content.txt", "same.txt", "sub/same.txt"}));
    FC_CHECK_EQ(result.different, paths({"size.txt"}));
    FC_CHECK_EQ(result.onlyA, paths({"only_a.txt", "sub/only_a_nested.txt"}));
    FC_CHECK_EQ(result.onlyB, paths({"only_b.txt", "sub/deeper/only_b_nested.txt"}));
    FC_CHECK(result.errors.empty());
}

FC_TEST(compare_recursive_deep)
{
    const Fixture fixture;
    const auto result = fc::FolderManager::compare(fixture.a, fixture.b, {.deepComparison = true, .recursive = true});

    FC_CHECK_EQ(result.identical, paths({"same.txt", "sub/same.txt"}));
    FC_CHECK_EQ(result.different, paths({"content.txt", "size.txt"}));
    FC_CHECK_EQ(result.totalCount(), std::size_t{8});
    FC_CHECK_EQ(result.allDifferencesCount(), std::size_t{6});
}

FC_TEST(compare_non_recursive)
{
    const Fixture fixture;
    const auto result = fc::FolderManager::compare(fixture.a, fixture.b, {.deepComparison = true, .recursive = false});

    FC_CHECK_EQ(result.identical, paths({"same.txt"}));
    FC_CHECK_EQ(result.different, paths({"content.txt", "size.txt"}));
    FC_CHECK_EQ(result.onlyA, paths({"only_a.txt"}));
    FC_CHECK_EQ(result.onlyB, paths({"only_b.txt"}));
}

FC_TEST(compare_reports_progress_for_each_common_file)
{
    const Fixture fixture;
    std::vector<std::tuple<std::size_t, std::size_t, fs::path>> calls;
    const auto result = fc::FolderManager::compare(
        fixture.a, fixture.b, {.deepComparison = true, .recursive = true},
        [&calls](std::size_t current, std::size_t total, const fs::path& path) {
            calls.emplace_back(current, total, path);
        });

    const auto expected = paths({"content.txt", "same.txt", "size.txt", "sub/same.txt"});
    FC_REQUIRE_EQ(calls.size(), expected.size());
    for (std::size_t index = 0; index < calls.size(); ++index) {
        FC_CHECK_EQ(std::get<0>(calls[index]), index + 1);
        FC_CHECK_EQ(std::get<1>(calls[index]), expected.size());
        FC_CHECK(std::get<2>(calls[index]) == expected[index]);
    }
    FC_CHECK_EQ(result.identicalCount() + result.differentCount(), expected.size());
}

FC_TEST(compare_empty_folders)
{
    const TempDir dir;
    const auto result = fc::FolderManager::compare(dir.makeFolder("A"), dir.makeFolder("B"), {});
    FC_CHECK_EQ(result.totalCount(), std::size_t{0});
}

FC_TEST(compare_can_be_cancelled)
{
    const Fixture fixture;
    std::stop_source source;
    std::size_t calls = 0;
    FC_CHECK_THROWS_AS((void)fc::FolderManager::compare(
                           fixture.a, fixture.b, {.deepComparison = true, .recursive = true},
                           [&](std::size_t, std::size_t, const fs::path&) {
                               ++calls;
                               source.request_stop();
                           },
                           source.get_token()),
                       fc::OperationCancelled);
    FC_CHECK_EQ(calls, std::size_t{1});
}

FC_TEST(compare_missing_folder_throws)
{
    const TempDir dir;
    FC_CHECK_THROWS_AS((void)fc::FolderManager::compare(dir / "missing", dir.makeFolder("B"), {}),
                       fs::filesystem_error);
}

#if !defined(_WIN32)
FC_TEST(compare_unreadable_file_is_reported_as_different_with_error)
{
    const TempDir dir;
    dir.write("A/locked.txt", "same");
    const auto locked = dir.write("B/locked.txt", "same");
    fs::permissions(locked, fs::perms::none);

    // root can read anything: the scenario cannot be reproduced then.
    if (std::ifstream(locked).is_open()) {
        fs::permissions(locked, fs::perms::owner_all);
        return;
    }

    const auto result = fc::FolderManager::compare(dir / "A", dir / "B", {.deepComparison = true, .recursive = true});
    fs::permissions(locked, fs::perms::owner_all);

    FC_CHECK_EQ(result.different, paths({"locked.txt"}));
    FC_REQUIRE_EQ(result.errors.size(), std::size_t{1});
    FC_CHECK(result.errors.front().path == fs::path("locked.txt"));
    FC_CHECK(!result.errors.front().message.empty());
}
#endif

// ---------------------------------------------------------------------------
// Folder paths
// ---------------------------------------------------------------------------

FC_TEST(normalizeFolderPath_is_absolute_normal_without_trailing_separator)
{
    const TempDir dir;
    const fs::path folder = dir.makeFolder("x/y");

    fs::path messy = dir.path() / "x" / "." / "z" / ".." / "y";
    messy += fs::path::preferred_separator;

    const fs::path normalized = fc::FolderManager::normalizeFolderPath(messy);
    FC_CHECK(normalized.is_absolute());
    fs::path expected = folder;
    FC_CHECK_EQ(normalized, expected.make_preferred().lexically_normal());
    FC_CHECK(normalized.has_filename());
}

FC_TEST(normalizeFolderPath_keeps_root_and_handles_relative)
{
    const fs::path root = fs::current_path().root_path();
    fs::path expectedRoot = root;
    FC_CHECK_EQ(fc::FolderManager::normalizeFolderPath(root), expectedRoot.make_preferred());
    FC_CHECK_EQ(fc::FolderManager::normalizeFolderPath("."),
                fs::current_path().make_preferred().lexically_normal());
    FC_CHECK(fc::FolderManager::normalizeFolderPath("").empty());
}

FC_TEST(isFolderPathValid_cases)
{
    const TempDir dir;
    FC_CHECK(!fc::FolderManager::isFolderPathValid(""));
    FC_CHECK(!fc::FolderManager::isFolderPathValid(dir / "missing"));
    FC_CHECK(!fc::FolderManager::isFolderPathValid(dir.write("file.txt", "x")));
    FC_CHECK(fc::FolderManager::isFolderPathValid(dir.makeFolder("folder")));
}

FC_TEST(checkSameFolders_cases)
{
    const TempDir dir;
    const fs::path a = dir.makeFolder("A");
    const fs::path b = dir.makeFolder("B");

    FC_CHECK(fc::FolderManager::checkSameFolders(a, a));
    FC_CHECK(fc::FolderManager::checkSameFolders(a, dir.path() / "B" / ".." / "A"));
    FC_CHECK(!fc::FolderManager::checkSameFolders(a, b));
    // The Python version crashed here (os.path.samefile on a missing / None path).
    FC_CHECK(!fc::FolderManager::checkSameFolders(a, dir / "missing"));
    FC_CHECK(!fc::FolderManager::checkSameFolders("", a));
}

FC_TEST(trimmed_removes_surrounding_whitespace_only)
{
    FC_CHECK_EQ(fc::FolderManager::trimmed("  /tmp/my folder \t\n"), fs::path("/tmp/my folder"));
    FC_CHECK_EQ(fc::FolderManager::trimmed("   "), fs::path());
    FC_CHECK_EQ(fc::FolderManager::trimmed(""), fs::path());
}

// ---------------------------------------------------------------------------
// validateFolders: the checks and messages of App.get_folder_path
// ---------------------------------------------------------------------------

namespace {

fc::FolderValidationError expectError(const fc::FolderValidation& validation)
{
    const auto* error = std::get_if<fc::FolderValidationError>(&validation);
    FC_REQUIRE(error != nullptr);
    return *error;
}

} // namespace

FC_TEST(validateFolders_rejects_empty_folder)
{
    const TempDir dir;
    const auto error = expectError(fc::FolderManager::validateFolders("  ", dir.path()));
    FC_CHECK_EQ(error.title, std::string("Invalid folder"));
    FC_CHECK_EQ(error.message, std::string("Folder must not be empty."));

    const auto errorB = expectError(fc::FolderManager::validateFolders(dir.path(), ""));
    FC_CHECK_EQ(errorB.message, std::string("Folder must not be empty."));
}

FC_TEST(validateFolders_rejects_missing_folder)
{
    const TempDir dir;
    const auto error = expectError(fc::FolderManager::validateFolders(dir.path(), dir / "missing"));
    FC_CHECK_EQ(error.title, std::string("Invalid folder"));
    FC_CHECK_EQ(error.message, std::string("Folder does not exist."));
}

FC_TEST(validateFolders_rejects_same_folder)
{
    const TempDir dir;
    const fs::path a = dir.makeFolder("A");
    const auto error = expectError(fc::FolderManager::validateFolders(a, dir.path() / "A" / "."));
    FC_CHECK_EQ(error.title, std::string("Invalid folders"));
    FC_CHECK_EQ(error.message, std::string("Folder A and Folder B are the same folder."));
}

FC_TEST(validateFolders_accepts_and_normalizes)
{
    const TempDir dir;
    const fs::path a = dir.makeFolder("A");
    const fs::path b = dir.makeFolder("B");

    fs::path paddedB = " ";
    paddedB += b;
    paddedB += "  ";

    const auto validation = fc::FolderManager::validateFolders(a, paddedB);
    const auto* folders = std::get_if<fc::ValidatedFolders>(&validation);
    FC_REQUIRE(folders != nullptr);
    FC_CHECK_EQ(folders->folderA, fc::FolderManager::normalizeFolderPath(a));
    FC_CHECK_EQ(folders->folderB, fc::FolderManager::normalizeFolderPath(b));
}
