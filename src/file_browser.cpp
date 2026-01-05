#include "file_browser.h"
#include "editor.h"
#include "gitignore.h"
#include "terminal.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

#include "file_utils.h"

void Editor::openFileBrowser(const std::string& path)
{
    fileBrowser.open(*this, path);
    setMode(FILE_BROWSER);
}

void FileBrowser::open(Editor& editor, const std::string& path)
{
    if(editor.buffers.empty())
    {
        editor.createNewBuffer();
    }

    if(editor.currentMode != FILE_BROWSER && editor.currentBuffer != nullptr)
    {
        previousFile = *editor.filename;
    }

    std::error_code ec;

    // Prefer resolving the provided path (works for relative paths too)
    fs::path p = path.empty() ? fs::path{"."} : fs::path{path};

    // weakly_canonical: resolves what it can without failing hard if parts
    // don't exist
    fs::path resolved = fs::weakly_canonical(p, ec);
    if(ec)
    {
        ec.clear();
        resolved = fs::current_path(ec);
        if(ec)
            resolved = ".";
    }

    currentDirectory = resolved.string(); // or UTF-8 helper if you use one
    loadDirectory(editor, currentDirectory);

    if(fileList.empty())
    {
        editor.setStatusMessage("Failed to load directory: " +
                                currentDirectory);
    }

    browserCursor = 0;
    browserOffset = 0;
    editor.needsFullRedraw = true;
}

void FileBrowser::setDirectory(Editor& editor, const std::string& path)
{
    currentDirectory = path;
    loadDirectory(editor, currentDirectory);
    browserCursor = 0;
    browserOffset = 0;
    editor.needsFullRedraw = true;
}

void FileBrowser::loadDirectory(Editor& editor, const std::string& pathStr)
{
    fileList.clear();

    fs::path dirPath = pathStr.empty() ? fs::path{"."} : fs::path{pathStr};

    std::error_code ec;
    if(!fs::is_directory(dirPath, ec))
    {
        dirPath = ".";
        ec.clear();
        if(!fs::is_directory(dirPath, ec))
        {
            editor.setStatusMessage("Cannot open any directory!");
            return;
        }
        currentDirectory = ".";
    }
    else
    {
        currentDirectory = file_utils::path_to_utf8_string(dirPath);
    }

    // Load gitignore patterns
    GitIgnore gitignore;
    if(respectGitignore)
    {
        gitignore.loadRecursive(dirPath);
    }

    std::vector<FileEntry> dirs;
    std::vector<FileEntry> files;

    const auto push_entry = [&](FileEntry&& fe)
    { (fe.isDirectory ? dirs : files).push_back(std::move(fe)); };

    // Optional: add ".." explicitly on top (most file browsers do)
    if(dirPath.has_parent_path())
    {
        FileEntry up;
        up.name = "..";
        up.path = file_utils::path_to_utf8_string(dirPath.parent_path());
        up.isDirectory = true;
        up.size = 0;
        up.modTime = 0;
        dirs.push_back(std::move(up));
    }

    fs::directory_options opts = fs::directory_options::skip_permission_denied;

    for(fs::directory_iterator it{dirPath, opts, ec}, end; it != end;
        it.increment(ec))
    {
        if(ec)
        {
            ec.clear();
            continue;
        }

        const fs::directory_entry& de = *it;

        // Name as UTF-8 std::string
        std::string name =
            file_utils::path_to_utf8_string(de.path().filename());

        if(name == ".")
            continue;

        if(!showHidden && name != ".." && file_utils::is_hidden_name(name))
            continue;

        std::error_code ec2;
        auto st = file_utils::status_with_policy(de.path(), ec2);
        if(ec2)
            continue;

        bool isDir = fs::is_directory(st);

        // Check gitignore (skip .git and ignored files)
        if(respectGitignore && gitignore.isIgnored(de.path(), isDir))
            continue;

        FileEntry fe;
        fe.name = std::move(name);
        fe.path = file_utils::path_to_utf8_string(de.path());
        fe.isDirectory = isDir;

        if(fs::is_regular_file(st))
        {
            fe.size = file_utils::file_size_to_size_t(de.path());
        }
        else
        {
            fe.size = 0;
        }

        fe.modTime = file_utils::mtime_nothrow(de.path());

        push_entry(std::move(fe));
    }

    auto dirCmp = [](const FileEntry& a, const FileEntry& b)
    {
        if(a.name == "..")
            return true;
        if(b.name == "..")
            return false;
        return a.name < b.name;
    };
    auto nameCmp = [](const FileEntry& a, const FileEntry& b)
    { return a.name < b.name; };

    std::sort(dirs.begin(), dirs.end(), dirCmp);
    std::sort(files.begin(), files.end(), nameCmp);

    fileList.reserve(dirs.size() + files.size());
    fileList.insert(fileList.end(), std::make_move_iterator(dirs.begin()),
                    std::make_move_iterator(dirs.end()));
    fileList.insert(fileList.end(), std::make_move_iterator(files.begin()),
                    std::make_move_iterator(files.end()));
}

