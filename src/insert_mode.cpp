#include "constants.h"
#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"

// ============================================================================
// InsertMode Implementation
// ============================================================================

namespace
{
bool isEscaped(const std::string& line, int pos)
{
    int backslashes = 0;
    for(int i = pos - 1; i >= 0 && line[i] == '\\'; --i)
        backslashes++;
    return (backslashes % 2) == 1;
}

bool isCursorInsideQuote(const std::string& line, int cursorX, char quote)
{
    bool inside = false;
    int limit = std::min(cursorX, (int)line.size());
    for(int i = 0; i < limit; ++i)
    {
        if(line[i] == quote && !isEscaped(line, i))
            inside = !inside;
    }
    return inside;
}

bool isCursorInString(const std::string& line, int cursorX)
{
    return isCursorInsideQuote(line, cursorX, '"') ||
           isCursorInsideQuote(line, cursorX, '\'');
}
} // namespace

void InsertMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    ed->needsFullRedraw = true;

    // Set cursor to bar for insert mode
    Terminal::setCursorBarBlinking();

    ed->updateClangFormatIndentWidth();
}

void InsertMode::on_exit(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    // Cancel any active completion
    if(ed->completionActive)
    {
    }

    ed->finishChangeRecordingIfDeferred();

#ifdef UVIM_ENABLE_CLANGD_LSP
    ed->syncClangdDiagnosticsIfNeeded(true);
#endif

    // Restore block cursor
    Terminal::setCursorBlock();
}

