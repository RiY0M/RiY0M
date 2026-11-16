// ComparisonSession: rows (flat files, single row per missing folder, expandable
// tree), copy / delete actions and their undo.

#include "TempDir.hpp"
#include "TestFramework.hpp"

#include "core/ComparisonSession.hpp"
#include "core/FileManager.hpp"
#include "core/FolderManager.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using fc::RowCommand;
using fc::test::TempDir;

namespace {

fs::path native(const char* path)
{
    return fs::path(path).make_preferred();
}

struct SessionFixture {
    TempDir dir;
    fs::path a = dir.makeFolder("A");
    fs::path b = dir.makeFolder("B");
    fc::ComparisonSession session;

    SessionFixture()
    {
        dir.write("A/same.txt", "same");
        dir.write("B/same.txt", "same");
        dir.write("A/diff.txt", "version A");
        dir.write("B/diff.txt", "version B!");
        dir.write("A/only_a.txt", "only in A");
        dir.write("A/photos/a.jpg", "a");
        dir.write("A/photos/2023/b.jpg", "b");
        dir.write("A/photos/2023/c.jpg", "c");
        dir.makeFolder("A/photos/empty");
        dir.write("A/common/kept.txt", "kept");
        dir.write("B/common/kept.txt", "kept");
        dir.write("B/common/new_in_b.txt", "new");
        dir.write("B/only_b.txt", "only in B");
        dir.write("B/z_only_b.txt", "only in B too");
        compare();
    }

    void compare()
    {
        session.setResult(a, b, fc::FolderManager::compare(a, b, {.deepComparison = true, .recursive = true}));
    }

    [[nodiscard]] fc::Row row(const char* path) const
    {
        const auto found = session.row(native(path));
        FC_REQUIRE(found.has_value());
        return *found;
    }
};

/// "path=STATUS" for each row, with depth shown as indentation.
std::vector<std::string> describe(const std::vector<fc::Row>& rows)
{
    std::vector<std::string> texts;
    texts.reserve(rows.size());
    for (const auto& row : rows) {
        texts.push_back(std::string(row.depth * 2, ' ') + fc::toUtf8(row.displayName.generic_u8string()) + "=" +
                        row.statusText);
    }
    return texts;
}

/// Every command reachable from the row, menus included, in display order.
std::vector<RowCommand> commands(const fc::Row& row)
{
    std::vector<RowCommand> result;
    for (const auto& button : row.buttons) {
        for (const RowCommand command : button.commands()) {
            result.push_back(command);
        }
    }
    return result;
}

std::vector<std::string> buttonLabels(const fc::Row& row)
{
    std::vector<std::string> labels;
    labels.reserve(row.buttons.size());
    for (const auto& button : row.buttons) {
        labels.push_back(button.label);
    }
    return labels;
}

} // namespace

FC_TEST(session_starts_empty)
{
    const fc::ComparisonSession session;
    FC_CHECK(!session.hasResult());
    FC_CHECK(session.rows(fc::Filter::AllDifferences).empty());
    FC_CHECK(!session.row("anything").has_value());
    FC_CHECK(!session.canUndo());
}

FC_TEST(session_all_differences_are_sorted_by_name_across_categories)
{
    const SessionFixture fixture;
    FC_CHECK_EQ(describe(fixture.session.rows(fc::Filter::AllDifferences)),
                (std::vector<std::string>{"common/new_in_b.txt=ONLY B", "diff.txt=DIFFERENT", "only_a.txt=ONLY A",
                                          "only_b.txt=ONLY B", "photos/=ONLY A", "z_only_b.txt=ONLY B"}));
    FC_CHECK_EQ(describe(fixture.session.rows(fc::Filter::Different)),
                (std::vector<std::string>{"diff.txt=DIFFERENT"}));
}

FC_TEST(session_missing_folder_is_a_single_collapsed_row)
{
    const SessionFixture fixture;
    const fc::Row photos = fixture.row("photos");
    FC_CHECK(photos.kind == fc::EntryKind::Folder);
    FC_CHECK(photos.expandable);
    FC_CHECK(!photos.expanded);
    FC_CHECK_EQ(photos.detail, std::string("D(2) F(3)"));
    FC_CHECK_EQ(describe(fixture.session.rows(fc::Filter::OnlyA)),
                (std::vector<std::string>{"only_a.txt=ONLY A", "photos/=ONLY A"}));
}

FC_TEST(session_expanding_folders_shows_sub_rows_folders_first)
{
    SessionFixture fixture;
    fixture.session.toggleExpanded(native("photos"));
    FC_CHECK_EQ(describe(fixture.session.rows(fc::Filter::OnlyA)),
                (std::vector<std::string>{"only_a.txt=ONLY A", "photos/=ONLY A", "  2023/=ONLY A",
                                          "  empty/=ONLY A", "  a.jpg=ONLY A"}));
    FC_CHECK(!fixture.row("photos/empty").expandable); // nothing inside

    fixture.session.toggleExpanded(native("photos/2023"));
    FC_CHECK_EQ(describe(fixture.session.rows(fc::Filter::OnlyA)),
                (std::vector<std::string>{"only_a.txt=ONLY A", "photos/=ONLY A", "  2023/=ONLY A",
                                          "    b.jpg=ONLY A", "    c.jpg=ONLY A", "  empty/=ONLY A",
                                          "  a.jpg=ONLY A"}));
    FC_CHECK_EQ(fixture.row("photos/2023/b.jpg").depth, std::size_t{2});

    fixture.session.toggleExpanded(native("photos")); // collapse: the sub-rows disappear
    FC_CHECK_EQ(describe(fixture.session.rows(fc::Filter::OnlyA)),
                (std::vector<std::string>{"only_a.txt=ONLY A", "photos/=ONLY A"}));
    FC_CHECK_THROWS_AS(fixture.session.toggleExpanded("only_a.txt"), std::invalid_argument);
}

FC_TEST(session_rows_offer_copy_and_delete_buttons)
{
    const SessionFixture fixture;
    FC_CHECK_EQ(commands(fixture.row("diff.txt")),
                (std::vector<RowCommand>{RowCommand::CopyAToB, RowCommand::CopyBToA, RowCommand::DeleteA,
                                         RowCommand::DeleteB, RowCommand::DeleteBoth, RowCommand::Skip}));
    FC_CHECK_EQ(commands(fixture.row("only_a.txt")),
                (std::vector<RowCommand>{RowCommand::CopyAToB, RowCommand::DeleteA, RowCommand::Skip}));
    FC_CHECK_EQ(commands(fixture.row("photos")),
                (std::vector<RowCommand>{RowCommand::CopyAToB, RowCommand::DeleteA, RowCommand::Skip}));
    FC_CHECK_EQ(commands(fixture.row("only_b.txt")),
                (std::vector<RowCommand>{RowCommand::CopyBToA, RowCommand::DeleteB, RowCommand::Skip}));
    FC_CHECK_EQ(fixture.row("only_a.txt").buttons.front().label, std::string("Copy → B"));
    FC_CHECK_EQ(fixture.row("diff.txt").buttons.back().label, std::string("Skip"));
}

FC_TEST(session_different_rows_group_copy_and_delete_in_menus)
{
    const SessionFixture fixture;
    const fc::Row row = fixture.row("diff.txt");
    FC_CHECK_EQ(buttonLabels(row), (std::vector<std::string>{"Copy", "Delete", "Skip"}));

    const fc::RowButton& copy = row.buttons.at(0);
    FC_CHECK_EQ(copy.choices, (std::vector<fc::RowChoice>{{"A \u2192 B", RowCommand::CopyAToB},
                                                          {"B \u2192 A", RowCommand::CopyBToA}}));
    FC_CHECK(fc::commandStyle(copy.command) == fc::actionStyle(fc::FileAction::Copy)); // green

    const fc::RowButton& remove = row.buttons.at(1);
    FC_CHECK_EQ(remove.choices, (std::vector<fc::RowChoice>{{"Delete A", RowCommand::DeleteA},
                                                            {"Delete B", RowCommand::DeleteB},
                                                            {"Delete both", RowCommand::DeleteBoth}}));
    FC_CHECK(fc::commandStyle(remove.command) == fc::actionStyle(fc::FileAction::Delete)); // red

    FC_CHECK(row.buttons.at(2).choices.empty()); // Skip: a plain button
    // One-sided rows keep plain buttons.
    for (const auto& button : fixture.row("only_a.txt").buttons) {
        FC_CHECK(button.choices.empty());
    }
}