void FileBrowser::navigateTo(Editor& editor, const FileEntry& entry)
{
    if(entry.isDirectory)
    {
        open(editor, entry.path);
    }
    else
    {
        editor.openFile(entry.path);
    }
}

void FileBrowser::toggleHidden(Editor& editor)
{
    showHidden = !showHidden;
    loadDirectory(editor, currentDirectory);
    editor.setStatusMessage(showHidden ? "Showing hidden files"
                                       : "Hiding hidden files");
}

void FileBrowser::toggleGitignore(Editor& editor)
{
    respectGitignore = !respectGitignore;
    editor.fuzzyInitialized = false; // Force re-scan of project files
    loadDirectory(editor, currentDirectory);
    editor.setStatusMessage(respectGitignore ? "Respecting .gitignore"
                                             : "Ignoring .gitignore");
}

std::string FileBrowser::formatFileSize(size_t size) const
{
    const char* units[] = {"B", "K", "M", "G", "T"};
    int unitIndex = 0;
    double displaySize = size;

    while(displaySize >= 1024 && unitIndex < 4)
    {
        displaySize /= 1024;
        unitIndex++;
    }

    std::stringstream ss;
    if(unitIndex == 0)
    {
        ss << std::setw(5) << size << units[unitIndex];
    }
    else
    {
        ss << std::fixed << std::setprecision(1) << std::setw(5) << displaySize
           << units[unitIndex];
    }

    return ss.str();
}

std::string FileBrowser::formatFileTime(time_t time) const
{
    char buffer[20];
    struct tm* timeinfo = localtime(&time);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", timeinfo);
    return std::string(buffer);
}

void FileBrowser::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;

    output += Terminal::ESC_CLEAR_LINE;
    output += Terminal::ESC_BOLD;
    output += "  " + currentDirectory;
    output += Terminal::ESC_RESET_ALL;
    output += Terminal::NEWLINE_CLEAR;
    output += Terminal::FG_BRIGHT_BLACK;
    output +=
        "  [Enter: open] [q: quit] [.: hidden] [-: parent] [i: gitignore]";
    output += Terminal::FG_DEFAULT;

    int availableRows = editor.screenRows - 2;

    for(int i = 0; i < availableRows && i + browserOffset < fileList.size();
        i++)
    {
        output += Terminal::NEWLINE_CLEAR;

        int index = i + browserOffset;
        const FileEntry& entry = fileList[index];

        if(index == browserCursor)
        {
            output += Terminal::STYLE_SELECTION;
        }

        output += "  ";

        if(entry.isDirectory)
        {
            output += Terminal::FG_BLUE;
            output += "📁 ";
            output += Terminal::ESC_BOLD;
        }
        else
        {
            output += "📄 ";
        }

        std::string displayName = entry.name;
        if(entry.isDirectory && entry.name != "..")
        {
            displayName += "/";
        }

        int maxNameLen = editor.screenCols - 30;
        if(displayName.length() > maxNameLen)
        {
            displayName = displayName.substr(0, maxNameLen - 3) + "...";
        }

        output += displayName;

        if(entry.name != "..")
        {
            std::string info = formatFileSize(entry.size) + "  " +
                               formatFileTime(entry.modTime);

            int padding =
                editor.screenCols - 5 - displayName.length() - info.length();
            if(padding > 0)
            {
                output.append(padding, ' ');
            }

            output += Terminal::FG_BRIGHT_BLACK;
            output += info;
        }

        output += Terminal::ESC_RESET_ALL;
    }

    for(int i = fileList.size() - browserOffset; i < availableRows; i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        output += Terminal::FG_BLUE;
        output += "~";
        output += Terminal::FG_DEFAULT;
    }

    output += Terminal::NEWLINE_CLEAR;
    output += Terminal::STYLE_SELECTION;

    std::string status = " BROWSE";
    if(respectGitignore)
        status += " [gi]";
    if(showHidden)
        status += " [H]";
    status += " | " + currentDirectory;
    std::string right = " " + std::to_string(browserCursor + 1) + "/" +
                        std::to_string(fileList.size()) + " ";

    output += status;
    int padding = editor.screenCols - status.length() - right.length();
    if(padding > 0)
    {
        output.append(padding, ' ');
    }
    output += right;
    output += Terminal::ESC_RESET_ALL;

    output += Terminal::NEWLINE_CLEAR;
    if(!editor.statusMessage.empty())
    {
        output += editor.statusMessage.substr(
            0, std::min((size_t)editor.screenCols,
                        editor.statusMessage.length()));
    }

    Terminal::write(output);
    Terminal::flush();
}

