#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"

// ============================================================================
// FileBrowserMode Implementation
// ============================================================================

void FileBrowserMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    if(ed->fileList.empty() && !ed->currentDirectory.empty())
    {
        ed->loadDirectory(ed->currentDirectory);
    }

    ed->needsFullRedraw = true;
}

void FileBrowserMode::on_exit(ModeContext& /* ctx */) {}

std::optional<ModeState> FileBrowserMode::handle(ModeContext& ctx,
                                                 const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // Escape or 'q' -> return to normal mode
    if(c == Terminal::ESC || c == 'q')
    {
        return NormalMode{};
    }

    // Navigation
    if(c == 'j' || c == Terminal::ARROW_DOWN)
    {
        ed->fileBrowserDown();
    }
    else if(c == 'k' || c == Terminal::ARROW_UP)
    {
        ed->fileBrowserUp();
    }
    else if(c == 'G')
    {
        ed->fileBrowserEnd();
    }
    else if(c == 'g')
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
        {
            ed->fileBrowserStart();
        }
    }
    else if(c == Terminal::CTRL_D)
    {
        ed->fileBrowserHalfPageDown();
    }
    else if(c == Terminal::CTRL_U)
    {
        ed->fileBrowserHalfPageUp();
    }

    // Selection
    else if(c == Terminal::ENTER || c == 'l' || c == Terminal::ARROW_RIGHT)
    {
        if(ed->selectFileBrowserEntry())
        {
            return NormalMode{};
        }
    }

    // Go up a directory
    else if(c == 'h' || c == Terminal::ARROW_LEFT || c == '-')
    {
        ed->fileBrowserParent();
    }

    // Refresh
    else if(c == 'r' || c == Terminal::CTRL_L)
    {
        ed->refreshFileBrowser();
    }

    // Toggle hidden files
    else if(c == '.')
    {
        ed->toggleHiddenFiles();
    }

    // Create new file
    else if(c == '%')
    {
        ed->createNewFilePrompt();
    }

    // Create new directory
    else if(c == 'd')
    {
        ed->createNewDirectoryPrompt();
    }

    // Delete file/directory
    else if(c == 'D')
    {
        ed->deleteFilePrompt();
    }

    // Rename
    else if(c == 'R')
    {
        ed->renameFilePrompt();
    }

    return std::nullopt;
}

// ============================================================================
// FuzzyFindMode Implementation
// ============================================================================

void FuzzyFindMode::on_enter(ModeContext& ctx)
{
    ctx.editor->initializeFuzzyFind();
    ctx.editor->needsFullRedraw = true;
}

void FuzzyFindMode::on_exit(ModeContext& /* ctx */) {}

std::optional<ModeState> FuzzyFindMode::handle(ModeContext& ctx,
                                               const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // Escape -> return to normal mode
    if(c == Terminal::ESC)
    {
        return NormalMode{};
    }

    // Enter -> open selected file
    if(c == Terminal::ENTER)
    {
        if(ed->selectFuzzyFindEntry())
        {
            return NormalMode{};
        }
        return std::nullopt;
    }

    // Navigation
    if(c == Terminal::CTRL_N || c == Terminal::ARROW_DOWN)
    {
        ed->fuzzyFindDown();
    }
    else if(c == Terminal::CTRL_P || c == Terminal::ARROW_UP)
    {
        ed->fuzzyFindUp();
    }
    else if(c == Terminal::CTRL_D)
    {
        ed->fuzzyFindHalfPageDown();
    }
    else if(c == Terminal::CTRL_U)
    {
        ed->fuzzyFindHalfPageUp();
    }

    // Backspace
    else if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
    {
        ed->fuzzyFindBackspace();
    }

    // Ctrl+W - delete word
    else if(c == Terminal::CTRL_W)
    {
        ed->fuzzyFindDeleteWord();
    }

    // Ctrl+U - clear query
    else if(c == Terminal::CTRL_U)
    {
        ed->fuzzyFindClear();
    }

    // Tab - toggle preview
    else if(c == Terminal::TAB)
    {
        ed->toggleFuzzyPreview();
    }

    // Regular character input
    else if(c >= 32 && c < 127)
    {
        ed->fuzzyFindAddChar(static_cast<char>(c));
    }

    return std::nullopt;
}

