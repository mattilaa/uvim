#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <string>
#include <string_view>

// ============================================================================
// CommandOutputMode Implementation
// ============================================================================

namespace
{
// Grey background with black foreground — used for search highlight.
constexpr const char* HIGHLIGHT_SEQ =
    "\x1b[48;2;200;200;200m\x1b[38;2;0;0;0m";

// Match file-browser selection colors (see file_browser_mode.cpp:draw).
constexpr const char* SELECTED_BG = "\x1b[48;2;24;64;36m";
constexpr const char* SELECTED_CURSOR_BG = "\x1b[48;2;56;120;72m";

int caseInsensitiveFind(std::string_view haystack, std::string_view needle,
                        size_t from = 0)
{
    if(needle.empty() || from > haystack.size())
        return -1;
    auto tolow = [](unsigned char c) -> unsigned char
    { return (c >= 'A' && c <= 'Z') ? (unsigned char)(c - 'A' + 'a') : c; };
    for(size_t i = from; i + needle.size() <= haystack.size(); ++i)
    {
        size_t j = 0;
        for(; j < needle.size(); ++j)
        {
            if(tolow((unsigned char)haystack[i + j]) !=
               tolow((unsigned char)needle[j]))
                break;
        }
        if(j == needle.size())
            return (int)i;
    }
    return -1;
}

void appendHighlighted(std::string& out, std::string_view text,
                       std::string_view query,
                       const std::string& normalSeq)
{
    if(query.empty() || text.empty())
    {
        out += normalSeq;
        out.append(text.data(), text.size());
        return;
    }
    size_t pos = 0;
    while(pos < text.size())
    {
        int found = caseInsensitiveFind(text, query, pos);
        if(found < 0)
        {
            out += normalSeq;
            out.append(text.data() + pos, text.size() - pos);
            break;
        }
        if((size_t)found > pos)
        {
            out += normalSeq;
            out.append(text.data() + pos, (size_t)found - pos);
        }
        out += HIGHLIGHT_SEQ;
        out.append(text.data() + found, query.size());
        out += normalSeq;
        pos = (size_t)found + query.size();
    }
}
} // namespace

int CommandOutputMode::contentRows(const Editor& editor) const
{
    // 2 rows for header, 1 row for status bar, 1 row for message line
    return std::max(1, editor.screenRows - 3);
}

int CommandOutputMode::displayHeight(int idx, int cols) const
{
    if(idx < 0 || idx >= (int)lines.size())
        return 1;
    int usable = std::max(1, cols - 2); // 2-column left margin
    int w = text_utils::utf8DisplayWidth(lines[idx]);
    if(w <= 0)
        return 1;
    return (w + usable - 1) / usable;
}

void CommandOutputMode::clampOffsetToCursor(const Editor& editor)
{
    if(cursor < 0)
        cursor = 0;
    if(cursor >= (int)lines.size())
        cursor = std::max(0, (int)lines.size() - 1);

    if(cursor < offset)
    {
        offset = cursor;
        return;
    }

    int rows = contentRows(editor);
    int cols = std::max(1, editor.screenCols);
    while(offset < cursor)
    {
        int used = 0;
        for(int i = offset; i <= cursor; ++i)
            used += displayHeight(i, cols);
        if(used <= rows)
            break;
        ++offset;
    }
}

void CommandOutputMode::updateVisualSelection()
{
    if(!visualMode)
        return;
    selectedLines = preVisualSelected;
    int a = std::min(visualAnchor, cursor);
    int b = std::max(visualAnchor, cursor);
    a = std::max(0, a);
    b = std::min((int)lines.size() - 1, b);
    for(int i = a; i <= b; ++i)
        selectedLines.insert(i);
}

void CommandOutputMode::yankSelection(Editor& editor)
{
    if(lines.empty())
        return;
    std::vector<int> ordered;
    if(!selectedLines.empty())
    {
        ordered.reserve(selectedLines.size());
        for(int i : selectedLines)
        {
            if(i >= 0 && i < (int)lines.size())
                ordered.push_back(i);
        }
        std::sort(ordered.begin(), ordered.end());
    }
    else
    {
        int c = std::clamp(cursor, 0, (int)lines.size() - 1);
        ordered.push_back(c);
    }
    std::string buf;
    for(int i : ordered)
    {
        buf += lines[i];
        buf += '\n';
    }
    if(buf.empty())
        return;
    editor.yankBuffer = buf;
    if(editor.useSystemClipboard)
        editor.setSystemClipboard(buf);
    editor.setStatusMessage("Yanked " + std::to_string(ordered.size()) +
                            " line(s)");
}

