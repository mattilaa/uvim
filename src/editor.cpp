#include "editor.h"
#include "terminal.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client_query.h"
#endif

// ============================================================================
// Constructor / Destructor
// ============================================================================

Editor::Editor()
    : bufferMgr(ctx), cursor(ctx), textOps(ctx), undoMgr(ctx), search(ctx),
      fileIO(ctx, bufferMgr), visualMode(ctx, textOps), formatter(ctx, undoMgr),
      syntax(ctx), renderer(ctx, syntax), opPending(ctx, textOps, cursor),
      lsp(ctx, bufferMgr, cursor)
{
    Terminal::enableRawMode();
    Terminal::getWindowSize(ctx.screenRows, ctx.screenCols);
    ctx.screenRows -= 2; // Reserve for status and message bars

    bufferMgr.createNewBuffer();
    Terminal::setCursorBlock();
}

Editor::~Editor()
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(ctx.lspClient)
    {
        ctx.lspClient->stop();
    }
#endif
    Terminal::restoreTerminal();
}

// ============================================================================
// Main Loop
// ============================================================================

void Editor::run()
{
    setStatusMessage("Welcome to uVim! Type :help for help.");

    while(true)
    {
        draw();
        handleKeypress();
    }
}

void Editor::draw()
{
    renderer.refreshScreen();

    // Draw completion popup if active
    if(ctx.completionActive)
    {
        std::string output;
        lsp.drawCompletionPopup(output);
        std::cout << output << std::flush;
        renderer.updateCursorPosition();
    }
}

// ============================================================================
// Public Interface
// ============================================================================

void Editor::openFile(const std::string& fname)
{
    fileIO.openFile(fname);
}

void Editor::enableClangdLsp(bool enable, const std::string& compileCommandsDir,
                             const std::string& clangdPath,
                             const std::string& queryDriverAllowList)
{
    if(enable)
    {
        lsp.enable(compileCommandsDir, clangdPath, queryDriverAllowList);
    }
}

// ============================================================================
// Mode Management
// ============================================================================

void Editor::setStatusMessage(const std::string& msg)
{
    ctx.statusMessage = msg;
}

void Editor::setMode(Mode mode)
{
    Mode previousMode = ctx.currentMode;
    ctx.currentMode = mode;

    if(mode == Mode::INSERT)
    {
        Terminal::setCursorBarBlinking();
    }
    else
    {
        Terminal::setCursorBlock();
    }

    if(mode == Mode::NORMAL)
    {
        ctx.pendingOperator = 0;
        ctx.pendingAwaitingObject = false;
        ctx.pendingObjectType = 0;
        ctx.pendingCount = 0;
        ctx.commandBuffer.clear();
        ctx.repeatCount = 0;

        if(previousMode == Mode::INSERT && *ctx.cursorX > 0)
        {
            if(*ctx.cursorY < (int)ctx.lines->size())
            {
                int lineLen = (*ctx.lines)[*ctx.cursorY].length();
                if(*ctx.cursorX >= lineLen && lineLen > 0)
                {
                    *ctx.cursorX = lineLen - 1;
                }
            }
        }

        if(ctx.completionActive)
        {
            lsp.cancelCompletion();
        }
    }

    ctx.needsFullRedraw = true;
}

std::string Editor::getModeString() const
{
    switch(ctx.currentMode)
    {
    case Mode::NORMAL:
        return "NORMAL";
    case Mode::INSERT:
        return "INSERT";
    case Mode::VISUAL:
        return "VISUAL";
    case Mode::VISUAL_LINE:
        return "V-LINE";
    case Mode::VISUAL_BLOCK:
        return "V-BLOCK";
    case Mode::COMMAND:
        return "COMMAND";
    case Mode::SEARCH_FORWARD:
    case Mode::SEARCH_BACKWARD:
        return "SEARCH";
    case Mode::FILE_BROWSER:
        return "FILES";
    case Mode::FUZZY_FIND:
        return "FIND";
    case Mode::BUFFER_BROWSER:
        return "BUFFERS";
    case Mode::GREP_SEARCH:
        return "GREP";
    case Mode::OP_PENDING:
        return "OP-PEND";
    default:
        return "UNKNOWN";
    }
}

void Editor::forceQuit()
{
    Terminal::restoreTerminal();
    exit(0);
}

std::string Editor::trimString(const std::string& str)
{
    size_t start = str.find_first_not_of(" \t");
    if(start == std::string::npos)
        return "";
    size_t end = str.find_last_not_of(" \t");
    return str.substr(start, end - start + 1);
}

// ============================================================================
// Keypress Dispatch
// ============================================================================

void Editor::handleKeypress()
{
    int c = Terminal::readKey();

#ifdef UVIM_DEBUG_LOGGING
    {
        std::ofstream dbg("/tmp/uvim_debug.txt", std::ios::app);
        dbg << "key=" << c << " mode=" << static_cast<int>(ctx.currentMode)
            << std::endl;
    }
#endif

    switch(ctx.currentMode)
    {
    case Mode::NORMAL:
        handleNormalMode(c);
        break;
    case Mode::INSERT:
        handleInsertMode(c);
        break;
    case Mode::VISUAL:
    case Mode::VISUAL_LINE:
        handleVisualMode(c);
        break;
    case Mode::VISUAL_BLOCK:
        handleVisualBlockMode(c);
        break;
    case Mode::COMMAND:
        handleCommandMode(c);
        break;
    case Mode::SEARCH_FORWARD:
    case Mode::SEARCH_BACKWARD:
        handleSearchMode(c);
        break;
    case Mode::FILE_BROWSER:
        handleFileBrowserMode(c);
        break;
    case Mode::FUZZY_FIND:
        handleFuzzyFindMode(c);
        break;
    case Mode::BUFFER_BROWSER:
        handleBufferBrowserMode(c);
        break;
    case Mode::GREP_SEARCH:
        handleGrepSearchMode(c);
        break;
    case Mode::OP_PENDING:
        handleOperatorPendingMode(c);
        break;
    }
}

// ============================================================================
// Normal Mode
// ============================================================================

