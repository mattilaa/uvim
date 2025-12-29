#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace file_utils
{

enum class LinkPolicy
{
    Follow,
    NoFollow
};

static constexpr LinkPolicy kLinkPolicy = LinkPolicy::Follow;

static inline bool is_directory(const std::filesystem::path& p)
{
    std::error_code ec;
    return fs::is_directory(p, ec);
}

static inline std::string path_to_utf8_string(const fs::path& p)
{
#if defined(__cpp_char8_t) && (__cpp_char8_t >= 201811L)
    auto u8 = p.u8string(); // std::u8string
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
#else
    // Pre-C++20 fallback; on most POSIX systems this is UTF-8 already.
    return p.u8string();
#endif
}

static inline std::time_t to_time_t(fs::file_time_type ft)
{
    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(
        ft - fs::file_time_type::clock::now() + system_clock::now());
    return system_clock::to_time_t(sctp);
}

static inline fs::file_status status_with_policy(const fs::path& p,
                                                 std::error_code& ec)
{
    if(kLinkPolicy == LinkPolicy::Follow)
        return fs::status(p, ec);     // follows symlinks
    return fs::symlink_status(p, ec); // does not follow
}

static inline size_t file_size_to_size_t(const fs::path& p)
{
    std::error_code ec;
    auto sz = fs::file_size(p, ec); // follows symlinks to target
    if(ec)
        return 0;

    if(sz > static_cast<std::uintmax_t>(std::numeric_limits<size_t>::max()))
        return std::numeric_limits<size_t>::max();

    return static_cast<size_t>(sz);
}

static inline time_t mtime_nothrow(const fs::path& p)
{
    std::error_code ec;
    auto ft = fs::last_write_time(p, ec); // typically follows symlinks
    if(ec)
        return 0;
    return to_time_t(ft);
}

static inline bool is_hidden_name(const std::string& name)
{
    return !name.empty() && name[0] == '.';
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

} // namespace file_utils
