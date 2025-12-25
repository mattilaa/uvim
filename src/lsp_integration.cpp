#include "lsp_integration.h"
#include "buffer_manager.h"
#include "cursor_movement.h"
#include "terminal.h"
#include <algorithm>
#include <climits>
#include <fstream>
#include <sstream>
#include <unistd.h>

#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client_query.h"
#endif

LspIntegration::LspIntegration(EditorContext& ctx, BufferManager& bufferMgr,
                               CursorMovement& cursor)
    : ctx(ctx), bufferMgr(bufferMgr), cursor(cursor)
{
}

void LspIntegration::enable(const std::string& compileCommandsDir,
                            const std::string& clangdPath,
                            const std::string& queryDriverAllowList)
{
    ctx.clangdLspEnabled = true;
    ctx.clangdLspCompileCommandsDir = compileCommandsDir;
    ctx.clangdLspPath = clangdPath;
    ctx.clangdLspQueryDriverAllowList = queryDriverAllowList;

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!ctx.lspClient)
    {
        ctx.lspClient = std::make_unique<LspClient>();

        char cwd[PATH_MAX];
        std::string rootDir = ".";
        if(getcwd(cwd, sizeof(cwd)))
        {
            rootDir = cwd;
        }

        if(!ctx.lspClient->start(clangdPath, rootDir, compileCommandsDir,
                                 queryDriverAllowList))
        {
            ctx.lspClient.reset();
            ctx.clangdLspEnabled = false;
        }
    }
#endif
}

bool LspIntegration::isEnabled() const
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    return ctx.clangdLspEnabled && ctx.lspClient && ctx.lspClient->running();
#else
    return false;
#endif
}

bool LspIntegration::isCppFile() const
{
    if(ctx.filename->empty())
        return false;

    size_t dotPos = ctx.filename->find_last_of('.');
    if(dotPos == std::string::npos)
    {
        const std::string& path = *ctx.filename;
        return (path.find("/c++/") != std::string::npos ||
                path.find("/bits/") != std::string::npos ||
                path.find("/ext/") != std::string::npos ||
                path.find("/__") != std::string::npos);
    }

    std::string ext = ctx.filename->substr(dotPos);
    return (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" ||
            ext == ".hpp" || ext == ".hxx" || ext == ".c" || ext == ".C");
}

std::string LspIntegration::getSymbolUnderCursor() const
{
    if(*ctx.cursorY >= ctx.lines->size())
        return "";

    const std::string& line = (*ctx.lines)[*ctx.cursorY];
    if(*ctx.cursorX >= (int)line.length())
        return "";

    int start = *ctx.cursorX;
    while(start > 0 && ctx.isWordChar(line[start - 1]))
    {
        start--;
    }

    int end = *ctx.cursorX;
    while(end < (int)line.length() && ctx.isWordChar(line[end]))
    {
        end++;
    }

    if(start == end)
        return "";

    return line.substr(start, end - start);
}

void LspIntegration::didOpen(const std::string& filename)
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(isEnabled() && isCppFile())
    {
        std::string content;
        for(size_t i = 0; i < ctx.lines->size(); i++)
        {
            content += (*ctx.lines)[i];
            if(i + 1 < ctx.lines->size())
                content += "\n";
        }
        ctx.lspClient->didOpen(filename, "cpp", content);
    }
#endif
}

void LspIntegration::didChange()
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(isEnabled() && isCppFile())
    {
        std::string content;
        for(size_t i = 0; i < ctx.lines->size(); i++)
        {
            content += (*ctx.lines)[i];
            if(i + 1 < ctx.lines->size())
                content += "\n";
        }
        ctx.lspClient->didChange(*ctx.filename, content);
    }
#endif
}

void LspIntegration::didSave(const std::string& filename)
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(isEnabled() && isCppFile())
    {
        ctx.lspClient->didSave(filename);
    }
#endif
}

void LspIntegration::didClose(const std::string& filename)
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(isEnabled())
    {
        ctx.lspClient->didClose(filename);
    }
#endif
}

