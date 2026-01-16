#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>
#include <chrono>

// ============================================================================
// GitLogMode Implementation
// ============================================================================

void GitLogMode::rebuildFilter(Editor& editor)
{
    filtered.clear();
    if(entries.empty())
        return;

    if(query.empty())
    {
        filtered.reserve(entries.size());
        for(int i = 0; i < (int)entries.size(); ++i)
            filtered.push_back(i);
    }
    else
    {
        std::vector<std::pair<int, int>> scored;
        scored.reserve(entries.size());
        std::vector<int> positions;
        for(int i = 0; i < (int)entries.size(); ++i)
        {
            std::string text = entries[i].hash + " " + entries[i].subject;
            int score = editor.fuzzyScore(query, text, positions);
            if(score >= 0)
                scored.emplace_back(i, score);
        }
        std::stable_sort(scored.begin(), scored.end(),
                         [](const auto& a, const auto& b)
                         {
                             if(a.second != b.second)
                                 return a.second > b.second;
                             return a.first < b.first;
                         });
        for(const auto& item : scored)
            filtered.push_back(item.first);
    }

    cursor = std::clamp(cursor, 0,
                        filtered.empty() ? 0 : (int)filtered.size() - 1);
    int window = std::max(1, editor.screenRows - 2);
    if(cursor < scrollOffset)
        scrollOffset = cursor;
    else if(cursor >= scrollOffset + window)
        scrollOffset = cursor - window + 1;
}

void GitLogMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    rebuildFilter(*ed);
    ctx.requestFullRedraw();
}

void GitLogMode::on_exit(ModeContext& ctx)
{
    ctx.requestFullRedraw();
}

