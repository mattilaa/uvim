#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>
#include <filesystem>

// ============================================================================
// VisualMode Implementation
// ============================================================================

void VisualMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    // Initialize visual selection start
    ed->currentBuffer->visualStartX = ctx.cursorX();
    ed->currentBuffer->visualStartY = ctx.cursorY();
    ed->currentBuffer->visualEndX = ctx.cursorX();
    ed->currentBuffer->visualEndY = ctx.cursorY();

    ed->needsFullRedraw = true;
}

void VisualMode::on_exit(ModeContext& ctx)
{
    ctx.editor->needsFullRedraw = true;
}

std::optional<ModeState> VisualMode::handle(ModeContext& ctx,
                                            int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    if(ed->diagnosticPopupActive)
    {
        if(c == keyCode(typed::TypedKey::KEY_Q))
        {
            ed->closeDiagnosticPopup();
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::CTRL_J) || c == keyCode(navigation::NavigationKey::ARROW_DOWN))
        {
            if(!ed->diagnosticPopupFixes.empty())
            {
                ed->diagnosticPopupFixIndex =
                    std::min(ed->diagnosticPopupFixIndex + 1,
                             (int)ed->diagnosticPopupFixes.size() - 1);
                int window = std::min(6, (int)ed->diagnosticPopupFixes.size());
                if(ed->diagnosticPopupFixIndex < ed->diagnosticPopupFixScroll)
                    ed->diagnosticPopupFixScroll =
                        ed->diagnosticPopupFixIndex;
                else if(ed->diagnosticPopupFixIndex >=
                        ed->diagnosticPopupFixScroll + window)
                    ed->diagnosticPopupFixScroll =
                        ed->diagnosticPopupFixIndex - window + 1;
                ed->needsFullRedraw = true;
            }
            ctx.repeatCount = 0;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::CTRL_K) || c == keyCode(navigation::NavigationKey::ARROW_UP))
        {
            if(!ed->diagnosticPopupFixes.empty())
            {
                ed->diagnosticPopupFixIndex =
                    std::max(ed->diagnosticPopupFixIndex - 1, 0);
                int window = std::min(6, (int)ed->diagnosticPopupFixes.size());
                if(ed->diagnosticPopupFixIndex < ed->diagnosticPopupFixScroll)
                    ed->diagnosticPopupFixScroll =
                        ed->diagnosticPopupFixIndex;
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

    // ========================================================================
    // Refactor-Move Prompt (R) — path entry then optional create-y/n
    // ========================================================================

    if(refactorAwaitConfirm)
    {
        if(c == keyCode(typed::TypedKey::KEY_Y) ||
           c == keyCode(typed::TypedKey::KEY_CAP_Y) ||
           c == keyCode(control::ControlKey::ENTER))
        {
            std::string path = refactorPath;
            std::string text = refactorYankedText;
            refactorPromptActive = false;
            refactorAwaitConfirm = false;
            refactorPath.clear();
            refactorYankedText.clear();
            ed->performRefactorMove(path, text, /*isLineMode=*/false,
                                    /*createIfMissing=*/true);
            return NormalMode{};
        }
        if(c == keyCode(typed::TypedKey::KEY_N) ||
           c == keyCode(typed::TypedKey::KEY_CAP_N) ||
           c == keyCode(control::ControlKey::ESC))
        {
            refactorPromptActive = false;
            refactorAwaitConfirm = false;
            refactorPath.clear();
            refactorYankedText.clear();
            ctx.setStatusMessage("Cancelled");
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        return std::nullopt;
    }

    if(refactorPromptActive)
    {
        if(c == keyCode(control::ControlKey::ESC))
        {
            refactorPromptActive = false;
            refactorPath.clear();
            refactorYankedText.clear();
            ctx.setStatusMessage("Cancelled");
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ENTER))
        {
            if(refactorPath.empty())
            {
                ctx.setStatusMessage("Move to file: (empty path)");
                return std::nullopt;
            }
            if(refactorPath.front() == '~')
            {
                ctx.setStatusMessage(
                    "'~' is not expanded — use absolute path: " +
                    refactorPath);
                ed->needsFullRedraw = true;
                return std::nullopt;
            }
            if(std::filesystem::exists(refactorPath))
            {
                std::string path = refactorPath;
                std::string text = refactorYankedText;
                refactorPromptActive = false;
                refactorPath.clear();
                refactorYankedText.clear();
                ed->performRefactorMove(path, text, /*isLineMode=*/false,
                                        /*createIfMissing=*/false);
                return NormalMode{};
            }
            refactorAwaitConfirm = true;
            ctx.setStatusMessage(
                "File '" + refactorPath +
                "' does not exist. Create new? y/n [y default]");
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::BACKSPACE) ||
           c == keyCode(control::ControlKey::DEL) ||
           c == keyCode(control::ControlKey::CTRL_H))
        {
            if(!refactorPath.empty())
                refactorPath.pop_back();
            ctx.setStatusMessage("Move to file: " + refactorPath);
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c >= keyCode(control::ControlKey::SPACE) &&
           c < keyCode(control::ControlKey::DEL))
        {
            refactorPath += static_cast<char>(c);
            ctx.setStatusMessage("Move to file: " + refactorPath);
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        return std::nullopt;
    }

    // ========================================================================
    // Count Prefix Accumulation
    // ========================================================================

    if(ctx.repeatCount == 0 && c >= keyCode(typed::TypedKey::KEY_1) && c <= keyCode(typed::TypedKey::KEY_9))
    {
        ctx.repeatCount = c - keyCode(typed::TypedKey::KEY_0);
        return std::nullopt;
    }
    if(ctx.repeatCount > 0 && c >= keyCode(typed::TypedKey::KEY_0) && c <= keyCode(typed::TypedKey::KEY_9))
    {
        ctx.repeatCount = ctx.repeatCount * 10 + (c - keyCode(typed::TypedKey::KEY_0));
        return std::nullopt;
    }
    int count = std::max(1, ctx.repeatCount);

    // ========================================================================
    // Leader Key (Space)
    // ========================================================================

    if(ctx.commandBuffer == " ")
    {
        if(c == keyCode(typed::TypedKey::KEY_F))
        {
            ctx.commandBuffer.clear();
            ctx.repeatCount = 0;
            if(ed->isFileType<FileType::Python>())
            {
                ed->pythonFormatBuffer();
            }
            else if(ed->isFileType<FileType::Robot>())
            {
                ed->robotFormatBuffer();
            }
            else if(ed->isFileType<FileType::Json>())
            {
                ed->jsonFormatBuffer();
            }
            else if(ed->isFileType<FileType::Yaml>())
            {
                ed->yamlFormatBuffer();
            }
            else if(ed->isFileType<FileType::Mla>())
            {
                ed->mlangFormatBuffer();
            }
            else
            {
                ed->clangFormatVisualSelection();
            }
            return NormalMode{};
        }
        if(c == keyCode(typed::TypedKey::KEY_P))
        {
            ctx.commandBuffer.clear();
            ctx.repeatCount = 0;
            std::string clipboard = ed->getSystemClipboard();
            if(clipboard.empty())
            {
                ctx.setStatusMessage("System clipboard is empty");
                return std::nullopt;
            }
            ed->deleteSelection();
            ed->yankBuffer = clipboard;
            ed->pasteBefore();
            return NormalMode{};
        }
        if(c == keyCode(control::ControlKey::SPACE))
        {
            ctx.commandBuffer.clear();
            ctx.setStatusMessage("");
            ctx.repeatCount = 0;
            return std::nullopt;
        }

        // Unknown leader command: cancel leader
        ctx.commandBuffer.clear();
        ctx.setStatusMessage("");
        ctx.repeatCount = 0;
    }

    if(c == keyCode(control::ControlKey::SPACE))
    {
        ctx.commandBuffer = " ";
        ctx.setStatusMessage("Leader");
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Exit Visual Mode
    // ========================================================================

    if(c == keyCode(control::ControlKey::ESC) || c == keyCode(typed::TypedKey::KEY_V))
    {
        if(c == keyCode(control::ControlKey::ESC))
            ed->noteDoubleEscStatusClear();
        return NormalMode{};
    }

    // ========================================================================
    // Switch to Visual Line Mode
    // ========================================================================

    if(c == keyCode(typed::TypedKey::KEY_CAP_V))
    {
        return VisualLineMode{};
    }

    // ========================================================================
    // Switch to Visual Block Mode
    // ========================================================================

    if(c == keyCode(control::ControlKey::CTRL_V))
    {
        return VisualBlockMode{};
    }

    // ========================================================================
    // Movement
    // ========================================================================

    bool didMove = false;
    switch(c)
    {
    case keyCode(typed::TypedKey::KEY_H):
    case keyCode(navigation::NavigationKey::ARROW_LEFT):
        ed->moveLeft(count);
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_J):
    case keyCode(navigation::NavigationKey::ARROW_DOWN):
        ed->moveDown(count);
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_K):
    case keyCode(navigation::NavigationKey::ARROW_UP):
        ed->moveUp(count);
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_L):
    case keyCode(navigation::NavigationKey::ARROW_RIGHT):
        ed->moveRight(count);
        didMove = true;
        break;

    // Word movements
    case keyCode(typed::TypedKey::KEY_W):
        for(int i = 0; i < count; i++)
            ed->moveWordForward();
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_CAP_W):
        for(int i = 0; i < count; i++)
            ed->moveWordForwardBig();
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_B):
        for(int i = 0; i < count; i++)
            ed->moveWordBackward();
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_CAP_B):
        for(int i = 0; i < count; i++)
            ed->moveWordBackwardBig();
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_E):
        for(int i = 0; i < count; i++)
            ed->moveToEndOfWord();
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_CAP_E):
        for(int i = 0; i < count; i++)
            ed->moveToEndOfWordBig();
        didMove = true;
        break;

    // Line movements
    case keyCode(typed::TypedKey::KEY_0):
        ed->moveToLineStart();
        didMove = true;
        break;
    case keyCode(command::CommandKey::KEY_CARET):
        ed->moveToFirstNonBlank();
        didMove = true;
        break;
    case keyCode(command::CommandKey::KEY_DOLLAR):
        ed->moveToLineEnd();
        didMove = true;
        break;

    // Paragraph movements
    case keyCode(command::CommandKey::KEY_LEFT_BRACE):
        for(int i = 0; i < count; i++)
            ed->moveParagraphBackward();
        didMove = true;
        break;
    case keyCode(command::CommandKey::KEY_RIGHT_BRACE):
        for(int i = 0; i < count; i++)
            ed->moveParagraphForward();
        didMove = true;
        break;

    // File movements
    case keyCode(typed::TypedKey::KEY_CAP_G):
        ed->moveToLastLine();
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_G):
    {
        int nextChar = Terminal::readKey();
        if(nextChar == keyCode(typed::TypedKey::KEY_G))
        {
            ed->moveToFirstLine();
            didMove = true;
        }
    }
    break;
    // Screen movements
    case keyCode(typed::TypedKey::KEY_CAP_H):
        ed->moveToScreenTop();
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_CAP_M):
        ed->moveToScreenMiddle();
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_CAP_L):
        ed->moveToScreenBottom();
        didMove = true;
        break;

    // Scrolling
    case keyCode(control::ControlKey::CTRL_D):
        ed->scrollHalfPageDown(false);
        didMove = true;
        break;
    case keyCode(control::ControlKey::CTRL_U):
        ed->scrollHalfPageUp(false);
        didMove = true;
        break;
    case keyCode(control::ControlKey::CTRL_F):
    case keyCode(navigation::NavigationKey::PAGE_DOWN):
        ed->scrollPageDown();
        didMove = true;
        break;
    case keyCode(control::ControlKey::CTRL_B):
    case keyCode(navigation::NavigationKey::PAGE_UP):
        ed->scrollPageUp();
        didMove = true;
        break;

        // ========================================================================
        // Operations on Selection
        // ========================================================================

    case keyCode(typed::TypedKey::KEY_D):
    case keyCode(typed::TypedKey::KEY_X):
        ed->yankSelection();
        ed->deleteSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_Y):
        ed->yankSelection();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_P):
    {
        std::string pasteBuffer = ed->yankBuffer;
        if(pasteBuffer.empty() && ed->useSystemClipboard)
        {
            pasteBuffer = ed->getSystemClipboard();
        }
        if(pasteBuffer.empty())
            return std::nullopt;
        ed->yankBuffer = pasteBuffer;
        ed->deleteSelection();
        ed->pasteBefore();
        return NormalMode{};
    }

    case keyCode(typed::TypedKey::KEY_C):
    {
        int nextChar = Terminal::readKeyTimeout(300);
        if(nextChar == keyCode(typed::TypedKey::KEY_I))
        {
            int startY = std::min(ed->currentBuffer->visualStartY,
                                  ed->currentBuffer->visualEndY);
            int endY = std::max(ed->currentBuffer->visualStartY,
                                ed->currentBuffer->visualEndY);
            ed->commentLines(startY, endY);
            return NormalMode{};
        }
        ed->yankSelection();
        ed->deleteSelection();
        ed->saveState();
        return InsertMode{};
    }

    case keyCode(command::CommandKey::KEY_GREATER):
        ed->indentSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(command::CommandKey::KEY_LESS):
        ed->dedentSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(command::CommandKey::KEY_TILDE):
        ed->toggleCaseSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_U):
        ed->lowercaseSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_CAP_U):
        ed->uppercaseSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_CAP_J):
    {
        // Join selected lines
        int startLine = std::min(ed->currentBuffer->visualStartY,
                                 ed->currentBuffer->visualEndY);
        int endLine = std::max(ed->currentBuffer->visualStartY,
                               ed->currentBuffer->visualEndY);
        ctx.cursorY() = startLine;
        for(int i = startLine;
            i < endLine && ctx.cursorY() < (int)ctx.lines().size() - 1; i++)
        {
            ed->joinLines();
        }
        ed->saveState();
        return NormalMode{};
    }

    case keyCode(typed::TypedKey::KEY_CAP_R):
    {
        ed->yankSelection();
        refactorYankedText = ed->yankBuffer;
        refactorPath.clear();
        refactorPromptActive = true;
        refactorAwaitConfirm = false;
        ctx.setStatusMessage("Move to file: ");
        ed->needsFullRedraw = true;
        return std::nullopt;
    }

    // Swap selection ends
    case keyCode(typed::TypedKey::KEY_O):
    {
        std::swap(ctx.cursorX(), ed->currentBuffer->visualStartX);
        std::swap(ctx.cursorY(), ed->currentBuffer->visualStartY);
        didMove = true;
    }
    break;
    }

    if(didMove)
        ctx.repeatCount = 0;

    // Update visual end
    ed->currentBuffer->visualEndX = ctx.cursorX();
    ed->currentBuffer->visualEndY = ctx.cursorY();
    ed->needsFullRedraw = true;

    return std::nullopt;
}

