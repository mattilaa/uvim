#include "editor.h"
#include "formatter.h"
#include "process_pipe.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

bool PythonFormatter::operator()(Mode mode)
{
    if(!editor.lines || !editor.filename)
        return false;
    if(!editor.isFileType<FileType::Python>())
    {
        editor.setStatusMessage("python: not a Python file");
        return false;
    }

    const int savedY = editor.cursorY ? *editor.cursorY : 0;
    const int savedX = editor.cursorX ? *editor.cursorX : 0;

    std::string tempPath =
        "/tmp/uvim_pyfmt_" + std::to_string(getpid()) + ".py";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        editor.setStatusMessage("black: failed to create temp file");
        return false;
    }

    for(size_t i = 0; i < editor.lines->size(); ++i)
        tempFile << (*editor.lines)[i] << '\n';
    tempFile.close();

    std::string errPath = "/tmp/uvim_pyfmt_err.log";
    std::string cmd;
    if(editor.pythonFormatter == "ruff")
    {
        cmd = "ruff format \"" + tempPath + "\" 2>\"" + errPath + "\"";
    }
    else
    {
        cmd = "black --quiet \"" + tempPath + "\" 2>\"" + errPath + "\"";
    }

    int status = std::system(cmd.c_str());
    if(status != 0)
    {
        std::ifstream errFile(errPath);
        std::string errMsg;
        if(errFile.is_open())
        {
            std::getline(errFile, errMsg);
            errFile.close();
        }
        if(errMsg.empty())
            errMsg = editor.pythonFormatter + " failed";
        unlink(tempPath.c_str());
        editor.setStatusMessage(editor.pythonFormatter + ": " +
                                errMsg.substr(0, 80));
        return false;
    }

    std::ifstream in(tempPath);
    if(!in.is_open())
    {
        unlink(tempPath.c_str());
        editor.setStatusMessage("black: failed to read temp output");
        return false;
    }

    std::vector<std::string> newLines;
    std::string line;
    while(std::getline(in, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        newLines.push_back(line);
    }
    in.close();
    unlink(tempPath.c_str());

    if(!newLines.empty() && newLines.back().empty())
        newLines.pop_back();
    if(newLines.empty())
        newLines.push_back("");

    if(newLines == *editor.lines)
    {
        editor.setStatusMessage(editor.pythonFormatter + ": no changes");
        return true;
    }

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

    editor.saveState();
    editor.adjustViewport();
    editor.needsFullRedraw = true;
    editor.setStatusMessage(editor.pythonFormatter + ": formatted buffer");
    return true;
}

void PythonFormatter::lintBuffer()
{
    if(!editor.lines || !editor.filename)
        return;
    if(!editor.isFileType<FileType::Python>())
    {
        editor.setStatusMessage("ruff: not a Python file");
        return;
    }

    std::string tempPath = "/tmp/uvim_ruff_" + std::to_string(getpid()) + ".py";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        editor.setStatusMessage("ruff: failed to create temp file");
        return;
    }
    for(size_t i = 0; i < editor.lines->size(); ++i)
        tempFile << (*editor.lines)[i] << '\n';
    tempFile.close();

    ProcessPipe pipe({"ruff", "check", "--quiet", tempPath});
    if(!pipe)
    {
        unlink(tempPath.c_str());
        editor.setStatusMessage("ruff: failed to run");
        return;
    }

    std::string output = pipe.readAll();
    unlink(tempPath.c_str());

    if(output.empty())
    {
        editor.setStatusMessage("ruff: clean");
        return;
    }

    size_t nl = output.find('\n');
    if(nl != std::string::npos)
        output = output.substr(0, nl);
    editor.setStatusMessage("ruff: " + output);
}