void Editor::handleLeaderKey(int c)
{
    switch(c)
    {
    case 'f': // Format with clang-format
        formatter.formatFile();
        break;
    case 'p': // Fuzzy find
        initializeFuzzyFind();
        break;
    case 'b': // Buffer browser
        initializeBufferBrowser();
        break;
    case 'g': // Grep search
        initializeGrepSearch();
        break;
    case 'e': // File browser
        openFileBrowser();
        break;
    case 'w': // Save file
        fileIO.saveFile();
        break;
    case 'q': // Close buffer
        bufferMgr.closeCurrentBuffer();
        break;
    case 'a': // Alternate file
        fileIO.jumpToAlternateFile();
        break;
    case 'y': // Yank to system clipboard
        textOps.yankLine();
        textOps.setSystemClipboard(ctx.yankBuffer);
        setStatusMessage("Yanked to system clipboard");
        break;
    case 'd': // Go to definition
        lsp.goToDefinition();
        break;
    case '/': // Clear search
        search.clearSearch();
        break;
    default:
        setStatusMessage("Unknown leader command");
        break;
    }
}

void Editor::handleGCommand(int c)
{
    switch(c)
    {
    case 'g': // gg - go to first line
        cursor.pushJumpLocation();
        cursor.moveToFirstLine();
        break;
    case 'd': // gd - go to definition
        lsp.goToDefinition();
        break;
    case 't': // gt - next buffer
        bufferMgr.nextBuffer();
        break;
    case 'T': // gT - previous buffer
        bufferMgr.previousBuffer();
        break;
    default:
        break;
    }
}

void Editor::handleZCommand(int c)
{
    switch(c)
    {
    case 'z': // zz - center screen
        cursor.centerScreen();
        break;
    case 't': // zt - scroll cursor to top
        *ctx.offsetY = *ctx.cursorY;
        ctx.needsFullRedraw = true;
        break;
    case 'b': // zb - scroll cursor to bottom
        *ctx.offsetY = *ctx.cursorY - ctx.screenRows + 1;
        if(*ctx.offsetY < 0)
            *ctx.offsetY = 0;
        ctx.needsFullRedraw = true;
        break;
    default:
        break;
    }
}

