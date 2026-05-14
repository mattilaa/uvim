#include "editor_file_controller.h"
#include "editor.h"
#include "editor_utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using editor::helper::hash_lines;

namespace
{
bool isIdent(char c)
{
    return std::isalnum((unsigned char)c) || c == '_';
}
} // namespace

EditorFileController::EditorFileController(Editor& editor) : editor(editor) {}

void EditorFileController::saveFile()
{
    editor.saveFileImpl();
}

void EditorFileController::checkFileChanges()
{
    editor.checkFileChangesImpl();
}

void EditorFileController::reloadCurrentFile()
{
    editor.reloadCurrentFileImpl();
}

bool EditorFileController::fileExists(const std::string& path)
{
    return editor.fileExistsImpl(path);
}

std::string EditorFileController::getSymbolUnderCursor()
{
    return editor.getSymbolUnderCursorImpl();
}

std::string
EditorFileController::findAlternateFile(const std::string& currentFile)
{
    return editor.findAlternateFileImpl(currentFile);
}

void EditorFileController::jumpToAlternateFile()
{
    editor.jumpToAlternateFileImpl();
}

void EditorFileController::goToFile()
{
    editor.goToFileImpl();
}

void EditorFileController::showFileInfo()
{
    editor.showFileInfoImpl();
}

void EditorFileController::deleteFilePrompt()
{
    editor.deleteFilePromptImpl();
}

void EditorFileController::renameFilePrompt()
{
    editor.renameFilePromptImpl();
}

void EditorFileController::createNewFilePrompt()
{
    editor.createNewFilePromptImpl();
}

void EditorFileController::createNewDirectoryPrompt()
{
    editor.createNewDirectoryPromptImpl();
}

void Editor::saveFileImpl()
{
    if(filename->empty())
    {
        setStatusMessage("No file name");
        return;
    }

    if(formatOnSave)
        formatBufferForSave();

    int linesModified = 0;
    for(size_t lineIdx = 0; lineIdx < lines->size(); lineIdx++)
    {
        std::string& line = (*lines)[lineIdx];
        std::string original = line;

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
            if((int)lineIdx == *cursorY && *cursorX > (int)line.length())
                *cursorX = line.length() > 0 ? line.length() - 1 : 0;
        }
    }

    std::ofstream file(*filename);
    if(file.is_open())
    {
        for(const auto& line : *lines)
            file << line << '\n';
        file.close();
        *dirty = false;
        currentBuffer->savedUndoIndex = currentBuffer->undoIndex;
        currentBuffer->savedContentHash = hash_lines(*lines);
        currentBuffer->savedContentHashValid = true;

        std::error_code ec;
        auto ftime = std::filesystem::last_write_time(*filename, ec);
        if(!ec)
            currentBuffer->lastModificationTime = ftime;

        std::string msg = "\"" + *filename + "\" " +
                          std::to_string(lines->size()) + "L written";
        if(linesModified > 0)
        {
            msg += " (" + std::to_string(linesModified) + " lines cleaned)";
            needsFullRedraw = true;
        }
        setStatusMessage(msg);
    }
    else
    {
        setStatusMessage("Can't save! I/O error");
    }
}

void Editor::checkFileChangesImpl()
{
    if(!currentBuffer || filename->empty() || *dirty)
        return;

    std::error_code ec;
    if(!std::filesystem::exists(*filename, ec) || ec)
        return;

    auto currentTime = std::filesystem::last_write_time(*filename, ec);
    if(ec)
        return;

    if(currentTime != currentBuffer->lastModificationTime)
        reloadCurrentFile();
}

void Editor::reloadCurrentFileImpl()
{
    if(!currentBuffer || filename->empty())
        return;

    int savedCursorX = *cursorX;
    int savedCursorY = *cursorY;
    int savedOffsetX = *offsetX;
    int savedOffsetY = *offsetY;
    std::string filepath = *filename;

    lines->clear();

    std::ifstream file(filepath);
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

    std::error_code ec;
    auto ftime = std::filesystem::last_write_time(filepath, ec);
    if(!ec)
        currentBuffer->lastModificationTime = ftime;

    *cursorY = std::min(savedCursorY, (int)lines->size() - 1);
    *cursorX = std::min(savedCursorX, (int)(*lines)[*cursorY].length());
    *offsetX = savedOffsetX;
    *offsetY =
        std::min(savedOffsetY, std::max(0, (int)lines->size() - screenRows));

    *dirty = false;
    currentBuffer->savedContentHash = hash_lines(*lines);
    currentBuffer->savedContentHashValid = true;
    needsFullRedraw = true;

    setStatusMessage("File reloaded from disk");
}

