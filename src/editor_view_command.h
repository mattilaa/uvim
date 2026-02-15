#pragma once

#include "editor_view.h"

class EditorViewCommand : public EditorView
{
public:
    bool draw(Editor& editor) override;
};
