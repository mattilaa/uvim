#pragma once

#include "editor_view.h"

class EditorViewGitCommit : public EditorView
{
public:
    bool draw(Editor& editor) override;
};
