#include "constants.h"
#include "cpp_constants.h"
#include "editor.h"
#include "terminal.h"
#include "text_utils.h"
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>

template <class Arr>
inline bool contains_sorted(const Arr& arr, std::string_view s)
{
    return std::ranges::binary_search(arr, s);
}

inline bool is_two_char_op(char a, char b) noexcept
{
    switch(a)
    {
    case '+':
        return (b == '+' || b == '=');
    case '-':
        return (b == '-' || b == '=' || b == '>');
    case '&':
        return (b == '&');
    case '|':
        return (b == '|');
    case '=':
        return (b == '=');
    case '!':
        return (b == '=');
    case '<':
        return (b == '<' || b == '=');
    case '>':
        return (b == '>' || b == '=');
    case ':':
        return (b == ':');
    case '.':
        // treats ".." as two-char; "..." will become ".." + "."
        return (b == '.');
    case '*':
        return (b == '=');
    case '/':
        return (b == '=');
    case '%':
        return (b == '=');
    default:
        return false;
    }
}

bool Editor::isCppFile() const
{
    if(!filename || filename->empty())
        return false;

    // Work with views to avoid copies
    std::string_view pathSv{*filename};

    // Use filesystem for extension logic
    std::filesystem::path p{*filename};

    // If there is no extension, keep your "stdlib header path" heuristics
    if(!p.has_extension())
    {
        return text_utils::contains(pathSv, "/c++/") ||
               text_utils::contains(pathSv, "/bits/") ||
               text_utils::contains(pathSv, "/ext/") ||
               text_utils::contains(pathSv, "/__");
    }

    // Compare extensions (including the dot) without allocating a new string
    // Note: path.extension() returns a path; .string() allocates. Use native
    // string view if possible: We'll just take a small allocation-free route by
    // comparing on the filename view.
    auto dot = pathSv.find_last_of('.');
    if(dot == std::string_view::npos)
        return false;
    std::string_view ext = pathSv.substr(dot); // includes '.'

    return std::any_of(constants::CPP_FILE_EXTENSIONS.begin(),
                       constants::CPP_FILE_EXTENSIONS.end(),
                       [&](std::string_view e)
                       { return text_utils::iequals_ascii(ext, e); });
}

bool Editor::isMlaFile() const
{
    if(filename->empty())
        return false;

    size_t dotPos = filename->find_last_of('.');
    if(dotPos == std::string::npos)
        return false;

    std::string ext = filename->substr(dotPos);
    return (ext == ".mla");
}

size_t Editor::byteOffsetForPosition(int y, int x) const
{
    if(!lines || lines->empty())
        return 0;

    y = std::clamp(y, 0, (int)lines->size() - 1);
    const std::string& ln = (*lines)[y];
    x = std::clamp(x, 0, (int)ln.size());

    size_t off = 0;
    for(int i = 0; i < y; ++i)
        off += (*lines)[i].size() + 1; // + '\n'
    off += (size_t)x;
    return off;
}

bool Editor::clangFormatWithArgs(const std::string& extraArgs,
                                 const std::string& successMessage)
{
    if(!lines || !filename)
        return false;

    if(!isCppFile())
    {
        setStatusMessage("clang-format: not a C/C++ file (" + *filename + ")");
        return false;
    }

    const int savedY = cursorY ? *cursorY : 0;
    const int savedX = cursorX ? *cursorX : 0;

    std::string tempPath =
        "/tmp/uvim_format_" + std::to_string(getpid()) + ".tmp";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        setStatusMessage("clang-format: failed to create temp file");
        return false;
    }

    for(size_t i = 0; i < lines->size(); ++i)
        tempFile << (*lines)[i] << '\n';
    tempFile.close();

    std::string absFilename = *filename;
    if(!absFilename.empty() && absFilename[0] != '/')
    {
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
            absFilename = std::string(cwd) + "/" + *filename;
    }

    auto buildCmd = [&](const std::string& exe) -> std::string
    {
        std::string cmd = "cat \"" + tempPath + "\" | " + exe +
                          " -style=file -assume-filename=\"" + absFilename +
                          "\"";
        if(!extraArgs.empty())
            cmd += " " + extraArgs;
        cmd += " 2>/tmp/uvim_clang_err.log";
        return cmd;
    };

    std::string cmd = buildCmd("/opt/homebrew/bin/clang-format");
    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
    {
        cmd = buildCmd("clang-format");
        pipe = popen(cmd.c_str(), "r");
    }

    if(!pipe)
    {
        unlink(tempPath.c_str());
        setStatusMessage("clang-format: failed to run");
        return false;
    }

    std::string formatted;
    char buffer[4096];
    while(fgets(buffer, sizeof(buffer), pipe))
        formatted += buffer;

    int status = pclose(pipe);
    (void)status;
    unlink(tempPath.c_str());

    if(formatted.empty())
    {
        std::ifstream errFile("/tmp/uvim_clang_err.log");
        std::string errMsg;
        if(errFile.is_open())
        {
            std::getline(errFile, errMsg);
            errFile.close();
        }
        if(errMsg.empty())
            errMsg = "no output";
        setStatusMessage("clang-format: " + errMsg.substr(0, 80));
        return false;
    }

    std::vector<std::string> newLines;
    std::istringstream iss(formatted);
    std::string line;
    while(std::getline(iss, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        newLines.push_back(line);
    }

    if(!newLines.empty() && newLines.back().empty())
        newLines.pop_back();
    if(newLines.empty())
        newLines.push_back("");

    if(newLines == *lines)
    {
        setStatusMessage("clang-format: no changes needed");
        return true;
    }

    saveState();
    *lines = std::move(newLines);
    if(dirty)
        *dirty = true;

    if(cursorY && cursorX && lines && !lines->empty())
    {
        *cursorY = std::clamp(savedY, 0, (int)lines->size() - 1);
        *cursorX = std::clamp(savedX, 0, (int)(*lines)[*cursorY].size());
    }

    adjustViewport();
    needsFullRedraw = true;
    setStatusMessage(successMessage);
    return true;
}