FC_TEST(session_rows_show_the_file_extension)
{
    SessionFixture fixture;
    FC_CHECK_EQ(fixture.row("diff.txt").extension, std::string("TXT"));
    fixture.session.toggleExpanded(native("photos"));
    FC_CHECK_EQ(fixture.row("photos/a.jpg").extension, std::string("JPG"));
    FC_CHECK(fixture.row("photos").extension.empty()); // folders show their content instead
}

FC_TEST(session_copy_file_marks_row_and_undo_reverts)
{
    SessionFixture fixture;
    fixture.session.apply("diff.txt", RowCommand::CopyAToB);
    FC_CHECK_EQ(fc::test::readFile(fixture.b / "diff.txt"), std::string("version A"));

    const fc::Row copied = fixture.row("diff.txt");
    FC_CHECK_EQ(copied.statusText, std::string("COPIED → B"));
    FC_CHECK(copied.action == RowCommand::CopyAToB);
    FC_CHECK_EQ(commands(copied), (std::vector<RowCommand>{RowCommand::Undo}));
    FC_CHECK(copied.status == fc::FileStatus::Different); // the category is kept

    fixture.session.apply("diff.txt", RowCommand::Undo);
    FC_CHECK_EQ(fc::test::readFile(fixture.b / "diff.txt"), std::string("version B!"));
    FC_CHECK_EQ(fixture.row("diff.txt").statusText, std::string("DIFFERENT"));
    FC_CHECK_EQ(fixture.row("diff.txt").buttons.size(), std::size_t{3});
}

FC_TEST(session_delete_different_file_on_either_side_and_undo)
{
    SessionFixture fixture;
    fixture.session.apply("diff.txt", RowCommand::DeleteA);
    FC_CHECK(!fs::exists(fixture.a / "diff.txt"));
    FC_CHECK(fs::exists(fixture.b / "diff.txt"));
    FC_CHECK_EQ(fixture.row("diff.txt").statusText, std::string("DELETED A"));
    FC_CHECK(fixture.session.hasUndoableBackups());

    fixture.session.apply("diff.txt", RowCommand::Undo);
    FC_CHECK_EQ(fc::test::readFile(fixture.a / "diff.txt"), std::string("version A"));
    FC_CHECK(!fixture.session.hasUndoableBackups());

    fixture.session.apply("diff.txt", RowCommand::DeleteB);
    FC_CHECK(!fs::exists(fixture.b / "diff.txt"));
    FC_CHECK_EQ(fixture.row("diff.txt").statusText, std::string("DELETED B"));
}

FC_TEST(session_copy_missing_folder_with_its_content_and_undo)
{
    SessionFixture fixture;
    fixture.session.toggleExpanded(native("photos"));
    fixture.session.apply(native("photos"), RowCommand::CopyAToB);

    FC_CHECK_EQ(fc::test::readFile(fixture.b / "photos/2023/c.jpg"), std::string("c"));
    FC_CHECK(fs::is_directory(fixture.b / "photos/empty"));

    // The folder row carries the action, its content shows it without buttons.
    const auto rows = fixture.session.rows(fc::Filter::OnlyA);
    FC_CHECK_EQ(describe(rows), (std::vector<std::string>{"only_a.txt=ONLY A", "photos/=COPIED → B",
                                                          "  2023/=COPIED → B", "  empty/=COPIED → B",
                                                          "  a.jpg=COPIED → B"}));
    FC_CHECK_EQ(commands(rows[1]), (std::vector<RowCommand>{RowCommand::Undo}));
    FC_CHECK(rows[2].inherited);
    FC_CHECK(rows[4].buttons.empty());
    FC_CHECK_THROWS_AS(fixture.session.apply(native("photos/a.jpg"), RowCommand::DeleteA), std::logic_error);

    fixture.session.apply(native("photos"), RowCommand::Undo);
    FC_CHECK(!fs::exists(fixture.b / "photos"));
    FC_CHECK_EQ(fixture.row("photos/a.jpg").statusText, std::string("ONLY A"));
}

FC_TEST(session_deleted_row_is_red)
{
    SessionFixture fixture;
    fixture.session.apply("only_b.txt", RowCommand::DeleteB);
    const fc::Row row = fixture.row("only_b.txt");
    FC_CHECK_EQ(row.style.color, fc::colors::kRed);
    FC_CHECK_EQ(row.style.background, fc::colors::kRedBackground);
}

FC_TEST(session_skip_marks_row_grey_and_touches_nothing)
{
    SessionFixture fixture;
    const auto timeA = fs::last_write_time(fixture.a / "diff.txt");
    const auto timeB = fs::last_write_time(fixture.b / "diff.txt");

    fixture.session.apply("diff.txt", RowCommand::Skip);
    const fc::Row skipped = fixture.row("diff.txt");
    FC_CHECK_EQ(skipped.statusText, std::string("SKIPPED"));
    FC_CHECK_EQ(skipped.style.color, fc::colors::kGrey);
    FC_CHECK_EQ(commands(skipped), (std::vector<RowCommand>{RowCommand::Undo}));
    FC_CHECK_EQ(fc::test::readFile(fixture.a / "diff.txt"), std::string("version A"));
    FC_CHECK_EQ(fc::test::readFile(fixture.b / "diff.txt"), std::string("version B!"));
    FC_CHECK(fs::last_write_time(fixture.a / "diff.txt") == timeA);
    FC_CHECK(fs::last_write_time(fixture.b / "diff.txt") == timeB);
    FC_CHECK(!fs::exists(fixture.a / fc::kTrashFolderName));
    FC_CHECK(!fs::exists(fixture.b / fc::kTrashFolderName));
    FC_CHECK(!fixture.session.hasUndoableBackups()); // nothing to lose

    fixture.session.apply("diff.txt", RowCommand::Undo);
    FC_CHECK_EQ(fixture.row("diff.txt").statusText, std::string("DIFFERENT"));
}

FC_TEST(session_skip_folder_is_inherited_by_its_content)
{
    SessionFixture fixture;
    fixture.session.toggleExpanded(native("photos"));
    fixture.session.apply(native("photos"), RowCommand::Skip);

    FC_CHECK_EQ(describe(fixture.session.rows(fc::Filter::OnlyA)),
                (std::vector<std::string>{"only_a.txt=ONLY A", "photos/=SKIPPED", "  2023/=SKIPPED",
                                          "  empty/=SKIPPED", "  a.jpg=SKIPPED"}));
    FC_CHECK(fixture.row("photos/a.jpg").buttons.empty());
    FC_CHECK(fs::exists(fixture.a / "photos/a.jpg"));
    FC_CHECK(!fs::exists(fixture.b / "photos"));
}

