#include "editor.h"
#include "terminal.h"
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <limits.h>
#include <memory>
#include <pwd.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_set>

Editor::Editor()
{
    Terminal::enableRawMode();
    Terminal::getWindowSize(screenRows, screenCols);
    screenRows -= 2; // Status bar and message bar

    // Create initial empty buffer
    createNewBuffer();
    saveState();
}

Editor::~Editor()
{
    Terminal::clearScreen();
    Terminal::moveCursor(1, 1);
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
    for(size_t i = 0; i < buffers.size(); i++)
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
    applyOperatorToRange(pendingOperator, startY, startX, endY, endX);

    // Clear state and return to NORMAL (or INSERT if operator was 'c')
    pendingOperator = 0;
    pendingAwaitingObject = false;
    pendingObjectType = 0;
    pendingCount = 0;

    setMode(NORMAL);
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

    if(op == 'c')
    {
        // After change, enter insert mode at start
        *cursorY = startY;
        *cursorX = startX;
        setMode(INSERT);
        setStatusMessage("-- INSERT --");
    }
    else
    {
        // Place cursor at start of affected range
        *cursorY = startY;
        *cursorX = startX;
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
    // Check if file is already open in a buffer
    int existingBufferIndex = findBufferByFilename(fname);
    if(existingBufferIndex >= 0)
    {
        switchToBuffer(existingBufferIndex);
        setStatusMessage("\"" + fname + "\" already open in buffer " +
                         std::to_string(existingBufferIndex + 1));
        return;
    }

    // Check if current buffer is empty and unnamed
    if(currentBuffer && currentBuffer->lines.size() == 1 &&
       currentBuffer->lines[0].empty() && currentBuffer->filename.empty() &&
       !currentBuffer->dirty)
    {
        // Reuse current empty buffer
    }
    else
    {
        // Create new buffer for this file
        createNewBuffer();
    }

    *filename = fname;
    lines->clear();

    std::ifstream file(*filename);
    if(file.is_open())
    {
        std::string line;
        while(std::getline(file, line))
        {
            if(!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            lines->push_back(line);
        }
        file.close();
    }

    if(lines->empty())
    {
        lines->push_back("");
    }

    *dirty = false;
    *cursorX = *cursorY = 0;
    *offsetX = *offsetY = 0;
    currentBuffer->undoStack.clear();
    currentBuffer->undoIndex = -1;
    saveState();

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

    std::ofstream file(*filename);
    if(file.is_open())
    {
        for(const auto& line : *lines)
        {
            file << line << '\n';
        }
        file.close();
        *dirty = false;
        setStatusMessage("\"" + *filename + "\" " +
                         std::to_string(lines->size()) + "L written");
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
        if (std::isspace((unsigned char)c))
        {
            while (x < (int)line.length() &&
                   std::isspace((unsigned char)line[x]))
            {
                x++;
            }
            break;
        }

        // MAIN LOGIC: eat an entire "word unit"
        // (either alphanumeric-run OR punctuation-run)
        // This matches vim behavior: stop at transitions between character types

        bool isAlphaWord = (std::isalnum((unsigned char)c) || c == '_');
        x++;

        while (x < (int)line.length())
        {
            char d = line[x];
            bool dAlpha = (std::isalnum((unsigned char)d) || d == '_');

            // Break on whitespace or character type change
            if (std::isspace((unsigned char)d))
                break;
            if (isAlphaWord != dAlpha)
                break;  // Stop at boundary between alphanumeric and punctuation

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
    if (y == 0 && x == 0)
    {
        return;
    }

    // If at start of line, go to end of previous line
    if (x == 0)
    {
        y--;
        x = (*lines)[y].length();
    }

    const std::string& line = (*lines)[y];

    // Move back one character to start
    if (x > 0)
    {
        x--;
    }

    // Skip whitespace backwards
    while (x > 0 && std::isspace((unsigned char)line[x]))
    {
        x--;
    }

    // Now we're on a non-whitespace character
    // Determine its type and find the start of this word
    if (x >= 0 && !std::isspace((unsigned char)line[x]))
    {
        bool isAlphaWord = (std::isalnum((unsigned char)line[x]) || line[x] == '_');

        // Move backwards while we're in the same character type
        while (x > 0)
        {
            char prevChar = line[x - 1];
            bool prevAlpha = (std::isalnum((unsigned char)prevChar) || prevChar == '_');

            // Stop if we hit whitespace or change character type
            if (std::isspace((unsigned char)prevChar))
                break;
            if (isAlphaWord != prevAlpha)
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
    if (x >= (int)line.length() - 1)
    {
        if (y + 1 < (int)lines->size())
        {
            y++;
            x = 0;
            const std::string& nextLine = (*lines)[y];

            // Skip whitespace at start of next line
            while (x < (int)nextLine.length() && std::isspace((unsigned char)nextLine[x]))
            {
                x++;
            }

            // Then move to end of first word
            if (x < (int)nextLine.length())
            {
                bool isAlphaWord = (std::isalnum((unsigned char)nextLine[x]) || nextLine[x] == '_');

                while (x < (int)nextLine.length() - 1)
                {
                    char nextChar = nextLine[x + 1];
                    bool nextAlpha = (std::isalnum((unsigned char)nextChar) || nextChar == '_');

                    if (std::isspace((unsigned char)nextChar))
                        break;
                    if (isAlphaWord != nextAlpha)
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
        while (x < (int)line.length() && std::isspace((unsigned char)line[x]))
        {
            x++;
        }

        // Now find end of current word
        if (x < (int)line.length())
        {
            bool isAlphaWord = (std::isalnum((unsigned char)line[x]) || line[x] == '_');

            // Move forward while in same character type
            while (x < (int)line.length() - 1)
            {
                char nextChar = line[x + 1];
                bool nextAlpha = (std::isalnum((unsigned char)nextChar) || nextChar == '_');

                if (std::isspace((unsigned char)nextChar))
                    break;
                if (isAlphaWord != nextAlpha)
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
    }
    else if(*cursorX >= (*lines)[*cursorY].length())
    {
        lines->insert(lines->begin() + *cursorY + 1, "");
    }
    else
    {
        std::string remainder = (*lines)[*cursorY].substr(*cursorX);
        (*lines)[*cursorY] = (*lines)[*cursorY].substr(0, *cursorX);
        lines->insert(lines->begin() + *cursorY + 1, remainder);
    }

    (*cursorY)++;
    *cursorX = 0;
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
    setStatusMessage("-- VISUAL --");
}

void Editor::startVisualLineMode()
{
    setMode(VISUAL_LINE);
    setStatusMessage("-- VISUAL LINE --");
}

void Editor::updateVisualSelection()
{
    currentBuffer->visualEndX = *cursorX;
    currentBuffer->visualEndY = *cursorY;
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

// Syntax highlighting functions
bool Editor::isCppFile() const
{
    if (filename->empty()) return false;

    size_t dotPos = filename->find_last_of('.');
    if (dotPos == std::string::npos) return false;

    std::string ext = filename->substr(dotPos);
    return (ext == ".cpp" || ext == ".cc" || ext == ".cxx" ||
            ext == ".h" || ext == ".hpp" || ext == ".hxx" ||
            ext == ".c" || ext == ".C");
}

std::string Editor::getColorCode(TokenType type) const
{
    switch(type)
    {
    case TOKEN_KEYWORD:
        return "\x1b[35m";      // Magenta for keywords
    case TOKEN_TYPE:
        return "\x1b[36m";      // Cyan for types
    case TOKEN_STRING:
        return "\x1b[32m";      // Green for strings
    case TOKEN_CHAR:
        return "\x1b[32m";      // Green for chars
    case TOKEN_COMMENT:
        return "\x1b[90m";      // Bright black (gray) for comments
    case TOKEN_PREPROCESSOR:
        return "\x1b[33m";      // Yellow for preprocessor
    case TOKEN_NUMBER:
        return "\x1b[31m";      // Red for numbers
    case TOKEN_OPERATOR:
        return "\x1b[93m";      // Bright yellow for operators
    case TOKEN_FUNCTION:
        return "\x1b[94m";      // Bright blue for functions
    default:
        return "\x1b[39m";      // Default color
    }
}

std::vector<Editor::Token> Editor::tokenizeLine(const std::string& line, bool& inBlockComment)
{
    std::vector<Token> tokens;

    // C++ keywords
    static const std::unordered_set<std::string> keywords = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
        "break", "case", "catch", "class", "compl", "concept", "const", "consteval",
        "constexpr", "constinit", "const_cast", "continue", "co_await", "co_return",
        "co_yield", "decltype", "default", "delete", "do", "dynamic_cast", "else",
        "enum", "explicit", "export", "extern", "false", "for", "friend", "goto",
        "if", "inline", "mutable", "namespace", "new", "noexcept", "not", "not_eq",
        "nullptr", "operator", "or", "or_eq", "private", "protected", "public",
        "reflexpr", "register", "reinterpret_cast", "requires", "return", "sizeof",
        "static", "static_assert", "static_cast", "struct", "switch", "synchronized",
        "template", "this", "thread_local", "throw", "true", "try", "typedef",
        "typeid", "typename", "union", "using", "virtual", "volatile", "while",
        "xor", "xor_eq", "override", "final"
    };

    // C++ types
    static const std::unordered_set<std::string> types = {
        // Fundamental types
        "bool", "char", "char8_t", "char16_t", "char32_t", "double", "float",
        "int", "long", "short", "signed", "unsigned", "void", "wchar_t",
        "size_t", "ptrdiff_t", "nullptr_t", "int8_t", "int16_t", "int32_t",
        "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
        "intptr_t", "uintptr_t", "intmax_t", "uintmax_t",

        // STL containers
        "std::vector", "std::list", "std::deque", "std::array", "std::forward_list",
        "std::map", "std::set", "std::multimap", "std::multiset",
        "std::unordered_map", "std::unordered_set", "std::unordered_multimap", "std::unordered_multiset",
        "std::stack", "std::queue", "std::priority_queue",
        "std::pair", "std::tuple",

        // STL strings
        "std::string", "std::wstring", "std::u8string", "std::u16string", "std::u32string",
        "std::string_view", "std::wstring_view", "std::u8string_view", "std::u16string_view", "std::u32string_view",

        // Smart pointers
        "std::unique_ptr", "std::shared_ptr", "std::weak_ptr", "std::auto_ptr",

        // Function objects
        "std::function", "std::bind", "std::reference_wrapper",

        // Utility types
        "std::optional", "std::variant", "std::any", "std::expected",
        "std::bitset", "std::complex",

        // Stream types
        "std::iostream", "std::istream", "std::ostream", "std::stringstream",
        "std::istringstream", "std::ostringstream", "std::fstream", "std::ifstream", "std::ofstream",
        "std::cout", "std::cin", "std::cerr", "std::clog",

        // Iterator types
        "std::iterator", "std::reverse_iterator", "std::move_iterator",
        "std::back_insert_iterator", "std::front_insert_iterator", "std::insert_iterator",

        // Thread types
        "std::thread", "std::mutex", "std::recursive_mutex", "std::timed_mutex",
        "std::lock_guard", "std::unique_lock", "std::shared_lock",
        "std::condition_variable", "std::condition_variable_any",
        "std::future", "std::promise", "std::packaged_task",
        "std::async", "std::atomic", "std::atomic_bool", "std::atomic_int",

        // Chrono types
        "std::chrono::duration", "std::chrono::time_point",
        "std::chrono::system_clock", "std::chrono::steady_clock", "std::chrono::high_resolution_clock",
        "std::chrono::seconds", "std::chrono::milliseconds", "std::chrono::microseconds", "std::chrono::nanoseconds",

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
        "std::nothrow_t", "std::align_val_t", "std::byte"
    };

    int i = 0;
    int len = line.length();

    while (i < len)
    {
        // Skip whitespace
        while (i < len && std::isspace(line[i]))
            i++;

        if (i >= len) break;

        // Check for block comment continuation
        if (inBlockComment)
        {
            int start = i;
            while (i < len && !(i < len - 1 && line[i] == '*' && line[i + 1] == '/'))
                i++;

            if (i < len - 1 && line[i] == '*' && line[i + 1] == '/')
            {
                i += 2;
                inBlockComment = false;
            }

            tokens.push_back({TOKEN_COMMENT, start, i - start});
            continue;
        }

        // Preprocessor directives
        if (i == 0 && line[i] == '#')
        {
            tokens.push_back({TOKEN_PREPROCESSOR, i, len - i});
            break;
        }

        // Line comments
        if (i < len - 1 && line[i] == '/' && line[i + 1] == '/')
        {
            tokens.push_back({TOKEN_COMMENT, i, len - i});
            break;
        }

        // Block comments
        if (i < len - 1 && line[i] == '/' && line[i + 1] == '*')
        {
            int start = i;
            i += 2;
            while (i < len - 1 && !(line[i] == '*' && line[i + 1] == '/'))
                i++;

            if (i < len - 1 && line[i] == '*' && line[i + 1] == '/')
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
        if (line[i] == '"')
        {
            int start = i;
            i++;
            while (i < len && line[i] != '"')
            {
                if (line[i] == '\\' && i + 1 < len)
                    i += 2;
                else
                    i++;
            }
            if (i < len) i++;
            tokens.push_back({TOKEN_STRING, start, i - start});
            continue;
        }

        // Character literals
        if (line[i] == '\'')
        {
            int start = i;
            i++;
            while (i < len && line[i] != '\'')
            {
                if (line[i] == '\\' && i + 1 < len)
                    i += 2;
                else
                    i++;
            }
            if (i < len) i++;
            tokens.push_back({TOKEN_CHAR, start, i - start});
            continue;
        }

        // Numbers
        if (std::isdigit(line[i]) || (line[i] == '.' && i + 1 < len && std::isdigit(line[i + 1])))
        {
            int start = i;
            bool hasHex = false;

            // Check for hex prefix
            if (line[i] == '0' && i + 1 < len && (line[i + 1] == 'x' || line[i + 1] == 'X'))
            {
                hasHex = true;
                i += 2;
            }

            while (i < len && (std::isdigit(line[i]) ||
                              (hasHex && std::isxdigit(line[i])) ||
                              line[i] == '.' || line[i] == 'e' || line[i] == 'E' ||
                              line[i] == 'f' || line[i] == 'F' ||
                              line[i] == 'u' || line[i] == 'U' ||
                              line[i] == 'l' || line[i] == 'L'))
            {
                i++;
            }

            tokens.push_back({TOKEN_NUMBER, start, i - start});
            continue;
        }

        // Identifiers and keywords
        if (std::isalpha(line[i]) || line[i] == '_')
        {
            int start = i;
            while (i < len && (std::isalnum(line[i]) || line[i] == '_' || line[i] == ':'))
                i++;

            std::string word = line.substr(start, i - start);

            // Check if it's followed by '(' to identify functions
            int j = i;
            while (j < len && std::isspace(line[j]))
                j++;

            TokenType type = TOKEN_NORMAL;
            if (j < len && line[j] == '(')
            {
                type = TOKEN_FUNCTION;
            }
            else if (keywords.find(word) != keywords.end())
            {
                type = TOKEN_KEYWORD;
            }
            else if (types.find(word) != types.end())
            {
                type = TOKEN_TYPE;
            }

            tokens.push_back({type, start, i - start});
            continue;
        }

        // Operators and punctuation
        if (std::strchr("+-*/%=<>!&|^~?:.,;()[]{}\\", line[i]))
        {
            int start = i;
            i++;

            // Multi-character operators
            if (i < len && std::strchr("=<>+-&|*/%:", line[i - 1]))
            {
                if ((line[i - 1] == '+' && line[i] == '+') ||
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
    if (fileRow == 0)
        inBlockComment = false;

    // Get tokens for the line
    bool blockCommentState = inBlockComment;
    std::vector<Token> tokens = tokenizeLine(line, blockCommentState);

    // Track what parts of the visible line need highlighting
    std::vector<TokenType> charColors(len, TOKEN_NORMAL);

    // Map tokens to visible characters
    for (const auto& token : tokens)
    {
        int tokenEnd = token.start + token.length;
        for (int pos = token.start; pos < tokenEnd; pos++)
        {
            int visiblePos = pos - start;
            if (visiblePos >= 0 && visiblePos < len)
            {
                charColors[visiblePos] = token.type;
            }
        }
    }

    // Render with colors
    TokenType currentColor = TOKEN_NORMAL;
    for (int x = 0; x < len; x++)
    {
        int col = x + start;

        // Check for selection or search highlighting first
        bool highlighted = false;
        if (isInSelection(fileRow, col))
        {
            output += "\x1b[7m";  // Reverse video for selection
            highlighted = true;
        }
        else if (isInSearchMatch(fileRow, col))
        {
            output += "\x1b[43m\x1b[30m";  // Yellow background for search
            highlighted = true;
        }

        // Apply syntax color if not highlighted
        if (!highlighted && charColors[x] != currentColor)
        {
            currentColor = charColors[x];
            output += getColorCode(currentColor);
        }

        output += line[col];

        if (highlighted)
        {
            output += "\x1b[m";  // Reset all attributes
            currentColor = TOKEN_NORMAL;  // Need to reapply color after reset
        }
    }

    // Reset color at end of line
    if (currentColor != TOKEN_NORMAL)
    {
        output += "\x1b[39m";
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

    output += "\x1b[H";
    output += "\x1b[K";

    // Header with search box
    output += "\x1b[1m";
    output += "  Find File: ";
    output += "\x1b[m";
    output += "\x1b[32m";
    output += fuzzyQuery;

    // Show cursor in search box
    output += "\x1b[5m_\x1b[25m"; // Blinking underscore
    output += "\x1b[39m";

    output += "\r\n\x1b[K";
    output += "\x1b[90m";
    output += "  [Enter: open] [Esc: cancel] [↑↓: navigate]";
    output += "\x1b[39m";
    output += "\r\n\x1b[K";
    output += "\x1b[90m";

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
    output += "\x1b[39m";

    int availableRows = screenRows - 3;

    // Draw matched files
    for(int i = 0; i < availableRows && i + fuzzyOffset < fuzzyMatches.size();
        i++)
    {
        output += "\r\n\x1b[K";

        int index = i + fuzzyOffset;
        const FuzzyMatch& match = fuzzyMatches[index];

        // Highlight current selection
        if(index == fuzzyCursor)
        {
            output += "\x1b[7m"; // Reverse video
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
                        output += "\x1b[32;1m"; // Bright green
                    }
                    output += displayPath[pos];
                    if(index != fuzzyCursor)
                    {
                        output += "\x1b[39;22m"; // Reset color
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
            output += "\x1b[90m";
            output += sizeStr;
            output += "\x1b[39m";
        }

        output += "\x1b[m"; // Reset all attributes
    }

    // Fill remaining rows
    for(int i = fuzzyMatches.size() - fuzzyOffset; i < availableRows; i++)
    {
        output += "\r\n\x1b[K";
        output += "\x1b[34m~\x1b[39m";
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

    output += "\x1b[H";
    output += "\x1b[K";

    // Header with search box
    output += "\x1b[1m";
    output += "  Buffers: ";
    output += "\x1b[m";
    output += "\x1b[32m";
    output += bufferQuery;
    output += "\x1b[5m_\x1b[25m";
    output += "\x1b[39m";

    output += "\r\n\x1b[K";
    output += "\x1b[90m";
    output += "  [Enter: switch] [Esc: cancel] [↑↓: navigate]";
    output += "\x1b[39m";

    output += "\r\n\x1b[K";
    output += "\x1b[90m";
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
    output += "\x1b[39m";

    int availableRows = screenRows - 3;

    for(int i = 0;
        i < availableRows && i + bufferOffset < (int)bufferMatches.size(); i++)
    {
        output += "\r\n\x1b[K";
        int idx = i + bufferOffset;
        const BufferMatch& m = bufferMatches[idx];

        if(idx == bufferCursor)
            output += "\x1b[7m";

        // Trim to screen width (leave leading two spaces)
        std::string line = "  " + m.display;
        if((int)line.length() > screenCols)
            line = line.substr(0, screenCols);

        output += line;
        output += "\x1b[m";
    }

    // Fill remaining rows
    for(int i = (int)bufferMatches.size() - bufferOffset; i < availableRows;
        i++)
    {
        output += "\r\n\x1b[K";
        output += "\x1b[34m~\x1b[39m";
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

    output += "\x1b[H";
    output += "\x1b[K";

    // Header with search box
    output += "\x1b[1m";
    output += "  Search in Files: ";
    output += "\x1b[m";
    output += "\x1b[32m";
    output += grepQuery;

    // Show cursor in search box
    output += "\x1b[5m_\x1b[25m"; // Blinking underscore
    output += "\x1b[39m";

    output += "\r\n\x1b[K";
    output += "\x1b[90m";
    output += "  [Enter: open] [Esc: cancel] [Tab: toggle case] [↑↓: navigate]";
    output += "\x1b[39m";

    output += "\r\n\x1b[K";
    output += "\x1b[90m";

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
    output += "\x1b[39m";

    int availableRows = screenRows - 3;

    // Draw matched lines
    for(int i = 0; i < availableRows && i + grepOffset < grepMatches.size();
        i++)
    {
        output += "\r\n\x1b[K";

        int index = i + grepOffset;
        const GrepMatch& match = grepMatches[index];

        // Highlight current selection
        if(index == grepCursor)
        {
            output += "\x1b[7m"; // Reverse video
        }

        // Format: filename:linenum: content
        output += "  ";

        // Filename in cyan
        output += "\x1b[36m";
        output += match.filename;
        output += "\x1b[39m";

        // Line number in yellow
        output += ":";
        output += "\x1b[33m";
        output += std::to_string(match.lineNumber);
        output += "\x1b[39m";
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
                    output += "\x1b[32;1m"; // Bright green
                    size_t endPos =
                        std::min((size_t)range.second, content.length());
                    output += content.substr(range.first, endPos - range.first);
                    output += "\x1b[39;22m"; // Reset color

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

        output += "\x1b[m"; // Reset all attributes
    }

    // Fill remaining rows
    for(int i = grepMatches.size() - grepOffset; i < availableRows; i++)
    {
        output += "\r\n\x1b[K";
        output += "\x1b[34m~\x1b[39m";
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
        *cursorX = state.cursorX;
        *cursorY = state.cursorY;
        *dirty = true;
        needsFullRedraw = true;
        setStatusMessage("Undo!");
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
        *dirty = true;
        needsFullRedraw = true;
        setStatusMessage("Redo!");
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
        if(y > 0)
        {
            Terminal::write("\r\n");
        }

        Terminal::write("\x1b[K");

        int fileRow = y + *offsetY;

        if(fileRow >= lines->size())
        {
            Terminal::write("\x1b[34m~\x1b[39m");
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

                for(int x = 0; x < len; x++)
                {
                    int col = x + *offsetX;
                    if(col < line.length())
                    {
                        bool highlighted = false;

                        if(isInSelection(fileRow, col))
                        {
                            Terminal::write("\x1b[7m");
                            highlighted = true;
                        }
                        else if(isInSearchMatch(fileRow, col))
                        {
                            Terminal::write("\x1b[43m\x1b[30m");
                            highlighted = true;
                        }

                        Terminal::write(line[col]);

                        if(highlighted)
                        {
                            Terminal::write("\x1b[m");
                        }
                    }
                }
            }
        }
    }
}

void Editor::drawStatusBar()
{
    Terminal::write("\r\n\x1b[K\x1b[7m");

    std::string status = " " + getModeString() + " | ";

    // Add buffer indicator
    if(buffers.size() > 1)
    {
        status += "[" + std::to_string(currentBufferIndex + 1) + "/" +
                  std::to_string(buffers.size()) + "] ";
    }

    status += (filename->empty() ? "[No Name]" : *filename);
    if(*dirty)
        status += " [+]";

    Terminal::write(status);

    char rightStatus[32];
    snprintf(rightStatus, sizeof(rightStatus), " %d:%d ", *cursorY + 1,
             *cursorX + 1);

    int padding = screenCols - status.length() - strlen(rightStatus);
    while(padding-- > 0)
        Terminal::write(' ');
    Terminal::write(rightStatus);

    Terminal::write("\x1b[m");
}

void Editor::drawMessageBar()
{
    Terminal::write("\r\n\x1b[K");

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

    output += "\x1b[H";

    // Draw header
    output += "\x1b[K";
    output += "\x1b[1m";
    output += "  " + currentDirectory;
    output += "\x1b[m";
    output += "\r\n\x1b[K";
    output += "\x1b[90m";
    output += "  [Enter: open] [q: quit] [.: toggle hidden] [-: parent]";
    output += "\x1b[39m";

    int availableRows = screenRows - 2;

    // Draw file entries
    for(int i = 0; i < availableRows && i + browserOffset < fileList.size();
        i++)
    {
        output += "\r\n\x1b[K";

        int index = i + browserOffset;
        const FileEntry& entry = fileList[index];

        if(index == browserCursor)
        {
            output += "\x1b[7m";
        }

        if(entry.isDirectory)
        {
            output += "\x1b[34m";
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
                output += "\x1b[32m";
                output += "  ◆ ";
            }
            else if(ext == ".txt" || ext == ".md")
            {
                output += "\x1b[37m";
                output += "  ○ ";
            }
            else if(ext == ".sh" || ext == ".py" || ext == ".js")
            {
                output += "\x1b[33m";
                output += "  ★ ";
            }
            else
            {
                output += "\x1b[37m";
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

            output += "\x1b[90m";
            output += info;
        }

        output += "\x1b[m";
    }

    // Fill remaining rows
    for(int i = fileList.size() - browserOffset; i < availableRows; i++)
    {
        output += "\r\n\x1b[K";
        output += "\x1b[34m~\x1b[39m";
    }

    // Status bar
    output += "\r\n\x1b[K\x1b[7m";

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
    output += "\x1b[m";

    // Message bar
    output += "\r\n\x1b[K";
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

    output += "\x1b[?25l";

    if(scrollDelta > 0)
    {
        char scrollCmd[64];
        snprintf(scrollCmd, sizeof(scrollCmd), "\x1b[1;%dr", screenRows);
        output += scrollCmd;

        output += "\x1b[H";
        for(int i = 0; i < scrollDelta; i++)
        {
            output += "\x1b[M";
        }

        for(int i = 0; i < scrollDelta; i++)
        {
            int row = screenRows - scrollDelta + i;
            int fileRow = row + *offsetY;

            char moveBuf[32];
            snprintf(moveBuf, sizeof(moveBuf), "\x1b[%d;1H", row + 1);
            output += moveBuf;
            output += "\x1b[K";

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

                                if(isInSelection(fileRow, col))
                                {
                                    output += "\x1b[7m";
                                    highlighted = true;
                                }
                                else if(isInSearchMatch(fileRow, col))
                                {
                                    output += "\x1b[43m\x1b[30m";
                                    highlighted = true;
                                }

                                output += line[col];

                                if(highlighted)
                                {
                                    output += "\x1b[m";
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                output += "\x1b[34m~\x1b[39m";
            }
        }

        output += "\x1b[r";
    }
    else if(scrollDelta < 0)
    {
        int absDelta = -scrollDelta;

        char scrollCmd[64];
        snprintf(scrollCmd, sizeof(scrollCmd), "\x1b[1;%dr", screenRows);
        output += scrollCmd;

        output += "\x1b[H";
        for(int i = 0; i < absDelta; i++)
        {
            output += "\x1b[L";
        }

        for(int i = 0; i < absDelta; i++)
        {
            int fileRow = i + *offsetY;

            char moveBuf[32];
            snprintf(moveBuf, sizeof(moveBuf), "\x1b[%d;1H", i + 1);
            output += moveBuf;
            output += "\x1b[K";

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

                                if(isInSelection(fileRow, col))
                                {
                                    output += "\x1b[7m";
                                    highlighted = true;
                                }
                                else if(isInSearchMatch(fileRow, col))
                                {
                                    output += "\x1b[43m\x1b[30m";
                                    highlighted = true;
                                }

                                output += line[col];

                                if(highlighted)
                                {
                                    output += "\x1b[m";
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                output += "\x1b[34m~\x1b[39m";
            }
        }

        output += "\x1b[r";
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
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursorRow, cursorCol);
    output += buf;
    output += "\x1b[?25h";

    lastCursorScreenY = cursorRow;
    lastCursorScreenX = cursorCol;

    Terminal::write(output);
    Terminal::flush();
}

void Editor::drawStatusBarQuick()
{
    std::string output;
    char moveBuf[32];
    snprintf(moveBuf, sizeof(moveBuf), "\x1b[%d;1H", screenRows + 1);
    output += moveBuf;

    output += "\x1b[K\x1b[7m";

    std::string statusLeft = " " + getModeString() + " | ";

    if(buffers.size() > 1)
    {
        statusLeft += "[" + std::to_string(currentBufferIndex + 1) + "/" +
                      std::to_string(buffers.size()) + "] ";
    }

    statusLeft += (filename->empty() ? "[No Name]" : *filename);
    if(*dirty)
        statusLeft += " [+]";

    output += statusLeft;

    char rightStatus[32];
    snprintf(rightStatus, sizeof(rightStatus), " %d:%d ", *cursorY + 1,
             *cursorX + 1);

    int padding = screenCols - statusLeft.length() - strlen(rightStatus);
    if(padding > 0)
    {
        output.append(padding, ' ');
    }
    output += rightStatus;
    output += "\x1b[m";

    Terminal::write(output);
}

void Editor::drawMessageBarQuick()
{
    std::string output;
    char moveBuf[32];
    snprintf(moveBuf, sizeof(moveBuf), "\x1b[%d;1H", screenRows + 2);
    output += moveBuf;

    output += "\x1b[K";

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

    Terminal::write(output);
}

void Editor::drawFullScreen()
{
    adjustViewport();

    std::string output;
    output.reserve((screenRows + 3) * screenCols * 3);

    output += "\x1b[H";

    for(int y = 0; y < screenRows; y++)
    {
        if(y > 0)
            output += "\r\n";

        output += "\x1b[K";

        int fileRow = y + *offsetY;

        if(fileRow >= lines->size())
        {
            output += "\x1b[34m~\x1b[39m";
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
                   !searchMatches.empty())
                {
                    for(int x = 0; x < len; x++)
                    {
                        int col = x + *offsetX;
                        if(isInSelection(fileRow, col) ||
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
                    // Use syntax highlighting for C++ files (handles selections too)
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

                        if(isInSelection(fileRow, col))
                        {
                            output += "\x1b[7m";
                            highlighted = true;
                        }
                        else if(isInSearchMatch(fileRow, col))
                        {
                            output += "\x1b[43m\x1b[30m";
                            highlighted = true;
                        }

                        output += line[col];

                        if(highlighted)
                        {
                            output += "\x1b[m";
                        }
                    }
                }
            }
        }
    }

    // Status bar
    output += "\r\n\x1b[K\x1b[7m";

    std::string statusLeft = " " + getModeString() + " | ";

    if(buffers.size() > 1)
    {
        statusLeft += "[" + std::to_string(currentBufferIndex + 1) + "/" +
                      std::to_string(buffers.size()) + "] ";
    }

    statusLeft += (filename->empty() ? "[No Name]" : *filename);
    if(*dirty)
        statusLeft += " [+]";

    output += statusLeft;

    char rightStatus[32];
    snprintf(rightStatus, sizeof(rightStatus), " %d:%d ", *cursorY + 1,
             *cursorX + 1);

    int padding = screenCols - statusLeft.length() - strlen(rightStatus);
    if(padding > 0)
        output.append(padding, ' ');
    output += rightStatus;
    output += "\x1b[m";

    // Message bar
    output += "\r\n\x1b[K";

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
    if(currentMode == VISUAL || currentMode == VISUAL_LINE)
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
       (currentMode == VISUAL || currentMode == VISUAL_LINE) || isEditingMode)
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

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursorRow, cursorCol);
    Terminal::write(buf);
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

// Mode handlers
void Editor::handleNormalMode(int c)
{
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

    switch(c)
    {
    case 'i':
        setMode(INSERT);
        setStatusMessage("-- INSERT --");
        break;
    case 'I':
        moveToLineStart();
        setMode(INSERT);
        setStatusMessage("-- INSERT --");
        break;
    case 'a':
        if(*cursorX < (*lines)[*cursorY].length())
            (*cursorX)++;
        setMode(INSERT);
        setStatusMessage("-- INSERT --");
        break;
    case 'A':
        moveToLineEnd();
        if(*cursorX < (*lines)[*cursorY].length())
            (*cursorX)++;
        setMode(INSERT);
        setStatusMessage("-- INSERT --");
        break;
    case 'o':
        moveToLineEnd();
        if(*cursorX < (*lines)[*cursorY].length())
            (*cursorX)++;
        insertNewline();
        setMode(INSERT);
        saveState();
        setStatusMessage("-- INSERT --");
        break;
    case 'O':
        moveToLineStart();
        lines->insert(lines->begin() + *cursorY, "");
        *dirty = true;
        setMode(INSERT);
        saveState();
        setStatusMessage("-- INSERT --");
        break;
    case 'v':
        startVisualMode();
        break;
    case 'V':
        startVisualLineMode();
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
    case Terminal::CTRL_F: // Ctrl+F for grep search (find in files)
        setMode(GREP_SEARCH);
        break;

    case 'h':
        if(commandBuffer == "\\")
        {
            // Leader + h: jump to alternate file (header/source)
            jumpToAlternateFile();
            commandBuffer.clear();
        }
        else
        {
            // Normal h: move left
            moveLeft(count);
        }
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
        if(commandBuffer == "g")
        {
            moveToFirstLine();
            commandBuffer.clear();
        }
        else
        {
            commandBuffer = "g";
        }
        break;
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

    case '\\':  // Leader key (backslash)
        if(commandBuffer == "\\")
        {
            commandBuffer.clear();  // Double backslash cancels
        }
        else
        {
            commandBuffer = "\\";
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
    case 'd':
        if(commandBuffer == "d")
        {
            while(count-- > 0)
                deleteLine();
            commandBuffer.clear();
            saveState();
        }
        else
        {
            // enter operator-pending mode
            enterOperatorPending('d');
        }
        break;
    case 'D':
        deleteToLineEnd();
        saveState();
        break;
    case 'y':
        if(commandBuffer == "y")
        {
            yankLine();
            commandBuffer.clear();
        }
        else
        {
            enterOperatorPending('y');
        }
        break;

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

    // Clear command buffer if we had a pending command but didn't recognize what followed
    if((commandBuffer == "\\" || commandBuffer == "g") && c != '\\' && c != 'g')
    {
        // If the command wasn't recognized, clear the buffer
        bool recognized = false;
        if(commandBuffer == "\\" && c == 'h')
            recognized = true;
        if(commandBuffer == "g" && c == 'g')
            recognized = true;

        if(!recognized)
        {
            commandBuffer.clear();
            setStatusMessage("");
        }
    }

    repeatCount = 0;
}

void Editor::handleInsertMode(int c)
{
    switch(c)
    {
    case Terminal::ESC:
        if(*cursorX > 0)
            (*cursorX)--;
        setMode(NORMAL);
        saveState();
        statusMessage.clear();
        break;
    case Terminal::ENTER:
        insertNewline();
        break;
    case Terminal::BACKSPACE:
    case Terminal::DEL:
        deleteChar();
        break;
    case Terminal::TAB:
        for(int i = 0; i < 4; i++)
        {
            insertChar(' ');
        }
        break;
    default:
        if(c >= 32 && c < 127)
        {
            insertChar(c);
        }
        break;
    }
}

void Editor::handleVisualMode(int c)
{
    switch(c)
    {
    case Terminal::ESC:
        setMode(NORMAL);
        statusMessage.clear();
        needsFullRedraw = true;
        break;
    case 'h':
    case Terminal::ARROW_LEFT:
        moveLeft();
        updateVisualSelection();
        needsFullRedraw = true;
        break;
    case 'l':
    case Terminal::ARROW_RIGHT:
        moveRight();
        updateVisualSelection();
        needsFullRedraw = true;
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        moveDown();
        updateVisualSelection();
        needsFullRedraw = true;
        break;
    case 'k':
    case Terminal::ARROW_UP:
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
    case 'w': // Visual w - extend selection forward by word
        moveWordForward();
        updateVisualSelection();
        adjustViewport();
        break;

    case 'b': // Visual b - extend selection backward by word
        moveWordBackward();
        updateVisualSelection();
        adjustViewport();
        break;

    case 'e': // Visual e - extend selection to end of word
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
    }
}

void Editor::handleCommandMode(int c)
{
    switch(c)
    {
    case Terminal::ENTER:
        executeCommand(commandBuffer.substr(1));
        setMode(NORMAL);
        break;
    case Terminal::ESC:
        setMode(NORMAL);
        statusMessage.clear();
        break;
    case Terminal::BACKSPACE:
    case Terminal::DEL:
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