void Editor::clangFormatVisualSelection()
{
    if(currentMode != VISUAL && currentMode != VISUAL_LINE)
        return;

    if(!lines || lines->empty())
    {
        setStatusMessage("clang-format: empty buffer");
        return;
    }

    if(currentMode == VISUAL_LINE)
    {
        const int startY =
            std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
        const int endY =
            std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);

        const int startLine = startY + 1;
        const int endLine = endY + 1;

        const std::string args = "-lines=" + std::to_string(startLine) + ":" +
                                 std::to_string(endLine);

        clangFormatWithArgs(args, "clang-format: formatted selection (lines)");
        return;
    }

    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    endY = std::clamp(endY, 0, (int)lines->size() - 1);
    const int endLineLen = (int)(*lines)[endY].size();
    const int endXExclusive = std::clamp(endX + 1, 0, endLineLen);

    const size_t startOff = byteOffsetForPosition(startY, startX);
    const size_t endOff = byteOffsetForPosition(endY, endXExclusive);

    if(endOff <= startOff)
    {
        setStatusMessage("clang-format: empty selection");
        return;
    }

    const size_t len = endOff - startOff;
    const std::string args = "-offset=" + std::to_string(startOff) +
                             " -length=" + std::to_string(len);

    clangFormatWithArgs(args, "clang-format: formatted selection");
}

void Editor::clangFormatVisualBlockSelection()
{
    if(currentMode != VISUAL_BLOCK)
        return;

    if(!lines || lines->empty())
    {
        setStatusMessage("clang-format: empty buffer");
        return;
    }

    int startY, startX, endY, endX;
    getVisualBlockBounds(startY, startX, endY, endX);

    std::string args;
    for(int y = startY; y <= endY && y < (int)lines->size(); ++y)
    {
        const int lineLen = (int)(*lines)[y].size();
        const int segStart = std::clamp(startX, 0, lineLen);
        const int segEndExclusive = std::clamp(endX + 1, 0, lineLen);

        if(segEndExclusive <= segStart)
            continue;

        const size_t off = byteOffsetForPosition(y, segStart);
        const size_t len = (size_t)(segEndExclusive - segStart);

        args += " -offset=" + std::to_string(off) +
                " -length=" + std::to_string(len);
    }

    if(args.empty())
    {
        setStatusMessage("clang-format: empty visual block");
        return;
    }

    if(!args.empty() && args[0] == ' ')
        args.erase(0, 1);

    clangFormatWithArgs(args, "clang-format: formatted visual block");
}
std::string Editor::getColorCode(TokenType type) const
{
    switch(type)
    {
    case TOKEN_KEYWORD:
        return Terminal::FG_MAGENTA;
    case TOKEN_TYPE:
        return Terminal::FG_CYAN;
    case TOKEN_STRING:
        return Terminal::FG_GREEN;
    case TOKEN_CHAR:
        return Terminal::FG_GREEN;
    case TOKEN_COMMENT:
        return Terminal::FG_BRIGHT_BLACK;
    case TOKEN_PREPROCESSOR:
        return Terminal::FG_YELLOW;
    case TOKEN_NUMBER:
        return Terminal::FG_RED;
    case TOKEN_OPERATOR:
        return Terminal::FG_BRIGHT_YELLOW;
    case TOKEN_FUNCTION:
        return Terminal::FG_BRIGHT_BLUE;
    default:
        return Terminal::FG_DEFAULT;
    }
}

