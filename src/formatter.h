#pragma once

#include <string>
#include <string_view>

class Editor;

class Formatter
{
public:
    explicit Formatter(Editor* editor);

    size_t byteOffsetForPosition(int y, int x) const;
    bool clangFormatWithArgs(const std::string& extraArgs,
                             const std::string& successMessage);
    bool pythonFormatBuffer();
    void pythonLintBuffer();
    bool robotFormatBuffer();
    bool jsonFormatBuffer();
    bool yamlFormatBuffer();
    bool mlangFormatBuffer();
    void clangFormatVisualSelection();
    void clangFormatVisualBlockSelection();

private:
    Editor* editor;
};