void Editor::handleNormalMode(int c)
{
    // Count prefix
    if(c >= '1' && c <= '9')
    {
        ctx.repeatCount = ctx.repeatCount * 10 + (c - '0');
        return;
    }
    if(c == '0' && ctx.repeatCount > 0)
    {
        ctx.repeatCount = ctx.repeatCount * 10;
        return;
    }

    int count = std::max(1, ctx.repeatCount);
    ctx.repeatCount = 0;

    // Leader key (space)
    if(c == ' ')
    {
        int nextKey = Terminal::readKey();
        handleLeaderKey(nextKey);
        return;
    }

    // Multi-key commands
    if(!ctx.commandBuffer.empty())
    {
        char first = ctx.commandBuffer[0];
        ctx.commandBuffer.clear();

        if(first == 'g')
        {
            handleGCommand(c);
            return;
        }
        if(first == 'z')
        {
            handleZCommand(c);
            return;
        }
        if(first == 'f')
        {
            for(int i = 0; i < count; i++)
                cursor.findCharForward((char)c);
            return;
        }
        if(first == 'F')
        {
            for(int i = 0; i < count; i++)
                cursor.findCharBackward((char)c);
            return;
        }
        if(first == 't')
        {
            for(int i = 0; i < count; i++)
                cursor.findCharTillForward((char)c);
            return;
        }
        if(first == 'T')
        {
            for(int i = 0; i < count; i++)
                cursor.findCharTillBackward((char)c);
            return;
        }
        if(first == 'r')
        { // Replace char
            if(*ctx.cursorY < (int)ctx.lines->size() &&
               *ctx.cursorX < (int)(*ctx.lines)[*ctx.cursorY].length())
            {
                undoMgr.saveState();
                (*ctx.lines)[*ctx.cursorY][*ctx.cursorX] = (char)c;
                *ctx.dirty = true;
            }
            return;
        }
        return;
    }

    switch(c)
    {
    // Movement
    case 'h':
    case Terminal::ARROW_LEFT:
        cursor.moveLeft(count);
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        cursor.moveDown(count);
        break;
    case 'k':
    case Terminal::ARROW_UP:
        cursor.moveUp(count);
        break;
    case 'l':
    case Terminal::ARROW_RIGHT:
        cursor.moveRight(count);
        break;
    case 'w':
        for(int i = 0; i < count; i++)
            cursor.moveWordForward();
        break;
    case 'b':
        for(int i = 0; i < count; i++)
            cursor.moveWordBackward();
        break;
    case 'e':
        for(int i = 0; i < count; i++)
            cursor.moveToEndOfWord();
        break;
    case '0':
        cursor.moveToLineStart();
        break;
    case '$':
        cursor.moveToLineEnd();
        break;
    case '^':
        cursor.moveToFirstNonBlank();
        break;
    case '%':
        cursor.moveToMatchingBracket();
        break;
    case 'G':
        cursor.pushJumpLocation();
        if(count > 1)
            cursor.moveToLine(count - 1);
        else
            cursor.moveToLastLine();
        break;

    // Scrolling
    case Terminal::CTRL_D:
        cursor.scrollHalfPageDown();
        break;
    case Terminal::CTRL_U:
        cursor.scrollHalfPageUp();
        break;
    case Terminal::CTRL_F:
        for(int i = 0; i < ctx.screenRows - 2; i++)
            cursor.moveDown();
        break;
        /*
    case Terminal::CTRL_B:
        for(int i = 0; i < ctx.screenRows - 2; i++)
            cursor.moveUp();
        break;
        */
    // Insert mode entry
    case 'i':
        setMode(Mode::INSERT);
        break;
    case 'I':
        cursor.moveToFirstNonBlank();
        setMode(Mode::INSERT);
        break;
    case 'a':
        if(*ctx.cursorY < (int)ctx.lines->size() &&
           *ctx.cursorX < (int)(*ctx.lines)[*ctx.cursorY].length())
            (*ctx.cursorX)++;
        setMode(Mode::INSERT);
        break;
    case 'A':
        if(*ctx.cursorY < (int)ctx.lines->size())
            *ctx.cursorX = (*ctx.lines)[*ctx.cursorY].length();
        setMode(Mode::INSERT);
        break;
    case 'o':
        undoMgr.saveState();
        ctx.lines->insert(ctx.lines->begin() + *ctx.cursorY + 1, "");
        (*ctx.cursorY)++;
        *ctx.cursorX = 0;
        *ctx.dirty = true;
        ctx.needsFullRedraw = true;
        setMode(Mode::INSERT);
        break;
    case 'O':
        undoMgr.saveState();
        ctx.lines->insert(ctx.lines->begin() + *ctx.cursorY, "");
        *ctx.cursorX = 0;
        *ctx.dirty = true;
        ctx.needsFullRedraw = true;
        setMode(Mode::INSERT);
        break;

    // Editing
    case 'x':
        undoMgr.saveState();
        for(int i = 0; i < count; i++)
            textOps.deleteCharForward();
        break;
    case 'X':
        undoMgr.saveState();
        for(int i = 0; i < count; i++)
            textOps.deleteChar();
        break;
    case 's':
        undoMgr.saveState();
        textOps.deleteCharForward();
        setMode(Mode::INSERT);
        break;
    case 'S':
        undoMgr.saveState();
        (*ctx.lines)[*ctx.cursorY].clear();
        *ctx.cursorX = 0;
        *ctx.dirty = true;
        setMode(Mode::INSERT);
        break;
    case 'D':
        undoMgr.saveState();
        textOps.deleteToLineEnd();
        break;
    case 'C':
        undoMgr.saveState();
        textOps.deleteToLineEnd();
        setMode(Mode::INSERT);
        break;
    case 'J':
        undoMgr.saveState();
        textOps.joinLines(count);
        break;
    case '~':
        undoMgr.saveState();
        for(int i = 0; i < count; i++)
        {
            textOps.toggleCase();
            cursor.moveRight();
        }
        break;

    // Yank/Paste
    case 'y':
        ctx.pendingCount = count;
        opPending.enter('y');
        break;
    case 'Y':
        textOps.yankLine();
        break;
    case 'p':
        undoMgr.saveState();
        textOps.pasteAfter();
        break;
    case 'P':
        undoMgr.saveState();
        textOps.pasteBefore();
        break;

    // Delete/Change operators
    case 'd':
        ctx.pendingCount = count;
        opPending.enter('d');
        break;
    case 'c':
        ctx.pendingCount = count;
        opPending.enter('c');
        break;

    // Indent operator
    case '=':
        ctx.pendingCount = count;
        opPending.enter('=');
        break;
    case '>':
        undoMgr.saveState();
        for(int i = 0; i < count; i++)
            textOps.shiftLineRight(*ctx.cursorY);
        ctx.needsFullRedraw = true;
        break;
    case '<':
        undoMgr.saveState();
        for(int i = 0; i < count; i++)
            textOps.shiftLineLeft(*ctx.cursorY);
        ctx.needsFullRedraw = true;
        break;

    // Visual mode
    case 'v':
        visualMode.startVisualMode();
        break;
    case 'V':
        visualMode.startVisualLineMode();
        break;
    case Terminal::CTRL_V:
        visualMode.startVisualBlockMode();
        break;

    // Search
    case '/':
        search.startSearchForward();
        break;
    case '?':
        search.startSearchBackward();
        break;
    case 'n':
        search.searchNext();
        break;
    case 'N':
        search.searchPrevious();
        break;
    case '*': // Search word under cursor forward
    {
        std::string word = fileIO.getSymbolUnderCursor();
        if(!word.empty())
        {
            ctx.searchQuery = word;
            ctx.searchForward = true;
            search.findAllMatches();
            search.searchNext();
        }
    }
    break;
    case '#': // Search word under cursor backward
    {
        std::string word = fileIO.getSymbolUnderCursor();
        if(!word.empty())
        {
            ctx.searchQuery = word;
            ctx.searchForward = false;
            search.findAllMatches();
            search.searchPrevious();
        }
    }
    break;

    // Undo/Redo
    case 'u':
        undoMgr.undo();
        break;
    case Terminal::CTRL_R:
        undoMgr.redo();
        break;

    // Jump list
    case Terminal::CTRL_O:
        cursor.jumpBack();
        break;
    case Terminal::CTRL_I:
        cursor.jumpForward();
        break;

    // Command mode
    case ':':
        ctx.currentMode = Mode::COMMAND;
        ctx.commandBuffer.clear();
        break;

    // Multi-key command starters
    case 'g':
    case 'z':
    case 'f':
    case 'F':
    case 't':
    case 'T':
    case 'r':
        ctx.commandBuffer = (char)c;
        break;

    // Repeat find char
    case ';':
        cursor.repeatFindChar();
        break;
    case ',':
        cursor.repeatFindCharReverse();
        break;

    // Marks (simplified - just jump back)
    case '\'':
    case '`':
        cursor.jumpBack();
        break;

    // ESC - clear state
    case 27:
        ctx.commandBuffer.clear();
        ctx.repeatCount = 0;
        search.clearSearch();
        break;

    default:
        break;
    }

    cursor.adjustViewport();
}

// ============================================================================
// Insert Mode
// ============================================================================

void Editor::handleInsertMode(int c)
{
    switch(c)
    {
    case 27: // ESC
        setMode(Mode::NORMAL);
        undoMgr.saveState();
        break;

    case Terminal::ARROW_LEFT:
        cursor.moveLeft();
        break;
    case Terminal::ARROW_RIGHT:
        cursor.moveRight();
        break;
    case Terminal::ARROW_UP:
        cursor.moveUp();
        break;
    case Terminal::ARROW_DOWN:
        cursor.moveDown();
        break;

    case Terminal::HOME:
        cursor.moveToLineStart();
        break;
    case Terminal::END:
        if(*ctx.cursorY < (int)ctx.lines->size())
            *ctx.cursorX = (*ctx.lines)[*ctx.cursorY].length();
        break;

    case 127: // Backspace
    case Terminal::BACKSPACE:
        textOps.deleteChar();
        break;

    case Terminal::DELETE:
        textOps.deleteCharForward();
        break;

    case '\r': // Enter
    case '\n':
        textOps.insertNewline();
        break;

    case '\t': // Tab
        textOps.insertTab();
        break;

    case Terminal::CTRL_W: // Delete word backward
        while(*ctx.cursorX > 0 &&
              (*ctx.lines)[*ctx.cursorY][*ctx.cursorX - 1] == ' ')
            textOps.deleteChar();
        while(*ctx.cursorX > 0 &&
              (*ctx.lines)[*ctx.cursorY][*ctx.cursorX - 1] != ' ')
            textOps.deleteChar();
        break;

    case Terminal::CTRL_U: // Delete to line start
        while(*ctx.cursorX > 0)
            textOps.deleteChar();
        break;

    case Terminal::CTRL_N: // Completion next
        if(ctx.completionActive)
            lsp.completionNext();
        else
            lsp.requestCompletion();
        break;

    case Terminal::CTRL_P: // Completion prev
        if(ctx.completionActive)
            lsp.completionPrev();
        else
            lsp.requestCompletion();
        break;

    default:
        if(c >= 32 && c < 127) // Printable
        {
            textOps.insertChar((char)c);

            // Cancel completion if typing non-word char
            if(ctx.completionActive && !ctx.isWordChar((char)c))
            {
                lsp.cancelCompletion();
            }
            else if(ctx.completionActive)
            {
                // Update completion filter
                const std::string& line = (*ctx.lines)[*ctx.cursorY];
                int prefixStart = ctx.completionStartX;
                ctx.completionPrefix =
                    line.substr(prefixStart, *ctx.cursorX - prefixStart);
                lsp.rebuildCompletionFilter();
            }
        }
        break;
    }

    cursor.adjustViewport();
}