FC_TEST(session_skipped_filter_and_counts)
{
    SessionFixture fixture;
    FC_CHECK(fixture.session.summaryCounts(fc::SummaryItem::Skipped) == fc::EntryCounts{});
    FC_CHECK(fixture.session.rows(fc::Filter::Skipped).empty());

    fixture.session.toggleExpanded(native("photos"));
    fixture.session.apply(native("photos/2023"), RowCommand::Skip); // a folder with 2 files
    fixture.session.apply("only_b.txt", RowCommand::Skip);
    fixture.session.apply("diff.txt", RowCommand::Skip);
    fixture.session.apply("only_a.txt", RowCommand::CopyAToB); // not a skip

    FC_CHECK(fixture.session.summaryCounts(fc::SummaryItem::Skipped) == (fc::EntryCounts{.files = 4, .folders = 1}));
    // Skipped rows at the top level with their full path, sorted; folders expand as usual.
    FC_CHECK_EQ(describe(fixture.session.rows(fc::Filter::Skipped)),
                (std::vector<std::string>{"diff.txt=SKIPPED", "only_b.txt=SKIPPED", "photos/2023/=SKIPPED"}));
    fixture.session.toggleExpanded(native("photos/2023"));
    FC_CHECK_EQ(describe(fixture.session.rows(fc::Filter::Skipped)),
                (std::vector<std::string>{"diff.txt=SKIPPED", "only_b.txt=SKIPPED", "photos/2023/=SKIPPED",
                                          "  b.jpg=SKIPPED", "  c.jpg=SKIPPED"}));

    // Skipping changes nothing on disk, so no other count moves because of it.
    FC_CHECK(fixture.session.summaryCounts(fc::SummaryItem::OnlyB) ==
             fixture.session.result().counts(fc::FileStatus::OnlyB));

    fixture.session.apply("diff.txt", RowCommand::Undo);
    FC_CHECK(fixture.session.summaryCounts(fc::SummaryItem::Skipped) == (fc::EntryCounts{.files = 3, .folders = 1}));
    FC_CHECK_EQ(fixture.session.rows(fc::Filter::Skipped).size(), std::size_t{4});
}

FC_TEST(session_undo_last_mixes_skips_and_file_actions)
{
    SessionFixture fixture;
    fixture.session.apply("only_a.txt", RowCommand::CopyAToB);
    fixture.session.apply("diff.txt", RowCommand::Skip);
    fixture.session.apply("only_b.txt", RowCommand::DeleteB);

    FC_CHECK(fixture.session.undoLast()); // the deletion
    FC_CHECK(fs::exists(fixture.b / "only_b.txt"));
    FC_CHECK(fixture.session.undoLast()); // the skip
    FC_CHECK(!fixture.session.actionOf("diff.txt").has_value());
    FC_CHECK(fixture.session.actionOf("only_a.txt") == RowCommand::CopyAToB);
    FC_CHECK(fixture.session.undoLast()); // the copy
    FC_CHECK(!fs::exists(fixture.b / "only_a.txt"));
    FC_CHECK(!fixture.session.canUndo());
}

FC_TEST(session_delete_missing_folder_and_undo)
{
    SessionFixture fixture;
    fixture.session.apply(native("photos"), RowCommand::DeleteA);
    FC_CHECK(!fs::exists(fixture.a / "photos"));
    FC_CHECK_EQ(fixture.row("photos").statusText, std::string("DELETED A"));

    fixture.session.apply(native("photos"), RowCommand::Undo);
    FC_CHECK_EQ(fc::test::readFile(fixture.a / "photos/2023/b.jpg"), std::string("b"));
    FC_CHECK(fs::is_directory(fixture.a / "photos/empty"));
}

FC_TEST(session_action_inside_a_folder_blocks_the_folder_actions)
{
    SessionFixture fixture;
    fixture.session.toggleExpanded(native("photos"));
    fixture.session.apply(native("photos/a.jpg"), RowCommand::CopyAToB);
    FC_CHECK_EQ(fc::test::readFile(fixture.b / "photos/a.jpg"), std::string("a"));

    FC_CHECK(fixture.row("photos").buttons.empty());
    FC_CHECK_THROWS_AS(fixture.session.apply(native("photos"), RowCommand::CopyAToB), std::logic_error);
    // Siblings stay independent.
    FC_CHECK_EQ(fixture.row("photos/2023").buttons.size(), std::size_t{3});

    fixture.session.apply(native("photos/a.jpg"), RowCommand::Undo);
    FC_CHECK(!fs::exists(fixture.b / "photos")); // the parent created by the copy is removed
    FC_CHECK_EQ(fixture.row("photos").buttons.size(), std::size_t{3});
}

FC_TEST(session_undo_last_reverts_most_recent_first)
{
    SessionFixture fixture;
    fixture.session.apply("only_a.txt", RowCommand::CopyAToB);
    fixture.session.apply("only_b.txt", RowCommand::DeleteB);
    FC_CHECK_EQ(fixture.session.actionCount(), std::size_t{2});

    FC_CHECK(fixture.session.undoLast());
    FC_CHECK(fs::exists(fixture.b / "only_b.txt"));
    FC_CHECK(fs::exists(fixture.b / "only_a.txt"));
    FC_CHECK(fixture.session.actionOf("only_a.txt") == RowCommand::CopyAToB);

    FC_CHECK(fixture.session.undoLast());
    FC_CHECK(!fs::exists(fixture.b / "only_a.txt"));
    FC_CHECK(!fixture.session.undoLast());
    FC_CHECK(!fixture.session.canUndo());
}

FC_TEST(session_existing_locations_follow_the_disk)
{
    SessionFixture fixture;
    const auto sides = [&fixture](const char* path) {
        std::vector<fc::Side> result;
        for (const auto& location : fixture.session.existingLocations(native(path))) {
            result.push_back(location.side);
            FC_CHECK(fs::exists(location.path));
        }
        return result;
    };

    FC_CHECK_EQ(sides("diff.txt"), (std::vector<fc::Side>{fc::Side::A, fc::Side::B}));
    FC_CHECK_EQ(sides("only_a.txt"), (std::vector<fc::Side>{fc::Side::A}));
    FC_CHECK_EQ(sides("photos/2023"), (std::vector<fc::Side>{fc::Side::A}));
    FC_CHECK_EQ(sides("only_b.txt"), (std::vector<fc::Side>{fc::Side::B}));
    FC_CHECK(sides("same.txt").empty()); // not listed
    FC_CHECK(sides("../escape").empty());

    fixture.session.apply("diff.txt", RowCommand::DeleteA);
    FC_CHECK_EQ(sides("diff.txt"), (std::vector<fc::Side>{fc::Side::B}));
    fixture.session.apply(native("photos"), RowCommand::CopyAToB);
    FC_CHECK_EQ(sides("photos/2023"), (std::vector<fc::Side>{fc::Side::A, fc::Side::B}));
}

FC_TEST(session_rejects_invalid_commands)
{
    SessionFixture fixture;
    FC_CHECK_THROWS_AS(fixture.session.apply("same.txt", RowCommand::CopyAToB), std::invalid_argument);
    FC_CHECK_THROWS_AS(fixture.session.apply("../escape.txt", RowCommand::DeleteA), std::invalid_argument);
    FC_CHECK_THROWS_AS(fixture.session.apply("only_b.txt", RowCommand::CopyAToB), std::logic_error);
    FC_CHECK_THROWS_AS(fixture.session.apply("only_b.txt", RowCommand::DeleteA), std::logic_error);
    FC_CHECK_THROWS_AS(fixture.session.apply("only_b.txt", RowCommand::Undo), std::logic_error);

    fixture.session.apply("only_b.txt", RowCommand::CopyBToA);
    FC_CHECK_THROWS_AS(fixture.session.apply("only_b.txt", RowCommand::DeleteB), std::logic_error);
}

FC_TEST(session_failed_action_records_nothing)
{
    SessionFixture fixture;
    fs::remove(fixture.b / "only_b.txt"); // vanished after the comparison

    FC_CHECK_THROWS_AS(fixture.session.apply("only_b.txt", RowCommand::CopyBToA), fc::SourceMissingError);
    FC_CHECK_THROWS_AS(fixture.session.apply("only_b.txt", RowCommand::DeleteB), fc::SourceMissingError);
    FC_CHECK(!fixture.session.actionOf("only_b.txt").has_value());
    FC_CHECK_EQ(fixture.row("only_b.txt").buttons.size(), std::size_t{3}); // the user can retry
}

