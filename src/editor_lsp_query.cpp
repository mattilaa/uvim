#include "editor_lsp_query.h"
#include "terminal.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
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

Editor::Editor(bool skipInitialBuffer)
{
    Terminal::enableRawMode();
    Terminal::getWindowSize(screenRows, screenCols);
    screenRows -= 2; // Status bar and message bar

    // Create initial empty buffer only if not opening files
    //if (!skipInitialBuffer)
    {
        createNewBuffer();
        saveState();
        currentBuffer->savedUndoIndex = 0; // Mark initial empty buffer as saved
    }
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
        {
            // For dw/cw: delete from cursor to start of next word (exclusive)
            // This is vim's behavior: delete current word + trailing whitespace
            // but stay on the same line
            const std::string& line = (*lines)[*cursorY];
            int end = *cursorX;

            // Helper lambda for word character check
            auto isWordChar = [](char ch)
            {
                return std::isalnum(static_cast<unsigned char>(ch)) ||
                       ch == '_';
            };

            if(end < (int)line.length())
            {
                char startChar = line[end];

                if(std::isspace(static_cast<unsigned char>(startChar)))
                {
                    // On whitespace: skip whitespace then skip next word/punct
                    while(end < (int)line.length() &&
                          std::isspace(static_cast<unsigned char>(line[end])))
                        end++;

                    if(end < (int)line.length())
                    {
                        if(isWordChar(line[end]))
                        {
                            while(end < (int)line.length() &&
                                  isWordChar(line[end]))
                                end++;
                        }
                        else
                        {
                            while(end < (int)line.length() &&
                                  !isWordChar(line[end]) &&
                                  !std::isspace(
                                      static_cast<unsigned char>(line[end])))
                                end++;
                        }
                    }
                }
                else if(isWordChar(startChar))
                {
                    // On word: skip word + trailing whitespace
                    while(end < (int)line.length() && isWordChar(line[end]))
                        end++;
                    while(end < (int)line.length() &&
                          std::isspace(static_cast<unsigned char>(line[end])))
                        end++;
                }
                else
                {
                    // On punctuation: skip punctuation + trailing whitespace
                    while(end < (int)line.length() && !isWordChar(line[end]) &&
                          !std::isspace(static_cast<unsigned char>(line[end])))
                        end++;
                    while(end < (int)line.length() &&
                          std::isspace(static_cast<unsigned char>(line[end])))
                        end++;
                }
            }

            // Set destination
            *cursorX = end;
            // 'w' is exclusive, so we'll subtract 1 later
            isExclusiveMotion = true;
        }
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

    // DEBUG: Write to file what we're about to delete
    {
        std::ofstream dbg("/tmp/uvim_dw_debug.txt", std::ios::app);
        dbg << "=== dw operation ===" << std::endl;
        dbg << "op=" << op << std::endl;
        dbg << "startY=" << startY << " startX=" << startX << std::endl;
        dbg << "endY=" << endY << " endX=" << endX << std::endl;
        if(startY < (int)lines->size())
        {
            dbg << "line[" << startY << "]=" << (*lines)[startY] << std::endl;
            dbg << "deleting chars " << startX << " to " << endX << std::endl;
            if(startY == endY && startX < (int)(*lines)[startY].length())
            {
                dbg << "text to delete: ["
                    << (*lines)[startY].substr(startX, endX - startX + 1) << "]"
                    << std::endl;
            }
        }
        dbg << std::endl;
    }

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

// yankRange and deleteRange are now in text_operations.cpp

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

// Cursor movement methods (moveLeft through moveToMatchingBracket) are now in
// cursor_movement.cpp

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

// Text operations (insertChar through pasteBefore) are now in
// text_operations.cpp

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

// deleteVisualBlock, yankVisualBlock, changeVisualBlock,
// applyVisualBlockInsert, deleteSelection are now in text_operations.cpp

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

// adjustViewport and centerScreen are now in cursor_movement.cpp

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

std::string Editor::getAlternateFilePath()
{
    if(!currentBuffer || currentBuffer->filename.empty())
        return "";

    return findAlternateFile(currentBuffer->filename);
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
void Editor::run()
{
    setStatusMessage("Welcome to uVim!");

    while(true)
    {
        draw();
        handleKeypress();
    }
}