bool LspIntegration::searchDefinitionInBuffer(Buffer* buf,
                                              const std::string& symbol,
                                              int& outLine, int& outCol)
{
    // Simple text search for definition
    for(size_t i = 0; i < buf->lines.size(); i++)
    {
        const std::string& line = buf->lines[i];

        // Look for function/class/struct definition patterns
        size_t pos = line.find(symbol);
        while(pos != std::string::npos)
        {
            // Check if this is a definition (followed by '(' or preceded by
            // keywords)
            bool isDefinition = false;

            // Check for function definition: symbol followed by '('
            size_t afterSymbol = pos + symbol.length();
            if(afterSymbol < line.length())
            {
                size_t nextNonSpace = afterSymbol;
                while(nextNonSpace < line.length() && line[nextNonSpace] == ' ')
                {
                    nextNonSpace++;
                }
                if(nextNonSpace < line.length() && line[nextNonSpace] == '(')
                {
                    isDefinition = true;
                }
            }

            // Check for class/struct definition
            if(!isDefinition && pos >= 6)
            {
                std::string before = line.substr(0, pos);
                if(before.find("class ") != std::string::npos ||
                   before.find("struct ") != std::string::npos ||
                   before.find("enum ") != std::string::npos)
                {
                    isDefinition = true;
                }
            }

            if(isDefinition)
            {
                outLine = i;
                outCol = pos;
                return true;
            }

            pos = line.find(symbol, pos + 1);
        }
    }

    return false;
}

void LspIntegration::goToDefinition()
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!isEnabled())
    {
        // Fallback: search in current buffer
        std::string symbol = getSymbolUnderCursor();
        if(symbol.empty())
        {
            ctx.statusMessage = "No symbol under cursor";
            return;
        }

        int line, col;
        if(searchDefinitionInBuffer(ctx.currentBuffer, symbol, line, col))
        {
            cursor.pushJumpLocation();
            *ctx.cursorY = line;
            *ctx.cursorX = col;
            cursor.adjustViewport();
            ctx.needsFullRedraw = true;
            ctx.statusMessage = "Found: " + symbol;
        }
        else
        {
            ctx.statusMessage = "Definition not found: " + symbol;
        }
        return;
    }

    // Use LSP
    std::string uri = "file://" + *ctx.filename;
    int line = *ctx.cursorY;
    int col = *ctx.cursorX;

    auto result = ctx.lspClient->gotoDefinition(uri, line, col);

    if(result.empty())
    {
        ctx.statusMessage = "No definition found";
        return;
    }

    // Parse result (simplified - assumes single location)
    // Format: file:line:col
    std::string targetUri = result[0].uri;
    int targetLine = result[0].line;
    int targetCol = result[0].col;

    // Remove file:// prefix
    if(targetUri.substr(0, 7) == "file://")
    {
        targetUri = targetUri.substr(7);
    }

    cursor.pushJumpLocation();

    // Open file if different
    if(targetUri != *ctx.filename)
    {
        // Check if already open
        int bufIdx = bufferMgr.findBufferByFilename(targetUri);
        if(bufIdx >= 0)
        {
            bufferMgr.switchToBuffer(bufIdx);
        }
        else
        {
            // Need to open the file
            // This would require FileIO reference - for now just show message
            ctx.statusMessage = "Definition in: " + targetUri + ":" +
                                std::to_string(targetLine + 1);
            return;
        }
    }

    *ctx.cursorY = targetLine;
    *ctx.cursorX = targetCol;
    cursor.adjustViewport();
    ctx.needsFullRedraw = true;
    ctx.statusMessage = "Jumped to definition";
#else
    ctx.statusMessage = "LSP not enabled";
#endif
}

void LspIntegration::requestCompletion()
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!isEnabled() || !isCppFile())
        return;

    // Get current position and prefix
    const std::string& line = (*ctx.lines)[*ctx.cursorY];
    int prefixStart = *ctx.cursorX;
    while(prefixStart > 0 && ctx.isWordChar(line[prefixStart - 1]))
    {
        prefixStart--;
    }

    ctx.completionPrefix = line.substr(prefixStart, *ctx.cursorX - prefixStart);
    ctx.completionStartX = prefixStart;

    std::string uri = "file://" + *ctx.filename;
    auto items = ctx.lspClient->completion(uri, *ctx.cursorY, *ctx.cursorX);

    ctx.completionItems.clear();
    for(const auto& item : items)
    {
        CompletionItem ci;
        ci.label = item.label;
        ci.insertText = item.insertText.empty() ? item.label : item.insertText;
        ci.detail = item.detail;
        ci.kind = item.kind;
        ctx.completionItems.push_back(ci);
    }

    rebuildCompletionFilter();

    if(!ctx.filteredCompletions.empty())
    {
        ctx.completionActive = true;
        ctx.completionIndex = 0;
    }
