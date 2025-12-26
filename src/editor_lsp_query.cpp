#include "editor_lsp_query.h"
#include "terminal.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client_query.h"
#endif
#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits.h>
#include <memory>
#include <pwd.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_set>

static bool isIdent(char c)
{
    return std::isalnum((unsigned char)c) || c == '_';
}

bool isLikelyDefinition(const std::string& line, const std::string& symbol)
{
    // skip comments
    size_t commentPos = line.find("//");
    std::string effectiveLine =
        (commentPos != std::string::npos) ? line.substr(0, commentPos) : line;

    // name(
    if(effectiveLine.find(symbol + "(") != std::string::npos)
        return true;

    // Class::name(
    if(effectiveLine.find("::" + symbol + "(") != std::string::npos)
        return true;

    return false;
}

// Check if a line is likely a variable/parameter declaration for the symbol
// Returns the column position of the symbol if found, -1 otherwise
static int findLocalDeclaration(const std::string& line,
                                const std::string& symbol)
{
    // Skip pure comment lines
    size_t firstNonSpace = line.find_first_not_of(" \t");
    if(firstNonSpace != std::string::npos &&
       line.substr(firstNonSpace, 2) == "//")
        return -1;

    // Get effective line (before any comment)
    size_t commentPos = line.find("//");
    std::string effectiveLine =
        (commentPos != std::string::npos) ? line.substr(0, commentPos) : line;

    // Find the symbol in the line
    size_t pos = 0;
    while((pos = effectiveLine.find(symbol, pos)) != std::string::npos)
    {
        // Make sure it's a whole word match
        bool validStart = (pos == 0 || !isIdent(effectiveLine[pos - 1]));
        bool validEnd = (pos + symbol.length() >= effectiveLine.length() ||
                         !isIdent(effectiveLine[pos + symbol.length()]));

        if(!validStart || !validEnd)
        {
            pos++;
            continue;
        }

        // Check what comes after the symbol
        size_t afterSymbol = pos + symbol.length();
        while(afterSymbol < effectiveLine.length() &&
              std::isspace((unsigned char)effectiveLine[afterSymbol]))
            afterSymbol++;

        // Check what comes before the symbol (skipping spaces and qualifiers)
        int beforeSymbol = pos - 1;
        while(beforeSymbol >= 0 &&
              std::isspace((unsigned char)effectiveLine[beforeSymbol]))
            beforeSymbol--;

        // Common patterns for variable declarations:
        // type name;
        // type name =
        // type name,
        // type name)  - for function parameters
        // type& name
        // type* name
        // const type name
        // auto name

        if(afterSymbol < effectiveLine.length())
        {
            char nextChar = effectiveLine[afterSymbol];
            // If followed by =, ;, ,, ), [ then it's likely a declaration
            // NOT if followed by ( which would be a function call
            if(nextChar == '=' || nextChar == ';' || nextChar == ',' ||
               nextChar == ')' || nextChar == '[')
            {
                // Check that there's something before (a type)
                if(beforeSymbol >= 0)
                {
                    char prevChar = effectiveLine[beforeSymbol];
                    // Common chars before a variable name in declaration:
                    // identifier char (end of type name), >, *, &, ]
                    if(isIdent(prevChar) || prevChar == '>' ||
                       prevChar == '*' || prevChar == '&' || prevChar == ']')
                    {
                        return (int)pos;
                    }
                }
            }
        }
        // End of line after symbol (like in "int x")
        else if(beforeSymbol >= 0)
        {
            char prevChar = effectiveLine[beforeSymbol];
            if(isIdent(prevChar) || prevChar == '>' || prevChar == '*' ||
               prevChar == '&' || prevChar == ']')
            {
                return (int)pos;
            }
        }

        pos++;
    }

    return -1;
}

// Search backwards from current position for local variable declaration
static bool searchLocalDefinition(const std::vector<std::string>& lines,
                                  const std::string& symbol, int startY,
                                  int startX, int& outY, int& outX)
{
    // Track brace depth to stay within current scope
    int braceDepth = 0;
    bool foundOpenBrace = false;

    // Start from the line before cursor (or current line if cursor is past the
    // symbol)
    for(int y = startY; y >= 0; y--)
    {
        const std::string& line = lines[y];

        // Count braces in this line (from end to start for backwards search)
        for(int i = (int)line.length() - 1; i >= 0; i--)
        {
            // Skip if we're on the starting line and past start position
            if(y == startY && i >= startX)
                continue;

            char c = line[i];
            if(c == '}')
            {
                braceDepth++;
            }
            else if(c == '{')
            {
                if(braceDepth > 0)
                    braceDepth--;
                else
                    foundOpenBrace = true; // Found enclosing scope start
            }
        }

        // Don't search past the opening brace of current scope
        // (but do search the line with the opening brace for parameters)

        // Check if this line has a declaration of our symbol
        // Only search if we're at same or lower brace depth (within scope)
        if(braceDepth == 0)
        {
            int col = findLocalDeclaration(line, symbol);
            if(col >= 0)
            {
                // Make sure it's before our cursor position if on same line
                if(y < startY || col < startX)
                {
                    outY = y;
                    outX = col;
                    return true;
                }
            }
        }

        // If we've exited our function scope, stop searching
        if(foundOpenBrace && braceDepth == 0)
        {
            // Check this line one more time (function parameters are on/before
            // opening brace)
            int col = findLocalDeclaration(line, symbol);
            if(col >= 0)
            {
                outY = y;
                outX = col;
                return true;
            }

            // Also check the line above for multi-line function signatures
            if(y > 0)
            {
                col = findLocalDeclaration(lines[y - 1], symbol);
                if(col >= 0)
                {
                    outY = y - 1;
                    outX = col;
                    return true;
                }
            }
            break;
        }
    }

    return false;
}

// Search for member variable declaration in class/struct
static bool searchMemberDefinition(const std::vector<std::string>& lines,
                                   const std::string& symbol, int& outY,
                                   int& outX)
{
    // Look for struct/class definitions and their members
    bool inClassOrStruct = false;
    int classStartLine = -1;
    int braceDepth = 0;

    for(int y = 0; y < (int)lines.size(); y++)
    {
        const std::string& line = lines[y];

        // Check for class/struct keyword
        if(line.find("class ") != std::string::npos ||
           line.find("struct ") != std::string::npos)
        {
            inClassOrStruct = true;
            classStartLine = y;
            braceDepth = 0;
        }

        // Track braces
        for(char c : line)
        {
            if(c == '{')
                braceDepth++;
            else if(c == '}')
            {
                braceDepth--;
                if(braceDepth == 0 && inClassOrStruct)
                {
                    inClassOrStruct = false;
                }
            }
        }

        // If we're inside a class/struct at depth 1, look for member
        // declarations
        if(inClassOrStruct && braceDepth == 1)
        {
            int col = findLocalDeclaration(line, symbol);
            if(col >= 0)
            {
                outY = y;
                outX = col;
                return true;
            }
        }
    }

    return false;
}

static bool isHeaderFile(const std::string& path)
{
    return path == ".h" || path == ".hpp";
}

static bool isSourceFile(const std::string& path)
{
    return path == ".c" || path == ".cpp" || path == ".cc";
}

Editor::Editor()
{
    Terminal::enableRawMode();
    Terminal::getWindowSize(screenRows, screenCols);
    screenRows -= 2; // Status bar and message bar

    // Create initial empty buffer
    createNewBuffer();
    saveState();
    currentBuffer->savedUndoIndex = 0; // Mark initial empty buffer as saved
}

Editor::~Editor()
{
    Terminal::clearScreen();
    Terminal::moveCursor(1, 1);
}

void Editor::enableClangdLsp(bool enable, const std::string& compileCommandsDir,
                             const std::string& clangdPath,
                             const std::string& queryDriverAllowList)
{
    clangdLspEnabled = false;
    clangdLspCompileCommandsDir = compileCommandsDir;
    clangdLspPath = clangdPath;
    clangdLspQueryDriverAllowList = queryDriverAllowList;

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!enable)
    {
        if(lspClient)
        {
            lspClient->stop();
            lspClient.reset();
        }
        return;
    }

    // Determine project root (cwd at startup is good enough for uvim).
    char cwd[PATH_MAX];
    std::string rootDir = ".";
    if(getcwd(cwd, sizeof(cwd)))
        rootDir = std::string(cwd);

    // Auto-detect compile_commands.json if caller didn't specify --ccdir
    std::string ccdir = clangdLspCompileCommandsDir;
    auto exists = [](const std::string& p)
    {
        struct stat st;
        return stat(p.c_str(), &st) == 0;
    };

    if(ccdir.empty())
    {
        if(exists(rootDir + "/compile_commands.json"))
            ccdir = rootDir;
        else if(exists(rootDir + "/build/compile_commands.json"))
            ccdir = rootDir + "/build";
    }

    // If not provided, use a conservative default query-driver allowlist so
    // clangd can discover system include paths (standard library headers etc)
    // from common compilers referenced in compile_commands.json. Users with
    // custom toolchains can pass:
    //   --query-driver "/opt/toolchain/bin/*g++*,/opt/toolchain/bin/*gcc*"
    std::string qd = clangdLspQueryDriverAllowList;
    if(qd.empty())
    {
        // Only allow executing compilers from typical system locations.
        // clangd expects a comma-separated list of globs/paths.
        qd =
            "/usr/bin/*clang*,/usr/bin/*clang++*,/usr/bin/*gcc*,/usr/bin/*g++*,"
            "/bin/*gcc*,/bin/*g++*,"
            "/usr/local/bin/*clang*,/usr/local/bin/*clang++*,/usr/local/bin/"
            "*gcc*,/usr/local/bin/*g++*,"
            "/opt/homebrew/bin/*clang*,/opt/homebrew/bin/*clang++*,/opt/"
            "homebrew/bin/*gcc*,/opt/homebrew/bin/*g++*";
    }

    lspClient = std::make_unique<LspClient>();
    if(!lspClient->start(clangdLspPath, rootDir, ccdir, qd))
    {
        lspClient.reset();
        setStatusMessage("clangd LSP: failed to start");
        return;
    }

    clangdLspEnabled = true;
    clangdLspCompileCommandsDir = ccdir;
    setStatusMessage("clangd LSP: ON" +
                     (ccdir.empty() ? "" : (" (ccdir=" + ccdir + ")")));
#else
    (void)enable;
    (void)compileCommandsDir;
    (void)clangdPath;
    setStatusMessage("clangd LSP: not compiled in");
#endif
}

bool Editor::isClangdLspEnabled() const
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    return clangdLspEnabled && lspClient && lspClient->running();
#else
    return false;
#endif
}

// Buffer Management Functions
void Editor::createNewBuffer()
{
    auto buffer = std::make_unique<Buffer>();
    buffers.push_back(std::move(buffer));
    currentBufferIndex = buffers.size() - 1;
    updateCurrentBufferPointers();
    needsFullRedraw = true;
}

void Editor::updateCurrentBufferPointers()
{
    if(currentBufferIndex >= 0 && currentBufferIndex < buffers.size())
    {
        currentBuffer = buffers[currentBufferIndex].get();
        lines = &currentBuffer->lines;
        filename = &currentBuffer->filename;
        dirty = &currentBuffer->dirty;
        cursorX = &currentBuffer->cursorX;
        cursorY = &currentBuffer->cursorY;
        wantedX = &currentBuffer->wantedX;
        offsetX = &currentBuffer->offsetX;
        offsetY = &currentBuffer->offsetY;
    }
    else
    {
        createNewBuffer();
    }
}

void Editor::switchToBuffer(int index)
{
    if(index >= 0 && index < buffers.size())
    {
        saveBufferState();
        currentBufferIndex = index;
        updateCurrentBufferPointers();
        restoreBufferState();
        needsFullRedraw = true;

        std::string msg = "Buffer " + std::to_string(currentBufferIndex + 1) +
                          "/" + std::to_string(buffers.size());
        if(!filename->empty())
        {
            msg += ": " + *filename;
        }
        else
        {
            msg += ": [No Name]";
        }
        if(*dirty)
        {
            msg += " [+]";
        }
        setStatusMessage(msg);
    }
}

void Editor::nextBuffer()
{
    if(buffers.size() > 1)
    {
        int nextIndex = (currentBufferIndex + 1) % buffers.size();
        switchToBuffer(nextIndex);
    }
    else
    {
        setStatusMessage("No other buffers");
    }
}

void Editor::previousBuffer()
{
    if(buffers.size() > 1)
    {
        int prevIndex = currentBufferIndex - 1;
        if(prevIndex < 0)
            prevIndex = buffers.size() - 1;
        switchToBuffer(prevIndex);
    }
    else
    {
        setStatusMessage("No other buffers");
    }
}

void Editor::closeCurrentBuffer()
{
    if(*dirty)
    {
        setStatusMessage("No write since last change (add ! to override)");
        return;
    }

    if(buffers.size() == 1)
    {
        createNewBuffer();
        buffers.erase(buffers.begin());
        currentBufferIndex = 0;
        updateCurrentBufferPointers();
    }
    else
    {
        buffers.erase(buffers.begin() + currentBufferIndex);
        if(currentBufferIndex >= buffers.size())
        {
            currentBufferIndex = buffers.size() - 1;
        }
        updateCurrentBufferPointers();
        restoreBufferState();
    }

    needsFullRedraw = true;
    setStatusMessage("Buffer closed");
}

void Editor::listBuffers()
{
    std::stringstream ss;
    ss << "Buffers: ";

    for(size_t i = 0; i < buffers.size(); i++)
    {
        if(i == currentBufferIndex)
            ss << "[";

        ss << (i + 1) << ":";

        if(!buffers[i]->filename.empty())
        {
            size_t lastSlash = buffers[i]->filename.find_last_of("/\\");
            if(lastSlash != std::string::npos)
                ss << buffers[i]->filename.substr(lastSlash + 1);
            else
                ss << buffers[i]->filename;
        }
        else
        {
            ss << "[No Name]";
        }

        if(buffers[i]->dirty)
            ss << "+";

        if(i == currentBufferIndex)
            ss << "]";

        if(i < buffers.size() - 1)
            ss << " ";
    }

    setStatusMessage(ss.str());
}

int Editor::findBufferByFilename(const std::string& fname)
{
    for(int i = 0; i < buffers.size(); i++)
    {
        if(buffers[i]->filename == fname)
            return i;
    }
    return -1;
}

void Editor::saveBufferState()
{
    // State is automatically saved in buffer structure
}

void Editor::restoreBufferState()
{
    if(currentMode == VISUAL || currentMode == VISUAL_LINE)
    {
        setMode(NORMAL);
    }
}

bool Editor::searchDefinitionInBuffer(Buffer* buf, const std::string& symbol,
                                      int& outY, int& outX)
{
    for(int y = 0; y < buf->lines.size(); ++y)
    {
        const std::string& line = buf->lines[y];
        if(isLikelyDefinition(line, symbol))
        {
            size_t pos = line.find(symbol);
            if(pos != std::string::npos)
            {
                outY = y;
                outX = pos;
                return true;
            }
        }
    }
    return false;
}

void Editor::enterOperatorPending(char op)
{
    pendingOperator = op;
    pendingAwaitingObject = false;
    pendingObjectType = 0;
    pendingCount = std::max(1, repeatCount);
    commandBuffer.clear(); // keep UI tidy
    setStatusMessage(std::string("Operator: ") + op);
    setMode(OP_PENDING);
}

void Editor::handleOperatorPendingMode(int c)
{
    // If user pressed ESC, cancel
    if(c == Terminal::ESC)
    {
        setMode(NORMAL);
        setStatusMessage("");
        return;
    }

    // If user pressed a digit while building a count (rare), ignore here for
    // simplicity Support 'i' or 'a' to enter object-specifier substate
    if(!pendingAwaitingObject && (c == 'i' || c == 'a'))
    {
        pendingAwaitingObject = true;
        pendingObjectType = (char)c;
        setStatusMessage(std::string("Operator: ") + pendingOperator + " " +
                         pendingObjectType);
        return;
    }

    int startY, startX, endY, endX;
    bool rangeFound = false;

    if(pendingAwaitingObject)
    {
        // Expect a text-object specifier now (e.g. '(', '{', '"', 'w', etc.)
        char obj = (char)c;
        bool around = (pendingObjectType == 'a');
        rangeFound =
            getTextObjectRange(obj, around, startY, startX, endY, endX);
    }
    else
    {
        // Motion-based operator: treat c as a motion (w, b, e, $, 0, %, etc.)
        // We'll simulate the motion by saving cursor, doing it, reading
        // destination, then restoring.
        int saveX = *cursorX, saveY = *cursorY, saveWanted = *wantedX,
            saveOffsetY = *offsetY, saveOffsetX = *offsetX;

        bool isExclusiveMotion = false; // Track if motion should be exclusive

        // Apply motion
        switch(c)
        {
        case 'w':
            moveWordForward();
            isExclusiveMotion = true; // 'w' is exclusive in vim
            break;
        case 'b':
            moveWordBackward();
            isExclusiveMotion = true; // 'b' is exclusive in vim
            break;
        case 'e':
            moveToEndOfWord();
            isExclusiveMotion = false; // 'e' is inclusive in vim
            break;
        case '0':
            moveToLineStart();
            break;
        case '$':
            moveToLineEnd();
            break;
        case '%':
            moveToMatchingBracket();
            break;
        case 'j':
            moveDown(pendingCount);
            break;
        case 'k':
            moveUp(pendingCount);
            break;
        case 'G':
            // Move to last line or specific line if count given
            if(pendingCount > 0)
                moveToLine(pendingCount - 1);
            else
                moveToLastLine();
            break;
        case 'g':
            // For gg motion - need to read next char
            {
                int nextChar = Terminal::readKey();
                if(nextChar == 'g')
                {
                    moveToFirstLine();
                }
                else
                {
                    // Unsupported gg variant
                    setStatusMessage("Unknown motion for operator");
                    setMode(NORMAL);
                    *cursorX = saveX;
                    *cursorY = saveY;
                    *wantedX = saveWanted;
                    *offsetY = saveOffsetY;
                    *offsetX = saveOffsetX;
                    return;
                }
            }
            break;
        case '{':
            // Move to beginning of paragraph (previous blank line)
            {
                int targetY = *cursorY;
                // Skip current paragraph
                while(targetY > 0 && !(*lines)[targetY].empty())
                    targetY--;
                // Skip blank lines
                while(targetY > 0 && (*lines)[targetY].empty())
                    targetY--;
                // Find beginning of previous paragraph
                while(targetY > 0 && !(*lines)[targetY - 1].empty())
                    targetY--;
                *cursorY = targetY;
                *cursorX = 0;
            }
            break;
        case '}':
            // Move to end of paragraph (next blank line)
            {
                int targetY = *cursorY;
                int maxLine = lines->size() - 1;
                // Skip current paragraph
                while(targetY < maxLine && !(*lines)[targetY].empty())
                    targetY++;
                // Skip blank lines
                while(targetY < maxLine && (*lines)[targetY].empty())
                    targetY++;
                *cursorY = targetY;
                *cursorX = 0;
            }
            break;
        default:
            // unsupported motion -> cancel operator
            setStatusMessage("Unknown motion for operator");
            setMode(NORMAL);
            // restore
            *cursorX = saveX;
            *cursorY = saveY;
            *wantedX = saveWanted;
            *offsetY = saveOffsetY;
            *offsetX = saveOffsetX;
            return;
        }

        // get destination
        int destX = *cursorX, destY = *cursorY;
        // restore original cursor
        *cursorX = saveX;
        *cursorY = saveY;
        *wantedX = saveWanted;
        *offsetY = saveOffsetY;
        *offsetX = saveOffsetX;

        // compute range between original and dest
        if(saveY < destY || (saveY == destY && saveX <= destX))
        {
            // Forward motion
            startY = saveY;
            startX = saveX;
            endY = destY;
            endX = destX;

            // For exclusive forward motions, don't include the character at
            // destination
            if(isExclusiveMotion)
            {
                // Make the range exclusive by moving end back one position
                if(endX > 0)
                {
                    endX--;
                }
                else if(endY > 0)
                {
                    // If at start of line, go to end of previous line
                    endY--;
                    endX = (*lines)[endY].length() - 1;
                }
            }
        }
        else
        {
            // Backward motion
            startY = destY;
            startX = destX;
            endY = saveY;
            endX = saveX;

            // For exclusive backward motions, adjust similarly
            if(isExclusiveMotion)
            {
                // The range is already correct for backward exclusive motions
                // because we want to delete from dest to just before current
            }
        }

        rangeFound = true;
    }

    if(!rangeFound)
    {
        setStatusMessage("No object found");
        setMode(NORMAL);
        return;
    }

    // Apply operator
    char op = pendingOperator;
    applyOperatorToRange(op, startY, startX, endY, endX);

    // Clear state
    pendingOperator = 0;
    pendingAwaitingObject = false;
    pendingObjectType = 0;
    pendingCount = 0;

    // Return to NORMAL unless operator was 'c' (which already set INSERT mode)
    if(op != 'c')
    {
        setMode(NORMAL);
    }
    needsFullRedraw = true;
}

bool Editor::getTextObjectRange(char objChar, bool around, int& outStartY,
                                int& outStartX, int& outEndY, int& outEndX)
{
    // Current position
    int y = *cursorY;
    int x = *cursorX;

    // Support bracket pairs
    auto findEnclosing = [&](char openc, char closec) -> bool
    {
        // search left for the nearest openc
        int ly = y, lx = x;
        bool foundOpen = false;
        for(;;)
        {
            const std::string& line = (*lines)[ly];
            for(int i = lx; i >= 0; --i)
            {
                if(line[i] == openc)
                {
                    // try to find matching close from here
                    int matchY = ly, matchX = i;
                    // simulate bracket match forward
                    int depth = 0;
                    int ty = matchY, tx = matchX;
                    for(;;)
                    {
                        // move one char forward
                        tx++;
                        while(ty < lines->size() && tx >= (*lines)[ty].length())
                        {
                            ty++;
                            tx = 0;
                            if(ty >= lines->size())
                                break;
                        }
                        if(ty >= lines->size())
                            break;
                        char ch = (*lines)[ty][tx];
                        if(ch == openc)
                            depth++;
                        else if(ch == closec)
                        {
                            if(depth == 0)
                            {
                                // match found at ty,tx
                                outStartY = ly;
                                outStartX = i;
                                outEndY = ty;
                                outEndX = tx;
                                // adjust for 'inner' vs 'around'
                                if(!around)
                                {
                                    // inner: exclude the brackets themselves
                                    // move start forward one char
                                    if(outStartX + 1 <=
                                       (*lines)[outStartY].length())
                                    {
                                        outStartX = outStartX + 1;
                                    }
                                    else
                                    {
                                        // move to next position
                                        outStartY++;
                                        outStartX = 0;
                                    }
                                    // move end back one char
                                    if(outEndX - 1 >= 0)
                                    {
                                        outEndX = outEndX - 1;
                                    }
                                    else
                                    {
                                        // move to previous line end
                                        outEndY--;
                                        outEndX =
                                            (*lines)[outEndY].length() - 1;
                                    }
                                }
                                return true;
                            }
                            else
                            {
                                depth--;
                            }
                        }
                    }
                }
            }
            // move to previous line
            if(ly == 0)
                break;
            ly--;
            if(ly >= 0)
                lx = (*lines)[ly].length() - 1;
        }
        return false;
    };

    if(objChar == '(' || objChar == ')')
    {
        if(findEnclosing('(', ')'))
            return true;
    }
    if(objChar == '{' || objChar == '}')
    {
        if(findEnclosing('{', '}'))
            return true;
    }
    if(objChar == '[' || objChar == ']')
    {
        if(findEnclosing('[', ']'))
            return true;
    }

    // Quotes: find nearest pair of quotes in current line (simple)
    if(objChar == '"' || objChar == '\'')
    {
        const std::string& line = (*lines)[y];
        // search left for quote
        int lpos = -1, rpos = -1;
        for(int i = x; i >= 0; --i)
            if(line[i] == objChar)
            {
                lpos = i;
                break;
            }
        for(int i = x; i < line.length(); ++i)
            if(line[i] == objChar)
            {
                rpos = i;
                break;
            }

        if(lpos >= 0 && rpos >= 0 && lpos < rpos)
        {
            outStartY = y;
            outEndY = y;
            if(around)
            {
                outStartX = lpos;
                outEndX = rpos;
            }
            else
            {
                outStartX = lpos + 1;
                outEndX = rpos - 1;
            }
            return true;
        }
    }

    // Word objects: iw / aw
    if(objChar == 'w')
    {
        // For inner word -> find word boundaries around cursor on same line
        const std::string& line = (*lines)[y];
        int L = x, R = x;
        // If cursor at end-of-line and not in word, try next char
        if(L >= line.length())
            L = line.length() - 1;
        // move L to start of word
        while(L > 0 && !isWordChar(line[L]))
            L--;
        while(L > 0 && isWordChar(line[L - 1]))
            L--;
        // move R to end of word
        while(R < (int)line.length() && isWordChar(line[R]))
            R++;
        if(R <= L)
            return false;
        outStartY = y;
        outEndY = y;
        if(around)
        {
            outStartX = L;
            outEndX = R - 1;
        } // 'aw' includes trailing space? keep simple: word only
        else
        {
            outStartX = L;
            outEndX = R - 1;
        }
        return true;
    }

    // Paragraph 'p' (simple: blank-line separated)
    if(objChar == 'p')
    {
        int sy = y, ey = y;
        // find paragraph start
        while(sy > 0 && !(*lines)[sy].empty())
            sy--;
        if((*lines)[sy].empty() && sy < y)
            sy++;
        // find paragraph end
        while(ey < lines->size() - 1 && !(*lines)[ey].empty())
            ey++;
        if((*lines)[ey].empty() && ey > y)
            ey--;
        outStartY = sy;
        outEndY = ey;
        outStartX = 0;
        outEndX = (*lines)[outEndY].length() - 1;
        return true;
    }

    return false;
}

void Editor::applyOperatorToRange(char op, int startY, int startX, int endY,
                                  int endX)
{
    // Normalize bounds
    if(startY > endY || (startY == endY && startX > endX))
    {
        std::swap(startY, endY);
        std::swap(startX, endX);
    }

    // Yank if 'y' or for 'd' we fill yankBuffer
    if(op == 'y' || op == 'd' || op == 'c')
    {
        yankRange(startY, startX, endY, endX);
    }

    if(op == 'd' || op == 'c')
    {
        deleteRange(startY, startX, endY, endX);
        saveState();
    }

    if(op == '=')
    {
        // For indent operator, we indent all lines in the range
        // For line-wise motions or when the range spans multiple lines
        autoIndentRange(startY, endY);

        int linesIndented = endY - startY + 1;
        setStatusMessage(std::to_string(linesIndented) + " line" +
                         (linesIndented > 1 ? "s" : "") + " indented");
        saveState();
    }

    if(op == 'c')
    {
        // After change, enter insert mode at start
        *cursorY = startY;
        *cursorX = startX;
        setMode(INSERT);
    }
    else
    {
        // Place cursor at start of affected range (or keep it for indent)
        if(op != '=')
        {
            *cursorY = startY;
            *cursorX = startX;
        }
    }

    needsFullRedraw = true;
    *dirty = true;
}

void Editor::yankRange(int startY, int startX, int endY, int endX)
{
    yankBuffer.clear();
    if(startY == endY)
    {
        const std::string& line = (*lines)[startY];
        if(startX <= endX && startX < line.length())
            yankBuffer = line.substr(startX, endX - startX + 1);
    }
    else
    {
        // first line
        const std::string& firstLine = (*lines)[startY];
        if(startX < firstLine.length())
            yankBuffer = firstLine.substr(startX);
        yankBuffer += "\n";
        // middle lines
        for(int r = startY + 1; r < endY; ++r)
        {
            yankBuffer += (*lines)[r];
            yankBuffer += "\n";
        }
        // last line
        const std::string& lastLine = (*lines)[endY];
        if(endX >= 0 && endX < lastLine.length())
            yankBuffer += lastLine.substr(0, endX + 1);
    }
    setStatusMessage("Yanked");
}

void Editor::deleteRange(int startY, int startX, int endY, int endX)
{
    if(startY == endY)
    {
        std::string& line = (*lines)[startY];
        if(startX <= endX && startX < line.length())
        {
            line.erase(startX, endX - startX + 1);
        }
    }
    else
    {
        // First line: keep 0..startX-1
        std::string prefix = (*lines)[startY].substr(0, startX);
        std::string suffix = "";
        if(endX + 1 < (*lines)[endY].length())
            suffix = (*lines)[endY].substr(endX + 1);
        // Erase middle lines
        lines->erase(lines->begin() + startY, lines->begin() + endY + 1);
        // Insert combined line
        lines->insert(lines->begin() + startY, prefix + suffix);
    }

    if(lines->empty())
        lines->push_back("");
    // adjust cursor if needed
    if(*cursorY >= lines->size())
        *cursorY = lines->size() - 1;
    if(*cursorX > (*lines)[*cursorY].length())
        *cursorX = (*lines)[*cursorY].length();
    needsFullRedraw = true;
}

