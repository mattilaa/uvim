#pragma once

#include <string>
#include <vector>

// Forward declarations
class EditorContext;

// ============================================================================
// GrepMatch - Represents a grep search match result
// ============================================================================

struct GrepMatch
{
    std::string filepath;
    int lineNumber = 0;
    int columnNumber = 0;
    std::string lineContent;
    std::string matchText;
};

// ============================================================================
// GrepSearch - Self-contained grep search component
// ============================================================================
//
// Handles grep searching functionality including:
// - Running ripgrep/grep subprocess
// - Result navigation and selection
// - Drawing the search UI
//
// ============================================================================

class GrepSearch
{
public:
    explicit GrepSearch(EditorContext& ctx);

    // ========================================================================
    // Initialization
    // ========================================================================

    void open();
    void close();

    // ========================================================================
    // Input Handling
    // ========================================================================

    void addChar(char c);
    void backspace();
    void deleteWord();
    void clear();

    // ========================================================================
    // Navigation
    // ========================================================================

    void moveUp();
    void moveDown();
    void halfPageUp();
    void halfPageDown();

    // ========================================================================
    // Selection
    // ========================================================================

    // Returns true if a match was selected and opened
    bool selectEntry();

    // ========================================================================
    // Preview
    // ========================================================================

    void togglePreview();

    // ========================================================================
    // Drawing
    // ========================================================================

    void draw();
    void drawStatusLine();

    // ========================================================================
    // Accessors
    // ========================================================================

    const std::string& query() const
    {
        return m_query;
    }
    const std::vector<GrepMatch>& matches() const
    {
        return m_matches;
    }
    int cursorPosition() const
    {
        return m_cursor;
    }
    int scrollOffset() const
    {
        return m_offset;
    }
    bool showingPreview() const
    {
        return m_showPreview;
    }
    bool isActive() const
    {
        return m_active;
    }
    bool isSearching() const
    {
        return m_searching;
    }

private:
    // ========================================================================
    // Internal Methods
    // ========================================================================

    void performSearch();
    void parseGrepOutput(const std::string& output);
    void adjustScroll();

    // ========================================================================
    // State
    // ========================================================================

    EditorContext& m_ctx;

    std::string m_query;
    std::vector<GrepMatch> m_matches;

    int m_cursor = 0;
    int m_offset = 0;
    bool m_showPreview = false;
    bool m_active = false;
    bool m_searching = false;

    // Debounce timer for search
    int m_searchDelay = 200; // ms
};
