#pragma once

// RAII temporary folder used to build file trees for the tests.

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace fc::test {

class TempDir {
public:
    TempDir()
    {
        std::random_device device;
        std::mt19937_64 generator(device());
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = std::filesystem::temp_directory_path() /
                                   ("fc-test-" + std::to_string(generator()));
            if (std::filesystem::create_directory(candidate)) {
                path_ = candidate;
                return;
            }
        }
        throw std::runtime_error("Cannot create a temporary folder");
    }

    ~TempDir()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    [[nodiscard]] std::filesystem::path operator/(const std::filesystem::path& relative) const
    {
        return path_ / relative;
    }

    /// Create (or overwrite) a file, creating its parent folders.
    // NOLINTNEXTLINE(modernize-use-nodiscard): the returned path is optional
    std::filesystem::path write(const std::filesystem::path& relative, std::string_view content) const
    {
        const std::filesystem::path file = path_ / relative;
        std::filesystem::create_directories(file.parent_path());
        std::ofstream stream(file, std::ios::binary | std::ios::trunc);
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!stream) {
            throw std::runtime_error("Cannot write test file");
        }
        return file;
    }

    // NOLINTNEXTLINE(modernize-use-nodiscard): the returned path is optional
    std::filesystem::path makeFolder(const std::filesystem::path& relative) const
    {
        const std::filesystem::path folder = path_ / relative;
        std::filesystem::create_directories(folder);
        return folder;
    }

private:
    std::filesystem::path path_;
};

inline std::string readFile(const std::filesystem::path& file)
{
    // Sized read rather than istreambuf_iterator: GCC 13 at -O3 reports a false
    // -Wnull-dereference inside libstdc++ for the iterator version.
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Cannot read test file");
    }
    const std::uintmax_t size = std::filesystem::file_size(file);
    std::string content(size, '\0');
    stream.read(content.data(), static_cast<std::streamsize>(size));
    if (!stream) {
        throw std::runtime_error("Cannot read test file");
    }
    return content;
}

} // namespace fc::test
