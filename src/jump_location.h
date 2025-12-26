#pragma once

// Jump location for Ctrl-O/Ctrl-I navigation (extracted from Editor class)
struct JumpLocation
{
    int bufferIndex;
    int cursorX;
    int cursorY;
    int offsetX;
    int offsetY;
};
