#pragma once

#include "token_type.h"

#include <cstdint>
#include <string>
#include <vector>

// Syntax tokens for a picker preview pane: a file other than the current
// buffer, tokenized incrementally from its first line so multi-line state
// (block comments, fences, multi-line strings) is carried correctly. Kept by
// the picker and extended on demand by Editor::highlightPreviewLines().
struct PreviewHighlight
{
    std::string path;
    uint64_t fileTypeMask = 0;
    // Tokenizer state after the last tokenized line.
    bool inBlockComment = false;
    bool inTomlMultiline = false;
    char tomlQuote = 0;
    bool inMarkupFence = false;
    char markupFenceChar = 0;
    std::vector<std::vector<Token>> tokens; // per line, from line 0
};
