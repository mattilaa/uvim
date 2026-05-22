#pragma once

#include "buffer.h"
#include "file_type.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Editor;
class LspClient;

class EditorDefinitionController
{
public:
    explicit EditorDefinitionController(Editor& editor);

    void goToDefinition();

private:
    Editor& editor;

    void applyViewport();
    void clampCursor();
    std::string bufferText() const;
    bool jumpToLocation(const std::string& path, int line, int character,
                        std::string_view label);
    bool goToInclude();
    bool goToStdSymbol(const std::string& symbol);
    bool goToRobotDefinition();
    bool goToPythonDefinition(const std::string& symbol);
    bool goToWebDefinition(FileType fileType, const std::string& symbol);
    bool goToMlangDefinition(const std::string& symbol);
    bool goToAsmDefinition();
    bool goToCppDefinition(const std::string& symbol);

#ifdef UVIM_ENABLE_CLANGD_LSP
    bool goToGenericLspDefinition(LspClient* client, const char* languageId,
                                  std::string_view label);
#endif
};
