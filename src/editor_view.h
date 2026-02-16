#pragma once

class Editor;

class EditorView
{
public:
    virtual ~EditorView() = default;
    virtual bool draw(Editor& editor) = 0;
};
