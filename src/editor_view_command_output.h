#pragma once

#include "editor_view.h"

class EditorViewCommandOutput : public EditorView
{
public:
    bool draw(Editor& editor) override;
};
