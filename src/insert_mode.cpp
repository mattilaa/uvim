#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"

// ============================================================================
// InsertMode Implementation
// ============================================================================

void InsertMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    ed->needsFullRedraw = true;

    // Set cursor to bar for insert mode
    Terminal::setCursorBarBlinking();
}

void InsertMode::on_exit(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    // Cancel any active completion
    if(ed->completionActive)
    {
        ed->cancelCompletion();
    }

    // Restore block cursor
    Terminal::setCursorBlock();
}

std::optional<ModeState> InsertMode::handle(ModeContext& ctx,
                                            const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // ========================================================================
    // Completion Navigation (when active)
    // ========================================================================

    if(ed->completionActive)
    {
        if(c == Terminal::CTRL_N || c == Terminal::CTRL_J ||
           c == Terminal::ARROW_DOWN)
        {
            ed->nextCompletion();
            return std::nullopt;
        }
        if(c == Terminal::CTRL_P || c == Terminal::CTRL_K ||
           c == Terminal::ARROW_UP)
        {
            ed->previousCompletion();
            return std::nullopt;
        }
        if(c == Terminal::TAB || c == Terminal::ENTER)
        {
            ed->acceptCompletion();
            return std::nullopt;
        }
        if(c == Terminal::ESC || c == Terminal::CTRL_C)
        {
            ed->cancelCompletion();
            if(ctx.cursorX() > 0)
            {
                ctx.cursorX()--;
            }
            ed->saveState();
            return NormalMode{};
        }
    }

    // ========================================================================
    // Exit Insert Mode
    // ========================================================================

    if(c == Terminal::ESC || c == Terminal::CTRL_C)
    {
        if(ctx.cursorX() > 0)
        {
            ctx.cursorX()--;
        }
        ed->saveState();
        return NormalMode{};
    }

    // ========================================================================
    // Trigger/Navigate Completion
    // ========================================================================

    if(c == Terminal::CTRL_N)
    {
        if(!ed->completionActive)
        {
            ed->triggerCompletion();
        }
        else
        {
            ed->nextCompletion();
        }
        return std::nullopt;
    }

    if(c == Terminal::CTRL_P)
    {
        if(ed->completionActive)
        {
            ed->previousCompletion();
        }
        return std::nullopt;
    }

    // ========================================================================
    // Backspace
    // ========================================================================

    if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
    {
        auto& lines = ctx.lines();
        int& cursorX = ctx.cursorX();
        int& cursorY = ctx.cursorY();

        if(cursorX > 0)
        {
            cursorX--;
            if(cursorY < (int)lines.size())
            {
                lines[cursorY].erase(cursorX, 1);
            }
            *ed->dirty = true;

            // Update completion filter if active
            if(ed->completionActive)
            {
                ed->rebuildCompletionFilter();
            }
        }
        else if(cursorY > 0)
        {
            // Join with previous line
            int prevLen = lines[cursorY - 1].length();
            lines[cursorY - 1] += lines[cursorY];
            lines.erase(lines.begin() + cursorY);
            cursorY--;
            cursorX = prevLen;
            *ed->dirty = true;

            // Cancel completion if joining lines
            if(ed->completionActive)
            {
                ed->cancelCompletion();
            }
        }
        return std::nullopt;
    }

    // ========================================================================
    // Enter / Newline
    // ========================================================================

    if(c == Terminal::ENTER)
    {
        if(ed->completionActive)
        {
            ed->acceptCompletion();
        }
        else
        {
            ed->insertNewline();
        }
        return std::nullopt;
    }

    // ========================================================================
    // Tab
    // ========================================================================

    if(c == Terminal::TAB)
    {
        if(ed->completionActive)
        {
            ed->acceptCompletion();
        }
        else
        {
            ed->insertTab();
        }
        return std::nullopt;
    }

    if(c == Terminal::SHIFT_TAB)
    {
        if(!ed->completionActive)
        {
            auto& lines = ctx.lines();
            int& cursorX = ctx.cursorX();
            int cursorY = ctx.cursorY();
            if(cursorY >= 0 && cursorY < (int)lines.size() && cursorX > 0)
            {
                std::string& line = lines[cursorY];
                int remove = 0;
                int i = cursorX - 1;
                while(i >= 0 && remove < ed->tabSpaces && line[i] == ' ')
                {
                    remove++;
                    i--;
                }
                if(remove > 0)
                {
                    line.erase(cursorX - remove, remove);
                    cursorX -= remove;
                    *ed->dirty = true;
                    if(ed->completionActive)
                        ed->rebuildCompletionFilter();
                }
                else if(cursorX > 0 && line[cursorX - 1] == '\t')
                {
                    line.erase(cursorX - 1, 1);
                    cursorX--;
                    *ed->dirty = true;
                }
            }
        }
        return std::nullopt;
    }

    // ========================================================================
    // Arrow Keys
    // ========================================================================

    if(c == Terminal::ARROW_LEFT)
    {
        if(ctx.cursorX() > 0)
        {
            ctx.cursorX()--;
        }
        if(ed->completionActive)
        {
            ed->cancelCompletion();
        }
        return std::nullopt;
    }

    if(c == Terminal::ARROW_RIGHT)
    {
        auto& lines = ctx.lines();
        int& cursorX = ctx.cursorX();
        int cursorY = ctx.cursorY();

        if(cursorY < (int)lines.size() &&
           cursorX < (int)lines[cursorY].length())
        {
            cursorX++;
        }
        if(ed->completionActive)
        {
            ed->cancelCompletion();
        }
        return std::nullopt;
    }

    if(c == Terminal::ARROW_UP)
    {
        if(ed->completionActive)
        {
            ed->previousCompletion();
        }
        else
        {
            auto& lines = ctx.lines();
            int& cursorX = ctx.cursorX();
            int& cursorY = ctx.cursorY();

            if(cursorY > 0)
            {
                cursorY--;
                if(cursorY < (int)lines.size() &&
                   cursorX > (int)lines[cursorY].length())
                {
                    cursorX = lines[cursorY].length();
                }
            }
        }
        return std::nullopt;
    }

    if(c == Terminal::ARROW_DOWN)
    {
        if(ed->completionActive)
        {
            ed->nextCompletion();
        }
        else
        {
            auto& lines = ctx.lines();
            int& cursorX = ctx.cursorX();
            int& cursorY = ctx.cursorY();

            if(cursorY < (int)lines.size() - 1)
            {
                cursorY++;
                if(cursorX > (int)lines[cursorY].length())
                {
                    cursorX = lines[cursorY].length();
                }
            }
        }
        return std::nullopt;
    }

    // ========================================================================
    // Ctrl+W - Delete Word Backward
    // ========================================================================

    if(c == Terminal::CTRL_W)
    {
        ed->deleteWordBackward();
        if(ed->completionActive)
        {
            ed->rebuildCompletionFilter();
        }
        return std::nullopt;
    }

    // ========================================================================
    // Ctrl+U - Delete to Line Start
    // ========================================================================

    if(c == Terminal::CTRL_U)
    {
        ed->deleteToLineStart();
        if(ed->completionActive)
        {
            ed->cancelCompletion();
        }
        return std::nullopt;
    }

    // ========================================================================
    // Regular Character Input
    // ========================================================================

    if(c >= 32 && c < 127)
    {
        if(ed->autoBraces && c == '{')
        {
            auto& lines = ctx.lines();
            int& cursorX = ctx.cursorX();
            int& cursorY = ctx.cursorY();

            if(cursorY >= (int)lines.size())
                lines.resize(cursorY + 1);

            std::string& line = lines[cursorY];
            if(cursorX > (int)line.length())
                cursorX = line.length();

            std::string left = line.substr(0, cursorX);
            std::string right = line.substr(cursorX);

            size_t indent = 0;
            while(indent < line.length() &&
                  (line[indent] == ' ' || line[indent] == '\t'))
            {
                indent++;
            }
            std::string indentStr = line.substr(0, indent);
            std::string innerIndent =
                indentStr + std::string(ed->tabSpaces, ' ');

            line = left + "{";
            lines.insert(lines.begin() + cursorY + 1, innerIndent);
            lines.insert(lines.begin() + cursorY + 2, indentStr + "}" + right);

            cursorY += 1;
            cursorX = innerIndent.length();
            *ed->dirty = true;
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(ed->autoBraces && (c == '(' || c == '['))
        {
            char close = (c == '(') ? ')' : ']';
            auto& lines = ctx.lines();
            int& cursorX = ctx.cursorX();
            int cursorY = ctx.cursorY();

            if(cursorY >= (int)lines.size())
                lines.resize(cursorY + 1);
            std::string& line = lines[cursorY];
            if(cursorX > (int)line.length())
                cursorX = line.length();

            if(cursorX < (int)line.length() && line[cursorX] == close)
            {
                ed->insertChar(static_cast<char>(c));
                return std::nullopt;
            }

            ed->insertChar(static_cast<char>(c));
            ed->insertChar(close);
            ctx.cursorX()--;
            return std::nullopt;
        }

        ed->insertChar(static_cast<char>(c));

        auto& lines = ctx.lines();
        int cursorX = ctx.cursorX();
        int cursorY = ctx.cursorY();

        // Update completion filter if active
        if(ed->completionActive)
        {
            if(ed->completionFromLsp && ed->autoCompletion &&
               ed->shouldTriggerCompletion())
            {
                ed->requestCompletion();
            }
            else
            {
                ed->rebuildCompletionFilter();
            }
        }
        // Auto-trigger completion after '.', '::', or '->'
        else if(c == '.')
        {
            ed->triggerCompletion();
        }
        else if(c == ':' && cursorX >= 2 && lines[cursorY][cursorX - 2] == ':')
        {
            ed->triggerCompletion();
        }
        else if(c == '>' && cursorX >= 2 && lines[cursorY][cursorX - 2] == '-')
        {
            ed->triggerCompletion();
        }
        else if(ed->autoCompletion && ed->shouldTriggerCompletion())
        {
            bool canAuto = true;
            if(ed->isCppFile())
                canAuto = ed->isClangdLspEnabled();
            else if(ed->isPythonFile())
                canAuto = ed->isPythonLspEnabled();
            else if(ed->isRobotFile())
                canAuto = ed->isRobotLspEnabled();

            if(canAuto)
            {
                const std::string& line = lines[cursorY];
                auto isWordChar = [](char ch)
                { return text_utils::isIdent(ch) || ch == '-' || ch == '.'; };
                int start = cursorX;
                while(start > 0 && isWordChar(line[start - 1]))
                    --start;
                if(cursorX - start >= 2)
                    ed->triggerCompletion();
            }
        }
        return std::nullopt;
    }

    // ========================================================================
    // UTF-8 Character Input
    // ========================================================================

    if(c >= 128)
    {
        ed->insertUtf8Char(c);
        if(ed->completionActive)
        {
            ed->rebuildCompletionFilter();
        }
    }

    return std::nullopt;
}
