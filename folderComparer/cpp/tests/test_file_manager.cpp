#include "TempDir.hpp"
#include "TestFramework.hpp"

#include "core/FileManager.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
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

} // namespace

// ---------------------------------------------------------------------------
// formatSize: values produced by the Python FileManager.format_size
// ---------------------------------------------------------------------------

FC_TEST(formatSize_matches_python_output)
{
    using fc::FileManager::formatSize;
    FC_CHECK_EQ(formatSize(0), std::string("0.0 B"));
    FC_CHECK_EQ(formatSize(1), std::string("1.0 B"));
    FC_CHECK_EQ(formatSize(1023), std::string("1023.0 B"));
    FC_CHECK_EQ(formatSize(1024), std::string("1.0 KB"));
    FC_CHECK_EQ(formatSize(1536), std::string("1.5 KB"));
    FC_CHECK_EQ(formatSize(1048575), std::string("1024.0 KB"));
    FC_CHECK_EQ(formatSize(1048576), std::string("1.0 MB"));
    FC_CHECK_EQ(formatSize(123456789), std::string("117.7 MB"));
    FC_CHECK_EQ(formatSize(1073741824), std::string("1.0 GB"));
    FC_CHECK_EQ(formatSize(1099511627776ULL), std::string("1.0 TB"));
    FC_CHECK_EQ(formatSize(1125899906842624ULL), std::string("1.0 PB"));
    FC_CHECK_EQ(formatSize(1152921504606846976ULL), std::string("1024.0 PB"));
    FC_CHECK_EQ(formatSize(UINTMAX_MAX), std::string("16384.0 PB"));
}

// ---------------------------------------------------------------------------
// getFiles
// ---------------------------------------------------------------------------

FC_TEST(getFiles_recursive_lists_every_file_relative_and_sorted)
{
    const TempDir dir;
    dir.write("b.txt", "b");
    dir.write("a.txt", "a");
    dir.write("sub/c.txt", "c");
    dir.write("sub/deeper/d.txt", "d");
    dir.makeFolder("empty");

    FC_CHECK_EQ(fc::FileManager::getFiles(dir.path(), true),
                paths({"a.txt", "b.txt", "sub/c.txt", "sub/deeper/d.txt"}));
}

FC_TEST(getFiles_non_recursive_ignores_sub_folders)
{
    const TempDir dir;
    dir.write("b.txt", "b");
    dir.write("a.txt", "a");
    dir.write("sub/c.txt", "c");

    FC_CHECK_EQ(fc::FileManager::getFiles(dir.path(), false), paths({"a.txt", "b.txt"}));
}

FC_TEST(getFiles_sorts_like_python_string_sort)
{
    // Python sorts "a-c" before "a/b" ('-' < '/'), path::operator< would not.
    const TempDir dir;
    dir.write("a/b", "1");
    dir.write("a-c", "2");

    FC_CHECK_EQ(fc::FileManager::getFiles(dir.path(), true), paths({"a-c", "a/b"}));
}

FC_TEST(getFiles_accepts_trailing_separator)
{
    const TempDir dir;
    dir.write("sub/c.txt", "c");
    fs::path withSeparator = dir.path();
    withSeparator += fs::path::preferred_separator;

    FC_CHECK_EQ(fc::FileManager::getFiles(withSeparator, true), paths({"sub/c.txt"}));
}

FC_TEST(getFiles_empty_folder)
{
    const TempDir dir;
    FC_CHECK(fc::FileManager::getFiles(dir.path(), true).empty());
    FC_CHECK(fc::FileManager::getFiles(dir.path(), false).empty());
}

FC_TEST(getFiles_missing_folder_throws)
{
    const TempDir dir;
    FC_CHECK_THROWS_AS((void)fc::FileManager::getFiles(dir / "missing", true), fs::filesystem_error);
    FC_CHECK_THROWS_AS((void)fc::FileManager::getFiles(dir / "missing", false), fs::filesystem_error);
}

FC_TEST(getFiles_handles_unicode_names)
{
    const TempDir dir;
    const fs::path name = fs::path(u8"café 日本.txt");
    dir.write(name, "x");

    const auto files = fc::FileManager::getFiles(dir.path(), true);
    FC_REQUIRE_EQ(files.size(), std::size_t{1});
    FC_CHECK(files.front() == name);
}

