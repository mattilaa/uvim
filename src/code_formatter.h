#pragma once

#include "editor_context.h"
#include <string>

class UndoManager;

class CodeFormatter
{
public:
    CodeFormatter(EditorContext& ctx, UndoManager& undoMgr);

    // Format entire file with clang-format
    void formatFile();

    // Format selection (for visual mode)
    void formatSelection(int startLine, int endLine);

private:
    EditorContext& ctx;
    UndoManager& undoMgr;

    bool isCppFile() const;
    std::string findClangFormat() const;
    std::string getAbsoluteFilename() const;
};
