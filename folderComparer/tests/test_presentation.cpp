#include "TestFramework.hpp"

#include "core/ComparisonResult.hpp"
#include "core/Presentation.hpp"

#include <cstdint>
#include <filesystem>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

fc::ComparisonResult sampleResult()
{
    fc::ComparisonResult result;
    result.identical = {"i1", "i2", "i3"};
    result.different = {"d1", "d2"};
    result.onlyA = {"a1"};
    result.onlyB = {"b1", "b2", "b3", "b4"};
    result.identicalFolders = {"same"};
    result.differentFolders = {"mixed", "mixed/deep"};
    result.onlyAFolders = {"missing", "missing/x", "missing/y"};
    return result;
}

} // namespace

FC_TEST(comparisonResult_counts)
{
    const auto result = sampleResult();
    FC_CHECK_EQ(result.identicalCount(), std::size_t{3});
    FC_CHECK_EQ(result.allDifferencesCount(), std::size_t{7});
    FC_CHECK_EQ(result.totalCount(), std::size_t{10});

    FC_CHECK(result.counts(fc::FileStatus::OnlyA) == (fc::EntryCounts{.files = 1, .folders = 3}));
    FC_CHECK(result.counts(fc::FileStatus::OnlyB) == (fc::EntryCounts{.files = 4, .folders = 0}));
    FC_CHECK(result.allDifferencesCounts() == (fc::EntryCounts{.files = 7, .folders = 5}));
    FC_CHECK(result.totalCounts() == (fc::EntryCounts{.files = 10, .folders = 6}));
    FC_CHECK_EQ(result.totalCounts().entries(), std::size_t{16});
}

FC_TEST(comparisonResult_allDifferences_keeps_each_category)
{
    const auto result = sampleResult();
    const auto entries = result.allDifferences();
    FC_REQUIRE_EQ(entries.size(), std::size_t{7});
    FC_CHECK(entries[0] == (fc::FileEntry{"d1", fc::FileStatus::Different}));
    FC_CHECK(entries[2] == (fc::FileEntry{"a1", fc::FileStatus::OnlyA}));
    FC_CHECK(entries[6] == (fc::FileEntry{"b4", fc::FileStatus::OnlyB}));
}

FC_TEST(counts_are_formatted_as_entries_folders_files)
{
    FC_CHECK_EQ(fc::formatCounts({.files = 10, .folders = 2}), std::string("E(12) D(2) F(10)"));
    FC_CHECK_EQ(fc::formatCounts({}), std::string("E(0) D(0) F(0)"));
    FC_CHECK_EQ(fc::formatFolderContent({.files = 10, .folders = 2}), std::string("D(2) F(10)"));
}

FC_TEST(summary_texts_show_entry_folder_and_file_counts)
{
    const auto result = sampleResult();
    FC_CHECK_EQ(fc::summaryText(fc::SummaryItem::Total, result), std::string("Total: E(16) D(6) F(10)"));
    FC_CHECK_EQ(fc::summaryText(fc::SummaryItem::Identical, result), std::string("Identical: E(4) D(1) F(3)"));
    FC_CHECK_EQ(fc::summaryText(fc::SummaryItem::AllDifferences, result),
                std::string("All differences: E(12) D(5) F(7)"));
    FC_CHECK_EQ(fc::summaryText(fc::SummaryItem::Different, result), std::string("Different: E(4) D(2) F(2)"));
    FC_CHECK_EQ(fc::summaryText(fc::SummaryItem::OnlyA, result), std::string("Only A: E(4) D(3) F(1)"));
    FC_CHECK_EQ(fc::summaryText(fc::SummaryItem::OnlyB, result), std::string("Only B: E(4) D(0) F(4)"));
}

