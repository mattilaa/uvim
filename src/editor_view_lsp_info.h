#pragma once

#include "editor_view.h"

class EditorViewLspInfo : public EditorView
{
public:
    bool draw(Editor& editor) override;
};
