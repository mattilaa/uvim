#include "cpp_constants.h"

#include <algorithm>
#include <array>

namespace cpp_constants
{
using sv = std::string_view;

// constexpr insertion sort for std::array<string_view, N>
template <std::size_t N>
consteval std::array<sv, N> sort_array(std::array<sv, N> a)
{
    for(std::size_t i = 1; i < N; ++i)
    {
        auto key = a[i];
        std::size_t j = i;
        while(j > 0 && a[j - 1] > key)
        {
            a[j] = a[j - 1];
            --j;
        }
        a[j] = key;
    }
    return a;
}

template <class Arr>
constexpr bool contains_sorted(const Arr& arr, sv s) noexcept
{
    return std::ranges::binary_search(arr, s);
}

// Keep these lists exactly as you have them; sorting happens at compile time.
// Just move/paste your full lists into the braces.

static constexpr auto CPP_KEYWORDS = sort_array(std::to_array<sv>(
    {// --- paste ALL your keywords here ---
     "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
     "break", "case", "catch", "class", "compl", "concept", "const",
     "consteval", "constexpr", "constinit", "const_cast", "continue",
     "co_await", "co_return", "co_yield", "decltype", "default", "delete", "do",
     "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false",
     "for", "friend", "goto", "if", "inline", "mutable", "namespace", "new",
     "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq",
     "private", "protected", "public", "reflexpr", "register",
     "reinterpret_cast", "requires", "return", "sizeof", "static",
     "static_assert", "static_cast", "struct", "switch", "synchronized",
     "template", "this", "thread_local", "throw", "true", "try", "typedef",
     "typeid", "typename", "union", "using", "virtual", "volatile", "while",
     "xor", "xor_eq",
     // extras:
     "override", "final", "fn", "pub", "impl", "let", "var", "mod", "use",
     "in"}));

static constexpr auto CPP_TYPES = sort_array(std::to_array<sv>(
    {// --- paste ALL your types here ---
     "bool", "char", "char8_t", "char16_t", "char32_t", "double", "float",
     "int", "long", "short", "signed", "unsigned", "void", "wchar_t", "size_t",
     "ptrdiff_t", "nullptr_t", "int8_t", "int16_t", "int32_t", "int64_t",
     "uint8_t", "uint16_t", "uint32_t", "uint64_t", "intptr_t", "uintptr_t",
     "intmax_t", "uintmax_t", "std::vector", "std::list", "std::deque",
     "std::array", "std::string", "std::string_view",
     "unordered_map", "unordered_set", "unordered_multimap",
     "unordered_multiset",
     // ... etc ... (paste the entire list)
     "LspDiagnosticSummary", "i8", "i16", "i32", "i64", "optional", "u8",
     "u16", "u32", "u64", "print", "println", "eprint", "eprintln", "string",
     "str8", "str16", "list", "map", "tuple"}));

constexpr sv CPP_OP_CHARS = "+-*/%=<>!&|^~?:.,;()[]{}\\";

bool is_keyword(std::string_view s) noexcept
{
    return contains_sorted(CPP_KEYWORDS, s);
}

bool is_type(std::string_view s) noexcept
{
    return contains_sorted(CPP_TYPES, s);
}

bool is_operator_char(char c) noexcept
{
    return CPP_OP_CHARS.find(c) != sv::npos;
}
} // namespace cpp_constants
