#pragma once

#include "editor_view.h"

class EditorViewVisualLine : public EditorView
{
public:
    bool draw(Editor& editor) override;
};