void Editor::setMode(Mode mode)
{
    Mode previousMode = currentMode;
    currentMode = mode;
    needsFullRedraw = true;

    // Cursor shape handling (NEW)
    if(mode == INSERT)
    {
        Terminal::setCursorBarBlinking();
    }
    else
    {
        Terminal::setCursorBlock();
    }

    if(mode == NORMAL)
    {
        commandBuffer.clear();
        repeatCount = 0;
        pendingOperator = 0;
        pendingAwaitingObject = false;
        pendingObjectType = 0;
        pendingCount = 0;
    }
    else if(mode == COMMAND)
    {
        commandBuffer = ":";
    }
    else if(mode == VISUAL || mode == VISUAL_LINE)
    {
        currentBuffer->visualStartX = *cursorX;
        currentBuffer->visualStartY = *cursorY;
        currentBuffer->visualEndX = *cursorX;
        currentBuffer->visualEndY = *cursorY;
    }
    else if(mode == SEARCH_FORWARD)
    {
        commandBuffer = "/";
        searchQuery.clear();
        savedCursorX = *cursorX;
        savedCursorY = *cursorY;
        searchForward = true;
    }
    else if(mode == SEARCH_BACKWARD)
    {
        commandBuffer = "?";
        searchQuery.clear();
        savedCursorX = *cursorX;
        savedCursorY = *cursorY;
        searchForward = false;
    }
    else if(mode == FILE_BROWSER)
    {
        if(fileList.empty() && !currentDirectory.empty())
        {
            loadDirectory(currentDirectory);
        }
    }
    else if(mode == FUZZY_FIND)
    {
        initializeFuzzyFind();
    }
    else if(mode == BUFFER_BROWSER)
    {
        initializeBufferBrowser();
    }
    else if(mode == GREP_SEARCH)
    {
        initializeGrepSearch();
    }
}

std::string Editor::getModeString() const
{
    switch(currentMode)
    {
    case NORMAL:
        return "NORMAL";
    case INSERT:
        return "INSERT";
    case VISUAL:
        return "VISUAL";
    case VISUAL_LINE:
        return "VISUAL LINE";
    case VISUAL_BLOCK:
        return "VISUAL BLOCK";
    case COMMAND:
        return "COMMAND";
    case SEARCH_FORWARD:
        return "/";
    case SEARCH_BACKWARD:
        return "?";
    case FILE_BROWSER:
        return "BROWSE";
    case FUZZY_FIND:
        return "FUZZY";
    case BUFFER_BROWSER:
        return "BUFFERS";
    case GREP_SEARCH:
        return "GREP";
    }
    return "";
}

void Editor::openFile(const std::string& fname)
{
    // Normalize path (CRITICAL for buffer matching)
    std::string path = fname;
    try
    {
        path = std::filesystem::canonical(fname).string();
    }
    catch(...)
    {
        // fallback if file doesn't exist yet
        path = fname;
    }

    // Check if file already open
    int existing = findBufferByFilename(path);
    if(existing >= 0)
    {
        switchToBuffer(existing);
        setStatusMessage("\"" + path + "\" [Buffer " +
                         std::to_string(existing + 1) + "]");
        return;
    }

    // Always create a new buffer for explicit open/jump
    createNewBuffer();

    *filename = path;
    lines->clear();

    std::ifstream file(path);
    if(file.is_open())
    {
        std::string line;
        while(std::getline(file, line))
        {
            if(!line.empty() && line.back() == '\r')
                line.pop_back();
            lines->push_back(line);
        }
        file.close();
    }

    if(lines->empty())
        lines->push_back("");

    *dirty = false;
    *cursorX = *cursorY = 0;
    *offsetX = *offsetY = 0;

    // Reset undo state cleanly
    currentBuffer->undoStack.clear();
    currentBuffer->undoIndex = -1;
    saveState();
    currentBuffer->savedUndoIndex = currentBuffer->undoIndex;

    needsFullRedraw = true;

#ifdef UVIM_ENABLE_CLANGD_LSP
    // Notify LSP about the newly opened file so gd works from system headers
    if(isClangdLspEnabled() && lspClient)
    {
        // Build text content from loaded lines
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }
        // didChange will call didOpen if needed
        lspClient->didChange(path, text);
    }
#endif

    setStatusMessage("\"" + *filename + "\" " + std::to_string(lines->size()) +
                     " lines [Buffer " +
                     std::to_string(currentBufferIndex + 1) + "]");
}

void Editor::saveFile()
{
    if(filename->empty())
    {
        setStatusMessage("No file name");
        return;
    }

    // Clean up lines before saving: convert tabs to spaces, remove trailing
    // whitespace
    int linesModified = 0;
    for(size_t lineIdx = 0; lineIdx < lines->size(); lineIdx++)
    {
        std::string& line = (*lines)[lineIdx];
        std::string original = line;

        // Convert tabs to spaces (4 spaces per tab, aligned to tab stops)
        std::string expanded;
        expanded.reserve(line.size());
        int col = 0;
        for(char c : line)
        {
            if(c == '\t')
            {
                // Add spaces to reach next tab stop (every 4 columns)
                int spacesToAdd = 4 - (col % 4);
                expanded.append(spacesToAdd, ' ');
                col += spacesToAdd;
            }
            else
            {
                expanded += c;
                col++;
            }
        }
        line = expanded;

        // Remove trailing whitespace
        size_t endPos = line.find_last_not_of(" \t");
        if(endPos != std::string::npos)
        {
            line = line.substr(0, endPos + 1);
        }
        else if(!line.empty())
        {
            // Line is all whitespace
            line.clear();
        }

        if(line != original)
        {
            linesModified++;

            // Adjust cursor if on this line and beyond the new line length
            if((int)lineIdx == *cursorY && *cursorX > (int)line.length())
            {
                *cursorX = line.length() > 0 ? line.length() - 1 : 0;
            }
        }
    }

    std::ofstream file(*filename);
    if(file.is_open())
    {
        for(const auto& line : *lines)
        {
            file << line << '\n';
        }
        file.close();
        *dirty = false;
        currentBuffer->savedUndoIndex =
            currentBuffer->undoIndex; // Mark this state as saved

        std::string msg = "\"" + *filename + "\" " +
                          std::to_string(lines->size()) + "L written";
        if(linesModified > 0)
        {
            msg += " (" + std::to_string(linesModified) + " lines cleaned)";
            needsFullRedraw = true; // Redraw to show cleaned lines
        }
        setStatusMessage(msg);
    }
    else
    {
        setStatusMessage("Can't save! I/O error");
    }
}

// Jump between header and source file
bool Editor::fileExists(const std::string& path)
{
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

std::string Editor::getSymbolUnderCursor()
{
    if(*cursorY >= lines->size())
        return "";

    const std::string& line = (*lines)[*cursorY];
    int x = *cursorX;

    if(x >= line.size() || !isIdent(line[x]))
        return "";

    int l = x;
    int r = x;

    while(l > 0 && isIdent(line[l - 1]))
        l--;
    while(r < line.size() && isIdent(line[r]))
        r++;

    return line.substr(l, r - l);
}

std::string Editor::findAlternateFile(const std::string& currentFile)
{
    if(currentFile.empty())
        return "";

    // Find the last dot to get the extension
    size_t lastDot = currentFile.find_last_of('.');
    if(lastDot == std::string::npos)
        return "";

    std::string baseName = currentFile.substr(0, lastDot);
    std::string extension = currentFile.substr(lastDot);

    // List of header extensions
    static const std::vector<std::string> headerExts = {".h", ".hpp", ".hxx",
                                                        ".H", ".HPP", ".HXX"};

    // List of source extensions
    static const std::vector<std::string> sourceExts = {
        ".cpp", ".cc", ".cxx", ".c", ".C", ".CPP", ".CC", ".CXX"};

    // Check if current file is a header
    bool isHeader = false;
    for(const auto& ext : headerExts)
    {
        if(extension == ext)
        {
            isHeader = true;
            break;
        }
    }

    // Try to find the alternate file
    std::vector<std::string> candidates;

    if(isHeader)
    {
        // Current file is a header, look for source files
        for(const auto& ext : sourceExts)
        {
            candidates.push_back(baseName + ext);
        }
    }
    else
    {
        // Current file is likely a source, look for header files
        for(const auto& ext : headerExts)
        {
            candidates.push_back(baseName + ext);
        }
    }

    // Also check in common relative directories
    size_t lastSlash = currentFile.find_last_of('/');
    std::string dir = "";
    std::string fileName = currentFile;

    if(lastSlash != std::string::npos)
    {
        dir = currentFile.substr(0, lastSlash + 1);
        fileName = currentFile.substr(lastSlash + 1);
        baseName = fileName.substr(0, fileName.find_last_of('.'));
    }

    // Common directory pairs
    std::vector<std::pair<std::string, std::string>> dirPairs = {
        {"src/", "include/"},
        {"source/", "include/"},
        {"src/", "inc/"},
        {"source/", "headers/"},
        {"lib/", "include/"},
        {"", "../include/"},
        {"", "../inc/"},
        {"include/", "../src/"},
        {"include/", "../source/"},
        {"inc/", "../src/"},
        {"headers/", "../source/"},
        {"include/", "../lib/"},
    };

    // Add candidates from related directories
    for(const auto& [srcDir, incDir] : dirPairs)
    {
        if(dir.find(srcDir) != std::string::npos && isHeader == false)
        {
            // We're in a source dir, look for headers in include dir
            std::string altDir = dir;
            size_t pos = altDir.find(srcDir);
            if(pos != std::string::npos)
            {
                altDir.replace(pos, srcDir.length(), incDir);
                for(const auto& ext : headerExts)
                {
                    candidates.push_back(altDir + baseName + ext);
                }
            }
        }
        else if(dir.find(incDir) != std::string::npos && isHeader == true)
        {
            // We're in an include dir, look for sources in source dir
            std::string altDir = dir;
            size_t pos = altDir.find(incDir);
            if(pos != std::string::npos)
            {
                altDir.replace(pos, incDir.length(), srcDir);
                for(const auto& ext : sourceExts)
                {
                    candidates.push_back(altDir + baseName + ext);
                }
            }
        }
    }

    // Check which candidate exists
    for(const auto& candidate : candidates)
    {
        if(fileExists(candidate))
        {
            return candidate;
        }
    }

    return "";
}

void Editor::jumpToAlternateFile()
{
    if(filename->empty())
    {
        setStatusMessage("No file currently open");
        return;
    }

    std::string alternate = findAlternateFile(*filename);

    if(alternate.empty())
    {
        setStatusMessage("No alternate file found for " + *filename);
        return;
    }

    // Check if the alternate file is already open in a buffer
    int bufferIndex = findBufferByFilename(alternate);

    if(bufferIndex >= 0)
    {
        // Switch to existing buffer
        switchToBuffer(bufferIndex);
        setStatusMessage("Switched to " + alternate);
    }
    else
    {
        // Open the alternate file in a new buffer
        openFile(alternate);
        setStatusMessage("Opened " + alternate);
    }
}

// Movement implementations
void Editor::moveLeft(int count)
{
    while(count-- > 0)
    {
        if(*cursorX > 0)
        {
            (*cursorX)--;
        }
        else if(*cursorY > 0)
        {
            (*cursorY)--;
            *cursorX = (*lines)[*cursorY].length();
        }
    }
    *wantedX = *cursorX;
}

void Editor::moveRight(int count)
{
    while(count-- > 0)
    {
        if(*cursorY < lines->size())
        {
            if(*cursorX < (*lines)[*cursorY].length())
            {
                (*cursorX)++;
            }
            else if(*cursorY < lines->size() - 1)
            {
                (*cursorY)++;
                *cursorX = 0;
            }
        }
    }
    *wantedX = *cursorX;
}

void Editor::moveUp(int count)
{
    while(count-- > 0 && *cursorY > 0)
    {
        (*cursorY)--;
    }
    *cursorX = std::min(*wantedX, (int)(*lines)[*cursorY].length());
}

void Editor::moveDown(int count)
{
    while(count-- > 0 && *cursorY < lines->size() - 1)
    {
        (*cursorY)++;
    }
    *cursorX = std::min(*wantedX, (int)(*lines)[*cursorY].length());
}

void Editor::moveWordForward()
{
    int y = *cursorY;
    int x = *cursorX;

    while(true)
    {
        const std::string& line = (*lines)[y];

        // End of line → go to next line
        if(x >= (int)line.length())
        {
            if(y + 1 >= (int)lines->size())
                break;

            y++;
            x = 0;

            // Skip leading whitespace on next line
            while(x < (int)(*lines)[y].length() &&
                  std::isspace((unsigned char)(*lines)[y][x]))
            {
                x++;
            }

            break;
        }

        char c = line[x];

        // Starting inside whitespace → skip whitespace forward
        if(std::isspace((unsigned char)c))
        {
            while(x < (int)line.length() &&
                  std::isspace((unsigned char)line[x]))
            {
                x++;
            }
            break;
        }

        // MAIN LOGIC: eat an entire "word unit"
        // (either alphanumeric-run OR punctuation-run)
        // This matches vim behavior: stop at transitions between character
        // types

        bool isAlphaWord = (std::isalnum((unsigned char)c) || c == '_');
        x++;

        while(x < (int)line.length())
        {
            char d = line[x];
            bool dAlpha = (std::isalnum((unsigned char)d) || d == '_');

            // Break on whitespace or character type change
            if(std::isspace((unsigned char)d))
                break;
            if(isAlphaWord != dAlpha)
                break; // Stop at boundary between alphanumeric and punctuation

            x++;
        }

        // DO NOT consume trailing punctuation - this is the fix!
        // In vim, 'dw' at 'Foo()' should stop at '(', not after ')'

        break;
    }

    // Final cursor update
    *cursorX = x;
    *cursorY = y;
    *wantedX = x;
}

void Editor::moveWordBackward()
{
    int y = *cursorY;
    int x = *cursorX;

    // If at start of file, do nothing
    if(y == 0 && x == 0)
    {
        return;
    }

    // If at start of line, go to end of previous line
    if(x == 0)
    {
        y--;
        x = (*lines)[y].length();
    }

    const std::string& line = (*lines)[y];

    // Move back one character to start
    if(x > 0)
    {
        x--;
    }

    // Skip whitespace backwards
    while(x > 0 && std::isspace((unsigned char)line[x]))
    {
        x--;
    }

    // Now we're on a non-whitespace character
    // Determine its type and find the start of this word
    if(x >= 0 && !std::isspace((unsigned char)line[x]))
    {
        bool isAlphaWord =
            (std::isalnum((unsigned char)line[x]) || line[x] == '_');

        // Move backwards while we're in the same character type
        while(x > 0)
        {
            char prevChar = line[x - 1];
            bool prevAlpha =
                (std::isalnum((unsigned char)prevChar) || prevChar == '_');

            // Stop if we hit whitespace or change character type
            if(std::isspace((unsigned char)prevChar))
                break;
            if(isAlphaWord != prevAlpha)
                break;

            x--;
        }
    }

    // Final cursor update
    *cursorX = x;
    *cursorY = y;
    *wantedX = x;
}

void Editor::moveToEndOfWord()
{
    int y = *cursorY;
    int x = *cursorX;

    const std::string& line = (*lines)[y];

    // If already at end of line, move to next line
    if(x >= (int)line.length() - 1)
    {
        if(y + 1 < (int)lines->size())
        {
            y++;
            x = 0;
            const std::string& nextLine = (*lines)[y];

            // Skip whitespace at start of next line
            while(x < (int)nextLine.length() &&
                  std::isspace((unsigned char)nextLine[x]))
            {
                x++;
            }

            // Then move to end of first word
            if(x < (int)nextLine.length())
            {
                bool isAlphaWord = (std::isalnum((unsigned char)nextLine[x]) ||
                                    nextLine[x] == '_');

                while(x < (int)nextLine.length() - 1)
                {
                    char nextChar = nextLine[x + 1];
                    bool nextAlpha = (std::isalnum((unsigned char)nextChar) ||
                                      nextChar == '_');

                    if(std::isspace((unsigned char)nextChar))
                        break;
                    if(isAlphaWord != nextAlpha)
                        break;

                    x++;
                }
            }
        }
    }
    else
    {
        // Move forward one character to start
        x++;

        // Skip whitespace forward
        while(x < (int)line.length() && std::isspace((unsigned char)line[x]))
        {
            x++;
        }

        // Now find end of current word
        if(x < (int)line.length())
        {
            bool isAlphaWord =
                (std::isalnum((unsigned char)line[x]) || line[x] == '_');

            // Move forward while in same character type
            while(x < (int)line.length() - 1)
            {
                char nextChar = line[x + 1];
                bool nextAlpha =
                    (std::isalnum((unsigned char)nextChar) || nextChar == '_');

                if(std::isspace((unsigned char)nextChar))
                    break;
                if(isAlphaWord != nextAlpha)
                    break;

                x++;
            }
        }
    }

    // Final cursor update
    *cursorX = x;
    *cursorY = y;
    *wantedX = x;
}

void Editor::moveToLineStart()
{
    *cursorX = 0;
    *wantedX = *cursorX;
}

void Editor::moveToLineEnd()
{
    if(*cursorY < lines->size())
    {
        *cursorX = (*lines)[*cursorY].length();
        if(*cursorX > 0 && currentMode == NORMAL)
        {
            (*cursorX)--;
        }
    }
    *wantedX = *cursorX;
}

void Editor::moveToFirstLine()
{
    *cursorY = 0;
    *cursorX = 0;
    *wantedX = *cursorX;
}

void Editor::moveToLastLine()
{
    *cursorY = lines->size() - 1;
    *cursorX = 0;
    *wantedX = *cursorX;
}

void Editor::moveToLine(int line)
{
    *cursorY = std::max(0, std::min(line, (int)lines->size() - 1));
    *cursorX = 0;
}

void Editor::jumpForward()
{
    if(jumpForwardStack.empty())
    {
        setStatusMessage("Jump stack empty");
        return;
    }

    JumpLocation current;
    current.bufferIndex = currentBufferIndex;
    current.cursorX = *cursorX;
    current.cursorY = *cursorY;
    current.offsetX = *offsetX;
    current.offsetY = *offsetY;

    jumpBackStack.push_back(current);

    JumpLocation target = jumpForwardStack.back();
    jumpForwardStack.pop_back();

    restoreJumpLocation(target);
}

void Editor::jumpBack()
{
    if(jumpBackStack.empty())
    {
        setStatusMessage("Jump stack empty");
        return;
    }

    JumpLocation current;
    current.bufferIndex = currentBufferIndex;
    current.cursorX = *cursorX;
    current.cursorY = *cursorY;
    current.offsetX = *offsetX;
    current.offsetY = *offsetY;

    jumpForwardStack.push_back(current);

    JumpLocation target = jumpBackStack.back();
    jumpBackStack.pop_back();

    restoreJumpLocation(target);
}

void Editor::pushJumpLocation()
{
    if(!currentBuffer)
        return;

    JumpLocation loc;
    loc.bufferIndex = currentBufferIndex;
    loc.cursorX = *cursorX;
    loc.cursorY = *cursorY;
    loc.offsetX = *offsetX;
    loc.offsetY = *offsetY;

    jumpBackStack.push_back(loc);
    jumpForwardStack.clear(); // Vim behavior
}

void Editor::restoreJumpLocation(const JumpLocation& loc)
{
    if(loc.bufferIndex < 0 || loc.bufferIndex >= buffers.size())
        return;

    switchToBuffer(loc.bufferIndex);

    *cursorX = loc.cursorX;
    *cursorY = loc.cursorY;
    *offsetX = loc.offsetX;
    *offsetY = loc.offsetY;

    needsFullRedraw = true;
}

void Editor::scrollHalfPageDown(bool visual)
{
    int half = screenRows / 2;
    moveDown(half);
    adjustViewport();

    if(visual)
        updateVisualSelection();
}

void Editor::scrollHalfPageUp(bool visual)
{
    int half = screenRows / 2;
    moveUp(half);
    adjustViewport();

    if(visual)
        updateVisualSelection();
}

void Editor::moveToMatchingBracket()
{
    if(*cursorY >= lines->size())
        return;

    const std::string& line = (*lines)[*cursorY];
    if(*cursorX >= line.length())
        return;

    char c = line[*cursorX];
    char open, close;

    switch(c)
    {
    case '(':
        open = '(';
        close = ')';
        break;
    case ')':
        open = '(';
        close = ')';
        break;
    case '{':
        open = '{';
        close = '}';
        break;
    case '}':
        open = '{';
        close = '}';
        break;
    case '[':
        open = '[';
        close = ']';
        break;
    case ']':
        open = '[';
        close = ']';
        break;
    default:
        return; // not on bracket → do nothing
    }

    int direction = (c == open) ? +1 : -1;
    int depth = 0;

    int y = *cursorY;
    int x = *cursorX;

    while(true)
    {
        x += direction;

        // Move across lines if needed
        while(x < 0 || (y < lines->size() && x >= (*lines)[y].length()))
        {
            if(direction == +1)
            {
                y++;
                x = 0;
            }
            else
            {
                y--;
                if(y < 0)
                    return;
                x = (*lines)[y].length() - 1;
            }
            if(y < 0 || y >= lines->size())
                return;
        }

        char ch = (*lines)[y][x];

        if(ch == c)
            depth++;
        else if(ch == (direction == +1 ? close : open))
        {
            if(depth == 0)
            {
                *cursorY = y;
                *cursorX = x;
                *wantedX = x;
                return;
            }
            depth--;
        }
    }
}

bool Editor::isWordChar(char c) const
{
    if(std::isspace((unsigned char)c))
        return false;
    if(std::isalnum((unsigned char)c) || c == '_')
        return true; // letters/numbers
    // punctuation counts as “word” for w/dw/cw
    return true;
}

// Editing operations
void Editor::insertChar(char c)
{
    if(*cursorY >= lines->size())
    {
        lines->push_back("");
    }

    (*lines)[*cursorY].insert(*cursorX, 1, c);
    (*cursorX)++;
    *dirty = true;
}

void Editor::insertNewline()
{
    if(*cursorY >= lines->size())
    {
        lines->push_back("");
        (*cursorY)++;
        *cursorX = 0;
        *dirty = true;
        needsFullRedraw = true;
        return;
    }

    // Get current line and calculate indentation
    const std::string& currentLine = (*lines)[*cursorY];

    // Find leading whitespace of current line
    size_t indent = 0;
    while(indent < currentLine.length() &&
          (currentLine[indent] == ' ' || currentLine[indent] == '\t'))
    {
        indent++;
    }
    std::string indentStr = currentLine.substr(0, indent);

    // Check if we should add extra indent (after { or other block openers)
    bool addExtraIndent = false;
    if(isCppFile() && *cursorX > 0)
    {
        // Look for { before cursor position (ignoring trailing whitespace)
        int checkPos = *cursorX - 1;
        while(checkPos >= 0 &&
              (currentLine[checkPos] == ' ' || currentLine[checkPos] == '\t'))
        {
            checkPos--;
        }
        if(checkPos >= 0 && currentLine[checkPos] == '{')
        {
            addExtraIndent = true;
        }
    }

    // Split the line at cursor position
    std::string remainder;
    if(*cursorX < currentLine.length())
    {
        remainder = currentLine.substr(*cursorX);
        // Trim leading whitespace from remainder since we'll add proper indent
        size_t startPos = remainder.find_first_not_of(" \t");
        if(startPos != std::string::npos)
        {
            remainder = remainder.substr(startPos);
        }
        else
        {
            remainder = "";
        }
        (*lines)[*cursorY] = currentLine.substr(0, *cursorX);
    }

    // Build the new line with proper indentation
    std::string newLine = indentStr;
    if(addExtraIndent)
    {
        newLine += "    "; // Add 4 spaces for block indent
    }
    newLine += remainder;

    // Insert the new line
    lines->insert(lines->begin() + *cursorY + 1, newLine);

    (*cursorY)++;
    *cursorX = indentStr.length() + (addExtraIndent ? 4 : 0);
    *dirty = true;
    needsFullRedraw = true;
}

void Editor::deleteChar()
{
    if(*cursorY >= lines->size())
        return;
    if(*cursorX == 0 && *cursorY == 0)
        return;

    if(*cursorX > 0)
    {
        (*lines)[*cursorY].erase(*cursorX - 1, 1);
        (*cursorX)--;
    }
    else
    {
        *cursorX = (*lines)[*cursorY - 1].length();
        (*lines)[*cursorY - 1] += (*lines)[*cursorY];
        lines->erase(lines->begin() + *cursorY);
        (*cursorY)--;
        needsFullRedraw = true;
    }
    *dirty = true;
}

void Editor::deleteCharForward()
{
    if(*cursorY >= lines->size())
        return;

    if(*cursorX < (*lines)[*cursorY].length())
    {
        (*lines)[*cursorY].erase(*cursorX, 1);
    }
    else if(*cursorY < lines->size() - 1)
    {
        (*lines)[*cursorY] += (*lines)[*cursorY + 1];
        lines->erase(lines->begin() + *cursorY + 1);
        needsFullRedraw = true;
    }
    *dirty = true;
}

void Editor::deleteLine()
{
    if(lines->empty())
        return;

    yankLine();

    lines->erase(lines->begin() + *cursorY);
    if(lines->empty())
    {
        lines->push_back("");
    }

    if(*cursorY >= lines->size())
    {
        *cursorY = lines->size() - 1;
    }

    *cursorX = 0;
    *dirty = true;
    needsFullRedraw = true;
}

void Editor::deleteToLineEnd()
{
    if(*cursorY >= lines->size())
        return;

    if(*cursorX < (*lines)[*cursorY].length())
    {
        yankBuffer = (*lines)[*cursorY].substr(*cursorX);
        (*lines)[*cursorY] = (*lines)[*cursorY].substr(0, *cursorX);
        *dirty = true;
    }
}

void Editor::yankLine()
{
    if(*cursorY < lines->size())
    {
        yankBuffer = (*lines)[*cursorY] + "\n";
        setStatusMessage("Line yanked");
    }
}

void Editor::yankToLineEnd()
{
    if(*cursorY < lines->size() && *cursorX < (*lines)[*cursorY].length())
    {
        yankBuffer = (*lines)[*cursorY].substr(*cursorX);
        setStatusMessage("Yanked to line end");
    }
}

void Editor::yankSelection()
{
    yankBuffer.clear();

    if(currentMode == VISUAL_LINE)
    {
        int startY =
            std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
        int endY =
            std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);

        for(int i = startY; i <= endY; i++)
        {
            yankBuffer += (*lines)[i] + "\n";
        }
        setStatusMessage(std::to_string(endY - startY + 1) + " lines yanked");
    }
    else
    {
        int startY, startX, endY, endX;
        getSelectionBounds(startY, startX, endY, endX);

        if(startY == endY)
        {
            yankBuffer = (*lines)[startY].substr(startX, endX - startX + 1);
        }
        else
        {
            yankBuffer = (*lines)[startY].substr(startX) + "\n";
            for(int i = startY + 1; i < endY; i++)
            {
                yankBuffer += (*lines)[i] + "\n";
            }
            yankBuffer += (*lines)[endY].substr(0, endX + 1);
        }
        setStatusMessage("Selection yanked");
    }
}

// System clipboard support
static std::string getClipboardCommand()
{
#ifdef __APPLE__
    return "pbpaste";
#else
    // Linux - try xclip first, then xsel
    if(system("which xclip > /dev/null 2>&1") == 0)
        return "xclip -selection clipboard -o";
    if(system("which xsel > /dev/null 2>&1") == 0)
        return "xsel --clipboard --output";
    return "";
#endif
}

static std::string setClipboardCommand()
{
#ifdef __APPLE__
    return "pbcopy";
#else
    // Linux - try xclip first, then xsel
    if(system("which xclip > /dev/null 2>&1") == 0)
        return "xclip -selection clipboard";
    if(system("which xsel > /dev/null 2>&1") == 0)
        return "xsel --clipboard --input";
    return "";
#endif
}

std::string Editor::getSystemClipboard()
{
    std::string cmd = getClipboardCommand();
    if(cmd.empty())
        return "";

    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
        return "";

    std::string result;
    char buffer[256];
    while(fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        result += buffer;
    }
    pclose(pipe);

    return result;
}

void Editor::setSystemClipboard(const std::string& text)
{
    std::string cmd = setClipboardCommand();
    if(cmd.empty())
        return;

    FILE* pipe = popen(cmd.c_str(), "w");
    if(!pipe)
        return;

    fwrite(text.c_str(), 1, text.length(), pipe);
    pclose(pipe);
}

void Editor::yankToSystemClipboard()
{
    if(yankBuffer.empty())
    {
        // If no yank buffer, yank current line first
        yankLine();
    }
    setSystemClipboard(yankBuffer);
    setStatusMessage("Yanked to system clipboard");
}

void Editor::pasteFromSystemClipboard()
{
    std::string clipboard = getSystemClipboard();
    if(clipboard.empty())
    {
        setStatusMessage("System clipboard is empty");
        return;
    }

    // Store in yank buffer and paste
    yankBuffer = clipboard;
    pasteAfter();
    setStatusMessage("Pasted from system clipboard");
}

void Editor::pasteAfter()
{
    if(yankBuffer.empty())
        return;

    if(yankBuffer.back() == '\n')
    {
        // Line-wise paste
        lines->insert(lines->begin() + *cursorY + 1, "");
        (*cursorY)++;
        *cursorX = 0;

        std::istringstream ss(yankBuffer);
        std::string line;
        int insertPos = *cursorY;

        while(std::getline(ss, line))
        {
            if(insertPos == *cursorY)
            {
                (*lines)[insertPos] = line;
            }
            else
            {
                lines->insert(lines->begin() + insertPos, line);
            }
            insertPos++;
        }
    }
    else
    {
        // Character-wise paste
        if(*cursorX < (*lines)[*cursorY].length())
            (*cursorX)++;
        (*lines)[*cursorY].insert(*cursorX, yankBuffer);
        *cursorX += yankBuffer.length() - 1;
    }

    *dirty = true;
    needsFullRedraw = true;
    saveState();
    setStatusMessage("Pasted");
}

