#pragma once

#include "editor_draw_component.h"

#include <string>

class EditorMessageBarView : public EditorDrawComponent
{
public:
    using EditorDrawComponent::EditorDrawComponent;

    void append(std::string& output, bool includePopups);
    void draw();
    void drawQuick();
};
