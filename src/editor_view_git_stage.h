#pragma once

#include "editor_view.h"

class EditorViewGitStage : public EditorView
{
public:
    bool draw(Editor& editor) override;
};
