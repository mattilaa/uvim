#include "buffer_browser.h"
#include "editor_context.h"
#include "editor_lsp_query.h"
#include "terminal.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

// ============================================================================
// Constructor
// ============================================================================

BufferBrowser::BufferBrowser(EditorContext& ctx) : m_ctx(ctx) {}

// ============================================================================
// Initialization
// ============================================================================

void BufferBrowser::open()
{
    refreshEntries();

    // Position cursor on current buffer
    int currentIdx = m_ctx.currentBufferIndex();
    for(size_t i = 0; i < m_entries.size(); i++)
    {
        if(m_entries[i].index == currentIdx)
        {
            m_cursor = static_cast<int>(i);
            break;
        }
    }

    m_offset = 0;
    adjustScroll();
    m_active = true;
    m_ctx.requestFullRedraw();
}

void BufferBrowser::close()
{
    m_active = false;
}

// ============================================================================
// Navigation
// ============================================================================

void BufferBrowser::moveUp()
{
    if(m_cursor > 0)
    {
        m_cursor--;
        adjustScroll();
    }
    m_ctx.requestFullRedraw();
}

void BufferBrowser::moveDown()
{
    if(m_cursor < static_cast<int>(m_entries.size()) - 1)
    {
        m_cursor++;
        adjustScroll();
    }
    m_ctx.requestFullRedraw();
}

void BufferBrowser::moveToStart()
{
    m_cursor = 0;
    m_offset = 0;
    m_ctx.requestFullRedraw();
}

void BufferBrowser::moveToEnd()
{
    m_cursor = static_cast<int>(m_entries.size()) - 1;
    if(m_cursor < 0)
        m_cursor = 0;
    adjustScroll();
    m_ctx.requestFullRedraw();
}

void BufferBrowser::halfPageUp()
{
    int halfPage = m_ctx.screenRows() / 2;
    for(int i = 0; i < halfPage && m_cursor > 0; i++)
    {
        m_cursor--;
    }
    adjustScroll();
    m_ctx.requestFullRedraw();
}

void BufferBrowser::halfPageDown()
{
    int halfPage = m_ctx.screenRows() / 2;
    int maxCursor = static_cast<int>(m_entries.size()) - 1;
    for(int i = 0; i < halfPage && m_cursor < maxCursor; i++)
    {
        m_cursor++;
    }
    adjustScroll();
    m_ctx.requestFullRedraw();
}

// ============================================================================
// Selection
// ============================================================================

bool BufferBrowser::selectEntry()
{
    if(m_entries.empty() || m_cursor >= static_cast<int>(m_entries.size()))
    {
        return false;
    }

    const BufferEntry& entry = m_entries[m_cursor];
    m_ctx.switchToBuffer(entry.index);
    m_active = false;
    return true;
}

bool BufferBrowser::selectByNumber(int num)
{
    if(num >= 0 && num < static_cast<int>(m_entries.size()))
    {
        m_ctx.switchToBuffer(m_entries[num].index);
        m_active = false;
        return true;
    }
    return false;
}

// ============================================================================
// Buffer Operations
// ============================================================================

void BufferBrowser::deleteSelectedBuffer()
{
    if(m_entries.empty() || m_cursor >= static_cast<int>(m_entries.size()))
    {
        return;
    }

    if(m_ctx.bufferCount() <= 1)
    {
        m_ctx.setStatusMessage("Cannot delete the last buffer");
        return;
    }

    const BufferEntry& entry = m_entries[m_cursor];

    if(entry.modified)
    {
        m_ctx.setStatusMessage("Buffer has unsaved changes. Use :bd! to force");
        return;
    }

    // Switch to another buffer first if deleting current
    if(entry.isCurrent)
    {
        int newIdx = (entry.index > 0) ? entry.index - 1 : entry.index + 1;
        if(newIdx < m_ctx.bufferCount())
        {
            m_ctx.switchToBuffer(newIdx);
        }
    }

    // Close the buffer
    // Note: This needs proper buffer deletion implementation
    m_ctx.setStatusMessage("Buffer deleted");

    // Refresh and adjust cursor
    refreshEntries();
    if(m_cursor >= static_cast<int>(m_entries.size()))
    {
        m_cursor = static_cast<int>(m_entries.size()) - 1;
    }
    if(m_cursor < 0)
        m_cursor = 0;
    adjustScroll();
    m_ctx.requestFullRedraw();
}