std::vector<Token> Editor::tokenizeLine(const std::string& line,
                                        bool& inBlockComment)
{
    std::vector<Token> tokens;
    std::string_view sv{line};

    const int len = static_cast<int>(sv.size());
    int i = 0;

    while(i < len)
    {
        while(i < len && text_utils::is_space(sv[i]))
            ++i;
        if(i >= len)
            break;

        // In block comment: consume until "*/" or EOL
        if(inBlockComment)
        {
            const int start = i;
            while(i < len && !(i < len - 1 && sv[i] == '*' && sv[i + 1] == '/'))
                ++i;

            if(i < len - 1 && sv[i] == '*' && sv[i + 1] == '/')
            {
                i += 2;
                inBlockComment = false;
            }

            tokens.push_back({TOKEN_COMMENT, start, i - start});
            continue;
        }

        // Preprocessor line (only if first non-space is '#')
        if(i == 0 && sv[i] == '#')
        {
            tokens.push_back({TOKEN_PREPROCESSOR, i, len - i});
            break;
        }

        // Line comment
        if(i < len - 1 && sv[i] == '/' && sv[i + 1] == '/')
        {
            tokens.push_back({TOKEN_COMMENT, i, len - i});
            break;
        }

        // Block comment start
        if(i < len - 1 && sv[i] == '/' && sv[i + 1] == '*')
        {
            const int start = i;
            i += 2;

            while(i < len - 1 && !(sv[i] == '*' && sv[i + 1] == '/'))
                ++i;

            if(i < len - 1 && sv[i] == '*' && sv[i + 1] == '/')
            {
                i += 2;
            }
            else
            {
                inBlockComment = true;
                i = len;
            }

            tokens.push_back({TOKEN_COMMENT, start, i - start});
            continue;
        }

        // String literal
        if(sv[i] == '"')
        {
            const int start = i++;
            while(i < len && sv[i] != '"')
            {
                if(sv[i] == '\\' && i + 1 < len)
                    i += 2;
                else
                    ++i;
            }
            if(i < len)
                ++i;

            tokens.push_back({TOKEN_STRING, start, i - start});
            continue;
        }

        // Character literal
        if(sv[i] == '\'')
        {
            const int start = i++;
            while(i < len && sv[i] != '\'')
            {
                if(sv[i] == '\\' && i + 1 < len)
                    i += 2;
                else
                    ++i;
            }
            if(i < len)
                ++i;

            tokens.push_back({TOKEN_CHAR, start, i - start});
            continue;
        }

        // Number
        if(text_utils::is_digit(sv[i]) ||
           (sv[i] == '.' && i + 1 < len && text_utils::is_digit(sv[i + 1])))
        {
            const int start = i;
            bool hasHex = false;

            if(sv[i] == '0' && i + 1 < len &&
               (sv[i + 1] == 'x' || sv[i + 1] == 'X'))
            {
                hasHex = true;
                i += 2;
            }

            while(i < len && (text_utils::is_digit(sv[i]) ||
                              (hasHex && text_utils::is_xdigit(sv[i])) ||
                              sv[i] == '.' || sv[i] == 'e' || sv[i] == 'E' ||
                              sv[i] == 'f' || sv[i] == 'F' || sv[i] == 'u' ||
                              sv[i] == 'U' || sv[i] == 'l' || sv[i] == 'L'))
            {
                ++i;
            }

            tokens.push_back({TOKEN_NUMBER, start, i - start});
            continue;
        }

        // Identifier / keyword / type / function
        if(text_utils::is_alpha(sv[i]) || sv[i] == '_')
        {
            const int start = i;
            while(i < len &&
                  (text_utils::is_alnum(sv[i]) || sv[i] == '_' || sv[i] == ':'))
                ++i;

            const std::string_view word = sv.substr(start, i - start);

            int j = i;
            while(j < len && text_utils::is_space(sv[j]))
                ++j;

            TokenType type = TOKEN_NORMAL;

            if(j < len && sv[j] == '(')
            {
                type = TOKEN_FUNCTION;
            }
            else if(cpp_constants::is_keyword(word))
            {
                type = TOKEN_KEYWORD;
            }
            else if(cpp_constants::is_type(word))
            {
                type = TOKEN_TYPE;
            }
            else if(!tokens.empty())
            {
                const Token& prev = tokens.back();
                const std::string_view prevWord =
                    sv.substr(prev.start, prev.length);

                if(prevWord == "struct" || prevWord == "class" ||
                   prevWord == "enum" || prevWord == ":" || prevWord == "->")
                {
                    type = TOKEN_TYPE;
                }
            }

            tokens.push_back({type, start, i - start});
            continue;
        }

        // Operators / punctuation
        if(cpp_constants::is_operator_char(sv[i]))
        {
            const int start = i++;
            if(i < len)
            {
                const char a = sv[i - 1], b = sv[i];
                if(is_two_char_op(a, b))
                    ++i;
            }

            tokens.push_back({TOKEN_OPERATOR, start, i - start});
            continue;
        }

        // Fallback: single char
        tokens.push_back({TOKEN_NORMAL, i, 1});
        ++i;
    }

    return tokens;
}

