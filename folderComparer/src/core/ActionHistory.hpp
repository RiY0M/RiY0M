#pragma once

// File system actions that can be undone.
//
// Nothing is ever destroyed while an action can still be undone: a deleted
// file or folder, and a destination overwritten by a copy, are moved into a
// trash folder (kTrashFolderName/<session token>/<action id>/) at the root of
// the compared folder they belong to. Same folder tree, so the move is an
// instant rename. discard() (also run by the destructor) empties that trash,
// which makes every action permanent.

#include "core/FileManager.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace fc {

class ActionHistory {
public:
    using Id = std::uint64_t;

    ActionHistory();
    ~ActionHistory();

    ActionHistory(const ActionHistory&) = delete;
    ActionHistory& operator=(const ActionHistory&) = delete;
    ActionHistory(ActionHistory&&) = delete;
    ActionHistory& operator=(ActionHistory&&) = delete;

    /// Copy a file, or a folder and its whole content, to `destination`
    /// (inside the compared folder `destinationRoot`), creating the missing
    /// parent folders. An existing destination is moved to the trash. The copy
    /// is built next to the destination then renamed, so a failure leaves the
    /// destination untouched. Throws SourceMissingError if `source` vanished.
    Id copy(const std::filesystem::path& source, const std::filesystem::path& destination,
            const std::filesystem::path& destinationRoot);

    /// Move a file or a folder (inside the compared folder `root`) to the trash.
    Id remove(const std::filesystem::path& target, const std::filesystem::path& root);

    /// Revert an action. Throws std::invalid_argument for an unknown id, and
    /// std::runtime_error / std::filesystem::filesystem_error when the file
    /// system prevents it (the action is then kept and can be retried).
    void undo(Id id);

    /// Forget every action and empty the trash: every action becomes permanent.
    void discard() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }
    [[nodiscard]] bool contains(Id id) const noexcept { return records_.contains(id); }

    /// True when undoing is the only way to get some data back (a deletion or
    /// an overwritten destination): discard() would then lose it.
    [[nodiscard]] bool holdsBackups() const noexcept;

private:
    struct Record {
        bool isCopy = false;
        std::filesystem::path target;                ///< copied destination / deleted path
        std::filesystem::path root;                  ///< compared folder holding `target`
        std::optional<std::filesystem::path> backup; ///< trash copy of what `target` was
    };

    [[nodiscard]] std::filesystem::path trashSlot(const std::filesystem::path& root, Id id,
                                                  const std::filesystem::path& name);
    void createParents(const std::filesystem::path& path);
    void pruneCreatedFolders(const std::filesystem::path& path) noexcept;

    std::string token_;
    Id nextId_ = 1;
    std::map<Id, Record> records_;
    std::set<std::filesystem::path, PathLess> createdFolders_; ///< parents created by copies
    std::set<std::filesystem::path, PathLess> trashFolders_;   ///< <root>/<trash>/<token>
};

} // namespace fc
