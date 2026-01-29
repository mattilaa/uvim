#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace constants
{

namespace detail
{
inline bool iequals_suffix(std::string_view str, std::string_view suffix)
{
    if(suffix.size() > str.size())
        return false;
    auto str_end = str.substr(str.size() - suffix.size());
    return std::ranges::equal(
        str_end, suffix,
        [](char a, char b)
        {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
}

inline std::string_view basename(std::string_view path)
{
    size_t slash = path.find_last_of("/\\");
    return (slash == std::string_view::npos) ? path : path.substr(slash + 1);
}

inline std::string ascii_lower(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for(char c : value)
        out.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}
} // namespace detail

// Pattern matching utility for file detection
template <std::size_t N, std::size_t M>
bool matches_file_patterns(std::string_view filename,
                           const std::array<std::string_view, N>& prefixes,
                           const std::array<std::string_view, M>& suffixes,
                           bool case_sensitive = false)
{
    auto starts = [&](std::string_view p) { return filename.starts_with(p); };
    auto ends = [&](std::string_view p)
    {
        return case_sensitive ? filename.ends_with(p)
                              : detail::iequals_suffix(filename, p);
    };

    return std::ranges::any_of(prefixes, starts) ||
           std::ranges::any_of(suffixes, ends);
}

// Overload for suffix-only matching (most common case)
template <std::size_t N>
bool matches_file_patterns(std::string_view filename,
                           const std::array<std::string_view, N>& suffixes,
                           bool case_sensitive = false)
{
    return std::ranges::any_of(
        suffixes,
        [&](std::string_view p)
        {
            return case_sensitive ? filename.ends_with(p)
                                  : detail::iequals_suffix(filename, p);
        });
}

template <std::size_t N>
bool matches_path_substrings(std::string_view path,
                             const std::array<std::string_view, N>& patterns,
                             bool case_sensitive = false)
{
    if(case_sensitive)
    {
        return std::ranges::any_of(
            patterns, [&](std::string_view pattern)
            { return path.find(pattern) != std::string_view::npos; });
    }
    std::string lower_path = detail::ascii_lower(path);
    return std::ranges::any_of(
        patterns,
        [&](std::string_view pattern)
        {
            std::string lower_pattern = detail::ascii_lower(pattern);
            return lower_path.find(lower_pattern) != std::string_view::npos;
        });
}

// Empty array helper for when you only want prefix or suffix matching
inline constexpr std::array<std::string_view, 0> no_pattern{};

// File extension/suffix patterns (case-insensitive matching by default)
inline constexpr std::array cpp_suffixes = {
    std::string_view{".cpp"}, std::string_view{".cc"},
    std::string_view{".cxx"}, std::string_view{".h"},
    std::string_view{".hpp"}, std::string_view{".hxx"},
    std::string_view{".c"},   std::string_view{".ixx"},
    std::string_view{".tpp"}, std::string_view{".inl"}};

inline constexpr std::array mla_suffixes = {std::string_view{".mla"}};

inline constexpr std::array robot_suffixes = {
    std::string_view{".robot"},
    std::string_view{".resource"},
    std::string_view{".robotframework"},
};

inline constexpr std::array html_suffixes = {
    std::string_view{".html"}, std::string_view{".htm"},
    std::string_view{".xhtml"}, std::string_view{".shtml"}};

inline constexpr std::array html_void_tags = {
    std::string_view{"area"},  std::string_view{"base"},
    std::string_view{"br"},    std::string_view{"col"},
    std::string_view{"embed"}, std::string_view{"hr"},
    std::string_view{"img"},   std::string_view{"input"},
    std::string_view{"link"},  std::string_view{"meta"},
    std::string_view{"param"}, std::string_view{"source"},
    std::string_view{"track"}, std::string_view{"wbr"},
};

inline constexpr std::array xml_suffixes = {
    std::string_view{".xml"},  std::string_view{".xsl"},
    std::string_view{".xslt"}, std::string_view{".xsd"},
    std::string_view{".svg"},  std::string_view{".plist"}};

inline constexpr std::array markdown_suffixes = {
    std::string_view{".md"}, std::string_view{".markdown"},
    std::string_view{".mkd"}, std::string_view{".mdx"}};

inline constexpr std::array markup_text_suffixes = {
    std::string_view{".txt"},      std::string_view{".md"},
    std::string_view{".markdown"}, std::string_view{".rd"},
    std::string_view{".rdoc"},
};

inline constexpr std::array markup_readme_basenames = {
    std::string_view{"README.rd"},
};

inline constexpr std::array python_suffixes = {
    std::string_view{".py"}, std::string_view{".pyw"}, std::string_view{".pyi"},
    std::string_view{".pyx"}};

inline constexpr std::array javascript_suffixes = {
    std::string_view{".js"}, std::string_view{".jsx"}, std::string_view{".mjs"},
    std::string_view{".cjs"}};

inline constexpr std::array typescript_suffixes = {
    std::string_view{".ts"}, std::string_view{".tsx"}, std::string_view{".mts"},
    std::string_view{".cts"}};

inline constexpr std::array json_suffixes = {std::string_view{".json"},
                                             std::string_view{".jsonc"},
                                             std::string_view{".json5"}};

inline constexpr std::array yaml_suffixes = {std::string_view{".yaml"},
                                             std::string_view{".yml"}};

inline constexpr std::array toml_suffixes = {std::string_view{".toml"}};

inline constexpr std::array rust_suffixes = {std::string_view{".rs"}};

inline constexpr std::array go_suffixes = {std::string_view{".go"}};

inline constexpr std::array shell_suffixes = {
    std::string_view{".sh"},      std::string_view{".bash"},
    std::string_view{".zsh"},     std::string_view{".fish"},
    std::string_view{".ksh"},     std::string_view{".dash"},
    std::string_view{".profile"},
};

inline constexpr std::array shell_basenames = {
    std::string_view{".bashrc"},      std::string_view{".bash_profile"},
    std::string_view{".bash_logout"}, std::string_view{".zshrc"},
    std::string_view{".zprofile"},    std::string_view{".zlogin"},
    std::string_view{".zlogout"},     std::string_view{".kshrc"},
};

inline constexpr std::array shell_shebang_hints = {
    std::string_view{"bash"},
    std::string_view{"zsh"},
    std::string_view{"ksh"},
    std::string_view{"dash"},
};

inline constexpr std::array css_suffixes = {
    std::string_view{".css"}, std::string_view{".scss"},
    std::string_view{".sass"}, std::string_view{".less"}};

inline constexpr std::array cmake_suffixes = {std::string_view{".cmake"}};

inline constexpr std::array cmake_prefixes = {
    std::string_view{"CMakeLists"},
    std::string_view{"CMakeFiles"},
    std::string_view{"CMakeCache"},
};

// Path patterns for stdlib headers (no extension)
inline constexpr std::array cpp_stdlib_patterns = {
    std::string_view{"/c++/"}, std::string_view{"/bits/"},
    std::string_view{"/ext/"}, std::string_view{"/__"}};

template <const auto& Prefixes, const auto& Suffixes,
          const auto& Substrings = no_pattern>
bool is_filetype(std::string_view path, bool case_sensitive = false)
{
    std::string_view base = detail::basename(path);
    if(matches_file_patterns(base, Prefixes, Suffixes, case_sensitive))
        return true;
    if constexpr(Substrings.size() > 0)
    {
        return matches_path_substrings(path, Substrings, case_sensitive);
    }
    return false;
}

} // namespace constants
