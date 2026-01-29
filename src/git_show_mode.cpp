#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>
#include <chrono>

// ============================================================================
// GitShowCommitMode Implementation
// ============================================================================

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
                                                   const KeyEvent& event)
{
    int c = event.key;

    auto findNextMatch = [&](bool forward)
    {
        if(searchQuery.empty() || lines.empty())
            return false;
        int found = -1;
        if(forward)
        {
            for(int i = searchIndex + 1; i < (int)lines.size(); ++i)
            {
                if(lines[i].find(searchQuery) != std::string::npos)
                {
                    found = i;
                    break;
                }
            }
            if(found < 0)
            {
                for(int i = 0; i <= searchIndex; ++i)
                {
                    if(lines[i].find(searchQuery) != std::string::npos)
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
                if(lines[i].find(searchQuery) != std::string::npos)
                {
                    found = i;
                    break;
                }
            }
            if(found < 0)
            {
                for(int i = (int)lines.size() - 1; i >= searchIndex; --i)
                {
                    if(lines[i].find(searchQuery) != std::string::npos)
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
        if(c == Terminal::ESC)
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
        if(c == Terminal::ENTER)
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
                int found = -1;
                if(searchForward)
                {
                    for(int i = searchIndex + 1; i < (int)lines.size(); ++i)
                    {
                        if(lines[i].find(searchQuery) != std::string::npos)
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
                        if(lines[i].find(searchQuery) != std::string::npos)
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
        if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
        {
            if(!searchQuery.empty())
                searchQuery.pop_back();
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c == Terminal::CTRL_U)
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

    if(c == Terminal::ESC || c == 'q')
    {
        if(c == Terminal::ESC)
            ctx.editor->noteDoubleEscStatusClear();
        if(returnLog.has_value())
            return *returnLog;
        return NormalMode{};
    }

    int maxScroll = std::max(0, (int)lines.size() - (ctx.screenRows() - 2));

    if(c == 'j' || c == Terminal::ARROW_DOWN)
    {
        if(scrollOffset < maxScroll)
            scrollOffset++;
    }
    else if(c == 'k' || c == Terminal::ARROW_UP)
    {
        if(scrollOffset > 0)
            scrollOffset--;
    }
    else if(c == Terminal::CTRL_D)
    {
        int half = (ctx.screenRows() - 2) / 2;
        scrollOffset = std::min(scrollOffset + half, maxScroll);
    }
    else if(c == Terminal::CTRL_U)
    {
        int half = (ctx.screenRows() - 2) / 2;
        scrollOffset = std::max(scrollOffset - half, 0);
    }
    else if(c == Terminal::PAGE_DOWN)
    {
        scrollOffset =
            std::min(scrollOffset + (ctx.screenRows() - 2), maxScroll);
    }
    else if(c == Terminal::PAGE_UP)
    {
        scrollOffset = std::max(scrollOffset - (ctx.screenRows() - 2), 0);
    }
    else if(c == 'G')
    {
        scrollOffset = maxScroll;
    }
    else if(c == 'g')
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
            scrollOffset = 0;
    }
    else if(c == '/' || c == '?')
    {
        searchActive = true;
        searchForward = (c == '/');
        searchQuery.clear();
        searchPrevIndex = searchIndex;
        searchPrevScroll = scrollOffset;
        ctx.lastEscTime() = std::chrono::steady_clock::time_point();
    }
    else if((c == 'n' || c == 'N') && !searchQuery.empty())
    {
        bool forward = (c == 'n') ? searchForward : !searchForward;
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
    output += "  [q: quit] [j/k: scroll] [gg/G: top/bottom]";
    output += editor.theme.baseFg();

    int availableRows = editor.screenRows - 2;
    for(int i = 0; i < availableRows; ++i)
    {
        output += Terminal::NEWLINE_CLEAR;
        int idx = i + scrollOffset;
        if(idx >= 0 && idx < (int)lines.size())
        {
            output += "  ";
            if(editor.gitUseDefaultColors)
            {
                output += lines[idx];
                output += editor.theme.reset();
            }
            else
            {
                const std::string& line = lines[idx];
                if(line.rfind("diff --git", 0) == 0 ||
                   line.rfind("--- ", 0) == 0 || line.rfind("+++ ", 0) == 0)
                {
                    output += editor.theme.uiAccent();
                }
                else if(line.rfind("index ", 0) == 0 ||
                        line.rfind("commit ", 0) == 0)
                {
                    output += editor.theme.uiDim();
                }
                else if(line.rfind("@@ ", 0) == 0)
                {
                    output += editor.theme.uiInfo();
                }
                else if(line.rfind("Author:", 0) == 0 ||
                        line.rfind("Date:", 0) == 0)
                {
                    output += editor.theme.uiDim();
                }
                else if(!line.empty() && line[0] == '+')
                {
                    output += editor.theme.uiSuccess();
                }
                else if(!line.empty() && line[0] == '-')
                {
                    output += editor.theme.uiError();
                }
                else
                {
                    output += editor.theme.baseFg();
                }
                output += line;
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
        output.append(padding, ' ');
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

    Terminal::write(output);
    Terminal::flush();
}
