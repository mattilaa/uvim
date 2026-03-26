#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include <string_view>
#include <algorithm>
#include <chrono>
#include <regex>

// ============================================================================
// GitShowCommitMode Implementation
// ============================================================================

namespace
{
bool is_ansi_start(std::string_view text, size_t i)
{
    return i + 1 < text.size() && text[i] == '\x1b' && text[i + 1] == keyCode(command::CommandKey::KEY_LEFT_BRACKET);
}

size_t skip_ansi(std::string_view text, size_t i)
{
    i += 2;
    while(i < text.size())
    {
        char c = text[i++];
        if((c >= keyCode(typed::TypedKey::KEY_CAP_A) && c <= keyCode(typed::TypedKey::KEY_CAP_Z)) || (c >= keyCode(typed::TypedKey::KEY_A) && c <= keyCode(typed::TypedKey::KEY_Z)))
            break;
    }
    return i;
}

std::string strip_ansi(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while(i < text.size())
    {
        if(is_ansi_start(text, i))
        {
            i = skip_ansi(text, i);
            continue;
        }
        int next = text_utils::nextUtf8CharStart(text, (int)i);
        out.append(text.substr(i, next - (int)i));
        i = next;
    }
    return out;
}

int display_width_plain(std::string_view text)
{
    return text_utils::utf8DisplayWidth(text);
}

int display_width_with_ansi(std::string_view text)
{
    return text_utils::displayWidth(text);
}

int max_line_width(const std::vector<std::string>& lines,
                   bool useDefaultColors)
{
    int maxW = 0;
    for(const auto& line : lines)
    {
        int w = useDefaultColors ? display_width_with_ansi(line)
                                 : display_width_plain(line);
        if(w > maxW)
            maxW = w;
    }
    return maxW;
}

std::string slice_plain(std::string_view text, int startCol, int width)
{
    if(width <= 0 || text.empty())
        return "";
    if(startCol < 0)
        startCol = 0;

    std::string out;
    int col = 0;
    int pos = 0;
    while(pos < (int)text.size())
    {
        int next = text_utils::nextUtf8CharStart(text, pos);
        int w = text_utils::utf8DisplayWidth(text.substr(pos, next - pos));
        if(col + w <= startCol)
        {
            col += w;
            pos = next;
            continue;
        }
        if(col >= startCol + width)
            break;
        if(col >= startCol && col + w <= startCol + width)
            out.append(text.substr(pos, next - pos));
        col += w;
        pos = next;
    }
    return out;
}

std::string slice_with_ansi(std::string_view text, int startCol, int width)
{
    if(width <= 0 || text.empty())
        return "";
    if(startCol < 0)
        startCol = 0;

    std::string out;
    int col = 0;
    size_t i = 0;
    while(i < text.size())
    {
        if(is_ansi_start(text, i))
        {
            size_t end = skip_ansi(text, i);
            out.append(text.substr(i, end - i));
            i = end;
            continue;
        }

        int next = text_utils::nextUtf8CharStart(text, (int)i);
        int w = text_utils::utf8DisplayWidth(text.substr(i, next - (int)i));
        if(col + w <= startCol)
        {
            col += w;
            i = next;
            continue;
        }
        if(col >= startCol + width)
            break;
        if(col >= startCol && col + w <= startCol + width)
            out.append(text.substr(i, next - (int)i));
        col += w;
        i = next;
    }
    return out;
}

void append_highlighted(std::string& out, std::string_view text,
                        std::string_view query, const std::string& normalSeq,
                        const std::string& matchSeq)
{
    if(query.empty())
    {
        out += normalSeq;
        out.append(text.data(), text.size());
        return;
    }

    size_t pos = 0;
    while(pos < text.size())
    {
        size_t found = text.find(query, pos);
        if(found == std::string_view::npos)
        {
            out += normalSeq;
            out.append(text.data() + pos, text.size() - pos);
            break;
        }
        if(found > pos)
        {
            out += normalSeq;
            out.append(text.data() + pos, found - pos);
        }
        out += matchSeq;
        out.append(text.data() + found, query.size());
        pos = found + query.size();
    }
}

bool compile_search_regex(std::string_view query, std::regex& pattern)
{
    if(query.empty())
        return false;
    try
    {
        pattern = std::regex(std::string(query));
        return true;
    }
    catch(const std::regex_error&)
    {
        return false;
    }
}

bool line_matches_regex(const std::string& line, const std::regex& pattern)
{
    return std::regex_search(strip_ansi(line), pattern);
}

void append_regex_highlighted(std::string& out, std::string_view text,
                              const std::regex& pattern,
                              const std::string& normalSeq,
                              const std::string& matchSeq)
{
    std::string copy(text);
    std::sregex_iterator it(copy.begin(), copy.end(), pattern);
    std::sregex_iterator end;
    if(it == end)
    {
        out += normalSeq;
        out += copy;
        return;
    }

    size_t pos = 0;
    for(; it != end; ++it)
    {
        const std::smatch& match = *it;
        if(match.length() <= 0)
            continue;
        size_t matchPos = (size_t)match.position();
        if(matchPos > pos)
        {
            out += normalSeq;
            out.append(copy.data() + pos, matchPos - pos);
        }
        out += matchSeq;
        out.append(copy.data() + matchPos, (size_t)match.length());
        pos = matchPos + (size_t)match.length();
    }
    if(pos < copy.size())
    {
        out += normalSeq;
        out.append(copy.data() + pos, copy.size() - pos);
    }
}
} // namespace

