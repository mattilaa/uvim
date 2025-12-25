#pragma once

#include "editor_context.h"
#include <string>

class SyntaxHighlighter;

class Renderer
{
public:
    Renderer(EditorContext& ctx, SyntaxHighlighter& syntax);

    void draw();
    void refreshScreen();
    void drawFullScreen();
    void drawScrollUpdate(int scrollDelta);

    void drawRows();
    void drawStatusBar();
    void drawMessageBar();
    void drawFileBrowser();
    void drawFuzzyFind();
    void drawBufferBrowser();
    void drawGrepSearch();
    void drawCompletionPopup(std::string& output) const;

    void updateCursorPosition();

private:
    EditorContext& ctx;
    SyntaxHighlighter& syntax;

    void drawStatusBarQuick();
    void drawMessageBarQuick();

    std::string getModeString() const;
    bool isInSelection(int row, int col) const;
    bool isInVisualBlock(int row, int col) const;
    bool isInSearchMatch(int row, int col) const;
};
