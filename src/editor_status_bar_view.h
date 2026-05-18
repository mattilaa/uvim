#pragma once

#include "editor_draw_component.h"

#include <string>

class EditorStatusBarView : public EditorDrawComponent
{
public:
    using EditorDrawComponent::EditorDrawComponent;

    void append(std::string& output);
    void draw();
    void drawQuick();
};
