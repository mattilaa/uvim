#include "formatter.h"
#include "editor.h"
#include <algorithm>

Formatter::Formatter(Editor* editor) : editor(editor) {}

size_t Formatter::byteOffsetForPosition(int y, int x) const
{
    if(!editor->lines || editor->lines->empty())
        return 0;

    y = std::clamp(y, 0, (int)editor->lines->size() - 1);
    const std::string& ln = (*editor->lines)[y];
    x = std::clamp(x, 0, (int)ln.size());

    size_t off = 0;
    for(int i = 0; i < y; ++i)
        off += (*editor->lines)[i].size() + 1; // + '\n'
    off += (size_t)x;
    return off;
}
