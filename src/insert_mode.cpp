#include "constants.h"
#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"

// ============================================================================
// InsertMode Implementation
// ============================================================================

namespace editor::statemachine
{
namespace
{
void moveCursorLeftForNormalMode(Editor* ed, ModeContext& ctx)
{
    if(ctx.cursorX() <= 0)
        return;

    if(ed->utf8Mode && ed->lines && ctx.cursorY() >= 0 &&
       ctx.cursorY() < (int)ed->lines->size())
    {
        ctx.cursorX() = text_utils::prevUtf8CharStart(
            (*ed->lines)[ctx.cursorY()], ctx.cursorX());
    }
    else
    {
        ctx.cursorX()--;
    }
}

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
    return isCursorInsideQuote(
               line, cursorX, keyCode(command::CommandKey::KEY_DOUBLE_QUOTE)) ||
           isCursorInsideQuote(line, cursorX, '\'');
}

std::string trimCopy(std::string s)
{
    size_t start = 0;
    while(start < s.size() && std::isspace((unsigned char)s[start]))
        ++start;
    size_t end = s.size();
    while(end > start && std::isspace((unsigned char)s[end - 1]))
        --end;
    return s.substr(start, end - start);
}

bool startsKeyword(const std::string& text, std::string_view keyword)
{
    if(text.size() < keyword.size() ||
       text.compare(0, keyword.size(), keyword) != 0)
        return false;
    if(text.size() == keyword.size())
        return true;
    char next = text[keyword.size()];
    return !text_utils::isIdent(next);
}

bool cursorIsInsideDelimitedExpression(std::string_view left)
{
    int parenDepth = 0;
    int bracketDepth = 0;
    for(char ch : left)
    {
        if(ch == '(')
            ++parenDepth;
        else if(ch == ')' && parenDepth > 0)
            --parenDepth;
        else if(ch == '[')
            ++bracketDepth;
        else if(ch == ']' && bracketDepth > 0)
            --bracketDepth;
    }
    return parenDepth > 0 || bracketDepth > 0;
}

bool shouldExpandLeftBraceBlock(const Editor* ed, const std::string& left)
{
    if(!ed ||
       (!ed->isFileType<FileType::Cpp>() && !ed->isFileType<FileType::Mla>()))
        return false;

    if(cursorIsInsideDelimitedExpression(left))
        return false;

    std::string trimmed = trimCopy(left);
    if(trimmed.empty())
        return false;

    if(startsKeyword(trimmed, "return"))
        return false;

    if(startsKeyword(trimmed, "if") || startsKeyword(trimmed, "else") ||
       startsKeyword(trimmed, "for") || startsKeyword(trimmed, "while") ||
       startsKeyword(trimmed, "switch") || startsKeyword(trimmed, "catch") ||
       startsKeyword(trimmed, "try") || startsKeyword(trimmed, "do"))
        return true;

    if(ed->isFileType<FileType::Mla>())
    {
        return text_utils::contains(trimmed, "fn ") ||
               trimmed.rfind("fn", 0) == 0;
    }

    if(trimmed.back() != ')')
        return false;

    if(text_utils::contains(trimmed, '=') || startsKeyword(trimmed, "auto") ||
       startsKeyword(trimmed, "let"))
        return false;

    return true;
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

std::optional<ModeState> InsertMode::handle(ModeContext& ctx, int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    if(c == keyCode(control::ControlKey::PASTE))
    {
        std::string text = Terminal::takeLastPasteText();
        if(text.empty())
            return std::nullopt;
        ed->yankBuffer = text;
        const bool useSystemClipboard = ed->useSystemClipboard;
        ed->useSystemClipboard = false;
        ed->pasteBefore();
        ed->useSystemClipboard = useSystemClipboard;
        return std::nullopt;
    }

    if(ed->diagnosticPopupActive)
    {
        if(c == keyCode(typed::TypedKey::KEY_Q))
        {
            ed->closeDiagnosticPopup();
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::CTRL_J) ||
           c == keyCode(navigation::NavigationKey::ARROW_DOWN))
        {
            if(!ed->diagnosticPopupFixes.empty())
            {
                ed->diagnosticPopupFixIndex =
                    std::min(ed->diagnosticPopupFixIndex + 1,
                             (int)ed->diagnosticPopupFixes.size() - 1);
                int window = std::min(6, (int)ed->diagnosticPopupFixes.size());
                if(ed->diagnosticPopupFixIndex < ed->diagnosticPopupFixScroll)
                    ed->diagnosticPopupFixScroll = ed->diagnosticPopupFixIndex;
                else if(ed->diagnosticPopupFixIndex >=
                        ed->diagnosticPopupFixScroll + window)
                    ed->diagnosticPopupFixScroll =
                        ed->diagnosticPopupFixIndex - window + 1;
                ed->needsFullRedraw = true;
            }
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::CTRL_K) ||
           c == keyCode(navigation::NavigationKey::ARROW_UP))
        {
            if(!ed->diagnosticPopupFixes.empty())
            {
                ed->diagnosticPopupFixIndex =
                    std::max(ed->diagnosticPopupFixIndex - 1, 0);
                int window = std::min(6, (int)ed->diagnosticPopupFixes.size());
                if(ed->diagnosticPopupFixIndex < ed->diagnosticPopupFixScroll)
                    ed->diagnosticPopupFixScroll = ed->diagnosticPopupFixIndex;
                else if(ed->diagnosticPopupFixIndex >=
                        ed->diagnosticPopupFixScroll + window)
                    ed->diagnosticPopupFixScroll =
                        ed->diagnosticPopupFixIndex - window + 1;
                ed->needsFullRedraw = true;
            }
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ENTER))
        {
            ed->applyDiagnosticFix(ed->diagnosticPopupFixIndex);
            ctx.repeatCount = 0;
            return std::nullopt;
        }
    }
    if(ed->isRecordingChange() && !ed->isReplayingChange())
    {
        ed->recordChangeKey(c);
    }

    if(ed->emojiPopupActive)
    {
        if(c == keyCode(control::ControlKey::CTRL_J) ||
           c == keyCode(navigation::NavigationKey::ARROW_DOWN))
        {
            ed->emojiNext();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::CTRL_K) ||
           c == keyCode(navigation::NavigationKey::ARROW_UP))
        {
            ed->emojiPrev();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ENTER))
        {
            ed->acceptEmoji();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ESC) ||
           c == keyCode(control::ControlKey::CTRL_C))
        {
            ed->cancelEmojiPopup();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::BACKSPACE) ||
           c == keyCode(control::ControlKey::DEL) ||
           c == keyCode(control::ControlKey::CTRL_H))
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
        if(c == keyCode(control::ControlKey::CTRL_N) ||
           c == keyCode(control::ControlKey::CTRL_J) ||
           c == keyCode(navigation::NavigationKey::ARROW_DOWN))
        {
            ed->nextCompletion();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::CTRL_P) ||
           c == keyCode(control::ControlKey::CTRL_K) ||
           c == keyCode(navigation::NavigationKey::ARROW_UP))
        {
            ed->previousCompletion();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::TAB) ||
           c == keyCode(control::ControlKey::ENTER))
        {
            ed->acceptCompletion();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ESC) ||
           c == keyCode(control::ControlKey::CTRL_C))
        {
            ed->cancelCompletion();
            moveCursorLeftForNormalMode(ed, ctx);
            if(c == keyCode(control::ControlKey::ESC))
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

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(control::ControlKey::CTRL_C))
    {
        moveCursorLeftForNormalMode(ed, ctx);
        if(c == keyCode(control::ControlKey::ESC))
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

    if(c == keyCode(control::ControlKey::CTRL_N))
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

    if(c == keyCode(control::ControlKey::CTRL_P))
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

    if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 ||
       c == keyCode(control::ControlKey::CTRL_H))
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

    if(c == keyCode(control::ControlKey::ENTER))
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

    if(c == keyCode(control::ControlKey::TAB))
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

    if(c == keyCode(control::ControlKey::SHIFT_TAB))
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
                while(i >= 0 && remove < ed->tabSpaces &&
                      line[i] == keyCode(control::ControlKey::SPACE))
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

    if(c == keyCode(navigation::NavigationKey::ARROW_LEFT))
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

    if(c == keyCode(navigation::NavigationKey::ARROW_RIGHT))
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

    if(c == keyCode(navigation::NavigationKey::ARROW_UP))
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

    if(c == keyCode(navigation::NavigationKey::ARROW_DOWN))
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

    if(c == keyCode(control::ControlKey::CTRL_W))
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

    if(c == keyCode(control::ControlKey::CTRL_U))
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

        if(ed->autoBraces &&
           (c == keyCode(command::CommandKey::KEY_RIGHT_PAREN) ||
            c == keyCode(command::CommandKey::KEY_RIGHT_BRACKET) ||
            c == keyCode(command::CommandKey::KEY_RIGHT_BRACE)))
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

        if(ed->autoBraces && ed->autoQuotes &&
           (c == keyCode(command::CommandKey::KEY_DOUBLE_QUOTE) ||
            c == keyCode(command::CommandKey::KEY_APOSTROPHE) ||
            c == keyCode(command::CommandKey::KEY_BACKTICK)))
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

        if(ed->autoBraces && ed->autoBracesInStrings &&
           c == keyCode(command::CommandKey::KEY_LEFT_BRACE) && inString)
        {
            auto& lines = ctx.lines();
            int& cursorX = ctx.cursorX();
            int cursorY = ctx.cursorY();

            if(cursorY >= (int)lines.size())
                lines.resize(cursorY + 1);
            std::string& line = lines[cursorY];
            if(cursorX > (int)line.length())
                cursorX = line.length();

            if(cursorX < (int)line.length() &&
               line[cursorX] == keyCode(command::CommandKey::KEY_RIGHT_BRACE))
            {
                ed->insertChar(keyCode(command::CommandKey::KEY_LEFT_BRACE));
                return std::nullopt;
            }

            ed->insertChar(keyCode(command::CommandKey::KEY_LEFT_BRACE));
            ed->insertChar(keyCode(command::CommandKey::KEY_RIGHT_BRACE));
            ctx.cursorX()--;
            return std::nullopt;
        }

        if(ed->autoBraces &&
           c == keyCode(command::CommandKey::KEY_LEFT_BRACE) && !inString)
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

            if(!shouldExpandLeftBraceBlock(ed, left))
            {
                ed->insertChar(keyCode(command::CommandKey::KEY_LEFT_BRACE));
                ed->insertChar(keyCode(command::CommandKey::KEY_RIGHT_BRACE));
                ctx.cursorX()--;
                return std::nullopt;
            }

            size_t indent = 0;
            while(indent < line.length() &&
                  (line[indent] == keyCode(control::ControlKey::SPACE) ||
                   line[indent] == '\t'))
            {
                indent++;
            }
            std::string indentStr = line.substr(0, indent);
            const int innerWidth = ed->indentWidthForBraces();
            std::string innerIndent =
                indentStr +
                std::string(innerWidth, keyCode(control::ControlKey::SPACE));

            if(ed->braceNewLineForAutoBraces())
            {
                bool blankLine =
                    text_utils::is_not_found(line.find_first_not_of(" \t"));
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
        if(ed->autoBraces &&
           (c == keyCode(command::CommandKey::KEY_LEFT_PAREN) ||
            c == keyCode(command::CommandKey::KEY_LEFT_BRACKET)))
        {
            char close = (c == keyCode(command::CommandKey::KEY_LEFT_PAREN))
                             ? keyCode(command::CommandKey::KEY_RIGHT_PAREN)
                             : keyCode(command::CommandKey::KEY_RIGHT_BRACKET);
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

        if(ed->autoTags && c == keyCode(command::CommandKey::KEY_GREATER) &&
           !inString &&
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

            int ltPos = (int)line.rfind(keyCode(command::CommandKey::KEY_LESS),
                                        (size_t)gtPos);
            if(text_utils::is_not_found(static_cast<size_t>(ltPos)))
                return std::nullopt;

            if(ltPos + 1 < (int)line.size())
            {
                char next = line[ltPos + 1];
                if(next == keyCode(command::CommandKey::KEY_SLASH) ||
                   next == keyCode(command::CommandKey::KEY_EXCLAMATION) ||
                   next == keyCode(command::CommandKey::KEY_QUESTION))
                    return std::nullopt;
            }

            int j = gtPos - 1;
            while(j > ltPos && text_utils::is_space(line[j]))
                --j;
            if(j > ltPos && line[j] == keyCode(command::CommandKey::KEY_SLASH))
                return std::nullopt;

            int nameStart = ltPos + 1;
            while(nameStart < gtPos && text_utils::is_space(line[nameStart]))
                ++nameStart;
            if(nameStart >= gtPos)
                return std::nullopt;

            int nameEnd = nameStart;
            auto isTagChar = [](char ch)
            {
                return text_utils::is_alnum(ch) ||
                       ch == keyCode(command::CommandKey::KEY_COLON) ||
                       ch == keyCode(command::CommandKey::KEY_UNDERSCORE) ||
                       ch == keyCode(command::CommandKey::KEY_MINUS);
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
        // Auto-trigger completion after keyCode(command::CommandKey::KEY_DOT),
        // '::', or '->'
        else if(c == keyCode(command::CommandKey::KEY_DOT) && !isMarkup)
        {
            ed->triggerCompletion();
        }
        else if(c == keyCode(command::CommandKey::KEY_COLON) && !isMarkup &&
                cursorX >= 2 &&
                lines[cursorY][cursorX - 2] ==
                    keyCode(command::CommandKey::KEY_COLON))
        {
            ed->triggerCompletion();
        }
        else if(c == keyCode(command::CommandKey::KEY_GREATER) && !isMarkup &&
                cursorX >= 2 &&
                lines[cursorY][cursorX - 2] ==
                    keyCode(command::CommandKey::KEY_MINUS))
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
                {
                    return text_utils::isIdent(ch) ||
                           ch == keyCode(command::CommandKey::KEY_MINUS) ||
                           ch == keyCode(command::CommandKey::KEY_DOT);
                };
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
} // namespace editor::statemachine