void Editor::pasteBefore()
{
    if(yankBuffer.empty())
        return;

    if(yankBuffer.back() == '\n')
    {
        // Line-wise paste
        std::istringstream ss(yankBuffer);
        std::string line;
        int insertPos = *cursorY;

        while(std::getline(ss, line))
        {
            lines->insert(lines->begin() + insertPos, line);
            insertPos++;
        }
        *cursorX = 0;
    }
    else
    {
        // Character-wise paste
        (*lines)[*cursorY].insert(*cursorX, yankBuffer);
    }

    *dirty = true;
    needsFullRedraw = true;
    saveState();
    setStatusMessage("Pasted");
}

// Visual mode functions
void Editor::startVisualMode()
{
    setMode(VISUAL);
}

void Editor::startVisualLineMode()
{
    setMode(VISUAL_LINE);
}

void Editor::startVisualBlockMode()
{
    setMode(VISUAL_BLOCK);
    currentBuffer->visualBlockStartX = *cursorX;
    currentBuffer->visualBlockStartY = *cursorY;
    currentBuffer->visualBlockEndX = *cursorX;
    currentBuffer->visualBlockEndY = *cursorY;
    currentBuffer->visualBlockInsertText.clear();
}

void Editor::updateVisualSelection()
{
    currentBuffer->visualEndX = *cursorX;
    currentBuffer->visualEndY = *cursorY;
}

void Editor::updateVisualBlockSelection()
{
    currentBuffer->visualBlockEndX = *cursorX;
    currentBuffer->visualBlockEndY = *cursorY;
}

bool Editor::isInSelection(int row, int col)
{
    if(currentMode != VISUAL && currentMode != VISUAL_LINE)
        return false;

    if(currentMode == VISUAL_LINE)
    {
        int startY =
            std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
        int endY =
            std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);
        return row >= startY && row <= endY;
    }

    int startY, startX, endY, endX;
    getSelectionBounds(startY, startX, endY, endX);

    if(row < startY || row > endY)
        return false;
    if(row == startY && row == endY)
        return col >= startX && col <= endX;
    if(row == startY)
        return col >= startX;
    if(row == endY)
        return col <= endX;
    return true;
}

bool Editor::isInVisualBlock(int row, int col)
{
    if(currentMode != VISUAL_BLOCK)
        return false;

    int startY, startX, endY, endX;
    getVisualBlockBounds(startY, startX, endY, endX);

    return row >= startY && row <= endY && col >= startX && col <= endX;
}

void Editor::getVisualBlockBounds(int& startY, int& startX, int& endY,
                                  int& endX)
{
    startY = std::min(currentBuffer->visualBlockStartY,
                      currentBuffer->visualBlockEndY);
    endY = std::max(currentBuffer->visualBlockStartY,
                    currentBuffer->visualBlockEndY);
    startX = std::min(currentBuffer->visualBlockStartX,
                      currentBuffer->visualBlockEndX);
    endX = std::max(currentBuffer->visualBlockStartX,
                    currentBuffer->visualBlockEndX);
}

void Editor::getSelectionBounds(int& startY, int& startX, int& endY, int& endX)
{
    if(currentBuffer->visualStartY < currentBuffer->visualEndY ||
       (currentBuffer->visualStartY == currentBuffer->visualEndY &&
        currentBuffer->visualStartX <= currentBuffer->visualEndX))
    {
        startY = currentBuffer->visualStartY;
        startX = currentBuffer->visualStartX;
        endY = currentBuffer->visualEndY;
        endX = currentBuffer->visualEndX;
    }
    else
    {
        startY = currentBuffer->visualEndY;
        startX = currentBuffer->visualEndX;
        endY = currentBuffer->visualStartY;
        endX = currentBuffer->visualStartX;
    }
}

void Editor::deleteVisualBlock()
{
    int startY, startX, endY, endX;
    getVisualBlockBounds(startY, startX, endY, endX);

    // Delete characters in the block from each line
    for(int row = startY; row <= endY && row < lines->size(); row++)
    {
        std::string& line = (*lines)[row];
        if(startX < line.length())
        {
            int deleteEnd = std::min(endX + 1, (int)line.length());
            line.erase(startX, deleteEnd - startX);
        }
    }

    *cursorY = startY;
    *cursorX = startX;
    if(*cursorX > (*lines)[*cursorY].length())
        *cursorX = (*lines)[*cursorY].length();

    *dirty = true;
    saveState();
    setMode(NORMAL);
    needsFullRedraw = true;
    setStatusMessage("Block deleted");
}

void Editor::yankVisualBlock()
{
    int startY, startX, endY, endX;
    getVisualBlockBounds(startY, startX, endY, endX);

    yankBuffer.clear();

    // Yank the block - store with special marker to indicate block yank
    for(int row = startY; row <= endY && row < lines->size(); row++)
    {
        const std::string& line = (*lines)[row];
        if(startX < line.length())
        {
            int yankEnd = std::min(endX + 1, (int)line.length());
            yankBuffer += line.substr(startX, yankEnd - startX);
        }
        yankBuffer += "\n";
    }

    // Add special marker to indicate this is a block yank
    yankBuffer = "\x02" + yankBuffer; // STX character as block marker

    setStatusMessage("Block yanked");
}

void Editor::changeVisualBlock()
{
    currentBuffer->visualBlockInsertText.clear();
    visualBlockChanging = true;

    int startY, startX, endY, endX;
    getVisualBlockBounds(startY, startX, endY, endX);

    for(int row = startY; row <= endY && row < lines->size(); row++)
    {
        std::string& line = (*lines)[row];
        if(startX < line.length())
        {
            int deleteEnd = std::min(endX + 1, (int)line.length());
            line.erase(startX, deleteEnd - startX);
        }
    }

    *cursorY = startY;
    *cursorX = startX;

    setMode(INSERT);
}

void Editor::applyVisualBlockInsert()
{
    // Apply the inserted text to all lines in the visual block range
    if(currentBuffer->visualBlockInsertText.empty())
        return;

    int startY = currentBuffer->visualBlockStartY;
    int endY = currentBuffer->visualBlockEndY;
    int insertX = currentBuffer->visualBlockStartX;

    // Apply to all lines except the current one (already has the text)
    for(int row = startY; row <= endY && row < lines->size(); row++)
    {
        if(row == *cursorY)
            continue; // Skip current line, it already has the inserted text

        std::string& line = (*lines)[row];
        // Pad with spaces if line is too short
        while(line.length() < insertX)
        {
            line += " ";
        }
        line.insert(insertX, currentBuffer->visualBlockInsertText);
    }

    *dirty = true;
    saveState();
    needsFullRedraw = true;
    setStatusMessage("Block insert applied to " +
                     std::to_string(endY - startY + 1) + " lines");
}

void Editor::deleteSelection()
{
    if(currentMode == VISUAL_LINE)
    {
        int startY =
            std::min(currentBuffer->visualStartY, currentBuffer->visualEndY);
        int endY =
            std::max(currentBuffer->visualStartY, currentBuffer->visualEndY);

        yankSelection();

        for(int i = endY; i >= startY; i--)
        {
            lines->erase(lines->begin() + i);
        }

        if(lines->empty())
        {
            lines->push_back("");
        }

        *cursorY = std::min(startY, (int)lines->size() - 1);
        *cursorX = 0;
    }
    else
    {
        int startY, startX, endY, endX;
        getSelectionBounds(startY, startX, endY, endX);

        if(startY == endY)
        {
            (*lines)[startY].erase(startX, endX - startX + 1);
        }
        else
        {
            (*lines)[startY] = (*lines)[startY].substr(0, startX) +
                               (*lines)[endY].substr(endX + 1);
            for(int i = endY; i > startY; i--)
            {
                lines->erase(lines->begin() + i);
            }
        }

        *cursorY = startY;
        *cursorX = startX;
    }

    *dirty = true;
    needsFullRedraw = true;
}

// Search functions
std::string Editor::toLowerCase(const std::string& str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

int Editor::getLineIndent(int line)
{
    if(line < 0 || line >= (int)lines->size())
        return 0;

    const std::string& text = (*lines)[line];
    int indent = 0;
    for(char c : text)
    {
        if(c == ' ')
            indent++;
        else if(c == '\t')
            indent += 4; // Treat tab as 4 spaces
        else
            break;
    }
    return indent;
}

void Editor::indentLine(int line, int spaces)
{
    if(line < 0 || line >= (int)lines->size())
        return;

    std::string& text = (*lines)[line];

    // Remove existing indentation
    size_t firstNonSpace = 0;
    while(firstNonSpace < text.length() &&
          (text[firstNonSpace] == ' ' || text[firstNonSpace] == '\t'))
    {
        firstNonSpace++;
    }

    // Build new indentation
    std::string newIndent(spaces, ' ');
    text = newIndent + text.substr(firstNonSpace);
    *dirty = true;
}

// Auto-indent a line based on the previous line and C++ syntax rules
void Editor::autoIndentLine(int line)
{
    if(line < 0 || line >= (int)lines->size())
        return;

    // Get the content of current line (without leading spaces)
    std::string currentLine = (*lines)[line];
    size_t firstNonSpace = currentLine.find_first_not_of(" \t");
    if(firstNonSpace != std::string::npos)
        currentLine = currentLine.substr(firstNonSpace);
    else
        currentLine = "";

    // Start with previous line's indent
    int baseIndent = 0;
    if(line > 0)
    {
        baseIndent = getLineIndent(line - 1);

        // Check if previous line ends with { or starts a block
        const std::string& prevLine = (*lines)[line - 1];
        size_t lastNonSpace = prevLine.find_last_not_of(" \t\r\n");
        if(lastNonSpace != std::string::npos)
        {
            char lastChar = prevLine[lastNonSpace];
            if(lastChar == '{')
            {
                baseIndent += 4; // Increase indent after opening brace
            }
            else if(lastChar == ':' &&
                    (prevLine.find("public") != std::string::npos ||
                     prevLine.find("private") != std::string::npos ||
                     prevLine.find("protected") != std::string::npos ||
                     prevLine.find("case") != std::string::npos ||
                     prevLine.find("default") != std::string::npos))
            {
                baseIndent += 4; // Increase indent after class access
                                 // specifiers or case labels
            }
        }
    }

    // Check if current line starts with closing brace or special keywords
    if(!currentLine.empty())
    {
        if(currentLine[0] == '}')
        {
            baseIndent = std::max(
                0, baseIndent - 4); // Decrease indent for closing brace
        }
        else if(currentLine.find("public:") == 0 ||
                currentLine.find("private:") == 0 ||
                currentLine.find("protected:") == 0)
        {
            // Access specifiers typically have less indent than class members
            if(line > 0 && baseIndent >= 4)
                baseIndent -= 4;
        }
        else if(currentLine.find("case ") == 0 ||
                currentLine.find("default:") == 0)
        {
            // Case labels typically align with switch
            if(baseIndent >= 4)
                baseIndent -= 4;
        }
    }

    indentLine(line, baseIndent);
}

// Auto-indent a range of lines
void Editor::autoIndentRange(int startLine, int endLine)
{
    if(startLine > endLine)
        std::swap(startLine, endLine);

    startLine = std::max(0, startLine);
    endLine = std::min((int)lines->size() - 1, endLine);

    for(int i = startLine; i <= endLine; i++)
    {
        autoIndentLine(i);
    }

    *dirty = true;
    needsFullRedraw = true;
}

// Syntax highlighting functions
bool Editor::isCppFile() const
{
    if(filename->empty())
        return false;

    size_t dotPos = filename->find_last_of('.');
    if(dotPos == std::string::npos)
    {
        // No extension - check if it's a C++ standard library header
        // These are typically in paths like:
        //   /usr/include/c++/13/vector
        //   /Library/Developer/.../usr/include/c++/v1/cstdio
        //   /opt/homebrew/include/c++/...
        const std::string& path = *filename;
        if(path.find("/c++/") != std::string::npos ||
           path.find("/bits/") != std::string::npos ||
           path.find("/ext/") != std::string::npos ||
           path.find("/__") !=
               std::string::npos) // libc++ internal headers like __memory
        {
            return true;
        }
        return false;
    }

    std::string ext = filename->substr(dotPos);

    // Debug: uncomment to see what extension is being checked
    // FILE* f = fopen("/tmp/editor_debug.txt", "a");
    // if (f) { fprintf(f, "Checking ext: '%s' from file: '%s'\n", ext.c_str(),
    // filename->c_str()); fclose(f); }

    return (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" ||
            ext == ".hpp" || ext == ".hxx" || ext == ".c" || ext == ".C" ||
            ext == ".mla");
}

bool Editor::isMlaFile() const
{
    if(filename->empty())
        return false;

    size_t dotPos = filename->find_last_of('.');
    if(dotPos == std::string::npos)
        return false;

    std::string ext = filename->substr(dotPos);
    return (ext == ".mla");
}

std::string Editor::getColorCode(TokenType type) const
{
    switch(type)
    {
    case TOKEN_KEYWORD:
        return Terminal::FG_MAGENTA; // Magenta for keywords
    case TOKEN_TYPE:
        return Terminal::FG_CYAN; // Cyan for types
    case TOKEN_STRING:
        return Terminal::FG_GREEN; // Green for strings
    case TOKEN_CHAR:
        return Terminal::FG_GREEN; // Green for chars
    case TOKEN_COMMENT:
        return Terminal::FG_BRIGHT_BLACK; // Bright black (gray) for comments
    case TOKEN_PREPROCESSOR:
        return Terminal::FG_YELLOW; // Yellow for preprocessor
    case TOKEN_NUMBER:
        return Terminal::FG_RED; // Red for numbers
    case TOKEN_OPERATOR:
        return Terminal::FG_BRIGHT_YELLOW; // Bright yellow for operators
    case TOKEN_FUNCTION:
        return Terminal::FG_BRIGHT_BLUE; // Bright blue for functions
    default:
        return Terminal::FG_DEFAULT; // Default color
    }
}

std::vector<Token> Editor::tokenizeLine(const std::string& line,
                                        bool& inBlockComment)
{
    std::vector<Token> tokens;

    // C++ keywords
    static const std::unordered_set<std::string> keywords = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
        "break", "case", "catch", "class", "compl", "concept", "const",
        "consteval", "constexpr", "constinit", "const_cast", "continue",
        "co_await", "co_return", "co_yield", "decltype", "default", "delete",
        "do", "dynamic_cast", "else", "enum", "explicit", "export", "extern",
        "false", "for", "friend", "goto", "if", "inline", "mutable",
        "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator",
        "or", "or_eq", "private", "protected", "public", "reflexpr", "register",
        "reinterpret_cast", "requires", "return", "sizeof", "static",
        "static_assert", "static_cast", "struct", "switch", "synchronized",
        "template", "this", "thread_local", "throw", "true", "try", "typedef",
        "typeid", "typename", "union", "using", "virtual", "volatile", "while",
        "xor", "xor_eq", "override", "final",
        // MLA keywords
        "fn", "pub", "impl", "let", "var", "mod", "use", "in"};

    // C++ types
    static const std::unordered_set<std::string> types = {
        // Fundamental types
        "bool", "char", "char8_t", "char16_t", "char32_t", "double", "float",
        "int", "long", "short", "signed", "unsigned", "void", "wchar_t",
        "size_t", "ptrdiff_t", "nullptr_t", "int8_t", "int16_t", "int32_t",
        "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "intptr_t",
        "uintptr_t", "intmax_t", "uintmax_t",

        // STL containers
        "std::vector", "std::list", "std::deque", "std::array",
        "std::forward_list", "std::map", "std::set", "std::multimap",
        "std::multiset", "std::unordered_map", "std::unordered_set",
        "std::unordered_multimap", "std::unordered_multiset", "std::stack",
        "std::queue", "std::priority_queue", "std::pair", "std::tuple",

        // STL strings
        "std::string", "std::wstring", "std::u8string", "std::u16string",
        "std::u32string", "std::string_view", "std::wstring_view",
        "std::u8string_view", "std::u16string_view", "std::u32string_view",

        // Smart pointers
        "std::unique_ptr", "std::shared_ptr", "std::weak_ptr", "std::auto_ptr",

        // Function objects
        "std::function", "std::bind", "std::reference_wrapper",

        // Utility types
        "std::optional", "std::variant", "std::any", "std::expected",
        "std::bitset", "std::complex",

        // Stream types
        "std::iostream", "std::istream", "std::ostream", "std::stringstream",
        "std::istringstream", "std::ostringstream", "std::fstream",
        "std::ifstream", "std::ofstream", "std::cout", "std::cin", "std::cerr",
        "std::clog",

        // Iterator types
        "std::iterator", "std::reverse_iterator", "std::move_iterator",
        "std::back_insert_iterator", "std::front_insert_iterator",
        "std::insert_iterator",

        // Thread types
        "std::thread", "std::mutex", "std::recursive_mutex", "std::timed_mutex",
        "std::lock_guard", "std::unique_lock", "std::shared_lock",
        "std::condition_variable", "std::condition_variable_any", "std::future",
        "std::promise", "std::packaged_task", "std::async", "std::atomic",
        "std::atomic_bool", "std::atomic_int",

        // Chrono types
        "std::chrono::duration", "std::chrono::time_point",
        "std::chrono::system_clock", "std::chrono::steady_clock",
        "std::chrono::high_resolution_clock", "std::chrono::seconds",
        "std::chrono::milliseconds", "std::chrono::microseconds",
        "std::chrono::nanoseconds",

        // Random types
        "std::mt19937", "std::mt19937_64", "std::random_device",
        "std::uniform_int_distribution", "std::uniform_real_distribution",
        "std::normal_distribution", "std::bernoulli_distribution",
        "std::discrete_distribution", "std::poisson_distribution",

        // Exception types
        "std::exception", "std::runtime_error", "std::logic_error",
        "std::invalid_argument", "std::out_of_range", "std::overflow_error",

        // Type traits
        "std::is_same", "std::is_integral", "std::is_floating_point",
        "std::is_pointer", "std::is_reference", "std::is_const",
        "std::enable_if", "std::conditional", "std::decay",
        "std::remove_reference", "std::remove_const", "std::remove_pointer",

        // Algorithm types
        "std::less", "std::greater", "std::equal_to", "std::not_equal_to",
        "std::plus", "std::minus", "std::multiplies", "std::divides",

        // Numeric types
        "std::numeric_limits", "std::accumulate", "std::partial_sum",

        // Other common types
        "std::initializer_list", "std::type_info", "std::bad_alloc",
        "std::nothrow_t", "std::align_val_t", "std::byte",

        // MLA types
        "int", "float", "double", "bool", "string", "list", "map", "tuple",
        "void", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "print",
        "println", "eprint", "eprintln"};

    int i = 0;
    int len = line.length();

    while(i < len)
    {
        // Skip whitespace
        while(i < len && std::isspace(line[i]))
            i++;

        if(i >= len)
            break;

        // Check for block comment continuation
        if(inBlockComment)
        {
            int start = i;
            while(i < len &&
                  !(i < len - 1 && line[i] == '*' && line[i + 1] == '/'))
                i++;

            if(i < len - 1 && line[i] == '*' && line[i + 1] == '/')
            {
                i += 2;
                inBlockComment = false;
            }

            tokens.push_back({TOKEN_COMMENT, start, i - start});
            continue;
        }

        // Preprocessor directives
        if(i == 0 && line[i] == '#')
        {
            tokens.push_back({TOKEN_PREPROCESSOR, i, len - i});
            break;
        }

        // Line comments
        if(i < len - 1 && line[i] == '/' && line[i + 1] == '/')
        {
            tokens.push_back({TOKEN_COMMENT, i, len - i});
            break;
        }

        // Block comments
        if(i < len - 1 && line[i] == '/' && line[i + 1] == '*')
        {
            int start = i;
            i += 2;
            while(i < len - 1 && !(line[i] == '*' && line[i + 1] == '/'))
                i++;

            if(i < len - 1 && line[i] == '*' && line[i + 1] == '/')
            {
                i += 2;
            }
            else
            {
                inBlockComment = true;
                i = len;
            }

            tokens.push_back({TOKEN_COMMENT, start, i - start});
            continue;
        }

        // String literals
        if(line[i] == '"')
        {
            int start = i;
            i++;
            while(i < len && line[i] != '"')
            {
                if(line[i] == '\\' && i + 1 < len)
                    i += 2;
                else
                    i++;
            }
            if(i < len)
                i++;
            tokens.push_back({TOKEN_STRING, start, i - start});
            continue;
        }

        // Character literals
        if(line[i] == '\'')
        {
            int start = i;
            i++;
            while(i < len && line[i] != '\'')
            {
                if(line[i] == '\\' && i + 1 < len)
                    i += 2;
                else
                    i++;
            }
            if(i < len)
                i++;
            tokens.push_back({TOKEN_CHAR, start, i - start});
            continue;
        }

        // Numbers
        if(std::isdigit(line[i]) ||
           (line[i] == '.' && i + 1 < len && std::isdigit(line[i + 1])))
        {
            int start = i;
            bool hasHex = false;

            // Check for hex prefix
            if(line[i] == '0' && i + 1 < len &&
               (line[i + 1] == 'x' || line[i + 1] == 'X'))
            {
                hasHex = true;
                i += 2;
            }

            while(i < len &&
                  (std::isdigit(line[i]) ||
                   (hasHex && std::isxdigit(line[i])) || line[i] == '.' ||
                   line[i] == 'e' || line[i] == 'E' || line[i] == 'f' ||
                   line[i] == 'F' || line[i] == 'u' || line[i] == 'U' ||
                   line[i] == 'l' || line[i] == 'L'))
            {
                i++;
            }

            tokens.push_back({TOKEN_NUMBER, start, i - start});
            continue;
        }

        // Identifiers and keywords
        if(std::isalpha(line[i]) || line[i] == '_')
        {
            int start = i;
            while(i < len &&
                  (std::isalnum(line[i]) || line[i] == '_' || line[i] == ':'))
                i++;

            std::string word = line.substr(start, i - start);

            // Check if it's followed by '(' to identify functions
            int j = i;
            while(j < len && std::isspace(line[j]))
                j++;

            TokenType type = TOKEN_NORMAL;
            if(j < len && line[j] == '(')
            {
                type = TOKEN_FUNCTION;
            }
            else if(keywords.find(word) != keywords.end())
            {
                type = TOKEN_KEYWORD;
            }
            else if(types.find(word) != types.end())
            {
                type = TOKEN_TYPE;
            }
            else
            {
                // Check if previous token was 'struct', 'class', 'enum', or ':'
                // to identify type names in definitions/inheritance
                if(!tokens.empty())
                {
                    const Token& prevToken = tokens.back();
                    std::string prevWord =
                        line.substr(prevToken.start, prevToken.length);
                    if(prevWord == "struct" || prevWord == "class" ||
                       prevWord == "enum" || prevWord == ":" ||
                       prevWord == "->")
                    {
                        type = TOKEN_TYPE;
                    }
                }
            }

            tokens.push_back({type, start, i - start});
            continue;
        }

        // Operators and punctuation
        if(std::strchr("+-*/%=<>!&|^~?:.,;()[]{}\\", line[i]))
        {
            int start = i;
            i++;

            // Multi-character operators
            if(i < len && std::strchr("=<>+-&|*/%:.", line[i - 1]))
            {
                if((line[i - 1] == '+' && line[i] == '+') ||
                   (line[i - 1] == '-' && line[i] == '-') ||
                   (line[i - 1] == '&' && line[i] == '&') ||
                   (line[i - 1] == '|' && line[i] == '|') ||
                   (line[i - 1] == '=' && line[i] == '=') ||
                   (line[i - 1] == '!' && line[i] == '=') ||
                   (line[i - 1] == '<' && line[i] == '=') ||
                   (line[i - 1] == '>' && line[i] == '=') ||
                   (line[i - 1] == '<' && line[i] == '<') ||
                   (line[i - 1] == '>' && line[i] == '>') ||
                   (line[i - 1] == '-' && line[i] == '>') ||
                   (line[i - 1] == ':' && line[i] == ':') ||
                   (line[i - 1] == '.' &&
                    line[i] == '.') || // MLA range operator
                   (line[i - 1] == '+' && line[i] == '=') ||
                   (line[i - 1] == '-' && line[i] == '=') ||
                   (line[i - 1] == '*' && line[i] == '=') ||
                   (line[i - 1] == '/' && line[i] == '=') ||
                   (line[i - 1] == '%' && line[i] == '='))
                {
                    i++;
                }
            }

            tokens.push_back({TOKEN_OPERATOR, start, i - start});
            continue;
        }

        // Default - unknown character
        tokens.push_back({TOKEN_NORMAL, i, 1});
        i++;
    }

    return tokens;
}

void Editor::renderLineWithSyntax(std::string& output, const std::string& line,
                                  int start, int len, int fileRow)
{
    static bool inBlockComment = false;

    // Reset block comment state at start of file
    if(fileRow == 0)
        inBlockComment = false;

    // Get tokens for the line
    bool blockCommentState = inBlockComment;
    std::vector<Token> tokens = tokenizeLine(line, blockCommentState);

    // Track what parts of the visible line need highlighting
    std::vector<TokenType> charColors(len, TOKEN_NORMAL);

    // Map tokens to visible characters
    for(const auto& token : tokens)
    {
        int tokenEnd = token.start + token.length;
        for(int pos = token.start; pos < tokenEnd; pos++)
        {
            int visiblePos = pos - start;
            if(visiblePos >= 0 && visiblePos < len)
            {
                charColors[visiblePos] = token.type;
            }
        }
    }

    // Render with colors
    TokenType currentColor = TOKEN_NORMAL;
    for(int x = 0; x < len; x++)
    {
        int col = x + start;

        // Check for selection or search highlighting first
        bool highlighted = false;
        if(isInSelection(fileRow, col) || isInVisualBlock(fileRow, col))
        {
            output += Terminal::STYLE_SELECTION; // Reverse video for selection
            highlighted = true;
        }
        else if(isInSearchMatch(fileRow, col))
        {
            output +=
                Terminal::STYLE_SEARCH_MATCH; // Yellow background for search
            highlighted = true;
        }

        // Apply syntax color if not highlighted
        if(!highlighted && charColors[x] != currentColor)
        {
            currentColor = charColors[x];
            output += getColorCode(currentColor);
        }

        output += line[col];

        if(highlighted)
        {
            output += Terminal::ESC_RESET_ALL; // Reset all attributes
            currentColor = TOKEN_NORMAL; // Need to reapply color after reset
        }
    }

    // Reset color at end of line
    if(currentColor != TOKEN_NORMAL)
    {
        output += Terminal::FG_DEFAULT;
    }

    inBlockComment = blockCommentState;
}

void Editor::startSearchForward()
{
    setMode(SEARCH_FORWARD);
    clearSearch();
}

void Editor::startSearchBackward()
{
    setMode(SEARCH_BACKWARD);
    clearSearch();
}

void Editor::findAllMatches()
{
    searchMatches.clear();
    if(searchQuery.empty())
        return;

    std::string lowerQuery = toLowerCase(searchQuery);

    for(int row = 0; row < lines->size(); row++)
    {
        std::string lowerLine = toLowerCase((*lines)[row]);
        size_t pos = 0;

        while((pos = lowerLine.find(lowerQuery, pos)) != std::string::npos)
        {
            SearchMatch match;
            match.row = row;
            match.col = pos;
            match.len = searchQuery.length();
            searchMatches.push_back(match);
            pos++;
        }
    }

    if(!searchMatches.empty())
    {
        setStatusMessage(std::to_string(searchMatches.size()) +
                         " matches found");
    }
    else
    {
        setStatusMessage("Pattern not found: " + searchQuery);
    }
}

void Editor::jumpToMatch(int index)
{
    if(index >= 0 && index < searchMatches.size())
    {
        currentMatchIndex = index;
        const SearchMatch& match = searchMatches[index];
        *cursorY = match.row;
        *cursorX = match.col;
        adjustViewport();
    }
}

void Editor::performSearch()
{
    if(searchQuery.empty())
    {
        searchQuery = currentBuffer->lastSearchQuery;
        searchForward = currentBuffer->lastSearchForward;
    }

    if(searchQuery.empty())
    {
        setStatusMessage("No previous search");
        return;
    }

    currentBuffer->lastSearchQuery = searchQuery;
    currentBuffer->lastSearchForward = searchForward;

    findAllMatches();

    if(searchMatches.empty())
    {
        return;
    }

    int bestMatch = -1;

    if(searchForward)
    {
        for(int i = 0; i < searchMatches.size(); i++)
        {
            const SearchMatch& match = searchMatches[i];
            if(match.row > *cursorY ||
               (match.row == *cursorY && match.col > *cursorX))
            {
                bestMatch = i;
                break;
            }
        }
        if(bestMatch == -1 && !searchMatches.empty())
        {
            bestMatch = 0;
            setStatusMessage("Search wrapped to top");
        }
    }
    else
    {
        for(int i = searchMatches.size() - 1; i >= 0; i--)
        {
            const SearchMatch& match = searchMatches[i];
            if(match.row < *cursorY ||
               (match.row == *cursorY && match.col < *cursorX))
            {
                bestMatch = i;
                break;
            }
        }
        if(bestMatch == -1 && !searchMatches.empty())
        {
            bestMatch = searchMatches.size() - 1;
            setStatusMessage("Search wrapped to bottom");
        }
    }

    if(bestMatch != -1)
    {
        jumpToMatch(bestMatch);
    }
}