FC_TEST(session_only_deletions_and_overwrites_need_a_backup)
{
    SessionFixture fixture;
    fixture.session.apply("only_a.txt", RowCommand::CopyAToB); // new file: nothing to lose
    FC_CHECK(!fixture.session.hasUndoableBackups());
    fixture.session.apply("diff.txt", RowCommand::CopyBToA); // overwrites A/diff.txt
    FC_CHECK(fixture.session.hasUndoableBackups());
}

FC_TEST(regression_new_comparison_forgets_previous_actions_and_empties_trash)
{
    SessionFixture fixture;
    fixture.session.apply("only_b.txt", RowCommand::DeleteB);
    fixture.session.apply("only_a.txt", RowCommand::CopyAToB);
    fixture.compare();

    FC_CHECK(!fixture.session.canUndo());
    FC_CHECK(!fs::exists(fixture.b / fc::kTrashFolderName));
    FC_CHECK(!fixture.session.row("only_b.txt").has_value()); // deleted for good
    FC_CHECK(!fixture.session.row("only_a.txt").has_value()); // now identical
    FC_CHECK_EQ(fixture.session.result().identicalCount(), std::size_t{3});
}

FC_TEST(session_reset_clears_everything)
{
    SessionFixture fixture;
    fixture.session.toggleExpanded(native("photos"));
    fixture.session.apply("diff.txt", RowCommand::CopyAToB);
    fixture.session.reset();

    FC_CHECK(!fixture.session.hasResult());
    FC_CHECK(fixture.session.rows(fc::Filter::AllDifferences).empty());
    FC_CHECK(!fixture.session.canUndo());
    FC_CHECK(!fixture.session.isExpanded(native("photos")));
    FC_CHECK(fixture.session.folderA().empty());
}

// ---------------------------------------------------------------------------
// Live counts: the summary follows the copies and deletions
//
// Fixture state: files  identical 2 (same.txt, common/kept.txt), different 1 (diff.txt),
//                       only A 4 (only_a.txt, photos/a.jpg, photos/2023/b.jpg, photos/2023/c.jpg),
//                       only B 3 (common/new_in_b.txt, only_b.txt, z_only_b.txt)
//                folders different 1 (common), only A 3 (photos, photos/2023, photos/empty)
// ---------------------------------------------------------------------------

namespace {

fc::EntryCounts counts(const fc::ComparisonSession& session, fc::SummaryItem item)
{
    return session.summaryCounts(item);
}

fc::EntryCounts ef(std::size_t files, std::size_t folders)
{
    return fc::EntryCounts{.files = files, .folders = folders};
}

} // namespace

FC_TEST(live_counts_match_the_comparison_before_any_action)
{
    const SessionFixture fixture;
    const auto& result = fixture.session.result();
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Identical) == result.counts(fc::FileStatus::Identical));
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Different) == result.counts(fc::FileStatus::Different));
    FC_CHECK(counts(fixture.session, fc::SummaryItem::OnlyA) == result.counts(fc::FileStatus::OnlyA));
    FC_CHECK(counts(fixture.session, fc::SummaryItem::OnlyB) == result.counts(fc::FileStatus::OnlyB));
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Total) == result.totalCounts());
    FC_CHECK(counts(fixture.session, fc::SummaryItem::AllDifferences) == result.allDifferencesCounts());
    FC_CHECK(counts(fixture.session, fc::SummaryItem::OnlyA) == ef(4, 3));
}

FC_TEST(live_counts_copy_of_a_different_file_makes_it_identical_and_undo_reverts)
{
    SessionFixture fixture;
    fixture.session.apply("diff.txt", RowCommand::CopyBToA);
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Identical) == ef(3, 0));
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Different) == ef(0, 1));
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Total) == ef(10, 4)); // nothing appears or disappears

    fixture.session.apply("diff.txt", RowCommand::Undo);
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Identical) == ef(2, 0));
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Different) == ef(1, 1));
}

FC_TEST(live_counts_deleting_one_side_of_a_different_file_makes_it_only_on_the_other)
{
    SessionFixture fixture;
    fixture.session.apply("diff.txt", RowCommand::DeleteA);
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Different) == ef(0, 1));
    FC_CHECK(counts(fixture.session, fc::SummaryItem::OnlyB) == ef(4, 0));
}

FC_TEST(live_counts_deleting_an_only_entry_removes_it)
{
    SessionFixture fixture;
    fixture.session.apply("only_b.txt", RowCommand::DeleteB);
    FC_CHECK(counts(fixture.session, fc::SummaryItem::OnlyB) == ef(2, 0));
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Total) == ef(9, 4));

    fixture.session.apply(native("photos"), RowCommand::DeleteA);
    FC_CHECK(counts(fixture.session, fc::SummaryItem::OnlyA) == ef(1, 0)); // only_a.txt left
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Total) == ef(6, 1));
}

FC_TEST(live_counts_copying_the_last_difference_of_a_folder_makes_it_identical)
{
    SessionFixture fixture;
    fixture.session.apply(native("common/new_in_b.txt"), RowCommand::CopyBToA);
    FC_CHECK(counts(fixture.session, fc::SummaryItem::OnlyB) == ef(2, 0));
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Identical) == ef(3, 1)); // common/ too
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Different) == ef(1, 0));
}

FC_TEST(live_counts_copying_a_missing_folder_makes_all_its_content_identical)
{
    SessionFixture fixture;
    fixture.session.apply(native("photos"), RowCommand::CopyAToB);
    FC_CHECK(counts(fixture.session, fc::SummaryItem::OnlyA) == ef(1, 0));
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Identical) == ef(5, 3));
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Total) == ef(10, 4));
}

FC_TEST(live_counts_copying_a_file_out_of_a_missing_folder_creates_its_parent)
{
    SessionFixture fixture;
    fixture.session.toggleExpanded(native("photos"));
    fixture.session.apply(native("photos/a.jpg"), RowCommand::CopyAToB);

    // photos/ now exists on both sides but still differs (2023/ and empty/ are only in A).
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Identical) == ef(3, 0));
    FC_CHECK(counts(fixture.session, fc::SummaryItem::OnlyA) == ef(3, 2));
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Different) == ef(1, 2));

    fixture.session.apply(native("photos/a.jpg"), RowCommand::Undo); // the created parent is removed again
    FC_CHECK(counts(fixture.session, fc::SummaryItem::OnlyA) == ef(4, 3));
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Different) == ef(1, 1));
}

FC_TEST(live_counts_skip_changes_nothing_and_undoing_everything_restores_the_comparison)
{
    SessionFixture fixture;
    const fc::EntryCounts identical = counts(fixture.session, fc::SummaryItem::Identical);
    fixture.session.apply("z_only_b.txt", RowCommand::Skip);
    FC_CHECK(counts(fixture.session, fc::SummaryItem::OnlyB) == ef(3, 0));

    fixture.session.apply("diff.txt", RowCommand::CopyAToB);
    fixture.session.apply("only_b.txt", RowCommand::DeleteB);
    fixture.session.apply(native("photos"), RowCommand::CopyAToB);
    while (fixture.session.undoLast()) {
    }
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Identical) == identical);
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Total) == fixture.session.result().totalCounts());
}

// ---------------------------------------------------------------------------
// Redo
// ---------------------------------------------------------------------------

