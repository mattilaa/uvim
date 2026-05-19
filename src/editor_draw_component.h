#pragma once

class Editor;

class EditorDrawComponent
{
public:
    explicit EditorDrawComponent(Editor& editor) : editor(editor) {}
    virtual ~EditorDrawComponent() = default;

protected:
    Editor& editor;
};
