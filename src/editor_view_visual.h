#pragma once

#include "editor_view.h"

class EditorViewVisual : public EditorView
{
public:
    bool draw(Editor& editor) override;
};