// ============================================================================
// Visual Mode
// ============================================================================

void Editor::handleVisualMode(int c)
{
    // Leader key
    if(c == ' ')
    {
        int nextKey = Terminal::readKey();
        if(nextKey == 'y')
        {
            textOps.yankSelection();
            textOps.setSystemClipboard(ctx.yankBuffer);
            setStatusMessage("Yanked to system clipboard");
            setMode(Mode::NORMAL);
            return;
        }
        if(nextKey == 'd')
        {
            undoMgr.saveState();
            textOps.deleteSelection();
            setMode(Mode::NORMAL);
            return;
        }
        if(nextKey == 'f')
        {
            setMode(Mode::NORMAL);
            formatter.formatFile();
            return;
        }
        return;
    }

    switch(c)
    {
    case 27: // ESC
        setMode(Mode::NORMAL);
        break;

    // Movement
    case 'h':
    case Terminal::ARROW_LEFT:
        cursor.moveLeft();
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        cursor.moveDown();
        break;
    case 'k':
    case Terminal::ARROW_UP:
        cursor.moveUp();
        break;
    case 'l':
    case Terminal::ARROW_RIGHT:
        cursor.moveRight();
        break;
    case 'w':
        cursor.moveWordForward();
        break;
    case 'b':
        cursor.moveWordBackward();
        break;
    case 'e':
        cursor.moveToEndOfWord();
        break;
    case '0':
        cursor.moveToLineStart();
        break;
    case '$':
        cursor.moveToLineEnd();
        break;
    case '^':
        cursor.moveToFirstNonBlank();
        break;
    case 'G':
        cursor.moveToLastLine();
        break;
    case 'g':
    {
        int next = Terminal::readKey();
        if(next == 'g')
            cursor.moveToFirstLine();
    }
    break;

    // Operations
    case 'y':
        textOps.yankSelection();
        setMode(Mode::NORMAL);
        break;
    case 'd':
    case 'x':
        undoMgr.saveState();
        textOps.deleteSelection();
        setMode(Mode::NORMAL);
        break;
    case 'c':
        undoMgr.saveState();
        textOps.deleteSelection();
        setMode(Mode::INSERT);
        break;
    case '>':
        undoMgr.saveState();
        {
            int startY, startX, endY, endX;
            visualMode.getSelectionBounds(startY, startX, endY, endX);
            for(int y = startY; y <= endY; y++)
                textOps.shiftLineRight(y);
        }
        setMode(Mode::NORMAL);
        ctx.needsFullRedraw = true;
        break;
    case '<':
        undoMgr.saveState();
        {
            int startY, startX, endY, endX;
            visualMode.getSelectionBounds(startY, startX, endY, endX);
            for(int y = startY; y <= endY; y++)
                textOps.shiftLineLeft(y);
        }
        setMode(Mode::NORMAL);
        ctx.needsFullRedraw = true;
        break;
    case '=':
        undoMgr.saveState();
        {
            int startY, startX, endY, endX;
            visualMode.getSelectionBounds(startY, startX, endY, endX);
            textOps.autoIndentRange(startY, endY);
        }
        setMode(Mode::NORMAL);
        break;

    // Switch visual mode type
    case 'v':
        if(ctx.currentMode == Mode::VISUAL)
            setMode(Mode::NORMAL);
        else
            visualMode.startVisualMode();
        break;
    case 'V':
        if(ctx.currentMode == Mode::VISUAL_LINE)
            setMode(Mode::NORMAL);
        else
            visualMode.startVisualLineMode();
        break;
    case Terminal::CTRL_V:
        visualMode.startVisualBlockMode();
        break;

    default:
        break;
    }

    visualMode.updateVisualSelection();
    cursor.adjustViewport();
    ctx.needsFullRedraw = true;
}

// ============================================================================
// Visual Block Mode
// ============================================================================

void Editor::handleVisualBlockMode(int c)
{
    if(c == ' ')
    {
        int nextKey = Terminal::readKey();
        if(nextKey == 'f')
        {
            setMode(Mode::NORMAL);
            formatter.formatFile();
            return;
        }
        return;
    }

    switch(c)
    {
    case 27: // ESC
        setMode(Mode::NORMAL);
        break;

    // Movement
    case 'h':
    case Terminal::ARROW_LEFT:
        cursor.moveLeft();
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        cursor.moveDown();
        break;
    case 'k':
    case Terminal::ARROW_UP:
        cursor.moveUp();
        break;
    case 'l':
    case Terminal::ARROW_RIGHT:
        cursor.moveRight();
        break;
    case '0':
        cursor.moveToLineStart();
        break;
    case '$':
        cursor.moveToLineEnd();
        break;

    // Operations
    case 'y':
        visualMode.yankVisualBlock();
        setMode(Mode::NORMAL);
        break;
    case 'd':
    case 'x':
        undoMgr.saveState();
        visualMode.deleteVisualBlock();
        setMode(Mode::NORMAL);
        break;
    case 'c':
        undoMgr.saveState();
        visualMode.changeVisualBlock();
        break;
    case 'I': // Insert at block start
        ctx.visualBlockInsertStartX = std::min(ctx.currentBuffer->visualStartX,
                                               ctx.currentBuffer->visualEndX);
        *ctx.cursorX = ctx.visualBlockInsertStartX;
        setMode(Mode::INSERT);
        break;

    // Switch modes
    case 'v':
        visualMode.startVisualMode();
        break;
    case 'V':
        visualMode.startVisualLineMode();
        break;
    case Terminal::CTRL_V:
        setMode(Mode::NORMAL);
        break;

    default:
        break;
    }

    visualMode.updateVisualBlockSelection();
    cursor.adjustViewport();
    ctx.needsFullRedraw = true;
}

