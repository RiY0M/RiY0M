#include "core/Presentation.hpp"

#include <algorithm>
#include <limits>

namespace fc {

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
    case Filter::Skipped:
        return std::nullopt;
    }
    return std::nullopt;
}

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
        return {"Only B", colors::kTerracotta, colors::kTerracottaBackground};
    case SummaryItem::Skipped:
        return {"Skipped", colors::kGrey, colors::kGreyBackground};
    }
    return {};
}

EntryCounts summaryCounts(SummaryItem item, const ComparisonResult& result) noexcept
{
    switch (item) {
    case SummaryItem::Total:
        return result.totalCounts();
    case SummaryItem::Identical:
        return result.counts(FileStatus::Identical);
    case SummaryItem::AllDifferences:
        return result.allDifferencesCounts();
    case SummaryItem::Different:
        return result.counts(FileStatus::Different);
    case SummaryItem::OnlyA:
        return result.counts(FileStatus::OnlyA);
    case SummaryItem::OnlyB:
        return result.counts(FileStatus::OnlyB);
    case SummaryItem::Skipped:
        return {};
    }
    return {};
}

std::optional<Filter> summaryFilter(SummaryItem item) noexcept
{
    switch (item) {
    case SummaryItem::Total:
    case SummaryItem::Identical:
        return std::nullopt;
    case SummaryItem::AllDifferences:
        return Filter::AllDifferences;
    case SummaryItem::Different:
        return Filter::Different;
    case SummaryItem::OnlyA:
        return Filter::OnlyA;
    case SummaryItem::OnlyB:
        return Filter::OnlyB;
    case SummaryItem::Skipped:
        return Filter::Skipped;
    }
    return std::nullopt;
}

std::string formatCounts(const EntryCounts& counts)
{
    return "E(" + std::to_string(counts.entries()) + ") " + formatFolderContent(counts);
}

std::string formatFolderContent(const EntryCounts& counts)
{
    return "D(" + std::to_string(counts.folders) + ") F(" + std::to_string(counts.files) + ")";
}

std::string summaryText(SummaryItem item, const EntryCounts& counts)
{
    std::string text(summaryStyle(item).label);
    text += ": ";
    text += formatCounts(counts);
    return text;
}

std::string summaryText(SummaryItem item, const ComparisonResult& result)
{
    return summaryText(item, summaryCounts(item, result));
}

std::string_view countersHelp() noexcept
{
    return "E = entries (files + folders), D = folders, F = files.\n"
           "A folder present on both sides is Different when something inside it\n"
           "differs, Identical otherwise. Folders are only counted by recursive comparisons.";
}

std::optional<FileAction> commandAction(RowCommand command) noexcept
{
    switch (command) {
    case RowCommand::CopyAToB:
    case RowCommand::CopyBToA:
        return FileAction::Copy;
    case RowCommand::DeleteA:
    case RowCommand::DeleteB:
    case RowCommand::DeleteBoth:
        return FileAction::Delete;
    case RowCommand::Skip:
        return FileAction::Skip;
    case RowCommand::Undo:
        return std::nullopt;
    }
    return std::nullopt;
}

Style commandStyle(RowCommand command) noexcept
{
    const std::optional<FileAction> action = commandAction(command);
    return action ? actionStyle(*action) : Style{};
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
    case FileAction::Delete:
        return {"deleted", colors::kRed, colors::kRedBackground};
    case FileAction::Skip:
        return {"skipped", colors::kGrey, colors::kGreyBackground};
    }
    return {};
}

std::string actionStatusText(RowCommand command)
{
    std::string action = upperLabel(commandStyle(command).label);
    switch (command) {
    case RowCommand::CopyAToB:
        return action + " → B";
    case RowCommand::CopyBToA:
        return action + " → A";
    case RowCommand::DeleteA:
        return action + " A";
    case RowCommand::DeleteB:
        return action + " B";
    case RowCommand::DeleteBoth:
        return action + " A+B";
    case RowCommand::Skip:
        return action;
    case RowCommand::Undo:
        return {};
    }
    return {};
}

std::string_view commandLabel(RowCommand command) noexcept
{
    switch (command) {
    case RowCommand::CopyAToB:
        return "A → B";
    case RowCommand::CopyBToA:
        return "B → A";
    case RowCommand::DeleteA:
        return "Delete A";
    case RowCommand::DeleteB:
        return "Delete B";
    case RowCommand::DeleteBoth:
        return "Delete both";
    case RowCommand::Skip:
        return "Skip";
    case RowCommand::Undo:
        return "Undo";
    }
    return {};
}

int progressPercent(std::size_t current, std::size_t total) noexcept
{
    if (total == 0 || current >= total) {
        return 100;
    }
    constexpr std::size_t hundred = 100;
    if (current <= std::numeric_limits<std::size_t>::max() / hundred) {
        return static_cast<int>((current * hundred) / total); // current < total: in [0, 99]
    }
    // Huge counts: current * 100 would overflow; current / (total / 100) is exact enough.
    return static_cast<int>(std::min(current / (total / hundred), std::size_t{99}));
}

std::string extensionLabel(const std::filesystem::path& file)
{
    const std::u8string extension = file.extension().u8string(); // ".jpeg", or empty
    std::string label;
    for (const char8_t character : extension) {
        label.push_back(static_cast<char>(character));
    }
    if (!label.empty()) {
        label.erase(0, 1); // the dot
    }
    return upperLabel(label);
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
