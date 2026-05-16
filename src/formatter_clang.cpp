#include "editor.h"
#include "formatter.h"
#include "process_pipe.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

bool ClangFormatter::operator()(Mode mode)
{
    if(mode == Mode::VISUAL)
    {
        return formatVisualSelection();
    }
    else if(mode == Mode::VISUAL_BLOCK)
    {
        return formatVisualBlockSelection();
    }
    return formatWithArgs("", "clang-format: formatted file");
}

size_t ClangFormatter::byteOffsetForPosition(int y, int x) const
{
    if(!editor.lines || editor.lines->empty())
        return 0;

    y = std::clamp(y, 0, (int)editor.lines->size() - 1);
    const std::string& ln = (*editor.lines)[y];
    x = std::clamp(x, 0, (int)ln.size());

    size_t off = 0;
    for(int i = 0; i < y; ++i)
        off += (*editor.lines)[i].size() + 1; // + '\n'
    off += (size_t)x;
    return off;
}

bool ClangFormatter::formatWithArgs(const std::string& extraArgs,
                                    const std::string& successMessage)
{
    if(!editor.lines || !editor.filename)
        return false;

    if(!editor.isFileType<FileType::Cpp>() ||
       editor.isFileType<FileType::Mla>())
    {
        editor.setStatusMessage("clang-format: not a C/C++ file (" +
                                *editor.filename + ")");
        return false;
    }

    const int savedY = editor.cursorY ? *editor.cursorY : 0;
    const int savedX = editor.cursorX ? *editor.cursorX : 0;

    std::string tempPath =
        "/tmp/uvim_format_" + std::to_string(getpid()) + ".tmp";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        editor.setStatusMessage("clang-format: failed to create temp file");
        return false;
    }

    for(size_t i = 0; i < editor.lines->size(); ++i)
        tempFile << (*editor.lines)[i] << '\n';
    tempFile.close();

    std::string absFilename = *editor.filename;
    if(!absFilename.empty() && absFilename[0] != '/')
    {
        std::error_code cwdEc;
        auto cwd = std::filesystem::current_path(cwdEc);
        if(!cwdEc)
            absFilename = cwd.string() + "/" + *editor.filename;
    }

    auto buildCmd = [&](const std::string& exe) -> std::string
    {
        std::string cmd = "cat \"" + tempPath + "\" | " + exe +
                          " -style=file -assume-filename=\"" + absFilename +
                          "\"";
        if(!extraArgs.empty())
            cmd += " " + extraArgs;
        cmd += " 2>/tmp/uvim_clang_err.log";
        return cmd;
    };

    std::string cmd = buildCmd("/opt/homebrew/bin/clang-format");
    ProcessPipe pipe(cmd, "r");
    if(!pipe)
    {
        cmd = buildCmd("clang-format");
        pipe.open(cmd, "r");
    }

    if(!pipe)
    {
        unlink(tempPath.c_str());
        editor.setStatusMessage("clang-format: failed to run");
        return false;
    }

    std::string formatted;
    char buffer[4096];
    while(fgets(buffer, sizeof(buffer), pipe.get()))
        formatted += buffer;

    int status = pipe.close();
    (void)status;
    unlink(tempPath.c_str());

    if(formatted.empty())
    {
        std::ifstream errFile("/tmp/uvim_clang_err.log");
        std::string errMsg;
        if(errFile.is_open())
        {
            std::getline(errFile, errMsg);
            errFile.close();
        }
        if(errMsg.empty())
            errMsg = "no output";
        editor.setStatusMessage("clang-format: " + errMsg.substr(0, 80));
        return false;
    }

    std::vector<std::string> newLines;
    std::istringstream iss(formatted);
    std::string line;
    while(std::getline(iss, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        newLines.push_back(line);
    }

    if(!newLines.empty() && newLines.back().empty())
        newLines.pop_back();
    if(newLines.empty())
        newLines.push_back("");

    if(newLines == *editor.lines)
    {
        editor.setStatusMessage("clang-format: no changes needed");
        return true;
    }

    editor.saveState();
    *editor.lines = std::move(newLines);
    if(editor.dirty)
        *editor.dirty = true;

    if(editor.cursorY && editor.cursorX && editor.lines &&
       !editor.lines->empty())
    {
        *editor.cursorY = std::clamp(savedY, 0, (int)editor.lines->size() - 1);
        *editor.cursorX =
            std::clamp(savedX, 0, (int)(*editor.lines)[*editor.cursorY].size());
    }

    editor.adjustViewport();
    editor.needsFullRedraw = true;
    editor.setStatusMessage(successMessage);
    return true;
}

bool ClangFormatter::formatVisualSelection()
{
    if(editor.currentMode != VISUAL && editor.currentMode != VISUAL_LINE)
        return false;

    if(!editor.lines || editor.lines->empty())
    {
        editor.setStatusMessage("clang-format: empty buffer");
        return false;
    }

    if(editor.currentMode == VISUAL_LINE)
    {
        const int startY = std::min(editor.currentBuffer->visualStartY,
                                    editor.currentBuffer->visualEndY);
        const int endY = std::max(editor.currentBuffer->visualStartY,
                                  editor.currentBuffer->visualEndY);

        const int startLine = startY + 1;
        const int endLine = endY + 1;

        const std::string args = "-lines=" + std::to_string(startLine) + ":" +
                                 std::to_string(endLine);

        formatWithArgs(args, "clang-format: formatted selection (lines)");
        return false;
    }

    int startY, startX, endY, endX;
    editor.getSelectionBounds(startY, startX, endY, endX);

    endY = std::clamp(endY, 0, (int)editor.lines->size() - 1);
    const int endLineLen = (int)(*editor.lines)[endY].size();
    const int endXExclusive = std::clamp(endX + 1, 0, endLineLen);

    const size_t startOff = byteOffsetForPosition(startY, startX);
    const size_t endOff = byteOffsetForPosition(endY, endXExclusive);

    if(endOff <= startOff)
    {
        editor.setStatusMessage("clang-format: empty selection");
        return false;
    }

    const size_t len = endOff - startOff;
    const std::string args = "-offset=" + std::to_string(startOff) +
                             " -length=" + std::to_string(len);

    return formatWithArgs(args, "clang-format: formatted selection");
}

bool ClangFormatter::formatVisualBlockSelection()
{
    if(editor.currentMode != VISUAL_BLOCK)
        return false;

    if(!editor.lines || editor.lines->empty())
    {
        editor.setStatusMessage("clang-format: empty buffer");
        return false;
    }

    int startY, startX, endY, endX;
    editor.getVisualBlockBounds(startY, startX, endY, endX);

    std::string args;
    for(int y = startY; y <= endY && y < (int)editor.lines->size(); ++y)
    {
        const int lineLen = (int)(*editor.lines)[y].size();
        const int segStart = std::clamp(startX, 0, lineLen);
        const int segEndExclusive = std::clamp(endX + 1, 0, lineLen);

        if(segEndExclusive <= segStart)
            continue;

        const size_t off = byteOffsetForPosition(y, segStart);
        const size_t len = (size_t)(segEndExclusive - segStart);

        args += " -offset=" + std::to_string(off) +
                " -length=" + std::to_string(len);
    }

    if(args.empty())
    {
        editor.setStatusMessage("clang-format: empty visual block");
        return false;
    }

    if(!args.empty() && args[0] == ' ')
        args.erase(0, 1);

    return formatWithArgs(args, "clang-format: formatted visual block");
}
