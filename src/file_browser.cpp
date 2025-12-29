#include "editor_context.h"
#include "file_browser.h"
#include "terminal.h"

#include <algorithm>
#include <climits>
#include <dirent.h>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

// ============================================================================
// Constructor
// ============================================================================

FileBrowser::FileBrowser(EditorContext& ctx) : m_ctx(ctx) {}

// ============================================================================
// Initialization
// ============================================================================

void FileBrowser::open(const std::string& path)
{
    m_previousFile = m_ctx.filename();

    char resolvedPath[PATH_MAX];
    if(realpath(path.c_str(), resolvedPath))
    {
        m_currentDirectory = resolvedPath;
    }
    else
    {
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
        {
            m_currentDirectory = cwd;
        }
        else
        {
            m_currentDirectory = ".";
        }
    }

    loadDirectory(m_currentDirectory);

    if(m_entries.empty())
    {
        m_ctx.setStatusMessage("Failed to load directory: " +
                               m_currentDirectory);
        return;
    }

    m_cursor = 0;
    m_offset = 0;
    m_active = true;
    m_ctx.requestFullRedraw();
}

void FileBrowser::close()
{
    m_active = false;
}

// ============================================================================
// Navigation
// ============================================================================

void FileBrowser::moveUp()
{
    if(m_cursor > 0)
    {
        m_cursor--;
        adjustScroll();
    }
    m_ctx.requestFullRedraw();
}

void FileBrowser::moveDown()
{
    if(m_cursor < static_cast<int>(m_entries.size()) - 1)
    {
        m_cursor++;
        adjustScroll();
    }
    m_ctx.requestFullRedraw();
}

void FileBrowser::moveToStart()
{
    m_cursor = 0;
    m_offset = 0;
    m_ctx.requestFullRedraw();
}

void FileBrowser::moveToEnd()
{
    m_cursor = static_cast<int>(m_entries.size()) - 1;
    if(m_cursor < 0)
        m_cursor = 0;
    adjustScroll();
    m_ctx.requestFullRedraw();
}

void FileBrowser::halfPageUp()
{
    int halfPage = m_ctx.screenRows() / 2;
    for(int i = 0; i < halfPage && m_cursor > 0; i++)
    {
        m_cursor--;
    }
    adjustScroll();
    m_ctx.requestFullRedraw();
}

void FileBrowser::halfPageDown()
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

void FileBrowser::goToParent()
{
    size_t lastSlash = m_currentDirectory.rfind('/');
    if(lastSlash != std::string::npos && lastSlash > 0)
    {
        std::string parentDir = m_currentDirectory.substr(0, lastSlash);
        std::string currentName = m_currentDirectory.substr(lastSlash + 1);

        loadDirectory(parentDir);
        m_currentDirectory = parentDir;

        // Try to position cursor on the directory we came from
        for(size_t i = 0; i < m_entries.size(); i++)
        {
            if(m_entries[i].name == currentName)
            {
                m_cursor = static_cast<int>(i);
                break;
            }
        }
        adjustScroll();
    }
    m_ctx.requestFullRedraw();
}

// ============================================================================
// Selection
// ============================================================================

bool FileBrowser::selectEntry()
{
    if(m_entries.empty() || m_cursor >= static_cast<int>(m_entries.size()))
    {
        return false;
    }

    const FileEntry& entry = m_entries[m_cursor];

    if(entry.name == "..")
    {
        goToParent();
        return false;
    }

    if(entry.isDirectory)
    {
        loadDirectory(entry.path);
        m_currentDirectory = entry.path;
        m_cursor = 0;
        m_offset = 0;
        m_ctx.requestFullRedraw();
        return false;
    }
    else
    {
        // Open the file
        m_ctx.openFile(entry.path);
        m_active = false;
        return true;
    }
}

// ============================================================================
// File Operations
// ============================================================================

void FileBrowser::toggleHiddenFiles()
{
    m_showHidden = !m_showHidden;
    loadDirectory(m_currentDirectory);
    m_cursor = 0;
    m_offset = 0;
    m_ctx.setStatusMessage(m_showHidden ? "Showing hidden files"
                                        : "Hiding hidden files");
    m_ctx.requestFullRedraw();
}

void FileBrowser::refresh()
{
    int savedCursor = m_cursor;
    loadDirectory(m_currentDirectory);
    m_cursor = std::min(savedCursor, static_cast<int>(m_entries.size()) - 1);
    if(m_cursor < 0)
        m_cursor = 0;
    adjustScroll();
    m_ctx.setStatusMessage("Refreshed");
    m_ctx.requestFullRedraw();
}

void FileBrowser::createFile()
{
    // TODO: Implement with prompt
    m_ctx.setStatusMessage("Create file: not yet implemented");
}

void FileBrowser::createDirectory()
{
    // TODO: Implement with prompt
    m_ctx.setStatusMessage("Create directory: not yet implemented");
}

