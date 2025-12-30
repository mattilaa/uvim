#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace text_utils
{

// --- ASCII classification (constexpr, locale-free) ---

constexpr char ascii_tolower(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr bool is_space(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
           c == '\v';
}

constexpr bool is_digit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

constexpr bool is_alpha(char c) noexcept
{
    c = ascii_tolower(c);
    return c >= 'a' && c <= 'z';
}

constexpr bool is_alnum(char c) noexcept
{
    return is_alpha(c) || is_digit(c);
}

constexpr bool is_xdigit(char c) noexcept
{
    if(is_digit(c))
        return true;
    c = ascii_tolower(c);
    return c >= 'a' && c <= 'f';
}

constexpr bool isIdent(char c) noexcept
{
    return is_alnum(c) || c == '_';
}

// --- string helpers (constexpr where possible) ---

constexpr bool iequals_ascii(std::string_view a, std::string_view b) noexcept
{
    if(a.size() != b.size())
        return false;
    for(size_t i = 0; i < a.size(); ++i)
        if(ascii_tolower(a[i]) != ascii_tolower(b[i]))
            return false;
    return true;
}

constexpr bool contains(std::string_view s, std::string_view needle) noexcept
{
    return s.find(needle) != std::string_view::npos;
}

// --- UTF-8 appending (runtime; std::string mutation not constexpr in C++20)
// ---

inline void appendU8(std::string& out, std::u8string_view s)
{
    out.append(reinterpret_cast<const char*>(s.data()), s.size());
}

inline void appendUtf8Repeat(std::string& out, std::u8string_view glyph,
                             int count)
{
    for(int i = 0; i < count; ++i)
        appendU8(out, glyph);
}

// --- operator key (constexpr) ---

constexpr std::uint16_t op_key(char a, char b) noexcept
{
    return static_cast<std::uint16_t>(static_cast<unsigned char>(a)) |
           (static_cast<std::uint16_t>(static_cast<unsigned char>(b)) << 8);
}

} // namespace text_utils
