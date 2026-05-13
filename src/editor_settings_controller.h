#pragma once

#include <string_view>

class Editor;

class EditorSettingsController
{
public:
    explicit EditorSettingsController(Editor& editor);

    bool handleSetCommand(std::string_view cmd);

private:
    Editor& editor;
};
