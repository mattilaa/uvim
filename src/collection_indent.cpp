#include "collection_indent.h"
#include "editor.h"
#include <algorithm>

namespace collection_indent
{
namespace
{
char closing(char open)
{
    return open == '[' ? ']' : open == '{' ? '}' : ')';
}

size_t leading(std::string_view line)
{
    const size_t end = line.find_first_not_of(" \t");
    return end == std::string_view::npos ? line.size() : end;
}
} // namespace

void Scanner::scan(std::string_view line, size_t row)
{
    lineComment = false;
    const std::string indent(line.substr(0, leading(line)));
    for(size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];
        const char next = i + 1 < line.size() ? line[i + 1] : '\0';
        if(blockComment)
        {
            if(c == '*' && next == '/')
            {
                blockComment = false;
                ++i;
            }
            continue;
        }
        if(quote)
        {
            if(c == '\\')
                ++i;
            else if(c == quote)
                quote = 0;
            continue;
        }
        if((c == '/' && next == '/') || c == '#')
        {
            lineComment = true;
            break;
        }
        if(c == '/' && next == '*')
        {
            blockComment = true;
            ++i;
            continue;
        }
        if(c == '\'' || c == '"' || c == '`')
        {
            quote = c;
            continue;
        }
        if(c == '[' || c == '(' || c == '{')
        {
            const std::string_view prefix = line.substr(0, i);
            const size_t last = prefix.find_last_not_of(" \t");
            const bool collection =
                c != '{' || last == std::string_view::npos ||
                std::string_view("=:,[({").find(prefix[last]) !=
                    std::string_view::npos ||
                (last != std::string_view::npos &&
                 prefix.substr(leading(prefix), last - leading(prefix) + 1) ==
                     "return");
            stack.push_back({c, row, i, indent, collection});
        }
        else if(c == ']' || c == ')' || c == '}')
        {
            if(!stack.empty() && closing(stack.back().open) == c)
            {
                ranges.push_back({stack.back(), row, i});
                stack.pop_back();
            }
        }
    }
}

std::optional<std::string> Scanner::indentation(
    std::string_view remainder,
    int width) const
{
    if(stack.empty() || quote || blockComment || !stack.back().collection)
        return std::nullopt;
    std::string indent = stack.back().indent + std::string(width, ' ');
    size_t depth = stack.size();
    for(char c : remainder.substr(leading(remainder)))
    {
        if(c == ' ' || c == '\t')
            continue;
        if(depth == 0 || closing(stack[depth - 1].open) != c)
            break;
        indent = stack[--depth].indent;
    }
    return indent;
}
} // namespace collection_indent

int Editor::collectionIndentWidth() const
{
    return collectionIndentSpaces > 0 ? collectionIndentSpaces
                                      : indentWidthForBraces();
}

void Editor::reindentCollection()
{
    if(!currentBuffer || !lines || lines->empty())
        return;
    collection_indent::Scanner scan;
    for(size_t row = 0; row < lines->size(); ++row)
        scan.scan((*lines)[row], row);
    const size_t row = static_cast<size_t>(*cursorY);
    const size_t column = static_cast<size_t>(*cursorX);
    const collection_indent::Range* target = nullptr;
    bool targetContains = false;
    for(const auto& range : scan.ranges)
    {
        if(!range.start.collection || range.start.row == range.endRow)
            continue;
        const bool contains =
            (range.start.row < row ||
             (range.start.row == row && range.start.column <= column)) &&
            (row < range.endRow ||
             (row == range.endRow && column <= range.endColumn));
        const bool opensOnLine =
            range.start.row == row && range.start.column >= column;
        if(!contains && !opensOnLine)
            continue;
        if(!target || (contains && !targetContains) ||
           (contains == targetContains &&
            (contains ? range.start.row > target->start.row ||
                            (range.start.row == target->start.row &&
                             range.start.column > target->start.column)
                      : range.start.column < target->start.column)))
        {
            target = &range;
            targetContains = contains;
        }
    }
    if(!target)
    {
        setStatusMessage("No enclosing multiline list, map, or argument list");
        return;
    }

    collection_indent::Scanner state;
    bool changed = false;
    for(size_t y = 0; y <= target->endRow; ++y)
    {
        std::string& line = (*lines)[y];
        if(y > target->start.row)
        {
            // Line comments end at the newline; multiline strings/comments stay
            // intact.
            state.lineComment = false;
            const auto indent =
                state.indentation(line, collectionIndentWidth());
            const size_t oldLeading = line.find_first_not_of(" \t");
            if(indent && oldLeading != std::string::npos)
            {
                const std::string replacement =
                    *indent + line.substr(oldLeading);
                if(replacement != line)
                {
                    if(y == row)
                        *cursorX = static_cast<int>(indent->size()) +
                                   std::max(
                                       0,
                                       *cursorX - static_cast<int>(oldLeading));
                    line = replacement;
                    changed = true;
                }
            }
        }
        state.scan(line, y);
    }
    if(changed)
    {
        *cursorX = std::clamp(
            *cursorX,
            0,
            static_cast<int>((*lines)[*cursorY].size()));
        *dirty = true;
        currentBuffer->invalidateSyntaxCache();
        currentBuffer->lspSyncNeeded = true;
        currentBuffer->blameValid = false;
        saveState();
        needsFullRedraw = true;
    }
    setStatusMessage(
        changed ? "Collection indented" : "Collection indentation unchanged");
}