void FileBrowser::up(int screenRows)
{
    if(browserCursor > 0)
    {
        browserCursor--;
        if(browserCursor < browserOffset)
            browserOffset = browserCursor;
    }
}

void FileBrowser::down(int screenRows)
{
    if(browserCursor < (int)fileList.size() - 1)
    {
        browserCursor++;
        int visible = screenRows - 4;
        if(browserCursor >= browserOffset + visible)
            browserOffset = browserCursor - visible + 1;
    }
}

void FileBrowser::start()
{
    browserCursor = 0;
    browserOffset = 0;
}

void FileBrowser::end(int screenRows)
{
    browserCursor = fileList.size() - 1;
    int visible = screenRows - 4;
    if(browserCursor >= visible)
        browserOffset = browserCursor - visible + 1;
}

void FileBrowser::halfPageUp(int screenRows)
{
    int half = (screenRows - 4) / 2;
    browserCursor -= half;
    if(browserCursor < 0)
        browserCursor = 0;
    if(browserCursor < browserOffset)
        browserOffset = browserCursor;
}

void FileBrowser::halfPageDown(int screenRows)
{
    int half = (screenRows - 4) / 2;
    browserCursor += half;
    if(browserCursor >= (int)fileList.size())
        browserCursor = fileList.size() - 1;
    int visible = screenRows - 4;
    if(browserCursor >= browserOffset + visible)
        browserOffset = browserCursor - visible + 1;
}

void FileBrowser::parent(Editor& editor)
{
    size_t lastSlash = currentDirectory.find_last_of('/');
    if(lastSlash != std::string::npos && lastSlash > 0)
    {
        std::string parentDir = currentDirectory.substr(0, lastSlash);
        loadDirectory(editor, parentDir);
        browserCursor = 0;
        browserOffset = 0;
        editor.needsFullRedraw = true;
    }
}

bool FileBrowser::selectEntry(Editor& editor)
{
    if(browserCursor >= 0 && browserCursor < (int)fileList.size())
    {
        const FileEntry& entry = fileList[browserCursor];
        navigateTo(editor, entry);
        return !entry.isDirectory;
    }
    return false;
}

void FileBrowser::refresh(Editor& editor)
{
    loadDirectory(editor, currentDirectory);
    editor.needsFullRedraw = true;
}

const std::string& FileBrowser::directory() const
{
    return currentDirectory;
}

bool FileBrowser::isRespectGitignore() const
{
    return respectGitignore;
}

bool FileBrowser::isShowHidden() const
{
    return showHidden;
}

bool FileBrowser::hasEntries() const
{
    return !fileList.empty();
}

void FileBrowser::restorePrevious(Editor& editor) const
{
    if(!previousFile.empty())
    {
        editor.openFile(previousFile);
    }
}
