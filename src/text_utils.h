#pragma once
#include <climits>
#include <codecvt>
#include <cwchar>
#include <cstddef>
#include <cstdint>
#include <locale>
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

inline int prevUtf8CharStart(std::string_view s, int index)
{
    if(index <= 0)
        return 0;
    if(index > (int)s.size())
        index = (int)s.size();
    int i = index - 1;
    while(i > 0)
    {
        unsigned char c = (unsigned char)s[i];
        if((c & 0xC0) != 0x80)
            break;
        --i;
    }
    return i;
}

inline int nextUtf8CharStart(std::string_view s, int index)
{
    if(index < 0)
        return 0;
    if(index >= (int)s.size())
        return (int)s.size();
    int i = index + 1;
    while(i < (int)s.size())
    {
        unsigned char c = (unsigned char)s[i];
        if((c & 0xC0) != 0x80)
            break;
        ++i;
    }
    return i;
}

inline int utf8DisplayWidth(std::string_view s)
{
    int width = 0;
    std::mbstate_t st{};
    const char* p = s.data();
    size_t len = s.size();

    while(len > 0)
    {
        wchar_t wc = 0;
        size_t n = std::mbrtowc(&wc, p, len, &st);
        if(n == static_cast<size_t>(-1) || n == static_cast<size_t>(-2))
        {
            // Invalid sequence: treat byte as width 1.
            ++width;
            ++p;
            --len;
            std::mbstate_t reset{};
            st = reset;
            continue;
        }
        if(n == 0)
            break;

        int w = ::wcwidth(wc);
        if(w < 0)
            w = 1;
        width += w;
        p += n;
        len -= n;
    }
    return width;
}

// Convert a UTF-8 byte offset in a line to UTF-16 code units (LSP uses UTF-16).
inline int utf8ByteOffsetToUtf16(std::string_view line, int byteOffset)
{
    if(byteOffset <= 0)
        return 0;
    if(byteOffset > (int)line.size())
        byteOffset = (int)line.size();

    int u16 = 0;
    int i = 0;
    int safeLen = 0;
    while(i < byteOffset)
    {
        unsigned char c = (unsigned char)line[i];
        int codepoint = 0;
        int len = 1;

        if(c < 0x80)
        {
            codepoint = c;
            len = 1;
        }
        else if((c & 0xE0) == 0xC0 && i + 1 < (int)line.size())
        {
            codepoint =
                ((c & 0x1F) << 6) | ((unsigned char)line[i + 1] & 0x3F);
            len = 2;
        }
        else if((c & 0xF0) == 0xE0 && i + 2 < (int)line.size())
        {
            codepoint = ((c & 0x0F) << 12) |
                        (((unsigned char)line[i + 1] & 0x3F) << 6) |
                        ((unsigned char)line[i + 2] & 0x3F);
            len = 3;
        }
        else if((c & 0xF8) == 0xF0 && i + 3 < (int)line.size())
        {
            codepoint = ((c & 0x07) << 18) |
                        (((unsigned char)line[i + 1] & 0x3F) << 12) |
                        (((unsigned char)line[i + 2] & 0x3F) << 6) |
                        ((unsigned char)line[i + 3] & 0x3F);
            len = 4;
        }
        else
        {
            // invalid; treat as single byte
            codepoint = c;
            len = 1;
        }

        if(i + len > byteOffset)
            break;

        // UTF-16 units
        if(codepoint <= 0xFFFF)
            u16 += 1;
        else
            u16 += 2;

        i += len;
        safeLen = i;
    }

#if WCHAR_MAX <= 0xFFFF
    try
    {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
        return (int)conv.from_bytes(line.data(), line.data() + safeLen).size();
    }
    catch(const std::range_error&)
    {
        return u16;
    }
#else
    return u16;
#endif
}

// --- operator key (constexpr) ---

constexpr std::uint16_t op_key(char a, char b) noexcept
{
    return static_cast<std::uint16_t>(static_cast<unsigned char>(a)) |
           (static_cast<std::uint16_t>(static_cast<unsigned char>(b)) << 8);
}

} // namespace text_utils