void Editor::searchNext()
{
    if(searchMatches.empty())
    {
        if(!currentBuffer->lastSearchQuery.empty())
        {
            searchQuery = currentBuffer->lastSearchQuery;
            searchForward = currentBuffer->lastSearchForward;
            performSearch();
        }
        else
        {
            setStatusMessage("No previous search");
        }
        return;
    }

    int nextIndex = currentMatchIndex + 1;
    if(nextIndex >= searchMatches.size())
    {
        nextIndex = 0;
        setStatusMessage("Search wrapped to top");
    }
    jumpToMatch(nextIndex);
}

void Editor::searchPrevious()
{
    if(searchMatches.empty())
    {
        if(!currentBuffer->lastSearchQuery.empty())
        {
            searchQuery = currentBuffer->lastSearchQuery;
            searchForward = !currentBuffer->lastSearchForward;
            performSearch();
        }
        else
        {
            setStatusMessage("No previous search");
        }
        return;
    }

    int prevIndex = currentMatchIndex - 1;
    if(prevIndex < 0)
    {
        prevIndex = searchMatches.size() - 1;
        setStatusMessage("Search wrapped to bottom");
    }
    jumpToMatch(prevIndex);
}

void Editor::clearSearch()
{
    searchMatches.clear();
    currentMatchIndex = -1;
    searchQuery.clear();
    // Also clear the buffer's last search query
    if(currentBuffer)
    {
        currentBuffer->lastSearchQuery.clear();
    }
}

void Editor::cancelSearch()
{
    *cursorX = savedCursorX;
    *cursorY = savedCursorY;
    clearSearch();
    setMode(NORMAL);
    statusMessage.clear();
}

bool Editor::isInSearchMatch(int row, int col)
{
    for(const auto& match : searchMatches)
    {
        if(match.row == row && col >= match.col && col < match.col + match.len)
        {
            return true;
        }
    }
    return false;
}

// File browser functions
void Editor::openFileBrowser(const std::string& path)
{
    if(currentMode != FILE_BROWSER)
    {
        previousFile = *filename;
    }

    char resolvedPath[PATH_MAX];
    if(realpath(path.c_str(), resolvedPath))
    {
        currentDirectory = resolvedPath;
    }
    else
    {
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
        {
            currentDirectory = cwd;
        }
        else
        {
            currentDirectory = ".";
        }
    }

    loadDirectory(currentDirectory);

    if(fileList.empty())
    {
        setStatusMessage("Failed to load directory: " + currentDirectory);
        return;
    }

    setMode(FILE_BROWSER);
    browserCursor = 0;
    browserOffset = 0;
    needsFullRedraw = true;
}

void Editor::loadDirectory(const std::string& path)
{
    fileList.clear();

    DIR* dir = opendir(path.c_str());
    if(!dir)
    {
        dir = opendir(".");
        if(!dir)
        {
            setStatusMessage("Cannot open any directory!");
            return;
        }
        currentDirectory = ".";
    }

    if(currentDirectory != "/" && currentDirectory != "")
    {
        FileEntry parent;
        parent.name = "..";
        parent.path = currentDirectory + "/..";
        parent.isDirectory = true;
        parent.size = 0;
        parent.modTime = 0;
        fileList.push_back(parent);
    }

    struct dirent* entry;
    while((entry = readdir(dir)))
    {
        std::string name = entry->d_name;

        if(name == "." || name == "..")
            continue;

        if(!showHidden && name[0] == '.')
            continue;

        FileEntry fileEntry;
        fileEntry.name = name;
        fileEntry.path = currentDirectory + "/" + name;

        struct stat fileStat;
        if(stat(fileEntry.path.c_str(), &fileStat) == 0)
        {
            fileEntry.isDirectory = S_ISDIR(fileStat.st_mode);
            fileEntry.size = fileStat.st_size;
            fileEntry.modTime = fileStat.st_mtime;
        }
        else
        {
            fileEntry.isDirectory = (entry->d_type == DT_DIR);
            fileEntry.size = 0;
            fileEntry.modTime = 0;
        }

        fileList.push_back(fileEntry);
    }

    closedir(dir);
    sortFileList();
}

void Editor::sortFileList()
{
    std::sort(fileList.begin(), fileList.end(),
              [](const FileEntry& a, const FileEntry& b)
              {
                  if(a.name == "..")
                      return true;
                  if(b.name == "..")
                      return false;

                  if(a.isDirectory != b.isDirectory)
                      return a.isDirectory;

                  std::string aLower = a.name;
                  std::string bLower = b.name;
                  std::transform(aLower.begin(), aLower.end(), aLower.begin(),
                                 ::tolower);
                  std::transform(bLower.begin(), bLower.end(), bLower.begin(),
                                 ::tolower);
                  return aLower < bLower;
              });
}

void Editor::navigateTo(const FileEntry& entry)
{
    if(entry.isDirectory)
    {
        openFileBrowser(entry.path);
    }
    else
    {
        openFile(entry.path);
        setMode(NORMAL);
        setStatusMessage("Opened: " + entry.name);
    }
}

void Editor::toggleHidden()
{
    showHidden = !showHidden;
    loadDirectory(currentDirectory);
    setStatusMessage(showHidden ? "Showing hidden files"
                                : "Hiding hidden files");
}

std::string Editor::formatFileSize(size_t size)
{
    const char* units[] = {"B", "K", "M", "G", "T"};
    int unitIndex = 0;
    double displaySize = size;

    while(displaySize >= 1024 && unitIndex < 4)
    {
        displaySize /= 1024;
        unitIndex++;
    }

    std::stringstream ss;
    if(unitIndex == 0)
    {
        ss << std::setw(5) << size << units[unitIndex];
    }
    else
    {
        ss << std::fixed << std::setprecision(1) << std::setw(5) << displaySize
           << units[unitIndex];
    }

    return ss.str();
}

std::string Editor::formatFileTime(time_t time)
{
    char buffer[20];
    struct tm* timeinfo = localtime(&time);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", timeinfo);
    return std::string(buffer);
}

// Fuzzy Finder Implementation
void Editor::initializeFuzzyFind()
{
    if(!fuzzyInitialized)
    {
        allProjectFiles.clear();

        // Get current working directory as project root
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
        {
            collectProjectFiles(std::string(cwd));
        }

        fuzzyInitialized = true;
    }

    fuzzyQuery.clear();
    fuzzyCursor = 0;
    fuzzyOffset = 0;
    fuzzyMatches.clear();

    // Initially show all files
    for(const auto& file : allProjectFiles)
    {
        if(!file.isDirectory)
        {
            FuzzyMatch match;
            match.file = file;
            match.score = 0;
            fuzzyMatches.push_back(match);
        }
    }

    // Sort by path initially
    std::sort(fuzzyMatches.begin(), fuzzyMatches.end(),
              [](const FuzzyMatch& a, const FuzzyMatch& b)
              { return a.file.path < b.file.path; });
}

void Editor::collectProjectFiles(const std::string& dir, int depth)
{
    if(depth > 5)
        return; // Limit recursion depth

    DIR* d = opendir(dir.c_str());
    if(!d)
        return;

    struct dirent* entry;
    while((entry = readdir(d)))
    {
        std::string name = entry->d_name;

        // Skip hidden files and special directories
        if(name == "." || name == ".." || name[0] == '.')
            continue;

        // Skip common non-source directories
        if(name == "node_modules" || name == "build" || name == "dist" ||
           name == ".git" || name == "target" || name == "__pycache__")
            continue;

        std::string fullPath = dir + "/" + name;

        struct stat st;
        if(stat(fullPath.c_str(), &st) == 0)
        {
            FileEntry entry;
            entry.name = name;
            entry.path = fullPath;
            entry.isDirectory = S_ISDIR(st.st_mode);
            entry.size = st.st_size;
            entry.modTime = st.st_mtime;

            allProjectFiles.push_back(entry);

            if(entry.isDirectory)
            {
                collectProjectFiles(fullPath, depth + 1);
            }
        }
    }

    closedir(d);
}

int Editor::fuzzyScore(const std::string& needle, const std::string& haystack,
                       std::vector<int>& matchPositions)
{
    matchPositions.clear();

    if(needle.empty())
        return 0;
    if(needle.length() > haystack.length())
        return -1;

    int score = 0;
    int consecutiveBonus = 10;
    int separatorBonus = 30; // Bonus for matching after separator
    int camelBonus = 30;     // Bonus for matching camelCase
    int firstLetterBonus = 15;

    size_t needleIdx = 0;
    int prevMatchIdx = -1;
    bool prevWasSeparator = true;

    for(size_t i = 0; i < haystack.length() && needleIdx < needle.length(); i++)
    {
        char needleChar = std::tolower(needle[needleIdx]);
        char haystackChar = std::tolower(haystack[i]);

        if(needleChar == haystackChar)
        {
            matchPositions.push_back(i);

            score += 100; // Base score for match

            // Consecutive match bonus
            if(prevMatchIdx >= 0 && i == prevMatchIdx + 1)
            {
                score += consecutiveBonus;
            }

            // Separator bonus (after /, -, _, .)
            if(i > 0)
            {
                char prevChar = haystack[i - 1];
                if(prevChar == '/' || prevChar == '-' || prevChar == '_' ||
                   prevChar == '.')
                {
                    score += separatorBonus;
                }
            }

            // CamelCase bonus
            if(i > 0 && std::islower(haystack[i - 1]) &&
               std::isupper(haystack[i]))
            {
                score += camelBonus;
            }

            // First letter bonus
            if(i == 0)
            {
                score += firstLetterBonus;
            }

            // Exact case bonus
            if(needle[needleIdx] == haystack[i])
            {
                score += 5;
            }

            prevMatchIdx = i;
            needleIdx++;
        }
        else
        {
            // Penalty for gaps
            if(prevMatchIdx >= 0)
            {
                score -= (i - prevMatchIdx);
            }
        }
    }

    // All characters matched?
    if(needleIdx != needle.length())
    {
        return -1;
    }

    // Prefer shorter strings
    score -= haystack.length();

    return score;
}

void Editor::updateFuzzyMatches()
{
    fuzzyMatches.clear();

    if(fuzzyQuery.empty())
    {
        // Show all files when query is empty
        for(const auto& file : allProjectFiles)
        {
            if(!file.isDirectory)
            {
                FuzzyMatch match;
                match.file = file;
                match.score = 0;
                fuzzyMatches.push_back(match);
            }
        }
    }
    else
    {
        for(const auto& file : allProjectFiles)
        {
            if(file.isDirectory)
                continue;

            std::vector<int> positions;

            // Try matching against full path
            int pathScore = fuzzyScore(fuzzyQuery, file.path, positions);

            // Try matching against just filename
            std::vector<int> namePositions;
            int nameScore = fuzzyScore(fuzzyQuery, file.name, namePositions);

            // Use the better score
            int finalScore =
                std::max(pathScore, nameScore * 2); // Boost filename matches

            if(finalScore > 0)
            {
                FuzzyMatch match;
                match.file = file;
                match.score = finalScore;
                match.matchPositions =
                    (nameScore * 2 > pathScore) ? namePositions : positions;
                fuzzyMatches.push_back(match);
            }
        }

        // Sort by score
        std::sort(fuzzyMatches.begin(), fuzzyMatches.end(),
                  [](const FuzzyMatch& a, const FuzzyMatch& b)
                  { return a.score > b.score; });
    }

    // Reset cursor if it's out of bounds
    if(fuzzyCursor >= fuzzyMatches.size())
    {
        fuzzyCursor = 0;
        fuzzyOffset = 0;
    }
}

