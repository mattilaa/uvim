#include "ascii.h"
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
#include <regex>
#include <sstream>
#include <unistd.h>
#include <vector>

// ============================================================================
// FileBrowserMode Implementation
// ============================================================================

void FileBrowserMode::on_enter(ModeContext& ctx)
{
    commandPrompt = ctx.commandPrompt();
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
                                                 int key)
{
    int c = keyCode(key);

    auto clearSearchState = [&]()
    {
        searchMatches.clear();
        lastSearchPattern.clear();
        lastSearchPrefix = 0;
        currentSearchMatch = -1;
        ctx.setStatusMessage("");
    };

    const auto moveToVisibleCursor = [&]()
    {
        int visible = std::max(1, ctx.screenRows() - 4);
        if(browserCursor < browserOffset)
            browserOffset = browserCursor;
        if(browserCursor >= browserOffset + visible)
            browserOffset = browserCursor - visible + 1;
    };

    const auto collectRegexMatches =
        [&](char prefix, const std::string& pattern) -> std::vector<int>
    {
        (void)prefix;
        std::vector<int> matches;
        std::regex re(pattern);
        for(int i = 0; i < static_cast<int>(fileList.size()); ++i)
        {
            const auto& entry = fileList[i];
            if(entry.name == "..")
                continue;
            if(std::regex_search(entry.name, re))
                matches.push_back(i);
        }
        return matches;
    };

    auto resetSearchTabCompletion = [&]()
    {
        searchTabCandidates.clear();
        searchTabSeed.clear();
        searchTabIndex = -1;
    };

    auto searchTabComplete = [&](bool reverse) -> bool
    {
        if(!commandPrompt || !commandPrompt->isActive())
            return false;
        const std::string& input = commandPrompt->getInput();
        if(input.empty() || (input[0] != keyCode(command::CommandKey::KEY_SLASH) && input[0] != keyCode(command::CommandKey::KEY_QUESTION)))
            return false;

        const char prefix = input[0];
        std::string currentPattern = input.substr(1);
        std::string expectedInput;
        if(searchTabIndex >= 0 &&
           searchTabIndex < static_cast<int>(searchTabCandidates.size()))
        {
            expectedInput =
                std::string(1, prefix) + searchTabCandidates[searchTabIndex];
        }

        bool needRebuild = searchTabCandidates.empty() ||
                           expectedInput.empty() || input != expectedInput;
        if(needRebuild)
        {
            searchTabSeed = currentPattern;
            searchTabCandidates.clear();
            searchTabIndex = -1;

            auto lower = [](char ch) -> char
            { return static_cast<char>(std::tolower((unsigned char)ch)); };
            std::string needle = searchTabSeed;
            std::transform(needle.begin(), needle.end(), needle.begin(), lower);

            for(const auto& entry : fileList)
            {
                if(entry.name == "..")
                    continue;
                std::string name = entry.name;
                std::string folded = name;
                std::transform(folded.begin(), folded.end(), folded.begin(),
                               lower);
                if(needle.empty() || folded.find(needle) != std::string::npos)
                {
                    searchTabCandidates.push_back(std::move(name));
                }
            }
            std::sort(searchTabCandidates.begin(), searchTabCandidates.end());
            searchTabCandidates.erase(std::unique(searchTabCandidates.begin(),
                                                  searchTabCandidates.end()),
                                      searchTabCandidates.end());
        }

        if(searchTabCandidates.empty())
            return true;

        int count = static_cast<int>(searchTabCandidates.size());
        if(searchTabIndex < 0 || searchTabIndex >= count)
        {
            searchTabIndex = reverse ? count - 1 : 0;
        }
        else
        {
            searchTabIndex = reverse ? (searchTabIndex - 1 + count) % count
                                     : (searchTabIndex + 1) % count;
        }

        commandPrompt->setInput(std::string(1, prefix) +
                                searchTabCandidates[searchTabIndex]);
        ctx.requestFullRedraw();
        return true;
    };

    if(commandPrompt && commandPrompt->isActive())
    {
        const std::string& input = commandPrompt->getInput();
        bool promptSearch =
            !input.empty() && (input[0] == keyCode(command::CommandKey::KEY_SLASH) || input[0] == keyCode(command::CommandKey::KEY_QUESTION));

        if(promptSearch &&
           (c == keyCode(control::ControlKey::CTRL_J) || c == keyCode(control::ControlKey::CTRL_K) ||
            c == keyCode(navigation::NavigationKey::ARROW_DOWN) || c == keyCode(navigation::NavigationKey::ARROW_UP)))
        {
            char prefix = input[0];
            std::string pattern = input.substr(1);
            if(pattern.empty())
            {
                ctx.setStatusMessage("Usage: :/ <regex>");
                ctx.requestFullRedraw();
                return std::nullopt;
            }

            std::vector<int> matches;
            try
            {
                matches = collectRegexMatches(prefix, pattern);
            }
            catch(const std::regex_error&)
            {
                ctx.setStatusMessage("Invalid regex: " + pattern);
                ctx.requestFullRedraw();
                return std::nullopt;
            }

            if(matches.empty())
            {
                ctx.setStatusMessage("No match for regex: " + pattern);
                ctx.requestFullRedraw();
                return std::nullopt;
            }

            bool sameSearch =
                (lastSearchPattern == pattern && lastSearchPrefix == prefix &&
                 !searchMatches.empty());
            searchMatches = std::move(matches);
            lastSearchPattern = pattern;
            lastSearchPrefix = prefix;

            if(!sameSearch || currentSearchMatch < 0 ||
               currentSearchMatch >= static_cast<int>(searchMatches.size()))
            {
                currentSearchMatch = 0;
            }
            else
            {
                int count = static_cast<int>(searchMatches.size());
                bool down =
                    (c == keyCode(control::ControlKey::CTRL_J) || c == keyCode(navigation::NavigationKey::ARROW_DOWN));
                currentSearchMatch =
                    down ? (currentSearchMatch + 1) % count
                         : (currentSearchMatch - 1 + count) % count;
            }

            browserCursor = searchMatches[currentSearchMatch];
            moveToVisibleCursor();
            ctx.setStatusMessage("");
            ctx.requestFullRedraw();
            return std::nullopt;
        }

        if(promptSearch && c == keyCode(control::ControlKey::ESC))
        {
            std::optional<ModeState> nextState;
            if(commandPrompt)
            {
                (void)commandPrompt->handle(
                ctx, c, [&](std::string_view commandLine)
                { return executeCommand(ctx, commandLine); }, nextState);
            }
            clearSearchState();
            ctx.requestFullRedraw();
            return std::nullopt;
        }
    }

    if(commandPrompt && commandPrompt->isActive() && c != keyCode(control::ControlKey::TAB) &&
       c != keyCode(control::ControlKey::SHIFT_TAB))
    {
        resetSearchTabCompletion();
    }
    if(c == keyCode(control::ControlKey::TAB) && searchTabComplete(false))
        return std::nullopt;
    if(c == keyCode(control::ControlKey::SHIFT_TAB) && searchTabComplete(true))
        return std::nullopt;

    const auto syncPromptSearchToFirstMatch = [&]()
    {
        if(!commandPrompt || !commandPrompt->isActive())
            return;
        const std::string& input = commandPrompt->getInput();
        if(input.empty() || (input[0] != keyCode(command::CommandKey::KEY_SLASH) && input[0] != keyCode(command::CommandKey::KEY_QUESTION)))
            return;

        const char prefix = input[0];
        const std::string pattern = input.substr(1);
        if(pattern.empty())
        {
            clearSearchState();
            return;
        }

        std::vector<int> matches;
        try
        {
            matches = collectRegexMatches(prefix, pattern);
        }
        catch(const std::regex_error&)
        {
            clearSearchState();
            return;
        }
        if(matches.empty())
        {
            clearSearchState();
            return;
        }

        searchMatches = std::move(matches);
        lastSearchPattern = pattern;
        lastSearchPrefix = prefix;
        currentSearchMatch = 0;
        browserCursor = searchMatches[currentSearchMatch];
        moveToVisibleCursor();
    };

    std::optional<ModeState> nextState;
    if(commandPrompt && commandPrompt->handle(
           ctx, c, [&](std::string_view commandLine)
           { return executeCommand(ctx, commandLine); }, nextState))
    {
        bool stillBrowsing = true;
        if(ctx.editor && ctx.editor->getModeStateMachine())
        {
            const ModeState& state = ctx.editor->getModeStateMachine()->state();
            stillBrowsing = std::holds_alternative<FileBrowserMode>(state);
        }
        if(stillBrowsing)
            syncPromptSearchToFirstMatch();
        return nextState;
    }

    // Shortcut: start command prompt prefilled with local regex search.
    if((!commandPrompt || !commandPrompt->isActive()) &&
       (c == keyCode(command::CommandKey::KEY_SLASH) || c == keyCode(command::CommandKey::KEY_QUESTION)))
    {
        if(commandPrompt && commandPrompt->handle(
               ctx, keyCode(command::CommandKey::KEY_COLON), [&](std::string_view commandLine)
               { return executeCommand(ctx, commandLine); }, nextState))
        {
            if(commandPrompt && commandPrompt->handle(
                   ctx, c, [&](std::string_view commandLine)
                   { return executeCommand(ctx, commandLine); }, nextState))
            {
                return nextState;
            }
        }
        return std::nullopt;
    }

    if(!ctx.editor->fileBrowserFuzzy && filterActive)
    {
        filterActive = false;
        filterQuery.clear();
        filterMatches.clear();
    }

    if(c == keyCode(control::ControlKey::ESC) && !searchMatches.empty())
    {
        clearSearchState();
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_N) || c == keyCode(typed::TypedKey::KEY_CAP_N))
    {
        if(searchMatches.empty())
        {
            ctx.setStatusMessage("No active search");
            ctx.requestFullRedraw();
            return std::nullopt;
        }

        bool forward = (c == keyCode(typed::TypedKey::KEY_N));
        if(currentSearchMatch < 0 ||
           currentSearchMatch >= static_cast<int>(searchMatches.size()))
        {
            currentSearchMatch = 0;
        }
        else
        {
            int count = static_cast<int>(searchMatches.size());
            if(forward)
                currentSearchMatch = (currentSearchMatch + 1) % count;
            else
                currentSearchMatch = (currentSearchMatch - 1 + count) % count;
        }

        browserCursor = searchMatches[currentSearchMatch];
        moveToVisibleCursor();

        ctx.requestFullRedraw();
        return std::nullopt;
    }

    // ========================================================================
    // Exit
    // ========================================================================

    if(c == keyCode(control::ControlKey::ESC) || c == keyCode(typed::TypedKey::KEY_Q))
    {
        if(c == keyCode(control::ControlKey::ESC) && filterActive)
        {
            filterActive = false;
            filterQuery.clear();
            filterMatches.clear();
            browserCursor = 0;
            browserOffset = 0;
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ESC))
            ctx.editor->noteDoubleEscStatusClear();
        if(!previousFile.empty())
        {
            ctx.openFile(std::string_view(previousFile));
        }
        if(ctx.hasBuffer())
            return ModeState{NormalMode{}};
        ctx.forceQuit();
        return std::nullopt;
    }

    // ========================================================================
    // Navigation
    // ========================================================================

    if(c == keyCode(typed::TypedKey::KEY_J) || c == keyCode(navigation::NavigationKey::ARROW_DOWN) || c == keyCode(control::ControlKey::CTRL_J))
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
    else if(c == keyCode(typed::TypedKey::KEY_K) || c == keyCode(navigation::NavigationKey::ARROW_UP) || c == keyCode(control::ControlKey::CTRL_K))
    {
        if(browserCursor > 0)
        {
            browserCursor--;
            if(browserCursor < browserOffset)
                browserOffset = browserCursor;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_CAP_G))
    {
        int count = listSize();
        browserCursor = std::max(0, count - 1);
        int visible = ctx.screenRows() - 4;
        if(browserCursor >= visible)
            browserOffset = browserCursor - visible + 1;
    }
    else if(c == keyCode(typed::TypedKey::KEY_G))
    {
        int nextChar = Terminal::readKey();
        if(nextChar == keyCode(typed::TypedKey::KEY_G))
        {
            browserCursor = 0;
            browserOffset = 0;
        }
        else if(nextChar == keyCode(typed::TypedKey::KEY_A))
        {
            ctx.editor->openGitStageMode();
            return std::nullopt;
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_D))
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
    else if(c == keyCode(control::ControlKey::CTRL_U))
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

    else if(c == keyCode(control::ControlKey::ENTER) || c == keyCode(typed::TypedKey::KEY_L) || c == keyCode(navigation::NavigationKey::ARROW_RIGHT))
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

    else if(c == keyCode(typed::TypedKey::KEY_H) || c == keyCode(navigation::NavigationKey::ARROW_LEFT) || c == keyCode(command::CommandKey::KEY_MINUS))
    {
        size_t lastSlash = currentDirectory.find_last_of(keyCode(command::CommandKey::KEY_SLASH));
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
    else if(c == keyCode(command::CommandKey::KEY_DOT))
    {
        showHidden = !showHidden;
        loadDirectory(ctx, currentDirectory);
        ctx.setStatusMessage(showHidden ? "Showing hidden files"
                                        : "Hiding hidden files");
    }
    else if(c == keyCode(typed::TypedKey::KEY_I) || c == keyCode(control::ControlKey::CTRL_I))
    {
        ctx.setRespectGitignore(!ctx.respectGitignore());
        ctx.setFuzzyInitialized(false);
        loadDirectory(ctx, currentDirectory);
        ctx.setStatusMessage(ctx.respectGitignore() ? "Respecting .gitignore"
                                                    : "Ignoring .gitignore");
    }

    // Refresh
    else if(c == keyCode(typed::TypedKey::KEY_R) || c == keyCode(control::ControlKey::CTRL_L))
    {
        loadDirectory(ctx, currentDirectory);
    }

    // Create new file
    else if(c == keyCode(command::CommandKey::KEY_PERCENT))
    {
        ctx.createNewFilePrompt();
    }

    // Create new directory
    else if(c == keyCode(typed::TypedKey::KEY_D))
    {
        ctx.createNewDirectoryPrompt();
    }

    // Delete file/directory
    else if(c == keyCode(typed::TypedKey::KEY_CAP_D))
    {
        ctx.deleteFilePrompt();
    }

    // Rename file/directory
    else if(c == keyCode(typed::TypedKey::KEY_CAP_R))
    {
        ctx.renameFilePrompt();
    }

    // ========================================================================
    // Mode Switching
    // ========================================================================

    else if(c == keyCode(control::ControlKey::CTRL_P) || c == keyCode(typed::TypedKey::KEY_F))
    {
        return FuzzyFindMode{};
    }
    else if(c == keyCode(control::ControlKey::CTRL_W) || c == keyCode(typed::TypedKey::KEY_B))
    {
        return BufferBrowserMode{};
    }
    else if(c == keyCode(control::ControlKey::CTRL_S))
    {
        return GrepSearchMode{};
    }

    if((c == keyCode(control::ControlKey::BACKSPACE) || c == 127 || c == keyCode(control::ControlKey::CTRL_H)) &&
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
        if(std::isalnum(static_cast<unsigned char>(c)) || c == keyCode(command::CommandKey::KEY_UNDERSCORE) ||
           c == keyCode(command::CommandKey::KEY_MINUS) || (filterActive && c == keyCode(command::CommandKey::KEY_DOT)))
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

    std::vector<char> searchHit(fileList.size(), 0);
    bool hasLiveSearch = false;
    bool hasCommittedSearch =
        !searchMatches.empty() && !lastSearchPattern.empty();
    if(commandPrompt && commandPrompt->isActive())
    {
        const std::string& input = commandPrompt->getInput();
        if(input.size() > 1 && (input[0] == keyCode(command::CommandKey::KEY_SLASH) || input[0] == keyCode(command::CommandKey::KEY_QUESTION)))
        {
            try
            {
                std::regex re(input.substr(1));
                hasLiveSearch = true;
                for(size_t i = 0; i < fileList.size(); ++i)
                {
                    const auto& entry = fileList[i];
                    if(entry.name == "..")
                        continue;
                    if(std::regex_search(entry.name, re))
                        searchHit[i] = 1;
                }
            }
            catch(const std::regex_error&)
            {
            }
        }
    }
    if(!hasLiveSearch && hasCommittedSearch)
    {
        for(int idx : searchMatches)
        {
            if(idx >= 0 && idx < static_cast<int>(searchHit.size()))
                searchHit[idx] = 1;
        }
    }
    bool searchVisualActive = hasLiveSearch || hasCommittedSearch;

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
            output += std::string(Terminal::ESC_DIM) +
                      (searchVisualActive ? editor.theme.searchMatch()
                                          : editor.theme.selection());
        }
        else if(searchVisualActive)
        {
            int mappedIndex = index;
            if(filterActive)
            {
                if(index < 0 || index >= static_cast<int>(filterMatches.size()))
                    mappedIndex = -1;
                else
                    mappedIndex = filterMatches[index];
            }
            if(mappedIndex >= 0 &&
               mappedIndex < static_cast<int>(searchHit.size()) &&
               searchHit[mappedIndex])
            {
                if(searchVisualActive)
                    output += std::string(Terminal::ESC_DIM) +
                              editor.theme.selection();
                else
                    output += editor.theme.baseFg();
            }
        }

        output += "  ";

        if(entry.isDirectory)
        {
            output += editor.theme.uiDirectory();
            output += ascii::utf8(ascii::FOLDER_ICON);
            output += Terminal::ESC_BOLD;
        }
        else
        {
            output += ascii::utf8(ascii::FILE_ICON);
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
                output.append(padding, keyCode(control::ControlKey::SPACE));
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
    if(!lastSearchPattern.empty() && !searchMatches.empty() &&
       currentSearchMatch >= 0 &&
       currentSearchMatch < static_cast<int>(searchMatches.size()))
    {
        status += " [" + std::to_string(currentSearchMatch + 1) + "/" +
                  std::to_string(searchMatches.size()) + "]";
    }
    status += " | " + currentDirectory;
    std::string right = " " +
                        std::to_string(std::min(browserCursor + 1, count)) +
                        "/" + std::to_string(count) + " ";

    output += status;
    int padding = editor.screenCols - status.length() - right.length();
    if(padding > 0)
    {
        output.append(padding, keyCode(control::ControlKey::SPACE));
    }
    output += right;
    output += editor.theme.reset();

    output += Terminal::NEWLINE_CLEAR;
    if(commandPrompt && commandPrompt->isActive())
    {
        output += editor.theme.baseFg();
        output += ":" + commandPrompt->getInput();
    }
    else if(filterActive)
    {
        output += editor.theme.baseFg();
        output += "/" + filterQuery;
    }
    else if(!editor.statusMessage.empty())
    {
        output += editor.theme.baseFg();
        output += ": ";
        size_t maxLen =
            editor.screenCols > 2 ? (size_t)editor.screenCols - 2 : 0;
        output += editor.statusMessage.substr(
            0, std::min(maxLen, editor.statusMessage.length()));
    }
    else if(!editor.locMessage.empty())
    {
        output += editor.theme.baseFg();
        output += ": ";
        size_t maxLen =
            editor.screenCols > 2 ? (size_t)editor.screenCols - 2 : 0;
        output += editor.locMessage.substr(
            0, std::min(maxLen, editor.locMessage.length()));
    }
    else
    {
        output += editor.theme.baseFg();
        output += ":";
    }

    bool suppressCommandPopups = false;
    if(commandPrompt && commandPrompt->isActive())
    {
        const std::string& input = commandPrompt->getInput();
        suppressCommandPopups =
            !input.empty() && (input[0] == keyCode(command::CommandKey::KEY_SLASH) || input[0] == keyCode(command::CommandKey::KEY_QUESTION));
    }
    if(!suppressCommandPopups)
    {
        editor.drawCommandHistoryPopup(output);
        editor.drawCommandPopup(output);
    }

    if(commandPrompt && commandPrompt->isActive())
    {
        output += Terminal::ESC_SHOW_CURSOR;
        int row = editor.screenRows + 2;
        int col = 2 + (int)commandPrompt->getInput().size();
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

    const bool syncOutput = Terminal::useSynchronizedOutput();
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
    Terminal::write(output);
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
    Terminal::flush();
}

static int fuzzyScore(std::string_view text, std::string_view pattern)
{
    if(pattern.empty())
        return 0;

    auto lower = [](unsigned char ch) -> unsigned char
    {
        if(ch >= keyCode(typed::TypedKey::KEY_CAP_A) && ch <= keyCode(typed::TypedKey::KEY_CAP_Z))
            return (unsigned char)(ch - keyCode(typed::TypedKey::KEY_CAP_A) + keyCode(typed::TypedKey::KEY_A));
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
                    if(prev == keyCode(command::CommandKey::KEY_UNDERSCORE) || prev == keyCode(command::CommandKey::KEY_MINUS) || prev == keyCode(control::ControlKey::SPACE) ||
                       prev == '\t' || prev == keyCode(command::CommandKey::KEY_DOT))
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
    searchMatches.clear();
    lastSearchPattern.clear();
    lastSearchPrefix = 0;
    currentSearchMatch = -1;
    searchTabCandidates.clear();
    searchTabSeed.clear();
    searchTabIndex = -1;

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
    }

    std::filesystem::path resolvedDir;
    if(dirPath.is_absolute())
    {
        // Preserve absolute input path spelling (e.g. /var vs /private/var).
        resolvedDir = dirPath.lexically_normal();
    }
    else
    {
        resolvedDir = std::filesystem::absolute(dirPath, ec);
        if(ec || resolvedDir.empty())
        {
            ec.clear();
            resolvedDir = dirPath;
        }
        resolvedDir = resolvedDir.lexically_normal();
    }

    currentDirectory = file_utils::path_to_utf8_string(resolvedDir);

    GitIgnore gitignore;
    if(ctx.respectGitignore())
    {
        gitignore.loadRecursive(resolvedDir);
    }

    std::vector<FileEntry> dirs;
    std::vector<FileEntry> files;

    const auto push_entry = [&](FileEntry&& fe)
    { (fe.isDirectory ? dirs : files).push_back(std::move(fe)); };

    {
        std::error_code parentEc;
        std::filesystem::path resolved =
            std::filesystem::absolute(resolvedDir, parentEc);
        if(parentEc || resolved.empty())
        {
            resolved = resolvedDir;
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

    for(std::filesystem::directory_iterator it{resolvedDir, opts, ec}, end;
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
        std::filesystem::path entryPath = std::filesystem::absolute(de.path(), ec2);
        if(ec2 || entryPath.empty())
        {
            ec2.clear();
            entryPath = de.path();
        }
        fe.path = file_utils::path_to_utf8_string(entryPath);
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
                         return fileList[a.first].name < fileList[b.first].name;
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

            const auto runRegexSearch = [&](char prefix, bool backward) -> bool
            {
                if(cmd.empty() || cmd[0] != prefix)
                    return false;

                std::string pattern = cmd.substr(1);
                if(!args.empty())
                {
                    if(!pattern.empty())
                        pattern.push_back(keyCode(control::ControlKey::SPACE));
                    pattern += args;
                }
                if(pattern.empty())
                {
                    ctx.setStatusMessage(prefix == keyCode(command::CommandKey::KEY_SLASH) ? "Usage: :/ <regex>"
                                                       : "Usage: :? <regex>");
                    return true;
                }

                std::regex re;
                try
                {
                    re = std::regex(pattern);
                }
                catch(const std::regex_error&)
                {
                    ctx.setStatusMessage("Invalid regex: " + pattern);
                    return true;
                }

                if(fileList.empty())
                {
                    ctx.setStatusMessage("No entries to search");
                    return true;
                }

                int startIndex = browserCursor;
                if(filterActive)
                {
                    if(browserCursor >= 0 &&
                       browserCursor < static_cast<int>(filterMatches.size()))
                    {
                        startIndex = filterMatches[browserCursor];
                    }
                    filterActive = false;
                    filterQuery.clear();
                    filterMatches.clear();
                }
                startIndex = std::max(
                    0, std::min(startIndex,
                                static_cast<int>(fileList.size()) - 1));

                const int count = static_cast<int>(fileList.size());
                std::vector<int> matches;
                matches.reserve(count);
                for(int i = 0; i < count; ++i)
                {
                    const auto& entry = fileList[i];
                    if(entry.name == "..")
                        continue;
                    if(std::regex_search(entry.name, re))
                    {
                        matches.push_back(i);
                    }
                }

                if(matches.empty())
                {
                    searchMatches.clear();
                    lastSearchPattern.clear();
                    lastSearchPrefix = 0;
                    currentSearchMatch = -1;
                    ctx.setStatusMessage("No match for regex: " + pattern);
                    return true;
                }

                int matchPos = -1;
                if(backward)
                {
                    for(int i = static_cast<int>(matches.size()) - 1; i >= 0;
                        --i)
                    {
                        if(matches[i] <= startIndex)
                        {
                            matchPos = i;
                            break;
                        }
                    }
                    if(matchPos < 0)
                        matchPos = static_cast<int>(matches.size()) - 1;
                }
                else
                {
                    for(int i = 0; i < static_cast<int>(matches.size()); ++i)
                    {
                        if(matches[i] >= startIndex)
                        {
                            matchPos = i;
                            break;
                        }
                    }
                    if(matchPos < 0)
                        matchPos = 0;
                }

                searchMatches = std::move(matches);
                lastSearchPattern = pattern;
                lastSearchPrefix = prefix;
                currentSearchMatch = matchPos;
                browserCursor = searchMatches[currentSearchMatch];
                int visible = std::max(1, ctx.screenRows() - 4);
                if(browserCursor < browserOffset)
                    browserOffset = browserCursor;
                if(browserCursor >= browserOffset + visible)
                    browserOffset = browserCursor - visible + 1;

                const FileEntry* currentEntry = entryAt(browserCursor);
                if(!currentEntry)
                    return true;
                if(currentEntry->isDirectory)
                {
                    loadDirectory(ctx, currentEntry->path);
                    browserCursor = 0;
                    browserOffset = 0;
                    return true;
                }
                ctx.openFile(std::string_view(currentEntry->path));
                nextState = ctx.hasBuffer() ? std::optional<ModeState>(
                                                  ModeState{NormalMode{}})
                                            : std::optional<ModeState>(
                                                  ModeState{WelcomeMode{}});

                return true;
            };

            if(runRegexSearch(keyCode(command::CommandKey::KEY_SLASH), false))
                return true;
            if(runRegexSearch(keyCode(command::CommandKey::KEY_QUESTION), true))
                return true;

            // =================================================================
            // Quit commands - exit file browser mode
            // =================================================================
            if(cmd == "q" || cmd == "q!")
            {
                if(!previousFile.empty())
                {
                    ctx.openFile(std::string_view(previousFile));
                }
                if(ctx.hasBuffer())
                {
                    nextState = ModeState{NormalMode{}};
                }
                else
                {
                    ctx.forceQuit();
                    nextState.reset();
                }
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
                    if(args[0] == keyCode(command::CommandKey::KEY_SLASH) || args[0] == keyCode(command::CommandKey::KEY_TILDE))
                    {
                        // Absolute path
                        if(args[0] == keyCode(command::CommandKey::KEY_TILDE))
                        {
                            const char* home = getenv("HOME");
                            if(home)
                            {
                                targetPath = std::filesystem::path(home);
                                if(args.length() > 1 && args[1] == keyCode(command::CommandKey::KEY_SLASH))
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
                        std::string pathStr =
                            file_utils::path_to_utf8_string(targetPath);
                        if(chdir(pathStr.c_str()) == 0)
                        {
                            char cwd[PATH_MAX];
                            if(getcwd(cwd, sizeof(cwd)))
                                ctx.setStatusMessage(std::string(cwd));
                        }
                    }
                    else
                    {
                        ctx.setStatusMessage("Not a directory: " + args);
                    }
                }
                return true;
            }
            if(cmd == "cdr")
            {
                const std::string& root = ctx.editor->getProjectRoot();
                if(root.empty())
                {
                    ctx.setStatusMessage("Project root not set");
                    return true;
                }
                loadDirectory(ctx, root);
                browserCursor = 0;
                browserOffset = 0;
                if(chdir(root.c_str()) == 0)
                {
                    char cwd[PATH_MAX];
                    if(getcwd(cwd, sizeof(cwd)))
                        ctx.setStatusMessage(std::string(cwd));
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
                    ":mkdir <name> :touch <name> :cd :cdr :/re :?re "
                    "<path>");
                return true;
            }

            return false;
        },
        [&](ModeContext& ctx, std::string_view line) -> std::optional<ModeState>
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
