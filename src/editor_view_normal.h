#pragma once

#include "editor_view.h"

class EditorViewNormal : public EditorView
{
public:
    bool draw(Editor& editor) override;
};