void Editor::drawFuzzyFind()
{
    std::string output;
    output.reserve(screenRows * screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;
    output += Terminal::ESC_CLEAR_LINE;

    // Header with search box
    output += Terminal::ESC_BOLD;
    output += "  Find File: ";
    output += Terminal::ESC_RESET_ALL;
    output += Terminal::FG_GREEN;
    output += fuzzyQuery;

    // Show cursor in search box
    output += Terminal::ESC_BLINK;
    output += "_";
    output += Terminal::ESC_BLINK_OFF;
    output += Terminal::FG_DEFAULT;

    output += Terminal::NEWLINE_CLEAR;
    output += Terminal::FG_BRIGHT_BLACK;
    output += "  [Enter: open] [Esc: cancel] [↑↓: navigate]";
    output += Terminal::FG_DEFAULT;
    output += Terminal::NEWLINE_CLEAR;
    output += Terminal::FG_BRIGHT_BLACK;

    // Show match count
    if(!fuzzyMatches.empty())
    {
        output += "  " + std::to_string(fuzzyMatches.size()) + " matches";
    }
    else if(!fuzzyQuery.empty())
    {
        output += "  No matches";
    }
    else
    {
        output += "  " + std::to_string(allProjectFiles.size()) + " files";
    }
    output += Terminal::FG_DEFAULT;

    int availableRows = screenRows - 3;

    // Draw matched files
    for(int i = 0; i < availableRows && i + fuzzyOffset < fuzzyMatches.size();
        i++)
    {
        output += Terminal::NEWLINE_CLEAR;

        int index = i + fuzzyOffset;
        const FuzzyMatch& match = fuzzyMatches[index];

        // Highlight current selection
        if(index == fuzzyCursor)
        {
            output += Terminal::STYLE_SELECTION; // Reverse video
        }

        output += "  ";

        // Get relative path
        char cwd[PATH_MAX];
        std::string displayPath = match.file.path;
        if(getcwd(cwd, sizeof(cwd)))
        {
            std::string cwdStr(cwd);
            if(displayPath.find(cwdStr) == 0)
            {
                displayPath = displayPath.substr(cwdStr.length() + 1);
            }
        }

        // Highlight matching characters if we have a query
        if(!fuzzyQuery.empty() && !match.matchPositions.empty())
        {
            size_t lastPos = 0;
            for(int pos : match.matchPositions)
            {
                if(pos >= 0 && pos < displayPath.length())
                {
                    // Non-matching part
                    if(pos > lastPos)
                    {
                        output += displayPath.substr(lastPos, pos - lastPos);
                    }

                    // Matching character - highlight in green
                    if(index != fuzzyCursor)
                    {
                        output += Terminal::STYLE_GREEN_BOLD; // Bright green
                    }
                    output += displayPath[pos];
                    if(index != fuzzyCursor)
                    {
                        output +=
                            Terminal::STYLE_RESET_GREEN_BOLD; // Reset color
                    }

                    lastPos = pos + 1;
                }
            }
            // Remaining non-matching part
            if(lastPos < displayPath.length())
            {
                output += displayPath.substr(lastPos);
            }
        }
        else
        {
            output += displayPath;
        }

        // Show file size on the right
        if(screenCols > 60)
        {
            std::string sizeStr = formatFileSize(match.file.size);
            int padding =
                screenCols - 2 - displayPath.length() - sizeStr.length() - 2;
            if(padding > 0)
            {
                output.append(padding, ' ');
            }
            output += Terminal::FG_BRIGHT_BLACK;
            output += sizeStr;
            output += Terminal::FG_DEFAULT;
        }

        output += Terminal::ESC_RESET_ALL; // Reset all attributes
    }

    // Fill remaining rows
    for(int i = fuzzyMatches.size() - fuzzyOffset; i < availableRows; i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        output += Terminal::FG_BLUE;
        output += "~";
        output += Terminal::FG_DEFAULT;
    }

    Terminal::write(output);
    Terminal::flush();
}

void Editor::selectFuzzyMatch()
{
    if(fuzzyCursor < fuzzyMatches.size())
    {
        const FuzzyMatch& match = fuzzyMatches[fuzzyCursor];
        openFile(match.file.path);
        setMode(NORMAL);
    }
}

void Editor::handleFuzzyFindMode(int c)
{
    switch(c)
    {
    case Terminal::ENTER:
        selectFuzzyMatch();
        break;

    case Terminal::ESC:
        setMode(NORMAL);
        needsFullRedraw = true;
        break;

    case Terminal::ARROW_DOWN:
    case Terminal::CTRL_N:
    case Terminal::CTRL_J:
        if(fuzzyCursor < fuzzyMatches.size() - 1)
        {
            fuzzyCursor++;
            if(fuzzyCursor >= fuzzyOffset + screenRows - 3)
            {
                fuzzyOffset = fuzzyCursor - screenRows + 4;
            }
        }
        break;

    case Terminal::ARROW_UP:
    case Terminal::CTRL_P:
    case Terminal::CTRL_K:
        if(fuzzyCursor > 0)
        {
            fuzzyCursor--;
            if(fuzzyCursor < fuzzyOffset)
            {
                fuzzyOffset = fuzzyCursor;
            }
        }
        break;

    case Terminal::BACKSPACE:
    case Terminal::DEL:
        if(!fuzzyQuery.empty())
        {
            fuzzyQuery.pop_back();
            updateFuzzyMatches();
        }
        break;

    case Terminal::CTRL_U: // Clear line
        fuzzyQuery.clear();
        updateFuzzyMatches();
        break;

    default:
        if(c >= 32 && c < 127) // Printable characters
        {
            fuzzyQuery += static_cast<char>(c);
            updateFuzzyMatches();
            fuzzyCursor = 0;
            fuzzyOffset = 0;
        }
        break;
    }
}

// Buffer Browser Implementation (fzf-style over open buffers)
void Editor::initializeBufferBrowser()
{
    bufferQuery.clear();
    bufferCursor = 0;
    bufferOffset = 0;
    updateBufferMatches();
}

void Editor::updateBufferMatches()
{
    bufferMatches.clear();

    // Build display strings once, then optionally fuzzy-score them.
    for(size_t i = 0; i < buffers.size(); i++)
    {
        BufferMatch m;
        m.bufferIndex = (int)i;

        // Display: "N  [+]  filename" (or [No Name])
        std::string name =
            buffers[i]->filename.empty() ? "[No Name]" : buffers[i]->filename;
        // prefer basename for readability but keep path for matching
        std::string base = name;
        if(!buffers[i]->filename.empty())
        {
            size_t lastSlash = buffers[i]->filename.find_last_of("/\\");
            if(lastSlash != std::string::npos)
                base = buffers[i]->filename.substr(lastSlash + 1);
        }

        m.display = std::to_string(i + 1);
        if((int)i == currentBufferIndex)
            m.display += " *";
        else
            m.display += "  ";

        if(buffers[i]->dirty)
            m.display += " [+] ";
        else
            m.display += "     ";

        // Show base name, and (if different) a dimmed path.
        m.display += base;
        if(!buffers[i]->filename.empty() && base != buffers[i]->filename)
        {
            m.display += "  (" + buffers[i]->filename + ")";
        }

        if(bufferQuery.empty())
        {
            m.score = 0;
            bufferMatches.push_back(std::move(m));
            continue;
        }

        std::vector<int> posDisplay;
        int s1 = fuzzyScore(bufferQuery, m.display, posDisplay);

        // Also match against raw filename (full path) and basename for better
        // results.
        std::vector<int> posName;
        int s2 = buffers[i]->filename.empty()
                     ? -1
                     : fuzzyScore(bufferQuery, buffers[i]->filename, posName);
        std::vector<int> posBase;
        int s3 = fuzzyScore(bufferQuery, base, posBase);

        int best = std::max({s1, s2, s3 * 2}); // boost basename matches

        if(best > 0)
        {
            m.score = best;
            // pick the positions that correspond to the display string where
            // possible
            if(best == s1)
                m.matchPositions = posDisplay;
            else if(best == s3 * 2)
                m.matchPositions =
                    posBase; // best effort (used only for highlighting)
            else
                m.matchPositions.clear();
            bufferMatches.push_back(std::move(m));
        }
    }

    if(!bufferQuery.empty())
    {
        std::sort(bufferMatches.begin(), bufferMatches.end(),
                  [](const BufferMatch& a, const BufferMatch& b)
                  { return a.score > b.score; });
    }
    else
    {
        // Keep buffers in natural order when query empty.
        std::sort(bufferMatches.begin(), bufferMatches.end(),
                  [](const BufferMatch& a, const BufferMatch& b)
                  { return a.bufferIndex < b.bufferIndex; });
    }

    if(bufferCursor >= (int)bufferMatches.size())
    {
        bufferCursor = 0;
        bufferOffset = 0;
    }
}

void Editor::drawBufferBrowser()
{
    std::string output;
    output.reserve(screenRows * screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;
    output += Terminal::ESC_CLEAR_LINE;

    // Header with search box
    output += Terminal::ESC_BOLD;
    output += "  Buffers: ";
    output += Terminal::ESC_RESET_ALL;
    output += Terminal::FG_GREEN;
    output += bufferQuery;
    output += Terminal::ESC_BLINK;
    output += "_";
    output += Terminal::ESC_BLINK_OFF;
    output += Terminal::FG_DEFAULT;

    output += Terminal::NEWLINE_CLEAR;
    output += Terminal::FG_BRIGHT_BLACK;
    output += "  [Enter: switch] [Esc: cancel] [↑↓: navigate]";
    output += Terminal::FG_DEFAULT;

    output += Terminal::NEWLINE_CLEAR;
    output += Terminal::FG_BRIGHT_BLACK;
    if(!bufferMatches.empty())
    {
        output += "  " + std::to_string(bufferMatches.size()) + " matches";
    }
    else if(!bufferQuery.empty())
    {
        output += "  No matches";
    }
    else
    {
        output += "  " + std::to_string(buffers.size()) + " buffers";
    }
    output += Terminal::FG_DEFAULT;

    int availableRows = screenRows - 3;

    for(int i = 0;
        i < availableRows && i + bufferOffset < (int)bufferMatches.size(); i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        int idx = i + bufferOffset;
        const BufferMatch& m = bufferMatches[idx];

        if(idx == bufferCursor)
            output += Terminal::STYLE_SELECTION;

        // Trim to screen width (leave leading two spaces)
        std::string line = "  " + m.display;
        if((int)line.length() > screenCols)
            line = line.substr(0, screenCols);

        output += line;
        output += Terminal::ESC_RESET_ALL;
    }

    // Fill remaining rows
    for(int i = (int)bufferMatches.size() - bufferOffset; i < availableRows;
        i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        output += Terminal::FG_BLUE;
        output += "~";
        output += Terminal::FG_DEFAULT;
    }

    Terminal::write(output);
    Terminal::flush();
}

void Editor::selectBufferMatch()
{
    if(bufferCursor >= 0 && bufferCursor < (int)bufferMatches.size())
    {
        int idx = bufferMatches[bufferCursor].bufferIndex;
        if(idx >= 0 && idx < (int)buffers.size())
        {
            switchToBuffer(idx);
        }
        setMode(NORMAL);
    }
}

void Editor::handleBufferBrowserMode(int c)
{
    switch(c)
    {
    case Terminal::ENTER:
        selectBufferMatch();
        break;

    case Terminal::ESC:
        setMode(NORMAL);
        needsFullRedraw = true;
        break;

    case Terminal::ARROW_DOWN:
    case Terminal::CTRL_N:
    case Terminal::CTRL_J:
        if(bufferCursor < (int)bufferMatches.size() - 1)
        {
            bufferCursor++;
            if(bufferCursor >= bufferOffset + screenRows - 3)
                bufferOffset = bufferCursor - screenRows + 4;
        }
        break;

    case Terminal::ARROW_UP:
    case Terminal::CTRL_P:
    case Terminal::CTRL_K:
        if(bufferCursor > 0)
        {
            bufferCursor--;
            if(bufferCursor < bufferOffset)
                bufferOffset = bufferCursor;
        }
        break;

    case Terminal::BACKSPACE:
    case Terminal::DEL:
        if(!bufferQuery.empty())
        {
            bufferQuery.pop_back();
            updateBufferMatches();
            bufferCursor = 0;
            bufferOffset = 0;
        }
        break;

    case Terminal::CTRL_U:
        bufferQuery.clear();
        updateBufferMatches();
        bufferCursor = 0;
        bufferOffset = 0;
        break;

    default:
        if(c >= 32 && c < 127)
        {
            bufferQuery += static_cast<char>(c);
            updateBufferMatches();
            bufferCursor = 0;
            bufferOffset = 0;
        }
        break;
    }
}

// Grep Search Implementation
void Editor::initializeGrepSearch()
{
    // Initialize project files if not already done
    if(!fuzzyInitialized)
    {
        allProjectFiles.clear();
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
        {
            collectProjectFiles(std::string(cwd));
        }
        fuzzyInitialized = true;
    }

    grepQuery.clear();
    grepMatches.clear();
    grepCursor = 0;
    grepOffset = 0;
    grepSearching = false;
}

std::string Editor::trimString(const std::string& str)
{
    size_t first = str.find_first_not_of(" \t\r\n");
    if(first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

bool Editor::isTextFile(const std::string& filepath)
{
    // Check by extension first
    std::string ext;
    size_t dotPos = filepath.find_last_of('.');
    if(dotPos != std::string::npos)
    {
        ext = filepath.substr(dotPos);
        // Common text file extensions
        if(ext == ".txt" || ext == ".cpp" || ext == ".c" || ext == ".h" ||
           ext == ".hpp" || ext == ".py" || ext == ".js" || ext == ".ts" ||
           ext == ".jsx" || ext == ".tsx" || ext == ".java" || ext == ".rs" ||
           ext == ".go" || ext == ".rb" || ext == ".php" || ext == ".sh" ||
           ext == ".bash" || ext == ".zsh" || ext == ".vim" || ext == ".lua" ||
           ext == ".md" || ext == ".markdown" || ext == ".rst" ||
           ext == ".tex" || ext == ".css" || ext == ".scss" || ext == ".html" ||
           ext == ".xml" || ext == ".json" || ext == ".yaml" || ext == ".yml" ||
           ext == ".toml" || ext == ".ini" || ext == ".conf" ||
           ext == ".config" || ext == ".log" || ext == ".cmake" ||
           ext == ".make" || ext == ".mk" || ext == ".am")
        {
            return true;
        }

        // Common binary extensions to skip
        if(ext == ".exe" || ext == ".o" || ext == ".so" || ext == ".a" ||
           ext == ".dll" || ext == ".dylib" || ext == ".bin" || ext == ".dat" ||
           ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" ||
           ext == ".bmp" || ext == ".ico" || ext == ".pdf" || ext == ".doc" ||
           ext == ".docx" || ext == ".xls" || ext == ".xlsx" || ext == ".ppt" ||
           ext == ".pptx" || ext == ".zip" || ext == ".tar" || ext == ".gz" ||
           ext == ".7z" || ext == ".rar" || ext == ".mp3" || ext == ".mp4" ||
           ext == ".avi" || ext == ".mov" || ext == ".wav" || ext == ".flac")
        {
            return false;
        }
    }

    // Check filename without extension
    size_t lastSlash = filepath.find_last_of("/");
    std::string filename = (lastSlash != std::string::npos)
                               ? filepath.substr(lastSlash + 1)
                               : filepath;

    // Common text files without extensions
    if(filename == "Makefile" || filename == "makefile" ||
       filename == "CMakeLists.txt" || filename == "Dockerfile" ||
       filename == "README" || filename == "LICENSE" ||
       filename == "CHANGELOG" || filename == "TODO")
    {
        return true;
    }

    return !isBinaryFile(filepath);
}

bool Editor::isBinaryFile(const std::string& filepath)
{
    std::ifstream file(filepath, std::ios::binary);
    if(!file)
        return true; // Can't open, assume binary

    // Read first 8192 bytes to check
    char buffer[8192];
    file.read(buffer, sizeof(buffer));
    size_t bytesRead = file.gcount();
    file.close();

    if(bytesRead == 0)
        return false; // Empty file, treat as text

    // Check for null bytes (strong indicator of binary)
    for(size_t i = 0; i < bytesRead; i++)
    {
        if(buffer[i] == '\0')
            return true;
    }

    // Check for high proportion of non-printable characters
    int nonPrintable = 0;
    for(size_t i = 0; i < bytesRead; i++)
    {
        unsigned char c = static_cast<unsigned char>(buffer[i]);
        if(c < 32 && c != '\t' && c != '\n' && c != '\r')
        {
            nonPrintable++;
        }
    }

    // If more than 30% non-printable, consider binary
    return (nonPrintable * 100 / bytesRead) > 30;
}

void Editor::highlightGrepMatches(const std::string& line,
                                  const std::string& query,
                                  std::vector<std::pair<int, int>>& ranges)
{
    ranges.clear();
    if(query.empty())
        return;

    std::string searchLine = line;
    std::string searchQuery = query;

    if(!grepCaseSensitive)
    {
        std::transform(searchLine.begin(), searchLine.end(), searchLine.begin(),
                       ::tolower);
        std::transform(searchQuery.begin(), searchQuery.end(),
                       searchQuery.begin(), ::tolower);
    }

    size_t pos = 0;
    while((pos = searchLine.find(searchQuery, pos)) != std::string::npos)
    {
        ranges.push_back({pos, pos + searchQuery.length()});
        pos += searchQuery.length();
    }
}

void Editor::goToDefinition()
{
    std::string symbol = getSymbolUnderCursor();
    if(symbol.empty())
    {
        setStatusMessage("gd: no symbol");
        return;
    }

#ifdef UVIM_ENABLE_CLANGD_LSP
    // Prefer clangd definition when enabled; fallback to heuristic gd
    // otherwise.
    if(isClangdLspEnabled())
    {
        // Sync buffer text (full-text change) before querying.
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }

        // Open or change in LSP.
        // If this file hasn't been seen yet, didOpen is fine; otherwise
        // didChange updates version. We'll conservatively call didChange after
        // didOpen attempt.
        lspClient->didChange(currentBuffer->filename, text);

        // LSP uses UTF-16 positions; lsp_client converts from utf8 byte offset.
        auto loc =
            lspClient->definition(currentBuffer->filename, *cursorY, *cursorX);
        if(loc)
        {
            pushJumpLocation();
            openFile(loc->path);

            // Set cursor position from LSP (both are 0-based)
            *cursorY = loc->line;
            *cursorX = loc->character;

            // Ensure cursor is within valid bounds
            if(*cursorY >= (int)lines->size())
                *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
            if(*cursorY >= 0 && *cursorX > (int)(*lines)[*cursorY].length())
                *cursorX = (*lines)[*cursorY].length();

            centerScreen();

            // Show a cleaner message for system headers
            std::string displayPath = loc->path;
            bool isSystemHeader =
                (loc->path.find("/usr/") == 0 || loc->path.find("/opt/") == 0 ||
                 loc->path.find("/Library/") == 0 ||
                 loc->path.find("/Applications/") == 0);
            if(isSystemHeader)
            {
                // Show just the filename for system headers
                size_t lastSlash = loc->path.rfind('/');
                if(lastSlash != std::string::npos)
                    displayPath = "<sys>/" + loc->path.substr(lastSlash + 1);
            }
            setStatusMessage("gd (clangd) → " + displayPath + ":" +
                             std::to_string(loc->line + 1));
            return;
        }
    }
#endif

    pushJumpLocation();

    int y, x;
    std::string current = currentBuffer->filename;
    std::string alternate = findAlternateFile(current);

    // 1️⃣ First: Search for LOCAL variable/parameter declaration (backwards from
    // cursor) This handles local variables and function parameters
    if(searchLocalDefinition(*lines, symbol, *cursorY, *cursorX, y, x))
    {
        // Make sure we're not jumping to ourselves
        if(y != *cursorY || x != *cursorX)
        {
            *cursorY = y;
            *cursorX = x;
            centerScreen();
            setStatusMessage("gd → local '" + symbol + "' at " +
                             std::to_string(y + 1) + ":" +
                             std::to_string(x + 1));
            return;
        }
    }

    // 2️⃣ Search for member variable in current file (class/struct members)
    if(searchMemberDefinition(*lines, symbol, y, x))
    {
        if(y != *cursorY || x != *cursorX)
        {
            *cursorY = y;
            *cursorX = x;
            centerScreen();
            setStatusMessage("gd → member '" + symbol + "' at " +
                             std::to_string(y + 1) + ":" +
                             std::to_string(x + 1));
            return;
        }
    }

    // 3️⃣ Search for function definition in alternate file (header <-> source)
    if(!alternate.empty())
    {
        openFile(alternate);

        if(searchDefinitionInBuffer(currentBuffer, symbol, y, x))
        {
            *cursorY = y;
            *cursorX = x;
            centerScreen();
            setStatusMessage("gd → " + alternate);
            return;
        }

        // Not found → go back
        openFile(current);
    }

    // 4️⃣ Fallback: Search for function definition in current file
    if(searchDefinitionInBuffer(currentBuffer, symbol, y, x))
    {
        *cursorY = y;
        *cursorX = x;
        centerScreen();
        setStatusMessage("gd (same file)");
        return;
    }

    setStatusMessage("gd: '" + symbol +
                     "' not found (curY=" + std::to_string(*cursorY) +
                     " curX=" + std::to_string(*cursorX) + ")");
}

void Editor::searchFileContent(const std::string& filepath)
{
    if(!isTextFile(filepath))
        return;

    std::ifstream file(filepath);
    if(!file)
        return;

    std::string line;
    int lineNum = 0;

    // Get relative path for display
    char cwd[PATH_MAX];
    std::string displayPath = filepath;
    if(getcwd(cwd, sizeof(cwd)))
    {
        std::string cwdStr(cwd);
        if(displayPath.find(cwdStr) == 0)
        {
            displayPath = displayPath.substr(cwdStr.length() + 1);
        }
    }

    // Extract filename
    size_t lastSlash = displayPath.find_last_of("/");
    std::string filename = (lastSlash != std::string::npos)
                               ? displayPath.substr(lastSlash + 1)
                               : displayPath;

    while(std::getline(file, line))
    {
        lineNum++;

        std::string searchLine = line;
        std::string searchQuery = grepQuery;

        if(!grepCaseSensitive)
        {
            std::transform(searchLine.begin(), searchLine.end(),
                           searchLine.begin(), ::tolower);
            std::transform(searchQuery.begin(), searchQuery.end(),
                           searchQuery.begin(), ::tolower);
        }

        if(searchLine.find(searchQuery) != std::string::npos)
        {
            GrepMatch match;
            match.filename = filename;
            match.filepath = filepath;
            match.lineNumber = lineNum;
            match.lineContent = trimString(line);
            highlightGrepMatches(line, grepQuery, match.highlightRanges);

            // Limit line content to reasonable length
            if(match.lineContent.length() > 200)
            {
                match.lineContent = match.lineContent.substr(0, 197) + "...";
            }

            grepMatches.push_back(match);

            // Limit total matches to prevent memory issues
            if(grepMatches.size() > 10000)
            {
                return;
            }
        }
    }
}

void Editor::performGrepSearch()
{
    if(grepQuery.empty())
    {
        grepMatches.clear();
        return;
    }

    grepMatches.clear();
    grepSearching = true;

    // Search through all project files
    for(const auto& file : allProjectFiles)
    {
        if(!file.isDirectory)
        {
            searchFileContent(file.path);
        }
    }

    grepSearching = false;

    // Reset cursor if needed
    if(grepCursor >= grepMatches.size())
    {
        grepCursor = 0;
        grepOffset = 0;
    }
}

void Editor::drawGrepSearch()
{
    std::string output;
    output.reserve(screenRows * screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;
    output += Terminal::ESC_CLEAR_LINE;

    // Header with search box
    output += Terminal::ESC_BOLD;
    output += "  Search in Files: ";
    output += Terminal::ESC_RESET_ALL;
    output += Terminal::FG_GREEN;
    output += grepQuery;

    // Show cursor in search box
    output += Terminal::ESC_BLINK;
    output += "_";
    output += Terminal::ESC_BLINK_OFF; // Blinking underscore
    output += Terminal::FG_DEFAULT;

    output += Terminal::NEWLINE_CLEAR;
    output += Terminal::FG_BRIGHT_BLACK;
    output += "  [Enter: open] [Esc: cancel] [Tab: toggle case] [↑↓: navigate]";
    output += Terminal::FG_DEFAULT;

    output += Terminal::NEWLINE_CLEAR;
    output += Terminal::FG_BRIGHT_BLACK;

    // Show match count and case sensitivity
    if(grepSearching)
    {
        output += "  Searching...";
    }
    else if(!grepMatches.empty())
    {
        output += "  " + std::to_string(grepMatches.size()) + " matches";
    }
    else if(!grepQuery.empty())
    {
        output += "  No matches";
    }
    else
    {
        output += "  Type to search file contents";
    }

    if(grepCaseSensitive)
    {
        output += " [Case Sensitive]";
    }
    output += Terminal::FG_DEFAULT;

    int availableRows = screenRows - 3;

    // Draw matched lines
    for(int i = 0; i < availableRows && i + grepOffset < grepMatches.size();
        i++)
    {
        output += Terminal::NEWLINE_CLEAR;

        int index = i + grepOffset;
        const GrepMatch& match = grepMatches[index];

        // Highlight current selection
        if(index == grepCursor)
        {
            output += Terminal::STYLE_SELECTION; // Reverse video
        }

        // Format: filename:linenum: content
        output += "  ";

        // Filename in cyan
        output += Terminal::FG_CYAN;
        output += match.filename;
        output += Terminal::FG_DEFAULT;

        // Line number in yellow
        output += ":";
        output += Terminal::FG_YELLOW;
        output += std::to_string(match.lineNumber);
        output += Terminal::FG_DEFAULT;
        output += ": ";

        // Line content with highlighted matches
        std::string content = match.lineContent;

        // Highlight matching text if not currently selected
        if(!match.highlightRanges.empty() && index != grepCursor)
        {
            size_t lastPos = 0;
            for(const auto& range : match.highlightRanges)
            {
                if(range.first < content.length())
                {
                    // Non-matching part
                    if(range.first > lastPos)
                    {
                        output +=
                            content.substr(lastPos, range.first - lastPos);
                    }

                    // Matching part - highlight in green
                    output += Terminal::STYLE_GREEN_BOLD; // Bright green
                    size_t endPos =
                        std::min((size_t)range.second, content.length());
                    output += content.substr(range.first, endPos - range.first);
                    output += Terminal::STYLE_RESET_GREEN_BOLD; // Reset color

                    lastPos = endPos;
                }
            }
            // Remaining non-matching part
            if(lastPos < content.length())
            {
                output += content.substr(lastPos);
            }
        }
        else
        {
            output += content;
        }

        output += Terminal::ESC_RESET_ALL; // Reset all attributes
    }

    // Fill remaining rows
    for(int i = grepMatches.size() - grepOffset; i < availableRows; i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        output += Terminal::FG_BLUE;
        output += "~";
        output += Terminal::FG_DEFAULT;
    }

    Terminal::write(output);
    Terminal::flush();
}

void Editor::selectGrepMatch()
{
    if(grepCursor < grepMatches.size())
    {
        const GrepMatch& match = grepMatches[grepCursor];

        // Open the file
        openFile(match.filepath);

        // Move to the specific line
        if(match.lineNumber > 0 && match.lineNumber <= lines->size())
        {
            *cursorY = match.lineNumber - 1;
            *cursorX = 0;

            // Try to position cursor at the first match
            if(!match.highlightRanges.empty())
            {
                *cursorX = match.highlightRanges[0].first;
            }

            // Center the screen on the match
            centerScreen();
        }

        setMode(NORMAL);
    }
}

void Editor::handleGrepSearchMode(int c)
{
    switch(c)
    {
    case Terminal::ENTER:
        selectGrepMatch();
        break;

    case Terminal::ESC:
        setMode(NORMAL);
        needsFullRedraw = true;
        break;

    case Terminal::TAB:
        grepCaseSensitive = !grepCaseSensitive;
        performGrepSearch();
        break;

    case Terminal::ARROW_DOWN:
    case Terminal::CTRL_N:
    case Terminal::CTRL_J:
        if(grepCursor < grepMatches.size() - 1)
        {
            grepCursor++;
            if(grepCursor >= grepOffset + screenRows - 3)
            {
                grepOffset = grepCursor - screenRows + 4;
            }
        }
        break;

    case Terminal::ARROW_UP:
    case Terminal::CTRL_K:
        if(grepCursor > 0)
        {
            grepCursor--;
            if(grepCursor < grepOffset)
            {
                grepOffset = grepCursor;
            }
        }
        break;

    case Terminal::BACKSPACE:
    case Terminal::DEL:
        if(!grepQuery.empty())
        {
            grepQuery.pop_back();
            performGrepSearch();
        }
        break;

    case Terminal::CTRL_U: // Clear line
        grepQuery.clear();
        grepMatches.clear();
        break;

    case Terminal::PAGE_DOWN:
        if(grepMatches.size() > 0)
        {
            int pageSize = screenRows - 3;
            grepCursor =
                std::min((int)grepMatches.size() - 1, grepCursor + pageSize);
            if(grepCursor >= grepOffset + pageSize)
            {
                grepOffset = grepCursor - pageSize + 1;
            }
        }
        break;

    case Terminal::PAGE_UP:
        if(grepMatches.size() > 0)
        {
            int pageSize = screenRows - 3;
            grepCursor = std::max(0, grepCursor - pageSize);
            if(grepCursor < grepOffset)
            {
                grepOffset = grepCursor;
            }
        }
        break;

    default:
        if(c >= 32 && c < 127) // Printable characters
        {
            grepQuery += static_cast<char>(c);
            performGrepSearch();
            grepCursor = 0;
            grepOffset = 0;
        }
        break;
    }
}

// Undo/Redo functions
void Editor::saveState()
{
    if(!currentBuffer)
        return;

    if(currentBuffer->undoIndex < currentBuffer->undoStack.size() - 1)
    {
        // We're truncating the undo stack, so invalidate saved index if it's
        // beyond current position
        if(currentBuffer->savedUndoIndex > currentBuffer->undoIndex)
        {
            currentBuffer->savedUndoIndex =
                -1; // Saved state no longer exists in stack
        }

        currentBuffer->undoStack.erase(currentBuffer->undoStack.begin() +
                                           currentBuffer->undoIndex + 1,
                                       currentBuffer->undoStack.end());
    }

    Buffer::EditState state;
    state.lines = *lines;
    state.cursorX = *cursorX;
    state.cursorY = *cursorY;
    currentBuffer->undoStack.push_back(state);
    currentBuffer->undoIndex++;

    if(currentBuffer->undoStack.size() > 100)
    {
        currentBuffer->undoStack.erase(currentBuffer->undoStack.begin());
        currentBuffer->undoIndex--;

        // Adjust savedUndoIndex if the saved state is still in the stack
        if(currentBuffer->savedUndoIndex >= 0)
        {
            currentBuffer->savedUndoIndex--;
            if(currentBuffer->savedUndoIndex < 0)
            {
                // The saved state was removed from the stack
                currentBuffer->savedUndoIndex = -1;
            }
        }
    }
}

void Editor::undo()
{
    if(!currentBuffer)
        return;

    if(currentBuffer->undoIndex > 0)
    {
        currentBuffer->undoIndex--;
        const Buffer::EditState& state =
            currentBuffer->undoStack[currentBuffer->undoIndex];
        *lines = state.lines;

        // Only restore cursor position if not undoing to the initial state
        // (The initial state always has cursor at 0,0 which is not useful)
        if(currentBuffer->undoIndex > 0)
        {
            *cursorX = state.cursorX;
            *cursorY = state.cursorY;
        }
        // else: keep current cursor position when undoing to initial state

        // Ensure cursor is within bounds
        if(*cursorY >= lines->size())
            *cursorY = lines->size() - 1;
        if(*cursorY < 0)
            *cursorY = 0;
        if(*cursorX > (*lines)[*cursorY].length())
            *cursorX = (*lines)[*cursorY].length();
        if(*cursorX < 0)
            *cursorX = 0;

        // Check if we're back at the saved state
        if(currentBuffer->undoIndex == currentBuffer->savedUndoIndex)
        {
            *dirty = false;
        }
        else
        {
            *dirty = true;
        }

        needsFullRedraw = true;
    }
    else
    {
        setStatusMessage("Already at oldest change");
    }
}

void Editor::redo()
{
    if(!currentBuffer)
        return;

    if(currentBuffer->undoIndex < currentBuffer->undoStack.size() - 1)
    {
        currentBuffer->undoIndex++;
        const Buffer::EditState& state =
            currentBuffer->undoStack[currentBuffer->undoIndex];
        *lines = state.lines;
        *cursorX = state.cursorX;
        *cursorY = state.cursorY;

        // Check if we're back at the saved state
        if(currentBuffer->undoIndex == currentBuffer->savedUndoIndex)
        {
            *dirty = false;
        }
        else
        {
            *dirty = true;
        }

        needsFullRedraw = true;
    }
    else
    {
        setStatusMessage("Already at newest change");
    }
}

// Viewport adjustment with scroll margins
void Editor::adjustViewport()
{
    if(*cursorY < *offsetY)
    {
        *offsetY = std::max(0, *cursorY);
    }
    else if(*cursorY >= *offsetY + screenRows)
    {
        *offsetY = std::min((int)lines->size() - screenRows,
                            *cursorY - screenRows + 1);
    }

    if(*cursorX < *offsetX)
    {
        *offsetX = *cursorX;
    }
    else if(*cursorX >= *offsetX + screenCols)
    {
        *offsetX = *cursorX - screenCols + 1;
    }
}

void Editor::centerScreen()
{
    // Center the cursor vertically on screen
    *offsetY = std::max(0, *cursorY - screenRows / 2);
    if(*offsetY + screenRows > lines->size())
    {
        *offsetY = std::max(0, (int)lines->size() - screenRows);
    }
}

// Drawing functions
void Editor::drawRows()
{
    for(int y = 0; y < screenRows; y++)
    {
        int fileRow = y + *offsetY;
        Terminal::clearLine();

        if(fileRow >= lines->size())
        {
            Terminal::write('~');
        }
        else
        {
            const std::string& line = (*lines)[fileRow];

            for(int x = 0; x < screenCols; x++)
            {
                int fileCol = x + *offsetX;
                char ch = (fileCol < line.length()) ? line[fileCol] : ' ';

                bool isCursor = (fileRow == *cursorY && fileCol == *cursorX);

                bool inBlock = (currentMode == VISUAL_BLOCK &&
                                isInVisualBlock(fileRow, fileCol));

                bool inVisual =
                    ((currentMode == VISUAL || currentMode == VISUAL_LINE) &&
                     isInSelection(fileRow, fileCol));

                Terminal::resetAttributes();

                /* ---------- VISUAL BLOCK ---------- */
                if(inBlock)
                {
                    if(isCursor)
                    {
                        // Cursor highlight (Neovim style)
                        Terminal::setBold();
                        Terminal::write(ch);
                    }
                    else
                    {
                        // Visual block highlight
                        Terminal::setReverse();
                        Terminal::write(ch);
                    }
                }
                /* ---------- VISUAL / VISUAL LINE ---------- */
                else if(inVisual)
                {
                    Terminal::setReverse();
                    Terminal::write(ch);
                }
                /* ---------- NORMAL ---------- */
                else
                {
                    Terminal::write(ch);
                }

                Terminal::resetAttributes();
            }
        }

        Terminal::write("\r\n");
    }
}

void Editor::drawStatusBar()
{
    Terminal::write(Terminal::NEWLINE_CLEAR);
    Terminal::write(Terminal::STYLE_SELECTION);

    std::string status = " " + getModeString() + " | ";

    // Add buffer indicator
    if(buffers.size() > 1)
    {
        status += "[" + std::to_string(currentBufferIndex + 1) + "/" +
                  std::to_string(buffers.size()) + "] ";
    }

    char rightStatus[32];
    snprintf(rightStatus, sizeof(rightStatus), " %d:%d ", *cursorY + 1,
             *cursorX + 1);

    // Calculate available space for filename
    int rightLen = strlen(rightStatus);
    int availableForFile = screenCols - status.length() - rightLen - 1;

    std::string displayName = filename->empty() ? "[No Name]" : *filename;
    if(*dirty)
        displayName += " [+]";

    // Truncate filename from the beginning if too long
    if((int)displayName.length() > availableForFile && availableForFile > 4)
    {
        displayName = "..." + displayName.substr(displayName.length() -
                                                 availableForFile + 3);
    }

    status += displayName;

    Terminal::write(status);

    int padding = screenCols - status.length() - rightLen;
    while(padding-- > 0)
        Terminal::write(' ');
    Terminal::write(rightStatus);

    Terminal::write(Terminal::ESC_RESET_ALL);
}

void Editor::drawMessageBar()
{
    Terminal::write(Terminal::NEWLINE_CLEAR);

    if(currentMode == COMMAND || currentMode == SEARCH_FORWARD ||
       currentMode == SEARCH_BACKWARD)
    {
        Terminal::write(commandBuffer);
        if(currentMode == SEARCH_FORWARD || currentMode == SEARCH_BACKWARD)
        {
            if(!searchMatches.empty())
            {
                std::string matchInfo =
                    " [" + std::to_string(currentMatchIndex + 1) + "/" +
                    std::to_string(searchMatches.size()) + "]";
                Terminal::write(matchInfo);
            }
            else if(!searchQuery.empty())
            {
                Terminal::write(" [No matches]");
            }
        }
    }
    else if(!statusMessage.empty())
    {
        int msglen = std::min((int)statusMessage.length(), screenCols);
        Terminal::write(statusMessage.substr(0, msglen));
    }
}

void Editor::drawFileBrowser()
{
    std::string output;
    output.reserve(screenRows * screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;

    // Draw header
    output += Terminal::ESC_CLEAR_LINE;
    output += Terminal::ESC_BOLD;
    output += "  " + currentDirectory;
    output += Terminal::ESC_RESET_ALL;
    output += Terminal::NEWLINE_CLEAR;
    output += Terminal::FG_BRIGHT_BLACK;
    output += "  [Enter: open] [q: quit] [.: toggle hidden] [-: parent]";
    output += Terminal::FG_DEFAULT;

    int availableRows = screenRows - 2;

    // Draw file entries
    for(int i = 0; i < availableRows && i + browserOffset < fileList.size();
        i++)
    {
        output += Terminal::NEWLINE_CLEAR;

        int index = i + browserOffset;
        const FileEntry& entry = fileList[index];

        if(index == browserCursor)
        {
            output += Terminal::STYLE_SELECTION;
        }

        if(entry.isDirectory)
        {
            output += Terminal::FG_BLUE;
            output += "  ▶ ";
        }
        else
        {
            std::string ext;
            size_t dotPos = entry.name.find_last_of('.');
            if(dotPos != std::string::npos)
            {
                ext = entry.name.substr(dotPos);
            }

            if(ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp")
            {
                output += Terminal::FG_GREEN;
                output += "  ◆ ";
            }
            else if(ext == ".txt" || ext == ".md")
            {
                output += Terminal::FG_WHITE;
                output += "  ○ ";
            }
            else if(ext == ".sh" || ext == ".py" || ext == ".js")
            {
                output += Terminal::FG_YELLOW;
                output += "  ★ ";
            }
            else
            {
                output += Terminal::FG_WHITE;
                output += "  ○ ";
            }
        }

        std::string displayName = entry.name;
        if(entry.isDirectory && entry.name != "..")
        {
            displayName += "/";
        }

        int maxNameLen = screenCols - 30;
        if(displayName.length() > maxNameLen)
        {
            displayName = displayName.substr(0, maxNameLen - 3) + "...";
        }

        output += displayName;

        if(entry.name != "..")
        {
            std::string info = formatFileSize(entry.size) + "  " +
                               formatFileTime(entry.modTime);

            int padding = screenCols - 5 - displayName.length() - info.length();
            if(padding > 0)
            {
                output.append(padding, ' ');
            }

            output += Terminal::FG_BRIGHT_BLACK;
            output += info;
        }

        output += Terminal::ESC_RESET_ALL;
    }

    // Fill remaining rows
    for(int i = fileList.size() - browserOffset; i < availableRows; i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        output += Terminal::FG_BLUE;
        output += "~";
        output += Terminal::FG_DEFAULT;
    }

    // Status bar
    output += Terminal::NEWLINE_CLEAR;
    output += Terminal::STYLE_SELECTION;

    std::string status = " BROWSE | " + currentDirectory;
    std::string right = " " + std::to_string(browserCursor + 1) + "/" +
                        std::to_string(fileList.size()) + " ";

    output += status;
    int padding = screenCols - status.length() - right.length();
    if(padding > 0)
    {
        output.append(padding, ' ');
    }
    output += right;
    output += Terminal::ESC_RESET_ALL;

    // Message bar
    output += Terminal::NEWLINE_CLEAR;
    if(!statusMessage.empty())
    {
        output += statusMessage.substr(
            0, std::min((size_t)screenCols, statusMessage.length()));
    }

    Terminal::write(output);
    Terminal::flush();
}

// Optimized drawing functions
void Editor::drawScrollUpdate(int scrollDelta)
{
    if(abs(scrollDelta) >= screenRows - 2)
    {
        drawFullScreen();
        return;
    }

    std::string output;
    output.reserve(screenRows * screenCols * 2);

    output += Terminal::ESC_HIDE_CURSOR;

    if(scrollDelta > 0)
    {
        output += Terminal::scrollRegion(1, screenRows);

        output += Terminal::ESC_CURSOR_HOME;
        for(int i = 0; i < scrollDelta; i++)
        {
            output += Terminal::ESC_DELETE_LINE;
        }

        for(int i = 0; i < scrollDelta; i++)
        {
            int row = screenRows - scrollDelta + i;
            int fileRow = row + *offsetY;

            output += Terminal::cursorPos(row + 1, 1);
            output += Terminal::ESC_CLEAR_LINE;

            if(fileRow < lines->size())
            {
                const std::string& line = (*lines)[fileRow];
                int start = *offsetX;
                int len = std::min((int)line.length() - start, screenCols);

                if(len > 0)
                {
                    if(isCppFile())
                    {
                        // Use syntax highlighting for C++ files
                        renderLineWithSyntax(output, line, start, len, fileRow);
                    }
                    else
                    {
                        bool needsHighlight = false;
                        for(int x = 0; x < len; x++)
                        {
                            int col = x + *offsetX;
                            if(isInSelection(fileRow, col) ||
                               isInVisualBlock(fileRow, col) ||
                               isInSearchMatch(fileRow, col))
                            {
                                needsHighlight = true;
                                break;
                            }
                        }

                        if(!needsHighlight)
                        {
                            output.append(line, start, len);
                        }
                        else
                        {
                            for(int x = 0; x < len; x++)
                            {
                                int col = x + *offsetX;
                                bool highlighted = false;

                                if(isInSelection(fileRow, col) ||
                                   isInVisualBlock(fileRow, col))
                                {
                                    output += Terminal::STYLE_SELECTION;
                                    highlighted = true;
                                }
                                else if(isInSearchMatch(fileRow, col))
                                {
                                    output += Terminal::STYLE_SEARCH_MATCH;
                                    highlighted = true;
                                }

                                output += line[col];

                                if(highlighted)
                                {
                                    output += Terminal::ESC_RESET_ALL;
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                output += Terminal::FG_BLUE;
                output += "~";
                output += Terminal::FG_DEFAULT;
            }
        }

        output += Terminal::ESC_RESET_SCROLL_REGION;
    }
    else if(scrollDelta < 0)
    {
        int absDelta = -scrollDelta;

        output += Terminal::scrollRegion(1, screenRows);

        output += Terminal::ESC_CURSOR_HOME;
        for(int i = 0; i < absDelta; i++)
        {
            output += Terminal::ESC_INSERT_LINE;
        }

        for(int i = 0; i < absDelta; i++)
        {
            int fileRow = i + *offsetY;

            output += Terminal::cursorPos(i + 1, 1);
            output += Terminal::ESC_CLEAR_LINE;

            if(fileRow < lines->size())
            {
                const std::string& line = (*lines)[fileRow];
                int start = *offsetX;
                int len = std::min((int)line.length() - start, screenCols);

                if(len > 0)
                {
                    if(isCppFile())
                    {
                        // Use syntax highlighting for C++ files
                        renderLineWithSyntax(output, line, start, len, fileRow);
                    }
                    else
                    {
                        bool needsHighlight = false;
                        for(int x = 0; x < len; x++)
                        {
                            int col = x + *offsetX;
                            if(isInSelection(fileRow, col) ||
                               isInVisualBlock(fileRow, col) ||
                               isInSearchMatch(fileRow, col))
                            {
                                needsHighlight = true;
                                break;
                            }
                        }

                        if(!needsHighlight)
                        {
                            output.append(line, start, len);
                        }
                        else
                        {
                            for(int x = 0; x < len; x++)
                            {
                                int col = x + *offsetX;
                                bool highlighted = false;

                                if(isInSelection(fileRow, col) ||
                                   isInVisualBlock(fileRow, col))
                                {
                                    output += Terminal::STYLE_SELECTION;
                                    highlighted = true;
                                }
                                else if(isInSearchMatch(fileRow, col))
                                {
                                    output += Terminal::STYLE_SEARCH_MATCH;
                                    highlighted = true;
                                }

                                output += line[col];

                                if(highlighted)
                                {
                                    output += Terminal::ESC_RESET_ALL;
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                output += Terminal::FG_BLUE;
                output += "~";
                output += Terminal::FG_DEFAULT;
            }
        }

        output += Terminal::ESC_RESET_SCROLL_REGION;
    }

    drawStatusBarQuick();
    drawMessageBarQuick(); // Add this to redraw message bar

    // Calculate cursor position
    int cursorRow, cursorCol;
    if(currentMode == COMMAND || currentMode == SEARCH_FORWARD ||
       currentMode == SEARCH_BACKWARD)
    {
        cursorRow = screenRows + 2;
        cursorCol = commandBuffer.length() + 1;
    }
    else
    {
        cursorRow = (*cursorY - *offsetY) + 1;
        cursorCol = (*cursorX - *offsetX) + 1;
    }
    output += Terminal::cursorPos(cursorRow, cursorCol);
    output += Terminal::ESC_SHOW_CURSOR;

    lastCursorScreenY = cursorRow;
    lastCursorScreenX = cursorCol;

    Terminal::write(output);
    Terminal::flush();
}

void Editor::drawStatusBarQuick()
{
    std::string output;
    output += Terminal::cursorPos(screenRows + 1, 1);

    output += Terminal::ESC_CLEAR_LINE;
    output += Terminal::STYLE_SELECTION;

    std::string statusLeft = " " + getModeString() + " | ";

    if(buffers.size() > 1)
    {
        statusLeft += "[" + std::to_string(currentBufferIndex + 1) + "/" +
                      std::to_string(buffers.size()) + "] ";
    }

    char rightStatus[32];
    snprintf(rightStatus, sizeof(rightStatus), " %d:%d ", *cursorY + 1,
             *cursorX + 1);

    // Calculate available space for filename
    int rightLen = strlen(rightStatus);
    int availableForFile = screenCols - statusLeft.length() - rightLen - 1;

    std::string displayName = filename->empty() ? "[No Name]" : *filename;
    if(*dirty)
        displayName += " [+]";

    // Truncate filename from the beginning if too long
    if((int)displayName.length() > availableForFile && availableForFile > 4)
    {
        displayName = "..." + displayName.substr(displayName.length() -
                                                 availableForFile + 3);
    }

    statusLeft += displayName;

    output += statusLeft;

    int padding = screenCols - statusLeft.length() - rightLen;
    if(padding > 0)
    {
        output.append(padding, ' ');
    }
    output += rightStatus;
    output += Terminal::ESC_RESET_ALL;

    Terminal::write(output);
}

void Editor::drawMessageBarQuick()
{
    std::string output;
    output += Terminal::cursorPos(screenRows + 2, 1);

    output += Terminal::ESC_CLEAR_LINE;

    if(currentMode == COMMAND || currentMode == SEARCH_FORWARD ||
       currentMode == SEARCH_BACKWARD)
    {
        output += commandBuffer;
        if(currentMode == SEARCH_FORWARD || currentMode == SEARCH_BACKWARD)
        {
            if(!searchMatches.empty())
            {
                output += " [" + std::to_string(currentMatchIndex + 1) + "/" +
                          std::to_string(searchMatches.size()) + "]";
            }
            else if(!searchQuery.empty())
            {
                output += " [No matches]";
            }
        }
    }
    else if(!statusMessage.empty())
    {
        int msglen = std::min((int)statusMessage.length(), screenCols);
        output.append(statusMessage, 0, msglen);
    }

    // Completion popup (clangd)
    drawCompletionPopup(output);

    Terminal::write(output);
}

void Editor::drawFullScreen()
{
    adjustViewport();

    std::string output;
    output.reserve((screenRows + 3) * screenCols * 3);

    output += Terminal::ESC_CURSOR_HOME;

    for(int y = 0; y < screenRows; y++)
    {
        if(y > 0)
            output += "\r\n";

        output += Terminal::ESC_CLEAR_LINE;

        int fileRow = y + *offsetY;

        if(fileRow >= lines->size())
        {
            output += Terminal::FG_BLUE;
            output += "~";
            output += Terminal::FG_DEFAULT;
        }
        else
        {
            const std::string& line = (*lines)[fileRow];
            int start = *offsetX;
            int len = line.length() - start;

            if(len > 0)
            {
                if(len > screenCols)
                    len = screenCols;

                bool hasHighlighting = false;

                if(currentMode == VISUAL || currentMode == VISUAL_LINE ||
                   currentMode == VISUAL_BLOCK || !searchMatches.empty())
                {
                    for(int x = 0; x < len; x++)
                    {
                        int col = x + *offsetX;
                        if(isInSelection(fileRow, col) ||
                           isInVisualBlock(fileRow, col) ||
                           isInSearchMatch(fileRow, col))
                        {
                            hasHighlighting = true;
                            break;
                        }
                    }
                }

                // Check if we should use syntax highlighting
                if(isCppFile())
                {
                    // Use syntax highlighting for C++ files (handles selections
                    // too)
                    renderLineWithSyntax(output, line, start, len, fileRow);
                }
                else if(!hasHighlighting)
                {
                    // No highlighting needed
                    output.append(line, start, len);
                }
                else
                {
                    // Handle selection/search highlighting for non-C++ files
                    for(int x = 0; x < len; x++)
                    {
                        int col = x + *offsetX;
                        bool highlighted = false;

                        if(isInSelection(fileRow, col) ||
                           isInVisualBlock(fileRow, col))
                        {
                            output += Terminal::STYLE_SELECTION;
                            highlighted = true;
                        }
                        else if(isInSearchMatch(fileRow, col))
                        {
                            output += Terminal::STYLE_SEARCH_MATCH;
                            highlighted = true;
                        }

                        output += line[col];

                        if(highlighted)
                        {
                            output += Terminal::ESC_RESET_ALL;
                        }
                    }
                }
            }
        }
    }

    // Status bar
    output += Terminal::NEWLINE_CLEAR;
    output += Terminal::STYLE_SELECTION;

    std::string statusLeft = " " + getModeString() + " | ";

    if(buffers.size() > 1)
    {
        statusLeft += "[" + std::to_string(currentBufferIndex + 1) + "/" +
                      std::to_string(buffers.size()) + "] ";
    }

    char rightStatus[32];
    snprintf(rightStatus, sizeof(rightStatus), " %d:%d ", *cursorY + 1,
             *cursorX + 1);

    // Calculate available space for filename
    int rightLen = strlen(rightStatus);
    int availableForFile = screenCols - statusLeft.length() - rightLen - 1;

    std::string displayName = filename->empty() ? "[No Name]" : *filename;
    if(*dirty)
        displayName += " [+]";

    // Truncate filename from the beginning if too long
    if((int)displayName.length() > availableForFile && availableForFile > 4)
    {
        displayName = "..." + displayName.substr(displayName.length() -
                                                 availableForFile + 3);
    }

    statusLeft += displayName;

    output += statusLeft;

    int padding = screenCols - statusLeft.length() - rightLen;
    if(padding > 0)
        output.append(padding, ' ');
    output += rightStatus;
    output += Terminal::ESC_RESET_ALL;

    // Message bar
    output += Terminal::NEWLINE_CLEAR;

    if(currentMode == COMMAND || currentMode == SEARCH_FORWARD ||
       currentMode == SEARCH_BACKWARD)
    {
        output += commandBuffer;
        if(currentMode == SEARCH_FORWARD || currentMode == SEARCH_BACKWARD)
        {
            if(!searchMatches.empty())
            {
                output += " [" + std::to_string(currentMatchIndex + 1) + "/" +
                          std::to_string(searchMatches.size()) + "]";
            }
            else if(!searchQuery.empty())
            {
                output += " [No matches]";
            }
        }
    }
    else if(!statusMessage.empty())
    {
        int msglen = std::min((int)statusMessage.length(), screenCols);
        output.append(statusMessage, 0, msglen);
    }

    // Completion popup (clangd)
    drawCompletionPopup(output);

    Terminal::write(output);
    updateCursorPosition();
    Terminal::flush();
}

void Editor::refreshScreen()
{
    if(currentMode == FILE_BROWSER)
    {
        drawFileBrowser();
        return;
    }

    if(currentMode == FUZZY_FIND)
    {
        drawFuzzyFind();
        return;
    }

    if(currentMode == BUFFER_BROWSER)
    {
        drawBufferBrowser();
        return;
    }

    if(currentMode == GREP_SEARCH)
    {
        drawGrepSearch();
        return;
    }

    static int lastOffsetY = -1;
    static int lastOffsetX = -1;
    static Mode lastMode = NORMAL;
    static int lastVisualStartY = -1;
    static int lastVisualEndY = -1;

    int prevOffsetY = lastOffsetY;
    adjustViewport();

    bool scrolled = (*offsetY != lastOffsetY || *offsetX != lastOffsetX);
    bool modeChanged = (currentMode != lastMode);
    int scrollDelta = *offsetY - lastOffsetY;

    bool visualChanged = false;
    if(currentMode == VISUAL || currentMode == VISUAL_LINE ||
       currentMode == VISUAL_BLOCK)
    {
        visualChanged = (currentBuffer->visualStartY != lastVisualStartY ||
                         currentBuffer->visualEndY != lastVisualEndY);
        lastVisualStartY = currentBuffer->visualStartY;
        lastVisualEndY = currentBuffer->visualEndY;
    }
    else
    {
        lastVisualStartY = -1;
        lastVisualEndY = -1;
    }

    bool isEditingMode =
        (currentMode == INSERT || currentMode == COMMAND ||
         currentMode == SEARCH_FORWARD || currentMode == SEARCH_BACKWARD);

    if(modeChanged || needsFullRedraw || *offsetX != lastOffsetX ||
       abs(scrollDelta) > screenRows / 2 || visualChanged ||
       (currentMode == VISUAL || currentMode == VISUAL_LINE ||
        currentMode == VISUAL_BLOCK) ||
       isEditingMode)
    {
        drawFullScreen();
    }
    else if(scrollDelta != 0 && abs(scrollDelta) <= 5 && currentMode == NORMAL)
    {
        drawScrollUpdate(scrollDelta);
    }
    else if(scrollDelta == 0 && currentMode == NORMAL)
    {
        drawStatusBarQuick();
        drawMessageBarQuick(); // Add this
        updateCursorPosition();
    }
    else
    {
        drawFullScreen();
    }

    lastOffsetY = *offsetY;
    lastOffsetX = *offsetX;
    lastMode = currentMode;
    needsFullRedraw = false;
}

void Editor::updateCursorPosition()
{
    int cursorRow, cursorCol;

    if(currentMode == COMMAND || currentMode == SEARCH_FORWARD ||
       currentMode == SEARCH_BACKWARD)
    {
        cursorRow = screenRows + 2;
        cursorCol = commandBuffer.length() + 1;
    }
    else
    {
        cursorRow = (*cursorY - *offsetY) + 1;
        cursorCol = (*cursorX - *offsetX) + 1;
    }

    Terminal::write(Terminal::cursorPos(cursorRow, cursorCol));
    Terminal::flush();

    lastCursorScreenY = cursorRow;
    lastCursorScreenX = cursorCol;
}

void Editor::draw()
{
    refreshScreen();
}

void Editor::setStatusMessage(const std::string& msg)
{
    statusMessage = msg;
}

// Command execution
void Editor::executeCommand(const std::string& cmd)
{
    // Buffer commands
    if(cmd == "bn" || cmd == "bnext")
    {
        nextBuffer();
    }
    else if(cmd == "bp" || cmd == "bprev" || cmd == "bprevious")
    {
        previousBuffer();
    }
    else if(cmd == "bd" || cmd == "bdelete")
    {
        closeCurrentBuffer();
    }
    else if(cmd == "bd!")
    {
        *dirty = false;
        closeCurrentBuffer();
    }
    else if(cmd == "ls" || cmd == "buffers")
    {
        listBuffers();
    }
    else if(cmd.substr(0, 2) == "b " || cmd.substr(0, 7) == "buffer ")
    {
        std::string arg =
            (cmd.substr(0, 2) == "b ") ? cmd.substr(2) : cmd.substr(7);

        try
        {
            int bufNum = std::stoi(arg) - 1;
            if(bufNum >= 0 && bufNum < buffers.size())
            {
                switchToBuffer(bufNum);
            }
            else
            {
                setStatusMessage("Buffer " + arg + " does not exist");
            }
        }
        catch(...)
        {
            for(size_t i = 0; i < buffers.size(); i++)
            {
                if(buffers[i]->filename.find(arg) != std::string::npos)
                {
                    switchToBuffer(i);
                    return;
                }
            }
            setStatusMessage("No matching buffer for " + arg);
        }
    }
    else if(cmd == "enew")
    {
        createNewBuffer();
        setStatusMessage("New buffer created");
    }
    else if(cmd == "wall" || cmd == "wa")
    {
        int savedCount = 0;
        int currentBuf = currentBufferIndex;

        for(size_t i = 0; i < buffers.size(); i++)
        {
            if(buffers[i]->dirty && !buffers[i]->filename.empty())
            {
                switchToBuffer(i);
                saveFile();
                savedCount++;
            }
        }

        switchToBuffer(currentBuf);
        setStatusMessage("Saved " + std::to_string(savedCount) + " buffer(s)");
    }
    else if(cmd == "qall" || cmd == "qa")
    {
        bool hasUnsaved = false;
        for(const auto& buf : buffers)
        {
            if(buf->dirty)
            {
                hasUnsaved = true;
                break;
            }
        }

        if(hasUnsaved)
        {
            setStatusMessage(
                "Some buffers have unsaved changes (add ! to override)");
        }
        else
        {
            Terminal::clearScreen();
            exit(0);
        }
    }
    else if(cmd == "qall!" || cmd == "qa!")
    {
        Terminal::clearScreen();
        exit(0);
    }
    else if(cmd == "wqall" || cmd == "wqa" || cmd == "xa")
    {
        for(size_t i = 0; i < buffers.size(); i++)
        {
            if(buffers[i]->dirty && !buffers[i]->filename.empty())
            {
                switchToBuffer(i);
                saveFile();
            }
        }
        Terminal::clearScreen();
        exit(0);
    }
    // File browser commands
    else if(cmd == "Ex" || cmd == "ex" || cmd == "E" || cmd == "e ." ||
            cmd == "Explore" || cmd == "explore")
    {
        std::string dir = ".";
        if(!filename->empty())
        {
            size_t lastSlash = filename->find_last_of("/");
            if(lastSlash != std::string::npos)
            {
                dir = filename->substr(0, lastSlash);
                if(dir.empty())
                    dir = "/";
            }
        }
        openFileBrowser(dir);
        return;
    }
    else if(cmd == "Sex" || cmd == "Sexplore" || cmd == "Vex" ||
            cmd == "Vexplore")
    {
        setStatusMessage("Split explorer not yet implemented");
        openFileBrowser(".");
        return;
    }
    // Standard commands
    else if(cmd == "w")
    {
        saveFile();
    }
    else if(cmd == "q")
    {
        if(*dirty)
        {
            setStatusMessage("No write since last change (add ! to override)");
        }
        else if(buffers.size() > 1)
        {
            closeCurrentBuffer();
        }
        else
        {
            Terminal::clearScreen();
            exit(0);
        }
    }
    else if(cmd == "q!")
    {
        if(buffers.size() > 1)
        {
            *dirty = false;
            closeCurrentBuffer();
        }
        else
        {
            Terminal::clearScreen();
            exit(0);
        }
    }
    else if(cmd == "wq" || cmd == "x")
    {
        saveFile();
        if(buffers.size() > 1)
        {
            closeCurrentBuffer();
        }
        else
        {
            Terminal::clearScreen();
            exit(0);
        }
    }
    else if(cmd.substr(0, 2) == "w ")
    {
        *filename = cmd.substr(2);
        saveFile();
    }
    else if(cmd.substr(0, 2) == "e " || cmd.substr(0, 5) == "edit ")
    {
        std::string path =
            (cmd.substr(0, 2) == "e ") ? cmd.substr(2) : cmd.substr(5);

        if(path == ".")
        {
            openFileBrowser(".");
            return;
        }
        else
        {
            struct stat fileStat;
            if(stat(path.c_str(), &fileStat) == 0 && S_ISDIR(fileStat.st_mode))
            {
                openFileBrowser(path);
                return;
            }
            openFile(path);
            setMode(NORMAL);
        }
    }
    else if(cmd.substr(0, 6) == "tabnew" || cmd.substr(0, 5) == "tabe ")
    {
        std::string fname = "";
        if(cmd.substr(0, 5) == "tabe " && cmd.length() > 5)
        {
            fname = cmd.substr(5);
        }
        else if(cmd.substr(0, 7) == "tabnew " && cmd.length() > 7)
        {
            fname = cmd.substr(7);
        }

        if(!fname.empty())
        {
            openFile(fname);
        }
        else
        {
            createNewBuffer();
            setStatusMessage("New buffer created");
        }
    }
    else if(cmd == "tabn" || cmd == "tabnext")
    {
        nextBuffer();
    }
    else if(cmd == "tabp" || cmd == "tabprev")
    {
        previousBuffer();
    }
    else if(cmd == "pwd")
    {
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
        {
            setStatusMessage(std::string(cwd));
        }
        else
        {
            setStatusMessage("Error getting current directory");
        }
    }
    else if(cmd.substr(0, 3) == "cd " || cmd == "cd")
    {
        std::string path = (cmd.length() > 3) ? cmd.substr(3) : "";
        if(path.empty())
        {
            // cd with no args goes to home directory
            const char* home = getenv("HOME");
            if(home)
                path = home;
            else
                path = "/";
        }

        // Expand ~ to home directory
        if(!path.empty() && path[0] == '~')
        {
            const char* home = getenv("HOME");
            if(home)
                path = std::string(home) + path.substr(1);
        }

        if(chdir(path.c_str()) == 0)
        {
            char cwd[PATH_MAX];
            if(getcwd(cwd, sizeof(cwd)))
                setStatusMessage(std::string(cwd));
        }
        else
        {
            setStatusMessage("Cannot change to: " + path);
        }
    }
    else
    {
        try
        {
            int line = std::stoi(cmd);
            moveToLine(line - 1);
        }
        catch(...)
        {
            setStatusMessage("Not an editor command: " + cmd);
        }
    }
}

void Editor::forceQuit()
{
    Terminal::restoreTerminal();
    std::exit(0);
}

// ----- clangd completion popup helpers -----

static bool isIdentChar(char c)
{
    return std::isalnum((unsigned char)c) || c == '_';
}

// Very small snippet “desugaring”: turns clangd snippets into plain insert
// text.
// - removes $0, $1 ...
// - turns ${1:foo} -> foo
// - removes ${1}
static std::string stripSnippet(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for(size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        if(c != '$')
        {
            out.push_back(c);
            continue;
        }

        if(i + 1 >= s.size())
            continue;

        char n = s[i + 1];
        if(std::isdigit((unsigned char)n))
        {
            // $0, $1 ...
            i += 1;
            while(i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1]))
                i++;
            continue;
        }

        if(n == '{')
        {
            // ${1:foo} or ${1}
            size_t end = s.find('}', i + 2);
            if(end == std::string::npos)
                continue;

            std::string inner = s.substr(i + 2, end - (i + 2));
            // inner might be "1:foo" or "1"
            size_t colon = inner.find(':');
            if(colon != std::string::npos)
            {
                out += inner.substr(colon + 1);
            }
            // else: just a placeholder number → ignore
            i = end;
            continue;
        }

        // Unknown $-sequence → drop '$' and keep the next char
        // (so "$$" becomes "$", etc.)
        out.push_back(n);
        i += 1;
    }
    return out;
}

static inline void appendUtf8Repeat(std::string& out, const char* glyph,
                                    int count)
{
    for(int i = 0; i < count; ++i)
        out += glyph;
}

static inline bool isAnsiStart(const std::string& s, size_t i)
{
    return i + 1 < s.size() && s[i] == '\x1b' && s[i + 1] == '[';
}

static inline size_t skipAnsi(const std::string& s, size_t i)
{
    // Skip ESC[ ... <letter>
    i += 2;
    while(i < s.size())
    {
        char c = s[i++];
        if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
            break;
    }
    return i;
}

// Approximate terminal display width.
// - strips ANSI escapes
// - counts UTF-8 codepoints as width 1 (good enough for our popup)
static inline int displayWidth(const std::string& s)
{
    int w = 0;
    for(size_t i = 0; i < s.size();)
    {
        if(isAnsiStart(s, i))
        {
            i = skipAnsi(s, i);
            continue;
        }

        unsigned char c = (unsigned char)s[i];
        if(c < 0x80)
        {
            ++w;
            ++i;
            continue;
        }

        // UTF-8: skip continuation bytes
        if((c & 0xE0) == 0xC0)
            i += 2;
        else if((c & 0xF0) == 0xE0)
            i += 3;
        else if((c & 0xF8) == 0xF0)
            i += 4;
        else
            ++i;
        ++w;
    }
    return w;
}

static inline int fuzzyScore(const std::string& text,
                             const std::string& pattern)
{
    if(pattern.empty())
        return 0;

    auto lower = [](unsigned char ch) -> unsigned char
    {
        if(ch >= 'A' && ch <= 'Z')
            return (unsigned char)(ch - 'A' + 'a');
        return ch;
    };

    int score = 0;
    int ti = 0;
    int consecutive = 0;

    for(int pi = 0; pi < (int)pattern.size(); ++pi)
    {
        unsigned char pc = lower((unsigned char)pattern[pi]);
        bool found = false;

        while(ti < (int)text.size())
        {
            unsigned char tc = (unsigned char)text[ti];
            unsigned char ltc = lower(tc);
            if(ltc == pc)
            {
                // base points + consecutive bonus
                score += 10;
                score += consecutive * 5;

                // token-boundary bonus
                if(ti == 0)
                    score += 8;
                else
                {
                    char prev = text[ti - 1];
                    if(prev == '_' || prev == ':' || prev == ' ' ||
                       prev == '\t' || prev == '-')
                        score += 8;
                }

                ++consecutive;
                ++ti;
                found = true;
                break;
            }
            else
            {
                consecutive = 0;
                ++ti;
            }
        }
        if(!found)
            return -1;
    }

    return score;
}

void Editor::cancelCompletion()
{
    completionActive = false;
    completionAll.clear();
    completionFiltered.clear();
    completionSelected = 0;
    completionScroll = 0;
    completionQuery.clear();
}

void Editor::completionNext()
{
    if(!completionActive || completionFiltered.empty())
        return;
    completionSelected =
        (completionSelected + 1) % (int)completionFiltered.size();

    const int win = std::min(8, (int)completionFiltered.size());
    if(completionSelected < completionScroll)
        completionScroll = completionSelected;
    else if(completionSelected >= completionScroll + win)
        completionScroll = completionSelected - win + 1;

    needsFullRedraw = true;
}

void Editor::completionPrev()
{
    if(!completionActive || completionFiltered.empty())
        return;
    completionSelected = (completionSelected - 1);
    if(completionSelected < 0)
        completionSelected = (int)completionFiltered.size() - 1;

    const int win = std::min(8, (int)completionFiltered.size());
    if(completionSelected < completionScroll)
        completionScroll = completionSelected;
    else if(completionSelected >= completionScroll + win)
        completionScroll = completionSelected - win + 1;

    needsFullRedraw = true;
}

void Editor::acceptCompletion()
{
    if(!completionActive || completionFiltered.empty())
        return;

    // Ensure anchor is on the current line (if the user moved, recompute).
    completionAnchorY = *cursorY;
    const std::string& line = (*lines)[*cursorY];
    int ax = *cursorX;
    while(ax > 0 && isIdentChar(line[ax - 1]))
        --ax;
    completionAnchorX = ax;

    const CompletionEntry& it =
        completionAll[completionFiltered[completionSelected]];
    std::string insert = it.insertText.empty() ? it.label : it.insertText;
    if(it.isSnippet)
        insert = stripSnippet(insert);

    // Replace prefix [anchorX, cursorX)
    std::string& mutableLine = (*lines)[*cursorY];
    int start =
        std::max(0, std::min(completionAnchorX, (int)mutableLine.size()));
    int end = std::max(0, std::min(*cursorX, (int)mutableLine.size()));
    if(end < start)
        std::swap(start, end);

    mutableLine.replace(start, end - start, insert);
    *cursorX = start + (int)insert.size();
    *wantedX = *cursorX;
    *dirty = true;

    cancelCompletion();
    needsFullRedraw = true;
}

void Editor::rebuildCompletionFilter()
{
    if(!completionActive)
        return;

    // Cancel if cursor moved away from the anchor line or before anchor.
    if(*cursorY != completionAnchorY || *cursorX < completionAnchorX)
    {
        cancelCompletion();
        needsFullRedraw = true;
        return;
    }

    const std::string& line = (*lines)[*cursorY];
    int a = std::max(0, std::min(completionAnchorX, (int)line.size()));
    int b = std::max(0, std::min(*cursorX, (int)line.size()));
    if(b < a)
        b = a;
    completionQuery = line.substr(a, b - a);

    struct Scored
    {
        int idx;
        int score;
    };
    std::vector<Scored> scored;
    scored.reserve(completionAll.size());

    for(int i = 0; i < (int)completionAll.size(); ++i)
    {
        const auto& e = completionAll[i];
        // Use the completion-popup fuzzy matcher (simple subsequence).
        // Unqualified name would resolve to Editor::fuzzyScore (3-arg) used by
        // file/buffer pickers, so qualify explicitly.
        int s = ::fuzzyScore(e.label, completionQuery);
        if(s >= 0)
            scored.push_back({i, s});
    }

    std::stable_sort(scored.begin(), scored.end(),
                     [](const Scored& x, const Scored& y)
                     { return x.score > y.score; });

    completionFiltered.clear();
    completionFiltered.reserve(scored.size());
    for(const auto& s : scored)
        completionFiltered.push_back(s.idx);

    if(completionFiltered.empty())
    {
        cancelCompletion();
        needsFullRedraw = true;
        return;
    }

    if(completionSelected >= (int)completionFiltered.size())
        completionSelected = (int)completionFiltered.size() - 1;
    if(completionSelected < 0)
        completionSelected = 0;

    const int win = std::min(8, (int)completionFiltered.size());
    if(completionSelected < completionScroll)
        completionScroll = completionSelected;
    else if(completionSelected >= completionScroll + win)
        completionScroll = completionSelected - win + 1;

    if(completionScroll < 0)
        completionScroll = 0;

    // Ensure the popup updates immediately as the user types.
    needsFullRedraw = true;
}

void Editor::requestCompletion()
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!isClangdLspEnabled())
    {
        setStatusMessage("clangd completion: OFF");
        return;
    }
    if(!isCppFile())
    {
        setStatusMessage("clangd completion: only for C/C++");
        return;
    }

    // Compute prefix/anchor (identifier chars).
    const std::string& line = (*lines)[*cursorY];
    int ax = *cursorX;
    while(ax > 0 && isIdentChar(line[ax - 1]))
        --ax;

    completionAnchorX = ax;
    completionAnchorY = *cursorY;

    // Sync buffer text.
    std::string text;
    text.reserve(lines->size() * 80);
    for(size_t i = 0; i < lines->size(); ++i)
    {
        text += (*lines)[i];
        if(i + 1 < lines->size())
            text.push_back('\n');
    }
    lspClient->didChange(currentBuffer->filename, text);

    auto items =
        lspClient->completion(currentBuffer->filename, *cursorY, *cursorX);
    completionAll.clear();
    completionAll.reserve(items.size());

    for(const auto& ci : items)
    {
        CompletionEntry e;
        e.label = ci.label;
        e.insertText = ci.insertText;
        e.isSnippet = ci.isSnippet;
        completionAll.push_back(std::move(e));
    }

    if(completionAll.empty())
    {
        cancelCompletion();
        setStatusMessage("clangd completion: no results");
        return;
    }

    completionActive = true;
    completionSelected = 0;
    completionScroll = 0;
    rebuildCompletionFilter();
    needsFullRedraw = true;
#else
    setStatusMessage("clangd completion: not compiled in");
#endif
}

void Editor::drawCompletionPopup(std::string& output) const
{
    if(!completionActive || completionFiltered.empty())
        return;
    if(currentMode != INSERT)
        return;

    // Visible rows
    const int maxRows =
        std::min({8, (int)completionFiltered.size(), screenRows - 2});
    if(maxRows <= 0)
        return;

    // Determine cursor position on screen
    // Use the editor's viewport offsets.
    int cy = (*cursorY - *offsetY) + 1;
    int cx = (*cursorX - *offsetX) + 1;
    if(cy < 1)
        cy = 1;
    if(cy > screenRows)
        cy = screenRows;
    if(cx < 1)
        cx = 1;
    if(cx > screenCols)
        cx = screenCols;

    // Compute width from all filtered results (so the window doesn't
    // resize when scrolling).
    int maxW = 0;
    // Cap work for extremely large result sets.
    const int cap = std::min((int)completionFiltered.size(), 500);
    for(int i = 0; i < cap; ++i)
    {
        const auto& e = completionAll[completionFiltered[i]];
        maxW = std::max(maxW, displayWidth(e.label));
    }
    // Add padding inside box
    int innerW = std::max(12, maxW);
    // Box consumes innerW + 2 padding + 2 borders
    int totalW = innerW + 4;
    if(totalW > screenCols)
    {
        totalW = screenCols;
        innerW = std::max(4, totalW - 4);
    }

    // Place below cursor if possible, otherwise above
    int totalH = maxRows + 2;
    int top = cy + 1;
    if(top + totalH - 1 > screenRows)
        top = cy - totalH;
    if(top < 1)
        top = 1;

    int left = cx;
    if(left + totalW - 1 > screenCols)
        left = std::max(1, screenCols - totalW + 1);

    auto moveTo = [&](int r, int c) { output += Terminal::cursorPos(r, c); };

    // Top border
    moveTo(top, left);
    output += u8"┌";
    appendUtf8Repeat(output, u8"─", innerW + 2);
    output += u8"┐";

    // Rows
    for(int i = 0; i < maxRows; ++i)
    {
        int fidx = completionScroll + i;
        if(fidx >= (int)completionFiltered.size())
            break;
        const auto& e = completionAll[completionFiltered[fidx]];

        moveTo(top + 1 + i, left);
        output += u8"│";
        output += " ";

        bool sel = (fidx == completionSelected);
        if(sel)
            output += Terminal::STYLE_SELECTION;

        std::string row = e.label;
        // Trim to fit
        while(displayWidth(row) > innerW)
            row.pop_back();

        output += row;

        // Pad to width
        int pad = innerW - displayWidth(row);
        if(pad > 0)
            output += std::string(pad, ' ');

        if(sel)
            output += Terminal::ESC_RESET_ALL;

        output += " ";
        output += u8"│";
    }

    // Bottom border
    moveTo(top + 1 + maxRows, left);
    output += u8"└";
    appendUtf8Repeat(output, u8"─", innerW + 2);
    output += u8"┘";
}

std::string Editor::getAlternateFilePath()
{
    if(!currentBuffer || currentBuffer->filename.empty())
        return "";

    return findAlternateFile(currentBuffer->filename);
}

// Mode handlers
void Editor::handleNormalMode(int c)
{
#ifdef UVIM_DEBUG_LOGGING
    // Debug: log every keypress
    {
        std::ofstream dbg("/tmp/uvim_debug.txt", std::ios::app);
        dbg << "handleNormalMode c=" << c << " ('" << (char)c
            << "') commandBuffer='" << commandBuffer << "'" << std::endl;
    }
#endif

    static bool pendingDelete = false;
    static bool pendingYank = false;
    static bool pendingIndent = false;
    static bool pendingShiftRight = false;
    static bool pendingShiftLeft = false;

    // ----- single-character replace (vim/neovim-style 'r{char}') -----
    if(c == 'r')
    {
        // Cancel any pending operators
        pendingDelete = pendingYank = pendingIndent = false;

        int rc = Terminal::readKey();

        // Only accept printable characters
        if(rc < 32 || rc == 127)
            return;

        if(!lines || lines->empty())
            return;

        if(*cursorY < 0 || *cursorY >= (int)lines->size())
            return;

        std::string& line = (*lines)[*cursorY];
        if(*cursorX < 0 || *cursorX >= (int)line.size())
            return;

        line[*cursorX] = (char)rc;

        saveState(); // your undo model saves *after* changes
        *dirty = true;
        needsFullRedraw =
            true; // IMPORTANT: otherwise NORMAL mode may not redraw text
        return;
    }

    if(c >= '1' && c <= '9' && repeatCount == 0 && commandBuffer.empty())
    {
        repeatCount = c - '0';
        return;
    }
    else if(c >= '0' && c <= '9' && repeatCount > 0)
    {
        repeatCount = repeatCount * 10 + (c - '0');
        return;
    }
    int count = std::max(1, repeatCount);

    // ----- Leader (space) prefixed commands (MUST be early) -----
    if(commandBuffer == " ")
    {
        if(c == 'h')
        {
            // Leader + h: jump to alternate file (header/source)
            jumpToAlternateFile();
            commandBuffer.clear();
            repeatCount = 0;
            return;
        }
        else if(c == 'b')
        {
            // Leader + b: start buffer command sequence
            commandBuffer = " b";
            setStatusMessage("Leader-b");
            repeatCount = 0;
            return;
        }
        else if(c == 'y')
        {
            // Leader + y: yank to system clipboard
            yankToSystemClipboard();
            commandBuffer.clear();
            repeatCount = 0;
            return;
        }
        else if(c == 'p')
        {
            // Leader + p: paste from system clipboard
            pasteFromSystemClipboard();
            commandBuffer.clear();
            repeatCount = 0;
            return;
        }
        else if(c == 'f')
        {
            // Leader + f: format file with clang-format
            commandBuffer.clear();
            repeatCount = 0;

#ifdef UVIM_DEBUG_LOGGING
            // Debug: log to file
            {
                std::ofstream dbg("/tmp/uvim_debug.txt", std::ios::app);
                dbg << "Leader-f pressed. filename='" << *filename
                    << "' isCppFile=" << isCppFile() << std::endl;
            }
#endif

            if(!isCppFile())
            {
                setStatusMessage("clang-format: not a C/C++ file (" +
                                 *filename + ")");
                return;
            }

            // Save current cursor position
            int savedY = *cursorY;
            int savedX = *cursorX;

            // Write buffer to temp file
            std::string tempPath =
                "/tmp/uvim_format_" + std::to_string(getpid()) + ".tmp";
            std::ofstream tempFile(tempPath);
            if(!tempFile.is_open())
            {
                setStatusMessage("clang-format: failed to create temp file");
                return;
            }

            // Write content with trailing newline
            for(size_t i = 0; i < lines->size(); ++i)
            {
                tempFile << (*lines)[i] << '\n';
            }
            tempFile.close();

            // Get the directory of the current file for clang-format to find
            // .clang-format
            std::string fileDir = ".";
            std::string absFilename = *filename;

            // Make filename absolute if it isn't
            if(!absFilename.empty() && absFilename[0] != '/')
            {
                char cwd[PATH_MAX];
                if(getcwd(cwd, sizeof(cwd)))
                {
                    absFilename = std::string(cwd) + "/" + *filename;
                }
            }

            // Run clang-format with stdin, using actual filename for style
            // lookup clang-format searches for .clang-format starting from the
            // file's directory
            std::string cmd = "cat \"" + tempPath +
                              "\" | /opt/homebrew/bin/clang-format -style=file"
                              " -assume-filename=\"" +
                              absFilename +
                              "\""
                              " 2>/tmp/uvim_clang_err.txt";

#ifdef UVIM_DEBUG_LOGGING
            // Debug log
            {
                std::ofstream dbg("/tmp/uvim_debug.txt", std::ios::app);
                dbg << "Temp file written: " << tempPath
                    << " lines=" << lines->size() << std::endl;
                dbg << "Running: " << cmd << std::endl;
            }
#endif

            FILE* pipe = popen(cmd.c_str(), "r");
            if(!pipe)
            {
                // Try without full path
                cmd = "cat \"" + tempPath +
                      "\" | clang-format -style=file"
                      " -assume-filename=\"" +
                      absFilename +
                      "\""
                      " 2>/tmp/uvim_clang_err.txt";
                pipe = popen(cmd.c_str(), "r");
            }

            if(!pipe)
            {
                unlink(tempPath.c_str());
                setStatusMessage("clang-format: failed to run");
                return;
            }

            // Read formatted output
            std::string formatted;
            char buffer[4096];
            while(fgets(buffer, sizeof(buffer), pipe))
            {
                formatted += buffer;
            }
            int status = pclose(pipe);
            unlink(tempPath.c_str());

#ifdef UVIM_DEBUG_LOGGING
            // Debug log
            {
                std::ofstream dbg("/tmp/uvim_debug.txt", std::ios::app);
                dbg << "pclose status=" << status
                    << " formatted.size()=" << formatted.size() << std::endl;
            }
#endif

            // Check for errors
            if(formatted.empty())
            {
                // Read error file
                std::ifstream errFile("/tmp/uvim_clang_err.txt");
                std::string errMsg;
                if(errFile.is_open())
                {
                    std::getline(errFile, errMsg);
                    errFile.close();
                }
                if(errMsg.empty())
                    errMsg = "no output (exit=" +
                             std::to_string(WEXITSTATUS(status)) + ")";
                setStatusMessage("clang-format: " + errMsg.substr(0, 50));
                return;
            }

            // Parse formatted output into lines
            std::vector<std::string> newLines;
            std::istringstream iss(formatted);
            std::string line;
            while(std::getline(iss, line))
            {
                // Remove \r if present
                if(!line.empty() && line.back() == '\r')
                    line.pop_back();
                newLines.push_back(line);
            }

            // Remove trailing empty line if present (clang-format adds one)
            if(!newLines.empty() && newLines.back().empty())
            {
                newLines.pop_back();
            }

            // Ensure at least one line
            if(newLines.empty())
            {
                newLines.push_back("");
            }

            // Check if anything changed
            if(newLines == *lines)
            {
                setStatusMessage("clang-format: no changes needed");
                return;
            }

            // Save state for undo
            saveState();

            // Replace buffer content
            *lines = newLines;
            *dirty = true;

            // Restore cursor position (clamped to valid range)
            if(lines->empty())
            {
                *cursorY = 0;
                *cursorX = 0;
            }
            else
            {
                *cursorY = savedY;
                if(*cursorY >= (int)lines->size())
                    *cursorY = (int)lines->size() - 1;
                if(*cursorY < 0)
                    *cursorY = 0;

                *cursorX = savedX;
                int lineLen = (int)(*lines)[*cursorY].length();
                if(*cursorX > lineLen)
                    *cursorX = lineLen > 0 ? lineLen - 1 : 0;
                if(*cursorX < 0)
                    *cursorX = 0;
            }

            adjustViewport();
            needsFullRedraw = true;
            setStatusMessage("clang-format: formatted " +
                             std::to_string(lines->size()) + " lines");
            return;
        }
        else if(c == ' ')
        {
            // Double space cancels
            commandBuffer.clear();
            setStatusMessage("");
            repeatCount = 0;
            return;
        }
        else
        {
            // Unknown leader command - cancel
            commandBuffer.clear();
            setStatusMessage("");
        }
    }

    // ----- Leader-b (buffer) commands -----
    if(commandBuffer == " b")
    {
        if(c == 'd')
        {
            // Leader + bd: close current buffer
            commandBuffer.clear();
            closeCurrentBuffer();
            repeatCount = 0;
            return;
        }
        else
        {
            // Unknown buffer command - cancel
            commandBuffer.clear();
            setStatusMessage("");
        }
    }

    // ----- g-prefixed commands (MUST be first) -----
    if(commandBuffer == "g")
    {
        if(c == 'd')
        {
            goToDefinition();
            repeatCount = 0;
            return;
        }
        else if(c == 'g')
        {
            moveToFirstLine();
            commandBuffer.clear();
            repeatCount = 0;
            return;
        }
        else
        {
            // Unknown g-command → cancel
            commandBuffer.clear();
        }
    }
    else if(c == 'd')
    {
        if(pendingDelete)
        {
            // dd detected
            for(int i = 0; i < count; i++)
            {
                deleteLine();
            }
            saveState();
            setStatusMessage(std::to_string(count) + " line(s) deleted");
            pendingDelete = false;
            repeatCount = 0;
            return;
        }
        else
        {
            // first 'd'
            pendingDelete = true;
            pendingYank = false;   // Cancel any pending yank
            pendingIndent = false; // Cancel any pending indent
            return;
        }
    }
    else if(c == 'y')
    {
        if(pendingYank)
        {
            // yy detected - yank multiple lines
            yankBuffer.clear();
            int startLine = *cursorY;
            int endLine =
                std::min(startLine + count - 1, (int)lines->size() - 1);

            for(int i = startLine; i <= endLine; i++)
            {
                yankBuffer += (*lines)[i] + "\n";
            }

            int linesYanked = endLine - startLine + 1;
            setStatusMessage(std::to_string(linesYanked) + " line" +
                             (linesYanked > 1 ? "s" : "") + " yanked");
            pendingYank = false;
            repeatCount = 0;
            return;
        }
        else
        {
            // first 'y'
            pendingYank = true;
            pendingDelete = false; // Cancel any pending delete
            pendingIndent = false; // Cancel any pending indent
            return;
        }
    }
    else if(c == '=')
    {
        if(pendingIndent)
        {
            // == detected - indent current line(s)
            int startLine = *cursorY;
            int endLine =
                std::min(startLine + count - 1, (int)lines->size() - 1);

            autoIndentRange(startLine, endLine);

            int linesIndented = endLine - startLine + 1;
            setStatusMessage(std::to_string(linesIndented) + " line" +
                             (linesIndented > 1 ? "s" : "") + " indented");
            pendingIndent = false;
            repeatCount = 0;
            saveState();
            return;
        }
        else
        {
            // first '='
            pendingIndent = true;
            pendingDelete = false; // Cancel any pending delete
            pendingYank = false;   // Cancel any pending yank
            return;
        }
    }
    else if(c == '>')
    {
        if(pendingShiftRight)
        {
            // >> detected - shift right (increase indent)
            int startLine = *cursorY;
            int endLine =
                std::min(startLine + count - 1, (int)lines->size() - 1);

            for(int i = startLine; i <= endLine; i++)
            {
                int currentIndent = getLineIndent(i);
                indentLine(i, currentIndent + 4); // Add 4 spaces
            }

            int linesIndented = endLine - startLine + 1;
            setStatusMessage(std::to_string(linesIndented) + " line" +
                             (linesIndented > 1 ? "s" : "") + " >>");
            pendingShiftRight = false;
            repeatCount = 0;
            saveState();
            needsFullRedraw = true;
            return;
        }
        else
        {
            // first '>'
            pendingShiftRight = true;
            pendingShiftLeft = false;
            pendingDelete = false;
            pendingYank = false;
            pendingIndent = false;
            return;
        }
    }
    else if(c == '<')
    {
        if(pendingShiftLeft)
        {
            // << detected - shift left (decrease indent)
            int startLine = *cursorY;
            int endLine =
                std::min(startLine + count - 1, (int)lines->size() - 1);

            for(int i = startLine; i <= endLine; i++)
            {
                int currentIndent = getLineIndent(i);
                indentLine(i,
                           std::max(0, currentIndent - 4)); // Remove 4 spaces
            }

            int linesIndented = endLine - startLine + 1;
            setStatusMessage(std::to_string(linesIndented) + " line" +
                             (linesIndented > 1 ? "s" : "") + " <<");
            pendingShiftLeft = false;
            repeatCount = 0;
            saveState();
            needsFullRedraw = true;
            return;
        }
        else
        {
            // first '<'
            pendingShiftLeft = true;
            pendingShiftRight = false;
            pendingDelete = false;
            pendingYank = false;
            pendingIndent = false;
            return;
        }
    }
    else if(pendingYank && c != 'y')
    {
        // 'y' followed by motion command - enter operator-pending mode
        pendingYank = false;
        enterOperatorPending('y');
        pendingCount = count;
        // Process the motion command immediately
        handleOperatorPendingMode(c);
        repeatCount = 0;
        return;
    }
    else if(pendingDelete && c != 'd')
    {
        // 'd' followed by motion command - enter operator-pending mode
        pendingDelete = false;
        enterOperatorPending('d');
        pendingCount = count;
        // Process the motion command immediately
        handleOperatorPendingMode(c);
        repeatCount = 0;
        return;
    }
    else if(pendingIndent && c != '=')
    {
        // '=' followed by motion command - enter operator-pending mode
        pendingIndent = false;
        enterOperatorPending('=');
        pendingCount = count;
        // Process the motion command immediately
        handleOperatorPendingMode(c);
        repeatCount = 0;
        return;
    }
    else if(!pendingDelete && !pendingYank && !pendingIndent &&
            !pendingShiftRight && !pendingShiftLeft)
    {
        // Only reset if we're not in the middle of processing pending
        // operations
    }
    switch(c)
    {
    case Terminal::ESC:
    {
        // Handle double ESC to clear search highlights
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastEsc =
            std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                  lastEscTime)
                .count();

        if(timeSinceLastEsc <= DOUBLE_ESC_TIMEOUT_MS &&
           (!searchMatches.empty() || !searchQuery.empty()))
        {
            // Double ESC detected - clear search highlights
            clearSearch();
            setStatusMessage("Search cleared");
            needsFullRedraw = true; // Force full redraw to clear highlights
            lastEscTime = std::chrono::steady_clock::time_point(); // Reset
        }
        else
        {
            // First ESC or timeout exceeded
            lastEscTime = now;
            // Clear any pending operations
            pendingDelete = false;
            pendingYank = false;
            pendingIndent = false;
            pendingShiftRight = false;
            pendingShiftLeft = false;
            repeatCount = 0;
            commandBuffer.clear();
        }
    }
    break;
    case 'i':
        saveState();
        setMode(INSERT);
        break;
    case 'I':
        saveState();
        moveToLineStart();
        setMode(INSERT);
        break;
    case 'a':
        saveState();
        if(*cursorX < (*lines)[*cursorY].length())
            (*cursorX)++;
        setMode(INSERT);
        break;
    case 'A':
        saveState();
        moveToLineEnd();
        if(*cursorX < (*lines)[*cursorY].length())
            (*cursorX)++;
        setMode(INSERT);
        break;
    case 'o':
    {
        // Get indentation from current line
        const std::string& currentLine = (*lines)[*cursorY];
        size_t indent = 0;
        while(indent < currentLine.length() &&
              (currentLine[indent] == ' ' || currentLine[indent] == '\t'))
        {
            indent++;
        }
        std::string indentStr = currentLine.substr(0, indent);

        // Check if current line ends with { (add extra indent)
        bool addExtraIndent = false;
        if(isCppFile())
        {
            size_t lastNonSpace = currentLine.find_last_not_of(" \t");
            if(lastNonSpace != std::string::npos &&
               currentLine[lastNonSpace] == '{')
            {
                addExtraIndent = true;
            }
        }

        // Insert new line below with proper indentation
        std::string newLine = indentStr;
        if(addExtraIndent)
        {
            newLine += "    ";
        }
        lines->insert(lines->begin() + *cursorY + 1, newLine);
        (*cursorY)++;
        *cursorX = newLine.length();
        *dirty = true;
        needsFullRedraw = true;
        setMode(INSERT);
        saveState();
        break;
    }
    case 'O':
    {
        // Get indentation from current line
        const std::string& currentLine = (*lines)[*cursorY];
        size_t indent = 0;
        while(indent < currentLine.length() &&
              (currentLine[indent] == ' ' || currentLine[indent] == '\t'))
        {
            indent++;
        }
        std::string indentStr = currentLine.substr(0, indent);

        // Insert new line above with same indentation
        lines->insert(lines->begin() + *cursorY, indentStr);
        *cursorX = indentStr.length();
        *dirty = true;
        needsFullRedraw = true;
        setMode(INSERT);
        saveState();
        break;
    }
    case 'v':
        startVisualMode();
        break;
    case 'V':
        startVisualLineMode();
        break;
    case 22: // Ctrl-V (ASCII 22)
        startVisualBlockMode();
        break;
    case ':':
        setMode(COMMAND);
        break;
    case '/':
        startSearchForward();
        break;
    case '?':
        startSearchBackward();
        break;
    case 'n':
        searchNext();
        break;
    case 'N':
        searchPrevious();
        break;
    case '#':
    {
        // Vim-style: search backward for the word under the cursor.
        // Anchor at the start of the current word so we don't match the same
        // occurrence when the cursor is inside the word.
        std::string sym = getSymbolUnderCursor();
        if(sym.empty())
        {
            setStatusMessage("#: no word under cursor");
            break;
        }

        // Move cursor to the start of the current identifier.
        if(*cursorY >= 0 && *cursorY < (int)lines->size())
        {
            const std::string& line = (*lines)[*cursorY];
            int x = *cursorX;
            if(x >= (int)line.size())
                x = (int)line.size() - 1;

            while(x > 0 && isIdent(line[x - 1]))
                --x;
            *cursorX = x;
        }

        searchQuery = sym;
        searchForward = false;
        performSearch();
        needsFullRedraw = true;
        *wantedX = *cursorX;
        break;
    }
    case 30: // Ctrl+^ (Ctrl+6)
        if(buffers.size() > 1)
        {
            previousBuffer();
        }
        break;
    case Terminal::CTRL_P:
        setMode(FUZZY_FIND);
        break;
    case Terminal::CTRL_W: // Ctrl+W for buffer browser
        setMode(BUFFER_BROWSER);
        break;
    case Terminal::CTRL_S: // Ctrl+S for grep search (find in files)
        setMode(GREP_SEARCH);
        break;
    case Terminal::CTRL_O:
        jumpBack();
        break;
    case Terminal::CTRL_I:
        jumpForward();
        break;
    case 'h':
        moveLeft(count);
        break;
    case Terminal::ARROW_LEFT:
        moveLeft(count);
        break;
    case 'l':
    case Terminal::ARROW_RIGHT:
        moveRight(count);
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        moveDown(count);
        break;
    case 'k':
    case Terminal::ARROW_UP:
        moveUp(count);
        break;
    case Terminal::CTRL_D:
        scrollHalfPageDown(false);
        break;
    case Terminal::CTRL_U:
        scrollHalfPageUp(false);
        break;
    case 'w':
        while(count-- > 0)
            moveWordForward();
        break;
    case 'b':
        while(count-- > 0)
            moveWordBackward();
        break;
    case 'e':
        while(count-- > 0)
            moveToEndOfWord();
        break;
    case '0':
        if(repeatCount == 0)
            moveToLineStart();
        break;
    case '$':
        moveToLineEnd();
        break;
    case 'g':
        commandBuffer = "g";
        return;
    case 'G':
        if(repeatCount > 0)
        {
            moveToLine(repeatCount - 1);
        }
        else
        {
            moveToLastLine();
        }
        break;
    case ' ': // Leader key (space)
        if(commandBuffer == " ")
        {
            commandBuffer.clear(); // Double space cancels
        }
        else
        {
            commandBuffer = " ";
            setStatusMessage("Leader");
        }
        break;

    case 'x':
        while(count-- > 0)
        {
            deleteCharForward();
        }
        saveState();
        break;
    case 's':
        // Substitute: delete char(s) under cursor and enter insert mode
        while(count-- > 0)
        {
            deleteCharForward();
        }
        saveState();
        setMode(INSERT);
        break;
    case 'D':
        deleteToLineEnd();
        saveState();
        needsFullRedraw = true;
        break;
    case 'Y':
        yankToLineEnd();
        break;

    case 'J':
    {
        // Join lines: current line with next line(s)
        // count specifies how many lines to join (default 2 = current + next)
        int linesToJoin = (repeatCount > 0) ? repeatCount : 2;
        int joinCount = 0;

        for(int i = 0; i < linesToJoin - 1; ++i)
        {
            if(*cursorY >= (int)lines->size() - 1)
                break; // No more lines to join

            std::string& currentLine = (*lines)[*cursorY];
            std::string& nextLine = (*lines)[*cursorY + 1];

            // Remove trailing whitespace from current line
            size_t endPos = currentLine.find_last_not_of(" \t");
            if(endPos != std::string::npos)
                currentLine = currentLine.substr(0, endPos + 1);

            // Remove leading whitespace from next line
            size_t startPos = nextLine.find_first_not_of(" \t");
            std::string trimmedNext = (startPos != std::string::npos)
                                          ? nextLine.substr(startPos)
                                          : "";

            // Join with a single space (unless current line is empty)
            if(!currentLine.empty() && !trimmedNext.empty())
            {
                *cursorX =
                    currentLine.length(); // Position cursor at join point
                currentLine += " " + trimmedNext;
            }
            else if(currentLine.empty())
            {
                currentLine = trimmedNext;
                *cursorX = 0;
            }
            else
            {
                *cursorX = currentLine.length();
            }

            // Delete the next line
            lines->erase(lines->begin() + *cursorY + 1);
            joinCount++;
        }

        if(joinCount > 0)
        {
            *dirty = true;
            saveState();
            needsFullRedraw = true;
            if(joinCount > 1)
                setStatusMessage(std::to_string(joinCount + 1) +
                                 " lines joined");
        }
        break;
    }

    case 'c':
        // change operator: enter operator pending (support e.g. cw, ci(, etc.)
        enterOperatorPending('c');
        break;
    case 'p':
        pasteAfter();
        break;
    case 'P':
        pasteBefore();
        break;
    case 'u':
        undo();
        break;
    case Terminal::CTRL_R:
        redo();
        break;
    case '%':
        moveToMatchingBracket();
        adjustViewport();
        break;
    default:
        if(c != 'g' && c != 'd' && c != 'y')
        {
            commandBuffer.clear();
        }
        break;
    }

    // Handle g-prefixed commands at end (for 'gg' which needs switch case for
    // first 'g')
    if(commandBuffer == "g")
    {
        if(c == 'd')
        {
            commandBuffer.clear();
            goToDefinition();
            repeatCount = 0;
            return;
        }

        // Unknown g-command → cancel
        if(c != 'g')
        {
            commandBuffer.clear();
        }
    }

    repeatCount = 0;
}

void Editor::handleInsertMode(int c)
{

    // clangd completion popup navigation/accept (keep popup open while typing)
    if(completionActive)
    {
        if(c == Terminal::ESC)
        {
            cancelCompletion();
            needsFullRedraw = true;
            return;
        }
        if(c == Terminal::CTRL_J || c == Terminal::CTRL_N)
        {
            completionNext();
            return;
        }
        if(c == Terminal::CTRL_K || c == Terminal::CTRL_P)
        {
            completionPrev();
            return;
        }
        if(c == Terminal::ENTER || c == Terminal::TAB)
        {
            acceptCompletion();
            return;
        }
        // Otherwise fall through and treat the key as normal insert.
    }

    // Trigger completion manually in INSERT with Ctrl-N (navigate with
    // Ctrl-J/Ctrl-K)
    if(c == Terminal::CTRL_N && !completionActive)
    {
        requestCompletion();
        return;
    }

    if(c == Terminal::ESC)
    {
        if(visualBlockChanging)
        {
            int startY, startX, endY, endX;
            getVisualBlockBounds(startY, startX, endY, endX);

            for(int row = startY + 1; row <= endY; row++)
            {
                if(row >= lines->size())
                    continue;

                std::string& line = (*lines)[row];

                if(startX > line.length())
                    line.resize(startX, ' ');

                line.insert(startX, currentBuffer->visualBlockInsertText);
            }

            visualBlockChanging = false;
            saveState();
        }

        setMode(NORMAL);
        return;
    }

    if(c == Terminal::BACKSPACE || c == Terminal::DEL)
    {
        // Smart backspace: delete up to 4 spaces if we're in indentation
        if(*cursorX > 0 && *cursorY < lines->size())
        {
            const std::string& line = (*lines)[*cursorY];

            // Check if cursor is in the indentation area (only spaces/tabs
            // before cursor)
            bool inIndent = true;
            for(int i = 0; i < *cursorX; i++)
            {
                if(line[i] != ' ' && line[i] != '\t')
                {
                    inIndent = false;
                    break;
                }
            }

            if(inIndent && line[*cursorX - 1] == ' ')
            {
                // Calculate how many spaces to delete (up to 4, aligned to tab
                // stop)
                int spacesToDelete = ((*cursorX - 1) % 4) + 1;
                if(spacesToDelete == 0)
                    spacesToDelete = 4;

                // Make sure we have enough spaces to delete
                int actualSpaces = 0;
                for(int i = *cursorX - 1;
                    i >= 0 && actualSpaces < spacesToDelete; i--)
                {
                    if(line[i] == ' ')
                        actualSpaces++;
                    else
                        break;
                }

                // Delete the spaces
                (*lines)[*cursorY].erase(*cursorX - actualSpaces, actualSpaces);
                *cursorX -= actualSpaces;
                *dirty = true;
                if(completionActive)
                    rebuildCompletionFilter();
                return;
            }
        }

        deleteChar();
        if(completionActive)
            rebuildCompletionFilter();
        return;
    }

    if(c == Terminal::ENTER)
    {
        if(completionActive)
            cancelCompletion();
        insertNewline();
        return;
    }

    // Tab key: insert 4 spaces (aligned to tab stop)
    if(c == Terminal::TAB)
    {
        int spacesToInsert = 4 - (*cursorX % 4);
        if(spacesToInsert == 0)
            spacesToInsert = 4;

        for(int i = 0; i < spacesToInsert; i++)
        {
            insertChar(' ');
        }
        if(completionActive)
            rebuildCompletionFilter();
        return;
    }

    if(c >= 32 && c <= 126)
    {
        if(visualBlockChanging)
        {
            currentBuffer->visualBlockInsertText.push_back((char)c);
        }

        insertChar((char)c);
        if(completionActive)
            rebuildCompletionFilter();
    }
}

void Editor::handleVisualMode(int c)
{
    // Handle count prefix for visual mode (e.g., V4j)
    static int visualRepeatCount = 0;

    if(c >= '1' && c <= '9' && visualRepeatCount == 0)
    {
        visualRepeatCount = c - '0';
        return;
    }
    else if(c >= '0' && c <= '9' && visualRepeatCount > 0)
    {
        visualRepeatCount = visualRepeatCount * 10 + (c - '0');
        return;
    }

    int count = std::max(1, visualRepeatCount);
    visualRepeatCount = 0; // Reset after use

    switch(c)
    {
    case Terminal::ESC:
        setMode(NORMAL);
        statusMessage.clear();
        needsFullRedraw = true;
        break;
    case 'h':
    case Terminal::ARROW_LEFT:
        for(int i = 0; i < count; ++i)
            moveLeft();
        updateVisualSelection();
        needsFullRedraw = true;
        break;
    case 'l':
    case Terminal::ARROW_RIGHT:
        for(int i = 0; i < count; ++i)
            moveRight();
        updateVisualSelection();
        needsFullRedraw = true;
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        for(int i = 0; i < count; ++i)
            moveDown();
        updateVisualSelection();
        needsFullRedraw = true;
        break;
    case 'k':
    case Terminal::ARROW_UP:
        for(int i = 0; i < count; ++i)
            moveUp();
        updateVisualSelection();
        needsFullRedraw = true;
        break;
    case Terminal::CTRL_D:
        scrollHalfPageDown(true);
        break;

    case Terminal::CTRL_U:
        scrollHalfPageUp(true);
        break;
    case 'G': // visual G → extend to last line
        moveToLastLine();
        updateVisualSelection();
        adjustViewport();
        break;
    case '%':
        moveToMatchingBracket();
        updateVisualSelection();
        adjustViewport();
        break;
    case 'g': // possible gg
        if(commandBuffer == "g")
        {
            moveToFirstLine();
            updateVisualSelection();
            adjustViewport();
            commandBuffer.clear();
        }
        else
        {
            commandBuffer = "g";
        }
        break;
    case 'd':
    case 'x':
        deleteSelection();
        setMode(NORMAL);
        saveState();
        needsFullRedraw = true;
        break;
    case 'y':
        yankSelection();
        setMode(NORMAL);
        needsFullRedraw = true;
        break;
    case ' ':
        // Leader key in visual mode - wait for next key
        {
            int nextKey = Terminal::readKey();
            if(nextKey == 'y')
            {
                // Space + y: yank selection to system clipboard
                yankSelection();
                setSystemClipboard(yankBuffer);
                setMode(NORMAL);
                needsFullRedraw = true;
                setStatusMessage("Selection yanked to system clipboard");
            }
            else if(nextKey == 'd')
            {
                // Space + d: delete selection and copy to system clipboard
                yankSelection();
                setSystemClipboard(yankBuffer);
                deleteSelection();
                setMode(NORMAL);
                saveState();
                needsFullRedraw = true;
                setStatusMessage("Selection cut to system clipboard");
            }
            else if(nextKey == 'f')
            {
                // Space + f: format file with clang-format
                setMode(NORMAL);

                if(!isCppFile())
                {
                    setStatusMessage("clang-format: not a C/C++ file");
                    break;
                }

                // Save current cursor position
                int savedY = *cursorY;
                int savedX = *cursorX;

                // Write buffer to temp file
                std::string tempPath =
                    "/tmp/uvim_format_" + std::to_string(getpid()) + ".tmp";
                std::ofstream tempFile(tempPath);
                if(!tempFile.is_open())
                {
                    setStatusMessage(
                        "clang-format: failed to create temp file");
                    break;
                }

                // Write content with trailing newline
                for(size_t i = 0; i < lines->size(); ++i)
                {
                    tempFile << (*lines)[i] << '\n';
                }
                tempFile.close();

                // Get the directory of the current file for clang-format to
                // find .clang-format
                std::string absFilename = *filename;

                // Make filename absolute if it isn't
                if(!absFilename.empty() && absFilename[0] != '/')
                {
                    char cwd[PATH_MAX];
                    if(getcwd(cwd, sizeof(cwd)))
                    {
                        absFilename = std::string(cwd) + "/" + *filename;
                    }
                }

                // Run clang-format with stdin, using actual filename for style
                // lookup
                std::string cmd =
                    "cat \"" + tempPath +
                    "\" | /opt/homebrew/bin/clang-format -style=file"
                    " -assume-filename=\"" +
                    absFilename +
                    "\""
                    " 2>/tmp/uvim_clang_err.txt";

                FILE* pipe = popen(cmd.c_str(), "r");
                if(!pipe)
                {
                    // Try without full path
                    cmd = "cat \"" + tempPath +
                          "\" | clang-format -style=file"
                          " -assume-filename=\"" +
                          absFilename +
                          "\""
                          " 2>/tmp/uvim_clang_err.txt";
                    pipe = popen(cmd.c_str(), "r");
                }

                if(!pipe)
                {
                    unlink(tempPath.c_str());
                    setStatusMessage("clang-format: failed to run");
                    break;
                }

                // Read formatted output
                std::string formatted;
                char buffer[4096];
                while(fgets(buffer, sizeof(buffer), pipe))
                {
                    formatted += buffer;
                }
                int status = pclose(pipe);
                unlink(tempPath.c_str());

                // Check for errors
                if(formatted.empty())
                {
                    // Read error file
                    std::ifstream errFile("/tmp/uvim_clang_err.txt");
                    std::string errMsg;
                    if(errFile.is_open())
                    {
                        std::getline(errFile, errMsg);
                        errFile.close();
                    }
                    if(errMsg.empty())
                        errMsg = "no output (exit=" +
                                 std::to_string(WEXITSTATUS(status)) + ")";
                    setStatusMessage("clang-format: " + errMsg.substr(0, 50));
                    break;
                }

                // Parse formatted output into lines
                std::vector<std::string> newLines;
                std::istringstream iss(formatted);
                std::string line;
                while(std::getline(iss, line))
                {
                    // Remove \r if present
                    if(!line.empty() && line.back() == '\r')
                        line.pop_back();
                    newLines.push_back(line);
                }

                // Remove trailing empty line if present (clang-format adds one)
                if(!newLines.empty() && newLines.back().empty())
                {
                    newLines.pop_back();
                }

                // Ensure at least one line
                if(newLines.empty())
                {
                    newLines.push_back("");
                }

                // Check if anything changed
                if(newLines == *lines)
                {
                    setStatusMessage("clang-format: no changes needed");
                    break;
                }

                // Save state for undo
                saveState();

                // Replace buffer content
                *lines = newLines;
                *dirty = true;

                // Restore cursor position (clamped to valid range)
                if(lines->empty())
                {
                    *cursorY = 0;
                    *cursorX = 0;
                }
                else
                {
                    *cursorY = savedY;
                    if(*cursorY >= (int)lines->size())
                        *cursorY = (int)lines->size() - 1;
                    if(*cursorY < 0)
                        *cursorY = 0;

                    *cursorX = savedX;
                    int lineLen = (int)(*lines)[*cursorY].length();
                    if(*cursorX > lineLen)
                        *cursorX = lineLen > 0 ? lineLen - 1 : 0;
                    if(*cursorX < 0)
                        *cursorX = 0;
                }

                adjustViewport();
                needsFullRedraw = true;
                setStatusMessage("clang-format: formatted " +
                                 std::to_string(lines->size()) + " lines");
            }
            // Other space combinations in visual mode are ignored
        }
        break;
    case 'c':
        // Change - delete selection and enter insert mode
        {
            int startY, startX, endY, endX;
            if(currentMode == VISUAL_LINE)
            {
                // For visual line mode, delete whole lines and insert new line
                startY = std::min(currentBuffer->visualStartY,
                                  currentBuffer->visualEndY);
                endY = std::max(currentBuffer->visualStartY,
                                currentBuffer->visualEndY);

                // Delete the selected lines
                for(int i = endY; i >= startY; i--)
                {
                    lines->erase(lines->begin() + i);
                }

                // Insert empty line at start position
                lines->insert(lines->begin() + startY, "");

                *cursorY = startY;
                *cursorX = 0;
            }
            else
            {
                // For character-wise visual mode
                getSelectionBounds(startY, startX, endY, endX);

                // Delete the selection (same logic as deleteSelection)
                if(startY == endY)
                {
                    (*lines)[startY].erase(startX, endX - startX + 1);
                }
                else
                {
                    (*lines)[startY] = (*lines)[startY].substr(0, startX) +
                                       (*lines)[endY].substr(endX + 1);
                    for(int i = endY; i > startY; i--)
                    {
                        lines->erase(lines->begin() + i);
                    }
                }

                *cursorY = startY;
                *cursorX = startX;
            }

            *dirty = true;
            saveState();
            setMode(INSERT);
            needsFullRedraw = true;
        }
        break;
    case '=':
        // Indent selected lines
        {
            int startY, startX, endY, endX;
            getSelectionBounds(startY, startX, endY, endX);
            autoIndentRange(startY, endY);

            int linesIndented = endY - startY + 1;
            setStatusMessage(std::to_string(linesIndented) + " line" +
                             (linesIndented > 1 ? "s" : "") + " indented");
            setMode(NORMAL);
            saveState();
            needsFullRedraw = true;
        }
        break;

    case 'J':
        // Join all selected lines into one
        {
            int startY, startX, endY, endX;
            getSelectionBounds(startY, startX, endY, endX);

            int linesToJoin = endY - startY + 1;
            if(linesToJoin <= 1)
            {
                setMode(NORMAL);
                break;
            }

            // Move cursor to start line
            *cursorY = startY;

            // Join all lines from startY to endY
            int joinCount = 0;
            for(int i = 0; i < linesToJoin - 1; ++i)
            {
                if(*cursorY >= (int)lines->size() - 1)
                    break;

                std::string& currentLine = (*lines)[*cursorY];
                std::string& nextLine = (*lines)[*cursorY + 1];

                // Remove trailing whitespace from current line
                size_t endPos = currentLine.find_last_not_of(" \t");
                if(endPos != std::string::npos)
                    currentLine = currentLine.substr(0, endPos + 1);

                // Remove leading whitespace from next line
                size_t startPos = nextLine.find_first_not_of(" \t");
                std::string trimmedNext = (startPos != std::string::npos)
                                              ? nextLine.substr(startPos)
                                              : "";

                // Join with a single space
                if(!currentLine.empty() && !trimmedNext.empty())
                {
                    currentLine += " " + trimmedNext;
                }
                else if(currentLine.empty())
                {
                    currentLine = trimmedNext;
                }

                // Delete the next line
                lines->erase(lines->begin() + *cursorY + 1);
                joinCount++;
            }

            *cursorX = 0;
            *dirty = true;
            saveState();
            setMode(NORMAL);
            needsFullRedraw = true;
            setStatusMessage(std::to_string(joinCount + 1) + " lines joined");
        }
        break;

    case 'w': // Visual w - extend selection forward by word
        for(int i = 0; i < count; ++i)
            moveWordForward();
        updateVisualSelection();
        adjustViewport();
        break;

    case 'b': // Visual b - extend selection backward by word
        for(int i = 0; i < count; ++i)
            moveWordBackward();
        updateVisualSelection();
        adjustViewport();
        break;

    case 'e': // Visual e - extend selection to end of word
        for(int i = 0; i < count; ++i)
            moveToEndOfWord();
        updateVisualSelection();
        adjustViewport();
        break;

    case '0': // Visual 0 - extend to start of line
        moveToLineStart();
        updateVisualSelection();
        adjustViewport();
        break;

    case '$': // Visual $ - extend to end of line
        moveToLineEnd();
        updateVisualSelection();
        adjustViewport();
        break;
    case 'p':
    case 'P':
        // In visual mode, 'p' replaces the selection with yanked text
        if(!yankBuffer.empty())
        {
            // Store the current yank buffer content
            std::string pasteContent = yankBuffer;

            // Get selection bounds
            int startY, startX, endY, endX;

            if(currentMode == VISUAL_LINE)
            {
                // For visual line mode, we work with whole lines
                startY = std::min(currentBuffer->visualStartY,
                                  currentBuffer->visualEndY);
                endY = std::max(currentBuffer->visualStartY,
                                currentBuffer->visualEndY);

                // Delete the selected lines
                for(int i = endY; i >= startY; i--)
                {
                    lines->erase(lines->begin() + i);
                }

                if(lines->empty())
                {
                    lines->push_back("");
                }

                // Set cursor to start of deleted region
                *cursorY = std::min(startY, (int)lines->size() - 1);
                *cursorX = 0;
            }
            else
            {
                // For character-wise visual mode
                getSelectionBounds(startY, startX, endY, endX);

                // Delete the selected range
                deleteRange(startY, startX, endY, endX);

                // Set cursor to start of deleted region
                *cursorY = startY;
                *cursorX = startX;
            }

            // Restore the yank buffer (deleteRange doesn't modify it, but just
            // to be safe)
            yankBuffer = pasteContent;

            // Now paste the content
            // Use pasteBefore since we're at the start of where we deleted
            if(yankBuffer.back() == '\n')
            {
                // Line-wise paste - insert before current line
                std::istringstream ss(yankBuffer);
                std::string line;
                int insertPos = *cursorY;

                while(std::getline(ss, line))
                {
                    lines->insert(lines->begin() + insertPos, line);
                    insertPos++;
                }
                *cursorX = 0;
            }
            else
            {
                // Character-wise paste - insert at cursor position
                (*lines)[*cursorY].insert(*cursorX, yankBuffer);
                *cursorX += yankBuffer.length() - 1;
            }

            *dirty = true;
            setMode(NORMAL);
            saveState();
            needsFullRedraw = true;
            setStatusMessage("Pasted over selection");
        }
        else
        {
            setStatusMessage("Nothing to paste");
            setMode(NORMAL);
        }
        break;
    }
}

void Editor::handleVisualBlockMode(int c)
{
    // Track if we're collecting text in INSERT mode after 'c'
    static bool inBlockInsert = false;
    static int blockInsertStartX = 0;
    static bool changeMode = false; // Track if we entered via 'c' command

    if(inBlockInsert)
    {
        // We're in INSERT mode after pressing 'c' in visual block
        switch(c)
        {
        case Terminal::ESC:
        {
            // Check for double ESC to apply to all lines
            auto now = std::chrono::steady_clock::now();
            auto timeSinceLastEsc =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - lastEscTime)
                    .count();

            if(timeSinceLastEsc <= DOUBLE_ESC_TIMEOUT_MS)
            {
                // Double ESC - apply insert to all lines
                // Capture what was inserted on current line
                std::string& currentLine = (*lines)[*cursorY];
                if(*cursorX > blockInsertStartX)
                {
                    currentBuffer->visualBlockInsertText = currentLine.substr(
                        blockInsertStartX, *cursorX - blockInsertStartX);
                }
                else if(changeMode && *cursorX >= blockInsertStartX)
                {
                    // For change mode, even if cursor didn't move forward,
                    // capture any replacement text
                    currentBuffer->visualBlockInsertText = currentLine.substr(
                        blockInsertStartX, *cursorX - blockInsertStartX);
                }

                applyVisualBlockInsert();
                inBlockInsert = false;
                changeMode = false;
                setMode(NORMAL);
                lastEscTime = std::chrono::steady_clock::time_point();

                // Clear visual block bounds after applying
                currentBuffer->visualBlockStartY = -1;
                currentBuffer->visualBlockEndY = -1;
            }
            else
            {
                // Single ESC - just exit insert mode
                lastEscTime = now;
                inBlockInsert = false;
                changeMode = false;
                if(*cursorX > 0)
                    (*cursorX)--;
                setMode(NORMAL);
                saveState();
            }
        }
        break;

        default:
            // Let normal insert mode handle the character
            handleInsertMode(c);
            break;
        }
        return;
    }

    // Handle count prefix for visual block mode (e.g., Ctrl-V 4j)
    static int blockRepeatCount = 0;

    if(c >= '1' && c <= '9' && blockRepeatCount == 0)
    {
        blockRepeatCount = c - '0';
        return;
    }
    else if(c >= '0' && c <= '9' && blockRepeatCount > 0)
    {
        blockRepeatCount = blockRepeatCount * 10 + (c - '0');
        return;
    }

    int count = std::max(1, blockRepeatCount);
    blockRepeatCount = 0; // Reset after use

    // Normal visual block mode commands
    switch(c)
    {
    case Terminal::ESC:
        setMode(NORMAL);
        statusMessage.clear();
        needsFullRedraw = true;
        break;

    case 'h':
    case Terminal::ARROW_LEFT:
        for(int i = 0; i < count; ++i)
            moveLeft();
        updateVisualBlockSelection();
        needsFullRedraw = true;
        break;

    case 'l':
    case Terminal::ARROW_RIGHT:
        for(int i = 0; i < count; ++i)
            moveRight();
        updateVisualBlockSelection();
        needsFullRedraw = true;
        break;

    case 'j':
    case Terminal::ARROW_DOWN:
        for(int i = 0; i < count; ++i)
            moveDown();
        updateVisualBlockSelection();
        needsFullRedraw = true;
        break;

    case 'k':
    case Terminal::ARROW_UP:
        for(int i = 0; i < count; ++i)
            moveUp();
        updateVisualBlockSelection();
        needsFullRedraw = true;
        break;

    case 'd':
    case 'x':
        deleteVisualBlock();
        break;

    case 'y':
        yankVisualBlock();
        setMode(NORMAL);
        needsFullRedraw = true;
        break;

    case 'c':
        // Change - delete block and enter insert mode
        blockInsertStartX = currentBuffer->visualBlockStartX;
        changeVisualBlock();
        inBlockInsert = true;
        changeMode = true; // Mark that we entered via 'c'
        currentBuffer->visualBlockInsertText.clear(); // Clear any old text
        break;

    case 'I':
        // Insert at beginning of block
        *cursorX = std::min(currentBuffer->visualBlockStartX,
                            currentBuffer->visualBlockEndX);
        blockInsertStartX = *cursorX;
        setMode(INSERT);
        inBlockInsert = true;
        changeMode = false; // Not in change mode
        break;

    case 'A':
        // Append at end of block
        *cursorX = std::max(currentBuffer->visualBlockStartX,
                            currentBuffer->visualBlockEndX) +
                   1;
        if(*cursorX > (*lines)[*cursorY].length())
            *cursorX = (*lines)[*cursorY].length();
        blockInsertStartX = *cursorX;
        setMode(INSERT);
        inBlockInsert = true;
        changeMode = false; // Not in change mode
        break;

    case 'p':
    case 'P':
        // Paste - for block yanks, paste as a block
        if(!yankBuffer.empty())
        {
            if(yankBuffer[0] == '\x02') // Block yank marker
            {
                // Block paste
                std::string blockContent = yankBuffer.substr(1);
                std::istringstream ss(blockContent);
                std::string line;
                int row = *cursorY;

                while(std::getline(ss, line) && row < lines->size())
                {
                    std::string& destLine = (*lines)[row];
                    int insertPos = (c == 'p') ? *cursorX + 1 : *cursorX;
                    if(insertPos > destLine.length())
                        insertPos = destLine.length();
                    destLine.insert(insertPos, line);
                    row++;
                }

                *dirty = true;
                saveState();
                setMode(NORMAL);
                needsFullRedraw = true;
                setStatusMessage("Block pasted");
            }
            else
            {
                // Regular paste - just paste at cursor
                setMode(NORMAL);
                if(c == 'p')
                    pasteAfter();
                else
                    pasteBefore();
            }
        }
        break;

    // Movement commands
    case '0':
        moveToLineStart();
        updateVisualBlockSelection();
        break;

    case '$':
        moveToLineEnd();
        updateVisualBlockSelection();
        break;

    case 'G':
        moveToLastLine();
        updateVisualBlockSelection();
        break;

    case 'g':
        if(commandBuffer == "g")
        {
            moveToFirstLine();
            updateVisualBlockSelection();
            commandBuffer.clear();
        }
        else
        {
            commandBuffer = "g";
        }
        break;
    }
}

// Helper function for command-line path completion
static std::vector<std::string> getPathCompletions(const std::string& partial)
{
    std::vector<std::string> completions;

    std::string dirPath;
    std::string prefix;

    // Handle ~ expansion
    std::string expandedPartial = partial;
    if(!expandedPartial.empty() && expandedPartial[0] == '~')
    {
        const char* home = getenv("HOME");
        if(home)
            expandedPartial = std::string(home) + expandedPartial.substr(1);
    }

    size_t lastSlash = expandedPartial.find_last_of('/');
    if(lastSlash != std::string::npos)
    {
        dirPath = expandedPartial.substr(0, lastSlash);
        if(dirPath.empty())
            dirPath = "/";
        prefix = expandedPartial.substr(lastSlash + 1);
    }
    else
    {
        dirPath = ".";
        prefix = expandedPartial;
    }

    DIR* dir = opendir(dirPath.c_str());
    if(!dir)
        return completions;

    struct dirent* entry;
    while((entry = readdir(dir)) != nullptr)
    {
        std::string name = entry->d_name;

        // Skip . and ..
        if(name == "." || name == "..")
            continue;

        // Skip hidden files unless prefix starts with .
        if(name[0] == '.' && (prefix.empty() || prefix[0] != '.'))
            continue;

        // Check if name starts with prefix
        if(prefix.empty() || name.substr(0, prefix.length()) == prefix)
        {
            std::string fullPath;
            if(lastSlash != std::string::npos)
            {
                // Keep original path format (with ~ if used)
                if(!partial.empty() && partial[0] == '~')
                {
                    size_t origSlash = partial.find_last_of('/');
                    fullPath = partial.substr(0, origSlash + 1) + name;
                }
                else
                {
                    fullPath = dirPath + "/" + name;
                }
            }
            else
            {
                fullPath = name;
            }

            // Check if it's a directory and append /
            struct stat st;
            std::string checkPath = dirPath + "/" + name;
            if(stat(checkPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
            {
                fullPath += "/";
            }

            completions.push_back(fullPath);
        }
    }

    closedir(dir);

    // Sort completions
    std::sort(completions.begin(), completions.end());

    return completions;
}

// Find longest common prefix among completions
static std::string longestCommonPrefix(const std::vector<std::string>& strings)
{
    if(strings.empty())
        return "";
    if(strings.size() == 1)
        return strings[0];

    std::string prefix = strings[0];
    for(size_t i = 1; i < strings.size(); ++i)
    {
        size_t j = 0;
        while(j < prefix.length() && j < strings[i].length() &&
              prefix[j] == strings[i][j])
        {
            ++j;
        }
        prefix = prefix.substr(0, j);
        if(prefix.empty())
            break;
    }
    return prefix;
}

void Editor::handleCommandMode(int c)
{
    // Static state for Tab completion cycling
    static std::vector<std::string> tabCompletions;
    static size_t tabIndex = 0;
    static std::string tabOriginal;
    static std::string tabCmdPrefix;

    switch(c)
    {
    case Terminal::TAB:
    {
        std::string cmd = commandBuffer.substr(1); // Remove leading ':'

        // Check if this is a command that needs path completion
        // Support: :e, :e<partial>, :e <partial>, :edit, :edit <partial>
        //          :cd, :cd <partial>, :w <partial>
        bool isEditCmd =
            (cmd == "e" || cmd == "edit" || cmd.substr(0, 2) == "e " ||
             cmd.substr(0, 5) == "edit ");
        bool isCdCmd = (cmd == "cd" || cmd.substr(0, 3) == "cd ");
        bool isWCmd = (cmd.substr(0, 2) == "w ");

        // Also handle :e followed directly by text (no space), like :efoo
        if(!isEditCmd && !isCdCmd && !isWCmd && cmd.length() > 1 &&
           cmd[0] == 'e')
        {
            // Check if it looks like :e<filename> (no space)
            isEditCmd = true;
        }

        if(isEditCmd || isCdCmd || isWCmd)
        {
            std::string pathStart;
            std::string cmdPrefix;

            if(isEditCmd)
            {
                if(cmd == "e" || cmd == "edit")
                {
                    cmdPrefix = ":" + cmd + " ";
                    pathStart = "";
                }
                else if(cmd.substr(0, 5) == "edit ")
                {
                    cmdPrefix = ":edit ";
                    pathStart = cmd.substr(5);
                }
                else if(cmd.substr(0, 2) == "e ")
                {
                    cmdPrefix = ":e ";
                    pathStart = cmd.substr(2);
                }
                else if(cmd[0] == 'e')
                {
                    // :efoo -> treat as :e foo
                    cmdPrefix = ":e ";
                    pathStart = cmd.substr(1);
                }
            }
            else if(isCdCmd)
            {
                if(cmd == "cd")
                {
                    cmdPrefix = ":cd ";
                    pathStart = "";
                }
                else
                {
                    cmdPrefix = ":cd ";
                    pathStart = cmd.substr(3);
                }
            }
            else // isWCmd
            {
                cmdPrefix = ":w ";
                pathStart = cmd.substr(2);
            }

            // Check if we're continuing a previous Tab sequence
            // (same path and same command prefix)
            if(tabCompletions.empty() || tabOriginal != pathStart ||
               tabCmdPrefix != cmdPrefix)
            {
                // New completion sequence
                tabOriginal = pathStart;
                tabCmdPrefix = cmdPrefix;
                tabCompletions = getPathCompletions(pathStart);
                tabIndex = 0;

                if(tabCompletions.empty())
                {
                    setStatusMessage("No matches");
                    break;
                }

                // If there's a common prefix longer than what we have, complete
                // to that
                std::string common = longestCommonPrefix(tabCompletions);
                if(common.length() > pathStart.length())
                {
                    commandBuffer = cmdPrefix + common;
                    tabOriginal = common;
                    if(tabCompletions.size() > 1)
                    {
                        setStatusMessage(std::to_string(tabCompletions.size()) +
                                         " matches");
                    }
                    break;
                }
            }

            // Cycle through completions
            if(!tabCompletions.empty())
            {
                commandBuffer = cmdPrefix + tabCompletions[tabIndex];
                if(tabCompletions.size() > 1)
                {
                    setStatusMessage("(" + std::to_string(tabIndex + 1) + "/" +
                                     std::to_string(tabCompletions.size()) +
                                     ") " + tabCompletions[tabIndex]);
                }
                tabIndex = (tabIndex + 1) % tabCompletions.size();
            }
        }
        break;
    }
    case Terminal::ENTER:
    {
        // Clear Tab completion state
        tabCompletions.clear();
        tabIndex = 0;
        tabOriginal.clear();
        tabCmdPrefix.clear();

        std::string cmd = commandBuffer.substr(1);

        if(cmd == "q!")
        {
            forceQuit();
            return;
        }

        if(cmd == "q")
        {
            // refuse quit if ANY buffer is dirty
            for(const auto& buf : buffers)
            {
                if(buf->dirty)
                {
                    setStatusMessage("No write since last change (use :q!)");
                    setMode(NORMAL);
                    return;
                }
            }

            // clean quit
            Terminal::restoreTerminal();
            std::exit(0);
        }
    }
        executeCommand(commandBuffer.substr(1));
        setMode(NORMAL);
        break;
    case Terminal::ESC:
        // Clear Tab completion state
        tabCompletions.clear();
        tabIndex = 0;
        tabOriginal.clear();
        tabCmdPrefix.clear();

        setMode(NORMAL);
        statusMessage.clear();
        break;
    case Terminal::BACKSPACE:
    case Terminal::DEL:
        // Clear Tab completion state on edit
        tabCompletions.clear();
        tabIndex = 0;
        tabOriginal.clear();
        tabCmdPrefix.clear();

        if(commandBuffer.length() > 1)
        {
            commandBuffer.pop_back();
        }
        else
        {
            setMode(NORMAL);
        }
        break;
    default:
        // Clear Tab completion state on any other input
        tabCompletions.clear();
        tabIndex = 0;
        tabOriginal.clear();
        tabCmdPrefix.clear();

        if(c >= 32 && c < 127)
        {
            commandBuffer += (char)c;
        }
        break;
    }
}

void Editor::handleSearchMode(int c)
{
    switch(c)
    {
    case Terminal::ENTER:
        performSearch();
        setMode(NORMAL);
        break;

    case Terminal::ESC:
        cancelSearch();
        break;

    case Terminal::BACKSPACE:
    case Terminal::DEL:
        if(!searchQuery.empty())
        {
            searchQuery.pop_back();
            commandBuffer = (searchForward ? "/" : "?") + searchQuery;
            findAllMatches();
            if(!searchMatches.empty())
            {
                jumpToMatch(searchForward ? 0 : searchMatches.size() - 1);
            }
        }
        else
        {
            cancelSearch();
        }
        break;

    default:
        if(c >= 32 && c < 127)
        {
            searchQuery += (char)c;
            commandBuffer = (searchForward ? "/" : "?") + searchQuery;
            findAllMatches();
            if(!searchMatches.empty())
            {
                jumpToMatch(searchForward ? 0 : searchMatches.size() - 1);
            }
            else
            {
                *cursorX = savedCursorX;
                *cursorY = savedCursorY;
            }
        }
        break;
    }
}

void Editor::handleFileBrowserMode(int c)
{
    switch(c)
    {
    case Terminal::ENTER:
    case 'l':
    case Terminal::ARROW_RIGHT:
        if(browserCursor < fileList.size())
        {
            navigateTo(fileList[browserCursor]);
        }
        break;
    case 'h':
    case Terminal::ARROW_LEFT:
    case '-':
        if(currentDirectory != "/" && currentDirectory != "")
        {
            size_t lastSlash = currentDirectory.find_last_of("/");
            std::string parentDir = "/";
            if(lastSlash != std::string::npos && lastSlash > 0)
            {
                parentDir = currentDirectory.substr(0, lastSlash);
            }
            openFileBrowser(parentDir);
        }
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        if(browserCursor < fileList.size() - 1)
        {
            browserCursor++;
            if(browserCursor >= browserOffset + screenRows - 2)
            {
                browserOffset = browserCursor - screenRows + 3;
            }
        }
        break;
    case 'k':
    case Terminal::ARROW_UP:
        if(browserCursor > 0)
        {
            browserCursor--;
            if(browserCursor < browserOffset)
            {
                browserOffset = browserCursor;
            }
        }
        break;

    case 'g':
        browserCursor = 0;
        browserOffset = 0;
        break;
    case 'G':
        if(fileList.size() > 0)
        {
            browserCursor = fileList.size() - 1;
            if(fileList.size() > screenRows - 2)
            {
                browserOffset = fileList.size() - screenRows + 2;
            }
        }
        break;
    case '.':
        toggleHidden();
        break;
    case 'R':
        loadDirectory(currentDirectory);
        setStatusMessage("Refreshed");
        break;
    case 'q':
    case Terminal::ESC:
        if(!previousFile.empty())
        {
            openFile(previousFile);
        }
        setMode(NORMAL);
        break;
    case '?':
        setStatusMessage(
            "[Enter/l]:open [h]:parent [j/k]:nav [.]:hidden [q]:quit");
        break;
    }

    needsFullRedraw = true;
}

void Editor::handleKeypress()
{
    int c = Terminal::readKey();

#ifdef UVIM_DEBUG_LOGGING
    // Debug: log every keypress with mode
    {
        std::ofstream dbg("/tmp/uvim_debug.txt", std::ios::app);
        dbg << "handleKeypress c=" << c << " ('" << (char)c
            << "') mode=" << currentMode << std::endl;
    }
#endif

    switch(currentMode)
    {
    case NORMAL:
        handleNormalMode(c);
        break;
    case INSERT:
        handleInsertMode(c);
        break;
    case VISUAL:
    case VISUAL_LINE:
        handleVisualMode(c);
        break;
    case VISUAL_BLOCK:
        handleVisualBlockMode(c);
        break;
    case COMMAND:
        handleCommandMode(c);
        break;
    case SEARCH_FORWARD:
    case SEARCH_BACKWARD:
        handleSearchMode(c);
        break;
    case FILE_BROWSER:
        handleFileBrowserMode(c);
        break;
    case FUZZY_FIND:
        handleFuzzyFindMode(c);
        break;
    case BUFFER_BROWSER:
        handleBufferBrowserMode(c);
        break;
    case GREP_SEARCH:
        handleGrepSearchMode(c);
        break;
    case OP_PENDING:
        handleOperatorPendingMode(c);
        break;
    }
}

void Editor::run()
{
    setStatusMessage("Welcome to uVim!");

    while(true)
    {
        draw();
        handleKeypress();
    }
}
