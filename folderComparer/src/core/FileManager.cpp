#include "core/FileManager.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <ios>
#include <locale>
#include <span>
#include <sstream>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace fc {

std::string toUtf8(const fs::path& path)
{
    const std::u8string utf8 = path.u8string();
    std::string result;
    result.reserve(utf8.size());
    for (const char8_t character : utf8) {
        result.push_back(static_cast<char>(character));
    }
    return result;
}

bool pathLess(const fs::path& left, const fs::path& right) noexcept
{
    return left.native() < right.native();
}

namespace FileManager {

namespace {

constexpr std::size_t kChunkSize = std::size_t{1024} * 1024;

std::ifstream openForReading(const fs::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Cannot open file: " + toUtf8(file));
    }
    return stream;
}

std::streamsize readChunk(std::ifstream& stream, std::vector<char>& buffer, const fs::path& file)
{
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (stream.bad()) {
        throw std::runtime_error("Cannot read file: " + toUtf8(file));
    }
    return stream.gcount();
}

bool sameBytes(const fs::path& fileA, const fs::path& fileB, const std::stop_token& stopToken)
{
    std::ifstream streamA = openForReading(fileA);
    std::ifstream streamB = openForReading(fileB);

    std::vector<char> bufferA(kChunkSize);
    std::vector<char> bufferB(kChunkSize);

    while (true) {
        if (stopToken.stop_requested()) {
            throw OperationCancelled{};
        }

        const std::streamsize countA = readChunk(streamA, bufferA, fileA);
        const std::streamsize countB = readChunk(streamB, bufferB, fileB);

        if (countA != countB) {
            return false; // a file changed while being compared
        }

        const auto count = static_cast<std::size_t>(countA);
        if (!std::ranges::equal(std::span(bufferA).first(count), std::span(bufferB).first(count))) {
            return false;
        }

        if (count < kChunkSize) {
            return true; // both streams reached their end
        }
    }
}

fs::path partialCopyPath(const fs::path& destination)
{
    fs::path partial = destination;
    partial += ".fc-partial";
    return partial;
}

} // namespace

std::vector<fs::path> getFiles(const fs::path& folder, bool recursive)
{
    std::vector<fs::path> files;
    std::vector<fs::path> pending{fs::path{}}; // folders still to list, relative to `folder`

    while (!pending.empty()) {
        const fs::path relativeFolder = pending.back();
        pending.pop_back();
        const fs::path currentFolder = relativeFolder.empty() ? folder : folder / relativeFolder;
        const bool isRoot = relativeFolder.empty();

        std::error_code error;
        fs::directory_iterator iterator(currentFolder, fs::directory_options::skip_permission_denied, error);
        if (error) {
            if (isRoot) {
                throw fs::filesystem_error("Cannot list folder", currentFolder, error);
            }
            continue; // like os.walk: unreadable sub-folders are ignored
        }

        for (const fs::directory_iterator end; iterator != end; iterator.increment(error)) {
            const fs::directory_entry& entry = *iterator;
            const fs::path relativePath = relativeFolder / entry.path().filename();

            std::error_code statusError;
            if (entry.is_regular_file(statusError)) {
                files.push_back(relativePath);
            }
            else if (recursive && entry.is_directory(statusError) && !entry.is_symlink(statusError)) {
                pending.push_back(relativePath);
            }
        }

        if (error && isRoot) {
            throw fs::filesystem_error("Cannot list folder", currentFolder, error);
        }
    }

    std::ranges::sort(files, pathLess);
    return files;
}

bool filesAreIdentical(const fs::path& fileA, const fs::path& fileB, bool deepComparison,
                       const std::stop_token& stopToken)
{
    if (fs::file_size(fileA) != fs::file_size(fileB)) {
        return false;
    }

    if (!deepComparison) {
        return true;
    }

    // Byte comparison instead of the Python SHA-256: same answer without any
    // collision risk, and it stops at the first difference.
    return sameBytes(fileA, fileB, stopToken);
}

void copyFile(const fs::path& source, const fs::path& destination)
{
    std::error_code existsError;
    if (!fs::exists(source, existsError)) {
        throw SourceMissingError("Source file no longer exists:\n\n" + toUtf8(source));
    }

    if (const fs::path parent = destination.parent_path(); !parent.empty()) {
        fs::create_directories(parent);
    }

    const fs::path partial = partialCopyPath(destination);
    try {
        fs::copy_file(source, partial, fs::copy_options::overwrite_existing);
        fs::last_write_time(partial, fs::last_write_time(source));

        std::error_code renameError;
        fs::rename(partial, destination, renameError);
        if (renameError && fs::is_regular_file(destination)) {
            // Some Windows standard libraries refuse to rename over an existing file.
            fs::remove(destination);
            fs::rename(partial, destination);
        }
        else if (renameError) {
            throw fs::filesystem_error("Cannot move the copied file into place", partial, destination,
                                       renameError);
        }
    }
    catch (...) {
        std::error_code cleanupError;
        fs::remove(partial, cleanupError);
        throw;
    }
}

std::string formatSize(std::uintmax_t size)
{
    constexpr std::array<std::string_view, 5> units{"B", "KB", "MB", "GB", "TB"};
    constexpr double step = 1024.0;

    auto format = [](double value, std::string_view unit) {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::fixed << std::setprecision(1) << value << ' ' << unit;
        return stream.str();
    };

    auto value = static_cast<double>(size);
    for (const std::string_view unit : units) {
        if (value < step) {
            return format(value, unit);
        }
        value /= step;
    }
    return format(value, "PB");
}

std::string formatLocalTime(std::chrono::system_clock::time_point time)
{
    const std::time_t seconds = std::chrono::system_clock::to_time_t(
        std::chrono::floor<std::chrono::seconds>(time));

    std::tm local{};
#if defined(_WIN32)
    if (localtime_s(&local, &seconds) != 0) {
        throw std::runtime_error("Cannot convert time to local time");
    }
#else
    if (localtime_r(&seconds, &local) == nullptr) {
        throw std::runtime_error("Cannot convert time to local time");
    }
#endif

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

FileInfo getFileInfo(const fs::path& file)
{
    FileInfo info;
    info.size = fs::file_size(file);
    info.sizeText = formatSize(info.size);

    const fs::file_time_type fileTime = fs::last_write_time(file);
#if defined(_MSC_VER)
    const auto systemTime = std::chrono::clock_cast<std::chrono::system_clock>(fileTime);
#else
    const auto systemTime = std::chrono::file_clock::to_sys(fileTime);
#endif
    info.modified = std::chrono::time_point_cast<std::chrono::system_clock::duration>(systemTime);
    info.modifiedText = formatLocalTime(info.modified);
    return info;
}

} // namespace FileManager
} // namespace fc
