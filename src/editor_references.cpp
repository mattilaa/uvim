// Editor methods for LSP References browser
// Add these declarations to editor.h and implementations here

#include "editor.h"
#include "lsp_client.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ============================================================================
// References Browser - Find all usages of symbol under cursor
// ============================================================================

void Editor::findReferences()
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!isClangdLspEnabled())
    {
        setStatusMessage("clangd: not enabled");
        return;
    }
    if(!isCppFile())
    {
        setStatusMessage("clangd references: only for C/C++");
        return;
    }

    // Sync current buffer to LSP
    std::string text;
    text.reserve(lines->size() * 80);
    for(size_t i = 0; i < lines->size(); ++i)
    {
        text += (*lines)[i];
        if(i + 1 < lines->size())
            text.push_back('\n');
    }
    lspClient->didChange(currentBuffer->filename, text);

    setStatusMessage("Finding references...");
    refreshScreen();

    // Query references
    auto refs = lspClient->references(currentBuffer->filename, *cursorY,
                                      *cursorX, true);

    if(refs.empty())
    {
        setStatusMessage("No references found");
        return;
    }

    // Store references
    referencesList.clear();
    referencesList.reserve(refs.size());

    std::string cwd = fs::current_path().string();

    for(const auto& ref : refs)
    {
        ReferenceEntry entry;
        entry.path = ref.path;
        entry.line = ref.line;
        entry.col = ref.character;

        // Make path relative if possible
        if(entry.path.rfind(cwd, 0) == 0 && entry.path.size() > cwd.size() + 1)
        {
            entry.displayPath = entry.path.substr(cwd.size() + 1);
        }
        else
        {
            entry.displayPath = entry.path;
        }

        // Try to read the line content for preview
        entry.lineContent = readLineFromFile(entry.path, entry.line);

        referencesList.push_back(std::move(entry));
    }

    // Sort by file then line
    std::stable_sort(referencesList.begin(), referencesList.end(),
                     [](const ReferenceEntry& a, const ReferenceEntry& b)
                     {
                         if(a.path != b.path)
                             return a.path < b.path;
                         return a.line < b.line;
                     });

    referencesCursor = 0;
    referencesOffset = 0;
    referencesPreview = true;

    setStatusMessage(std::to_string(referencesList.size()) +
                     " references found");
#else
    setStatusMessage("clangd references: not compiled in");
#endif
}

std::string Editor::readLineFromFile(const std::string& path, int lineNum)
{
    std::ifstream file(path);
    if(!file)
        return "";

    std::string line;
    int currentLine = 0;
    while(std::getline(file, line))
    {
        if(currentLine == lineNum)
        {
            // Trim leading whitespace for display
            size_t start = line.find_first_not_of(" \t");
            if(start != std::string::npos)
                return line.substr(start);
            return line;
        }
        currentLine++;
    }
    return "";
}

void Editor::clearReferences()
{
    referencesList.clear();
    referencesCursor = 0;
    referencesOffset = 0;
}

bool Editor::selectReference()
{
    if(referencesList.empty() || referencesCursor >= (int)referencesList.size())
        return false;

    const auto& ref = referencesList[referencesCursor];

    // Open the file at the reference location
    openFile(ref.path);
    moveToLine(ref.line);
    *cursorX = ref.col;
    if(*cursorX >= (int)(*lines)[*cursorY].length())
        *cursorX = std::max(0, (int)(*lines)[*cursorY].length() - 1);

    adjustViewport();
    centerScreen();

    clearReferences();
    return true;
}

void Editor::openReferencePreview()
{
    if(referencesList.empty() || referencesCursor >= (int)referencesList.size())
        return;

    const auto& ref = referencesList[referencesCursor];

    // Open file but don't clear references (stay in references mode)
    openFile(ref.path);
    moveToLine(ref.line);
    *cursorX = ref.col;
    if(*cursorX >= (int)(*lines)[*cursorY].length())
        *cursorX = std::max(0, (int)(*lines)[*cursorY].length() - 1);

    adjustViewport();
    centerScreen();
    needsFullRedraw = true;
}

void Editor::referencesUp()
{
    if(referencesList.empty())
        return;

    if(referencesCursor > 0)
    {
        referencesCursor--;
        if(referencesCursor < referencesOffset)
            referencesOffset = referencesCursor;
    }
    needsFullRedraw = true;
}

void Editor::referencesDown()
{
    if(referencesList.empty())
        return;

    if(referencesCursor < (int)referencesList.size() - 1)
    {
        referencesCursor++;
        int visibleRows = screenRows - 3; // Header + status + message
        if(referencesCursor >= referencesOffset + visibleRows)
            referencesOffset = referencesCursor - visibleRows + 1;
    }
    needsFullRedraw = true;
}

