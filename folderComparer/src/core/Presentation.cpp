#include "core/Presentation.hpp"

#include <algorithm>

namespace fc {

Style summaryStyle(SummaryItem item) noexcept
{
    switch (item) {
    case SummaryItem::Total:
        return {"Total", colors::kBlack, {}};
    case SummaryItem::Identical:
        return {"Identical", colors::kGreen, colors::kGreenBackground};
    case SummaryItem::AllDifferences:
        return {"All differences", colors::kYellow, {}};
    case SummaryItem::Different:
        return {"Different", colors::kYellow, colors::kYellowBackground};
    case SummaryItem::OnlyA:
        return {"Only A", colors::kOrange, colors::kOrangeBackground};
    case SummaryItem::OnlyB:
        return {"Only B", colors::kRed, colors::kRedBackground};
    }
    return {};
}

std::size_t summaryCount(SummaryItem item, const ComparisonResult& result) noexcept
{
    switch (item) {
    case SummaryItem::Total:
        return result.totalCount();
    case SummaryItem::Identical:
        return result.identicalCount();
    case SummaryItem::AllDifferences:
        return result.allDifferencesCount();
    case SummaryItem::Different:
        return result.differentCount();
    case SummaryItem::OnlyA:
        return result.onlyACount();
    case SummaryItem::OnlyB:
        return result.onlyBCount();
    }
    return 0;
}

std::string summaryText(SummaryItem item, const ComparisonResult& result)
{
    return std::string(summaryStyle(item).label) + ": " + std::to_string(summaryCount(item, result));
}

std::string_view filterLabel(Filter filter) noexcept
{
    switch (filter) {
    case Filter::AllDifferences:
        return "All";
    case Filter::Different:
        return "Different";
    case Filter::OnlyA:
        return "Only A";
    case Filter::OnlyB:
        return "Only B";
    }
    return {};
}

std::string filterButtonText(Filter filter, Filter currentFilter)
{
    std::string text = filter == currentFilter ? "● " : "";
    text += filterLabel(filter);
    return text;
}

std::optional<FileStatus> filterStatus(Filter filter) noexcept
{
    switch (filter) {
    case Filter::AllDifferences:
        return std::nullopt;
    case Filter::Different:
        return FileStatus::Different;
    case Filter::OnlyA:
        return FileStatus::OnlyA;
    case Filter::OnlyB:
        return FileStatus::OnlyB;
    }
    return std::nullopt;
}

Style statusStyle(FileStatus status) noexcept
{
    switch (status) {
    case FileStatus::Identical:
        return summaryStyle(SummaryItem::Identical);
    case FileStatus::Different:
        return summaryStyle(SummaryItem::Different);
    case FileStatus::OnlyA:
        return summaryStyle(SummaryItem::OnlyA);
    case FileStatus::OnlyB:
        return summaryStyle(SummaryItem::OnlyB);
    }
    return {};
}

Style actionStyle(FileAction action) noexcept
{
    switch (action) {
    case FileAction::Copy:
        return {"copied", colors::kGreen, colors::kGreenBackground};
    case FileAction::Move:
        return {"moved", colors::kGreen, colors::kGreenBackground};
    case FileAction::Skip:
        return {"skipped", colors::kYellow, colors::kYellowBackground};
    case FileAction::Delete:
        return {"deleted", colors::kRed, colors::kRedBackground};
    }
    return {};
}

std::string upperLabel(std::string_view label)
{
    std::string upper(label);
    std::ranges::transform(upper, upper.begin(), [](char character) {
        return (character >= 'a' && character <= 'z') ? static_cast<char>(character - 'a' + 'A')
                                                      : character;
    });
    return upper;
}

} // namespace fc
