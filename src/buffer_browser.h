#pragma once

#include "search_types.h"
#include <string>
#include <vector>

class Editor;

class BufferBrowser
{
public:
    void initialize(Editor& editor);
    void updateMatches(Editor& editor);
    void draw(Editor& editor) const;

    void up(int screenRows);
    void down(int screenRows);
    void start();
    void end(int screenRows);
    void halfPageUp(int screenRows);
    void halfPageDown(int screenRows);

    void addChar(Editor& editor, char c);
    void backspace(Editor& editor);
    void clear(Editor& editor);

    bool selectEntry(Editor& editor);
    void deleteSelected(Editor& editor);
    bool switchToBufferByNumber(Editor& editor, int num);

private:
    void selectMatch(Editor& editor);

    std::vector<BufferMatch> bufferMatches;
    std::string bufferQuery;
    int bufferCursor = 0;
    int bufferOffset = 0;
};