FC_TEST(summary_difference_labels_are_the_filter_buttons)
{
    FC_CHECK(!fc::summaryFilter(fc::SummaryItem::Total).has_value());
    FC_CHECK(!fc::summaryFilter(fc::SummaryItem::Identical).has_value());
    FC_CHECK(fc::summaryFilter(fc::SummaryItem::AllDifferences) == fc::Filter::AllDifferences);
    FC_CHECK(fc::summaryFilter(fc::SummaryItem::Different) == fc::Filter::Different);
    FC_CHECK(fc::summaryFilter(fc::SummaryItem::OnlyA) == fc::Filter::OnlyA);
    FC_CHECK(fc::summaryFilter(fc::SummaryItem::OnlyB) == fc::Filter::OnlyB);
    FC_CHECK(fc::summaryFilter(fc::SummaryItem::Skipped) == fc::Filter::Skipped);
    FC_CHECK(!fc::filterStatus(fc::Filter::Skipped).has_value());
    FC_CHECK(fc::kDefaultFilter == fc::Filter::AllDifferences);

    // No marker: the current filter is shown by the button's background.
    const fc::ComparisonResult empty;
    FC_CHECK_EQ(fc::summaryText(fc::SummaryItem::AllDifferences, empty),
                std::string("All differences: E(0) D(0) F(0)"));
}

FC_TEST(summary_text_from_live_counts)
{
    FC_CHECK_EQ(fc::summaryText(fc::SummaryItem::Skipped, fc::EntryCounts{.files = 4, .folders = 3}),
                std::string("Skipped: E(7) D(3) F(4)"));
    FC_CHECK(fc::summaryCounts(fc::SummaryItem::Skipped, fc::ComparisonResult{}) == fc::EntryCounts{});
    FC_CHECK_EQ(fc::summaryStyle(fc::SummaryItem::Skipped).color, fc::colors::kGrey);
}

FC_TEST(summary_colors)
{
    FC_CHECK_EQ(fc::summaryStyle(fc::SummaryItem::Total).color, std::string_view("#000000"));
    FC_CHECK_EQ(fc::summaryStyle(fc::SummaryItem::Identical).color, std::string_view("#008000"));
    FC_CHECK_EQ(fc::summaryStyle(fc::SummaryItem::AllDifferences).color, std::string_view("#B88600"));
    FC_CHECK_EQ(fc::summaryStyle(fc::SummaryItem::Different).background, std::string_view("#FFF8E1"));
    FC_CHECK_EQ(fc::summaryStyle(fc::SummaryItem::OnlyA).color, std::string_view("#E07000"));
    FC_CHECK_EQ(fc::summaryStyle(fc::SummaryItem::OnlyB).color, std::string_view("#A0522D"));
    FC_CHECK_EQ(fc::summaryStyle(fc::SummaryItem::OnlyB).background, std::string_view("#F6EBE1"));
}

FC_TEST(action_colors_red_only_for_deletions)
{
    FC_CHECK_EQ(fc::actionStyle(fc::FileAction::Copy).color, std::string_view("#008000"));
    FC_CHECK_EQ(fc::actionStyle(fc::FileAction::Delete).color, std::string_view("#CC0000"));
    FC_CHECK_EQ(fc::actionStyle(fc::FileAction::Delete).background, std::string_view("#FFEBEE"));
    FC_CHECK_EQ(fc::actionStyle(fc::FileAction::Skip).color, std::string_view("#616161"));
    FC_CHECK_EQ(fc::actionStyle(fc::FileAction::Skip).background, std::string_view("#EEEEEE"));
    FC_CHECK(fc::commandStyle(fc::RowCommand::DeleteB) == fc::actionStyle(fc::FileAction::Delete));
    FC_CHECK(fc::commandStyle(fc::RowCommand::Undo) == fc::Style{});
    // No difference category uses the deletion red.
    for (const auto status : {fc::FileStatus::Different, fc::FileStatus::OnlyA, fc::FileStatus::OnlyB}) {
        FC_CHECK(fc::statusStyle(status).color != fc::colors::kRed);
    }
}

