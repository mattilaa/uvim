#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

using u8sv = std::basic_string_view<char8_t>;

namespace text_utils
{

static bool isIdent(char c)
{
    return std::isalnum((unsigned char)c) || c == '_';
}

static inline void appendU8(std::string& out, u8sv s)
{
    // Append UTF-8 bytes (char8_t) into std::string (char)
    out.append(reinterpret_cast<const char*>(s.data()), s.size());
}

static inline void appendUtf8Repeat(std::string& out, u8sv glyph, int count)
{
    if(count <= 0)
        return;
    for(int i = 0; i < count; ++i)
        appendU8(out, glyph);
}

static inline char ascii_tolower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

static inline bool iequals_ascii(std::string_view a, std::string_view b)
{
    if(a.size() != b.size())
        return false;
    for(size_t i = 0; i < a.size(); ++i)
        if(ascii_tolower(a[i]) != ascii_tolower(b[i]))
            return false;
    return true;
}

static inline bool contains(std::string_view s, std::string_view needle)
{
    return s.find(needle) != std::string_view::npos;
}

} // namespace text_utils