std::optional<ModeState> GitLogMode::handle(ModeContext& ctx,
                                            const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    auto findNextMatch = [&](bool forward)
    {
        if(searchQuery.empty() || filtered.empty())
            return false;
        int start = cursor;
        int found = -1;
        if(forward)
        {
            for(int i = start + 1; i < (int)filtered.size(); ++i)
            {
                int idx = filtered[i];
                std::string text =
                    entries[idx].hash + " " + entries[idx].subject;
                if(text.find(searchQuery) != std::string::npos)
                {
                    found = i;
                    break;
                }
            }
            if(found < 0)
            {
                for(int i = 0; i <= start; ++i)
                {
                    int idx = filtered[i];
                    std::string text =
                        entries[idx].hash + " " + entries[idx].subject;
                    if(text.find(searchQuery) != std::string::npos)
                    {
                        found = i;
                        break;
                    }
                }
            }
        }
        else
        {
            for(int i = start - 1; i >= 0; --i)
            {
                int idx = filtered[i];
                std::string text =
                    entries[idx].hash + " " + entries[idx].subject;
                if(text.find(searchQuery) != std::string::npos)
                {
                    found = i;
                    break;
                }
            }
            if(found < 0)
            {
                for(int i = (int)filtered.size() - 1; i >= start; --i)
                {
                    int idx = filtered[i];
                    std::string text =
                        entries[idx].hash + " " + entries[idx].subject;
                    if(text.find(searchQuery) != std::string::npos)
                    {
                        found = i;
                        break;
                    }
                }
            }
        }
        if(found >= 0)
        {
            cursor = found;
            int window = std::max(1, ed->screenRows - 2);
            if(cursor < scrollOffset)
                scrollOffset = cursor;
            else if(cursor >= scrollOffset + window)
                scrollOffset = cursor - window + 1;
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
                    now - ed->lastEscTime)
                    .count();
            if(timeSinceLastEsc <= Editor::DOUBLE_ESC_TIMEOUT_MS)
            {
                cursor = searchPrevCursor;
                scrollOffset = searchPrevScroll;
                searchQuery.clear();
                ed->setStatusMessage("");
                ed->lastEscTime = std::chrono::steady_clock::time_point();
            }
            else
            {
                ed->lastEscTime = now;
            }
            searchActive = false;
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c == Terminal::ENTER)
        {
            if(searchQuery.empty())
            {
                cursor = searchPrevCursor;
                scrollOffset = searchPrevScroll;
                searchActive = false;
                ctx.requestFullRedraw();
                return std::nullopt;
            }
            if(!filtered.empty())
            {
                int start = cursor;
                int found = -1;
                if(searchForward)
                {
                    for(int i = start + 1; i < (int)filtered.size(); ++i)
                    {
                        int idx = filtered[i];
                        std::string text =
                            entries[idx].hash + " " + entries[idx].subject;
                        if(text.find(searchQuery) != std::string::npos)
                        {
                            found = i;
                            break;
                        }
                    }
                }
                else
                {
                    for(int i = start - 1; i >= 0; --i)
                    {
                        int idx = filtered[i];
                        std::string text =
                            entries[idx].hash + " " + entries[idx].subject;
                        if(text.find(searchQuery) != std::string::npos)
                        {
                            found = i;
                            break;
                        }
                    }
                }
                if(found >= 0)
                {
                    cursor = found;
                    int window = std::max(1, ed->screenRows - 2);
                    if(cursor < scrollOffset)
                        scrollOffset = cursor;
                    else if(cursor >= scrollOffset + window)
                        scrollOffset = cursor - window + 1;
                }
                else
                {
                    ed->setStatusMessage("search: not found");
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
            ed->noteDoubleEscStatusClear();
        return NormalMode{};
    }

    if(c == Terminal::ENTER)
    {
        if(filtered.empty())
            return std::nullopt;
        int idx = filtered[cursor];
        if(idx < 0 || idx >= (int)entries.size())
            return std::nullopt;
        std::vector<std::string> showLines =
            ed->loadGitShowLines(entries[idx].hash);
        if(showLines.empty())
        {
            ed->setStatusMessage("git show: no output");
            return std::nullopt;
        }
        return GitShowCommitMode{entries[idx].hash, std::move(showLines),
                                 *this};
    }

    int window = std::max(1, ed->screenRows - 2);
    int maxScroll =
        std::max(0, (int)filtered.size() - window);

    if(c == Terminal::CTRL_J || c == Terminal::ARROW_DOWN || c == 'j')
    {
        if(cursor + 1 < (int)filtered.size())
        {
            cursor++;
            if(cursor >= scrollOffset + window)
                scrollOffset = cursor - window + 1;
        }
    }
    else if(c == Terminal::CTRL_K || c == Terminal::ARROW_UP || c == 'k')
    {
        if(cursor > 0)
        {
            cursor--;
            if(cursor < scrollOffset)
                scrollOffset = cursor;
        }
    }
    else if(c == Terminal::CTRL_D)
    {
        cursor = std::min(cursor + window / 2,
                          filtered.empty() ? 0 : (int)filtered.size() - 1);
        scrollOffset = std::min(scrollOffset + window / 2, maxScroll);
    }
    else if(c == Terminal::CTRL_U)
    {
        cursor = std::max(cursor - window / 2, 0);
        scrollOffset = std::max(scrollOffset - window / 2, 0);
    }
    else if(c == 'G')
    {
        cursor = filtered.empty() ? 0 : (int)filtered.size() - 1;
        scrollOffset = maxScroll;
    }
    else if(c == 'g')
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
        {
            cursor = 0;
            scrollOffset = 0;
        }
    }
    else if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
    {
        if(!query.empty())
        {
            query.pop_back();
            rebuildFilter(*ed);
        }
    }
    else if(c == Terminal::CTRL_U)
    {
        if(!query.empty())
        {
            query.clear();
            rebuildFilter(*ed);
        }
    }
    else if(c == '/' || c == '?')
    {
        searchActive = true;
        searchForward = (c == '/');
        searchQuery.clear();
        searchPrevCursor = cursor;
        searchPrevScroll = scrollOffset;
        ed->lastEscTime = std::chrono::steady_clock::time_point();
    }
    else if((c == 'n' || c == 'N') && !searchQuery.empty())
    {
        bool forward = (c == 'n') ? searchForward : !searchForward;
        if(!findNextMatch(forward))
            ed->setStatusMessage("search: not found");
    }
    else if(c >= 32 && c < 127)
    {
        query += static_cast<char>(c);
        rebuildFilter(*ed);
    }

    ctx.requestFullRedraw();
    return std::nullopt;
}

void GitLogMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;
    output += editor.theme.reset();

    output += Terminal::ESC_CLEAR_LINE;
    output += editor.theme.uiAccent();
    output += Terminal::ESC_BOLD;
    output += "  GITLOG";
    if(fileOnly)
        output += " (file)";
    output += editor.theme.reset();
    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    output += "  [q: quit] [ctrl-j/k: move] [enter: show] [type: filter]";
    output += editor.theme.baseFg();

    int availableRows = editor.screenRows - 2;
    for(int i = 0; i < availableRows; ++i)
    {
        output += Terminal::NEWLINE_CLEAR;
        int idx = scrollOffset + i;
        if(idx >= 0 && idx < (int)filtered.size())
        {
            int entryIndex = filtered[idx];
            const auto& entry = entries[entryIndex];
            std::string line = entry.hash + " " + entry.subject;
            if((int)line.size() > editor.screenCols - 4)
                line.resize(editor.screenCols - 4);
            output += "  ";
            if(idx == cursor)
                output += editor.theme.selection();
            output += editor.theme.uiAccent();
            output += entry.hash;
            output += editor.theme.baseFg();
            output += " " + entry.subject;
            output += editor.theme.reset();
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

    std::string status = " GITLOG";
    std::string right = " " + std::to_string(filtered.empty() ? 0 : cursor + 1) +
                        "/" + std::to_string(filtered.size()) + " ";

    output += status;
    int padding = editor.screenCols - status.length() - right.length();
    if(padding > 0)
        output.append(padding, ' ');
    output += right;
    output += editor.theme.reset();

    output += Terminal::NEWLINE_CLEAR;
    if(searchActive)
        output += (searchForward ? "/" : "?") + searchQuery;
    else if(!query.empty())
        output += "filter: " + query;

    Terminal::write(output);
    Terminal::flush();
}