// ============================================================================
// Operator Pending Mode
// ============================================================================

void Editor::handleOperatorPendingMode(int c)
{
    opPending.handleKey(c);
}

// ============================================================================
// Command Mode
// ============================================================================

void Editor::handleCommandMode(int c)
{
    switch(c)
    {
    case 27: // ESC
        ctx.commandBuffer.clear();
        setMode(Mode::NORMAL);
        break;

    case '\r': // Enter
    case '\n':
        executeCommand(ctx.commandBuffer);
        ctx.commandBuffer.clear();
        setMode(Mode::NORMAL);
        break;

    case 127: // Backspace
    case Terminal::BACKSPACE:
        if(!ctx.commandBuffer.empty())
            ctx.commandBuffer.pop_back();
        else
            setMode(Mode::NORMAL);
        break;

    case Terminal::CTRL_U:
        ctx.commandBuffer.clear();
        break;

    case '\t': // Tab completion
        // TODO: implement file path completion
        break;

    default:
        if(c >= 32 && c < 127)
            ctx.commandBuffer += (char)c;
        break;
    }
}

void Editor::executeCommand(const std::string& cmd)
{
    std::string trimmed = trimString(cmd);
    if(trimmed.empty())
        return;

    // Parse command and arguments
    std::istringstream iss(trimmed);
    std::string command;
    iss >> command;

    // Write commands
    if(command == "w" || command == "write")
    {
        std::string filename;
        iss >> filename;
        if(!filename.empty())
            *ctx.filename = filename;
        fileIO.saveFile();
    }
    else if(command == "wq" || command == "x")
    {
        fileIO.saveFile();
        if(!*ctx.dirty)
            forceQuit();
    }
    else if(command == "q" || command == "quit")
    {
        if(*ctx.dirty)
            setStatusMessage(
                "No write since last change (use :q! to override)");
        else if(ctx.buffers.size() > 1)
            bufferMgr.closeCurrentBuffer();
        else
            forceQuit();
    }
    else if(command == "q!")
    {
        if(ctx.buffers.size() > 1)
            bufferMgr.closeCurrentBuffer();
        else
            forceQuit();
    }
    else if(command == "qa!")
    {
        forceQuit();
    }
    else if(command == "e" || command == "edit")
    {
        std::string filename;
        iss >> filename;
        if(!filename.empty())
            fileIO.openFile(filename);
        else
            setStatusMessage("No filename");
    }
    else if(command == "enew")
    {
        bufferMgr.createNewBuffer();
    }
    else if(command == "bn" || command == "bnext")
    {
        bufferMgr.nextBuffer();
    }
    else if(command == "bp" || command == "bprev")
    {
        bufferMgr.previousBuffer();
    }
    else if(command == "bd" || command == "bdelete")
    {
        bufferMgr.closeCurrentBuffer();
    }
    else if(command == "ls" || command == "buffers")
    {
        bufferMgr.listBuffers();
    }
    else if(command == "b")
    {
        int num;
        if(iss >> num)
            bufferMgr.switchToBuffer(num - 1);
    }
    else if(command == "pwd")
    {
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
            setStatusMessage(cwd);
    }
    else if(command == "cd")
    {
        std::string dir;
        iss >> dir;
        if(!dir.empty())
        {
            if(chdir(dir.c_str()) == 0)
                setStatusMessage("Changed to: " + dir);
            else
                setStatusMessage("Failed to change directory");
        }
    }
    else if(command == "set")
    {
        std::string option;
        iss >> option;
        if(option == "number" || option == "nu")
            setStatusMessage("Line numbers enabled");
        else if(option == "nonumber" || option == "nonu")
            setStatusMessage("Line numbers disabled");
        else
            setStatusMessage("Unknown option: " + option);
    }
    else if(command == "noh" || command == "nohlsearch")
    {
        search.clearSearch();
    }
    else if(command == "help")
    {
        setStatusMessage(
            "uVim - :w save, :q quit, :e file, Space+p fuzzy, Space+g grep");
    }
    else if(std::isdigit(command[0]))
    {
        // Line number
        int line = std::stoi(command);
        cursor.pushJumpLocation();
        cursor.moveToLine(line - 1);
    }
    else
    {
        setStatusMessage("Unknown command: " + command);
    }
}

// ============================================================================
// Search Mode
// ============================================================================

void Editor::handleSearchMode(int c)
{
    switch(c)
    {
    case 27: // ESC
        search.cancelSearch();
        break;

    case '\r':
    case '\n':
        search.performSearch();
        break;

    case 127:
    case Terminal::BACKSPACE:
        if(!ctx.searchQuery.empty())
            ctx.searchQuery.pop_back();
        else
            search.cancelSearch();
        break;

    case Terminal::CTRL_U:
        ctx.searchQuery.clear();
        break;

    default:
        if(c >= 32 && c < 127)
            ctx.searchQuery += (char)c;
        break;
    }
}

// ============================================================================
// File Browser
// ============================================================================

void Editor::openFileBrowser(const std::string& path)
{
    char cwd[PATH_MAX];
    if(path.empty())
    {
        if(getcwd(cwd, sizeof(cwd)))
            ctx.currentDirectory = cwd;
        else
            ctx.currentDirectory = ".";
    }
    else
    {
        ctx.currentDirectory = path;
    }

    loadDirectory(ctx.currentDirectory);
    ctx.currentMode = Mode::FILE_BROWSER;
    ctx.fileBrowserIndex = 0;
    ctx.fileBrowserOffset = 0;
    ctx.needsFullRedraw = true;
}

