#include "editor.h"
#include "formatter.h"
#include "os_compat.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

static std::string read_first_line(const std::string& path)
{
    std::ifstream in(path);
    if(!in.is_open())
        return {};
    std::string line;
    std::getline(in, line);
    return line;
}

bool Formatter::jsonFormatBuffer()
{
    if(!editor->lines || !editor->filename)
        return false;
    if(!editor->isFileType<FileType::Json>())
    {
        editor->setStatusMessage("json.tool: not a JSON file");
        return false;
    }

    const int savedY = editor->cursorY ? *editor->cursorY : 0;
    const int savedX = editor->cursorX ? *editor->cursorX : 0;

    std::string tempPath =
        "/tmp/uvim_json_" + std::to_string(getpid()) + ".json";
    std::string outPath =
        "/tmp/uvim_json_" + std::to_string(getpid()) + "_out.json";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        editor->setStatusMessage("json.tool: failed to create temp file");
        return false;
    }
    for(size_t i = 0; i < editor->lines->size(); ++i)
        tempFile << (*editor->lines)[i] << '\n';
    tempFile.close();

    std::string cmd = "python -m json.tool \"" + tempPath + "\" > \"" +
                      outPath + "\" 2>/tmp/uvim_json_err.log";
    int status = std::system(cmd.c_str());
    if(status != 0)
    {
        std::string err = read_first_line("/tmp/uvim_json_err.log");
        if(err.empty())
            err = "json.tool failed";
        editor->setStatusMessage(err);
        unlink(tempPath.c_str());
        unlink(outPath.c_str());
        return false;
    }

    std::ifstream in(outPath);
    if(!in.is_open())
    {
        unlink(tempPath.c_str());
        unlink(outPath.c_str());
        editor->setStatusMessage("json.tool: failed to read temp output");
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
    unlink(outPath.c_str());

    if(!newLines.empty() && newLines.back().empty())
        newLines.pop_back();
    if(newLines.empty())
        newLines.push_back("");

    if(newLines == *editor->lines)
    {
        editor->setStatusMessage("json.tool: no changes");
        return true;
    }

    editor->saveState();
    *editor->lines = std::move(newLines);
    if(editor->dirty)
        *editor->dirty = true;

    if(editor->cursorY && editor->cursorX && editor->lines &&
       !editor->lines->empty())
    {
        *editor->cursorY =
            std::clamp(savedY, 0, (int)editor->lines->size() - 1);
        *editor->cursorX = std::clamp(
            savedX, 0, (int)(*editor->lines)[*editor->cursorY].size());
    }

    editor->adjustViewport();
    editor->needsFullRedraw = true;
    editor->setStatusMessage("json.tool: formatted buffer");
    return true;
}

bool Formatter::yamlFormatBuffer()
{
    if(!editor->lines || !editor->filename)
        return false;
    if(!editor->isFileType<FileType::Yaml>())
    {
        editor->setStatusMessage("yaml: not a YAML file");
        return false;
    }

    const int savedY = editor->cursorY ? *editor->cursorY : 0;
    const int savedX = editor->cursorX ? *editor->cursorX : 0;

    std::string tempPath =
        "/tmp/uvim_yaml_" + std::to_string(getpid()) + ".yml";
    std::string outPath =
        "/tmp/uvim_yaml_" + std::to_string(getpid()) + "_out.yml";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        editor->setStatusMessage("yaml: failed to create temp file");
        return false;
    }
    for(size_t i = 0; i < editor->lines->size(); ++i)
        tempFile << (*editor->lines)[i] << '\n';
    tempFile.close();

    std::string cmd = "python -m yaml \"" + tempPath + "\" > \"" + outPath +
                      "\" 2>/tmp/uvim_yaml_err.log";
    int status = std::system(cmd.c_str());
    if(status != 0)
    {
        std::string err = read_first_line("/tmp/uvim_yaml_err.log");
        if(err.empty())
            err = "yaml formatter failed";
        editor->setStatusMessage(err);
        unlink(tempPath.c_str());
        unlink(outPath.c_str());
        return false;
    }

    std::ifstream in(outPath);
    if(!in.is_open())
    {
        unlink(tempPath.c_str());
        unlink(outPath.c_str());
        editor->setStatusMessage("yaml: failed to read temp output");
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
    unlink(outPath.c_str());

    if(!newLines.empty() && newLines.back().empty())
        newLines.pop_back();
    if(newLines.empty())
        newLines.push_back("");

    if(newLines == *editor->lines)
    {
        editor->setStatusMessage("yaml: no changes");
        return true;
    }

    editor->saveState();
    *editor->lines = std::move(newLines);
    if(editor->dirty)
        *editor->dirty = true;

    if(editor->cursorY && editor->cursorX && editor->lines &&
       !editor->lines->empty())
    {
        *editor->cursorY =
            std::clamp(savedY, 0, (int)editor->lines->size() - 1);
        *editor->cursorX = std::clamp(
            savedX, 0, (int)(*editor->lines)[*editor->cursorY].size());
    }

    editor->adjustViewport();
    editor->needsFullRedraw = true;
    editor->setStatusMessage("yaml: formatted buffer");
    return true;
}
