#include "editor_file_controller.h"
#include "editor.h"
#include "editor_utils.h"
#include "text_utils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using editor::helper::hash_lines;

namespace fs = std::filesystem;

namespace
{
bool isIdent(char c)
{
    return std::isalnum((unsigned char)c) || c == '_';
}

template <size_t N>
bool extensionIn(std::string_view extension,
                 const std::array<std::string_view, N>& extensions)
{
    return std::find(extensions.begin(), extensions.end(), extension) !=
           extensions.end();
}

fs::path projectRootFor(const fs::path& file)
{
    std::error_code ec;
    fs::path dir = file.has_parent_path() ? file.parent_path() : fs::path{"."};

    for(fs::path current = fs::absolute(dir, ec); !current.empty();
        current = current.parent_path())
    {
        if(fs::exists(current / ".git", ec) ||
           fs::exists(current / "CMakeLists.txt", ec))
            return current;

        if(current == current.parent_path())
            break;
    }

    return fs::absolute(dir, ec);
}

std::string localIncludeFromLine(std::string_view line)
{
    size_t pos = line.find_first_not_of(" \t");
    if(text_utils::is_not_found(pos) || line[pos] != '#')
        return "";

    ++pos;
    pos = line.find_first_not_of(" \t", pos);
    if(text_utils::is_not_found(pos))
        return "";

    constexpr std::string_view include = "include";
    if(line.substr(pos, include.size()) != include)
        return "";

    pos += include.size();
    pos = line.find_first_not_of(" \t", pos);
    if(text_utils::is_not_found(pos) || line[pos] != '"')
        return "";

    const size_t end = line.find('"', pos + 1);
    if(text_utils::is_not_found(end))
        return "";

    return std::string(line.substr(pos + 1, end - pos - 1));
}

fs::path resolveLocalInclude(const fs::path& include, const fs::path& sourceDir,
                             const fs::path& projectRoot)
{
    std::error_code ec;
    const std::array candidates = {
        sourceDir / include,
        projectRoot / include,
        projectRoot / "include" / include,
        projectRoot / "src" / include,
    };

    for(const auto& candidate : candidates)
    {
        if(fs::exists(candidate, ec))
            return candidate.lexically_normal();
    }

    return {};
}

bool sameFile(const fs::path& left, const fs::path& right)
{
    std::error_code ec;
    const fs::path leftCanonical = fs::weakly_canonical(left, ec);
    if(ec)
        return false;

    const fs::path rightCanonical = fs::weakly_canonical(right, ec);
    if(ec)
        return false;

    return leftCanonical == rightCanonical;
}

bool sourceIncludesHeader(const fs::path& source, const fs::path& header,
                          const fs::path& projectRoot)
{
    std::ifstream in(source);
    if(!in.is_open())
        return false;

    std::string line;
    while(std::getline(in, line))
    {
        const std::string include = localIncludeFromLine(line);
        if(include.empty())
            continue;

        const fs::path resolved =
            resolveLocalInclude(include, source.parent_path(), projectRoot);
        if(!resolved.empty())
            return sameFile(resolved, header);

        if(fs::path(include).filename() == header.filename())
            return true;
    }

    return false;
}
} // namespace

EditorFileController::EditorFileController(Editor& editor) : editor(editor) {}

void EditorFileController::saveFile()
{
    auto* filename = editor.filename;
    auto* lines = editor.lines;
    auto* cursorX = editor.cursorX;
    auto* cursorY = editor.cursorY;
    auto* dirty = editor.dirty;

    if(filename->empty())
    {
        editor.setStatusMessage("No file name");
        return;
    }

    if(editor.formatOnSave)
        editor.formatBufferForSave();

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
        if(text_utils::is_found(endPos))
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
        editor.currentBuffer->savedUndoIndex = editor.currentBuffer->undoIndex;
        editor.currentBuffer->savedContentHash = hash_lines(*lines);
        editor.currentBuffer->savedContentHashValid = true;

        std::error_code ec;
        auto ftime = std::filesystem::last_write_time(*filename, ec);
        if(!ec)
            editor.currentBuffer->lastModificationTime = ftime;

        std::string msg = "\"" + *filename + "\" " +
                          std::to_string(lines->size()) + "L written";
        if(linesModified > 0)
        {
            msg += " (" + std::to_string(linesModified) + " lines cleaned)";
            editor.needsFullRedraw = true;
        }
        editor.setStatusMessage(msg);
    }
    else
    {
        editor.setStatusMessage("Can't save! I/O error");
    }
}

