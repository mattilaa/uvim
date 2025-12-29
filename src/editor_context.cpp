#include "editor_context.h"
#include "editor_lsp_query.h"

EditorContext::EditorContext(Editor* editor) : m_editor(editor) {}

// ============================================================================
// Cursor Access
// ============================================================================

int& EditorContext::cursorX()
{
    return *(m_editor->cursorX);
}

int& EditorContext::cursorY()
{
    return *(m_editor->cursorY);
}

int& EditorContext::offsetX()
{
    return *(m_editor->offsetX);
}

int& EditorContext::offsetY()
{
    return *(m_editor->offsetY);
}

int& EditorContext::wantedX()
{
    return *(m_editor->wantedX);
}

const int& EditorContext::cursorX() const
{
    return *(m_editor->cursorX);
}

const int& EditorContext::cursorY() const
{
    return *(m_editor->cursorY);
}

// ============================================================================
// Buffer Access
// ============================================================================

std::vector<std::string>& EditorContext::lines()
{
    return *(m_editor->lines);
}

const std::vector<std::string>& EditorContext::lines() const
{
    return *(m_editor->lines);
}

std::string& EditorContext::filename()
{
    return *(m_editor->filename);
}

const std::string& EditorContext::filename() const
{
    return *(m_editor->filename);
}

bool& EditorContext::dirty()
{
    return *(m_editor->dirty);
}

const bool& EditorContext::dirty() const
{
    return *(m_editor->dirty);
}

Buffer* EditorContext::currentBuffer()
{
    return m_editor->currentBuffer;
}

const Buffer* EditorContext::currentBuffer() const
{
    return m_editor->currentBuffer;
}

std::vector<std::unique_ptr<Buffer>>& EditorContext::buffers()
{
    return m_editor->buffers;
}

const std::vector<std::unique_ptr<Buffer>>& EditorContext::buffers() const
{
    return m_editor->buffers;
}

// ============================================================================
// Screen Dimensions
// ============================================================================

int EditorContext::screenRows() const
{
    return m_editor->screenRows;
}

int EditorContext::screenCols() const
{
    return m_editor->screenCols;
}

// ============================================================================
// Status and Command Buffer
// ============================================================================

void EditorContext::setStatusMessage(const std::string& msg)
{
    m_editor->setStatusMessage(msg);
}

std::string& EditorContext::commandBuffer()
{
    return m_editor->commandBuffer;
}

const std::string& EditorContext::commandBuffer() const
{
    return m_editor->commandBuffer;
}

// ============================================================================
// Mode Management
// ============================================================================

void EditorContext::setMode(int mode)
{
    m_editor->setMode(static_cast<Mode>(mode));
}

int EditorContext::currentMode() const
{
    return static_cast<int>(m_editor->currentMode);
}

// ============================================================================
// Redraw Control
// ============================================================================

void EditorContext::requestFullRedraw()
{
    m_editor->needsFullRedraw = true;
}

bool& EditorContext::needsFullRedraw()
{
    return m_editor->needsFullRedraw;
}

// ============================================================================
// File Operations
// ============================================================================

void EditorContext::openFile(const std::string& path)
{
    m_editor->openFile(path);
}

void EditorContext::saveFile()
{
    m_editor->saveFile();
}

bool EditorContext::fileExists(const std::string& path) const
{
    return m_editor->fileExists(path);
}

// ============================================================================
// Buffer Management
// ============================================================================

void EditorContext::createNewBuffer()
{
    m_editor->createNewBuffer();
}

void EditorContext::switchToBuffer(int index)
{
    m_editor->switchToBuffer(index);
}

void EditorContext::closeCurrentBuffer()
{
    m_editor->closeCurrentBuffer();
}

int EditorContext::bufferCount() const
{
    return static_cast<int>(m_editor->buffers.size());
}

int EditorContext::currentBufferIndex() const
{
    return m_editor->currentBufferIndex;
}

// ============================================================================
// Undo/Redo
// ============================================================================

void EditorContext::saveState()
{
    m_editor->saveState();
}

void EditorContext::undo()
{
    m_editor->undo();
}

void EditorContext::redo()
{
    m_editor->redo();
}
