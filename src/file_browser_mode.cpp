#include "editor.h"
#include "file_utils.h"
#include "gitignore.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

// ============================================================================
// FileBrowserMode Implementation
// ============================================================================

void FileBrowserMode::on_enter(ModeContext& ctx)
{
    ctx.setStatusMessage("");
    if(!ctx.editor->fileBrowserFuzzy && filterActive)
    {
        filterActive = false;
        filterQuery.clear();
        filterMatches.clear();
    }
    if(previousFile.empty() && ctx.hasCurrentBuffer() && ctx.hasFilename())
    {
        previousFile = std::string(ctx.currentFilename());
    }

    if(currentDirectory.empty())
    {
        currentDirectory = ".";
    }

    if(fileList.empty())
    {
        loadDirectory(ctx, currentDirectory);
    }

    ctx.requestFullRedraw();
}

void FileBrowserMode::on_exit(ModeContext& /* ctx */) {}

std::optional<ModeState> FileBrowserMode::handle(ModeContext& ctx,
                                                 const KeyEvent& event)
{
    int c = event.key;

    std::optional<ModeState> nextState;
    if(commandPrompt.handle(
           ctx, c, [&](std::string_view commandLine)
           { return executeCommand(ctx, commandLine); }, nextState))
    {
        return nextState;
    }

    if(!ctx.editor->fileBrowserFuzzy && filterActive)
    {
        filterActive = false;
        filterQuery.clear();
        filterMatches.clear();
    }

    // ========================================================================
    // Exit
    // ========================================================================

    if(c == Terminal::ESC || c == 'q')
    {
        if(c == Terminal::ESC && filterActive)
        {
            filterActive = false;
            filterQuery.clear();
            filterMatches.clear();
            browserCursor = 0;
            browserOffset = 0;
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c == Terminal::ESC)
            ctx.editor->noteDoubleEscStatusClear();
        if(!previousFile.empty())
        {
            ctx.openFile(std::string_view(previousFile));
        }
        return ctx.hasBuffer() ? ModeState{NormalMode{}}
                               : ModeState{WelcomeMode{}};
    }

    // ========================================================================
    // Navigation
    // ========================================================================

    if(c == 'j' || c == Terminal::ARROW_DOWN || c == Terminal::CTRL_J)
    {
        int count = listSize();
        if(browserCursor < count - 1)
        {
            browserCursor++;
            int visible = ctx.screenRows() - 4;
            if(browserCursor >= browserOffset + visible)
                browserOffset = browserCursor - visible + 1;
        }
    }
    else if(c == 'k' || c == Terminal::ARROW_UP || c == Terminal::CTRL_K)
    {
        if(browserCursor > 0)
        {
            browserCursor--;
            if(browserCursor < browserOffset)
                browserOffset = browserCursor;
        }
    }
    else if(c == 'G')
    {
        int count = listSize();
        browserCursor = std::max(0, count - 1);
        int visible = ctx.screenRows() - 4;
        if(browserCursor >= visible)
            browserOffset = browserCursor - visible + 1;
    }
    else if(c == 'g')
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
        {
            browserCursor = 0;
            browserOffset = 0;
        }
    }
    else if(c == Terminal::CTRL_D)
    {
        int half = (ctx.screenRows() - 4) / 2;
        browserCursor += half;
        int count = listSize();
        if(browserCursor >= count)
            browserCursor = std::max(0, count - 1);
        int visible = ctx.screenRows() - 4;
        if(browserCursor >= browserOffset + visible)
            browserOffset = browserCursor - visible + 1;
    }
    else if(c == Terminal::CTRL_U)
    {
        int half = (ctx.screenRows() - 4) / 2;
        browserCursor -= half;
        if(browserCursor < 0)
            browserCursor = 0;
        if(browserCursor < browserOffset)
            browserOffset = browserCursor;
    }

    // ========================================================================
    // Selection / Enter Directory
    // ========================================================================

    else if(c == Terminal::ENTER || c == 'l' || c == Terminal::ARROW_RIGHT)
    {
        if(browserCursor >= 0 && browserCursor < listSize())
        {
            const FileEntry* entryPtr = entryAt(browserCursor);
            if(!entryPtr)
                return std::nullopt;
            const FileEntry& entry = *entryPtr;
            if(entry.isDirectory ||
               file_utils::is_directory(std::filesystem::path(entry.path)))
            {
                std::string targetPath = entry.path;
                loadDirectory(ctx, targetPath);
                browserCursor = 0;
                browserOffset = 0;
            }
            else
            {
                ctx.openFile(std::string_view(entry.path));
                return ctx.hasBuffer() ? ModeState{NormalMode{}}
                                       : ModeState{WelcomeMode{}};
            }
        }
    }

    // ========================================================================
    // Go Up Directory
    // ========================================================================

    else if(c == 'h' || c == Terminal::ARROW_LEFT || c == '-')
    {
        size_t lastSlash = currentDirectory.find_last_of('/');
        if(lastSlash != std::string::npos && lastSlash > 0)
        {
            std::string parentDir = currentDirectory.substr(0, lastSlash);
            loadDirectory(ctx, parentDir);
            browserCursor = 0;
            browserOffset = 0;
        }
    }

    // ========================================================================
    // File Operations
    // ========================================================================

    // Toggle hidden files
    else if(c == '.')
    {
        showHidden = !showHidden;
        loadDirectory(ctx, currentDirectory);
        ctx.setStatusMessage(showHidden ? "Showing hidden files"
                                        : "Hiding hidden files");
    }
    else if(c == 'i' || c == Terminal::CTRL_I)
    {
        ctx.setRespectGitignore(!ctx.respectGitignore());
        ctx.setFuzzyInitialized(false);
        loadDirectory(ctx, currentDirectory);
        ctx.setStatusMessage(ctx.respectGitignore() ? "Respecting .gitignore"
                                                    : "Ignoring .gitignore");
    }

    // Refresh
    else if(c == 'r' || c == Terminal::CTRL_L)
    {
        loadDirectory(ctx, currentDirectory);
    }

    // Create new file
    else if(c == '%')
    {
        ctx.createNewFilePrompt();
    }

    // Create new directory
    else if(c == 'd')
    {
        ctx.createNewDirectoryPrompt();
    }

    // Delete file/directory
    else if(c == 'D')
    {
        ctx.deleteFilePrompt();
    }

    // Rename file/directory
    else if(c == 'R')
    {
        ctx.renameFilePrompt();
    }

    // ========================================================================
    // Mode Switching
    // ========================================================================

    else if(c == Terminal::CTRL_P || c == 'f')
    {
        return FuzzyFindMode{};
    }
    else if(c == Terminal::CTRL_W || c == 'b')
    {
        return BufferBrowserMode{};
    }
    else if(c == Terminal::CTRL_S || c == '/')
    {
        return GrepSearchMode{};
    }

    if((c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H) &&
       filterActive && ctx.editor->fileBrowserFuzzy)
    {
        if(!filterQuery.empty())
        {
            filterQuery.pop_back();
            if(filterQuery.empty())
            {
                filterActive = false;
                filterMatches.clear();
            }
            updateFilter(ctx);
        }
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c >= 32 && c < 127 && ctx.editor->fileBrowserFuzzy)
    {
        if(std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
           c == '-' || (filterActive && c == '.'))
        {
            filterActive = true;
            filterQuery.push_back(static_cast<char>(c));
            updateFilter(ctx);
            ctx.requestFullRedraw();
            return std::nullopt;
        }
    }

    ctx.requestFullRedraw();
    return std::nullopt;
}

void FileBrowserMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;
    output += editor.theme.reset();

    output += Terminal::ESC_CLEAR_LINE;
    output += Terminal::ESC_BOLD;
    output += "  " + currentDirectory;
    output += editor.theme.reset();
    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    output +=
        "  [Enter: open] [q: quit] [.: hidden] [-: parent] [i: gitignore] "
        "[:cmd]";
    output += editor.theme.baseFg();

    int availableRows = editor.screenRows - 2;

    int count = listSize();
    for(int i = 0; i < availableRows && i + browserOffset < count; i++)
    {
        output += Terminal::NEWLINE_CLEAR;

        int index = i + browserOffset;
        const FileEntry* entryPtr = entryAt(index);
        if(!entryPtr)
            break;
        const FileEntry& entry = *entryPtr;

        if(index == browserCursor)
        {
            output += editor.theme.selection();
        }

        output += "  ";

        if(entry.isDirectory)
        {
            output += editor.theme.uiDirectory();
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

            output += editor.theme.uiDim();
            output += info;
        }

        output += editor.theme.reset();
    }

    int fillerStart = std::max(0, count - browserOffset);
    for(int i = fillerStart; i < availableRows; i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        output += editor.theme.uiGutter();
        output += "~";
        output += editor.theme.baseFg();
    }

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.statusBar();

    std::string status = " BROWSE";
    if(editor.respectGitignore)
        status += " [gi]";
    if(showHidden)
        status += " [H]";
    if(filterActive)
        status += " [/]";
    status += " | " + currentDirectory;
    std::string right =
        " " + std::to_string(std::min(browserCursor + 1, count)) + "/" +
        std::to_string(count) + " ";

    output += status;
    int padding = editor.screenCols - status.length() - right.length();
    if(padding > 0)
    {
        output.append(padding, ' ');
    }
    output += right;
    output += editor.theme.reset();

    output += Terminal::NEWLINE_CLEAR;
    if(commandPrompt.isActive())
    {
        output += editor.theme.baseFg();
        output += ":" + commandPrompt.getInput();
    }
    else if(filterActive)
    {
        output += editor.theme.baseFg();
        output += "/" + filterQuery;
    }
    else if(!editor.statusMessage.empty())
    {
        output += editor.statusMessage.substr(
            0,
            std::min((size_t)editor.screenCols, editor.statusMessage.length()));
    }
    else if(!editor.locMessage.empty())
    {
        output += editor.locMessage.substr(
            0, std::min((size_t)editor.screenCols, editor.locMessage.length()));
    }

    editor.drawCommandHistoryPopup(output);
    editor.drawCommandPopup(output);

    if(commandPrompt.isActive())
    {
        output += Terminal::ESC_SHOW_CURSOR;
        int row = editor.screenRows + 2;
        int col = 2 + (int)commandPrompt.getInput().size();
        output += Terminal::cursorPos(row, col);
    }
    else if(filterActive)
    {
        output += Terminal::ESC_SHOW_CURSOR;
        int row = editor.screenRows + 2;
        int col = 2 + (int)filterQuery.size();
        output += Terminal::cursorPos(row, col);
    }
    else
    {
        output += Terminal::ESC_HIDE_CURSOR;
    }

    Terminal::write(output);
    Terminal::flush();
}

