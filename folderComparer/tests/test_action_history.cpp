#include "TempDir.hpp"
#include "TestFramework.hpp"

#include "core/ActionHistory.hpp"
#include "core/FileManager.hpp"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using fc::test::TempDir;

namespace {

struct HistoryFixture {
    TempDir dir;
    fs::path a = dir.makeFolder("A");
    fs::path b = dir.makeFolder("B");
    fc::ActionHistory history;
};

bool trashExists(const fs::path& root)
{
    return fs::exists(root / fc::kTrashFolderName);
}

} // namespace

// ---------------------------------------------------------------------------
// Copy
// ---------------------------------------------------------------------------

FC_TEST(history_copy_new_file_then_undo_removes_it_and_created_parents)
{
    HistoryFixture fixture;
    fixture.dir.write("A/x/y/file.txt", "payload");

    const auto id = fixture.history.copy(fixture.a / "x/y/file.txt", fixture.b / "x/y/file.txt", fixture.b);
    FC_CHECK_EQ(fc::test::readFile(fixture.b / "x/y/file.txt"), std::string("payload"));
    FC_CHECK(!fs::exists(fixture.b / "x/y/file.txt.fc-partial"));
    FC_CHECK(!fixture.history.holdsBackups()); // nothing would be lost by discarding

    fixture.history.undo(id);
    FC_CHECK(!fs::exists(fixture.b / "x")); // the parents created by the copy are removed
    FC_CHECK(fs::exists(fixture.b));
    FC_CHECK_EQ(fixture.history.size(), std::size_t{0});
}

FC_TEST(history_copy_over_existing_file_keeps_it_for_undo)
{
    HistoryFixture fixture;
    fixture.dir.write("A/file.txt", "new version");
    const auto old = fixture.dir.write("B/file.txt", "old");
    fs::last_write_time(old, fs::file_time_type::clock::now() - std::chrono::hours(72));
    // Read back: the file system rounds the time (100 ns on NTFS).
    const auto oldTime = fs::last_write_time(old);

    const auto id = fixture.history.copy(fixture.a / "file.txt", fixture.b / "file.txt", fixture.b);
    FC_CHECK_EQ(fc::test::readFile(fixture.b / "file.txt"), std::string("new version"));
    FC_CHECK(fixture.history.holdsBackups());
    // The backup lives in B's trash, which the comparison ignores.
    FC_CHECK(trashExists(fixture.b));
    FC_CHECK_EQ(fc::FileManager::getFiles(fixture.b, true), (std::vector<fs::path>{"file.txt"}));

    fixture.history.undo(id);
    FC_CHECK_EQ(fc::test::readFile(fixture.b / "file.txt"), std::string("old"));
    FC_CHECK(fs::last_write_time(fixture.b / "file.txt") == oldTime);
    FC_CHECK(!fixture.history.holdsBackups());
}

FC_TEST(history_copy_whole_folder_then_undo)
{
    HistoryFixture fixture;
    fixture.dir.write("A/photos/2023/a.jpg", "a");
    fixture.dir.write("A/photos/b.jpg", "b");
    fixture.dir.makeFolder("A/photos/empty");

    const auto id = fixture.history.copy(fixture.a / "photos", fixture.b / "photos", fixture.b);
    const auto listing = fc::FileManager::listEntries(fixture.b, true);
    FC_CHECK_EQ(listing.files.size(), std::size_t{2});
    FC_CHECK_EQ(listing.folders.size(), std::size_t{3});

    fixture.history.undo(id);
    FC_CHECK(!fs::exists(fixture.b / "photos"));
}

FC_TEST(history_undo_of_sibling_copies_in_any_order_prunes_shared_parents)
{
    HistoryFixture fixture;
    fixture.dir.write("A/d/f1.txt", "1");
    fixture.dir.write("A/d/f2.txt", "2");

    const auto first = fixture.history.copy(fixture.a / "d/f1.txt", fixture.b / "d/f1.txt", fixture.b);
    const auto second = fixture.history.copy(fixture.a / "d/f2.txt", fixture.b / "d/f2.txt", fixture.b);

    fixture.history.undo(first);
    FC_CHECK(fs::exists(fixture.b / "d/f2.txt")); // still needed by the second copy
    fixture.history.undo(second);
    FC_CHECK(!fs::exists(fixture.b / "d")); // created by the first copy, now empty
}

FC_TEST(history_copy_missing_source_throws_and_leaves_nothing)
{
    HistoryFixture fixture;
    FC_CHECK_THROWS_AS((void)fixture.history.copy(fixture.a / "x/missing.txt", fixture.b / "x/missing.txt", fixture.b),
                       fc::SourceMissingError);
    FC_CHECK(!fs::exists(fixture.b / "x"));
    FC_CHECK_EQ(fixture.history.size(), std::size_t{0});
}