// ============================================================================
// VisualLineMode Implementation
// ============================================================================

void VisualLineMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    // Initialize visual line selection
    ed->currentBuffer->visualStartY = ctx.cursorY();
    ed->currentBuffer->visualEndY = ctx.cursorY();

    ed->needsFullRedraw = true;
}

void VisualLineMode::on_exit(ModeContext& ctx)
{
    ctx.editor->needsFullRedraw = true;
}

std::optional<ModeState> VisualLineMode::handle(ModeContext& ctx,
                                                int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    // ========================================================================
    // Refactor-Move Prompt (R) — path entry then optional create-y/n
    // ========================================================================

    if(refactorAwaitConfirm)
    {
        if(c == keyCode(typed::TypedKey::KEY_Y) ||
           c == keyCode(typed::TypedKey::KEY_CAP_Y) ||
           c == keyCode(control::ControlKey::ENTER))
        {
            std::string path = refactorPath;
            std::string text = refactorYankedText;
            refactorPromptActive = false;
            refactorAwaitConfirm = false;
            refactorPath.clear();
            refactorYankedText.clear();
            ed->performRefactorMove(path, text, /*isLineMode=*/true,
                                    /*createIfMissing=*/true);
            return NormalMode{};
        }
        if(c == keyCode(typed::TypedKey::KEY_N) ||
           c == keyCode(typed::TypedKey::KEY_CAP_N) ||
           c == keyCode(control::ControlKey::ESC))
        {
            refactorPromptActive = false;
            refactorAwaitConfirm = false;
            refactorPath.clear();
            refactorYankedText.clear();
            ctx.setStatusMessage("Cancelled");
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        return std::nullopt;
    }

    if(refactorPromptActive)
    {
        if(c == keyCode(control::ControlKey::ESC))
        {
            refactorPromptActive = false;
            refactorPath.clear();
            refactorYankedText.clear();
            ctx.setStatusMessage("Cancelled");
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ENTER))
        {
            if(refactorPath.empty())
            {
                ctx.setStatusMessage("Move to file: (empty path)");
                return std::nullopt;
            }
            if(refactorPath.front() == '~')
            {
                ctx.setStatusMessage(
                    "'~' is not expanded — use absolute path: " +
                    refactorPath);
                ed->needsFullRedraw = true;
                return std::nullopt;
            }
            if(std::filesystem::exists(refactorPath))
            {
                std::string path = refactorPath;
                std::string text = refactorYankedText;
                refactorPromptActive = false;
                refactorPath.clear();
                refactorYankedText.clear();
                ed->performRefactorMove(path, text, /*isLineMode=*/true,
                                        /*createIfMissing=*/false);
                return NormalMode{};
            }
            refactorAwaitConfirm = true;
            ctx.setStatusMessage(
                "File '" + refactorPath +
                "' does not exist. Create new? y/n [y default]");
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::BACKSPACE) ||
           c == keyCode(control::ControlKey::DEL) ||
           c == keyCode(control::ControlKey::CTRL_H))
        {
            if(!refactorPath.empty())
                refactorPath.pop_back();
            ctx.setStatusMessage("Move to file: " + refactorPath);
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c >= keyCode(control::ControlKey::SPACE) &&
           c < keyCode(control::ControlKey::DEL))
        {
            refactorPath += static_cast<char>(c);
            ctx.setStatusMessage("Move to file: " + refactorPath);
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        return std::nullopt;
    }

    // ========================================================================
    // Count Prefix Accumulation
    // ========================================================================

    if(ctx.repeatCount == 0 && c >= keyCode(typed::TypedKey::KEY_1) && c <= keyCode(typed::TypedKey::KEY_9))
    {
        ctx.repeatCount = c - keyCode(typed::TypedKey::KEY_0);
        return std::nullopt;
    }
    if(ctx.repeatCount > 0 && c >= keyCode(typed::TypedKey::KEY_0) && c <= keyCode(typed::TypedKey::KEY_9))
    {
        ctx.repeatCount = ctx.repeatCount * 10 + (c - keyCode(typed::TypedKey::KEY_0));
        return std::nullopt;
    }
    int count = std::max(1, ctx.repeatCount);

    // ========================================================================
    // Leader Key (Space)
    // ========================================================================

    if(ctx.commandBuffer == " ")
    {
        if(c == keyCode(typed::TypedKey::KEY_F))
        {
            ctx.commandBuffer.clear();
            ctx.repeatCount = 0;
            if(ed->isFileType<FileType::Python>())
            {
                ed->pythonFormatBuffer();
            }
            else if(ed->isFileType<FileType::Robot>())
            {
                ed->robotFormatBuffer();
            }
            else if(ed->isFileType<FileType::Json>())
            {
                ed->jsonFormatBuffer();
            }
            else if(ed->isFileType<FileType::Yaml>())
            {
                ed->yamlFormatBuffer();
            }
            else if(ed->isFileType<FileType::Mla>())
            {
                ed->mlangFormatBuffer();
            }
            else
            {
                ed->clangFormatVisualSelection();
            }
            return NormalMode{};
        }
        if(c == keyCode(typed::TypedKey::KEY_P))
        {
            ctx.commandBuffer.clear();
            ctx.repeatCount = 0;
            std::string clipboard = ed->getSystemClipboard();
            if(clipboard.empty())
            {
                ctx.setStatusMessage("System clipboard is empty");
                return std::nullopt;
            }
            ed->deleteLineSelection();
            ed->yankBuffer = clipboard;
            ed->pasteBefore();
            return NormalMode{};
        }
        if(c == keyCode(control::ControlKey::SPACE))
        {
            ctx.commandBuffer.clear();
            ctx.setStatusMessage("");
            ctx.repeatCount = 0;
            return std::nullopt;
        }

        ctx.commandBuffer.clear();
        ctx.setStatusMessage("");
        ctx.repeatCount = 0;
    }

    if(c == keyCode(control::ControlKey::SPACE))
    {
        ctx.commandBuffer = " ";
        ctx.setStatusMessage("Leader");
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Exit / Switch Modes
    // ========================================================================

    if(c == keyCode(control::ControlKey::ESC) || c == keyCode(typed::TypedKey::KEY_CAP_V))
    {
        if(c == keyCode(control::ControlKey::ESC))
            ed->noteDoubleEscStatusClear();
        return NormalMode{};
    }

    if(c == keyCode(typed::TypedKey::KEY_V))
    {
        return VisualMode{};
    }

    if(c == keyCode(control::ControlKey::CTRL_V))
    {
        return VisualBlockMode{};
    }

    // ========================================================================
    // Movement
    // ========================================================================

    bool didMove = false;
    switch(c)
    {
    case keyCode(typed::TypedKey::KEY_J):
    case keyCode(navigation::NavigationKey::ARROW_DOWN):
        ed->moveDown(count);
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_K):
    case keyCode(navigation::NavigationKey::ARROW_UP):
        ed->moveUp(count);
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_CAP_G):
        ed->moveToLastLine();
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_G):
    {
        int nextChar = Terminal::readKey();
        if(nextChar == keyCode(typed::TypedKey::KEY_G))
        {
            ed->moveToFirstLine();
            didMove = true;
        }
    }
    break;
    case keyCode(command::CommandKey::KEY_LEFT_BRACE):
        for(int i = 0; i < count; i++)
            ed->moveParagraphBackward();
        didMove = true;
        break;
    case keyCode(command::CommandKey::KEY_RIGHT_BRACE):
        for(int i = 0; i < count; i++)
            ed->moveParagraphForward();
        didMove = true;
        break;
    case keyCode(control::ControlKey::CTRL_D):
        ed->scrollHalfPageDown(false);
        didMove = true;
        break;
    case keyCode(control::ControlKey::CTRL_U):
        ed->scrollHalfPageUp(false);
        didMove = true;
        break;
    case keyCode(command::CommandKey::KEY_PERCENT):
        ed->moveToMatchingBracket();
        didMove = true;
        break;

        // ========================================================================
        // Line Operations
        // ========================================================================

    case keyCode(typed::TypedKey::KEY_D):
    case keyCode(typed::TypedKey::KEY_X):
        ed->yankLineSelection();
        ed->deleteLineSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_Y):
        ed->yankLineSelection();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_P):
    {
        std::string pasteBuffer = ed->yankBuffer;
        if(pasteBuffer.empty() && ed->useSystemClipboard)
        {
            pasteBuffer = ed->getSystemClipboard();
        }
        if(pasteBuffer.empty())
            return std::nullopt;
        ed->deleteLineSelection();
        ed->yankBuffer = pasteBuffer;
        ed->pasteBefore();
        return NormalMode{};
    }

    case keyCode(typed::TypedKey::KEY_C):
    {
        int nextChar = Terminal::readKeyTimeout(300);
        if(nextChar == keyCode(typed::TypedKey::KEY_I))
        {
            int startY = std::min(ed->currentBuffer->visualStartY,
                                  ed->currentBuffer->visualEndY);
            int endY = std::max(ed->currentBuffer->visualStartY,
                                ed->currentBuffer->visualEndY);
            ed->commentLines(startY, endY);
            return NormalMode{};
        }
        ed->yankLineSelection();
        ed->deleteLineSelection();
        ed->saveState();
        return InsertMode{};
    }

    case keyCode(command::CommandKey::KEY_GREATER):
        ed->indentLineSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(command::CommandKey::KEY_LESS):
        ed->dedentLineSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(command::CommandKey::KEY_EQUAL):
        ed->autoIndentLineSelection();
        ed->saveState();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_CAP_J):
    {
        int startLine = std::min(ed->currentBuffer->visualStartY,
                                 ed->currentBuffer->visualEndY);
        int endLine = std::max(ed->currentBuffer->visualStartY,
                               ed->currentBuffer->visualEndY);
        ctx.cursorY() = startLine;
        for(int i = startLine;
            i < endLine && ctx.cursorY() < (int)ctx.lines().size() - 1; i++)
        {
            ed->joinLines();
        }
        ed->saveState();
        return NormalMode{};
    }

    case keyCode(typed::TypedKey::KEY_CAP_R):
    {
        ed->yankLineSelection();
        refactorYankedText = ed->yankBuffer;
        refactorPath.clear();
        refactorPromptActive = true;
        refactorAwaitConfirm = false;
        ctx.setStatusMessage("Move to file: ");
        ed->needsFullRedraw = true;
        return std::nullopt;
    }

    // Swap selection ends
    case keyCode(typed::TypedKey::KEY_O):
        std::swap(ctx.cursorY(), ed->currentBuffer->visualStartY);
        didMove = true;
        break;
    }

    if(didMove)
        ctx.repeatCount = 0;

    // Update visual end
    ed->currentBuffer->visualEndY = ctx.cursorY();
    ed->needsFullRedraw = true;

    return std::nullopt;
}