static int fuzzyScore(std::string_view text, std::string_view pattern)
{
    if(pattern.empty())
        return 0;

    auto lower = [](unsigned char ch) -> unsigned char
    {
        if(ch >= 'A' && ch <= 'Z')
            return (unsigned char)(ch - 'A' + 'a');
        return ch;
    };

    int score = 0;
    int ti = 0;
    int consecutive = 0;
    for(int pi = 0; pi < (int)pattern.size(); ++pi)
    {
        unsigned char pc = lower((unsigned char)pattern[pi]);
        bool found = false;
        while(ti < (int)text.size())
        {
            unsigned char tc = (unsigned char)text[ti];
            unsigned char ltc = lower(tc);
            if(ltc == pc)
            {
                score += 10 + consecutive * 5;
                if(ti == 0)
                    score += 8;
                else
                {
                    char prev = (char)text[ti - 1];
                    if(prev == '_' || prev == '-' || prev == ' ' ||
                       prev == '\t' || prev == '.')
                        score += 8;
                }
                ++consecutive;
                ++ti;
                found = true;
                break;
            }
            consecutive = 0;
            ++ti;
        }
        if(!found)
            return -1;
    }
    return score;
}

void FileBrowserMode::loadDirectory(ModeContext& ctx,
                                    const std::string& pathStr)
{
    fileList.clear();

    std::filesystem::path dirPath = pathStr.empty()
                                        ? std::filesystem::path{"."}
                                        : std::filesystem::path{pathStr};

    std::error_code ec;
    if(!std::filesystem::is_directory(dirPath, ec))
    {
        dirPath = ".";
        ec.clear();
        if(!std::filesystem::is_directory(dirPath, ec))
        {
            ctx.setStatusMessage("Cannot open any directory!");
            return;
        }
        currentDirectory = ".";
    }
    else
    {
        currentDirectory = file_utils::path_to_utf8_string(dirPath);
    }

    GitIgnore gitignore;
    if(ctx.respectGitignore())
    {
        gitignore.loadRecursive(dirPath);
    }

    std::vector<FileEntry> dirs;
    std::vector<FileEntry> files;

    const auto push_entry = [&](FileEntry&& fe)
    { (fe.isDirectory ? dirs : files).push_back(std::move(fe)); };

    {
        std::error_code parentEc;
        std::filesystem::path resolved =
            std::filesystem::absolute(dirPath, parentEc);
        if(parentEc || resolved.empty())
        {
            resolved = dirPath;
        }

        std::filesystem::path parentPath = resolved.parent_path();
        if(parentPath.empty())
        {
            parentPath = resolved;
        }

        FileEntry up;
        up.name = "..";
        up.path = file_utils::path_to_utf8_string(parentPath);
        up.isDirectory = true;
        up.size = 0;
        up.modTime = 0;
        dirs.push_back(std::move(up));
    }

    auto opts = std::filesystem::directory_options::skip_permission_denied;

    for(std::filesystem::directory_iterator it{dirPath, opts, ec}, end;
        it != end; it.increment(ec))
    {
        if(ec)
        {
            ec.clear();
            continue;
        }

        const std::filesystem::directory_entry& de = *it;

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

        bool isDir = std::filesystem::is_directory(st);

        if(ctx.respectGitignore() && gitignore.isIgnored(de.path(), isDir))
            continue;

        FileEntry fe;
        fe.name = std::move(name);
        fe.path = file_utils::path_to_utf8_string(de.path());
        fe.isDirectory = isDir;

        if(std::filesystem::is_regular_file(st))
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

    updateFilter(ctx);
}

void FileBrowserMode::updateFilter(ModeContext& ctx)
{
    if(!filterActive || filterQuery.empty())
    {
        filterActive = false;
        filterMatches.clear();
        if(fileList.empty())
        {
            browserCursor = 0;
            browserOffset = 0;
            return;
        }

        if(browserCursor >= (int)fileList.size())
            browserCursor = (int)fileList.size() - 1;
        if(browserCursor < 0)
            browserCursor = 0;

        int visible = std::max(1, ctx.screenRows() - 4);
        if(browserOffset > browserCursor)
            browserOffset = browserCursor;
        int maxOffset = std::max(0, (int)fileList.size() - visible);
        if(browserOffset > maxOffset)
            browserOffset = maxOffset;
        return;
    }

    filterMatches.clear();
    int parentIndex = -1;
    std::vector<std::pair<int, int>> scored;
    scored.reserve(fileList.size());

    for(int i = 0; i < (int)fileList.size(); ++i)
    {
        const auto& entry = fileList[i];
        if(entry.name == "..")
        {
            parentIndex = i;
            continue;
        }

        int score = fuzzyScore(entry.name, filterQuery);
        if(score >= 0)
            scored.emplace_back(i, score);
    }

    std::stable_sort(scored.begin(), scored.end(),
                     [&](const auto& a, const auto& b)
                     {
                         if(a.second != b.second)
                             return a.second > b.second;
                         return fileList[a.first].name <
                                fileList[b.first].name;
                     });

    if(parentIndex >= 0)
        filterMatches.push_back(parentIndex);
    for(const auto& entry : scored)
        filterMatches.push_back(entry.first);

    int count = listSize();
    browserCursor = std::min(browserCursor, std::max(0, count - 1));
    browserOffset = std::min(browserOffset, std::max(0, count - 1));
    int visible = std::max(1, ctx.screenRows() - 4);
    if(browserCursor < browserOffset)
        browserOffset = browserCursor;
    if(browserCursor >= browserOffset + visible)
        browserOffset = std::max(0, browserCursor - visible + 1);
}

int FileBrowserMode::listSize() const
{
    return filterActive ? (int)filterMatches.size() : (int)fileList.size();
}

const FileEntry* FileBrowserMode::entryAt(int index) const
{
    if(filterActive)
    {
        if(index < 0 || index >= (int)filterMatches.size())
            return nullptr;
        int mapped = filterMatches[index];
        if(mapped < 0 || mapped >= (int)fileList.size())
            return nullptr;
        return &fileList[mapped];
    }
    if(index < 0 || index >= (int)fileList.size())
        return nullptr;
    return &fileList[index];
}

std::string FileBrowserMode::formatFileSize(size_t size) const
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

std::string FileBrowserMode::formatFileTime(time_t time) const
{
    char buffer[20];
    struct tm* timeinfo = localtime(&time);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", timeinfo);
    return std::string(buffer);
}

std::optional<ModeState>
FileBrowserMode::executeCommand(ModeContext& ctx, std::string_view commandLine)
{
    return dispatchCommandLine(
        ctx, commandLine,
        [&](ModeContext& ctx, const ParsedCommand& command,
            std::optional<ModeState>& nextState) -> bool
        {
            const std::string& cmd = command.cmd;
            const std::string& args = command.args;

            // =================================================================
            // Quit commands - exit file browser mode
            // =================================================================
            if(cmd == "q" || cmd == "q!")
            {
                if(!previousFile.empty())
                {
                    ctx.openFile(std::string_view(previousFile));
                }
                nextState = ctx.hasBuffer() ? ModeState{NormalMode{}}
                                            : ModeState{WelcomeMode{}};
                return true;
            }

            if(cmd == "wq" || cmd == "x")
            {
                ctx.setStatusMessage("Not applicable in file browser mode");
                return true;
            }

            // Quit all commands - delegate to editor to quit entire application
            if(cmd == "qa" || cmd == "qall" || cmd == "qa!" || cmd == "qall!" ||
               cmd == "wqa" || cmd == "wqall" || cmd == "xa")
            {
                ctx.executeCommand(cmd);
                return true;
            }

            // Get current file entry if one is selected
            const FileEntry* currentEntry = entryAt(browserCursor);

            // =================================================================
            // Delete command
            // =================================================================
            if(cmd == "delete" || cmd == "d" || cmd == "rm")
            {
                if(!currentEntry || currentEntry->name == "..")
                {
                    ctx.setStatusMessage("No file selected to delete");
                    return true;
                }

                // Reuse existing delete logic
                ctx.deleteFilePrompt();
                return true;
            }

            // =================================================================
            // Rename/Move command
            // =================================================================
            if(cmd == "rename" || cmd == "r" || cmd == "mv")
            {
                if(args.empty())
                {
                    if(!currentEntry || currentEntry->name == "..")
                    {
                        ctx.setStatusMessage("No file selected to rename");
                        return true;
                    }
                    // Reuse existing rename prompt
                    ctx.renameFilePrompt();
                }
                else
                {
                    // Rename with provided name
                    if(!currentEntry || currentEntry->name == "..")
                    {
                        ctx.setStatusMessage("No file selected to rename");
                        return true;
                    }

                    std::filesystem::path oldPath(currentEntry->path);
                    std::filesystem::path newPath =
                        oldPath.parent_path() / std::filesystem::path(args);

                    std::error_code ec;
                    std::filesystem::rename(oldPath, newPath, ec);
                    if(ec)
                    {
                        ctx.setStatusMessage("Failed to rename: " +
                                             ec.message());
                    }
                    else
                    {
                        ctx.setStatusMessage("Renamed to: " + args);
                        loadDirectory(ctx, currentDirectory);
                    }
                }
                return true;
            }

            // =================================================================
            // Make directory command
            // =================================================================
            if(cmd == "mkdir" || cmd == "md")
            {
                if(args.empty())
                {
                    ctx.createNewDirectoryPrompt();
                }
                else
                {
                    std::filesystem::path dirPath =
                        std::filesystem::path(currentDirectory) /
                        std::filesystem::path(args);
                    std::error_code ec;
                    std::filesystem::create_directory(dirPath, ec);
                    if(ec)
                    {
                        ctx.setStatusMessage("Failed to create directory: " +
                                             ec.message());
                    }
                    else
                    {
                        ctx.setStatusMessage("Created directory: " + args);
                        loadDirectory(ctx, currentDirectory);
                    }
                }
                return true;
            }

            // =================================================================
            // Create file command
            // =================================================================
            if(cmd == "touch" || cmd == "new")
            {
                if(args.empty())
                {
                    ctx.createNewFilePrompt();
                }
                else
                {
                    std::filesystem::path filePath =
                        std::filesystem::path(currentDirectory) /
                        std::filesystem::path(args);
                    std::ofstream file(filePath);
                    if(!file.is_open())
                    {
                        ctx.setStatusMessage("Failed to create file: " + args);
                    }
                    else
                    {
                        file.close();
                        ctx.setStatusMessage("Created file: " + args);
                        loadDirectory(ctx, currentDirectory);
                    }
                }
                return true;
            }

            // =================================================================
            // Change directory command
            // =================================================================
            if(cmd == "cd")
            {
                if(args.empty())
                {
                    ctx.setStatusMessage("Usage: :cd <path>");
                }
                else
                {
                    std::filesystem::path targetPath;
                    if(args[0] == '/' || args[0] == '~')
                    {
                        // Absolute path
                        if(args[0] == '~')
                        {
                            const char* home = getenv("HOME");
                            if(home)
                            {
                                targetPath = std::filesystem::path(home);
                                if(args.length() > 1 && args[1] == '/')
                                {
                                    targetPath /= args.substr(2);
                                }
                            }
                            else
                            {
                                ctx.setStatusMessage(
                                    "HOME environment variable not set");
                                return true;
                            }
                        }
                        else
                        {
                            targetPath = std::filesystem::path(args);
                        }
                    }
                    else
                    {
                        // Relative path
                        targetPath = std::filesystem::path(currentDirectory) /
                                     std::filesystem::path(args);
                    }

                    std::error_code ec;
                    if(std::filesystem::is_directory(targetPath, ec) && !ec)
                    {
                        loadDirectory(
                            ctx, file_utils::path_to_utf8_string(targetPath));
                        browserCursor = 0;
                        browserOffset = 0;
                    }
                    else
                    {
                        ctx.setStatusMessage("Not a directory: " + args);
                    }
                }
                return true;
            }

            // =================================================================
            // Help command
            // =================================================================
            if(cmd == "help" || cmd == "h")
            {
                nextState = HelpMode{args, previousFile};
                return true;
            }
            if(cmd == "?")
            {
                ctx.setStatusMessage(
                    ":q :help <topic> :d[elete] :r[ename] <name> "
                    ":mkdir <name> :touch <name> :cd "
                    "<path>");
                return true;
            }

            return false;
        },
        [&](ModeContext& ctx,
            std::string_view line) -> std::optional<ModeState>
        {
            auto next = dispatchEditorCommand(ctx, line, previousFile, false);
            if(next && std::holds_alternative<LocListMode>(*next))
            {
                LocListMode loc = std::get<LocListMode>(*next);
                loc.returnMode = FILE_BROWSER;
                loc.returnBrowseCursor = browserCursor;
                loc.returnBrowseOffset = browserOffset;
                loc.returnBrowseDirectory = currentDirectory;
                return std::optional<ModeState>(ModeState{loc});
            }
            return next;
        });
}
