#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace collection_indent
{
struct Delimiter
{
    char open;
    size_t row;
    size_t column;
    std::string indent;
    bool collection;
};

struct Range
{
    Delimiter start;
    size_t endRow;
    size_t endColumn;
};

// A lightweight lexical scan; delimiters in strings and comments are ignored.
class Scanner
{
public:
    std::vector<Delimiter> stack;
    std::vector<Range> ranges;
    char quote = 0;
    bool blockComment = false;
    bool lineComment = false;
    void scan(std::string_view line, size_t row);
    std::optional<std::string> indentation(
        std::string_view remainder,
        int width) const;
};
} // namespace collection_indent