void GitShowCommitMode::on_enter(ModeContext& ctx)
{
    searchIndex = std::clamp(searchIndex, 0, (int)lines.size());
    ctx.requestFullRedraw();
}

void GitShowCommitMode::on_exit(ModeContext& ctx)
{
    ctx.requestFullRedraw();
}

std::optional<ModeState> GitShowCommitMode::handle(ModeContext& ctx,
                                                   int key)
{
    int c = keyCode(key);

    auto findNextMatch = [&](bool forward)
    {
        if(searchQuery.empty() || lines.empty())
            return false;
        std::regex pattern;
        if(!compile_search_regex(searchQuery, pattern))
            return false;
        int found = -1;
        if(forward)
        {
            for(int i = searchIndex + 1; i < (int)lines.size(); ++i)
            {
                if(line_matches_regex(lines[i], pattern))
                {
                    found = i;
                    break;
                }
            }
            if(found < 0)
            {
                for(int i = 0; i <= searchIndex; ++i)
                {
                    if(line_matches_regex(lines[i], pattern))
                    {
                        found = i;
                        break;
                    }
                }
            }
        }
        else
        {
            for(int i = searchIndex - 1; i >= 0; --i)
            {
                if(line_matches_regex(lines[i], pattern))
                {
                    found = i;
                    break;
                }
            }
            if(found < 0)
            {
                for(int i = (int)lines.size() - 1; i >= searchIndex; --i)
                {
                    if(line_matches_regex(lines[i], pattern))
                    {
                        found = i;
                        break;
                    }
                }
            }
        }
        if(found >= 0)
        {
            searchIndex = found;
            int maxScroll =
                std::max(0, (int)lines.size() - (ctx.screenRows() - 2));
            scrollOffset = std::clamp(found, 0, maxScroll);
            return true;
        }
        return false;
    };

    if(searchActive)
    {
        if(c == keyCode(control::ControlKey::ESC))
        {
            auto now = std::chrono::steady_clock::now();
            auto timeSinceLastEsc =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - ctx.lastEscTime())
                    .count();
            if(timeSinceLastEsc <= Editor::DOUBLE_ESC_TIMEOUT_MS)
            {
                searchIndex = searchPrevIndex;
                scrollOffset = searchPrevScroll;
                searchQuery.clear();
                ctx.editor->setStatusMessage("");
                ctx.lastEscTime() = std::chrono::steady_clock::time_point();
            }
            else
            {
                ctx.lastEscTime() = now;
            }
            searchActive = false;
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ENTER))
        {
            if(searchQuery.empty())
            {
                searchIndex = searchPrevIndex;
                scrollOffset = searchPrevScroll;
                searchActive = false;
                ctx.requestFullRedraw();
                return std::nullopt;
            }
            if(!lines.empty())
            {
                std::regex pattern;
                if(!compile_search_regex(searchQuery, pattern))
                {
                    ctx.setStatusMessage("Invalid regex: " + searchQuery);
                    searchActive = false;
                    ctx.requestFullRedraw();
                    return std::nullopt;
                }
                int found = -1;
                if(searchForward)
                {
                    for(int i = searchIndex + 1; i < (int)lines.size(); ++i)
                    {
                        if(line_matches_regex(lines[i], pattern))
                        {
                            found = i;
                            break;
                        }
                    }
                }
                else
                {
                    for(int i = searchIndex - 1; i >= 0; --i)
                    {
                        if(line_matches_regex(lines[i], pattern))
                        {
                            found = i;
                            break;
                        }
                    }
                }
                if(found >= 0)
                {
                    searchIndex = found;
                    int maxScroll =
                        std::max(0, (int)lines.size() - (ctx.screenRows() - 2));
                    scrollOffset = std::clamp(found, 0, maxScroll);
                }
                else
                {
                    ctx.setStatusMessage("search: not found");
                }
            }
            searchActive = false;
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 || c == keyCode(control::ControlKey::CTRL_H))
        {
            if(!searchQuery.empty())
                searchQuery.pop_back();
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::CTRL_U))
        {
            searchQuery.clear();
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c >= 32 && c < 127)
        {
            searchQuery += static_cast<char>(c);
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::ESC) || c == keyCode(typed::TypedKey::KEY_Q))
    {
        if(c == keyCode(control::ControlKey::ESC))
            ctx.editor->noteDoubleEscStatusClear();
        if(returnLog.has_value())
            return *returnLog;
        return NormalMode{};
    }

    int maxScroll = std::max(0, (int)lines.size() - (ctx.screenRows() - 2));

    if(c == keyCode(typed::TypedKey::KEY_J) || c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        if(scrollOffset < maxScroll)
            scrollOffset++;
    }
    else if(c == keyCode(typed::TypedKey::KEY_K) || c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        if(scrollOffset > 0)
            scrollOffset--;
    }
    else if(c == keyCode(control::ControlKey::CTRL_D))
    {
        int half = (ctx.screenRows() - 2) / 2;
        scrollOffset = std::min(scrollOffset + half, maxScroll);
    }
    else if(c == keyCode(control::ControlKey::CTRL_U))
    {
        int half = (ctx.screenRows() - 2) / 2;
        scrollOffset = std::max(scrollOffset - half, 0);
    }
    else if(c == keyCode(navigation::NavigationKey::PAGE_DOWN))
    {
        scrollOffset =
            std::min(scrollOffset + (ctx.screenRows() - 2), maxScroll);
    }
    else if(c == keyCode(navigation::NavigationKey::PAGE_UP))
    {
        scrollOffset = std::max(scrollOffset - (ctx.screenRows() - 2), 0);
    }
    else if(c == keyCode(typed::TypedKey::KEY_CAP_G))
    {
        scrollOffset = maxScroll;
    }
    else if(c == keyCode(typed::TypedKey::KEY_G))
    {
        int nextChar = Terminal::readKey();
        if(nextChar == keyCode(typed::TypedKey::KEY_G))
            scrollOffset = 0;
    }
    else if(c == keyCode(command::CommandKey::KEY_SLASH) || c == keyCode(command::CommandKey::KEY_QUESTION))
    {
        searchActive = true;
        searchForward = (c == keyCode(command::CommandKey::KEY_SLASH));
        searchQuery.clear();
        searchPrevIndex = searchIndex;
        searchPrevScroll = scrollOffset;
        ctx.lastEscTime() = std::chrono::steady_clock::time_point();
    }
    else if(c == keyCode(control::ControlKey::CTRL_H) || c == keyCode(control::ControlKey::CTRL_L))
    {
        int viewWidth = std::max(0, ctx.screenCols() - 2);
        int maxW = max_line_width(lines, ctx.editor->gitUseDefaultColors);
        int maxOffset = std::max(0, maxW - viewWidth);
        if(c == keyCode(control::ControlKey::CTRL_H))
            horizontalOffset = std::max(0, horizontalOffset - 1);
        else
            horizontalOffset = std::min(maxOffset, horizontalOffset + 1);
    }
    else if((c == keyCode(typed::TypedKey::KEY_N) || c == keyCode(typed::TypedKey::KEY_CAP_N)) && !searchQuery.empty())
    {
        bool forward = (c == keyCode(typed::TypedKey::KEY_N)) ? searchForward : !searchForward;
        if(!findNextMatch(forward))
            ctx.setStatusMessage("search: not found");
    }

    ctx.requestFullRedraw();
    return std::nullopt;
}

void GitShowCommitMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::ESC_CURSOR_HOME;
    output += editor.theme.reset();

    output += Terminal::ESC_CLEAR_LINE;
    output += editor.theme.uiAccent();
    output += Terminal::ESC_BOLD;
    output += "  GITSHOW";
    if(!commitHash.empty())
        output += " " + commitHash;
    output += editor.theme.reset();
    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    output += "  [q: quit] [j/k: scroll] [gg/G: top/bottom] [ctrl-h/l: pan]";
    output += editor.theme.baseFg();

    int availableRows = editor.screenRows - 2;
    int viewWidth = std::max(0, editor.screenCols - 2);
    int maxW = max_line_width(lines, editor.gitUseDefaultColors);
    int maxOffset = std::max(0, maxW - viewWidth);
    int hOff = std::min(horizontalOffset, maxOffset);
    for(int i = 0; i < availableRows; ++i)
    {
        output += Terminal::NEWLINE_CLEAR;
        int idx = i + scrollOffset;
        if(idx >= 0 && idx < (int)lines.size())
        {
            output += "  ";
            if(editor.gitUseDefaultColors)
            {
                std::string visibleLine = strip_ansi(lines[idx]);
                std::string slicedVisible =
                    slice_plain(visibleLine, hOff, viewWidth);
                if(searchQuery.empty())
                {
                    output += slice_with_ansi(lines[idx], hOff, viewWidth);
                    output += editor.theme.reset();
                }
                else
                {
                    const std::string& line = visibleLine;
                    const std::string* lineSeq = &editor.theme.baseFg();
                    if(line.rfind("diff --git", 0) == 0 ||
                       line.rfind("--- ", 0) == 0 || line.rfind("+++ ", 0) == 0)
                        lineSeq = &editor.theme.uiAccent();
                    else if(line.rfind("index ", 0) == 0 ||
                            line.rfind("commit ", 0) == 0)
                        lineSeq = &editor.theme.uiDim();
                    else if(line.rfind("@@ ", 0) == 0)
                        lineSeq = &editor.theme.uiInfo();
                    else if(line.rfind("Author:", 0) == 0 ||
                            line.rfind("Date:", 0) == 0)
                        lineSeq = &editor.theme.uiDim();
                    else if(!line.empty() && line[0] == keyCode(command::CommandKey::KEY_PLUS))
                        lineSeq = &editor.theme.uiSuccess();
                    else if(!line.empty() && line[0] == keyCode(command::CommandKey::KEY_MINUS))
                        lineSeq = &editor.theme.uiError();

                    std::regex pattern;
                    std::string normalSeq = editor.theme.reset() + *lineSeq;
                    if(compile_search_regex(searchQuery, pattern))
                        append_regex_highlighted(output, slicedVisible, pattern,
                                                 normalSeq,
                                                 editor.theme.searchMatch());
                    else
                        append_highlighted(output, slicedVisible, searchQuery,
                                           normalSeq,
                                           editor.theme.searchMatch());
                    output += editor.theme.reset();
                }
            }
            else
            {
                const std::string& line = lines[idx];
                const std::string* lineSeq = &editor.theme.baseFg();
                if(line.rfind("diff --git", 0) == 0 ||
                   line.rfind("--- ", 0) == 0 || line.rfind("+++ ", 0) == 0)
                    lineSeq = &editor.theme.uiAccent();
                else if(line.rfind("index ", 0) == 0 ||
                        line.rfind("commit ", 0) == 0)
                    lineSeq = &editor.theme.uiDim();
                else if(line.rfind("@@ ", 0) == 0)
                    lineSeq = &editor.theme.uiInfo();
                else if(line.rfind("Author:", 0) == 0 ||
                        line.rfind("Date:", 0) == 0)
                    lineSeq = &editor.theme.uiDim();
                else if(!line.empty() && line[0] == keyCode(command::CommandKey::KEY_PLUS))
                    lineSeq = &editor.theme.uiSuccess();
                else if(!line.empty() && line[0] == keyCode(command::CommandKey::KEY_MINUS))
                    lineSeq = &editor.theme.uiError();

                std::string normalSeq = editor.theme.reset() + *lineSeq;
                std::string sliced = slice_plain(line, hOff, viewWidth);
                std::regex pattern;
                if(compile_search_regex(searchQuery, pattern))
                    append_regex_highlighted(output, sliced, pattern,
                                             normalSeq,
                                             editor.theme.searchMatch());
                else
                    append_highlighted(output, sliced, searchQuery, normalSeq,
                                       editor.theme.searchMatch());
                output += editor.theme.reset();
            }
        }
        else
        {
            output += editor.theme.uiGutter();
            output += "  ~";
            output += editor.theme.baseFg();
        }
    }

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.statusBar();

    std::string status = " GITSHOW";
    if(!commitHash.empty())
        status += " | " + commitHash;

    std::string right = " " + std::to_string(scrollOffset + 1) + "-" +
                        std::to_string(std::min(scrollOffset + availableRows,
                                                (int)lines.size())) +
                        "/" + std::to_string(lines.size()) + " ";

    output += status;
    int padding = editor.screenCols - status.length() - right.length();
    if(padding > 0)
        output.append(padding, keyCode(control::ControlKey::SPACE));
    output += right;
    output += editor.theme.reset();

    output += Terminal::NEWLINE_CLEAR;
    if(!editor.statusMessage.empty())
    {
        output += editor.statusMessage.substr(
            0,
            std::min((size_t)editor.screenCols, editor.statusMessage.length()));
    }
    else if(searchActive)
    {
        output += (searchForward ? "/" : "?") + searchQuery;
    }

    const bool syncOutput = Terminal::useSynchronizedOutput();
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
    Terminal::write(output);
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
    Terminal::flush();
}

