#pragma once

#include "editor_context.h"
#include <string>

class BufferManager;
class CursorMovement;

class LspIntegration
{
public:
    LspIntegration(EditorContext& ctx, BufferManager& bufferMgr,
                   CursorMovement& cursor);

    // Initialize LSP
    void enable(const std::string& compileCommandsDir,
                const std::string& clangdPath,
                const std::string& queryDriverAllowList);

    bool isEnabled() const;

    // Document sync
    void didOpen(const std::string& filename);
    void didChange();
    void didSave(const std::string& filename);
    void didClose(const std::string& filename);

    // Go to definition
    void goToDefinition();

    // Completion
    void requestCompletion();
    void cancelCompletion();
    void completionNext();
    void completionPrev();
    void acceptCompletion();
    void rebuildCompletionFilter();

    // Draw completion popup
    void drawCompletionPopup(std::string& output) const;

private:
    EditorContext& ctx;
    BufferManager& bufferMgr;
    CursorMovement& cursor;

    bool isCppFile() const;
    std::string getSymbolUnderCursor() const;
    bool searchDefinitionInBuffer(Buffer* buf, const std::string& symbol,
                                  int& outLine, int& outCol);
};