void EditorFileController::checkFileChanges()
{
    auto* filename = editor.filename;
    auto* dirty = editor.dirty;

    if(!editor.currentBuffer || filename->empty() || *dirty)
        return;

    std::error_code ec;
    if(!std::filesystem::exists(*filename, ec) || ec)
        return;

    auto currentTime = std::filesystem::last_write_time(*filename, ec);
    if(ec)
        return;

    if(currentTime != editor.currentBuffer->lastModificationTime)
        reloadCurrentFile();
}

void EditorFileController::reloadCurrentFile()
{
    auto* filename = editor.filename;
    auto* lines = editor.lines;
    auto* cursorX = editor.cursorX;
    auto* cursorY = editor.cursorY;
    auto* offsetX = editor.offsetX;
    auto* offsetY = editor.offsetY;
    auto* dirty = editor.dirty;

    if(!editor.currentBuffer || filename->empty())
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
        editor.currentBuffer->lastModificationTime = ftime;

    *cursorY = std::min(savedCursorY, (int)lines->size() - 1);
    *cursorX = std::min(savedCursorX, (int)(*lines)[*cursorY].length());
    *offsetX = savedOffsetX;
    *offsetY = std::min(savedOffsetY,
                        std::max(0, (int)lines->size() - editor.screenRows));

    *dirty = false;
    editor.currentBuffer->savedContentHash = hash_lines(*lines);
    editor.currentBuffer->savedContentHashValid = true;
    editor.needsFullRedraw = true;

    editor.setStatusMessage("File reloaded from disk");
}