void Editor::renderLineWithSyntax(std::string& output, const std::string& line,
                                  int start, int len, int fileRow)
{
    // Calculate the absolute line number in the file
    int absoluteLineNum = fileRow + *offsetY;

    // Helper function to scan a line and update block comment state
    // This needs to be careful about string literals and character literals
    auto scanLineForBlockComments = [](const std::string& scanLine, bool& inComment)
    {
        size_t pos = 0;
        size_t len = scanLine.length();

        while(pos < len)
        {
            if(inComment)
            {
                // Looking for closing */
                size_t closePos = scanLine.find("*/", pos);
                if(closePos != std::string::npos)
                {
                    inComment = false;
                    pos = closePos + 2;
                }
                else
                {
                    break; // Still in block comment at end of line
                }
            }
            else
            {
                char c = scanLine[pos];

                // Skip string literals
                if(c == '"')
                {
                    pos++;
                    while(pos < len)
                    {
                        if(scanLine[pos] == '\\' && pos + 1 < len)
                        {
                            pos += 2; // Skip escaped character
                        }
                        else if(scanLine[pos] == '"')
                        {
                            pos++;
                            break;
                        }
                        else
                        {
                            pos++;
                        }
                    }
                    continue;
                }

                // Skip character literals
                if(c == '\'')
                {
                    pos++;
                    while(pos < len)
                    {
                        if(scanLine[pos] == '\\' && pos + 1 < len)
                        {
                            pos += 2; // Skip escaped character
                        }
                        else if(scanLine[pos] == '\'')
                        {
                            pos++;
                            break;
                        }
                        else
                        {
                            pos++;
                        }
                    }
                    continue;
                }

                // Check for line comment
                if(pos + 1 < len && scanLine[pos] == '/' && scanLine[pos + 1] == '/')
                {
                    // Rest of line is a line comment
                    break;
                }

                // Check for block comment start
                if(pos + 1 < len && scanLine[pos] == '/' && scanLine[pos + 1] == '*')
                {
                    inComment = true;
                    pos += 2;
                    continue;
                }

                pos++;
            }
        }
    };

    // Determine block comment state for this line
    // We scan from the beginning of the file to ensure correctness
    // Performance optimization: this is a lightweight scan (just looking for comment delimiters)
    bool blockCommentState = false;

    for(int i = 0; i < absoluteLineNum && i < (int)lines->size(); i++)
    {
        scanLineForBlockComments((*lines)[i], blockCommentState);
    }

    // Now tokenize the current line
    std::vector<Token> tokens = tokenizeLine(line, blockCommentState);

    std::vector<TokenType> charColors(len, TOKEN_NORMAL);

    for(const auto& token : tokens)
    {
        int tokenEnd = token.start + token.length;
        for(int pos = token.start; pos < tokenEnd; pos++)
        {
            int visiblePos = pos - start;
            if(visiblePos >= 0 && visiblePos < len)
            {
                charColors[visiblePos] = token.type;
            }
        }
    }

    TokenType currentColor = TOKEN_NORMAL;
    for(int x = 0; x < len; x++)
    {
        int col = x + start;

        bool highlighted = false;
        if(isInSelection(fileRow, col) || isInVisualBlock(fileRow, col))
        {
            output += Terminal::STYLE_SELECTION;
            highlighted = true;
        }
        else if(isInSearchMatch(fileRow, col))
        {
            output += Terminal::STYLE_SEARCH_MATCH;
            highlighted = true;
        }

        if(!highlighted && charColors[x] != currentColor)
        {
            currentColor = charColors[x];
            output += getColorCode(currentColor);
        }

        output += line[col];

        if(highlighted)
        {
            output += Terminal::ESC_RESET_ALL;
            currentColor = TOKEN_NORMAL;
        }
    }

    if(currentColor != TOKEN_NORMAL)
    {
        output += Terminal::FG_DEFAULT;
    }
}