// ============================================================================
// VisualBlockMode Implementation
// ============================================================================

void VisualBlockMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    // Initialize visual block selection
    ed->currentBuffer->visualBlockStartX = ctx.cursorX();
    ed->currentBuffer->visualBlockStartY = ctx.cursorY();
    ed->currentBuffer->visualBlockEndX = ctx.cursorX();
    ed->currentBuffer->visualBlockEndY = ctx.cursorY();

    ed->needsFullRedraw = true;
}

void VisualBlockMode::on_exit(ModeContext& ctx)
{
    ctx.editor->needsFullRedraw = true;
}

std::optional<ModeState> VisualBlockMode::handle(ModeContext& ctx,
                                                 int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    // ========================================================================
    // Count Prefix Accumulation
    // ========================================================================

    if(ctx.repeatCount == 0 && c >= keyCode(typed::TypedKey::KEY_1) && c <= keyCode(typed::TypedKey::KEY_9))
    {
        ctx.repeatCount = c - keyCode(typed::TypedKey::KEY_0);
        return std::nullopt;
    }
    if(ctx.repeatCount > 0 && c >= keyCode(typed::TypedKey::KEY_0) && c <= keyCode(typed::TypedKey::KEY_9))
    {
        ctx.repeatCount = ctx.repeatCount * 10 + (c - keyCode(typed::TypedKey::KEY_0));
        return std::nullopt;
    }
    int count = std::max(1, ctx.repeatCount);

    // ========================================================================
    // Exit
    // ========================================================================

    if(c == keyCode(control::ControlKey::ESC) || c == keyCode(control::ControlKey::CTRL_V))
    {
        if(c == keyCode(control::ControlKey::ESC))
            ed->noteDoubleEscStatusClear();
        return NormalMode{};
    }

    // ========================================================================
    // Leader Key (Space)
    // ========================================================================

    if(ctx.commandBuffer == " ")
    {
        if(c == keyCode(typed::TypedKey::KEY_F))
        {
            ctx.commandBuffer.clear();
            ctx.repeatCount = 0;
            if(ed->isFileType<FileType::Python>())
            {
                ed->pythonFormatBuffer();
            }
            else if(ed->isFileType<FileType::Robot>())
            {
                ed->robotFormatBuffer();
            }
            else if(ed->isFileType<FileType::Json>())
            {
                ed->jsonFormatBuffer();
            }
            else if(ed->isFileType<FileType::Yaml>())
            {
                ed->yamlFormatBuffer();
            }
            else if(ed->isFileType<FileType::Mla>())
            {
                ed->mlangFormatBuffer();
            }
            else
            {
                ed->clangFormatVisualBlockSelection();
            }
            return NormalMode{};
        }
        if(c == keyCode(control::ControlKey::SPACE))
        {
            ctx.commandBuffer.clear();
            ctx.setStatusMessage("");
            ctx.repeatCount = 0;
            return std::nullopt;
        }

        ctx.commandBuffer.clear();
        ctx.setStatusMessage("");
        ctx.repeatCount = 0;
    }

    if(c == keyCode(control::ControlKey::SPACE))
    {
        ctx.commandBuffer = " ";
        ctx.setStatusMessage("Leader");
        ctx.repeatCount = 0;
        return std::nullopt;
    }

    // ========================================================================
    // Movement
    // ========================================================================

    bool didMove = false;
    switch(c)
    {
    case keyCode(typed::TypedKey::KEY_H):
    case keyCode(navigation::NavigationKey::ARROW_LEFT):
        ed->moveLeft(count);
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_J):
    case keyCode(navigation::NavigationKey::ARROW_DOWN):
        ed->moveDown(count);
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_K):
    case keyCode(navigation::NavigationKey::ARROW_UP):
        ed->moveUp(count);
        didMove = true;
        break;
    case keyCode(typed::TypedKey::KEY_L):
    case keyCode(navigation::NavigationKey::ARROW_RIGHT):
        ed->moveRight(count);
        didMove = true;
        break;

        // ========================================================================
        // Block Operations
        // ========================================================================

    case keyCode(typed::TypedKey::KEY_D):
    case keyCode(typed::TypedKey::KEY_X):
        ed->deleteVisualBlock();
        ed->saveState();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_Y):
        ed->yankVisualBlock();
        return NormalMode{};

    case keyCode(typed::TypedKey::KEY_C):
        ed->changeVisualBlock();
        return InsertMode{};

    // Insert at block start
    case keyCode(typed::TypedKey::KEY_CAP_I):
        ed->prepareBlockInsert(false);
        return InsertMode{};

    // Append at block end
    case keyCode(typed::TypedKey::KEY_CAP_A):
        ed->prepareBlockInsert(true);
        return InsertMode{};

    // Swap corners
    case keyCode(typed::TypedKey::KEY_O):
    case keyCode(typed::TypedKey::KEY_CAP_O):
        ed->swapVisualBlockCorner();
        didMove = true;
        break;
    }

    if(didMove)
        ctx.repeatCount = 0;

    // Update block end
    ed->currentBuffer->visualBlockEndX = ctx.cursorX();
    ed->currentBuffer->visualBlockEndY = ctx.cursorY();
    ed->needsFullRedraw = true;

    return std::nullopt;
}