void Editor::loadDirectory(const std::string& path)
{
    ctx.fileList.clear();

    DIR* dir = opendir(path.c_str());
    if(!dir)
        return;

    struct dirent* entry;
    while((entry = readdir(dir)) != nullptr)
    {
        std::string name = entry->d_name;
        if(name == ".")
            continue;
        if(!ctx.showHiddenFiles && name[0] == '.' && name != "..")
            continue;

        FileEntry fe;
        fe.name = name;

        std::string fullPath = path + "/" + name;
        struct stat st;
        if(stat(fullPath.c_str(), &st) == 0)
        {
            fe.isDirectory = S_ISDIR(st.st_mode);
            fe.size = st.st_size;
            fe.modTime = st.st_mtime;
        }
        else
        {
            fe.isDirectory = (entry->d_type == DT_DIR);
            fe.size = 0;
            fe.modTime = 0;
        }

        ctx.fileList.push_back(fe);
    }
    closedir(dir);

    // Sort: directories first, then alphabetically
    std::sort(ctx.fileList.begin(), ctx.fileList.end(),
              [](const FileEntry& a, const FileEntry& b)
              {
                  if(a.name == "..")
                      return true;
                  if(b.name == "..")
                      return false;
                  if(a.isDirectory != b.isDirectory)
                      return a.isDirectory;
                  return a.name < b.name;
              });
}

std::string Editor::formatFileSize(size_t size)
{
    if(size < 1024)
        return std::to_string(size) + "B";
    if(size < 1024 * 1024)
        return std::to_string(size / 1024) + "K";
    return std::to_string(size / (1024 * 1024)) + "M";
}

void Editor::handleFileBrowserMode(int c)
{
    switch(c)
    {
    case 27:
    case 'q': // ESC or q
        setMode(Mode::NORMAL);
        ctx.needsFullRedraw = true;
        break;

    case 'j':
    case Terminal::ARROW_DOWN:
        if(ctx.fileBrowserIndex < (int)ctx.fileList.size() - 1)
        {
            ctx.fileBrowserIndex++;
            if(ctx.fileBrowserIndex >=
               ctx.fileBrowserOffset + ctx.screenRows - 2)
                ctx.fileBrowserOffset++;
        }
        ctx.needsFullRedraw = true;
        break;

    case 'k':
    case Terminal::ARROW_UP:
        if(ctx.fileBrowserIndex > 0)
        {
            ctx.fileBrowserIndex--;
            if(ctx.fileBrowserIndex < ctx.fileBrowserOffset)
                ctx.fileBrowserOffset--;
        }
        ctx.needsFullRedraw = true;
        break;

    case '\r':
    case '\n':
    case 'l': // Enter or l
        if(ctx.fileBrowserIndex < (int)ctx.fileList.size())
        {
            const FileEntry& entry = ctx.fileList[ctx.fileBrowserIndex];
            std::string fullPath = ctx.currentDirectory + "/" + entry.name;

            if(entry.isDirectory)
            {
                char resolved[PATH_MAX];
                if(realpath(fullPath.c_str(), resolved))
                {
                    ctx.currentDirectory = resolved;
                    loadDirectory(ctx.currentDirectory);
                    ctx.fileBrowserIndex = 0;
                    ctx.fileBrowserOffset = 0;
                }
            }
            else
            {
                setMode(Mode::NORMAL);
                fileIO.openFile(fullPath);
            }
        }
        ctx.needsFullRedraw = true;
        break;

    case 'h': // Go up
    case '-':
    {
        std::string parent = ctx.currentDirectory + "/..";
        char resolved[PATH_MAX];
        if(realpath(parent.c_str(), resolved))
        {
            ctx.currentDirectory = resolved;
            loadDirectory(ctx.currentDirectory);
            ctx.fileBrowserIndex = 0;
            ctx.fileBrowserOffset = 0;
        }
    }
        ctx.needsFullRedraw = true;
        break;

    case '.': // Toggle hidden
        ctx.showHiddenFiles = !ctx.showHiddenFiles;
        loadDirectory(ctx.currentDirectory);
        ctx.fileBrowserIndex = 0;
        ctx.fileBrowserOffset = 0;
        ctx.needsFullRedraw = true;
        break;

    default:
        break;
    }
}

// ============================================================================
// Fuzzy Finder
// ============================================================================

void Editor::initializeFuzzyFind()
{
    ctx.fuzzyQuery.clear();
    ctx.projectFiles.clear();
    ctx.fuzzyMatches.clear();
    ctx.fuzzySelectedIndex = 0;
    ctx.fuzzyScrollOffset = 0;

    char cwd[PATH_MAX];
    std::string rootDir = ".";
    if(getcwd(cwd, sizeof(cwd)))
        rootDir = cwd;

    collectProjectFiles(rootDir, 0);
    updateFuzzyMatches();

    ctx.currentMode = Mode::FUZZY_FIND;
    ctx.needsFullRedraw = true;
}

void Editor::collectProjectFiles(const std::string& dir, int depth)
{
    if(depth > 10)
        return; // Limit recursion

    DIR* d = opendir(dir.c_str());
    if(!d)
        return;

    struct dirent* entry;
    while((entry = readdir(d)) != nullptr)
    {
        std::string name = entry->d_name;
        if(name[0] == '.')
            continue; // Skip hidden

        std::string fullPath = dir + "/" + name;
        struct stat st;
        if(stat(fullPath.c_str(), &st) != 0)
            continue;

        if(S_ISDIR(st.st_mode))
        {
            // Skip common non-project directories
            if(name == "node_modules" || name == "build" || name == ".git" ||
               name == "__pycache__" || name == "target" || name == "dist")
                continue;
            collectProjectFiles(fullPath, depth + 1);
        }
        else if(S_ISREG(st.st_mode))
        {
            // Make path relative
            char cwd[PATH_MAX];
            if(getcwd(cwd, sizeof(cwd)))
            {
                std::string cwdStr = cwd;
                if(fullPath.substr(0, cwdStr.length()) == cwdStr)
                    fullPath = fullPath.substr(cwdStr.length() + 1);
            }
            ctx.projectFiles.push_back(fullPath);
        }
    }
    closedir(d);
}

