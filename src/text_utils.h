#pragma once
#include <climits>
#include <codecvt>
#include <cstddef>
#include <cstdint>
#include <cwchar>
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

inline std::string ascii_lower(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for(char c : value)
        out.push_back(ascii_tolower(c));
    return out;
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

#ifdef _WIN32
// Approximate wcwidth replacement for Windows MSVC (BMP only — wchar_t is
// 16-bit). Covers the common Unicode East-Asian Wide / Fullwidth ranges,
// returns 0 for typical zero-width blocks and -1 for control codes.
inline int wcwidth_win(wchar_t wc) noexcept
{
    if(wc == 0)
        return 0;
    if(wc < 0x20 || (wc >= 0x7F && wc < 0xA0))
        return -1;
    if((wc >= 0x0300 && wc <= 0x036F) || // combining diacriticals
       (wc >= 0x200B && wc <= 0x200F) || // zero-width / format
       (wc >= 0xFE00 && wc <= 0xFE0F) || // variation selectors
       wc == 0x2028 || wc == 0x2029)
        return 0;
    if((wc >= 0x1100 && wc <= 0x115F) || // Hangul Jamo
       (wc >= 0x2E80 && wc <= 0x303E) || // CJK radicals / symbols
       (wc >= 0x3041 && wc <= 0x33FF) || // Hiragana / Katakana / etc.
       (wc >= 0x3400 && wc <= 0x4DBF) || // CJK Ext A
       (wc >= 0x4E00 && wc <= 0x9FFF) || // CJK Unified
       (wc >= 0xA000 && wc <= 0xA4CF) || // Yi
       (wc >= 0xAC00 && wc <= 0xD7A3) || // Hangul Syllables
       (wc >= 0xF900 && wc <= 0xFAFF) || // CJK Compatibility
       (wc >= 0xFE30 && wc <= 0xFE4F) || // CJK Compat Forms
       (wc >= 0xFF00 && wc <= 0xFF60) || // Fullwidth
       (wc >= 0xFFE0 && wc <= 0xFFE6))   // Fullwidth signs
        return 2;
    return 1;
}
#endif

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

#ifdef _WIN32
        int w = wcwidth_win(wc);
#else
        int w = ::wcwidth(wc);
#endif
        if(w < 0)
            w = 1;
        width += w;
        p += n;
        len -= n;
    }
    return width;
}

// Approximate terminal display width.
// - strips ANSI escapes
// - counts UTF-8 codepoints as width 1 (good enough for popups)
inline int displayWidth(std::string_view s)
{
    auto isAnsiStart = [](std::string_view text, size_t i) -> bool
    { return i + 1 < text.size() && text[i] == '\x1b' && text[i + 1] == '['; };
    auto skipAnsi = [](std::string_view text, size_t i) -> size_t
    {
        i += 2;
        while(i < text.size())
        {
            char c = text[i++];
            if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
                break;
        }
        return i;
    };

    int w = 0;
    for(size_t i = 0; i < s.size();)
    {
        if(isAnsiStart(s, i))
        {
            i = skipAnsi(s, i);
            continue;
        }

        unsigned char c = (unsigned char)s[i];
        if(c < 0x80)
        {
            ++w;
            ++i;
            continue;
        }

        if((c & 0xE0) == 0xC0)
            i += 2;
        else if((c & 0xF0) == 0xE0)
            i += 3;
        else if((c & 0xF8) == 0xF0)
            i += 4;
        else
            ++i;
        ++w;
    }
    return w;
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
#if WCHAR_MAX <= 0xFFFF
    int safeLen = 0;
#endif
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
            codepoint = ((c & 0x1F) << 6) | ((unsigned char)line[i + 1] & 0x3F);
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
#if WCHAR_MAX <= 0xFFFF
        safeLen = i;
#endif
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
