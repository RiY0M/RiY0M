#include "core/LiveCounts.hpp"

#include <set>

namespace fs = std::filesystem;

namespace fc {

const EntryCounts& CategoryCounts::of(FileStatus status) const noexcept
{
    switch (status) {
    case FileStatus::Identical:
        return identical;
    case FileStatus::Different:
        return different;
    case FileStatus::OnlyA:
        return onlyA;
    case FileStatus::OnlyB:
        return onlyB;
    }
    return identical; // unreachable: every enumerator is handled above
}

EntryCounts CategoryCounts::allDifferences() const noexcept
{
    EntryCounts counts = different;
    counts += onlyA;
    counts += onlyB;
    return counts;
}

EntryCounts CategoryCounts::total() const noexcept
{
    EntryCounts counts = identical;
    counts += allDifferences();
    return counts;
}

bool LiveCounts::KeyLess::operator()(const Key& left, const Key& right) const noexcept
{
    if (pathLess(left.path, right.path)) {
        return true;
    }
    if (pathLess(right.path, left.path)) {
        return false;
    }
    return left.kind < right.kind;
}

void LiveCounts::reset(const ComparisonResult& result)
{
    base_.clear();

    const auto add = [this](const std::vector<fs::path>& paths, Kind kind, State state) {
        for (const fs::path& path : paths) {
            base_.insert_or_assign(Key{path, kind}, state);
        }
    };

    add(result.identical, Kind::File, State{.inA = true, .inB = true, .sameContent = true});
    add(result.different, Kind::File, State{.inA = true, .inB = true, .sameContent = false});
    add(result.onlyA, Kind::File, State{.inA = true, .inB = false, .sameContent = false});
    add(result.onlyB, Kind::File, State{.inA = false, .inB = true, .sameContent = false});
    add(result.identicalFolders, Kind::Folder, State{.inA = true, .inB = true, .sameContent = false});
    add(result.differentFolders, Kind::Folder, State{.inA = true, .inB = true, .sameContent = false});
    add(result.onlyAFolders, Kind::Folder, State{.inA = true, .inB = false, .sameContent = false});
    add(result.onlyBFolders, Kind::Folder, State{.inA = false, .inB = true, .sameContent = false});
}

template <typename Function>
void LiveCounts::forEachAtOrBelow(States& states, const fs::path& path, Function function)
{
    for (const Kind kind : {Kind::File, Kind::Folder}) {
        if (const auto found = states.find(Key{path, kind}); found != states.end()) {
            function(found->second);
        }
    }

    // Descendants are contiguous in the native-string ordering, right after "path/".
    fs::path::string_type prefix = path.native();
    prefix.push_back(fs::path::preferred_separator);
    for (auto entry = states.lower_bound(Key{fs::path(prefix), Kind::File});
         entry != states.end() && entry->first.path.native().starts_with(prefix); ++entry) {
        function(entry->second);
    }
}

CategoryCounts LiveCounts::compute(const std::vector<Action>& actions) const
{
    States states = base_;

    for (const auto& [path, command] : actions) {
        switch (command) {
        case RowCommand::CopyAToB:
        case RowCommand::CopyBToA: {
            const bool toB = command == RowCommand::CopyAToB;
            // The destination is replaced by the source: identical copies.
            forEachAtOrBelow(states, path, [toB](State& state) {
                const bool inSource = toB ? state.inA : state.inB;
                (toB ? state.inB : state.inA) = inSource;
                state.sameContent = inSource;
            });
            // The parent folders the copy created.
            for (fs::path parent = path.parent_path(); !parent.empty(); parent = parent.parent_path()) {
                if (const auto folder = states.find(Key{parent, Kind::Folder}); folder != states.end()) {
                    (toB ? folder->second.inB : folder->second.inA) = true;
                }
            }
            break;
        }
        case RowCommand::DeleteA:
            forEachAtOrBelow(states, path, [](State& state) { state.inA = false; });
            break;
        case RowCommand::DeleteB:
            forEachAtOrBelow(states, path, [](State& state) { state.inB = false; });
            break;
        case RowCommand::DeleteBoth:
            forEachAtOrBelow(states, path, [](State& state) {
                state.inA = false;
                state.inB = false;
            });
            break;
        case RowCommand::Skip:
        case RowCommand::Undo:
            break; // nothing changes on disk
        }
    }

    // Classify again.
    CategoryCounts counts;
    std::set<fs::path, PathLess> foldersWithDifferences;
    std::vector<fs::path> commonFolders;

    const auto markParents = [&foldersWithDifferences](const fs::path& path) {
        for (fs::path parent = path.parent_path(); !parent.empty(); parent = parent.parent_path()) {
            if (!foldersWithDifferences.insert(parent).second) {
                break; // its own parents are already marked
            }
        }
    };
    const auto countIn = [](EntryCounts& target, Kind kind) {
        ++(kind == Kind::File ? target.files : target.folders);
    };

    for (const auto& [key, state] : states) {
        if (state.inA && state.inB) {
            if (key.kind == Kind::Folder) {
                commonFolders.push_back(key.path); // classified once every difference is known
            }
            else if (state.sameContent) {
                ++counts.identical.files;
            }
            else {
                ++counts.different.files;
                markParents(key.path);
            }
        }
        else if (state.inA || state.inB) {
            countIn(state.inA ? counts.onlyA : counts.onlyB, key.kind);
            markParents(key.path);
        }
        // on no side anymore: deleted, not counted
    }

    for (const fs::path& folder : commonFolders) {
        ++(foldersWithDifferences.contains(folder) ? counts.different.folders : counts.identical.folders);
    }
    return counts;
}

} // namespace fc
