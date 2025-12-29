#include "editor.h"
#include "terminal.h"
#include <algorithm>
#include <dirent.h>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

#include "file_utils.h"

void Editor::openFileBrowser(const std::string& path)
{
    if(currentMode != FILE_BROWSER)
    {
        previousFile = *filename;
    }

    char resolvedPath[PATH_MAX];
    if(realpath(path.c_str(), resolvedPath))
    {
        currentDirectory = resolvedPath;
    }
    else
    {
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
        {
            currentDirectory = cwd;
        }
        else
        {
            currentDirectory = ".";
        }
    }

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

void Editor::loadDirectory(const std::string& path)
{
    fileList.clear();

    DIR* dir = opendir(path.c_str());
    if(!dir)
    {
        dir = opendir(".");
        if(!dir)
        {
            setStatusMessage("Cannot open any directory!");
            return;
        }
        currentDirectory = ".";
    }

    struct dirent* entry;
    std::vector<FileEntry> dirs;
    std::vector<FileEntry> files;

    while((entry = readdir(dir)))
    {
        std::string name = entry->d_name;

        if(name == ".")
            continue;

        if(!showHidden && name != ".." && name[0] == '.')
            continue;

        fs::path fullPath = path + "/" + name;
        std::error_code ec;
        auto st = fs::symlink_status(fullPath, ec);
        // doesn't follow symlinks; use status() if you want to follow
        if(ec)
            continue;

        FileEntry fe;
        fe.name = name;
        fe.path = fullPath;
        fe.isDirectory = fs::is_directory(st);
        fe.size = fs::is_regular_file(st) ? file_size_nothrow(fullPath) : 0;
        fe.modTime = mtime_nothrow(fullPath);

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

    std::sort(dirs.begin(), dirs.end(),
              [](const FileEntry& a, const FileEntry& b)
              {
                  if(a.name == "..")
                      return true;
                  if(b.name == "..")
                      return false;
                  return a.name < b.name;
              });

    std::sort(files.begin(), files.end(),
              [](const FileEntry& a, const FileEntry& b)
              { return a.name < b.name; });

    fileList.insert(fileList.end(), dirs.begin(), dirs.end());
    fileList.insert(fileList.end(), files.begin(), files.end());
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
    output += "  [Enter: open] [q: quit] [.: toggle hidden] [-: parent]";
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

    std::string status = " BROWSE | " + currentDirectory;
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
        setStatusMessage(
            "[Enter/l]:open [h]:parent [j/k]:nav [.]:hidden [q]:quit");
        break;
    }

    needsFullRedraw = true;
}