std::optional<ModeState> InsertMode::handle(ModeContext& ctx,
                                            const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;
    if(ed->isRecordingChange() && !ed->isReplayingChange())
    {
        ed->recordChangeKey(c);
    }

    if(ed->emojiPopupActive)
    {
        if(c == Terminal::CTRL_J || c == Terminal::ARROW_DOWN || c == 'j')
        {
            ed->emojiNext();
            return std::nullopt;
        }
        if(c == Terminal::CTRL_K || c == Terminal::ARROW_UP || c == 'k')
        {
            ed->emojiPrev();
            return std::nullopt;
        }
        if(c == Terminal::ENTER)
        {
            ed->acceptEmoji();
            return std::nullopt;
        }
        if(c == Terminal::ESC || c == Terminal::CTRL_C)
        {
            ed->cancelEmojiPopup();
            return std::nullopt;
        }
        if(c == Terminal::BACKSPACE || c == Terminal::DEL ||
           c == Terminal::CTRL_H)
        {
            if(!ed->emojiQuery.empty())
            {
                ed->emojiQuery.pop_back();
                ed->rebuildEmojiFilter();
            }
            return std::nullopt;
        }
        if(c >= 32 && c < 127)
        {
            ed->emojiQuery.push_back((char)c);
            ed->rebuildEmojiFilter();
            return std::nullopt;
        }

        return std::nullopt;
    }

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
            if(c == Terminal::ESC)
            {
                ed->formatOnDoubleEscPending =
                    ed->formatOnInsertLeave &&
                    ed->isFileType<FileType::Cpp>() &&
                    !ed->isFileType<FileType::Mla>();
                ed->lastEscTime = std::chrono::steady_clock::now();
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
        if(c == Terminal::ESC)
        {
            ed->formatOnDoubleEscPending = ed->formatOnInsertLeave &&
                                           ed->isFileType<FileType::Cpp>() &&
                                           !ed->isFileType<FileType::Mla>();
            ed->lastEscTime = std::chrono::steady_clock::now();
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
            if(cursorY < (int)lines.size())
            {
                if(ed->utf8Mode)
                {
                    int start =
                        text_utils::prevUtf8CharStart(lines[cursorY], cursorX);
                    int len = cursorX - start;
                    if(len > 0)
                    {
                        lines[cursorY].erase(start, len);
                        cursorX = start;
                    }
                }
                else
                {
                    cursorX--;
                    lines[cursorY].erase(cursorX, 1);
                }
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
        bool inString = false;
        {
            auto& lines = ctx.lines();
            int cursorY = ctx.cursorY();
            if(cursorY >= 0 && cursorY < (int)lines.size())
                inString = isCursorInString(lines[cursorY], ctx.cursorX());
        }

        if(ed->autoBraces && (c == ')' || c == ']' || c == '}'))
        {
            auto& lines = ctx.lines();
            int& cursorX = ctx.cursorX();
            int cursorY = ctx.cursorY();

            if(cursorY >= 0 && cursorY < (int)lines.size())
            {
                std::string& line = lines[cursorY];
                if(cursorX < (int)line.length() && line[cursorX] == c)
                {
                    cursorX++;
                    return std::nullopt;
                }
            }
        }

        if(ed->autoBraces &&
           (c == '"' || c == '\'' || c == '`'))
        {
            auto handleQuote = [&](char quote) -> bool
            {
                auto& lines = ctx.lines();
                int& cursorX = ctx.cursorX();
                int cursorY = ctx.cursorY();

                if(cursorY >= (int)lines.size())
                    lines.resize(cursorY + 1);
                std::string& line = lines[cursorY];
                if(cursorX > (int)line.length())
                    cursorX = line.length();

                if(cursorX < (int)line.length() && line[cursorX] == quote)
                {
                    cursorX++;
                    return true;
                }

                if(!inString)
                {
                    ed->insertChar(quote);
                    ed->insertChar(quote);
                    ctx.cursorX()--;
                    return true;
                }

                return false;
            };

            if(handleQuote(static_cast<char>(c)))
                return std::nullopt;
        }

        if(ed->autoBraces && c == '{' && !inString)
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
                indentStr + std::string(ed->indentWidthForBraces(), ' ');

            if(ed->braceNewLineForAutoBraces())
            {
                bool blankLine =
                    line.find_first_not_of(" \t") == std::string::npos;
                if(blankLine)
                {
                    line = indentStr + "{";
                    lines.insert(lines.begin() + cursorY + 1, innerIndent);
                    lines.insert(lines.begin() + cursorY + 2,
                                 indentStr + "}" + right);
                    cursorY += 1;
                    cursorX = innerIndent.length();
                }
                else
                {
                    line = left;
                    lines.insert(lines.begin() + cursorY + 1, indentStr + "{");
                    lines.insert(lines.begin() + cursorY + 2, innerIndent);
                    lines.insert(lines.begin() + cursorY + 3,
                                 indentStr + "}" + right);
                    cursorY += 2;
                    cursorX = innerIndent.length();
                }
            }
            else
            {
                line = left + "{";
                lines.insert(lines.begin() + cursorY + 1, innerIndent);
                lines.insert(lines.begin() + cursorY + 2,
                             indentStr + "}" + right);
                cursorY += 1;
                cursorX = innerIndent.length();
            }
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

        if(ed->autoTags && c == '>' && !inString &&
           (ed->isFileType<FileType::Html>() ||
            ed->isFileType<FileType::Xml>()))
        {
            ed->insertChar(static_cast<char>(c));

            auto& lines = ctx.lines();
            int& cursorX = ctx.cursorX();
            int cursorY = ctx.cursorY();
            if(cursorY < 0 || cursorY >= (int)lines.size())
                return std::nullopt;

            std::string& line = lines[cursorY];
            int gtPos = cursorX - 1;
            if(gtPos < 0 || gtPos >= (int)line.size())
                return std::nullopt;

            int ltPos = (int)line.rfind('<', (size_t)gtPos);
            if(ltPos == (int)std::string::npos)
                return std::nullopt;

            if(ltPos + 1 < (int)line.size())
            {
                char next = line[ltPos + 1];
                if(next == '/' || next == '!' || next == '?')
                    return std::nullopt;
            }

            int j = gtPos - 1;
            while(j > ltPos && text_utils::is_space(line[j]))
                --j;
            if(j > ltPos && line[j] == '/')
                return std::nullopt;

            int nameStart = ltPos + 1;
            while(nameStart < gtPos && text_utils::is_space(line[nameStart]))
                ++nameStart;
            if(nameStart >= gtPos)
                return std::nullopt;

            int nameEnd = nameStart;
            auto isTagChar = [](char ch)
            {
                return text_utils::is_alnum(ch) || ch == ':' || ch == '_' ||
                       ch == '-';
            };
            while(nameEnd < gtPos && isTagChar(line[nameEnd]))
                ++nameEnd;
            if(nameEnd == nameStart)
                return std::nullopt;

            std::string tag = line.substr(nameStart, nameEnd - nameStart);
            bool isVoid = false;
            if(ed->isFileType<FileType::Html>())
            {
                for(auto v : constants::html_void_tags)
                {
                    if(text_utils::iequals_ascii(tag, v))
                    {
                        isVoid = true;
                        break;
                    }
                }
            }
            if(isVoid)
                return std::nullopt;

            std::string close = "</" + tag + ">";

            line.insert((size_t)cursorX, close);
            *ed->dirty = true;
            return std::nullopt;
        }

        ed->insertChar(static_cast<char>(c));

        auto& lines = ctx.lines();
        int cursorX = ctx.cursorX();
        int cursorY = ctx.cursorY();
        bool isMarkup =
            ed->isFileType<FileType::Html>() || ed->isFileType<FileType::Xml>();

        // Update completion filter if active
        if(ed->completionActive)
        {
            if(ed->completionFromLsp && ed->autoCompletion &&
               ed->shouldTriggerCompletion() && !isMarkup)
            {
                ed->requestCompletion();
            }
            else
            {
                ed->rebuildCompletionFilter();
            }
        }
        // Auto-trigger completion after '.', '::', or '->'
        else if(c == '.' && !isMarkup)
        {
            ed->triggerCompletion();
        }
        else if(c == ':' && !isMarkup && cursorX >= 2 &&
                lines[cursorY][cursorX - 2] == ':')
        {
            ed->triggerCompletion();
        }
        else if(c == '>' && !isMarkup && cursorX >= 2 &&
                lines[cursorY][cursorX - 2] == '-')
        {
            ed->triggerCompletion();
        }
        else if(ed->autoCompletion && ed->shouldTriggerCompletion())
        {
            bool canAuto = true;
            if(ed->isFileType<FileType::Cpp>())
                canAuto = ed->isClangdLspEnabled();
            else if(ed->isFileType<FileType::Python>())
                canAuto = ed->isPythonLspEnabled();
            else if(ed->isFileType<FileType::Robot>())
                canAuto = ed->isRobotLspEnabled();
            else if(ed->isFileType<FileType::Mla>())
                canAuto = ed->isMlangLspEnabled();
            else if(isMarkup)
                canAuto = false;

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
        std::string bytes;
        bytes.push_back(static_cast<char>(c));
        int expected = 1;
        unsigned char lead = (unsigned char)c;
        if((lead & 0xE0) == 0xC0)
            expected = 2;
        else if((lead & 0xF0) == 0xE0)
            expected = 3;
        else if((lead & 0xF8) == 0xF0)
            expected = 4;

        for(int i = 1; i < expected; ++i)
        {
            int next = Terminal::readKeyTimeout(0);
            if(next < 0)
                break;
            if((next & 0xC0) != 0x80)
            {
                Terminal::unreadKey(next);
                break;
            }
            bytes.push_back(static_cast<char>(next));
        }

        for(char byte : bytes)
            ed->insertChar(byte);
        if(ed->completionActive)
        {
            ed->rebuildCompletionFilter();
        }
    }

    return std::nullopt;
}
