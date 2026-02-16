#pragma once

#include "editor_view.h"

class EditorViewGitPatch : public EditorView
{
public:
    bool draw(Editor& editor) override;
};