FC_TEST(session_redo_applies_again_what_was_undone_most_recent_first)
{
    SessionFixture fixture;
    FC_CHECK(!fixture.session.canRedo());
    FC_CHECK(!fixture.session.redoLast());

    fixture.session.apply("only_a.txt", RowCommand::CopyAToB);
    fixture.session.apply("only_b.txt", RowCommand::DeleteB);
    FC_CHECK(fixture.session.undoLast()); // the deletion
    FC_CHECK(fixture.session.undoLast()); // the copy
    FC_CHECK(!fs::exists(fixture.b / "only_a.txt"));
    FC_CHECK(fs::exists(fixture.b / "only_b.txt"));
    FC_CHECK(fixture.session.canRedo());

    FC_CHECK(fixture.session.redoLast()); // the copy, undone last
    FC_CHECK(fs::exists(fixture.b / "only_a.txt"));
    FC_CHECK(fixture.session.actionOf("only_a.txt") == RowCommand::CopyAToB);
    FC_CHECK(fs::exists(fixture.b / "only_b.txt"));

    FC_CHECK(fixture.session.redoLast()); // then the deletion
    FC_CHECK(!fs::exists(fixture.b / "only_b.txt"));
    FC_CHECK_EQ(fixture.row("only_b.txt").statusText, std::string("DELETED B"));
    FC_CHECK(!fixture.session.canRedo());
    FC_CHECK(counts(fixture.session, fc::SummaryItem::OnlyB) == ef(2, 0)); // counts follow
}

FC_TEST(session_redo_also_follows_row_undo_and_skip)
{
    SessionFixture fixture;
    fixture.session.apply("diff.txt", RowCommand::Skip);
    fixture.session.apply("diff.txt", RowCommand::Undo);
    FC_CHECK(fixture.session.redoLast());
    FC_CHECK(fixture.session.actionOf("diff.txt") == RowCommand::Skip);
}

FC_TEST(session_new_action_or_comparison_empties_redo)
{
    SessionFixture fixture;
    fixture.session.apply("only_a.txt", RowCommand::CopyAToB);
    FC_CHECK(fixture.session.undoLast());
    fixture.session.apply("diff.txt", RowCommand::Skip);
    FC_CHECK(!fixture.session.canRedo());

    FC_CHECK(fixture.session.undoLast());
    FC_CHECK(fixture.session.canRedo());
    fixture.compare();
    FC_CHECK(!fixture.session.canRedo());
}

FC_TEST(session_failed_redo_stays_redoable)
{
    SessionFixture fixture;
    fixture.session.apply("only_b.txt", RowCommand::CopyBToA);
    FC_CHECK(fixture.session.undoLast());
    fs::remove(fixture.b / "only_b.txt"); // the source vanished meanwhile

    FC_CHECK_THROWS_AS((void)fixture.session.redoLast(), fc::SourceMissingError);
    FC_CHECK(fixture.session.canRedo());
    FC_CHECK(!fixture.session.actionOf("only_b.txt").has_value());
}

// ---------------------------------------------------------------------------
// Delete both
// ---------------------------------------------------------------------------

FC_TEST(session_delete_both_removes_both_versions_and_undo_restores_them)
{
    SessionFixture fixture;
    FC_CHECK(fixture.row("only_a.txt").buttons.size() == std::size_t{3}); // only for Different files

    fixture.session.apply("diff.txt", RowCommand::DeleteBoth);
    FC_CHECK(!fs::exists(fixture.a / "diff.txt"));
    FC_CHECK(!fs::exists(fixture.b / "diff.txt"));
    const fc::Row row = fixture.row("diff.txt");
    FC_CHECK_EQ(row.statusText, std::string("DELETED A+B"));
    FC_CHECK_EQ(row.style.color, fc::colors::kRed);
    FC_CHECK(fixture.session.hasUndoableBackups());
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Different) == ef(0, 1));
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Total) == ef(9, 4)); // gone from both sides

    fixture.session.apply("diff.txt", RowCommand::Undo);
    FC_CHECK_EQ(fc::test::readFile(fixture.a / "diff.txt"), std::string("version A"));
    FC_CHECK_EQ(fc::test::readFile(fixture.b / "diff.txt"), std::string("version B!"));
    FC_CHECK(counts(fixture.session, fc::SummaryItem::Different) == ef(1, 1));

    FC_CHECK(fixture.session.redoLast());
    FC_CHECK(!fs::exists(fixture.a / "diff.txt"));
    FC_CHECK(!fs::exists(fixture.b / "diff.txt"));
}

FC_TEST(session_delete_both_is_all_or_nothing)
{
    SessionFixture fixture;
    fs::remove(fixture.b / "diff.txt"); // B's version vanished after the comparison

    FC_CHECK_THROWS_AS(fixture.session.apply("diff.txt", RowCommand::DeleteBoth), fc::SourceMissingError);
    FC_CHECK_EQ(fc::test::readFile(fixture.a / "diff.txt"), std::string("version A")); // A was put back
    FC_CHECK(!fixture.session.actionOf("diff.txt").has_value());
    FC_CHECK(!fixture.session.hasUndoableBackups());
}

FC_TEST(session_delete_both_is_refused_for_one_sided_rows)
{
    SessionFixture fixture;
    FC_CHECK_THROWS_AS(fixture.session.apply("only_a.txt", RowCommand::DeleteBoth), std::logic_error);
    FC_CHECK_THROWS_AS(fixture.session.apply(native("photos"), RowCommand::DeleteBoth), std::logic_error);
}

// ---------------------------------------------------------------------------
// Selection and mass actions
// ---------------------------------------------------------------------------

FC_TEST(selection_only_rows_offering_commands_are_selectable)
{
    SessionFixture fixture;
    FC_CHECK(fixture.row("diff.txt").selectable);
    FC_CHECK(!fixture.row("diff.txt").selected);

    fixture.session.toggleExpanded(native("photos"));
    fixture.session.apply(native("photos/a.jpg"), RowCommand::Skip);
    FC_CHECK(fixture.row("photos/a.jpg").selectable);  // Undo
    FC_CHECK(!fixture.row("photos").selectable);       // blocked by its content's action
    FC_CHECK_THROWS_AS(fixture.session.setSelected(native("photos"), true), std::invalid_argument);
    FC_CHECK_THROWS_AS(fixture.session.setSelected("same.txt", true), std::invalid_argument);

    fixture.session.setSelected("diff.txt", true);
    FC_CHECK(fixture.row("diff.txt").selected);
    FC_CHECK(fixture.session.isSelected("diff.txt"));
    fixture.session.setSelected("diff.txt", false);
    FC_CHECK_EQ(fixture.session.selectionCount(), std::size_t{0});
}

FC_TEST(selection_select_all_follows_the_view)
{
    SessionFixture fixture;
    FC_CHECK(fixture.session.selectionState(fc::Filter::OnlyB) == fc::SelectionState::None);

    fixture.session.setSelected("only_b.txt", true);
    FC_CHECK(fixture.session.selectionState(fc::Filter::OnlyB) == fc::SelectionState::Some);

    fixture.session.setAllSelected(fc::Filter::OnlyB, true);
    FC_CHECK(fixture.session.selectionState(fc::Filter::OnlyB) == fc::SelectionState::All);
    FC_CHECK_EQ(fixture.session.selectionCount(), std::size_t{3});
    FC_CHECK(fixture.session.selectionState(fc::Filter::OnlyA) == fc::SelectionState::None);
    FC_CHECK(fixture.session.selectionState(fc::Filter::AllDifferences) == fc::SelectionState::Some);

    fixture.session.setAllSelected(fc::Filter::OnlyB, false);
    FC_CHECK_EQ(fixture.session.selectionCount(), std::size_t{0});
}

