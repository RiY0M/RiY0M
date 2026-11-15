#pragma once

// Summary counts that follow the actions applied by the application.
//
// The comparison gives, for every file and folder, the sides it exists on and
// whether a file has the same content on both. The actions still applied are
// replayed on that state, then everything is classified again:
//  - copy X → Y of a path: whatever Y had at or below the path is replaced by
//    X's content (identical files), and the parent folders it needed now exist on Y;
//  - delete on X of a path: nothing remains on X at or below the path;
//  - skip: no change.
// A folder present on both sides is Different when anything inside differs.
// Undoing an action simply removes it from the replayed list.
//
// Changes made outside the application are not tracked: the state is the one
// of the comparison plus the application's own actions.

#include "core/ComparisonResult.hpp"
#include "core/FileManager.hpp"
#include "core/Presentation.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <utility>
#include <vector>

namespace fc {

/// Counts per category (Identical, Different, OnlyA, OnlyB).
struct CategoryCounts {
    EntryCounts identical;
    EntryCounts different;
    EntryCounts onlyA;
    EntryCounts onlyB;

    [[nodiscard]] const EntryCounts& of(FileStatus status) const noexcept;
    [[nodiscard]] EntryCounts allDifferences() const noexcept;
    [[nodiscard]] EntryCounts total() const noexcept;

    friend bool operator==(const CategoryCounts&, const CategoryCounts&) = default;
};

class LiveCounts {
public:
    /// An applied action: the row path and its command.
    using Action = std::pair<std::filesystem::path, RowCommand>;

    void reset(const ComparisonResult& result);

    /// Counts once `actions` are applied to the compared folders.
    [[nodiscard]] CategoryCounts compute(const std::vector<Action>& actions) const;

private:
    enum class Kind : std::uint8_t {
        File,
        Folder,
    };

    struct State {
        bool inA = false;
        bool inB = false;
        bool sameContent = false; ///< files present on both sides
    };

    /// A path can be a folder on one side and a file on the other.
    struct Key {
        std::filesystem::path path;
        Kind kind = Kind::File;
    };

    struct KeyLess {
        bool operator()(const Key& left, const Key& right) const noexcept;
    };

    using States = std::map<Key, State, KeyLess>;

    template <typename Function>
    static void forEachAtOrBelow(States& states, const std::filesystem::path& path, Function function);

    States base_;
};

} // namespace fc