FC_TEST(history_copy_ignores_a_stale_partial_copy)
{
    HistoryFixture fixture;
    fixture.dir.write("A/file.txt", "new");
    fixture.dir.write("B/file.txt.fc-partial/blocker/x", "x"); // stale partial: removed first
    fixture.dir.write("B/file.txt", "old");

    const auto id = fixture.history.copy(fixture.a / "file.txt", fixture.b / "file.txt", fixture.b);
    FC_CHECK_EQ(fc::test::readFile(fixture.b / "file.txt"), std::string("new"));
    FC_CHECK(!fs::exists(fixture.b / "file.txt.fc-partial"));
    fixture.history.undo(id);
    FC_CHECK_EQ(fc::test::readFile(fixture.b / "file.txt"), std::string("old"));
}

// ---------------------------------------------------------------------------
// Delete
// ---------------------------------------------------------------------------

FC_TEST(history_delete_file_then_undo_restores_it)
{
    HistoryFixture fixture;
    const auto file = fixture.dir.write("A/sub/file.txt", "content");
    fs::last_write_time(file, fs::file_time_type::clock::now() - std::chrono::hours(5));
    const auto time = fs::last_write_time(file); // as stored by the file system

    const auto id = fixture.history.remove(file, fixture.a);
    FC_CHECK(!fs::exists(file));
    FC_CHECK(fixture.history.holdsBackups());
    FC_CHECK(fc::FileManager::getFiles(fixture.a, true).empty()); // the trash is never listed

    fixture.history.undo(id);
    FC_CHECK_EQ(fc::test::readFile(file), std::string("content"));
    FC_CHECK(fs::last_write_time(file) == time);
    FC_CHECK(!fixture.history.holdsBackups());
}

FC_TEST(history_delete_folder_then_undo_restores_everything)
{
    HistoryFixture fixture;
    fixture.dir.write("A/folder/a.txt", "a");
    fixture.dir.write("A/folder/sub/b.txt", "b");
    fixture.dir.makeFolder("A/folder/empty");

    const auto id = fixture.history.remove(fixture.a / "folder", fixture.a);
    FC_CHECK(!fs::exists(fixture.a / "folder"));

    fixture.history.undo(id);
    FC_CHECK_EQ(fc::test::readFile(fixture.a / "folder/sub/b.txt"), std::string("b"));
    FC_CHECK(fs::is_directory(fixture.a / "folder/empty"));
}

FC_TEST(history_undo_delete_refuses_to_overwrite_a_new_file)
{
    HistoryFixture fixture;
    const auto file = fixture.dir.write("A/file.txt", "original");
    const auto id = fixture.history.remove(file, fixture.a);
    fixture.dir.write("A/file.txt", "created meanwhile");

    FC_CHECK_THROWS_AS(fixture.history.undo(id), std::runtime_error);
    FC_CHECK_EQ(fc::test::readFile(file), std::string("created meanwhile"));
    FC_CHECK(fixture.history.contains(id)); // can be retried

    fs::remove(file);
    fixture.history.undo(id);
    FC_CHECK_EQ(fc::test::readFile(file), std::string("original"));
}

FC_TEST(history_delete_missing_file_throws)
{
    HistoryFixture fixture;
    FC_CHECK_THROWS_AS((void)fixture.history.remove(fixture.a / "missing", fixture.a), fc::SourceMissingError);
}

// ---------------------------------------------------------------------------
// Discard
// ---------------------------------------------------------------------------

FC_TEST(history_discard_makes_actions_permanent_and_empties_the_trash)
{
    HistoryFixture fixture;
    const auto file = fixture.dir.write("A/file.txt", "x");
    const auto id = fixture.history.remove(file, fixture.a);
    FC_CHECK(trashExists(fixture.a));

    fixture.history.discard();
    FC_CHECK(!trashExists(fixture.a));
    FC_CHECK(!fs::exists(file));
    FC_CHECK_EQ(fixture.history.size(), std::size_t{0});
    FC_CHECK_THROWS_AS(fixture.history.undo(id), std::invalid_argument);
}

FC_TEST(history_destructor_empties_the_trash)
{
    const TempDir dir;
    const auto file = dir.write("A/file.txt", "x");
    {
        fc::ActionHistory history;
        (void)history.remove(file, dir / "A");
        FC_CHECK(trashExists(dir / "A"));
    }
    FC_CHECK(!trashExists(dir / "A"));
}

FC_TEST(history_discard_keeps_trash_of_other_sessions)
{
    // A trash left by a crashed session is not ours to empty.
    HistoryFixture fixture;
    fixture.dir.write(fs::path("A") / fc::kTrashFolderName / "othersession/1/keep.txt", "keep");
    const auto file = fixture.dir.write("A/file.txt", "x");
    (void)fixture.history.remove(file, fixture.a);

    fixture.history.discard();
    FC_CHECK(fs::exists(fixture.a / fc::kTrashFolderName / "othersession/1/keep.txt"));
}
