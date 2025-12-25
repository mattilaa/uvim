#pragma once

#include "editor_context.h"
#include <string>

// Forward declarations
class BufferManager;

class FileIO
{
public:
    FileIO(EditorContext& ctx, BufferManager& bufferMgr);

    void openFile(const std::string& fname);
    void saveFile();
    bool fileExists(const std::string& path);

    std::string findAlternateFile(const std::string& currentFile);
    void jumpToAlternateFile();

    std::string getSymbolUnderCursor();

private:
    EditorContext& ctx;
    BufferManager& bufferMgr;

    bool isCppFile() const;
    void notifyLspFileOpened(const std::string& fname);
    void notifyLspFileSaved(const std::string& fname);
};