bool Editor::fileExistsImpl(const std::string& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::string Editor::getSymbolUnderCursorImpl()
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

    symbolPrefix.clear();
    int prefixStart = l;
    while(prefixStart >= 2 && line[prefixStart - 1] == ':' &&
          line[prefixStart - 2] == ':')
    {
        int p = prefixStart - 3;
        while(p >= 0 && isIdent(line[p]))
            p--;
        if(p + 1 >= prefixStart - 1)
            break;
        prefixStart = p + 1;
    }

    if(prefixStart < l)
        symbolPrefix = line.substr(prefixStart, l - prefixStart);

    return line.substr(l, r - l);
}

std::string Editor::findAlternateFileImpl(const std::string& currentFile)
{
    if(currentFile.empty())
        return "";

    size_t lastDot = currentFile.find_last_of('.');
    if(lastDot == std::string::npos)
        return "";

    std::string baseName = currentFile.substr(0, lastDot);
    std::string extension = currentFile.substr(lastDot);

    static const std::vector<std::string> headerExts = {".h", ".hpp", ".hxx",
                                                        ".H", ".HPP", ".HXX"};
    static const std::vector<std::string> sourceExts = {
        ".cpp", ".cc", ".cxx", ".c", ".C", ".CPP", ".CC", ".CXX"};

    bool isHeader = false;
    for(const auto& ext : headerExts)
    {
        if(extension == ext)
        {
            isHeader = true;
            break;
        }
    }

    std::vector<std::string> candidates;

    if(isHeader)
    {
        for(const auto& ext : sourceExts)
            candidates.push_back(baseName + ext);
    }
    else
    {
        for(const auto& ext : headerExts)
            candidates.push_back(baseName + ext);
    }

    size_t lastSlash = currentFile.find_last_of('/');
    std::string dir = "";
    std::string fileName = currentFile;

    if(lastSlash != std::string::npos)
    {
        dir = currentFile.substr(0, lastSlash + 1);
        fileName = currentFile.substr(lastSlash + 1);
        baseName = fileName.substr(0, fileName.find_last_of('.'));
    }

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

    for(const auto& [srcDir, incDir] : dirPairs)
    {
        if(dir.find(srcDir) != std::string::npos && isHeader == false)
        {
            std::string altDir = dir;
            size_t pos = altDir.find(srcDir);
            if(pos != std::string::npos)
            {
                altDir.replace(pos, srcDir.length(), incDir);
                for(const auto& ext : headerExts)
                    candidates.push_back(altDir + baseName + ext);
            }
        }
        else if(dir.find(incDir) != std::string::npos && isHeader == true)
        {
            std::string altDir = dir;
            size_t pos = altDir.find(incDir);
            if(pos != std::string::npos)
            {
                altDir.replace(pos, incDir.length(), srcDir);
                for(const auto& ext : sourceExts)
                    candidates.push_back(altDir + baseName + ext);
            }
        }
    }

    for(const auto& candidate : candidates)
    {
        if(fileExists(candidate))
            return candidate;
    }

    return "";
}

void Editor::jumpToAlternateFileImpl()
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

    int bufferIndex = findBufferByFilename(alternate);

    if(bufferIndex >= 0)
    {
        switchToBuffer(bufferIndex);
        setStatusMessage("Switched to " + alternate);
    }
    else
    {
        openFile(alternate);
        setStatusMessage("Opened " + alternate);
    }
}

void Editor::goToFileImpl()
{
    std::string word = getSymbolUnderCursor();
    if(!word.empty())
    {
        if(fileExists(word))
            openFile(word);
        else
            setStatusMessage("File not found: " + word);
    }
}

void Editor::showFileInfoImpl()
{
    std::string info =
        "\"" + (filename->empty() ? "[No Name]" : *filename) + "\"";
    info += " " + std::to_string(lines->size()) + " lines";
    if(*dirty)
        info += " [Modified]";
    info += " -- " + std::to_string(*cursorY + 1) + "/" +
            std::to_string(lines->size());
    info +=
        " -- " +
        std::to_string((*cursorY + 1) * 100 / std::max(1, (int)lines->size())) +
        "%";
    setStatusMessage(info);
}

void Editor::deleteFilePromptImpl()
{
    setStatusMessage("File deletion not yet implemented");
}

void Editor::renameFilePromptImpl()
{
    setStatusMessage("File rename not yet implemented");
}

void Editor::createNewFilePromptImpl()
{
    setMode(COMMAND);
    commandBuffer = ":e ";
    cancelCommandPopup();
    needsFullRedraw = true;
}

void Editor::createNewDirectoryPromptImpl()
{
    setStatusMessage("New directory creation not yet implemented");
}
