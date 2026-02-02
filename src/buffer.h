#pragma once

#include "syntax_state.h"
#include "token_type.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

// Buffer structure to hold file data (extracted from Editor class)
struct Buffer
{
    std::vector<std::string> lines;
    std::string filename;
    bool dirty = false;
    std::filesystem::file_time_type lastModificationTime{};

    // Cursor position per buffer
    int cursorX = 0;
    int cursorY = 0;
    int wantedX = 0;

    // View position per buffer
    int offsetX = 0;
    int offsetY = 0;

    // Marks per buffer
    std::unordered_map<char, std::pair<int, int>> marks;

    struct BlameEntry
    {
        std::string hash;
        std::string author;
        std::string date;
        bool valid = false;
    };

    // Undo/redo stack per buffer
    struct EditState
    {
        std::vector<std::string> lines;
        int cursorX, cursorY;
        std::vector<BlameEntry> blameEntries;
        int blameStart = -1;
        int blameEnd = -1;
        bool blameValid = false;
    };
    std::vector<EditState> undoStack;
    int undoIndex = -1;
    int savedUndoIndex = -1; // Track which undo state was last saved

    // Search state per buffer
    std::string lastSearchQuery;
    bool lastSearchForward = true;

    // Visual mode state per buffer
    int visualStartX = 0;
    int visualStartY = 0;
    int visualEndX = 0;
    int visualEndY = 0;

    // Visual block mode state per buffer
    int visualBlockStartX = 0;
    int visualBlockStartY = 0;
    int visualBlockEndX = 0;
    int visualBlockEndY = 0;
    std::string visualBlockInsertText; // Text to insert in visual block mode
    bool lspSyncNeeded = false;
    bool lspHashValid = false;
    size_t lspContentHash = 0;
    bool lspDiagnosticsSeenValid = false;
    size_t lspDiagnosticsSeenRevision = 0;
    bool clangIndentWidthValid = false;
    int clangIndentWidth = -1;
    bool clangBraceStyleValid = false;
    bool clangBraceNewLine = false;
    std::vector<BlameEntry> blameEntries;
    int blameStart = -1;
    int blameEnd = -1;
    bool blameValid = false;

    struct SyntaxCacheLine
    {
        bool valid = false;
        bool inBlockComment = false;
        bool inTomlMultiline = false;
        char tomlQuote = 0;
        bool inMarkupFence = false;
        char markupFenceChar = 0;
        CppMethodScanState methodState;
        CppFunctionScanState functionState;
        CppParamListScanState paramState;
        bool inCppMethodContext = false;
        bool inCppFunctionContext = false;
        bool inCppParamListContext = false;
    };

    std::vector<SyntaxCacheLine> syntaxCache;
    int syntaxCacheComputedUpTo = -1;

    struct SemanticTokenRange
    {
        int start = 0;
        int length = 0;
        std::string tokenType;
        bool isDeclaration = false;
        bool isDefinition = false;
    };

    std::vector<std::vector<SemanticTokenRange>> lspSemanticTokens;
    bool lspSemanticTokensValid = false;
    size_t lspSemanticTokensHash = 0;
    size_t lspSemanticTokensRevision = 0;

    Buffer()
    {
        lines.push_back("");
    }
};