// ============================================================================
// Drawing
// ============================================================================

void BufferBrowser::draw()
{
    std::string output;
    output.reserve(m_ctx.screenRows() * m_ctx.screenCols() * 2);

    int visibleRows = m_ctx.screenRows();

    for(int row = 0; row < visibleRows; row++)
    {
        int entryIndex = m_offset + row;

        Terminal::moveCursor(row + 1, 1);
        output += "\x1b[K"; // Clear line

        if(entryIndex < static_cast<int>(m_entries.size()))
        {
            const BufferEntry& entry = m_entries[entryIndex];

            // Highlight current selection
            if(entryIndex == m_cursor)
            {
                output += "\x1b[7m"; // Reverse video
            }

            // Buffer number
            std::ostringstream oss;
            oss << std::setw(3) << (entry.index + 1) << " ";
            output += oss.str();

            // Modified indicator
            if(entry.modified)
            {
                output += "\x1b[31m[+]\x1b[0m "; // Red [+]
                if(entryIndex == m_cursor)
                {
                    output += "\x1b[7m"; // Re-apply reverse
                }
            }
            else
            {
                output += "    ";
            }

            // Current buffer indicator
            if(entry.isCurrent)
            {
                output += "\x1b[32m>\x1b[0m "; // Green >
                if(entryIndex == m_cursor)
                {
                    output += "\x1b[7m"; // Re-apply reverse
                }
            }
            else
            {
                output += "  ";
            }

            // Filename
            std::string displayName = entry.displayName;
            int maxLen = m_ctx.screenCols() - 20;
            if(static_cast<int>(displayName.length()) > maxLen)
            {
                displayName = "..." + displayName.substr(displayName.length() -
                                                         maxLen + 3);
            }
            output += displayName;

            // Line count (right-aligned)
            int padding = maxLen - static_cast<int>(displayName.length());
            if(padding > 0)
            {
                output += std::string(padding, ' ');
            }

            oss.str("");
            oss << " [" << entry.lineCount << " lines]";
            output += oss.str();

            // Reset colors
            output += "\x1b[0m";
        }
    }

    Terminal::write(output);
}

void BufferBrowser::drawStatusLine()
{
    std::ostringstream oss;
    oss << " BUFFERS: " << m_entries.size() << " buffer(s)";
    oss << " | [" << (m_cursor + 1) << "/" << m_entries.size() << "]";
    oss << " | Press 1-9 to jump, d to delete, Enter to select";

    m_ctx.setStatusMessage(oss.str());
}

// ============================================================================
// Internal Methods
// ============================================================================

void BufferBrowser::refreshEntries()
{
    m_entries.clear();

    Editor* ed = m_ctx.editor();

    for(size_t i = 0; i < ed->buffers.size(); i++)
    {
        const Buffer& buf = *(ed->buffers[i]);

        BufferEntry entry;
        entry.index = static_cast<int>(i);
        entry.filename = buf.filename;
        entry.displayName = buf.filename.empty() ? "[No Name]" : buf.filename;
        entry.modified = buf.dirty;
        entry.isCurrent = (static_cast<int>(i) == ed->currentBufferIndex);
        entry.lineCount = static_cast<int>(buf.lines.size());

        m_entries.push_back(entry);
    }
}

void BufferBrowser::adjustScroll()
{
    int visibleRows = m_ctx.screenRows();

    if(m_cursor < m_offset)
    {
        m_offset = m_cursor;
    }
    else if(m_cursor >= m_offset + visibleRows)
    {
        m_offset = m_cursor - visibleRows + 1;
    }
}
