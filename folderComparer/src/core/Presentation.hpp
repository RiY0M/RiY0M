#pragma once

// Data driven description of the UI (labels, colors, counters, buttons). Kept
// free of any GUI dependency so it can be unit tested and reused by any front-end.

#include "core/ComparisonResult.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fc {

/// Colors are "#RRGGBB" strings; an empty background means "default".
struct Style {
    std::string_view label;
    std::string_view color;
    std::string_view background;

    friend bool operator==(const Style&, const Style&) = default;
};

namespace colors {
inline constexpr std::string_view kBlack = "#000000";
inline constexpr std::string_view kGreen = "#008000";
inline constexpr std::string_view kGreenBackground = "#E8F5E9";
inline constexpr std::string_view kYellow = "#B88600";
inline constexpr std::string_view kYellowBackground = "#FFF8E1";
inline constexpr std::string_view kOrange = "#E07000";
inline constexpr std::string_view kOrangeBackground = "#FFF3E0";
inline constexpr std::string_view kTerracotta = "#A0522D";
inline constexpr std::string_view kTerracottaBackground = "#F6EBE1";
inline constexpr std::string_view kRed = "#CC0000";
inline constexpr std::string_view kRedBackground = "#FFEBEE";
inline constexpr std::string_view kGrey = "#616161";
inline constexpr std::string_view kGreyBackground = "#EEEEEE";
} // namespace colors

inline constexpr std::string_view kFontFamily = "Arial";

// ---------------------------------------------------------------------------
// Filters
// ---------------------------------------------------------------------------

enum class Filter : std::uint8_t {
    AllDifferences,
    Different,
    OnlyA,
    OnlyB,
    Skipped, ///< every row the user skipped, whatever its category
};

inline constexpr std::array<Filter, 5> kFilters{
    Filter::AllDifferences, Filter::Different, Filter::OnlyA, Filter::OnlyB, Filter::Skipped,
};
inline constexpr Filter kDefaultFilter = Filter::AllDifferences;

/// The category shown by a single-category filter (nullopt for "All differences" and "Skipped").
[[nodiscard]] std::optional<FileStatus> filterStatus(Filter filter) noexcept;

// ---------------------------------------------------------------------------
// Summary: one label per item; the difference items are also the filter buttons
// ---------------------------------------------------------------------------

enum class SummaryItem : std::uint8_t {
    Total,
    Identical,
    AllDifferences,
    Different,
    OnlyA,
    OnlyB,
    Skipped, ///< changes with the user's actions: counted by ComparisonSession
};

inline constexpr std::array<SummaryItem, 7> kSummaryItems{
    SummaryItem::Total, SummaryItem::Identical, SummaryItem::AllDifferences, SummaryItem::Different,
    SummaryItem::OnlyA, SummaryItem::OnlyB,     SummaryItem::Skipped,
};

[[nodiscard]] Style summaryStyle(SummaryItem item) noexcept;
/// Counts of an item computed from the comparison alone (Skipped is always 0
/// here: use ComparisonSession::summaryCounts for the live value).
[[nodiscard]] EntryCounts summaryCounts(SummaryItem item, const ComparisonResult& result) noexcept;
/// The filter selected by clicking the item (nullopt: plain label).
[[nodiscard]] std::optional<Filter> summaryFilter(SummaryItem item) noexcept;

/// "E(12) D(2) F(10)": entries (files + folders), folders, files.
[[nodiscard]] std::string formatCounts(const EntryCounts& counts);
/// "D(2) F(10)", shown on folder rows.
[[nodiscard]] std::string formatFolderContent(const EntryCounts& counts);

/// "Only A: E(12) D(2) F(10)" (the current filter is shown by the button's background).
[[nodiscard]] std::string summaryText(SummaryItem item, const EntryCounts& counts);
/// Same, with the counts of summaryCounts(item, result).
[[nodiscard]] std::string summaryText(SummaryItem item, const ComparisonResult& result);

/// Explanation of the E / D / F counters (tooltip).
[[nodiscard]] std::string_view countersHelp() noexcept;

// ---------------------------------------------------------------------------
// Row actions
// ---------------------------------------------------------------------------

enum class Side : std::uint8_t {
    A,
    B,
};

enum class CopyDirection : std::uint8_t {
    AToB,
    BToA,
};

enum class FileAction : std::uint8_t {
    Copy,
    Delete,
    Skip, ///< "seen, nothing to do": the files are left untouched
};

/// What a row button does. Undo reverts the action applied to the row; every
/// other command is an action.
enum class RowCommand : std::uint8_t {
    CopyAToB,
    CopyBToA,
    DeleteA,
    DeleteB,
    DeleteBoth, ///< Different files only
    Skip,
    Undo,
};

/// The action of a command (nullopt for Undo).
[[nodiscard]] std::optional<FileAction> commandAction(RowCommand command) noexcept;

[[nodiscard]] Style statusStyle(FileStatus status) noexcept;
[[nodiscard]] Style actionStyle(FileAction action) noexcept;
/// Style of a row carrying the action of `command`, also used to color the
/// command's button (an Undo command has none: default look).
[[nodiscard]] Style commandStyle(RowCommand command) noexcept;
/// "COPIED → B", "DELETED A", "SKIPPED" (Undo has no status text).
[[nodiscard]] std::string actionStatusText(RowCommand command);
/// "A → B", "Delete A", "Skip", "Undo"...
[[nodiscard]] std::string_view commandLabel(RowCommand command) noexcept;

/// Progress of `current` out of `total`, in whole percents (0-100, 100 when total is 0).
[[nodiscard]] int progressPercent(std::size_t current, std::size_t total) noexcept;

/// Upper-cased label, as displayed in a row ("ONLY A", "COPIED").
[[nodiscard]] std::string upperLabel(std::string_view label);

} // namespace fc