#if !defined(_WIN32)
FC_TEST(getFiles_follows_file_symlinks_but_not_folder_symlinks)
{
    const TempDir dir;
    dir.write("real/file.txt", "x");
    const fs::path root = dir.makeFolder("root");
    dir.write("root/own.txt", "y");
    fs::create_symlink(dir / "real/file.txt", root / "link.txt");
    fs::create_directory_symlink(dir / "real", root / "linked_folder");
    fs::create_symlink(dir / "nowhere", root / "broken");

    // Same as os.walk(followlinks=False): the symlinked folder is not walked.
    FC_CHECK_EQ(fc::FileManager::getFiles(root, true), paths({"link.txt", "own.txt"}));
    FC_CHECK_EQ(fc::FileManager::getFiles(root, false), paths({"link.txt", "own.txt"}));
}
#endif

// ---------------------------------------------------------------------------
// filesAreIdentical
// ---------------------------------------------------------------------------

FC_TEST(filesAreIdentical_different_sizes)
{
    const TempDir dir;
    const auto a = dir.write("a", "hello");
    const auto b = dir.write("b", "hello!");
    FC_CHECK(!fc::FileManager::filesAreIdentical(a, b, true));
    FC_CHECK(!fc::FileManager::filesAreIdentical(a, b, false));
}

FC_TEST(filesAreIdentical_same_size_different_content)
{
    const TempDir dir;
    const auto a = dir.write("a", "hello");
    const auto b = dir.write("b", "jello");
    FC_CHECK(!fc::FileManager::filesAreIdentical(a, b, true));
    // Shallow comparison only looks at the size.
    FC_CHECK(fc::FileManager::filesAreIdentical(a, b, false));
}

FC_TEST(filesAreIdentical_same_content)
{
    const TempDir dir;
    const auto a = dir.write("a", "same content");
    const auto b = dir.write("b", "same content");
    FC_CHECK(fc::FileManager::filesAreIdentical(a, b, true));
    FC_CHECK(fc::FileManager::filesAreIdentical(a, b, false));
}

FC_TEST(filesAreIdentical_empty_files)
{
    const TempDir dir;
    const auto a = dir.write("a", "");
    const auto b = dir.write("b", "");
    FC_CHECK(fc::FileManager::filesAreIdentical(a, b, true));
}

FC_TEST(filesAreIdentical_large_files_across_chunks)
{
    // 3 MiB + 7 bytes, so the comparison spans several 1 MiB chunks, with the
    // only difference in the last chunk, then exactly on a chunk boundary.
    const std::size_t size = (std::size_t{3} << 20U) + 7;
    const std::string content(size, 'x');
    const TempDir dir;
    const auto a = dir.write("a", content);
    const auto same = dir.write("same", content);

    std::string lastByte = content;
    lastByte.back() = 'y';
    const auto b = dir.write("b", lastByte);

    std::string boundary = content;
    boundary[std::size_t{2} << 20U] = 'z';
    const auto c = dir.write("c", boundary);

    FC_CHECK(fc::FileManager::filesAreIdentical(a, same, true));
    FC_CHECK(!fc::FileManager::filesAreIdentical(a, b, true));
    FC_CHECK(!fc::FileManager::filesAreIdentical(a, c, true));
}

FC_TEST(filesAreIdentical_exact_chunk_multiple)
{
    const std::string content(std::size_t{1} << 21U, 'q'); // exactly 2 chunks
    const TempDir dir;
    FC_CHECK(fc::FileManager::filesAreIdentical(dir.write("a", content), dir.write("b", content), true));
}

FC_TEST(filesAreIdentical_missing_file_throws)
{
    const TempDir dir;
    const auto a = dir.write("a", "x");
    FC_CHECK_THROWS_AS((void)fc::FileManager::filesAreIdentical(a, dir / "missing", true),
                       fs::filesystem_error);
}

FC_TEST(filesAreIdentical_honors_stop_token)
{
    const TempDir dir;
    const auto a = dir.write("a", "abc");
    const auto b = dir.write("b", "abc");
    std::stop_source source; // NOLINT(misc-const-correctness): request_stop() is not const everywhere
    source.request_stop();
    FC_CHECK_THROWS_AS((void)fc::FileManager::filesAreIdentical(a, b, true, source.get_token()),
                       fc::OperationCancelled);
    // A shallow comparison does not read the files and is not interrupted.
    FC_CHECK(fc::FileManager::filesAreIdentical(a, b, false, source.get_token()));
}

