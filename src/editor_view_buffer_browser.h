#pragma once

#include "editor_view.h"

class EditorViewBufferBrowser : public EditorView
{
public:
    bool draw(Editor& editor) override;
};
