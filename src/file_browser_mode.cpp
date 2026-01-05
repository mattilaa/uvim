#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"

// ============================================================================
// FileBrowserMode Implementation
// ============================================================================

void FileBrowserMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    // Load directory if not already loaded
    if(ed->fileBrowser.directory().empty())
    {
        ed->fileBrowser.setDirectory(*ed, ".");
    }
    if(!ed->fileBrowser.hasEntries())
    {
        ed->fileBrowser.loadDirectory(*ed, ed->fileBrowser.directory());
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
        ed->fileBrowser.restorePrevious(*ed);
        return NormalMode{};
    }

    // ========================================================================
    // Navigation
    // ========================================================================

    if(c == 'j' || c == Terminal::ARROW_DOWN)
    {
        ed->fileBrowser.down(ed->screenRows);
    }
    else if(c == 'k' || c == Terminal::ARROW_UP)
    {
        ed->fileBrowser.up(ed->screenRows);
    }
    else if(c == 'G')
    {
        ed->fileBrowser.end(ed->screenRows);
    }
    else if(c == 'g')
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
        {
            ed->fileBrowser.start();
        }
    }
    else if(c == Terminal::CTRL_D)
    {
        ed->fileBrowser.halfPageDown(ed->screenRows);
    }
    else if(c == Terminal::CTRL_U)
    {
        ed->fileBrowser.halfPageUp(ed->screenRows);
    }

    // ========================================================================
    // Selection / Enter Directory
    // ========================================================================

    else if(c == Terminal::ENTER || c == 'l' || c == Terminal::ARROW_RIGHT)
    {
        if(ed->fileBrowser.selectEntry(*ed))
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
        ed->fileBrowser.parent(*ed);
    }

    // ========================================================================
    // File Operations
    // ========================================================================

    // Toggle hidden files
    else if(c == '.')
    {
        ed->fileBrowser.toggleHidden(*ed);
    }
    else if(c == 'i' || c == Terminal::CTRL_I)
    {
        ed->fileBrowser.toggleGitignore(*ed);
    }

    // Refresh
    else if(c == 'r' || c == Terminal::CTRL_L)
    {
        ed->fileBrowser.refresh(*ed);
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
