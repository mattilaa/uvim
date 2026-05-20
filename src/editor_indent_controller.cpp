#include "editor_indent_controller.h"
#include "editor.h"
#include "text_utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>

namespace
{
std::optional<int> parseIndentWidthLine(const std::string& line)
{
    size_t start = 0;
    while(start < line.size() && (line[start] == ' ' || line[start] == '\t'))
        start++;
    if(start >= line.size() || line[start] == '#')
        return std::nullopt;

    constexpr std::string_view key = "IndentWidth";
    if(line.compare(start, key.size(), key) != 0)
        return std::nullopt;
    size_t pos = start + key.size();
    while(pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
        pos++;
    if(pos >= line.size() || line[pos] != ':')
        return std::nullopt;
    pos++;
    while(pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
        pos++;
    if(pos >= line.size())
        return std::nullopt;

    size_t end = pos;
    while(end < line.size() && std::isdigit((unsigned char)line[end]))
        end++;
    if(end == pos)
        return std::nullopt;

    try
    {
        int value = std::stoi(line.substr(pos, end - pos));
        if(value > 0)
            return value;
    }
    catch(...)
    {
    }
    return std::nullopt;
}

std::optional<std::string> parseScalarValueLine(const std::string& line,
                                                std::string_view key)
{
    size_t start = 0;
    while(start < line.size() && (line[start] == ' ' || line[start] == '\t'))
        start++;
    if(start >= line.size() || line[start] == '#')
        return std::nullopt;

    if(line.compare(start, key.size(), key) != 0)
        return std::nullopt;
    size_t pos = start + key.size();
    while(pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
        pos++;
    if(pos >= line.size() || line[pos] != ':')
        return std::nullopt;
    pos++;
    while(pos < line.size() && (line[pos] == ' ' || line[pos] == '\t'))
        pos++;
    if(pos >= line.size())
        return std::nullopt;

    std::string value = line.substr(pos);
    while(!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                             value.back() == '\r' || value.back() == '\n'))
        value.pop_back();
    return value;
}

bool parseBraceNewLineValue(std::string value)
{
    for(char& c : value)
        c = (char)std::tolower((unsigned char)c);
    if(value == "allman" || value == "whitesmiths" || value == "gnu")
        return true;
    if(value == "attach" || value == "stroustrup" || value == "linux" ||
       value == "webkit")
        return false;
    if(value == "true" || value == "always")
        return true;
    if(value == "false" || value == "never")
        return false;
    return false;
}
} // namespace

EditorIndentController::EditorIndentController(Editor& editor) : editor(editor)
{
}

std::string EditorIndentController::toLowerCase(const std::string& str)
{
    return editor.toLowerCaseImpl(str);
}

int EditorIndentController::getLineIndent(int line)
{
    return editor.getLineIndentImpl(line);
}

void EditorIndentController::indentLine(int line, int spaces)
{
    editor.indentLineImpl(line, spaces);
}

void EditorIndentController::autoIndentLine(int line)
{
    editor.autoIndentLineImpl(line);
}

void EditorIndentController::autoIndentRange(int startLine, int endLine)
{
    editor.autoIndentRangeImpl(startLine, endLine);
}

void EditorIndentController::updateClangFormatIndentWidth()
{
    editor.updateClangFormatIndentWidthImpl();
}

int EditorIndentController::indentWidthForBraces() const
{
    return editor.indentWidthForBracesImpl();
}

bool EditorIndentController::braceNewLineForAutoBraces() const
{
    return editor.braceNewLineForAutoBracesImpl();
}

void EditorIndentController::commentLines(int startY, int endY)
{
    editor.commentLinesImpl(startY, endY);
}

std::string Editor::toLowerCaseImpl(const std::string& str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

int Editor::getLineIndentImpl(int line)
{
    if(line < 0 || line >= (int)lines->size())
        return 0;

    const std::string& text = (*lines)[line];
    int indent = 0;
    for(char c : text)
    {
        if(c == ' ')
            indent++;
        else if(c == '\t')
            indent += 4;
        else
            break;
    }
    return indent;
}

void Editor::indentLineImpl(int line, int spaces)
{
    if(line < 0 || line >= (int)lines->size())
        return;

    std::string& text = (*lines)[line];

    size_t firstNonSpace = 0;
    while(firstNonSpace < text.length() &&
          (text[firstNonSpace] == ' ' || text[firstNonSpace] == '\t'))
    {
        firstNonSpace++;
    }

    std::string newIndent(spaces, ' ');
    text = newIndent + text.substr(firstNonSpace);
    *dirty = true;
}

void Editor::autoIndentLineImpl(int line)
{
    if(line < 0 || line >= (int)lines->size())
        return;

    std::string currentLine = (*lines)[line];
    size_t firstNonSpace = currentLine.find_first_not_of(" \t");
    if(text_utils::is_found(firstNonSpace))
        currentLine = currentLine.substr(firstNonSpace);
    else
        currentLine = "";

    int baseIndent = 0;
    if(line > 0)
    {
        baseIndent = getLineIndent(line - 1);

        const std::string& prevLine = (*lines)[line - 1];
        size_t lastNonSpace = prevLine.find_last_not_of(" \t\r\n");
        if(text_utils::is_found(lastNonSpace))
        {
            char lastChar = prevLine[lastNonSpace];
            if(lastChar == '{')
            {
                baseIndent += 4;
            }
            else if(lastChar == ':' &&
                    (text_utils::contains(prevLine, "public") ||
                     text_utils::contains(prevLine, "private") ||
                     text_utils::contains(prevLine, "protected") ||
                     text_utils::contains(prevLine, "case") ||
                     text_utils::contains(prevLine, "default")))
            {
                baseIndent += 4;
            }
        }
    }

    if(!currentLine.empty())
    {
        if(currentLine[0] == '}')
        {
            baseIndent = std::max(0, baseIndent - 4);
        }
        else if(currentLine.find("public:") == 0 ||
                currentLine.find("private:") == 0 ||
                currentLine.find("protected:") == 0)
        {
            if(line > 0 && baseIndent >= 4)
                baseIndent -= 4;
        }
        else if(currentLine.find("case ") == 0 ||
                currentLine.find("default:") == 0)
        {
            if(baseIndent >= 4)
                baseIndent -= 4;
        }
    }

    indentLine(line, baseIndent);
}

void Editor::autoIndentRangeImpl(int startLine, int endLine)
{
    if(startLine > endLine)
        std::swap(startLine, endLine);

    startLine = std::max(0, startLine);
    endLine = std::min((int)lines->size() - 1, endLine);

    for(int i = startLine; i <= endLine; i++)
        autoIndentLine(i);

    *dirty = true;
    needsFullRedraw = true;
}

void Editor::updateClangFormatIndentWidthImpl()
{
    if(!currentBuffer)
        return;

    currentBuffer->clangIndentWidthValid = true;
    currentBuffer->clangIndentWidth = -1;
    currentBuffer->clangBraceStyleValid = true;
    currentBuffer->clangBraceNewLine = false;

    if(!isFileType<FileType::Cpp>() || !filename || filename->empty())
        return;

    std::filesystem::path path = *filename;
    if(path.is_relative())
        path = std::filesystem::absolute(path);
    if(path.has_parent_path())
        path = path.parent_path();

    std::error_code ec;
    while(true)
    {
        std::filesystem::path clangFormat = path / ".clang-format";
        std::filesystem::path altFormat = path / "_clang-format";
        std::filesystem::path found;

        if(std::filesystem::exists(clangFormat, ec))
            found = clangFormat;
        else if(std::filesystem::exists(altFormat, ec))
            found = altFormat;

        if(!found.empty())
        {
            std::ifstream in(found);
            if(in.is_open())
            {
                std::string line;
                bool inBraceWrapping = false;
                size_t braceWrappingIndent = 0;
                while(std::getline(in, line))
                {
                    std::optional<int> width = parseIndentWidthLine(line);
                    if(width)
                        currentBuffer->clangIndentWidth = *width;

                    auto breakValue =
                        parseScalarValueLine(line, "BreakBeforeBraces");
                    if(breakValue)
                    {
                        currentBuffer->clangBraceNewLine =
                            parseBraceNewLineValue(*breakValue);
                        continue;
                    }

                    auto braceWrapping =
                        parseScalarValueLine(line, "BraceWrapping");
                    if(braceWrapping)
                    {
                        inBraceWrapping = true;
                        braceWrappingIndent = line.find_first_not_of(" \t");
                        if(text_utils::is_not_found(braceWrappingIndent))
                            braceWrappingIndent = 0;
                        continue;
                    }

                    if(inBraceWrapping)
                    {
                        size_t indent = line.find_first_not_of(" \t");
                        if(text_utils::is_not_found(indent))
                            continue;
                        if(indent <= braceWrappingIndent)
                        {
                            inBraceWrapping = false;
                            continue;
                        }

                        auto afterControl =
                            parseScalarValueLine(line, "AfterControlStatement");
                        if(afterControl)
                        {
                            currentBuffer->clangBraceNewLine =
                                parseBraceNewLineValue(*afterControl);
                        }
                    }
                }
            }
            return;
        }

        if(path == path.root_path())
            break;
        path = path.parent_path();
    }
}

int Editor::indentWidthForBracesImpl() const
{
    if(currentBuffer && currentBuffer->clangIndentWidthValid &&
       currentBuffer->clangIndentWidth > 0)
        return currentBuffer->clangIndentWidth;
    return tabSpaces;
}

bool Editor::braceNewLineForAutoBracesImpl() const
{
    if(currentBuffer && currentBuffer->clangBraceStyleValid)
        return currentBuffer->clangBraceNewLine;
    return false;
}

void Editor::commentLinesImpl(int startY, int endY)
{
    if(!currentBuffer || !lines)
        return;
    if(!isFileType<FileType::Cpp>() && !isFileType<FileType::Python>())
    {
        setStatusMessage("comment: unsupported filetype");
        return;
    }

    std::string prefix = isFileType<FileType::Python>() ? "#" : "//";
    if(startY > endY)
        std::swap(startY, endY);

    bool allCommented = true;
    bool anyCommented = false;
    for(int y = startY; y <= endY && y < (int)lines->size(); ++y)
    {
        const std::string& line = (*lines)[y];
        size_t pos = line.find_first_not_of(" \t");
        if(text_utils::is_not_found(pos))
            continue;
        if(line.compare(pos, prefix.size(), prefix) == 0)
            anyCommented = true;
        else
            allCommented = false;
    }

    if(commentTogglePartial && anyCommented)
        allCommented = true;

    for(int y = startY; y <= endY && y < (int)lines->size(); ++y)
    {
        std::string& line = (*lines)[y];
        size_t pos = line.find_first_not_of(" \t");
        if(text_utils::is_not_found(pos))
            continue;

        if(allCommented)
        {
            if(line.compare(pos, prefix.size(), prefix) != 0)
                continue;
            size_t eraseLen = prefix.size();
            if(pos + eraseLen < line.size() && line[pos + eraseLen] == ' ')
                eraseLen++;
            line.erase(pos, eraseLen);
            continue;
        }

        if(line.compare(pos, prefix.size(), prefix) == 0)
            continue;
        line.insert(pos, prefix + " ");
    }

    *dirty = true;
    saveState();
    currentBuffer->lspSyncNeeded = true;
    needsFullRedraw = true;
}
