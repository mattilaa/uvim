#include "editor_references_controller.h"
#include "editor.h"
#include "enablelog.h"
#include "lsp_client.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
int referencesVisibleRows(const Editor& editor)
{
    // Editor::screenRows excludes the normal status and message bars. The
    // references browser owns the full terminal, including its own status and
    // message rows, so it has two extra rows available.
    return std::max(1, editor.screenRows - 1);
}

int referencesRowsThroughCursor(const Editor& editor, int offset, int cursor)
{
    int rows = 0;
    std::string lastFile;
    for(int idx = offset;
        idx <= cursor && idx < (int)editor.referencesList.size(); ++idx)
    {
        const auto& ref = editor.referencesList[idx];
        if(ref.displayPath != lastFile)
        {
            ++rows;
            lastFile = ref.displayPath;
        }
        ++rows;
    }
    return rows;
}

void clampReferencesOffsetToCursor(Editor& editor)
{
    if(editor.referencesList.empty())
    {
        editor.referencesCursor = 0;
        editor.referencesOffset = 0;
        return;
    }

    int maxCursor = (int)editor.referencesList.size() - 1;
    editor.referencesCursor =
        std::clamp(editor.referencesCursor, 0, maxCursor);
    editor.referencesOffset =
        std::clamp(editor.referencesOffset, 0, maxCursor);

    if(editor.referencesCursor < editor.referencesOffset)
        editor.referencesOffset = editor.referencesCursor;

    int visibleRows = referencesVisibleRows(editor);
    while(editor.referencesOffset < editor.referencesCursor &&
          referencesRowsThroughCursor(editor, editor.referencesOffset,
                                      editor.referencesCursor) > visibleRows)
    {
        ++editor.referencesOffset;
    }
}
} // namespace

// ============================================================================
// References Browser - Find all usages of symbol under cursor
// ============================================================================

EditorReferencesController::EditorReferencesController(Editor& editor)
    : editor(editor)
{
}

void EditorReferencesController::findReferences()
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    LspClient* client = nullptr;
    std::string label;
    std::string languageId;
    if(editor.template isFileType<FileType::Robot>())
    {
        if(!editor.isRobotLspEnabled())
        {
            LOG_WARNING(LOG, "Robot LSP is not enabled");
            return;
        }
        client = editor.robotLspClient.get();
        label = "robot";
        languageId = "robotframework";
    }
    else if(editor.template isFileType<FileType::Python>())
    {
        if(!editor.isPythonLspEnabled())
        {
            LOG_WARNING(LOG, "Python LSP is not enabled");
            return;
        }
        client = editor.pythonLspClient.get();
        label = "python";
        languageId = "python";
    }
    else if(editor.template isFileType<FileType::Mla>())
    {
        if(!editor.isMlangLspEnabled())
        {
            LOG_WARNING(LOG, "Mlang LSP is not enabled");
            return;
        }
        client = editor.mlangLspClient.get();
        label = "mlang";
        languageId = "mlang";
    }
    else if(editor.template isFileType<FileType::Cpp>())
    {
        if(!editor.isClangdLspEnabled())
        {
            LOG_WARNING(LOG, "Clangd LSP is not enabled");
            return;
        }
        client = editor.lspClient.get();
        label = "clangd";
        languageId = "cpp";
    }
    else
    {
        // editor.setStatusMessage("references: no LSP for filetype");
        return;
    }

    // Sync current buffer to LSP
    std::string text;
    text.reserve(editor.lines->size() * 80);
    for(size_t i = 0; i < editor.lines->size(); ++i)
    {
        text += (*editor.lines)[i];
        if(i + 1 < editor.lines->size())
            text.push_back('\n');
    }
    client->didChange(editor.currentBuffer->filename, text, languageId);

    editor.setStatusMessage("Finding references...");
    editor.refreshScreen();

    // Query references
    auto refs = client->references(editor.currentBuffer->filename,
                                   *editor.cursorY, *editor.cursorX, true);

    if(refs.empty())
    {
        editor.setStatusMessage("No references found");
        return;
    }

    // Store references
    editor.referencesList.clear();
    editor.referencesList.reserve(refs.size());

    std::string cwd = fs::current_path().string();

    for(const auto& ref : refs)
    {
        Editor::ReferenceEntry entry;
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

        editor.referencesList.push_back(std::move(entry));
    }

    // Sort by file then line
    std::stable_sort(editor.referencesList.begin(), editor.referencesList.end(),
                     [](const Editor::ReferenceEntry& a,
                        const Editor::ReferenceEntry& b)
                     {
                         if(a.path != b.path)
                             return a.path < b.path;
                         return a.line < b.line;
                     });

    editor.referencesCursor = 0;
    editor.referencesOffset = 0;
    editor.referencesPreview = true;

    editor.setStatusMessage(label + " references: " +
                            std::to_string(editor.referencesList.size()) +
                            " found");