FC_TEST(selection_offers_only_the_commands_common_to_every_row)
{
    SessionFixture fixture;
    FC_CHECK(fixture.session.commonCommands().empty());

    fixture.session.setSelected("diff.txt", true);
    FC_CHECK_EQ(fixture.session.commonCommands(),
                (std::vector<RowCommand>{RowCommand::CopyAToB, RowCommand::CopyBToA, RowCommand::DeleteA,
                                         RowCommand::DeleteB, RowCommand::DeleteBoth, RowCommand::Skip}));

    fixture.session.setSelected("only_a.txt", true);
    FC_CHECK_EQ(fixture.session.commonCommands(),
                (std::vector<RowCommand>{RowCommand::CopyAToB, RowCommand::DeleteA, RowCommand::Skip}));

    fixture.session.setSelected("only_b.txt", true);
    FC_CHECK_EQ(fixture.session.commonCommands(), (std::vector<RowCommand>{RowCommand::Skip}));
    FC_CHECK_THROWS_AS((void)fixture.session.applyToSelection(RowCommand::DeleteA), std::logic_error);
    FC_CHECK(!fixture.session.canUndo()); // nothing was applied
}

FC_TEST(mass_action_is_one_action_for_undo_and_redo)
{
    SessionFixture fixture;
    fixture.session.setSelected("diff.txt", true);
    fixture.session.setSelected("only_a.txt", true);

    FC_CHECK(fixture.session.applyToSelection(RowCommand::CopyAToB).empty());
    FC_CHECK_EQ(fc::test::readFile(fixture.b / "diff.txt"), std::string("version A"));
    FC_CHECK_EQ(fc::test::readFile(fixture.b / "only_a.txt"), std::string("only in A"));
    FC_CHECK_EQ(fixture.session.selectionCount(), std::size_t{2}); // kept: Undo is now common
    FC_CHECK_EQ(fixture.session.commonCommands(), (std::vector<RowCommand>{RowCommand::Undo}));

    FC_CHECK(fixture.session.undoLast()); // both at once
    FC_CHECK_EQ(fc::test::readFile(fixture.b / "diff.txt"), std::string("version B!"));
    FC_CHECK(!fs::exists(fixture.b / "only_a.txt"));
    FC_CHECK(!fixture.session.canUndo());

    FC_CHECK(fixture.session.redoLast()); // both at once
    FC_CHECK_EQ(fixture.session.actionCount(), std::size_t{2});
    FC_CHECK(fs::exists(fixture.b / "only_a.txt"));
    FC_CHECK(fixture.session.undoLast());
    FC_CHECK_EQ(fixture.session.actionCount(), std::size_t{0});
}

FC_TEST(mass_undo_of_the_selection_is_redone_as_one)
{
    SessionFixture fixture;
    fixture.session.setAllSelected(fc::Filter::OnlyB, true);
    FC_CHECK(fixture.session.applyToSelection(RowCommand::Skip).empty());
    FC_CHECK(fixture.session.summaryCounts(fc::SummaryItem::Skipped) == ef(3, 0));

    FC_CHECK(fixture.session.applyToSelection(RowCommand::Undo).empty());
    FC_CHECK_EQ(fixture.session.actionCount(), std::size_t{0});
    FC_CHECK(fixture.session.redoLast());
    FC_CHECK_EQ(fixture.session.actionCount(), std::size_t{3});
}

FC_TEST(mass_action_on_a_folder_covers_its_selected_content)
{
    SessionFixture fixture;
    fixture.session.toggleExpanded(native("photos"));
    fixture.session.setSelected(native("photos"), true);
    fixture.session.setSelected(native("photos/a.jpg"), true);

    FC_CHECK(fixture.session.applyToSelection(RowCommand::DeleteA).empty()); // no conflict on a.jpg
    FC_CHECK(!fs::exists(fixture.a / "photos"));
    FC_CHECK(fixture.session.actionOf(native("photos")) == RowCommand::DeleteA);
    FC_CHECK(!fixture.session.actionOf(native("photos/a.jpg")).has_value()); // inherited
    FC_CHECK_EQ(fixture.session.selectionCount(), std::size_t{1}); // a.jpg is no longer selectable
}

FC_TEST(mass_action_reports_failures_and_applies_the_rest)
{
    SessionFixture fixture;
    fixture.session.setSelected("only_b.txt", true);
    fixture.session.setSelected("z_only_b.txt", true);
    fs::remove(fixture.b / "z_only_b.txt"); // vanished after the comparison

    const auto failures = fixture.session.applyToSelection(RowCommand::CopyBToA);
    FC_REQUIRE_EQ(failures.size(), std::size_t{1});
    FC_CHECK(failures.front().path == fs::path("z_only_b.txt"));
    FC_CHECK(!failures.front().message.empty());
    FC_CHECK(fs::exists(fixture.a / "only_b.txt"));
    FC_CHECK(fixture.session.actionOf("only_b.txt") == RowCommand::CopyBToA);
    FC_CHECK(!fixture.session.actionOf("z_only_b.txt").has_value());
}

FC_TEST(new_comparison_clears_the_selection)
{
    SessionFixture fixture;
    fixture.session.setAllSelected(fc::Filter::AllDifferences, true);
    FC_CHECK(fixture.session.selectionCount() > 0);
    fixture.compare();
    FC_CHECK_EQ(fixture.session.selectionCount(), std::size_t{0});
}

// ---------------------------------------------------------------------------
// Sorting, type filter, selection counts
// ---------------------------------------------------------------------------

FC_TEST(session_sorts_by_name_ignoring_case_in_both_orders)
{
    const TempDir dir;
    for (const char* name : {"b.txt", "A.txt", "c.TXT", "Docs/z.txt", "Docs/Y.txt", "Docs/Sub/x.txt"}) {
        dir.write(fs::path("A") / name, "x");
    }
    dir.makeFolder("B");
    fc::ComparisonSession session;
    session.setResult(dir / "A", dir / "B",
                      fc::FolderManager::compare(dir / "A", dir / "B", {.deepComparison = false, .recursive = true}));
    session.toggleExpanded("Docs");

    FC_CHECK_EQ(describe(session.rows(fc::Filter::OnlyA)),
                (std::vector<std::string>{"A.txt=ONLY A", "b.txt=ONLY A", "c.TXT=ONLY A", "Docs/=ONLY A",
                                          "  Sub/=ONLY A", "  Y.txt=ONLY A", "  z.txt=ONLY A"}));

    session.setSortOrder(fc::SortOrder::Descending);
    FC_CHECK(session.sortOrder() == fc::SortOrder::Descending);
    FC_CHECK_EQ(describe(session.rows(fc::Filter::OnlyA)),
                (std::vector<std::string>{"Docs/=ONLY A", "  Sub/=ONLY A", "  z.txt=ONLY A", "  Y.txt=ONLY A",
                                          "c.TXT=ONLY A", "b.txt=ONLY A", "A.txt=ONLY A"})); // sub-folders stay first

    session.setResult(dir / "A", dir / "B", fc::ComparisonResult{});
    FC_CHECK(session.sortOrder() == fc::SortOrder::Descending); // a display preference: kept
}

FC_TEST(session_file_types_follow_the_view)
{
    SessionFixture fixture;
    const auto all = fixture.session.fileTypes(fc::Filter::AllDifferences);
    FC_CHECK_EQ(all.size(), std::size_t{2});
    FC_CHECK_EQ(all.at("TXT"), std::size_t{5}); // diff, only_a, only_b, z_only_b, common/new_in_b
    FC_CHECK_EQ(all.at("JPG"), std::size_t{3});

    const auto onlyA = fixture.session.fileTypes(fc::Filter::OnlyA);
    FC_CHECK_EQ(onlyA.at("TXT"), std::size_t{1});
    FC_CHECK_EQ(onlyA.at("JPG"), std::size_t{3});
    const auto onlyB = fixture.session.fileTypes(fc::Filter::OnlyB);
    FC_CHECK_EQ(onlyB.size(), std::size_t{1}); // no image only in B
    FC_CHECK_EQ(onlyB.at("TXT"), std::size_t{3});

    FC_CHECK(fixture.session.fileTypes(fc::Filter::Skipped).empty());
    fixture.session.apply(native("photos"), RowCommand::Skip); // a skipped folder brings its content
    fixture.session.apply("diff.txt", RowCommand::Skip);
    const auto skipped = fixture.session.fileTypes(fc::Filter::Skipped);
    FC_CHECK_EQ(skipped.at("JPG"), std::size_t{3});
    FC_CHECK_EQ(skipped.at("TXT"), std::size_t{1});
}