int Editor::fuzzyScore(const std::string& needle, const std::string& haystack,
                       std::vector<size_t>& matchPositions)
{
    matchPositions.clear();
    if(needle.empty())
        return 1;

    int score = 0;
    size_t needleIdx = 0;
    size_t lastMatch = 0;

    for(size_t i = 0; i < haystack.length() && needleIdx < needle.length(); i++)
    {
        char h = std::tolower(static_cast<unsigned char>(haystack[i]));
        char n = std::tolower(static_cast<unsigned char>(needle[needleIdx]));

        if(h == n)
        {
            matchPositions.push_back(i);
            score += 10;

            // Bonus for consecutive matches
            if(i == lastMatch + 1)
                score += 5;

            // Bonus for start of word
            if(i == 0 || haystack[i - 1] == '/' || haystack[i - 1] == '_' ||
               haystack[i - 1] == '-' || haystack[i - 1] == '.')
                score += 10;

            lastMatch = i;
            needleIdx++;
        }
    }

    if(needleIdx < needle.length())
        return 0; // Not all characters matched

    // Penalty for length difference
    score -= (haystack.length() - needle.length()) / 2;

    return std::max(1, score);
}

void Editor::updateFuzzyMatches()
{
    ctx.fuzzyMatches.clear();

    for(const auto& file : ctx.projectFiles)
    {
        std::vector<size_t> positions;
        int score = fuzzyScore(ctx.fuzzyQuery, file, positions);
        if(score > 0)
        {
            FuzzyMatch match;
            match.path = file;
            match.score = score;
            match.matchPositions = positions;
            ctx.fuzzyMatches.push_back(match);
        }
    }

    // Sort by score descending
    std::sort(ctx.fuzzyMatches.begin(), ctx.fuzzyMatches.end(),
              [](const FuzzyMatch& a, const FuzzyMatch& b)
              { return a.score > b.score; });

    ctx.fuzzySelectedIndex = 0;
    ctx.fuzzyScrollOffset = 0;
}

void Editor::selectFuzzyMatch()
{
    if(ctx.fuzzySelectedIndex < (int)ctx.fuzzyMatches.size())
    {
        std::string path = ctx.fuzzyMatches[ctx.fuzzySelectedIndex].path;
        setMode(Mode::NORMAL);
        fileIO.openFile(path);
    }
}

void Editor::handleFuzzyFindMode(int c)
{
    switch(c)
    {
    case 27: // ESC
        setMode(Mode::NORMAL);
        ctx.needsFullRedraw = true;
        break;

    case '\r':
    case '\n':
        selectFuzzyMatch();
        break;

    case Terminal::CTRL_N:
    case Terminal::ARROW_DOWN:
        if(ctx.fuzzySelectedIndex < (int)ctx.fuzzyMatches.size() - 1)
        {
            ctx.fuzzySelectedIndex++;
            if(ctx.fuzzySelectedIndex >=
               ctx.fuzzyScrollOffset + ctx.screenRows - 2)
                ctx.fuzzyScrollOffset++;
        }
        ctx.needsFullRedraw = true;
        break;

    case Terminal::CTRL_P:
    case Terminal::ARROW_UP:
        if(ctx.fuzzySelectedIndex > 0)
        {
            ctx.fuzzySelectedIndex--;
            if(ctx.fuzzySelectedIndex < ctx.fuzzyScrollOffset)
                ctx.fuzzyScrollOffset--;
        }
        ctx.needsFullRedraw = true;
        break;

    case 127:
    case Terminal::BACKSPACE:
        if(!ctx.fuzzyQuery.empty())
        {
            ctx.fuzzyQuery.pop_back();
            updateFuzzyMatches();
        }
        ctx.needsFullRedraw = true;
        break;

    case Terminal::CTRL_U:
        ctx.fuzzyQuery.clear();
        updateFuzzyMatches();
        ctx.needsFullRedraw = true;
        break;

    default:
        if(c >= 32 && c < 127)
        {
            ctx.fuzzyQuery += (char)c;
            updateFuzzyMatches();
        }
        ctx.needsFullRedraw = true;
        break;
    }
}

// ============================================================================
// Buffer Browser
// ============================================================================

void Editor::initializeBufferBrowser()
{
    ctx.bufferQuery.clear();
    ctx.bufferSelectedIndex = 0;
    updateBufferMatches();
    ctx.currentMode = Mode::BUFFER_BROWSER;
    ctx.needsFullRedraw = true;
}

void Editor::updateBufferMatches()
{
    ctx.bufferMatches.clear();

    for(size_t i = 0; i < ctx.buffers.size(); i++)
    {
        std::string name = ctx.buffers[i]->filename;
        if(name.empty())
            name = "[No Name]";
        if(ctx.buffers[i]->dirty)
            name += " [+]";

        if(ctx.bufferQuery.empty())
        {
            FuzzyMatch match;
            match.path = name;
            match.score = 1;
            ctx.bufferMatches.push_back(match);
        }
        else
        {
            std::vector<size_t> positions;
            int score = fuzzyScore(ctx.bufferQuery, name, positions);
            if(score > 0)
            {
                FuzzyMatch match;
                match.path = name;
                match.score = score;
                match.matchPositions = positions;
                ctx.bufferMatches.push_back(match);
            }
        }
    }
}

void Editor::selectBufferMatch()
{
    if(ctx.bufferSelectedIndex < (int)ctx.bufferMatches.size())
    {
        // Find the actual buffer index
        std::string selected = ctx.bufferMatches[ctx.bufferSelectedIndex].path;
        for(size_t i = 0; i < ctx.buffers.size(); i++)
        {
            std::string name = ctx.buffers[i]->filename;
            if(name.empty())
                name = "[No Name]";
            if(ctx.buffers[i]->dirty)
                name += " [+]";
            if(name == selected)
            {
                setMode(Mode::NORMAL);
                bufferMgr.switchToBuffer(i);
                return;
            }
        }
    }
}

void Editor::handleBufferBrowserMode(int c)
{
    switch(c)
    {
    case 27: // ESC
        setMode(Mode::NORMAL);
        ctx.needsFullRedraw = true;
        break;

    case '\r':
    case '\n':
        selectBufferMatch();
        break;

    case Terminal::CTRL_N:
    case Terminal::ARROW_DOWN:
        if(ctx.bufferSelectedIndex < (int)ctx.bufferMatches.size() - 1)
            ctx.bufferSelectedIndex++;
        ctx.needsFullRedraw = true;
        break;

    case Terminal::CTRL_P:
    case Terminal::ARROW_UP:
        if(ctx.bufferSelectedIndex > 0)
            ctx.bufferSelectedIndex--;
        ctx.needsFullRedraw = true;
        break;

    case 127:
    case Terminal::BACKSPACE:
        if(!ctx.bufferQuery.empty())
        {
            ctx.bufferQuery.pop_back();
            updateBufferMatches();
        }
        ctx.needsFullRedraw = true;
        break;

    default:
        if(c >= 32 && c < 127)
        {
            ctx.bufferQuery += (char)c;
            updateBufferMatches();
        }
        ctx.needsFullRedraw = true;
        break;
    }
}