FC_TEST(action_texts_and_labels)
{
    FC_CHECK_EQ(fc::actionStatusText(fc::RowCommand::CopyAToB), std::string("COPIED → B"));
    FC_CHECK_EQ(fc::actionStatusText(fc::RowCommand::CopyBToA), std::string("COPIED → A"));
    FC_CHECK_EQ(fc::actionStatusText(fc::RowCommand::DeleteA), std::string("DELETED A"));
    FC_CHECK_EQ(fc::actionStatusText(fc::RowCommand::DeleteB), std::string("DELETED B"));
    FC_CHECK_EQ(fc::commandLabel(fc::RowCommand::CopyAToB), std::string_view("A → B"));
    FC_CHECK_EQ(fc::commandLabel(fc::RowCommand::DeleteB), std::string_view("Delete B"));
    FC_CHECK_EQ(fc::commandLabel(fc::RowCommand::Undo), std::string_view("Undo"));
    FC_CHECK_EQ(fc::commandLabel(fc::RowCommand::Skip), std::string_view("Skip"));
    FC_CHECK_EQ(fc::actionStatusText(fc::RowCommand::Skip), std::string("SKIPPED"));
    FC_CHECK(fc::commandAction(fc::RowCommand::Skip) == fc::FileAction::Skip);
    FC_CHECK(!fc::commandAction(fc::RowCommand::Undo).has_value());
    FC_CHECK(fc::commandAction(fc::RowCommand::CopyBToA) == fc::FileAction::Copy);
    FC_CHECK(fc::commandAction(fc::RowCommand::DeleteA) == fc::FileAction::Delete);
    FC_CHECK_EQ(fc::actionStyle(fc::FileAction::Copy).color, std::string_view("#008000"));
}

FC_TEST(row_status_labels)
{
    FC_CHECK_EQ(fc::upperLabel(fc::statusStyle(fc::FileStatus::Different).label), std::string("DIFFERENT"));
    FC_CHECK_EQ(fc::upperLabel(fc::statusStyle(fc::FileStatus::OnlyA).label), std::string("ONLY A"));
    FC_CHECK_EQ(fc::upperLabel(fc::statusStyle(fc::FileStatus::OnlyB).label), std::string("ONLY B"));
}

FC_TEST(delete_both_labels)
{
    FC_CHECK_EQ(fc::commandLabel(fc::RowCommand::DeleteBoth), std::string_view("Delete both"));
    FC_CHECK_EQ(fc::actionStatusText(fc::RowCommand::DeleteBoth), std::string("DELETED A+B"));
    FC_CHECK(fc::commandStyle(fc::RowCommand::DeleteBoth) == fc::actionStyle(fc::FileAction::Delete));
}

FC_TEST(progress_percent)
{
    FC_CHECK_EQ(fc::progressPercent(0, 200), 0);
    FC_CHECK_EQ(fc::progressPercent(1, 200), 0); // truncated: 100 % only when done
    FC_CHECK_EQ(fc::progressPercent(199, 200), 99);
    FC_CHECK_EQ(fc::progressPercent(200, 200), 100);
    FC_CHECK_EQ(fc::progressPercent(37, 74), 50);
    FC_CHECK_EQ(fc::progressPercent(0, 0), 100); // nothing to compare
    FC_CHECK_EQ(fc::progressPercent(5, 3), 100);
    FC_CHECK_EQ(fc::progressPercent(SIZE_MAX / 200, SIZE_MAX / 100), 50); // no overflow
    const int half = fc::progressPercent(SIZE_MAX / 2, SIZE_MAX); // approximated at this scale
    FC_CHECK(half >= 49);
    FC_CHECK(half <= 50);
    FC_CHECK_EQ(fc::progressPercent(SIZE_MAX - 1, SIZE_MAX), 99);
}

