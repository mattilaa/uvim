#pragma once

#include <memory>
#include <string>
#include <vector>

// Forward declarations
class Editor;
struct Buffer;

// ============================================================================
// EditorContext - Provides controlled access to editor state
// ============================================================================
//
// This class provides a clean interface for mode classes and browser classes
// to interact with the editor without directly accessing Editor internals.
// It acts as a facade/mediator between components and the editor.
//
// ============================================================================

class EditorContext
{
public:
    explicit EditorContext(Editor* editor);

    // ========================================================================
    // Cursor Access
    // ========================================================================

    int& cursorX();
    int& cursorY();
    int& offsetX();
    int& offsetY();
    int& wantedX();

    const int& cursorX() const;
    const int& cursorY() const;

    // ========================================================================
    // Buffer Access
    // ========================================================================

    std::vector<std::string>& lines();
    const std::vector<std::string>& lines() const;

    std::string& filename();
    const std::string& filename() const;

    bool& dirty();
    const bool& dirty() const;

    Buffer* currentBuffer();
    const Buffer* currentBuffer() const;

    // Access to all buffers
    std::vector<std::unique_ptr<Buffer>>& buffers();
    const std::vector<std::unique_ptr<Buffer>>& buffers() const;

    // ========================================================================
    // Screen Dimensions
    // ========================================================================

    int screenRows() const;
    int screenCols() const;

    // ========================================================================
    // Status and Command Buffer
    // ========================================================================

    void setStatusMessage(const std::string& msg);
    std::string& commandBuffer();
    const std::string& commandBuffer() const;

    // ========================================================================
    // Mode Management
    // ========================================================================

    void setMode(int mode);
    int currentMode() const;

    // ========================================================================
    // Redraw Control
    // ========================================================================

    void requestFullRedraw();
    bool& needsFullRedraw();

    // ========================================================================
    // File Operations
    // ========================================================================

    void openFile(const std::string& path);
    void saveFile();
    bool fileExists(const std::string& path) const;

    // ========================================================================
    // Buffer Management
    // ========================================================================

    void createNewBuffer();
    void switchToBuffer(int index);
    void closeCurrentBuffer();
    int bufferCount() const;
    int currentBufferIndex() const;

    // ========================================================================
    // Undo/Redo
    // ========================================================================

    void saveState();
    void undo();
    void redo();

    // ========================================================================
    // Editor Access (for advanced operations)
    // ========================================================================

    Editor* editor()
    {
        return m_editor;
    }
    const Editor* editor() const
    {
        return m_editor;
    }

private:
    Editor* m_editor;
};
