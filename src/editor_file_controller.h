#pragma once

#include <string>

class Editor;

class EditorFileController
{
public:
    explicit EditorFileController(Editor& editor);

    void saveFile();
    void checkFileChanges();
    void reloadCurrentFile();
    bool fileExists(const std::string& path);
    std::string getSymbolUnderCursor();
    std::string findAlternateFile(const std::string& currentFile);
    void jumpToAlternateFile();
    void goToFile();
    void showFileInfo();
    void deleteFilePrompt();
    void renameFilePrompt();
    void createNewFilePrompt();
    void createNewDirectoryPrompt();

private:
    Editor& editor;
};
