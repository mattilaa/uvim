#pragma once

#include "editor_view.h"

class EditorViewWelcome : public EditorView
{
public:
    bool draw(Editor& editor) override;
};
