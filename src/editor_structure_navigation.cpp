#include "editor.h"
#include "structural_navigation.h"

#include <algorithm>

namespace structural_navigation
{
FileType fileType(const Editor& editor)
{
    for(FileType candidate :
        {FileType::Cpp,
         FileType::Python,
         FileType::Mla,
         FileType::JavaScript,
         FileType::TypeScript,
         FileType::CSharp})
    {
        if(editor.isFileType(candidate))
        {
            return candidate;
        }
    }
    return FileType::MarkupText;
}

bool navigate(Editor& editor, Target target, bool forward, int count)
{
    if(!editor.currentBuffer || !editor.lines || editor.lines->empty())
        return false;
    const FileType type = fileType(editor);
    if(!supports(type))
    {
        editor.setStatusMessage("Structural navigation: unsupported file type");
        return false;
    }
    const Position cursor{*editor.cursorY, *editor.cursorX};
    const auto positions = targets(*editor.lines, type, target, cursor);
    const std::string label =
        target == Target::Function ? "Functions" : "Locals/calls";
    const std::string hint = " (n/N repeat, Esc restores search)";
    if(positions.empty())
    {
        editor.setStatusMessage(label + ": no targets" + hint);
        return false;
    }
    int index;
    if(forward)
    {
        const auto first =
            std::upper_bound(positions.begin(), positions.end(), cursor);
        index = static_cast<int>(first - positions.begin());
        if(index == static_cast<int>(positions.size()))
        {
            editor.setStatusMessage(label + ": at last target" + hint);
            return false;
        }
        index += std::min(
            std::max(1, count) - 1,
            static_cast<int>(positions.size()) - 1 - index);
    }
    else
    {
        const auto first =
            std::lower_bound(positions.begin(), positions.end(), cursor);
        index = static_cast<int>(first - positions.begin()) - 1;
        if(index < 0)
        {
            editor.setStatusMessage(label + ": at first target" + hint);
            return false;
        }
        index -= std::min(std::max(1, count) - 1, index);
    }
    editor.pushJumpLocation();
    *editor.cursorY = positions[index].line;
    *editor.cursorX = positions[index].column;
    *editor.wantedX = positions[index].column;
    editor.adjustViewport();
    editor.needsFullRedraw = true;
    editor.setStatusMessage(
        label + ": " + std::to_string(index + 1) + "/" +
        std::to_string(positions.size()) + hint);
    return true;
}
} // namespace structural_navigation