std::optional<ModeState> CommandOutputMode::returnToFileBrowser() const
{
    FileBrowserMode fb(returnDirectory, previousFile);
    fb.browserCursor = returnBrowseCursor;
    fb.browserOffset = returnBrowseOffset;
    return ModeState{std::move(fb)};
}

void CommandOutputMode::on_enter(ModeContext& ctx)
{
    ctx.editor->needsFullRedraw = true;
    Terminal::setCursorBlock();
}

void CommandOutputMode::on_exit(ModeContext& /*ctx*/) {}

std::optional<ModeState> CommandOutputMode::handle(ModeContext& ctx, int key)
{
    int c = keyCode(key);
    Editor* ed = ctx.editor;

    if(searchActive)
    {
        if(c == keyCode(control::ControlKey::ESC))
        {
            cursor = searchPrevCursor;
            offset = searchPrevOffset;
            searchQuery.clear();
            searchActive = false;
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ENTER))
        {
            searchActive = false;
            if(!searchQuery.empty() && !lines.empty())
            {
                int found = -1;
                for(int i = cursor; i < (int)lines.size(); ++i)
                {
                    if(caseInsensitiveFind(lines[i], searchQuery) >= 0)
                    {
                        found = i;
                        break;
                    }
                }
                if(found < 0)
                {
                    for(int i = 0; i < cursor; ++i)
                    {
                        if(caseInsensitiveFind(lines[i], searchQuery) >= 0)
                        {
                            found = i;
                            break;
                        }
                    }
                }
                if(found >= 0)
                {
                    cursor = found;
                    offset = cursor;
                    clampOffsetToCursor(*ed);
                    ed->setStatusMessage("");
                }
                else
                {
                    ed->setStatusMessage("search: not found");
                }
            }
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 ||
           c == keyCode(control::ControlKey::CTRL_H))
        {
            if(!searchQuery.empty())
                searchQuery.pop_back();
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::CTRL_U))
        {
            searchQuery.clear();
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c >= 32 && c < 127)
        {
            searchQuery += static_cast<char>(c);
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        return std::nullopt;
    }

    // q exits — clears search highlight first if active, else returns.
    if(c == keyCode(typed::TypedKey::KEY_Q))
    {
        if(!searchQuery.empty())
        {
            searchQuery.clear();
            ed->setStatusMessage("");
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        return returnToFileBrowser();
    }

    if(c == keyCode(control::ControlKey::ESC))
    {
        // In visual: cancel visual range, restoring pre-visual selection.
        if(visualMode)
        {
            selectedLines = preVisualSelected;
            preVisualSelected.clear();
            visualMode = false;
            ed->setStatusMessage("");
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        // Double-ESC: clear all accumulated selection.
        if(ed->noteDoubleEscStatusClear())
        {
            selectedLines.clear();
            searchQuery.clear();
            ed->setStatusMessage("Cleared selections");
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(!searchQuery.empty())
        {
            searchQuery.clear();
            ed->setStatusMessage("");
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        return returnToFileBrowser();
    }

    if(c == keyCode(command::CommandKey::KEY_COLON) ||
       c == keyCode(command::CommandKey::KEY_SLASH))
    {
        searchActive = true;
        searchQuery.clear();
        searchPrevCursor = cursor;
        searchPrevOffset = offset;
        ed->needsFullRedraw = true;
        return std::nullopt;
    }

    bool cursorMoved = false;

    if(c == keyCode(typed::TypedKey::KEY_J) ||
       c == keyCode(navigation::NavigationKey::ARROW_DOWN) ||
       c == keyCode(control::ControlKey::CTRL_J))
    {
        if(cursor + 1 < (int)lines.size())
        {
            ++cursor;
            clampOffsetToCursor(*ed);
            cursorMoved = true;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_K) ||
            c == keyCode(navigation::NavigationKey::ARROW_UP) ||
            c == keyCode(control::ControlKey::CTRL_K))
    {
        if(cursor > 0)
        {
            --cursor;
            if(cursor < offset)
                offset = cursor;
            cursorMoved = true;
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_D) ||
            c == keyCode(navigation::NavigationKey::PAGE_DOWN))
    {
        int half = std::max(1, contentRows(*ed) / 2);
        int target = std::min((int)lines.size() - 1, cursor + half);
        if(target < 0)
            target = 0;
        if(target != cursor)
        {
            cursor = target;
            clampOffsetToCursor(*ed);
            cursorMoved = true;
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_U) ||
            c == keyCode(navigation::NavigationKey::PAGE_UP))
    {
        int half = std::max(1, contentRows(*ed) / 2);
        int target = std::max(0, cursor - half);
        if(target != cursor)
        {
            cursor = target;
            if(cursor < offset)
                offset = cursor;
            cursorMoved = true;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_CAP_G))
    {
        int target = std::max(0, (int)lines.size() - 1);
        if(target != cursor)
        {
            cursor = target;
            clampOffsetToCursor(*ed);
            cursorMoved = true;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_G))
    {
        int next = Terminal::readKey();
        if(next == keyCode(typed::TypedKey::KEY_G))
        {
            if(cursor != 0)
            {
                cursor = 0;
                offset = 0;
                cursorMoved = true;
            }
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_CAP_V))
    {
        if(visualMode)
        {
            // Exit visual, keeping the current range in selectedLines.
            visualMode = false;
            preVisualSelected.clear();
            ed->setStatusMessage("");
        }
        else if(!lines.empty())
        {
            preVisualSelected = selectedLines;
            visualAnchor = cursor;
            visualMode = true;
            updateVisualSelection();
        }
    }
    else if(c == keyCode(control::ControlKey::SPACE))
    {
        if(visualMode)
        {
            // Commit visual range into selectedLines, exit visual mode —
            // leaves the cursor free to move and start a new V-segment.
            updateVisualSelection();
            visualMode = false;
            preVisualSelected.clear();
            ed->setStatusMessage("");
        }
        else if(!lines.empty())
        {
            int c0 = std::clamp(cursor, 0, (int)lines.size() - 1);
            auto it = selectedLines.find(c0);
            if(it != selectedLines.end())
                selectedLines.erase(it);
            else
                selectedLines.insert(c0);
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_Y))
    {
        // If in visual mode, finalize the range into selectedLines first
        // (mirrors file browser behavior).
        if(visualMode)
        {
            updateVisualSelection();
            visualMode = false;
            preVisualSelected.clear();
        }
        yankSelection(*ed);
    }
    else if(c == keyCode(typed::TypedKey::KEY_N) ||
            c == keyCode(typed::TypedKey::KEY_CAP_N))
    {
        if(!searchQuery.empty() && !lines.empty())
        {
            bool forward = (c == keyCode(typed::TypedKey::KEY_N));
            int found = -1;
            int n = (int)lines.size();
            if(forward)
            {
                for(int step = 1; step <= n; ++step)
                {
                    int i = (cursor + step) % n;
                    if(caseInsensitiveFind(lines[i], searchQuery) >= 0)
                    {
                        found = i;
                        break;
                    }
                }
            }
            else
            {
                for(int step = 1; step <= n; ++step)
                {
                    int i = ((cursor - step) % n + n) % n;
                    if(caseInsensitiveFind(lines[i], searchQuery) >= 0)
                    {
                        found = i;
                        break;
                    }
                }
            }
            if(found >= 0)
            {
                cursor = found;
                offset = cursor;
                clampOffsetToCursor(*ed);
                ed->setStatusMessage("");
                cursorMoved = true;
            }
            else
            {
                ed->setStatusMessage("search: not found");
            }
        }
    }

    if(cursorMoved && visualMode)
        updateVisualSelection();

    ed->needsFullRedraw = true;
    return std::nullopt;
}

void CommandOutputMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve((size_t)editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::ESC_CURSOR_HOME;
    output += editor.theme.reset();

    output += Terminal::ESC_CLEAR_LINE;
    output += editor.theme.uiAccent();
    output += Terminal::ESC_BOLD;
    output += "  RUN: ";
    output += editor.theme.reset();
    output += editor.theme.uiPrompt();
    output += command;
    output += editor.theme.reset();

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    output +=
        "  [q: quit] [j/k: scroll] [space: mark] [V: visual] [y: yank] [/: search]";
    output += editor.theme.baseFg();

    int rows = contentRows(editor);
    int cols = std::max(1, editor.screenCols);
    int usable = std::max(1, cols - 2);

    int rowsUsed = 0;
    int idx = offset;
    while(rowsUsed < rows && idx < (int)lines.size())
    {
        int h = displayHeight(idx, cols);
        bool selected = selectedLines.count(idx) > 0;
        bool isCursor = idx == cursor;

        std::string bgPrefix;
        if(isCursor && selected)
        {
            bgPrefix = std::string(SELECTED_CURSOR_BG) + editor.theme.baseFg();
        }
        else if(isCursor)
        {
            bgPrefix = std::string(Terminal::ESC_DIM) + editor.theme.selection();
        }
        else if(selected)
        {
            bgPrefix = std::string(SELECTED_BG) + editor.theme.baseFg();
        }

        const std::string& line = lines[idx];
        int consumed = 0;
        int linePos = 0;
        int wRemaining = text_utils::utf8DisplayWidth(line);
        while(consumed < h && rowsUsed < rows)
        {
            output += Terminal::NEWLINE_CLEAR;
            if(!bgPrefix.empty())
                output += bgPrefix;
            output += "  ";

            // Extract substring covering `usable` display columns starting at
            // byte offset linePos.
            int takenWidth = 0;
            int startByte = linePos;
            while(linePos < (int)line.size() && takenWidth < usable)
            {
                int next = text_utils::nextUtf8CharStart(line, linePos);
                int w = text_utils::utf8DisplayWidth(
                    std::string_view(line).substr(linePos, next - linePos));
                if(takenWidth + w > usable)
                    break;
                takenWidth += w;
                linePos = next;
            }
            std::string_view chunk =
                std::string_view(line).substr(startByte, linePos - startByte);

            std::string normalSeq;
            if(!bgPrefix.empty())
                normalSeq = bgPrefix;
            else
                normalSeq = editor.theme.baseFg();

            if(!searchQuery.empty())
                appendHighlighted(output, chunk, searchQuery, normalSeq);
            else
            {
                output += normalSeq;
                output.append(chunk.data(), chunk.size());
            }

            // Fill remaining columns with bg so selection color spans the row.
            if(!bgPrefix.empty())
            {
                int pad = usable - takenWidth;
                if(pad > 0)
                {
                    output += bgPrefix;
                    output.append(pad, ' ');
                }
            }

            output += editor.theme.reset();
            ++consumed;
            ++rowsUsed;
            wRemaining -= takenWidth;
            if(wRemaining <= 0)
                break;
        }
        ++idx;
    }

    while(rowsUsed < rows)
    {
        output += Terminal::NEWLINE_CLEAR;
        output += editor.theme.uiGutter();
        output += "~";
        output += editor.theme.baseFg();
        ++rowsUsed;
    }

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.statusBar();
    std::string left = visualMode ? " V-RUN" : " RUN";
    if(!selectedLines.empty())
        left += " (" + std::to_string(selectedLines.size()) + " sel)";
    left += " | " + command;
    int total = (int)lines.size();
    int current = std::min(cursor + 1, total);
    std::string right = " " + std::to_string(current) + "/" +
                        std::to_string(total) + " ";
    output += left;
    int padding = editor.screenCols - (int)left.size() - (int)right.size();
    if(padding > 0)
        output.append(padding, ' ');
    output += right;
    output += editor.theme.reset();

    output += Terminal::NEWLINE_CLEAR;
    if(searchActive)
    {
        output += editor.theme.baseFg();
        output += "/";
        output += searchQuery;
        output += Terminal::ESC_BLINK;
        output += "_";
        output += Terminal::ESC_BLINK_OFF;
        output += editor.theme.reset();
    }
    else if(!editor.statusMessage.empty())
    {
        output += editor.theme.baseFg();
        output += ": ";
        size_t maxLen =
            editor.screenCols > 2 ? (size_t)editor.screenCols - 2 : 0;
        output += editor.statusMessage.substr(
            0, std::min(maxLen, editor.statusMessage.length()));
    }
    else if(!searchQuery.empty())
    {
        output += editor.theme.uiDim();
        output += " match: ";
        output += searchQuery;
        output += editor.theme.baseFg();
    }

    const bool syncOutput = Terminal::useSynchronizedOutput();
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
    Terminal::write(output);
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
    Terminal::flush();
}
