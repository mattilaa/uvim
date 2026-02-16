#pragma once

#include "editor_view.h"

class EditorViewSearchBackward : public EditorView
{
public:
    bool draw(Editor& editor) override;
};
