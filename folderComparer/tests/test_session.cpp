// ComparisonSession: the behavior of the Python App, including non-regression
// tests for the bugs of the Python version.

#include "TempDir.hpp"
#include "TestFramework.hpp"

#include "core/ComparisonSession.hpp"
#include "core/FileManager.hpp"
#include "core/FolderManager.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using fc::test::TempDir;

namespace {

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
        dir.write("A/sub/only_a.txt", "only in A");
        dir.write("B/only_b.txt", "only in B");
        dir.write("B/z_only_b.txt", "only in B too");
        compare();
    }

    void compare()
    {
        session.setResult(a, b, fc::FolderManager::compare(a, b, {.deepComparison = true, .recursive = true}));
    }
};

std::vector<std::string> statusTexts(const std::vector<fc::Row>& rows)
{
    std::vector<std::string> texts;
    texts.reserve(rows.size());
    for (const auto& row : rows) {
        texts.push_back(fc::toUtf8(row.path.generic_string()) + "=" + row.statusText);
    }
    return texts;
}

} // namespace

FC_TEST(session_starts_empty)
{
    const fc::ComparisonSession session;
    FC_CHECK(!session.hasResult());
    FC_CHECK(session.rows(fc::Filter::AllDifferences).empty());
    FC_CHECK(!session.row("anything").has_value());
}

FC_TEST(session_filter_rows_by_category)
{
    const SessionFixture fixture;

    FC_CHECK_EQ(statusTexts(fixture.session.rows(fc::Filter::Different)),
                (std::vector<std::string>{"diff.txt=DIFFERENT"}));
    FC_CHECK_EQ(statusTexts(fixture.session.rows(fc::Filter::OnlyA)),
                (std::vector<std::string>{"sub/only_a.txt=ONLY A"}));
    FC_CHECK_EQ(statusTexts(fixture.session.rows(fc::Filter::OnlyB)),
                (std::vector<std::string>{"only_b.txt=ONLY B", "z_only_b.txt=ONLY B"}));
}

FC_TEST(regression_all_filter_shows_every_difference_with_its_own_category)
{
    // Python bug: populate_file_list() recursed per filter and cleared the list
    // each time, so "All" only displayed the Only B files.
    const SessionFixture fixture;
    FC_CHECK_EQ(statusTexts(fixture.session.rows(fc::Filter::AllDifferences)),
                (std::vector<std::string>{"diff.txt=DIFFERENT", "sub/only_a.txt=ONLY A",
                                          "only_b.txt=ONLY B", "z_only_b.txt=ONLY B"}));
}

FC_TEST(session_rows_offer_the_python_buttons)
{
    const SessionFixture fixture;
    const auto different = fixture.session.row("diff.txt");
    const auto onlyA = fixture.session.row(fs::path("sub/only_a.txt").make_preferred());
    const auto onlyB = fixture.session.row("only_b.txt");
    FC_REQUIRE(different && onlyA && onlyB);

    FC_CHECK_EQ(different->buttons,
                (std::vector<fc::RowButton>{{"B → A", fc::CopyDirection::BToA},
                                            {"A → B", fc::CopyDirection::AToB}}));
    FC_CHECK_EQ(onlyA->buttons, (std::vector<fc::RowButton>{{"Copy → B", fc::CopyDirection::AToB}}));
    FC_CHECK_EQ(onlyB->buttons, (std::vector<fc::RowButton>{{"Copy → A", fc::CopyDirection::BToA}}));
    FC_CHECK_EQ(different->style.background, std::string_view("#FFF8E1"));
    FC_CHECK(!fixture.session.row("same.txt").has_value()); // identical files are not listed
}

FC_TEST(session_copy_only_a_to_b)
{
    SessionFixture fixture;
    const fs::path path = fs::path("sub/only_a.txt").make_preferred();

    fixture.session.copy(path, fc::CopyDirection::AToB);

    FC_CHECK_EQ(fc::test::readFile(fixture.b / path), std::string("only in A"));
    FC_CHECK(fixture.session.actionOf(path) == fc::FileAction::Copy);
}

