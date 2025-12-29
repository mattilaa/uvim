#pragma once

#include <ctime>
#include <string>
#include <vector>

// Forward declarations
class EditorContext;

// ============================================================================
// FileEntry - Represents a file or directory in the browser
// ============================================================================

struct FileEntry
{
    std::string name;
    std::string path;
    bool isDirectory = false;
    size_t size = 0;
    time_t modTime = 0;
};

// ============================================================================
// FileBrowser - Self-contained file browser component
// ============================================================================
//
// Handles all file browser functionality including:
// - Directory listing and navigation
// - File selection and opening
// - File operations (create, delete, rename)
// - Drawing the file browser UI
//
// ============================================================================

class FileBrowser
{
public:
    explicit FileBrowser(EditorContext& ctx);

    // ========================================================================
    // Initialization
    // ========================================================================

    void open(const std::string& path);
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
    void goToParent();

    // ========================================================================
    // Selection
    // ========================================================================

    // Returns true if a file was opened (should exit browser mode)
    // Returns false if a directory was entered (stay in browser mode)
    bool selectEntry();

    // ========================================================================
    // File Operations
    // ========================================================================

    void toggleHiddenFiles();
    void refresh();
    void createFile();
    void createDirectory();
    void deleteEntry();
    void renameEntry();

    // ========================================================================
    // Drawing
    // ========================================================================

    void draw();
    void drawStatusLine();

    // ========================================================================
    // Accessors
    // ========================================================================

    const std::string& currentDirectory() const
    {
        return m_currentDirectory;
    }
    const std::vector<FileEntry>& entries() const
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
    bool showingHidden() const
    {
        return m_showHidden;
    }

    // ========================================================================
    // State
    // ========================================================================

    bool isActive() const
    {
        return m_active;
    }

private:
    // ========================================================================
    // Internal Methods
    // ========================================================================

    void loadDirectory(const std::string& path);
    void adjustScroll();
    std::string formatFileSize(size_t size) const;
    std::string formatModTime(time_t time) const;

    // ========================================================================
    // State
    // ========================================================================

    EditorContext& m_ctx;

    std::string m_currentDirectory;
    std::string m_previousFile;
    std::vector<FileEntry> m_entries;

    int m_cursor = 0;
    int m_offset = 0;
    bool m_showHidden = false;
    bool m_active = false;
};