// ---------------------------------------------------------------------------
// copyFile
// ---------------------------------------------------------------------------

FC_TEST(copyFile_creates_parent_folders_and_copies_content)
{
    const TempDir dir;
    const auto source = dir.write("A/sub/file.txt", "payload");
    const auto destination = dir / "B/sub/deeper/file.txt";

    fc::FileManager::copyFile(source, destination);

    FC_REQUIRE(fs::exists(destination));
    FC_CHECK_EQ(fc::test::readFile(destination), std::string("payload"));
    FC_CHECK(!fs::exists(dir / "B/sub/deeper/file.txt.fc-partial"));
}

FC_TEST(copyFile_overwrites_existing_destination)
{
    const TempDir dir;
    const auto source = dir.write("A/file.txt", "new content");
    const auto destination = dir.write("B/file.txt", "old");

    fc::FileManager::copyFile(source, destination);
    FC_CHECK_EQ(fc::test::readFile(destination), std::string("new content"));
}

FC_TEST(copyFile_preserves_modification_time)
{
    const TempDir dir;
    const auto source = dir.write("A/file.txt", "x");
    const auto time = fs::file_time_type::clock::now() - std::chrono::hours(48);
    fs::last_write_time(source, time);

    const auto destination = dir / "B/file.txt";
    fc::FileManager::copyFile(source, destination);

    FC_CHECK(fs::last_write_time(destination) == fs::last_write_time(source));
}

FC_TEST(copyFile_missing_source_throws_with_python_message)
{
    const TempDir dir;
    try {
        fc::FileManager::copyFile(dir / "missing.txt", dir / "B/missing.txt");
        FC_CHECK(false);
    }
    catch (const fc::SourceMissingError& error) {
        const std::string message = error.what();
        FC_CHECK(message.starts_with("Source file no longer exists:\n\n"));
        FC_CHECK(message.find("missing.txt") != std::string::npos);
    }
    FC_CHECK(!fs::exists(dir / "B"));
}

FC_TEST(copyFile_failure_leaves_no_partial_file)
{
    const TempDir dir;
    const auto source = dir.write("A/file.txt", "x");
    dir.makeFolder("B/file.txt/child"); // destination is a non-empty folder: rename fails

    FC_CHECK_THROWS_AS(fc::FileManager::copyFile(source, dir / "B/file.txt"), fs::filesystem_error);
    FC_CHECK(!fs::exists(dir / "B/file.txt.fc-partial"));
}

// ---------------------------------------------------------------------------
// getFileInfo
// ---------------------------------------------------------------------------

FC_TEST(getFileInfo_reports_size_and_local_modification_time)
{
    const TempDir dir;
    const auto file = dir.write("file.bin", std::string(1536, 'x'));
    const auto info = fc::FileManager::getFileInfo(file);

    FC_CHECK_EQ(info.size, std::uintmax_t{1536});
    FC_CHECK_EQ(info.sizeText, std::string("1.5 KB"));
    FC_CHECK_EQ(info.modifiedText, fc::FileManager::formatLocalTime(info.modified));

    const auto age = std::chrono::system_clock::now() - info.modified;
    FC_CHECK(age < std::chrono::minutes(5));
    FC_CHECK(age > -std::chrono::minutes(5));
}

FC_TEST(formatLocalTime_has_python_strftime_layout)
{
    const std::string text = fc::FileManager::formatLocalTime(std::chrono::system_clock::now());
    // "%Y-%m-%d %H:%M:%S"
    FC_REQUIRE_EQ(text.size(), std::size_t{19});
    FC_CHECK_EQ(text[4], '-');
    FC_CHECK_EQ(text[7], '-');
    FC_CHECK_EQ(text[10], ' ');
    FC_CHECK_EQ(text[13], ':');
    FC_CHECK_EQ(text[16], ':');
}

FC_TEST(toUtf8_round_trips_unicode)
{
    const fs::path path(u8"dossier/été");
    FC_CHECK_EQ(fc::toUtf8(path), std::string("dossier/\xC3\xA9t\xC3\xA9"));
}