void Editor::referencesHalfPageUp()
{
    if(referencesList.empty())
        return;

    int halfPage = (screenRows - 3) / 2;
    referencesCursor = std::max(0, referencesCursor - halfPage);
    referencesOffset = std::max(0, referencesOffset - halfPage);
    needsFullRedraw = true;
}

void Editor::referencesHalfPageDown()
{
    if(referencesList.empty())
        return;

    int halfPage = (screenRows - 3) / 2;
    int maxCursor = (int)referencesList.size() - 1;
    referencesCursor = std::min(maxCursor, referencesCursor + halfPage);

    int visibleRows = screenRows - 3;
    if(referencesCursor >= referencesOffset + visibleRows)
        referencesOffset = referencesCursor - visibleRows + 1;

    needsFullRedraw = true;
}

void Editor::referencesFirst()
{
    if(referencesList.empty())
        return;

    referencesCursor = 0;
    referencesOffset = 0;
    needsFullRedraw = true;
}

void Editor::referencesLast()
{
    if(referencesList.empty())
        return;

    referencesCursor = (int)referencesList.size() - 1;
    int visibleRows = screenRows - 3;
    referencesOffset = std::max(0, referencesCursor - visibleRows + 1);
    needsFullRedraw = true;
}

void Editor::toggleReferencesPreview()
{
    referencesPreview = !referencesPreview;
    needsFullRedraw = true;
}

void Editor::drawReferences()
{
    std::string output;
    output.reserve(screenRows * screenCols * 2);

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::cursorPos(1, 1);

    // Header
    output += Terminal::BG_BLUE;
    output += Terminal::FG_WHITE;
    output += Terminal::ESC_BOLD;

    std::string header =
        " References (" + std::to_string(referencesList.size()) + ")";
    header += std::string(std::max(0, screenCols - (int)header.length()), ' ');
    output += header;
    output += Terminal::ESC_RESET_ALL;
    output += "\r\n";

    // References list
    int visibleRows = screenRows - 3; // Header + status + message bars
    std::string lastFile;
    int row = 0;
    int idx = referencesOffset;

    while(row < visibleRows && idx < (int)referencesList.size())
    {
        const auto& ref = referencesList[idx];

        // Show file header when file changes (never select header line)
        if(ref.displayPath != lastFile)
        {
            output += Terminal::ESC_CLEAR_LINE;
            output += Terminal::FG_CYAN;
            output += Terminal::ESC_BOLD;

            std::string fileHeader = ref.displayPath;
            if((int)fileHeader.length() > screenCols - 2)
                fileHeader = "..." + fileHeader.substr(fileHeader.length() -
                                                       screenCols + 5);

            output += " " + fileHeader;
            output += Terminal::ESC_RESET_ALL;
            output += "\r\n";

            lastFile = ref.displayPath;
            row++;

            if(row >= visibleRows)
                break;
        }

        output += Terminal::ESC_CLEAR_LINE;

        bool isSelected = (idx == referencesCursor);
        if(isSelected)
            output += Terminal::STYLE_SELECTION;

        // Line number and content
        std::string lineNum = std::to_string(ref.line + 1);
        while(lineNum.length() < 5)
            lineNum = " " + lineNum;

        output += Terminal::FG_YELLOW;
        if(isSelected)
            output += Terminal::STYLE_SELECTION;
        output += lineNum;
        output += Terminal::ESC_RESET_ALL;

        if(isSelected)
            output += Terminal::STYLE_SELECTION;

        output += " ";

        // Line content (truncated)
        std::string content = ref.lineContent;
        int maxContentLen = screenCols - 8;
        if((int)content.length() > maxContentLen)
            content = content.substr(0, maxContentLen - 3) + "...";

        output += content;

        if(isSelected)
            output += Terminal::ESC_RESET_ALL;

        // Fill rest of line
        int usedCols = 6 + 1 + content.length();
        if(usedCols < screenCols)
            output += std::string(screenCols - usedCols, ' ');

        output += "\r\n";
        row++;
        idx++;
    }

    // Fill remaining lines
    for(; row < visibleRows; row++)
    {
        output += Terminal::ESC_CLEAR_LINE;
        output += "~\r\n";
    }

    // Status bar
    output += Terminal::BG_WHITE;
    output += Terminal::FG_BLACK;

    std::string status = " [" + std::to_string(referencesCursor + 1) + "/" +
                         std::to_string(referencesList.size()) + "]";
    status += " <Enter> jump  <q/Esc> close  <j/k> navigate";

    if((int)status.length() < screenCols)
        status += std::string(screenCols - status.length(), ' ');

    output += status;
    output += Terminal::ESC_RESET_ALL;

    // Message bar
    output += "\r\n";
    output += Terminal::ESC_CLEAR_LINE;
    if(!statusMessage.empty())
        output += statusMessage;

    output += Terminal::ESC_SHOW_CURSOR;

    Terminal::write(output);
    Terminal::flush();
}

bool Editor::hasReferences() const
{
    return !referencesList.empty();
}
