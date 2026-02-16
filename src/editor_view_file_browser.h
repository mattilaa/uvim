#pragma once

#include "editor_view.h"

class EditorViewFileBrowser : public EditorView
{
public:
    bool draw(Editor& editor) override;
};
