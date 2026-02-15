#pragma once

#include "editor_view.h"

class EditorViewInsert : public EditorView
{
public:
    bool draw(Editor& editor) override;
};