void FileBrowser::deleteEntry()
{
    // TODO: Implement with confirmation
    m_ctx.setStatusMessage("Delete: not yet implemented");
}

void FileBrowser::renameEntry()
{
    // TODO: Implement with prompt
    m_ctx.setStatusMessage("Rename: not yet implemented");
}

// ============================================================================
// Drawing
// ============================================================================

void FileBrowser::draw()
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
            const FileEntry& entry = m_entries[entryIndex];

            // Highlight current selection
            if(entryIndex == m_cursor)
            {
                output += "\x1b[7m"; // Reverse video
            }

            // Directory indicator
            if(entry.isDirectory)
            {
                output += "\x1b[34m"; // Blue
                output += "📁 ";
            }
            else
            {
                output += "   ";
            }

            // Name (truncate if needed)
            std::string displayName = entry.name;
            int maxNameLen = m_ctx.screenCols() - 25;
            if(static_cast<int>(displayName.length()) > maxNameLen)
            {
                displayName = displayName.substr(0, maxNameLen - 3) + "...";
            }
            output += displayName;

            // Padding
            int padding = maxNameLen - static_cast<int>(displayName.length());
            if(padding > 0)
            {
                output += std::string(padding, ' ');
            }

            // File size (right-aligned)
            if(!entry.isDirectory)
            {
                output += " " + formatFileSize(entry.size);
            }

            // Reset colors
            output += "\x1b[0m";
        }
    }

    Terminal::write(output);
}

void FileBrowser::drawStatusLine()
{
    std::string status = " FILE BROWSER: " + m_currentDirectory;

    // Entry count
    std::ostringstream oss;
    oss << " [" << (m_cursor + 1) << "/" << m_entries.size() << "]";

    if(m_showHidden)
    {
        oss << " [H]";
    }

    status += oss.str();

    m_ctx.setStatusMessage(status);
}

// ============================================================================
// Internal Methods
// ============================================================================

void FileBrowser::loadDirectory(const std::string& path)
{
    m_entries.clear();

    DIR* dir = opendir(path.c_str());
    if(!dir)
    {
        dir = opendir(".");
        if(!dir)
        {
            m_ctx.setStatusMessage("Cannot open any directory!");
            return;
        }
        m_currentDirectory = ".";
    }

    std::vector<FileEntry> dirs;
    std::vector<FileEntry> files;

    struct dirent* entry;
    while((entry = readdir(dir)))
    {
        std::string name = entry->d_name;

        if(name == ".")
            continue;

        if(!m_showHidden && name != ".." && name[0] == '.')
            continue;

        std::string fullPath = path + "/" + name;

        struct stat st;
        if(stat(fullPath.c_str(), &st) != 0)
            continue;

        FileEntry fe;
        fe.name = name;
        fe.path = fullPath;
        fe.isDirectory = S_ISDIR(st.st_mode);
        fe.size = st.st_size;
        fe.modTime = st.st_mtime;

        if(fe.isDirectory)
        {
            dirs.push_back(fe);
        }
        else
        {
            files.push_back(fe);
        }
    }

    closedir(dir);

    // Sort directories and files separately
    auto sortByName = [](const FileEntry& a, const FileEntry& b)
    {
        // ".." always first
        if(a.name == "..")
            return true;
        if(b.name == "..")
            return false;
        return a.name < b.name;
    };

    std::sort(dirs.begin(), dirs.end(), sortByName);
    std::sort(files.begin(), files.end(), sortByName);

    // Combine: directories first, then files
    m_entries.reserve(dirs.size() + files.size());
    m_entries.insert(m_entries.end(), dirs.begin(), dirs.end());
    m_entries.insert(m_entries.end(), files.begin(), files.end());
}

void FileBrowser::adjustScroll()
{
    int visibleRows = m_ctx.screenRows();

    // Ensure cursor is visible
    if(m_cursor < m_offset)
    {
        m_offset = m_cursor;
    }
    else if(m_cursor >= m_offset + visibleRows)
    {
        m_offset = m_cursor - visibleRows + 1;
    }
}

std::string FileBrowser::formatFileSize(size_t size) const
{
    std::ostringstream oss;

    if(size < 1024)
    {
        oss << size << "B";
    }
    else if(size < 1024 * 1024)
    {
        oss << std::fixed << std::setprecision(1) << (size / 1024.0) << "K";
    }
    else if(size < 1024 * 1024 * 1024)
    {
        oss << std::fixed << std::setprecision(1) << (size / (1024.0 * 1024.0))
            << "M";
    }
    else
    {
        oss << std::fixed << std::setprecision(1)
            << (size / (1024.0 * 1024.0 * 1024.0)) << "G";
    }

    // Right-align to 8 characters
    std::string result = oss.str();
    if(result.length() < 8)
    {
        result = std::string(8 - result.length(), ' ') + result;
    }

    return result;
}

std::string FileBrowser::formatModTime(time_t time) const
{
    char buf[32];
    struct tm* tm = localtime(&time);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
    return buf;
}
