#include "core/ActionHistory.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace fc {

namespace {

std::string randomToken()
{
    std::random_device device;
    std::uniform_int_distribution<unsigned int> digit(0, 15);
    constexpr std::string_view hex = "0123456789abcdef";
    std::string token;
    for (int index = 0; index < 16; ++index) {
        token.push_back(hex.at(digit(device)));
    }
    return token;
}

bool entryExists(const fs::path& path)
{
    std::error_code error;
    return fs::exists(fs::symlink_status(path, error));
}

fs::path partialPath(const fs::path& destination)
{
    fs::path partial = destination;
    partial += ".fc-partial";
    return partial;
}

} // namespace

ActionHistory::ActionHistory() : token_(randomToken()) {}

ActionHistory::~ActionHistory()
{
    discard();
}

fs::path ActionHistory::trashSlot(const fs::path& root, Id id, const fs::path& name)
{
    const fs::path trashFolder = root / kTrashFolderName / token_;
    trashFolders_.insert(trashFolder);
    return trashFolder / std::to_string(id) / name;
}

void ActionHistory::createParents(const fs::path& path)
{
    std::vector<fs::path> missing;
    for (fs::path parent = path.parent_path(); !parent.empty() && !entryExists(parent);
         parent = parent.parent_path()) {
        missing.push_back(parent);
    }
    if (missing.empty()) {
        return;
    }
    fs::create_directories(path.parent_path());
    createdFolders_.insert(missing.begin(), missing.end());
}

void ActionHistory::pruneCreatedFolders(const fs::path& path) noexcept
{
    // Remove the parents created by copies once they are empty again, even when
    // another (now undone) copy created them.
    for (fs::path parent = path.parent_path(); createdFolders_.contains(parent); parent = parent.parent_path()) {
        std::error_code error;
        if (!fs::is_empty(parent, error) || error || !fs::remove(parent, error) || error) {
            break;
        }
        createdFolders_.erase(parent);
    }
}

ActionHistory::Id ActionHistory::copy(const fs::path& source, const fs::path& destination,
                                      const fs::path& destinationRoot)
{
    if (!entryExists(source)) {
        throw SourceMissingError("Source no longer exists:\n\n" + toUtf8(source));
    }

    const Id id = nextId_++;
    const fs::path partial = partialPath(destination);
    std::error_code ignored;
    fs::remove_all(partial, ignored); // left over by a crash

    try {
        createParents(destination);
        FileManager::copyEntry(source, partial);

        std::optional<fs::path> backup;
        if (entryExists(destination)) {
            backup = trashSlot(destinationRoot, id, destination.filename());
            FileManager::moveEntry(destination, *backup);
        }

        try {
            fs::rename(partial, destination);
        }
        catch (...) {
            if (backup) {
                std::error_code restoreError;
                fs::rename(*backup, destination, restoreError);
            }
            throw;
        }

        records_.emplace(id, Record{.isCopy = true, .target = destination, .root = destinationRoot,
                                    .backup = std::move(backup)});
        return id;
    }
    catch (...) {
        fs::remove_all(partial, ignored);
        pruneCreatedFolders(destination);
        throw;
    }
}

ActionHistory::Id ActionHistory::remove(const fs::path& target, const fs::path& root)
{
    if (!entryExists(target)) {
        throw SourceMissingError("File no longer exists:\n\n" + toUtf8(target));
    }

    const Id id = nextId_++;
    const fs::path backup = trashSlot(root, id, target.filename());
    FileManager::moveEntry(target, backup);
    records_.emplace(id, Record{.isCopy = false, .target = target, .root = root, .backup = backup});
    return id;
}

void ActionHistory::undo(Id id)
{
    const auto found = records_.find(id);
    if (found == records_.end()) {
        throw std::invalid_argument("Unknown action");
    }
    const Record& record = found->second;

    if (record.isCopy) {
        fs::remove_all(record.target); // our copy
        if (record.backup) {
            FileManager::moveEntry(*record.backup, record.target);
        }
        else {
            pruneCreatedFolders(record.target);
        }
    }
    else if (record.backup) {
        if (entryExists(record.target)) {
            throw std::runtime_error("Cannot restore, something now exists at:\n\n" + toUtf8(record.target));
        }
        FileManager::moveEntry(*record.backup, record.target);
    }

    if (record.backup) {
        std::error_code ignored;
        fs::remove(record.backup->parent_path(), ignored); // the now empty <id> folder
    }
    records_.erase(found);
}

void ActionHistory::discard() noexcept
{
    for (const fs::path& trashFolder : trashFolders_) {
        std::error_code ignored;
        fs::remove_all(trashFolder, ignored);
        fs::remove(trashFolder.parent_path(), ignored); // the trash root, if now empty
    }
    trashFolders_.clear();
    records_.clear();
    createdFolders_.clear();
}

bool ActionHistory::holdsBackups() const noexcept
{
    return std::ranges::any_of(records_, [](const auto& entry) { return entry.second.backup.has_value(); });
}

} // namespace fc
