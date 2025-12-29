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

// Safe ctype wrappers (avoid UB on negative char)
static inline bool is_space(char c)
{
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

static inline bool is_alpha(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

static inline bool is_alnum(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

static inline bool is_digit(char c)
{
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

static inline bool is_xdigit(char c)
{
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

} // namespace text_utils
