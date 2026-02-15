#pragma once

#include "editor_view.h"

class EditorViewOperatorPending : public EditorView
{
public:
    bool draw(Editor& editor) override;
};
