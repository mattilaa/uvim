#pragma once

#include "editor_view.h"

class EditorViewGitLog : public EditorView
{
public:
    bool draw(Editor& editor) override;
};
