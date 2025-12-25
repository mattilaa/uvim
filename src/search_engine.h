#pragma once

#include "editor_context.h"
#include <string>

class SearchEngine
{
public:
    explicit SearchEngine(EditorContext& ctx);

    void startSearchForward();
    void startSearchBackward();
    void performSearch();
    void findAllMatches();
    void jumpToMatch(int index);
    void searchNext();
    void searchPrevious();
    void clearSearch();
    void cancelSearch();

    bool isInSearchMatch(int row, int col) const;

private:
    EditorContext& ctx;
};