#endif
}

void LspIntegration::cancelCompletion()
{
    ctx.completionActive = false;
    ctx.completionItems.clear();
    ctx.filteredCompletions.clear();
    ctx.completionIndex = 0;
}

void LspIntegration::completionNext()
{
    if(!ctx.completionActive || ctx.filteredCompletions.empty())
        return;

    ctx.completionIndex =
        (ctx.completionIndex + 1) % ctx.filteredCompletions.size();
}

void LspIntegration::completionPrev()
{
    if(!ctx.completionActive || ctx.filteredCompletions.empty())
        return;

    ctx.completionIndex =
        (ctx.completionIndex - 1 + ctx.filteredCompletions.size()) %
        ctx.filteredCompletions.size();
}

void LspIntegration::acceptCompletion()
{
    if(!ctx.completionActive || ctx.filteredCompletions.empty())
        return;

    if(ctx.completionIndex >= (int)ctx.filteredCompletions.size())
        return;

    const CompletionItem& item = ctx.filteredCompletions[ctx.completionIndex];
    std::string insertText = item.insertText;

    // Delete the prefix
    std::string& line = (*ctx.lines)[*ctx.cursorY];
    line.erase(ctx.completionStartX, *ctx.cursorX - ctx.completionStartX);
    *ctx.cursorX = ctx.completionStartX;

    // Insert completion text
    line.insert(*ctx.cursorX, insertText);
    *ctx.cursorX += insertText.length();

    *ctx.dirty = true;
    cancelCompletion();
}

void LspIntegration::rebuildCompletionFilter()
{
    ctx.filteredCompletions.clear();

    std::string lowerPrefix = ctx.completionPrefix;
    for(char& c : lowerPrefix)
    {
        c = std::tolower(static_cast<unsigned char>(c));
    }

    for(const auto& item : ctx.completionItems)
    {
        std::string lowerLabel = item.label;
        for(char& c : lowerLabel)
        {
            c = std::tolower(static_cast<unsigned char>(c));
        }

        // Simple prefix match
        if(lowerLabel.find(lowerPrefix) == 0 || lowerPrefix.empty())
        {
            ctx.filteredCompletions.push_back(item);
        }
    }

    // Sort by label
    std::sort(ctx.filteredCompletions.begin(), ctx.filteredCompletions.end(),
              [](const CompletionItem& a, const CompletionItem& b)
              { return a.label < b.label; });

    if(ctx.completionIndex >= (int)ctx.filteredCompletions.size())
    {
        ctx.completionIndex = 0;
    }
}

void LspIntegration::drawCompletionPopup(std::string& output) const
{
    if(!ctx.completionActive || ctx.filteredCompletions.empty())
        return;

    int lineNumWidth = std::to_string(ctx.lines->size()).length();
    lineNumWidth = std::max(lineNumWidth, 3);

    int popupX = *ctx.cursorX - *ctx.offsetX + lineNumWidth + 2;
    int popupY = *ctx.cursorY - *ctx.offsetY + 2;

    int maxItems = std::min(10, (int)ctx.filteredCompletions.size());
    int popupWidth = 40;

    if(popupY + maxItems > ctx.screenRows)
    {
        popupY = *ctx.cursorY - *ctx.offsetY - maxItems;
    }
    if(popupX + popupWidth > ctx.screenCols)
    {
        popupX = ctx.screenCols - popupWidth;
    }

    for(int i = 0; i < maxItems; i++)
    {
        output += Terminal::cursorPos(popupY + i, popupX);

        if(i == ctx.completionIndex)
        {
            output += Terminal::ESC_REVERSE;
        }
        else
        {
            output += Terminal::BG_BLACK;
            output += Terminal::FG_WHITE;
        }

        const CompletionItem& item = ctx.filteredCompletions[i];
        std::string label = item.label;
        if((int)label.length() > popupWidth - 2)
        {
            label = label.substr(0, popupWidth - 5) + "...";
        }

        output += " ";
        output += label;

        int padding = popupWidth - label.length() - 2;
        if(padding > 0)
        {
            output += std::string(padding, ' ');
        }
        output += " ";

        output += Terminal::ESC_RESET_ALL;
    }
}