FC_TEST(session_copy_different_both_directions)
{
    SessionFixture fixture;
    fixture.session.copy("diff.txt", fc::CopyDirection::BToA);
    FC_CHECK_EQ(fc::test::readFile(fixture.a / "diff.txt"), std::string("version B!"));

    SessionFixture other;
    other.session.copy("diff.txt", fc::CopyDirection::AToB);
    FC_CHECK_EQ(fc::test::readFile(other.b / "diff.txt"), std::string("version A"));
}

FC_TEST(regression_copied_row_is_marked_copied_without_buttons)
{
    // Python bug: the action was stored as the tuple (path,) but looked up as
    // path, so copied rows were never marked "COPIED" and kept their buttons.
    SessionFixture fixture;
    fixture.session.copy("only_b.txt", fc::CopyDirection::BToA);

    const auto row = fixture.session.row("only_b.txt");
    FC_REQUIRE(row.has_value());
    FC_CHECK_EQ(row->statusText, std::string("COPIED"));
    FC_CHECK(row->action == fc::FileAction::Copy);
    FC_CHECK(row->buttons.empty());
    FC_CHECK_EQ(row->style.color, std::string_view("#008000"));
    FC_CHECK(row->status == fc::FileStatus::OnlyB); // the category is kept

    // The row stays in its filters, now marked as copied.
    const auto rows = fixture.session.rows(fc::Filter::OnlyB);
    FC_REQUIRE_EQ(rows.size(), std::size_t{2});
    FC_CHECK_EQ(rows[0].statusText, std::string("COPIED"));
    FC_CHECK_EQ(rows[1].statusText, std::string("ONLY B"));
}

FC_TEST(regression_new_comparison_forgets_previous_actions_and_rows)
{
    // Python bug: FILTERS[...]["files"] caches were never cleared, so a second
    // comparison displayed the files of the first one.
    SessionFixture fixture;
    fixture.session.copy("only_b.txt", fc::CopyDirection::BToA);
    fixture.compare();

    FC_CHECK(!fixture.session.actionOf("only_b.txt").has_value());
    FC_CHECK(!fixture.session.row("only_b.txt").has_value()); // now identical
    FC_CHECK_EQ(statusTexts(fixture.session.rows(fc::Filter::OnlyB)),
                (std::vector<std::string>{"z_only_b.txt=ONLY B"}));
    FC_CHECK_EQ(fixture.session.result().identicalCount(), std::size_t{2});
}

FC_TEST(session_copy_rejects_invalid_requests)
{
    SessionFixture fixture;
    FC_CHECK_THROWS_AS(fixture.session.copy("same.txt", fc::CopyDirection::AToB), std::invalid_argument);
    FC_CHECK_THROWS_AS(fixture.session.copy("unknown.txt", fc::CopyDirection::AToB), std::invalid_argument);
    FC_CHECK_THROWS_AS(fixture.session.copy("../escape.txt", fc::CopyDirection::AToB), std::invalid_argument);
    // Only B files can only be copied to A.
    FC_CHECK_THROWS_AS(fixture.session.copy("only_b.txt", fc::CopyDirection::AToB), std::logic_error);
    FC_CHECK(!fs::exists(fixture.b / "same.txt.fc-partial"));

    fixture.session.copy("only_b.txt", fc::CopyDirection::BToA);
    FC_CHECK_THROWS_AS(fixture.session.copy("only_b.txt", fc::CopyDirection::BToA), std::logic_error);
}

FC_TEST(session_copy_failure_records_nothing)
{
    SessionFixture fixture;
    fs::remove(fixture.b / "only_b.txt"); // the source vanished after the comparison

    FC_CHECK_THROWS_AS(fixture.session.copy("only_b.txt", fc::CopyDirection::BToA), fc::SourceMissingError);
    FC_CHECK(!fixture.session.actionOf("only_b.txt").has_value());
    const auto row = fixture.session.row("only_b.txt");
    FC_REQUIRE(row.has_value());
    FC_CHECK(!row->buttons.empty()); // the user can retry
}

FC_TEST(session_reset_clears_everything)
{
    SessionFixture fixture;
    fixture.session.copy("diff.txt", fc::CopyDirection::AToB);
    fixture.session.reset();

    FC_CHECK(!fixture.session.hasResult());
    FC_CHECK(fixture.session.rows(fc::Filter::AllDifferences).empty());
    FC_CHECK(!fixture.session.actionOf("diff.txt").has_value());
    FC_CHECK(fixture.session.folderA().empty());
}