#ifdef UVIM_TESTING
std::string GitShowCommitMode::testRenderLine(const Theme& theme,
                                              std::string_view line,
                                              std::string_view query,
                                              bool useDefaultColors)
{
    if(useDefaultColors)
        return std::string(line);

    const std::string* lineSeq = &theme.baseFg();
    if(line.rfind("diff --git", 0) == 0 || line.rfind("--- ", 0) == 0 ||
       line.rfind("+++ ", 0) == 0)
        lineSeq = &theme.uiAccent();
    else if(line.rfind("index ", 0) == 0 || line.rfind("commit ", 0) == 0)
        lineSeq = &theme.uiDim();
    else if(line.rfind("@@ ", 0) == 0)
        lineSeq = &theme.uiInfo();
    else if(line.rfind("Author:", 0) == 0 || line.rfind("Date:", 0) == 0)
        lineSeq = &theme.uiDim();
    else if(!line.empty() && line[0] == keyCode(command::CommandKey::KEY_PLUS))
        lineSeq = &theme.uiSuccess();
    else if(!line.empty() && line[0] == keyCode(command::CommandKey::KEY_MINUS))
        lineSeq = &theme.uiError();

    std::string normalSeq = theme.reset() + *lineSeq;
    std::string output;
    output.reserve(line.size() + 32);
    append_highlighted(output, line, query, normalSeq, theme.searchMatch());
    output += theme.reset();
    return output;
}
#endif