// ============================================================================
// BufferBrowserMode Implementation
// ============================================================================

void BufferBrowserMode::on_enter(ModeContext& ctx)
{
    ctx.editor->initializeBufferBrowser();
    ctx.editor->needsFullRedraw = true;
}

void BufferBrowserMode::on_exit(ModeContext& /* ctx */) {}

std::optional<ModeState> BufferBrowserMode::handle(ModeContext& ctx,
                                                   const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // Escape or 'q' -> return to normal mode
    if(c == Terminal::ESC || c == 'q')
    {
        return NormalMode{};
    }

    // Enter -> switch to selected buffer
    if(c == Terminal::ENTER || c == 'l')
    {
        if(ed->selectBufferBrowserEntry())
        {
            return NormalMode{};
        }
        return std::nullopt;
    }

    // Navigation
    if(c == 'j' || c == Terminal::ARROW_DOWN || c == Terminal::CTRL_N)
    {
        ed->bufferBrowserDown();
    }
    else if(c == 'k' || c == Terminal::ARROW_UP || c == Terminal::CTRL_P)
    {
        ed->bufferBrowserUp();
    }
    else if(c == 'G')
    {
        ed->bufferBrowserEnd();
    }
    else if(c == 'g')
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
        {
            ed->bufferBrowserStart();
        }
    }

    // Delete buffer
    else if(c == 'd' || c == 'D')
    {
        ed->deleteSelectedBuffer();
    }

    // Quick jump by number
    else if(c >= '1' && c <= '9')
    {
        int bufNum = c - '1';
        if(ed->switchToBufferByNumber(bufNum))
        {
            return NormalMode{};
        }
    }

    return std::nullopt;
}

// ============================================================================
// GrepSearchMode Implementation
// ============================================================================

void GrepSearchMode::on_enter(ModeContext& ctx)
{
    ctx.editor->initializeGrepSearch();
    ctx.editor->needsFullRedraw = true;
}

void GrepSearchMode::on_exit(ModeContext& /* ctx */) {}

std::optional<ModeState> GrepSearchMode::handle(ModeContext& ctx,
                                                const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // Escape -> return to normal mode
    if(c == Terminal::ESC)
    {
        return NormalMode{};
    }

    // Enter -> go to selected match
    if(c == Terminal::ENTER)
    {
        if(ed->selectGrepResult())
        {
            return NormalMode{};
        }
        return std::nullopt;
    }

    // Navigation through results
    if(c == Terminal::CTRL_N || c == Terminal::ARROW_DOWN)
    {
        ed->grepResultDown();
    }
    else if(c == Terminal::CTRL_P || c == Terminal::ARROW_UP)
    {
        ed->grepResultUp();
    }
    else if(c == Terminal::CTRL_D)
    {
        ed->grepResultHalfPageDown();
    }
    else if(c == Terminal::CTRL_U)
    {
        ed->grepResultHalfPageUp();
    }

    // Backspace
    else if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
    {
        ed->grepSearchBackspace();
    }

    // Ctrl+W - delete word
    else if(c == Terminal::CTRL_W)
    {
        ed->grepSearchDeleteWord();
    }

    // Ctrl+U - clear query
    else if(c == Terminal::CTRL_U)
    {
        ed->grepSearchClear();
    }

    // Tab - toggle preview
    else if(c == Terminal::TAB)
    {
        ed->toggleGrepPreview();
    }

    // Regular character input
    else if(c >= 32 && c < 127)
    {
        ed->grepSearchAddChar(static_cast<char>(c));
    }

    return std::nullopt;
}
