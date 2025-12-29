#pragma once

#include <string>
#include <vector>

// Forward declarations
class EditorContext;

// ============================================================================
// FuzzyMatch - Represents a fuzzy search match result
// ============================================================================

struct FuzzyMatch
{
    std::string path;
    std::string displayName;
    int score = 0;
    std::vector<size_t> matchPositions;
};

// ============================================================================
// FuzzyFinder - Self-contained fuzzy file finder component
// ============================================================================
//
// Handles fuzzy file finding functionality including:
// - File discovery and caching
// - Fuzzy matching algorithm
// - Result navigation and selection
// - Drawing the finder UI
//
// ============================================================================

class FuzzyFinder
{
public:
    explicit FuzzyFinder(EditorContext& ctx);

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

    // Returns true if a file was selected and opened
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
    const std::vector<FuzzyMatch>& matches() const
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

private:
    // ========================================================================
    // Internal Methods
    // ========================================================================

    void collectFiles();
    void updateMatches();
    int fuzzyScore(const std::string& str, const std::string& pattern,
                   std::vector<size_t>& positions) const;
    void adjustScroll();

    // ========================================================================
    // State
    // ========================================================================

    EditorContext& m_ctx;

    std::string m_query;
    std::vector<std::string> m_allFiles;
    std::vector<FuzzyMatch> m_matches;

    int m_cursor = 0;
    int m_offset = 0;
    bool m_showPreview = false;
    bool m_active = false;
    bool m_filesCollected = false;
};
