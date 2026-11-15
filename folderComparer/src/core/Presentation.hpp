#pragma once

// Data driven description of the UI, mirroring the SUMMARY_LABELS, FILTERS and
// FILE_ACTIONS tables of the Python App. Kept free of any GUI dependency so it
// can be unit tested and reused by any front-end.

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
};

namespace colors {
inline constexpr std::string_view kBlack = "#000000";
inline constexpr std::string_view kGreen = "#008000";
inline constexpr std::string_view kGreenBackground = "#E8F5E9";
inline constexpr std::string_view kYellow = "#B88600";
inline constexpr std::string_view kYellowBackground = "#FFF8E1";
inline constexpr std::string_view kOrange = "#E07000";
inline constexpr std::string_view kOrangeBackground = "#FFF3E0";
inline constexpr std::string_view kRed = "#CC0000";
inline constexpr std::string_view kRedBackground = "#FFEBEE";
} // namespace colors

inline constexpr std::string_view kFontFamily = "Arial";

// ---------------------------------------------------------------------------
// Summary
// ---------------------------------------------------------------------------

enum class SummaryItem : std::uint8_t {
    Total,
    Identical,
    AllDifferences,
    Different,
    OnlyA,
    OnlyB,
};

inline constexpr std::array<SummaryItem, 6> kSummaryItems{
    SummaryItem::Total,     SummaryItem::Identical, SummaryItem::AllDifferences,
    SummaryItem::Different, SummaryItem::OnlyA,     SummaryItem::OnlyB,
};

[[nodiscard]] Style summaryStyle(SummaryItem item) noexcept;
[[nodiscard]] std::size_t summaryCount(SummaryItem item, const ComparisonResult& result) noexcept;
/// "Total: 12"
[[nodiscard]] std::string summaryText(SummaryItem item, const ComparisonResult& result);

// ---------------------------------------------------------------------------
// Filters
// ---------------------------------------------------------------------------

enum class Filter : std::uint8_t {
    AllDifferences,
    Different,
    OnlyA,
    OnlyB,
};

inline constexpr std::array<Filter, 4> kFilters{
    Filter::AllDifferences,
    Filter::Different,
    Filter::OnlyA,
    Filter::OnlyB,
};
inline constexpr Filter kDefaultFilter = Filter::AllDifferences;

[[nodiscard]] std::string_view filterLabel(Filter filter) noexcept;
/// "● All" for the current filter, "Different" otherwise.
[[nodiscard]] std::string filterButtonText(Filter filter, Filter currentFilter);
/// The category shown by a single-category filter (nullopt for "All").
[[nodiscard]] std::optional<FileStatus> filterStatus(Filter filter) noexcept;

// ---------------------------------------------------------------------------
// File rows
// ---------------------------------------------------------------------------

/// Actions that can be applied to a file row. Only Copy is wired to the UI
/// today (Move / Skip / Delete are listed in the README as improvements).
enum class FileAction : std::uint8_t {
    Copy,
    Move,
    Skip,
    Delete,
};

enum class CopyDirection : std::uint8_t {
    AToB,
    BToA,
};

[[nodiscard]] Style statusStyle(FileStatus status) noexcept;
[[nodiscard]] Style actionStyle(FileAction action) noexcept;

/// Upper-cased label, as displayed in a row ("ONLY A", "COPIED").
[[nodiscard]] std::string upperLabel(std::string_view label);

} // namespace fc
