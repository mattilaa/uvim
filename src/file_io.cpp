#include "file_io.h"
#include "buffer_manager.h"
#include <climits>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client_query.h"
#endif

FileIO::FileIO(EditorContext& ctx, BufferManager& bufferMgr)
    : ctx(ctx), bufferMgr(bufferMgr)
{
}

bool FileIO::isCppFile() const
{
    if(ctx.filename->empty())
        return false;

    size_t dotPos = ctx.filename->find_last_of('.');
    if(dotPos == std::string::npos)
    {
        const std::string& path = *ctx.filename;
        if(path.find("/c++/") != std::string::npos ||
           path.find("/bits/") != std::string::npos ||
           path.find("/ext/") != std::string::npos ||
           path.find("/__") != std::string::npos)
        {
            return true;
        }
        return false;
    }

    std::string ext = ctx.filename->substr(dotPos);
    return (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" ||
            ext == ".hpp" || ext == ".hxx" || ext == ".c" || ext == ".C" ||
            ext == ".mla");
}

void FileIO::notifyLspFileOpened(const std::string& fname)
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(ctx.clangdLspEnabled && ctx.lspClient && ctx.lspClient->running() &&
       isCppFile())
    {
        std::string content;
        for(size_t i = 0; i < ctx.lines->size(); i++)
        {
            content += (*ctx.lines)[i];
            if(i + 1 < ctx.lines->size())
                content += "\n";
        }
        ctx.lspClient->didOpen(fname, "cpp", content);
    }
#endif
}

void FileIO::notifyLspFileSaved(const std::string& fname)
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(ctx.clangdLspEnabled && ctx.lspClient && ctx.lspClient->running() &&
       isCppFile())
    {
        ctx.lspClient->didSave(fname);
    }
#endif
}

