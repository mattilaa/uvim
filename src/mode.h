#pragma once

// Editor modes (extracted from Editor class)
// Using unscoped enum to match original Editor::Mode usage
enum Mode
{
    WELCOME,
    NORMAL,
    INSERT,
    REPLACE,
    VISUAL,
    VISUAL_LINE,
    VISUAL_BLOCK,
    COMMAND,
    SEARCH_FORWARD,
    SEARCH_BACKWARD,
    FILE_BROWSER,
    FUZZY_FIND,
    BUFFER_BROWSER,
    GREP_SEARCH,
    OP_PENDING,
    REFERENCES,
    LSP_INFO,
    LOC_LIST,
    HELP,
    GIT_SHOW,
    GIT_LOG,
    GIT_STAGE,
};