FC_TEST(session_type_filter_hides_folders_without_match_and_counts_what_matches)
{
    SessionFixture fixture;
    fixture.dir.write("A/docs/readme.txt", "x"); // a missing folder without any image
    fixture.compare();
    fixture.session.toggleExpanded(native("photos"));
    fixture.session.setTypeFilter(std::set<std::string>{"JPG"});

    // docs/ (no image) and photos/empty/ (nothing) are hidden; counts are the images only.
    FC_CHECK_EQ(describe(fixture.session.rows(fc::Filter::OnlyA)),
                (std::vector<std::string>{"photos/=ONLY A", "  2023/=ONLY A", "  a.jpg=ONLY A"}));
    FC_CHECK_EQ(fixture.row("photos").detail, std::string("D(1) F(3)"));
    FC_CHECK_EQ(fixture.row("photos/2023").detail, std::string("D(0) F(2)"));
    FC_CHECK(fixture.session.rows(fc::Filter::Different).empty());

    fixture.session.setTypeFilter(std::set<std::string>{"TXT"});
    FC_CHECK_EQ(describe(fixture.session.rows(fc::Filter::OnlyA)),
                (std::vector<std::string>{"docs/=ONLY A", "only_a.txt=ONLY A"}));

    fixture.session.setTypeFilter(std::nullopt); // everything again, full counts
    FC_CHECK_EQ(fixture.row("photos").detail, std::string("D(2) F(3)"));
    FC_CHECK_EQ(fixture.session.rows(fc::Filter::OnlyA).size(), std::size_t{6});
}

FC_TEST(session_type_filter_with_several_or_no_types)
{
    SessionFixture fixture;
    fixture.session.setTypeFilter(std::set<std::string>{"JPG", "TXT"});
    FC_CHECK_EQ(fixture.session.rows(fc::Filter::AllDifferences).size(), std::size_t{6});
    fixture.session.setTypeFilter(std::set<std::string>{}); // no type at all: nothing
    FC_CHECK(fixture.session.rows(fc::Filter::AllDifferences).empty());
}

FC_TEST(session_type_filter_clears_the_selection_and_resets_with_a_new_comparison)
{
    SessionFixture fixture;
    fixture.session.setSelected("diff.txt", true);
    fixture.session.setTypeFilter(std::set<std::string>{"JPG"});
    FC_CHECK_EQ(fixture.session.selectionCount(), std::size_t{0});

    fixture.compare();
    FC_CHECK(!fixture.session.typeFilter().has_value());
}

FC_TEST(session_mass_buttons_are_grouped_like_rows)
{
    using fc::ComparisonSession;
    const auto labels = [](const std::vector<fc::RowButton>& buttons) {
        std::vector<std::string> result;
        result.reserve(buttons.size());
        for (const auto& button : buttons) {
            result.push_back(button.label);
        }
        return result;
    };
    // Different rows only: menus.
    const auto different = ComparisonSession::massButtons({RowCommand::CopyAToB, RowCommand::CopyBToA, RowCommand::DeleteA,
                                                           RowCommand::DeleteB, RowCommand::DeleteBoth, RowCommand::Skip});
    FC_CHECK_EQ(labels(different), (std::vector<std::string>{"Copy", "Delete", "Skip"}));
    FC_CHECK_EQ(different.at(0).choices.size(), std::size_t{2});
    FC_CHECK_EQ(different.at(1).choices.size(), std::size_t{3});

    // Single commands: plain buttons with the row labels.
    const auto onlyA = ComparisonSession::massButtons({RowCommand::CopyAToB, RowCommand::DeleteA, RowCommand::Skip});
    FC_CHECK_EQ(labels(onlyA), (std::vector<std::string>{"Copy \u2192 B", "Delete A", "Skip"}));
    FC_CHECK(onlyA.at(0).choices.empty());

    FC_CHECK_EQ(labels(ComparisonSession::massButtons({RowCommand::Undo})), (std::vector<std::string>{"Undo"}));
    FC_CHECK(ComparisonSession::massButtons({}).empty());
}

FC_TEST(session_selection_counts_cover_folder_content)
{
    SessionFixture fixture;
    FC_CHECK(fixture.session.selectionCounts() == fc::EntryCounts{});
    fixture.session.toggleExpanded(native("photos"));
    fixture.session.setSelected(native("photos"), true);     // D(2) F(3) inside
    fixture.session.setSelected(native("photos/a.jpg"), true); // already counted with photos/
    fixture.session.setSelected("diff.txt", true);
    FC_CHECK(fixture.session.selectionCounts() == ef(4, 3));
}

// ---------------------------------------------------------------------------
// Folder actions under a type filter: only the matching files
// ---------------------------------------------------------------------------

namespace {

/// photos/ (only in A) also holds a text file next to the images.
struct MixedFolderFixture : SessionFixture {
    MixedFolderFixture()
    {
        dir.write("A/photos/info.txt", "notes");
        compare();
        session.setTypeFilter(std::set<std::string>{"JPG"});
    }
};

} // namespace

FC_TEST(filtered_folder_copy_copies_only_the_matching_files)
{
    MixedFolderFixture fixture;
    fixture.session.apply(native("photos"), RowCommand::CopyAToB);

    FC_CHECK(fs::exists(fixture.b / "photos/a.jpg"));
    FC_CHECK(fs::exists(fixture.b / "photos/2023/b.jpg"));
    FC_CHECK(fs::exists(fixture.b / "photos/2023/c.jpg"));
    FC_CHECK(!fs::exists(fixture.b / "photos/info.txt")); // filtered out
    FC_CHECK(!fs::exists(fixture.b / "photos/empty"));    // nothing matching inside

    // The files carry the actions; the folder row reflects them and offers Undo.
    FC_CHECK(!fixture.session.actionOf(native("photos")).has_value());
    FC_CHECK(fixture.session.actionOf(native("photos/a.jpg")) == RowCommand::CopyAToB);
    FC_CHECK_EQ(commands(fixture.row("photos")), (std::vector<RowCommand>{RowCommand::Undo}));
    FC_CHECK_EQ(fixture.row("photos").statusText, std::string("COPIED \u2192 B"));

    FC_CHECK(fixture.session.undoLast()); // one action: the three copies
    FC_CHECK(!fs::exists(fixture.b / "photos"));
    FC_CHECK(fixture.session.redoLast());
    FC_CHECK(fs::exists(fixture.b / "photos/2023/c.jpg"));
}

FC_TEST(filtered_folder_delete_and_skip_touch_only_the_matching_files)
{
    MixedFolderFixture fixture;
    fixture.session.apply(native("photos"), RowCommand::DeleteA);
    FC_CHECK(!fs::exists(fixture.a / "photos/a.jpg"));
    FC_CHECK(!fs::exists(fixture.a / "photos/2023/b.jpg"));
    FC_CHECK(fs::exists(fixture.a / "photos/info.txt")); // kept
    FC_CHECK(fixture.session.undoLast());
    FC_CHECK(fs::exists(fixture.a / "photos/a.jpg"));

    fixture.session.apply(native("photos"), RowCommand::Skip);
    FC_CHECK(fixture.session.summaryCounts(fc::SummaryItem::Skipped) == ef(3, 0)); // the 3 images
    FC_CHECK(!fixture.session.actionOf(native("photos/info.txt")).has_value());
}