void FileIO::openFile(const std::string& fname)
{
    // Check if file is already open
    int existingBuffer = bufferMgr.findBufferByFilename(fname);
    if(existingBuffer >= 0)
    {
        bufferMgr.switchToBuffer(existingBuffer);
        return;
    }

    // Create new buffer if current one has content
    if(!ctx.filename->empty() || *ctx.dirty || ctx.lines->size() > 1 ||
       !(*ctx.lines)[0].empty())
    {
        bufferMgr.createNewBuffer();
    }

    std::ifstream file(fname);
    if(file.is_open())
    {
        ctx.lines->clear();
        std::string line;
        while(std::getline(file, line))
        {
            // Remove \r if present (Windows line endings)
            if(!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            ctx.lines->push_back(line);
        }
        file.close();

        if(ctx.lines->empty())
        {
            ctx.lines->push_back("");
        }
    }
    else
    {
        // New file
        ctx.lines->clear();
        ctx.lines->push_back("");
    }

    *ctx.filename = fname;
    *ctx.cursorX = 0;
    *ctx.cursorY = 0;
    *ctx.offsetX = 0;
    *ctx.offsetY = 0;
    *ctx.dirty = false;

    notifyLspFileOpened(fname);

    ctx.needsFullRedraw = true;
    ctx.statusMessage =
        "\"" + fname + "\" " + std::to_string(ctx.lines->size()) + "L";
}

void FileIO::saveFile()
{
    if(ctx.filename->empty())
    {
        ctx.statusMessage = "No file name";
        return;
    }

    // Clean up lines before saving
    int linesModified = 0;
    for(size_t lineIdx = 0; lineIdx < ctx.lines->size(); lineIdx++)
    {
        std::string& line = (*ctx.lines)[lineIdx];
        std::string original = line;

        // Convert tabs to spaces (4 spaces per tab, aligned to tab stops)
        std::string expanded;
        expanded.reserve(line.size());
        int col = 0;
        for(char c : line)
        {
            if(c == '\t')
            {
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
            line.clear();
        }

        if(line != original)
        {
            linesModified++;
            if((int)lineIdx == *ctx.cursorY &&
               *ctx.cursorX > (int)line.length())
            {
                *ctx.cursorX = line.length() > 0 ? line.length() - 1 : 0;
            }
        }
    }

    std::ofstream file(*ctx.filename);
    if(file.is_open())
    {
        for(const auto& line : *ctx.lines)
        {
            file << line << '\n';
        }
        file.close();
        *ctx.dirty = false;
        ctx.currentBuffer->savedUndoIndex = ctx.currentBuffer->undoIndex;

        std::string msg = "\"" + *ctx.filename + "\" " +
                          std::to_string(ctx.lines->size()) + "L written";
        if(linesModified > 0)
        {
            msg += " (" + std::to_string(linesModified) + " lines cleaned)";
            ctx.needsFullRedraw = true;
        }
        ctx.statusMessage = msg;

        notifyLspFileSaved(*ctx.filename);
    }
    else
    {
        ctx.statusMessage = "Can't save! I/O error";
    }
}

bool FileIO::fileExists(const std::string& path)
{
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

std::string FileIO::getSymbolUnderCursor()
{
    if(*ctx.cursorY >= ctx.lines->size())
        return "";

    const std::string& line = (*ctx.lines)[*ctx.cursorY];
    if(*ctx.cursorX >= (int)line.length())
        return "";

    // Find start of symbol
    int start = *ctx.cursorX;
    while(start > 0 && ctx.isWordChar(line[start - 1]))
    {
        start--;
    }

    // Find end of symbol
    int end = *ctx.cursorX;
    while(end < (int)line.length() && ctx.isWordChar(line[end]))
    {
        end++;
    }

    if(start == end)
        return "";

    return line.substr(start, end - start);
}

std::string FileIO::findAlternateFile(const std::string& currentFile)
{
    size_t lastDot = currentFile.find_last_of('.');
    size_t lastSlash = currentFile.find_last_of('/');
    if(lastSlash == std::string::npos)
        lastSlash = 0;
    else
        lastSlash++;

    if(lastDot == std::string::npos || lastDot < lastSlash)
    {
        return "";
    }

    std::string dir = (lastSlash > 0) ? currentFile.substr(0, lastSlash) : "";
    std::string base = currentFile.substr(lastSlash, lastDot - lastSlash);
    std::string ext = currentFile.substr(lastDot);

    std::vector<std::string> sourceExts = {".cpp", ".cc", ".cxx", ".c", ".C"};
    std::vector<std::string> headerExts = {".h", ".hpp", ".hxx", ".hh"};

    std::vector<std::string> targetExts;
    bool isSource = false;

    for(const auto& e : sourceExts)
    {
        if(ext == e)
        {
            targetExts = headerExts;
            isSource = true;
            break;
        }
    }
    if(targetExts.empty())
    {
        for(const auto& e : headerExts)
        {
            if(ext == e)
            {
                targetExts = sourceExts;
                break;
            }
        }
    }

    if(targetExts.empty())
    {
        return "";
    }

    // Try same directory
    for(const auto& targetExt : targetExts)
    {
        std::string altPath = dir + base + targetExt;
        if(fileExists(altPath))
        {
            return altPath;
        }
    }

    // Try common alternate locations
    std::vector<std::string> alternateDirs;
    if(isSource)
    {
        if(dir.find("/src/") != std::string::npos)
        {
            std::string incDir = dir;
            size_t srcPos = incDir.find("/src/");
            incDir.replace(srcPos, 5, "/include/");
            alternateDirs.push_back(incDir);
        }
        alternateDirs.push_back(dir + "../include/");
        alternateDirs.push_back(dir + "../");
    }
    else
    {
        if(dir.find("/include/") != std::string::npos)
        {
            std::string srcDir = dir;
            size_t incPos = srcDir.find("/include/");
            srcDir.replace(incPos, 9, "/src/");
            alternateDirs.push_back(srcDir);
        }
        alternateDirs.push_back(dir + "../src/");
        alternateDirs.push_back(dir + "../");
    }

    for(const auto& altDir : alternateDirs)
    {
        for(const auto& targetExt : targetExts)
        {
            std::string altPath = altDir + base + targetExt;
            if(fileExists(altPath))
            {
                return altPath;
            }
        }
    }

    return "";
}

void FileIO::jumpToAlternateFile()
{
    if(ctx.filename->empty())
    {
        ctx.statusMessage = "No file";
        return;
    }

    std::string altFile = findAlternateFile(*ctx.filename);
    if(altFile.empty())
    {
        ctx.statusMessage = "No alternate file found";
        return;
    }

    openFile(altFile);
}