// ============================================================================
// Grep Search
// ============================================================================

void Editor::initializeGrepSearch()
{
    ctx.grepQuery.clear();
    ctx.grepMatches.clear();
    ctx.grepSelectedIndex = 0;
    ctx.grepScrollOffset = 0;
    ctx.grepSearching = false;
    ctx.currentMode = Mode::GREP_SEARCH;
    ctx.needsFullRedraw = true;
}

bool Editor::isTextFile(const std::string& filepath)
{
    // Check extension
    size_t dot = filepath.find_last_of('.');
    if(dot != std::string::npos)
    {
        std::string ext = filepath.substr(dot);
        static const std::vector<std::string> textExts = {
            ".txt", ".md",   ".c",    ".cpp", ".h",    ".hpp",  ".py",   ".js",
            ".ts",  ".java", ".rs",   ".go",  ".rb",   ".sh",   ".bash", ".zsh",
            ".vim", ".lua",  ".json", ".xml", ".yaml", ".yml",  ".toml", ".ini",
            ".css", ".scss", ".html", ".htm", ".mla",  ".cmake"};
        for(const auto& e : textExts)
            if(ext == e)
                return true;
    }
    return !isBinaryFile(filepath);
}

bool Editor::isBinaryFile(const std::string& filepath)
{
    std::ifstream file(filepath, std::ios::binary);
    if(!file)
        return true;

    char buffer[512];
    file.read(buffer, sizeof(buffer));
    std::streamsize bytesRead = file.gcount();

    for(std::streamsize i = 0; i < bytesRead; i++)
    {
        unsigned char c = buffer[i];
        if(c == 0)
            return true; // Null byte = binary
    }
    return false;
}

void Editor::searchFileContent(const std::string& filepath)
{
    std::ifstream file(filepath);
    if(!file)
        return;

    std::string line;
    int lineNum = 0;
    std::string lowerQuery = ctx.grepQuery;
    for(char& c : lowerQuery)
        c = std::tolower(static_cast<unsigned char>(c));

    while(std::getline(file, line) && ctx.grepMatches.size() < 1000)
    {
        lineNum++;
        std::string lowerLine = line;
        for(char& c : lowerLine)
            c = std::tolower(static_cast<unsigned char>(c));

        size_t pos = lowerLine.find(lowerQuery);
        if(pos != std::string::npos)
        {
            GrepMatch match;
            match.filepath = filepath;
            match.lineNumber = lineNum;
            match.lineContent = line;
            if(match.lineContent.length() > 200)
                match.lineContent = match.lineContent.substr(0, 200);
            match.matchStart = pos;
            match.matchEnd = pos + ctx.grepQuery.length() - 1;
            ctx.grepMatches.push_back(match);
        }
    }
}

void Editor::performGrepSearch()
{
    if(ctx.grepQuery.empty())
        return;

    ctx.grepMatches.clear();
    ctx.grepSearching = true;
    ctx.needsFullRedraw = true;

    char cwd[PATH_MAX];
    std::string rootDir = ".";
    if(getcwd(cwd, sizeof(cwd)))
        rootDir = cwd;

    // Collect and search files
    std::vector<std::string> files;
    collectProjectFiles(rootDir, 0);

    for(const auto& file : ctx.projectFiles)
    {
        if(isTextFile(file))
            searchFileContent(file);
    }

    ctx.grepSearching = false;
    ctx.grepSelectedIndex = 0;
    ctx.grepScrollOffset = 0;
    ctx.needsFullRedraw = true;
}

void Editor::selectGrepMatch()
{
    if(ctx.grepSelectedIndex < (int)ctx.grepMatches.size())
    {
        const GrepMatch& match = ctx.grepMatches[ctx.grepSelectedIndex];
        setMode(Mode::NORMAL);
        fileIO.openFile(match.filepath);
        cursor.moveToLine(match.lineNumber - 1);
        *ctx.cursorX = match.matchStart;
        cursor.adjustViewport();
    }
}

void Editor::handleGrepSearchMode(int c)
{
    switch(c)
    {
    case 27: // ESC
        setMode(Mode::NORMAL);
        ctx.needsFullRedraw = true;
        break;

    case '\r':
    case '\n':
        if(ctx.grepMatches.empty())
            performGrepSearch();
        else
            selectGrepMatch();
        break;

    case Terminal::CTRL_N:
    case Terminal::ARROW_DOWN:
        if(ctx.grepSelectedIndex < (int)ctx.grepMatches.size() - 1)
        {
            ctx.grepSelectedIndex++;
            if(ctx.grepSelectedIndex >=
               ctx.grepScrollOffset + ctx.screenRows - 2)
                ctx.grepScrollOffset++;
        }
        ctx.needsFullRedraw = true;
        break;

    case Terminal::CTRL_P:
    case Terminal::ARROW_UP:
        if(ctx.grepSelectedIndex > 0)
        {
            ctx.grepSelectedIndex--;
            if(ctx.grepSelectedIndex < ctx.grepScrollOffset)
                ctx.grepScrollOffset--;
        }
        ctx.needsFullRedraw = true;
        break;

    case 127:
    case Terminal::BACKSPACE:
        if(!ctx.grepQuery.empty())
        {
            ctx.grepQuery.pop_back();
            ctx.grepMatches.clear();
        }
        ctx.needsFullRedraw = true;
        break;

    case Terminal::CTRL_U:
        ctx.grepQuery.clear();
        ctx.grepMatches.clear();
        ctx.needsFullRedraw = true;
        break;

    default:
        if(c >= 32 && c < 127)
        {
            ctx.grepQuery += (char)c;
            ctx.grepMatches.clear(); // Clear results, user needs to press Enter
        }
        ctx.needsFullRedraw = true;
        break;
    }
}
