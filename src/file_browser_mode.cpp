#include "editor_lsp_query.h"
#include "mode_state_machine.h"
#include "terminal.h"

// ============================================================================
// FileBrowserMode Implementation
// ============================================================================

void FileBrowserMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    // Load directory if not already loaded
    if(ed->fileList.empty() && !ed->currentDirectory.empty())
    {
        ed->loadDirectory(ed->currentDirectory);
    }

    ed->needsFullRedraw = true;
}

void FileBrowserMode::on_exit(ModeContext& /* ctx */)
{
    // Nothing specific to do on exit
}

std::optional<ModeState> FileBrowserMode::handle(ModeContext& ctx,
                                                 const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    // ========================================================================
    // Exit
    // ========================================================================

    if(c == Terminal::ESC || c == 'q')
    {
        return NormalMode{};
    }

    // ========================================================================
    // Navigation
    // ========================================================================

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

    // ========================================================================
    // Selection / Enter Directory
    // ========================================================================

    else if(c == Terminal::ENTER || c == 'l' || c == Terminal::ARROW_RIGHT)
    {
        if(ed->selectFileBrowserEntry())
        {
            // File was opened, return to normal mode
            return NormalMode{};
        }
        // Directory was entered, stay in file browser mode
    }

    // ========================================================================
    // Go Up Directory
    // ========================================================================

    else if(c == 'h' || c == Terminal::ARROW_LEFT || c == '-')
    {
        ed->fileBrowserParent();
    }

    // ========================================================================
    // File Operations
    // ========================================================================

    // Toggle hidden files
    else if(c == '.')
    {
        ed->toggleHiddenFiles();
    }

    // Refresh
    else if(c == 'r' || c == Terminal::CTRL_L)
    {
        ed->refreshFileBrowser();
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

    // Rename file/directory
    else if(c == 'R')
    {
        ed->renameFilePrompt();
    }

    // ========================================================================
    // Mode Switching
    // ========================================================================

    // Switch to fuzzy find
    else if(c == Terminal::CTRL_P || c == 'f')
    {
        return FuzzyFindMode{};
    }

    // Switch to buffer browser
    else if(c == Terminal::CTRL_W || c == 'b')
    {
        return BufferBrowserMode{};
    }

    // Switch to grep search
    else if(c == Terminal::CTRL_S || c == '/')
    {
        return GrepSearchMode{};
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}
