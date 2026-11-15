#include "TestFramework.hpp"

#include "core/ComparisonResult.hpp"
#include "core/Presentation.hpp"

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
    return result;
}

} // namespace

FC_TEST(comparisonResult_counts)
{
    const auto result = sampleResult();
    FC_CHECK_EQ(result.identicalCount(), std::size_t{3});
    FC_CHECK_EQ(result.differentCount(), std::size_t{2});
    FC_CHECK_EQ(result.onlyACount(), std::size_t{1});
    FC_CHECK_EQ(result.onlyBCount(), std::size_t{4});
    FC_CHECK_EQ(result.allDifferencesCount(), std::size_t{7});
    FC_CHECK_EQ(result.totalCount(), std::size_t{10});
}

FC_TEST(comparisonResult_allDifferences_keeps_each_category)
{
    const auto result = sampleResult();
    const auto entries = result.allDifferences();
    FC_REQUIRE_EQ(entries.size(), std::size_t{7});
    FC_CHECK(entries[0] == (fc::FileEntry{"d1", fc::FileStatus::Different}));
    FC_CHECK(entries[1] == (fc::FileEntry{"d2", fc::FileStatus::Different}));
    FC_CHECK(entries[2] == (fc::FileEntry{"a1", fc::FileStatus::OnlyA}));
    FC_CHECK(entries[3] == (fc::FileEntry{"b1", fc::FileStatus::OnlyB}));
    FC_CHECK(entries[6] == (fc::FileEntry{"b4", fc::FileStatus::OnlyB}));
}

FC_TEST(summary_texts_match_python_labels)
{
    const auto result = sampleResult();
    FC_CHECK_EQ(fc::summaryText(fc::SummaryItem::Total, result), std::string("Total: 10"));
    FC_CHECK_EQ(fc::summaryText(fc::SummaryItem::Identical, result), std::string("Identical: 3"));
    FC_CHECK_EQ(fc::summaryText(fc::SummaryItem::AllDifferences, result), std::string("All differences: 7"));
    FC_CHECK_EQ(fc::summaryText(fc::SummaryItem::Different, result), std::string("Different: 2"));
    FC_CHECK_EQ(fc::summaryText(fc::SummaryItem::OnlyA, result), std::string("Only A: 1"));
    FC_CHECK_EQ(fc::summaryText(fc::SummaryItem::OnlyB, result), std::string("Only B: 4"));
    FC_CHECK_EQ(fc::summaryText(fc::SummaryItem::Total, fc::ComparisonResult{}), std::string("Total: 0"));
}

FC_TEST(summary_colors_match_python_table)
{
    FC_CHECK_EQ(fc::summaryStyle(fc::SummaryItem::Total).color, std::string_view("#000000"));
    FC_CHECK_EQ(fc::summaryStyle(fc::SummaryItem::Identical).color, std::string_view("#008000"));
    FC_CHECK_EQ(fc::summaryStyle(fc::SummaryItem::AllDifferences).color, std::string_view("#B88600"));
    FC_CHECK_EQ(fc::summaryStyle(fc::SummaryItem::Different).background, std::string_view("#FFF8E1"));
    FC_CHECK_EQ(fc::summaryStyle(fc::SummaryItem::OnlyA).color, std::string_view("#E07000"));
    FC_CHECK_EQ(fc::summaryStyle(fc::SummaryItem::OnlyB).color, std::string_view("#CC0000"));
    FC_CHECK_EQ(fc::summaryStyle(fc::SummaryItem::OnlyB).background, std::string_view("#FFEBEE"));
}

FC_TEST(filter_buttons_mark_current_filter)
{
    FC_CHECK_EQ(fc::filterButtonText(fc::Filter::AllDifferences, fc::Filter::AllDifferences),
                std::string("● All"));
    FC_CHECK_EQ(fc::filterButtonText(fc::Filter::Different, fc::Filter::AllDifferences),
                std::string("Different"));
    FC_CHECK_EQ(fc::filterButtonText(fc::Filter::OnlyB, fc::Filter::OnlyB), std::string("● Only B"));
    FC_CHECK(fc::kDefaultFilter == fc::Filter::AllDifferences);
}

FC_TEST(row_styles_match_python_tables)
{
    FC_CHECK_EQ(fc::upperLabel(fc::statusStyle(fc::FileStatus::Different).label), std::string("DIFFERENT"));
    FC_CHECK_EQ(fc::upperLabel(fc::statusStyle(fc::FileStatus::OnlyA).label), std::string("ONLY A"));
    FC_CHECK_EQ(fc::upperLabel(fc::statusStyle(fc::FileStatus::OnlyB).label), std::string("ONLY B"));
    FC_CHECK_EQ(fc::upperLabel(fc::actionStyle(fc::FileAction::Copy).label), std::string("COPIED"));
    FC_CHECK_EQ(fc::upperLabel(fc::actionStyle(fc::FileAction::Delete).label), std::string("DELETED"));
    FC_CHECK_EQ(fc::actionStyle(fc::FileAction::Copy).color, std::string_view("#008000"));
    FC_CHECK_EQ(fc::actionStyle(fc::FileAction::Skip).color, std::string_view("#B88600"));
    FC_CHECK_EQ(fc::actionStyle(fc::FileAction::Delete).background, std::string_view("#FFEBEE"));
}
