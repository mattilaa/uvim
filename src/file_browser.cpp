#include "editor.h"
#include "gitignore.h"
#include "terminal.h"
#include <sstream>

#include "file_utils.h"

void Editor::openFileBrowser(const std::string& path)
{
    if(currentMode != FILE_BROWSER && currentBuffer != nullptr)
    {
        previousFile = *filename;
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
    loadDirectory(currentDirectory);

    if(fileList.empty())
    {
        setStatusMessage("Failed to load directory: " + currentDirectory);
        return;
    }

    setMode(FILE_BROWSER);
    browserCursor = 0;
    browserOffset = 0;
    needsFullRedraw = true;
}

void Editor::loadDirectory(const std::string& pathStr)
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
            setStatusMessage("Cannot open any directory!");
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

void Editor::navigateTo(const FileEntry& entry)
{
    if(entry.isDirectory)
    {
        openFileBrowser(entry.path);
    }
    else
    {
        openFile(entry.path);
        setMode(NORMAL);
    }
}

void Editor::toggleHidden()
{
    showHidden = !showHidden;
    loadDirectory(currentDirectory);
    setStatusMessage(showHidden ? "Showing hidden files"
                                : "Hiding hidden files");
}

void Editor::toggleGitignore()
{
    respectGitignore = !respectGitignore;
    fuzzyInitialized = false; // Force re-scan of project files
    loadDirectory(currentDirectory);
    setStatusMessage(respectGitignore ? "Respecting .gitignore"
                                      : "Ignoring .gitignore");
}

std::string Editor::formatFileSize(size_t size)
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

std::string Editor::formatFileTime(time_t time)
{
    char buffer[20];
    struct tm* timeinfo = localtime(&time);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", timeinfo);
    return std::string(buffer);
}

void Editor::drawFileBrowser()
{
    std::string output;
    output.reserve(screenRows * screenCols * 2);

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

    int availableRows = screenRows - 2;

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

        int maxNameLen = screenCols - 30;
        if(displayName.length() > maxNameLen)
        {
            displayName = displayName.substr(0, maxNameLen - 3) + "...";
        }

        output += displayName;

        if(entry.name != "..")
        {
            std::string info = formatFileSize(entry.size) + "  " +
                               formatFileTime(entry.modTime);

            int padding = screenCols - 5 - displayName.length() - info.length();
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
    int padding = screenCols - status.length() - right.length();
    if(padding > 0)
    {
        output.append(padding, ' ');
    }
    output += right;
    output += Terminal::ESC_RESET_ALL;

    output += Terminal::NEWLINE_CLEAR;
    if(!statusMessage.empty())
    {
        output += statusMessage.substr(
            0, std::min((size_t)screenCols, statusMessage.length()));
    }

    Terminal::write(output);
    Terminal::flush();
}

void Editor::handleFileBrowserMode(int c)
{
    switch(c)
    {
    case Terminal::ENTER:
    case 'l':
    case Terminal::ARROW_RIGHT:
        if(browserCursor < fileList.size())
        {
            navigateTo(fileList[browserCursor]);
        }
        break;
    case 'h':
    case Terminal::ARROW_LEFT:
    case '-':
        if(currentDirectory != "/" && currentDirectory != "")
        {
            size_t lastSlash = currentDirectory.find_last_of("/");
            std::string parentDir = "/";
            if(lastSlash != std::string::npos && lastSlash > 0)
            {
                parentDir = currentDirectory.substr(0, lastSlash);
            }
            openFileBrowser(parentDir);
        }
        break;
    case 'j':
    case Terminal::ARROW_DOWN:
        if(browserCursor < fileList.size() - 1)
        {
            browserCursor++;
            if(browserCursor >= browserOffset + screenRows - 2)
            {
                browserOffset = browserCursor - screenRows + 3;
            }
        }
        break;
    case 'k':
    case Terminal::ARROW_UP:
        if(browserCursor > 0)
        {
            browserCursor--;
            if(browserCursor < browserOffset)
            {
                browserOffset = browserCursor;
            }
        }
        break;
    case Terminal::CTRL_P:
        setMode(FUZZY_FIND);
        break;
    case Terminal::CTRL_W:
        setMode(BUFFER_BROWSER);
        break;
    case 'g':
        browserCursor = 0;
        browserOffset = 0;
        break;
    case 'G':
        if(fileList.size() > 0)
        {
            browserCursor = fileList.size() - 1;
            if(fileList.size() > screenRows - 2)
            {
                browserOffset = fileList.size() - screenRows + 2;
            }
        }
        break;
    case '.':
        toggleHidden();
        break;
    case 'i':
    case Terminal::CTRL_I:
        toggleGitignore();
        break;
    case 'R':
        loadDirectory(currentDirectory);
        setStatusMessage("Refreshed");
        break;
    case 'q':
    case Terminal::ESC:
        if(!previousFile.empty())
        {
            openFile(previousFile);
        }
        setMode(NORMAL);
        break;
    case '?':
        setStatusMessage("[Enter/l]:open [h]:parent [j/k]:nav [.]:hidden "
                         "[i]:gitignore [q]:quit");
        break;
    }

    needsFullRedraw = true;
}
