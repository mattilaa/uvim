#pragma once

#include <string>
#include <vector>

// Forward declarations
class EditorContext;
struct Buffer;

// ============================================================================
// BufferEntry - Represents a buffer in the browser
// ============================================================================

struct BufferEntry
{
    int index;
    std::string filename;
    std::string displayName;
    bool modified = false;
    bool isCurrent = false;
    int lineCount = 0;
};

// ============================================================================
// BufferBrowser - Self-contained buffer browser component
// ============================================================================
//
// Handles buffer browsing functionality including:
// - Listing all open buffers
// - Buffer navigation and selection
// - Buffer deletion
// - Drawing the buffer list UI
//
// ============================================================================

class BufferBrowser
{
public:
    explicit BufferBrowser(EditorContext& ctx);

    // ========================================================================
    // Initialization
    // ========================================================================

    void open();
    void close();

    // ========================================================================
    // Navigation
    // ========================================================================

    void moveUp();
    void moveDown();
    void moveToStart();
    void moveToEnd();
    void halfPageUp();
    void halfPageDown();

    // ========================================================================
    // Selection
    // ========================================================================

    // Returns true if a buffer was selected
    bool selectEntry();
    bool selectByNumber(int num);

    // ========================================================================
    // Buffer Operations
    // ========================================================================

    void deleteSelectedBuffer();

    // ========================================================================
    // Drawing
    // ========================================================================

    void draw();
    void drawStatusLine();

    // ========================================================================
    // Accessors
    // ========================================================================

    const std::vector<BufferEntry>& entries() const
    {
        return m_entries;
    }
    int cursorPosition() const
    {
        return m_cursor;
    }
    int scrollOffset() const
    {
        return m_offset;
    }
    bool isActive() const
    {
        return m_active;
    }

private:
    // ========================================================================
    // Internal Methods
    // ========================================================================

    void refreshEntries();
    void adjustScroll();

    // ========================================================================
    // State
    // ========================================================================

    EditorContext& m_ctx;

    std::vector<BufferEntry> m_entries;

    int m_cursor = 0;
    int m_offset = 0;
    bool m_active = false;
};
