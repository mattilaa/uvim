#pragma once

#include "file_entry.h"
#include <string>
#include <vector>

class Editor;

class FileBrowser
{
public:
    void open(Editor& editor, const std::string& path);
    void loadDirectory(Editor& editor, const std::string& path);
    void draw(Editor& editor) const;

    bool selectEntry(Editor& editor);
    void parent(Editor& editor);
    void up(int screenRows);
    void down(int screenRows);
    void start();
    void end(int screenRows);
    void halfPageUp(int screenRows);
    void halfPageDown(int screenRows);
    void toggleHidden(Editor& editor);
    void toggleGitignore(Editor& editor);
    void refresh(Editor& editor);
    void setDirectory(Editor& editor, const std::string& path);

    const std::string& directory() const;
    bool isRespectGitignore() const;
    bool isShowHidden() const;
    bool hasEntries() const;

    void restorePrevious(Editor& editor) const;

private:
    void navigateTo(Editor& editor, const FileEntry& entry);
    std::string formatFileSize(size_t size) const;
    std::string formatFileTime(time_t time) const;

    std::vector<FileEntry> fileList;
    std::string currentDirectory;
    std::string previousFile;
    int browserCursor = 0;
    int browserOffset = 0;
    bool showHidden = false;
    bool respectGitignore = true;
};