#else
    editor.setStatusMessage("LSP references: not compiled in");
#endif
}

std::string EditorReferencesController::readLineFromFile(const std::string& path,
                                                         int lineNum)
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

void EditorReferencesController::clearReferences()
{
    editor.referencesList.clear();
    editor.referencesCursor = 0;
    editor.referencesOffset = 0;
}

bool EditorReferencesController::selectReference()
{
    if(editor.referencesList.empty() ||
       editor.referencesCursor >= (int)editor.referencesList.size())
        return false;

    const auto& ref = editor.referencesList[editor.referencesCursor];

    // Open the file at the reference location
    editor.openFile(std::string_view(ref.path));
    editor.moveToLine(ref.line);
    *editor.cursorX = ref.col;
    if(*editor.cursorX >= (int)(*editor.lines)[*editor.cursorY].length())
        *editor.cursorX =
            std::max(0, (int)(*editor.lines)[*editor.cursorY].length() - 1);

    editor.adjustViewport();
    editor.centerScreen();

    clearReferences();
    return true;
}

void EditorReferencesController::openReferencePreview()
{
    if(editor.referencesList.empty() ||
       editor.referencesCursor >= (int)editor.referencesList.size())
        return;

    const auto& ref = editor.referencesList[editor.referencesCursor];

    // Open file but don't clear references (stay in references mode)
    editor.openFile(std::string_view(ref.path));
    editor.moveToLine(ref.line);
    *editor.cursorX = ref.col;
    if(*editor.cursorX >= (int)(*editor.lines)[*editor.cursorY].length())
        *editor.cursorX =
            std::max(0, (int)(*editor.lines)[*editor.cursorY].length() - 1);

    editor.adjustViewport();
    editor.centerScreen();
    editor.needsFullRedraw = true;
}

void EditorReferencesController::referencesUp()
{
    if(editor.referencesList.empty())
        return;

    if(editor.referencesCursor > 0)
    {
        editor.referencesCursor--;
        clampReferencesOffsetToCursor(editor);
    }
    editor.needsFullRedraw = true;
}

void EditorReferencesController::referencesDown()
{
    if(editor.referencesList.empty())
        return;

    if(editor.referencesCursor < (int)editor.referencesList.size() - 1)
    {
        editor.referencesCursor++;
        clampReferencesOffsetToCursor(editor);
    }
    editor.needsFullRedraw = true;
}

void EditorReferencesController::referencesHalfPageUp()
{
    if(editor.referencesList.empty())
        return;

    int halfPage = std::max(1, referencesVisibleRows(editor) / 2);
    editor.referencesCursor = std::max(0, editor.referencesCursor - halfPage);
    editor.referencesOffset = std::max(0, editor.referencesOffset - halfPage);
    clampReferencesOffsetToCursor(editor);
    editor.needsFullRedraw = true;
}

void EditorReferencesController::referencesHalfPageDown()
{
    if(editor.referencesList.empty())
        return;

    int halfPage = std::max(1, referencesVisibleRows(editor) / 2);
    int maxCursor = (int)editor.referencesList.size() - 1;
    editor.referencesCursor =
        std::min(maxCursor, editor.referencesCursor + halfPage);
    clampReferencesOffsetToCursor(editor);

    editor.needsFullRedraw = true;
}