FC_TEST(extension_labels)
{
    FC_CHECK_EQ(fc::extensionLabel("photo.jpeg"), std::string("JPEG"));
    FC_CHECK_EQ(fc::extensionLabel("dir/Movie.Mp4"), std::string("MP4"));
    FC_CHECK_EQ(fc::extensionLabel("archive.tar.gz"), std::string("GZ"));
    FC_CHECK_EQ(fc::extensionLabel("README"), std::string(""));
    FC_CHECK_EQ(fc::extensionLabel(".gitignore"), std::string("")); // a hidden file, not an extension
    FC_CHECK_EQ(fc::extensionLabel("name."), std::string(""));
}

FC_TEST(type_groups_keep_text_apart_from_code)
{
    const auto groups = fc::typeGroups();
    FC_REQUIRE(groups.size() >= std::size_t{8});
    const auto find = [&groups](std::string_view name) -> const fc::TypeGroup& {
        for (const fc::TypeGroup& group : groups) {
            if (group.name == name) {
                return group;
            }
        }
        throw std::logic_error("missing group");
    };
    const auto contains = [](const fc::TypeGroup& group, std::string_view extension) {
        return std::ranges::find(group.extensions, extension) != group.extensions.end();
    };

    for (const char* image : {"PNG", "JPG", "JPEG", "WEBP"}) {
        FC_CHECK(contains(find("Images"), image));
    }
    FC_CHECK(contains(find("Videos"), "MP4"));
    FC_CHECK(contains(find("Text"), "TXT"));
    FC_CHECK(contains(find("Code"), "CPP"));
    FC_CHECK(!contains(find("Text"), "CPP"));
    FC_CHECK(!contains(find("Code"), "TXT"));

    // Upper-case like extensionLabel, so they compare directly.
    for (const fc::TypeGroup& group : groups) {
        for (const std::string_view extension : group.extensions) {
            FC_CHECK_EQ(fc::upperLabel(extension), std::string(extension));
        }
    }
    // Wrapped 8 types per line, so that long groups give a readable tooltip.
    const std::string code = fc::typeGroupDescription(find("Code"));
    FC_CHECK(code.starts_with("Code:\nC, CC, CPP, CXX, H, HPP, HXX, CS\nJAVA"));
    std::size_t longestLine = 0;
    std::size_t lineStart = 0;
    for (std::size_t index = 0; index <= code.size(); ++index) {
        if (index == code.size() || code[index] == '\n') {
            longestLine = std::max(longestLine, index - lineStart);
            lineStart = index + 1;
        }
    }
    FC_CHECK(longestLine < 60);
    FC_CHECK(fc::typeGroupDescription(find("Images")).starts_with("Images:\nPNG, JPG, JPEG, WEBP"));
}

FC_TEST(case_insensitive_order)
{
    FC_CHECK(fc::caseInsensitiveLess("apple", "Banana"));
    FC_CHECK(fc::caseInsensitiveLess("Banana", "cherry"));
    FC_CHECK(!fc::caseInsensitiveLess("cherry", "Banana"));
    FC_CHECK(fc::caseInsensitiveLess("abc", "ABCD")); // a prefix comes first
    // u8 literals: a narrow literal would be decoded with the ANSI code page on Windows.
    FC_CHECK(fc::caseInsensitiveLess(std::filesystem::path(u8"\u00C9t\u00E9"), std::filesystem::path(u8"\u00E9t\u00E9s")));
    // Same letters: a stable, total order (exactly one way round).
    FC_CHECK(fc::caseInsensitiveLess("A.txt", "a.txt") != fc::caseInsensitiveLess("a.txt", "A.txt"));
    FC_CHECK(!fc::caseInsensitiveLess("same", "same"));
}

FC_TEST(type_labels)
{
    FC_CHECK_EQ(fc::typeLabel("JPG"), std::string("JPG"));
    FC_CHECK_EQ(fc::typeLabel(""), std::string("(no extension)"));
}
