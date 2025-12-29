#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

static inline bool is_directory(const std::filesystem::path& p)
{
    std::error_code ec;
    return fs::is_directory(p, ec);
}

static inline bool exists_nothrow(const fs::path& p)
{
    std::error_code ec;
    return fs::exists(p, ec);
}

static inline bool is_directory_nothrow(const fs::path& p)
{
    std::error_code ec;
    return fs::is_directory(p, ec);
}

static inline std::uintmax_t file_size_nothrow(const fs::path& p)
{
    std::error_code ec;
    auto sz = fs::file_size(p, ec);
    return ec ? 0 : sz; // 0 for "unknown / not a regular file / error"
}

static inline std::time_t mtime_nothrow(const fs::path& p)
{
    std::error_code ec;
    auto ft = fs::last_write_time(p, ec);
    if(ec)
        return 0;

    // Convert filesystem clock -> system_clock -> time_t (portable-ish C++20)
    auto sctp =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ft - fs::file_time_type::clock::now() +
            std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(sctp);
}