FC_TEST(filtered_folder_mass_action_and_selection_counts)
{
    MixedFolderFixture fixture;
    fixture.session.setSelected(native("photos"), true);
    FC_CHECK(fixture.session.selectionCounts() == ef(3, 2)); // photos/, 2023/ and the 3 images

    FC_CHECK(fixture.session.applyToSelection(RowCommand::CopyAToB).empty());
    FC_CHECK(fs::exists(fixture.b / "photos/a.jpg"));
    FC_CHECK(!fs::exists(fixture.b / "photos/info.txt"));
    FC_CHECK(fixture.session.undoLast());
    FC_CHECK(!fs::exists(fixture.b / "photos"));
}

FC_TEST(filtered_folder_partial_failure_applies_the_rest_and_reports)
{
    MixedFolderFixture fixture;
    fs::remove(fixture.a / "photos/a.jpg"); // vanished after the comparison

    FC_CHECK_THROWS_AS(fixture.session.apply(native("photos"), RowCommand::CopyAToB), std::runtime_error);
    FC_CHECK(fs::exists(fixture.b / "photos/2023/b.jpg"));
    FC_CHECK(fs::exists(fixture.b / "photos/2023/c.jpg"));
    FC_CHECK_EQ(fixture.session.actionCount(), std::size_t{2});
    FC_CHECK(fixture.session.undoLast()); // the two copies, as one action
    FC_CHECK_EQ(fixture.session.actionCount(), std::size_t{0});
}

FC_TEST(unfiltered_folder_action_still_takes_the_whole_folder)
{
    SessionFixture fixture;
    fixture.dir.write("A/photos/info.txt", "notes");
    fixture.compare();
    fixture.session.apply(native("photos"), RowCommand::CopyAToB);
    FC_CHECK(fs::exists(fixture.b / "photos/info.txt"));
    FC_CHECK(fs::is_directory(fixture.b / "photos/empty"));
    FC_CHECK(fixture.session.actionOf(native("photos")) == RowCommand::CopyAToB);
}

FC_TEST(filtered_folder_undo_button_reverts_its_files)
{
    MixedFolderFixture fixture;
    fixture.session.apply(native("photos"), RowCommand::DeleteA);
    FC_CHECK(fixture.row("photos").selectable);

    fixture.session.apply(native("photos"), RowCommand::Undo); // the folder's Undo
    FC_CHECK(fs::exists(fixture.a / "photos/a.jpg"));
    FC_CHECK(fs::exists(fixture.a / "photos/2023/c.jpg"));
    FC_CHECK_EQ(fixture.session.actionCount(), std::size_t{0});
    FC_CHECK_EQ(fixture.row("photos").buttons.size(), std::size_t{3}); // Copy, Delete, Skip again

    FC_CHECK(fixture.session.redoLast()); // redone as one
    FC_CHECK_EQ(fixture.session.actionCount(), std::size_t{3});
    FC_CHECK(!fs::exists(fixture.a / "photos/a.jpg"));
}

FC_TEST(filtered_folder_with_some_files_done_offers_nothing_folder_wide)
{
    MixedFolderFixture fixture;
    fixture.session.toggleExpanded(native("photos"));
    fixture.session.apply(native("photos/a.jpg"), RowCommand::Skip);
    FC_CHECK(fixture.row("photos").buttons.empty()); // mixed: each file keeps its own buttons
    FC_CHECK_EQ(fixture.row("photos/2023/b.jpg").buttons.size(), std::size_t{3});

    // Different actions on every file: Undo, without a single status.
    fixture.session.apply(native("photos/2023"), RowCommand::CopyAToB);
    FC_CHECK_EQ(commands(fixture.row("photos")), (std::vector<RowCommand>{RowCommand::Undo}));
    FC_CHECK_EQ(fixture.row("photos").statusText, std::string("ONLY A"));
    fixture.session.apply(native("photos"), RowCommand::Undo);
    FC_CHECK_EQ(fixture.session.actionCount(), std::size_t{0});
}

FC_TEST(filtered_folder_mass_undo_from_the_selection)
{
    MixedFolderFixture fixture;
    fixture.session.setSelected(native("photos"), true);
    FC_CHECK(fixture.session.applyToSelection(RowCommand::Skip).empty());
    FC_CHECK_EQ(fixture.session.commonCommands(), (std::vector<RowCommand>{RowCommand::Undo}));
    FC_CHECK(fixture.session.applyToSelection(RowCommand::Undo).empty());
    FC_CHECK_EQ(fixture.session.actionCount(), std::size_t{0});
}

// ---------------------------------------------------------------------------
// Tree of check boxes
// ---------------------------------------------------------------------------

FC_TEST(selection_of_every_item_of_a_folder_selects_the_folder)
{
    SessionFixture fixture;
    fixture.session.toggleExpanded(native("photos"));
    fixture.session.toggleExpanded(native("photos/2023"));

    fixture.session.setSelected(native("photos/2023/b.jpg"), true);
    FC_CHECK(!fixture.session.isSelected(native("photos/2023")));
    fixture.session.setSelected(native("photos/2023/c.jpg"), true);
    FC_CHECK(fixture.session.isSelected(native("photos/2023"))); // all its content
    FC_CHECK(!fixture.session.isSelected(native("photos")));    // empty/ and a.jpg are not

    fixture.session.setSelected(native("photos/empty"), true);
    fixture.session.setSelected(native("photos/a.jpg"), true);
    FC_CHECK(fixture.session.isSelected(native("photos"))); // up the tree
}

FC_TEST(unselecting_an_item_unselects_its_folders)
{
    SessionFixture fixture;
    fixture.session.toggleExpanded(native("photos"));
    fixture.session.setSelected(native("photos"), true); // brings its whole content
    FC_CHECK(fixture.session.isSelected(native("photos/2023/b.jpg")));
    FC_CHECK(fixture.session.isSelected(native("photos/a.jpg")));

    fixture.session.setSelected(native("photos/2023/b.jpg"), false);
    FC_CHECK(!fixture.session.isSelected(native("photos/2023")));
    FC_CHECK(!fixture.session.isSelected(native("photos")));
    FC_CHECK(fixture.session.isSelected(native("photos/2023/c.jpg"))); // the others stay
    FC_CHECK(fixture.session.isSelected(native("photos/a.jpg")));

    fixture.session.setSelected(native("photos/2023/b.jpg"), true); // complete again
    FC_CHECK(fixture.session.isSelected(native("photos")));

    fixture.session.setSelected(native("photos"), false); // and its content with it
    FC_CHECK_EQ(fixture.session.selectionCount(), std::size_t{0});
}

FC_TEST(selection_with_a_type_filter_follows_the_shown_content)
{
    SessionFixture fixture;
    fixture.dir.write("A/photos/info.txt", "notes");
    fixture.compare();
    fixture.session.setTypeFilter(std::set<std::string>{"JPG"});
    fixture.session.toggleExpanded(native("photos"));

    // Shown: 2023/ (b, c) and a.jpg; info.txt and empty/ are filtered out.
    fixture.session.setSelected(native("photos/2023"), true);
    fixture.session.setSelected(native("photos/a.jpg"), true);
    FC_CHECK(fixture.session.isSelected(native("photos")));
    FC_CHECK(!fixture.session.isSelected(native("photos/info.txt")));
}

FC_TEST(selection_counts_include_the_missing_folders_above)
{
    SessionFixture fixture;
    fixture.session.toggleExpanded(native("photos"));
    fixture.session.toggleExpanded(native("photos/2023"));

    // One file deep in a missing folder: photos/ and 2023/ would be created too.
    fixture.session.setSelected(native("photos/2023/b.jpg"), true);
    FC_CHECK(fixture.session.selectionCounts() == ef(1, 2));

    fixture.session.setSelected(native("photos/a.jpg"), true); // same parents, counted once
    FC_CHECK(fixture.session.selectionCounts() == ef(2, 2));

    fixture.session.setSelected("diff.txt", true); // a common folder above: nothing more
    FC_CHECK(fixture.session.selectionCounts() == ef(3, 2));
}
