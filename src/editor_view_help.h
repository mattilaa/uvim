#pragma once

#include "editor_view.h"

class EditorViewHelp : public EditorView
{
public:
    bool draw(Editor& editor) override;
};