bool EditorFileController::fileExists(const std::string& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::string EditorFileController::getSymbolUnderCursor()
{
    auto* lines = editor.lines;
    auto* cursorX = editor.cursorX;
    auto* cursorY = editor.cursorY;
    auto& symbolPrefix = editor.symbolPrefix;

    if(*cursorY >= lines->size())
        return "";

    const std::string& line = (*lines)[*cursorY];
    int x = std::clamp(*cursorX, 0, (int)line.size());

    auto identifierToRight = [&]() -> std::optional<int>
    {
        int right = x;
        while(right < (int)line.size() &&
              !std::isspace(static_cast<unsigned char>(line[right])) &&
              !isIdent(line[right]))
        {
            ++right;
        }
        if(right < (int)line.size() && isIdent(line[right]))
            return right;
        return std::nullopt;
    };

    if(x >= (int)line.size() || !isIdent(line[x]))
    {
        if(x < (int)line.size() && line[x] == ':')
        {
            if(auto right = identifierToRight())
                x = *right;
            else if(x > 0 && isIdent(line[x - 1]))
                --x;
            else
                return "";
        }
        else if(x > 0 && isIdent(line[x - 1]))
            --x;
        else
        {
            if(auto right = identifierToRight())
                x = *right;
            else
                return "";
        }
    }

    int l = x;
    int r = x;

    while(l > 0 && isIdent(line[l - 1]))
        l--;
    while(r < (int)line.size() && isIdent(line[r]))
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

std::string
EditorFileController::findAlternateFile(const std::string& currentFile)
{
    if(currentFile.empty())
        return "";

    static constexpr std::array<std::string_view, 6> headerExts = {
        ".h", ".hpp", ".hxx", ".H", ".HPP", ".HXX"};

    static constexpr std::array<std::string_view, 8> sourceExts = {
        ".cpp", ".cc", ".cxx", ".c", ".C", ".CPP", ".CC", ".CXX"};

    struct DirPair
    {
        std::string_view srcDir;
        std::string_view incDir;
    };

    static constexpr std::array<DirPair, 12> dirPairs = {{
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
    }};

    const size_t lastDot = currentFile.find_last_of('.');
    if(text_utils::is_not_found(lastDot))
        return "";

    const std::string extension = currentFile.substr(lastDot);

    const bool isHeader = extensionIn(extension, headerExts);
    const bool isSource = extensionIn(extension, sourceExts);
    if(!isHeader && !isSource)
        return "";

    auto checkCandidate = [this](const std::string& candidate) -> std::string
    { return fileExists(candidate) ? candidate : ""; };

    const size_t lastSlash = currentFile.find_last_of('/');

    std::string dir;
    std::string fileName = currentFile;

    if(text_utils::is_found(lastSlash))
    {
        dir = currentFile.substr(0, lastSlash + 1);
        fileName = currentFile.substr(lastSlash + 1);
    }

    const size_t fileDot = fileName.find_last_of('.');
    if(text_utils::is_not_found(fileDot))
        return "";

    const std::string baseName = fileName.substr(0, fileDot);
    const fs::path currentPath(currentFile);
    const fs::path currentDir =
        currentPath.has_parent_path() ? currentPath.parent_path() : fs::path{};

    if(isHeader)
    {
        for(std::string_view ext : sourceExts)
        {
            const fs::path candidate =
                currentDir / (baseName + std::string(ext));
            if(auto found = checkCandidate(candidate.string()); !found.empty())
                return found;
        }
    }
    else
    {
        for(std::string_view ext : headerExts)
        {
            const fs::path candidate =
                currentDir / (baseName + std::string(ext));
            if(auto found = checkCandidate(candidate.string()); !found.empty())
                return found;
        }
    }

    if(isHeader)
    {
        for(std::string_view ext : sourceExts)
        {
            if(auto found = checkCandidate(baseName + std::string(ext));
               !found.empty())
                return found;
        }
    }
    else
    {
        for(std::string_view ext : headerExts)
        {
            if(auto found = checkCandidate(baseName + std::string(ext));
               !found.empty())
                return found;
        }
    }

    for(const auto& [srcDir, incDir] : dirPairs)
    {
        if(!isHeader && text_utils::contains(dir, srcDir))
        {
            std::string altDir = dir;
            const size_t pos = altDir.find(srcDir);

            if(text_utils::is_found(pos))
            {
                altDir.replace(pos, srcDir.length(), incDir);

                for(std::string_view ext : headerExts)
                {
                    if(auto found =
                           checkCandidate(altDir + baseName + std::string(ext));
                       !found.empty())
                        return found;
                }
            }
        }
        else if(isHeader && text_utils::contains(dir, incDir))
        {
            std::string altDir = dir;
            const size_t pos = altDir.find(incDir);

            if(text_utils::is_found(pos))
            {
                altDir.replace(pos, incDir.length(), srcDir);

                for(std::string_view ext : sourceExts)
                {
                    if(auto found =
                           checkCandidate(altDir + baseName + std::string(ext));
                       !found.empty())
                        return found;
                }
            }
        }
    }

    const fs::path projectRoot = projectRootFor(currentPath);

    if(isSource)
    {
        std::ifstream source(currentFile);
        std::string line;
        while(std::getline(source, line))
        {
            const std::string include = localIncludeFromLine(line);
            if(include.empty() ||
               !extensionIn(fs::path(include).extension().string(), headerExts))
                continue;

            const fs::path resolved =
                resolveLocalInclude(include, currentDir, projectRoot);
            if(!resolved.empty())
                return resolved.string();
        }
    }
    else
    {
        std::error_code ec;
        for(fs::recursive_directory_iterator it(projectRoot, ec), end;
            !ec && it != end; it.increment(ec))
        {
            if(it->is_directory(ec))
            {
                const std::string name = it->path().filename().string();
                if(name == ".git" || name == ".uvim" || name == "build")
                    it.disable_recursion_pending();
                continue;
            }

            if(!it->is_regular_file(ec) ||
               !extensionIn(it->path().extension().string(), sourceExts))
                continue;

            if(sourceIncludesHeader(it->path(), currentPath, projectRoot))
                return it->path().string();
        }
    }

    return "";
}

void EditorFileController::jumpToAlternateFile()
{
    auto* filename = editor.filename;

    if(filename->empty())
    {
        editor.setStatusMessage("No file currently open");
        return;
    }

    std::string alternate = findAlternateFile(*filename);

    if(alternate.empty())
    {
        editor.setStatusMessage("No alternate file found for " + *filename);
        return;
    }

    int bufferIndex = editor.findBufferByFilename(alternate);

    if(bufferIndex >= 0)
    {
        editor.switchToBuffer(bufferIndex);
        editor.setStatusMessage("Switched to " + alternate);
    }
    else
    {
        editor.openFile(alternate);
        editor.setStatusMessage("Opened " + alternate);
    }
}

void EditorFileController::goToFile()
{
    std::string word = getSymbolUnderCursor();
    if(!word.empty())
    {
        if(fileExists(word))
            editor.openFile(word);
        else
            editor.setStatusMessage("File not found: " + word);
    }
}

void EditorFileController::showFileInfo()
{
    auto* filename = editor.filename;
    auto* lines = editor.lines;
    auto* cursorY = editor.cursorY;
    auto* dirty = editor.dirty;

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
    editor.setStatusMessage(info);
}

void EditorFileController::deleteFilePrompt()
{
    editor.setStatusMessage("File deletion not yet implemented");
}

void EditorFileController::renameFilePrompt()
{
    editor.setStatusMessage("File rename not yet implemented");
}

void EditorFileController::createNewFilePrompt()
{
    editor.setMode(COMMAND);
    editor.commandBuffer = ":e ";
    editor.cancelCommandPopup();
    editor.needsFullRedraw = true;
}

void EditorFileController::createNewDirectoryPrompt()
{
    editor.setStatusMessage("New directory creation not yet implemented");
}
