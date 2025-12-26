#pragma once

#include <string>

// LSP completion popup entry (extracted from Editor class)
struct CompletionEntry
{
    // Primary display label (clangd: CompletionItem.label)
    std::string label;

    // Text to insert (may be snippet syntax)
    std::string insertText;
    bool isSnippet = false;

    // Extra info for richer UI + fuzzy matching
    int kind = 0;            // CompletionItemKind
    std::string detail;      // often signature or type
    std::string labelDetail; // labelDetails.detail (often "(...)")
    std::string
        labelDescription;   // labelDetails.description (often return/type)
    std::string filterText; // optional hint for filtering
};