void EditorReferencesController::referencesFirst()
{
    if(editor.referencesList.empty())
        return;

    editor.referencesCursor = 0;
    editor.referencesOffset = 0;
    editor.needsFullRedraw = true;
}

void EditorReferencesController::referencesLast()
{
    if(editor.referencesList.empty())
        return;

    editor.referencesCursor = (int)editor.referencesList.size() - 1;
    editor.referencesOffset =
        std::max(0, editor.referencesCursor - referencesVisibleRows(editor) + 1);
    clampReferencesOffsetToCursor(editor);
    editor.needsFullRedraw = true;
}

void EditorReferencesController::toggleReferencesPreview()
{
    editor.referencesPreview = !editor.referencesPreview;
    editor.needsFullRedraw = true;
}

void EditorReferencesController::drawReferences()
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::cursorPos(1, 1);
    output += editor.theme.reset();

    // Header
    output += editor.theme.panel();

    std::string header =
        " References (" + std::to_string(editor.referencesList.size()) + ")";
    header +=
        std::string(std::max(0, editor.screenCols - (int)header.length()), ' ');
    output += header;
    output += editor.theme.reset();
    output += "\r\n";

    // References list
    int visibleRows = referencesVisibleRows(editor);
    std::string lastFile;
    int row = 0;
    int idx = editor.referencesOffset;

    while(row < visibleRows && idx < (int)editor.referencesList.size())
    {
        const auto& ref = editor.referencesList[idx];

        // Show file header when file changes (never select header line)
        if(ref.displayPath != lastFile)
        {
            output += Terminal::ESC_CLEAR_LINE;
            output += editor.theme.uiInfo();
            output += Terminal::ESC_BOLD;

            std::string fileHeader = ref.displayPath;
            if((int)fileHeader.length() > editor.screenCols - 2)
                fileHeader = "..." + fileHeader.substr(fileHeader.length() -
                                                       editor.screenCols + 5);

            output += " " + fileHeader;
            output += editor.theme.reset();
            output += "\r\n";

            lastFile = ref.displayPath;
            row++;

            if(row >= visibleRows)
                break;
        }

        output += Terminal::ESC_CLEAR_LINE;

        bool isSelected = (idx == editor.referencesCursor);
        if(isSelected)
            output += editor.theme.selection();

        // Line number and content
        std::string lineNum = std::to_string(ref.line + 1);
        while(lineNum.length() < 5)
            lineNum = " " + lineNum;

        output += editor.theme.uiWarning();
        if(isSelected)
            output += editor.theme.selection();
        output += lineNum;
        output += editor.theme.reset();

        if(isSelected)
            output += editor.theme.selection();

        output += " ";

        // Line content (truncated)
        std::string content = ref.lineContent;
        int maxContentLen = editor.screenCols - 8;
        if((int)content.length() > maxContentLen)
            content = content.substr(0, maxContentLen - 3) + "...";

        output += content;

        if(isSelected)
            output += editor.theme.reset();

        // Fill rest of line
        int usedCols = 6 + 1 + content.length();
        if(usedCols < editor.screenCols)
            output += std::string(editor.screenCols - usedCols, ' ');

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
    output += editor.theme.statusBar();

    std::string status = " [" + std::to_string(editor.referencesCursor + 1) + "/" +
                         std::to_string(editor.referencesList.size()) + "]";
    status += " <Enter> jump  <q/Esc> close  <j/k> navigate";

    if((int)status.length() < editor.screenCols)
        status += std::string(editor.screenCols - status.length(), ' ');

    output += status;
    output += editor.theme.reset();

    // Message bar
    output += "\r\n";
    output += Terminal::ESC_CLEAR_LINE;
    if(!editor.statusMessage.empty())
        output += editor.statusMessage;

    output += Terminal::ESC_SHOW_CURSOR;

    Terminal::write(output);
    Terminal::flush();
}

bool EditorReferencesController::hasReferences() const
{
    return !editor.referencesList.empty();
}
