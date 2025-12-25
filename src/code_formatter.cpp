#include "code_formatter.h"
#include "undo_manager.h"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

CodeFormatter::CodeFormatter(EditorContext& ctx, UndoManager& undoMgr)
    : ctx(ctx), undoMgr(undoMgr)
{
}

bool CodeFormatter::isCppFile() const
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

std::string CodeFormatter::findClangFormat() const
{
    // Try common paths
    std::vector<std::string> paths = {"/opt/homebrew/bin/clang-format",
                                      "/usr/local/bin/clang-format",
                                      "/usr/bin/clang-format", "clang-format"};

    for(const auto& path : paths)
    {
        std::string testCmd = path + " --version >/dev/null 2>&1";
        if(system(testCmd.c_str()) == 0)
        {
            return path;
        }
    }

    return "";
}

std::string CodeFormatter::getAbsoluteFilename() const
{
    std::string absFilename = *ctx.filename;

    if(!absFilename.empty() && absFilename[0] != '/')
    {
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
        {
            absFilename = std::string(cwd) + "/" + *ctx.filename;
        }
    }

    return absFilename;
}

void CodeFormatter::formatFile()
{
    if(!isCppFile())
    {
        ctx.statusMessage = "clang-format: not a C/C++ file";
        return;
    }

    std::string clangFormatBin = findClangFormat();
    if(clangFormatBin.empty())
    {
        ctx.statusMessage = "clang-format: not found";
        return;
    }

    // Save current cursor position
    int savedY = *ctx.cursorY;
    int savedX = *ctx.cursorX;

    // Write buffer to temp file
    std::string tempPath =
        "/tmp/uvim_format_" + std::to_string(getpid()) + ".tmp";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        ctx.statusMessage = "clang-format: failed to create temp file";
        return;
    }

    for(size_t i = 0; i < ctx.lines->size(); ++i)
    {
        tempFile << (*ctx.lines)[i] << '\n';
    }
    tempFile.close();

    // Get absolute filename for style lookup
    std::string absFilename = getAbsoluteFilename();

    // Run clang-format
    std::string cmd = "cat \"" + tempPath + "\" | " + clangFormatBin +
                      " -style=file"
                      " -assume-filename=\"" +
                      absFilename +
                      "\""
                      " 2>/tmp/uvim_clang_err.txt";

#ifdef UVIM_DEBUG_LOGGING
    {
        std::ofstream dbg("/tmp/uvim_debug.txt", std::ios::app);
        dbg << "clang-format cmd: " << cmd << std::endl;
    }
#endif

    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
    {
        unlink(tempPath.c_str());
        ctx.statusMessage = "clang-format: failed to run";
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

    // Check for errors
    if(formatted.empty())
    {
        std::ifstream errFile("/tmp/uvim_clang_err.txt");
        std::string errMsg;
        if(errFile.is_open())
        {
            std::getline(errFile, errMsg);
            errFile.close();
        }
        if(errMsg.empty())
            errMsg =
                "no output (exit=" + std::to_string(WEXITSTATUS(status)) + ")";
        ctx.statusMessage = "clang-format: " + errMsg.substr(0, 50);
        return;
    }

    // Parse formatted output into lines
    std::vector<std::string> newLines;
    std::istringstream iss(formatted);
    std::string line;
    while(std::getline(iss, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        newLines.push_back(line);
    }

    // Remove trailing empty line if present
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
    if(newLines == *ctx.lines)
    {
        ctx.statusMessage = "clang-format: no changes needed";
        return;
    }

    // Save state for undo
    undoMgr.saveState();

    // Replace buffer content
    *ctx.lines = newLines;
    *ctx.dirty = true;

    // Restore cursor position (clamped to valid range)
    if(ctx.lines->empty())
    {
        *ctx.cursorY = 0;
        *ctx.cursorX = 0;
    }
    else
    {
        *ctx.cursorY = savedY;
        if(*ctx.cursorY >= (int)ctx.lines->size())
            *ctx.cursorY = (int)ctx.lines->size() - 1;
        if(*ctx.cursorY < 0)
            *ctx.cursorY = 0;

        *ctx.cursorX = savedX;
        int lineLen = (int)(*ctx.lines)[*ctx.cursorY].length();
        if(*ctx.cursorX > lineLen)
            *ctx.cursorX = lineLen > 0 ? lineLen - 1 : 0;
        if(*ctx.cursorX < 0)
            *ctx.cursorX = 0;
    }

    // Adjust viewport
    if(*ctx.cursorY < *ctx.offsetY)
    {
        *ctx.offsetY = *ctx.cursorY;
    }
    if(*ctx.cursorY >= *ctx.offsetY + ctx.screenRows)
    {
        *ctx.offsetY = *ctx.cursorY - ctx.screenRows + 1;
    }

    ctx.needsFullRedraw = true;
    ctx.statusMessage = "clang-format: formatted " +
                        std::to_string(ctx.lines->size()) + " lines";
}

void CodeFormatter::formatSelection(int startLine, int endLine)
{
    // For now, just format the entire file
    // TODO: Implement line-range formatting with clang-format --lines
    formatFile();
}
