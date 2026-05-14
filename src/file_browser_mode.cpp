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
#include <optional>
#include <regex>
#include <sstream>
#include "os_compat.h"
#include <vector>

// ============================================================================
// FileBrowserMode Implementation
// ============================================================================

namespace
{
bool isPlainSearchPattern(std::string_view pattern)
{
    return pattern.find_first_of(R"(\.^$|()[]{}*+?)") == std::string_view::npos;
}

bool fileBrowserNameMatchesSearch(const std::string& name,
                                  const std::string& pattern,
                                  bool plainPattern,
                                  const std::regex* regexPattern)
{
    if(plainPattern)
        return name.find(pattern) != std::string::npos;
    return regexPattern && std::regex_search(name, *regexPattern);
}

void ensureEntryMetadata(FileEntry& entry)
{
    if(entry.metadataLoaded || entry.name == "..")
        return;

    std::filesystem::path path(entry.path);
    std::error_code ec;
    auto st = file_utils::status_with_policy(path, ec);
    if(!ec && std::filesystem::is_regular_file(st))
        entry.size = file_utils::file_size_to_size_t(path);
    else
        entry.size = 0;
    entry.modTime = file_utils::mtime_nothrow(path);
    entry.metadataLoaded = true;
}

const std::string& trashRoot()
{
    static const std::string root = [] {
        std::filesystem::path base = std::filesystem::temp_directory_path();
        base /= ("uvim_trash_" + std::to_string(::getpid()));
        std::error_code ec;
        std::filesystem::create_directories(base, ec);
        return file_utils::path_to_utf8_string(base.lexically_normal());
    }();
    return root;
}

std::string allocateTrashPath(const std::string& originalPath)
{
    static uint64_t counter = 0;
    std::filesystem::path orig(originalPath);
    std::string name = file_utils::path_to_utf8_string(orig.filename());
    if(name.empty())
        name = "entry";
    std::filesystem::path target = std::filesystem::path(trashRoot()) /
                                   (std::to_string(counter++) + "_" + name);
    return file_utils::path_to_utf8_string(target.lexically_normal());
}

std::error_code moveToTrash(const std::string& src, const std::string& dst)
{
    std::error_code ec;
    std::filesystem::rename(std::filesystem::path(src),
                            std::filesystem::path(dst), ec);
    if(!ec)
        return ec;
    ec.clear();
    std::error_code typeEc;
    if(std::filesystem::is_directory(std::filesystem::path(src), typeEc))
    {
        std::filesystem::copy(std::filesystem::path(src),
                              std::filesystem::path(dst),
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::copy_symlinks,
                              ec);
    }
    else
    {
        std::filesystem::copy_file(std::filesystem::path(src),
                                   std::filesystem::path(dst), ec);
    }
    if(ec)
        return ec;
    std::error_code rmEc;
    std::filesystem::remove_all(std::filesystem::path(src), rmEc);
    return rmEc;
}
} // namespace

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

    if(confirmingDelete)
    {
        if(c == keyCode(typed::TypedKey::KEY_Y) ||
           c == keyCode(typed::TypedKey::KEY_CAP_Y))
        {
            int deleted = 0;
            int failed = 0;
            FileBrowserOp op;
            op.kind = FileBrowserOp::Kind::Delete;
            for(const auto& path : deleteTargets)
            {
                std::string trash = allocateTrashPath(path);
                std::error_code ec = moveToTrash(path, trash);
                if(ec)
                    ++failed;
                else
                {
                    op.pairs.emplace_back(trash, path);
                    ++deleted;
                }
            }
            deleteTargets.clear();
            selectedFiles.clear();
            confirmingDelete = false;
            if(!op.pairs.empty())
            {
                undoStack.push_back(std::move(op));
                redoStack.clear();
            }
            loadDirectory(ctx, currentDirectory);
            if(failed > 0)
                ctx.setStatusMessage("Deleted " + std::to_string(deleted) +
                                     ", " + std::to_string(failed) + " failed");
            else
                ctx.setStatusMessage("Deleted " + std::to_string(deleted) +
                                     " item(s)");
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c == keyCode(typed::TypedKey::KEY_N) ||
           c == keyCode(typed::TypedKey::KEY_CAP_N) ||
           c == keyCode(control::ControlKey::ESC) ||
           c == keyCode(control::ControlKey::ENTER))
        {
            deleteTargets.clear();
            confirmingDelete = false;
            ctx.setStatusMessage("");
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        return std::nullopt;
    }

    if(confirmingDirCreate)
    {
        if(c == keyCode(typed::TypedKey::KEY_Y) ||
           c == keyCode(typed::TypedKey::KEY_CAP_Y))
        {
            std::filesystem::path target(pendingFilePath);
            std::filesystem::path parent = target.parent_path();
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            bool ok = !ec;
            if(ok)
            {
                std::ofstream file(target);
                ok = file.is_open();
                if(ok)
                    file.close();
            }
            std::string createdPath = pendingFilePath;
            std::string parentStr =
                file_utils::path_to_utf8_string(parent.lexically_normal());
            confirmingDirCreate = false;
            pendingFilePath.clear();
            pendingParentRel.clear();
            if(!ok)
            {
                ctx.setStatusMessage("Failed to create file");
            }
            else
            {
                if(parentStr != currentDirectory)
                    navigateTo(ctx, parentStr);
                else
                    loadDirectory(ctx, currentDirectory);
                for(int i = 0; i < (int)fileList.size(); ++i)
                {
                    if(fileList[i].path == createdPath)
                    {
                        browserCursor = i;
                        int visible = std::max(1, ctx.screenRows() - 5);
                        if(browserCursor < browserOffset)
                            browserOffset = browserCursor;
                        if(browserCursor >= browserOffset + visible)
                            browserOffset = browserCursor - visible + 1;
                        break;
                    }
                }
                ctx.setStatusMessage("Created file: " + createdPath);
            }
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c == keyCode(typed::TypedKey::KEY_N) ||
           c == keyCode(typed::TypedKey::KEY_CAP_N) ||
           c == keyCode(control::ControlKey::ESC) ||
           c == keyCode(control::ControlKey::ENTER))
        {
            confirmingDirCreate = false;
            pendingFilePath.clear();
            pendingParentRel.clear();
            ctx.setStatusMessage("Cancelled");
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        return std::nullopt;
    }

    if(confirmingFileReplace)
    {
        auto finishSelect = [&]()
        {
            std::string parentStr = file_utils::path_to_utf8_string(
                std::filesystem::path(pendingFilePath)
                    .parent_path()
                    .lexically_normal());
            if(parentStr != currentDirectory)
                navigateTo(ctx, parentStr);
            else
                loadDirectory(ctx, currentDirectory);
            for(int i = 0; i < (int)fileList.size(); ++i)
            {
                if(fileList[i].path == pendingFilePath)
                {
                    browserCursor = i;
                    int visible = std::max(1, ctx.screenRows() - 5);
                    if(browserCursor < browserOffset)
                        browserOffset = browserCursor;
                    if(browserCursor >= browserOffset + visible)
                        browserOffset = browserCursor - visible + 1;
                    break;
                }
            }
        };

        if(c == keyCode(typed::TypedKey::KEY_O) ||
           c == keyCode(typed::TypedKey::KEY_CAP_O) ||
           c == keyCode(control::ControlKey::ENTER))
        {
            std::string target = pendingFilePath;
            confirmingFileReplace = false;
            pendingFilePath.clear();
            ctx.openFile(std::string_view(target));
            ctx.requestFullRedraw();
            return ctx.hasBuffer() ? ModeState{NormalMode{}}
                                   : ModeState{WelcomeMode{}};
        }
        if(c == keyCode(typed::TypedKey::KEY_R) ||
           c == keyCode(typed::TypedKey::KEY_CAP_R))
        {
            std::ofstream file(pendingFilePath, std::ios::trunc);
            bool ok = file.is_open();
            if(ok)
                file.close();
            if(!ok)
            {
                ctx.setStatusMessage("Failed to replace: " + pendingFilePath);
            }
            else
            {
                std::string msg = "Replaced file: " + pendingFilePath;
                finishSelect();
                ctx.setStatusMessage(msg);
            }
            confirmingFileReplace = false;
            pendingFilePath.clear();
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ESC))
        {
            confirmingFileReplace = false;
            pendingFilePath.clear();
            ctx.setStatusMessage("Cancelled");
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        return std::nullopt;
    }

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
        int visible = std::max(1, ctx.screenRows() - 5);
        if(browserCursor < browserOffset)
            browserOffset = browserCursor;
        if(browserCursor >= browserOffset + visible)
            browserOffset = browserCursor - visible + 1;
    };

    auto openSelectedFiles = [&]() -> std::optional<ModeState>
    {
        std::vector<std::string> toOpen;
        toOpen.reserve(selectedFiles.size());
        for(const auto& p : selectedFiles)
        {
            std::error_code dirEc;
            if(std::filesystem::is_directory(std::filesystem::path(p), dirEc))
                continue;
            toOpen.push_back(p);
        }
        if(toOpen.empty())
        {
            ctx.setStatusMessage("No files selected to open");
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        std::sort(toOpen.begin(), toOpen.end());
        for(const auto& p : toOpen)
            ctx.openFile(std::string_view(p));
        selectedFiles.clear();
        if(visualMode)
        {
            visualMode = false;
            preVisualSelected.clear();
        }
        ctx.setStatusMessage("Opened " + std::to_string(toOpen.size()) +
                             " file(s)");
        return ctx.hasBuffer()
                   ? std::optional<ModeState>(ModeState{NormalMode{}})
                   : std::optional<ModeState>(ModeState{WelcomeMode{}});
    };

    const auto collectRegexMatches =
        [&](char prefix, const std::string& pattern) -> std::vector<int>
    {
        (void)prefix;
        std::vector<int> matches;
        if(isPlainSearchPattern(pattern))
        {
            for(int i = 0; i < static_cast<int>(fileList.size()); ++i)
            {
                const auto& entry = fileList[i];
                if(entry.name == "..")
                    continue;
                if(entry.name.find(pattern) != std::string::npos)
                    matches.push_back(i);
            }
            return matches;
        }

        std::regex re(pattern, std::regex::optimize);
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

    const auto findFirstRegexMatch =
        [&](char prefix, const std::string& pattern) -> std::optional<int>
    {
        (void)prefix;
        if(isPlainSearchPattern(pattern))
        {
            for(int i = 0; i < static_cast<int>(fileList.size()); ++i)
            {
                const auto& entry = fileList[i];
                if(entry.name == "..")
                    continue;
                if(entry.name.find(pattern) != std::string::npos)
                    return i;
            }
            return std::nullopt;
        }

        std::regex re(pattern, std::regex::optimize);
        for(int i = 0; i < static_cast<int>(fileList.size()); ++i)
        {
            const auto& entry = fileList[i];
            if(entry.name == "..")
                continue;
            if(std::regex_search(entry.name, re))
                return i;
        }
        return std::nullopt;
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
            !input.empty() &&
            (input[0] == keyCode(command::CommandKey::KEY_SLASH) || input[0] == keyCode(command::CommandKey::KEY_QUESTION));

        if(promptSearch && c == keyCode(control::ControlKey::ENTER) &&
           !selectedFiles.empty())
        {
            std::optional<ModeState> ignored;
            (void)commandPrompt->handle(
                ctx, keyCode(control::ControlKey::ESC),
                [&](std::string_view commandLine)
                { return executeCommand(ctx, commandLine); }, ignored);
            return openSelectedFiles();
        }

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

            bool sameSearchPattern =
                lastSearchPattern == pattern && lastSearchPrefix == prefix;
            bool canReuseMatches = sameSearchPattern && !searchMatches.empty();
            if(!canReuseMatches)
            {
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

                searchMatches = std::move(matches);
                lastSearchPattern = pattern;
                lastSearchPrefix = prefix;
            }

            if(!sameSearchPattern || currentSearchMatch < 0 ||
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

        if(promptSearch &&
           (c == 0 || c == keyCode(control::ControlKey::CTRL_N)))
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
            bool allSelected = !matches.empty();
            for(int idx : matches)
            {
                if(idx < 0 || idx >= (int)fileList.size())
                    continue;
                const auto& entry = fileList[idx];
                if(entry.name == "..")
                    continue;
                if(!selectedFiles.count(entry.path))
                {
                    allSelected = false;
                    break;
                }
            }
            int changed = 0;
            if(allSelected)
            {
                for(int idx : matches)
                {
                    if(idx < 0 || idx >= (int)fileList.size())
                        continue;
                    const auto& entry = fileList[idx];
                    if(entry.name == "..")
                        continue;
                    if(selectedFiles.erase(entry.path))
                        ++changed;
                }
                ctx.setStatusMessage("Unselected " + std::to_string(changed) +
                                     " match(es)");
            }
            else
            {
                for(int idx : matches)
                {
                    if(idx < 0 || idx >= (int)fileList.size())
                        continue;
                    const auto& entry = fileList[idx];
                    if(entry.name == "..")
                        continue;
                    if(selectedFiles.insert(entry.path).second)
                        ++changed;
                }
                ctx.setStatusMessage("Selected " + std::to_string(changed) +
                                     " match(es)");
            }
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

    auto runTabAppendSelected = [&]() -> bool
    {
        if(!commandPrompt || !commandPrompt->isActive())
            return false;
        const std::string& input = commandPrompt->getInput();
        if(input.rfind("run ", 0) != 0 && input != "run")
            return false;
        if(selectedFiles.empty())
            return false;
        auto shellQuote = [](const std::string& s) -> std::string
        {
            bool needsQuote = s.find_first_of(" \t'\"\\$`()|&;<>*?[]{}") !=
                              std::string::npos;
            if(!needsQuote)
                return s;
            std::string out;
            out.reserve(s.size() + 2);
            out += '\'';
            for(char ch : s)
            {
                if(ch == '\'')
                    out += "'\\''";
                else
                    out += ch;
            }
            out += '\'';
            return out;
        };
        std::vector<std::string> ordered;
        ordered.reserve(selectedFiles.size());
        std::filesystem::path base(currentDirectory);
        for(const auto& p : selectedFiles)
        {
            std::error_code ec;
            std::filesystem::path rel =
                std::filesystem::relative(std::filesystem::path(p), base, ec);
            std::string s =
                ec ? p : file_utils::path_to_utf8_string(rel);
            if(s.empty())
                s = p;
            ordered.push_back(std::move(s));
        }
        std::sort(ordered.begin(), ordered.end());
        std::string newInput = input;
        if(!newInput.empty() && newInput.back() != ' ')
            newInput += ' ';
        for(size_t i = 0; i < ordered.size(); ++i)
        {
            if(i > 0)
                newInput += ' ';
            newInput += shellQuote(ordered[i]);
        }
        commandPrompt->setInput(std::move(newInput));
        ctx.cancelCommandPopup();
        ctx.requestFullRedraw();
        return true;
    };

    if(commandPrompt && commandPrompt->isActive() && c != keyCode(control::ControlKey::TAB) &&
       c != keyCode(control::ControlKey::SHIFT_TAB))
    {
        resetSearchTabCompletion();
    }
    if(c == keyCode(control::ControlKey::TAB) && runTabAppendSelected())
        return std::nullopt;
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

        std::optional<int> match;
        try
        {
            match = findFirstRegexMatch(prefix, pattern);
        }
        catch(const std::regex_error&)
        {
            clearSearchState();
            return;
        }
        if(!match)
        {
            clearSearchState();
            return;
        }

        lastSearchPattern = pattern;
        lastSearchPrefix = prefix;
        currentSearchMatch = 0;
        browserCursor = *match;
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

    if((c == keyCode(typed::TypedKey::KEY_N) ||
        c == keyCode(typed::TypedKey::KEY_CAP_N)) &&
       !searchMatches.empty())
    {
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
    // Exit / cancel
    // ========================================================================

    if(c == keyCode(control::ControlKey::ESC))
    {
        if(filterActive)
        {
            filterActive = false;
            filterQuery.clear();
            filterMatches.clear();
            browserCursor = 0;
            browserOffset = 0;
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        bool doubleEsc = ctx.editor->noteDoubleEscStatusClear();
        if(doubleEsc)
        {
            selectedFiles.clear();
            copyBuffer.clear();
            bool wasMove = moveMode;
            moveMode = false;
            visualMode = false;
            preVisualSelected.clear();
            confirmingDirCreate = false;
            confirmingFileReplace = false;
            pendingFilePath.clear();
            pendingParentRel.clear();
            if(wasMove)
                loadDirectory(ctx, currentDirectory);
            ctx.setStatusMessage("Cleared selections and buffer");
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(visualMode)
        {
            selectedFiles = preVisualSelected;
            preVisualSelected.clear();
            visualMode = false;
            ctx.setStatusMessage("Visual mode cancelled");
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(moveMode)
        {
            copyBuffer.clear();
            moveMode = false;
            loadDirectory(ctx, currentDirectory);
            ctx.setStatusMessage("Move cancelled");
        }
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_Q))
    {
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
            int visible = ctx.screenRows() - 5;
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
        int visible = ctx.screenRows() - 5;
        if(browserCursor >= visible)
            browserOffset = browserCursor - visible + 1;
    }
    else if(c == keyCode(typed::TypedKey::KEY_C))
    {
        int nextChar = Terminal::readKey();
        if(nextChar == keyCode(typed::TypedKey::KEY_D))
        {
            std::error_code chEc;
            std::filesystem::current_path(currentDirectory, chEc);
            if(!chEc)
            {
                ctx.setGrepFileIndexInitialized(false);
                std::error_code cwdEc;
                auto cwd = std::filesystem::current_path(cwdEc);
                if(!cwdEc)
                    ctx.setStatusMessage("PWD: " + cwd.string());
                else
                    ctx.setStatusMessage("PWD updated");
            }
            else
            {
                ctx.setStatusMessage("Failed to chdir: " + currentDirectory);
            }
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_G))
    {
        int nextChar = Terminal::readKey();
        if(nextChar == keyCode(typed::TypedKey::KEY_G))
        {
            if(visualMode)
            {
                int target = firstNonDotDotIndex();
                if(target < 0)
                {
                    ctx.setStatusMessage(
                        "No files or directories to select");
                }
                else
                {
                    browserCursor = target;
                    browserOffset = 0;
                }
            }
            else
            {
                browserCursor = 0;
                browserOffset = 0;
            }
        }
        else if(nextChar == keyCode(typed::TypedKey::KEY_A))
        {
            ctx.editor->openGitStageMode();
            return std::nullopt;
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_D))
    {
        int half = (ctx.screenRows() - 5) / 2;
        browserCursor += half;
        int count = listSize();
        if(browserCursor >= count)
            browserCursor = std::max(0, count - 1);
        int visible = ctx.screenRows() - 5;
        if(browserCursor >= browserOffset + visible)
            browserOffset = browserCursor - visible + 1;
    }
    else if(c == keyCode(control::ControlKey::CTRL_U))
    {
        int half = (ctx.screenRows() - 5) / 2;
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
        // If Enter and multiple files are selected, open all of them as
        // buffers. Directories in the selection are skipped.
        if(c == keyCode(control::ControlKey::ENTER) && !selectedFiles.empty())
        {
            return openSelectedFiles();
        }
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
                navigateTo(ctx, std::move(targetPath));
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
            navigateTo(ctx, std::move(parentDir));
            browserCursor = 0;
            browserOffset = 0;
        }
    }

    // ========================================================================
    // History (back / forward)
    // ========================================================================

    else if(c == keyCode(control::ControlKey::CTRL_O))
    {
        if(historyBack.empty())
        {
            ctx.setStatusMessage("No more history back");
        }
        else
        {
            std::string target = std::move(historyBack.back());
            historyBack.pop_back();
            historyForward.push_back(currentDirectory);
            redoStack.clear();
            loadDirectory(ctx, std::move(target));
            browserCursor = 0;
            browserOffset = 0;
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_I) ||
            c == keyCode(control::ControlKey::TAB))
    {
        if(historyForward.empty())
        {
            ctx.setStatusMessage("No more history forward");
        }
        else
        {
            std::string target = std::move(historyForward.back());
            historyForward.pop_back();
            historyBack.push_back(currentDirectory);
            redoStack.clear();
            loadDirectory(ctx, std::move(target));
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
    else if(c == keyCode(control::ControlKey::CTRL_G))
    {
        if(ctx.editor->gitignoreLockedOff)
        {
            ctx.setStatusMessage(".gitignore disabled by launch option");
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        ctx.setRespectGitignore(!ctx.respectGitignore());
        ctx.setGrepFileIndexInitialized(false);
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

    // Toggle selection on current entry
    else if(c == keyCode(control::ControlKey::SPACE))
    {
        const FileEntry* entryPtr = entryAt(browserCursor);
        if(entryPtr && entryPtr->name != "..")
        {
            auto it = selectedFiles.find(entryPtr->path);
            if(it != selectedFiles.end())
                selectedFiles.erase(it);
            else
                selectedFiles.insert(entryPtr->path);
        }
    }

    // Toggle visual-line selection mode (shift-V)
    else if(c == keyCode(typed::TypedKey::KEY_CAP_V))
    {
        if(visualMode)
        {
            visualMode = false;
            preVisualSelected.clear();
            ctx.setStatusMessage("");
        }
        else
        {
            int first = firstNonDotDotIndex();
            if(first < 0)
            {
                ctx.setStatusMessage("No files or directories");
            }
            else
            {
                const FileEntry* cur = entryAt(browserCursor);
                if(!cur || cur->name == "..")
                {
                    browserCursor = first;
                    int visible = std::max(1, ctx.screenRows() - 5);
                    if(browserCursor < browserOffset)
                        browserOffset = browserCursor;
                    if(browserCursor >= browserOffset + visible)
                        browserOffset = browserCursor - visible + 1;
                }
                preVisualSelected = selectedFiles;
                visualAnchor = browserCursor;
                visualMode = true;
                updateVisualSelection();
            }
        }
    }

    // Delete selected files (or current under cursor)
    else if(c == keyCode(typed::TypedKey::KEY_D))
    {
        deleteTargets.clear();
        if(!selectedFiles.empty())
        {
            for(const auto& p : selectedFiles)
                deleteTargets.push_back(p);
        }
        else
        {
            const FileEntry* entryPtr = entryAt(browserCursor);
            if(entryPtr && entryPtr->name != "..")
                deleteTargets.push_back(entryPtr->path);
        }
        if(deleteTargets.empty())
        {
            ctx.setStatusMessage("Nothing to delete");
        }
        else
        {
            confirmingDelete = true;
            ctx.setStatusMessage("Delete " +
                                 std::to_string(deleteTargets.size()) +
                                 " item(s)? y/n [n default]");
        }
    }

    // Yank selected files (or current) to paste buffer (vim-style copy)
    else if(c == keyCode(typed::TypedKey::KEY_Y))
    {
        if(visualMode)
            updateVisualSelection();
        copyBuffer.clear();
        moveMode = false;
        if(!selectedFiles.empty())
        {
            for(const auto& p : selectedFiles)
                copyBuffer.push_back(p);
        }
        else
        {
            const FileEntry* entryPtr = entryAt(browserCursor);
            if(entryPtr && entryPtr->name != "..")
                copyBuffer.push_back(entryPtr->path);
        }
        if(visualMode)
        {
            visualMode = false;
            preVisualSelected.clear();
        }
        if(copyBuffer.empty())
            ctx.setStatusMessage("Nothing to yank");
        else
            ctx.setStatusMessage("Yanked " +
                                 std::to_string(copyBuffer.size()) +
                                 " item(s)");
    }

    // Cut selected files (or current) into move buffer
    else if(c == keyCode(typed::TypedKey::KEY_M))
    {
        copyBuffer.clear();
        if(!selectedFiles.empty())
        {
            for(const auto& p : selectedFiles)
                copyBuffer.push_back(p);
        }
        else
        {
            const FileEntry* entryPtr = entryAt(browserCursor);
            if(entryPtr && entryPtr->name != "..")
                copyBuffer.push_back(entryPtr->path);
        }
        if(copyBuffer.empty())
        {
            moveMode = false;
            ctx.setStatusMessage("Nothing to move");
        }
        else
        {
            moveMode = true;
            loadDirectory(ctx, currentDirectory);
            ctx.setStatusMessage("Cut " +
                                 std::to_string(copyBuffer.size()) +
                                 " item(s)");
        }
    }

    // Paste buffer contents into current directory
    else if(c == keyCode(typed::TypedKey::KEY_P))
    {
        if(copyBuffer.empty())
        {
            ctx.setStatusMessage("Nothing to paste");
        }
        else
        {
            int done = 0;
            int failed = 0;
            int skipped = 0;
            const bool wasMove = moveMode;
            FileBrowserOp op;
            op.kind = wasMove ? FileBrowserOp::Kind::Move
                              : FileBrowserOp::Kind::Paste;
            std::filesystem::path destDir(currentDirectory);
            for(const auto& srcStr : copyBuffer)
            {
                std::filesystem::path src(srcStr);
                std::error_code ec;
                if(wasMove && src.parent_path() == destDir)
                {
                    ++skipped;
                    continue;
                }
                std::filesystem::path dst = destDir / src.filename();
                if(std::filesystem::exists(dst, ec))
                {
                    std::string stem = dst.stem().string();
                    std::string ext = dst.extension().string();
                    for(int n = 1; n < 1000; ++n)
                    {
                        std::filesystem::path candidate =
                            dst.parent_path() /
                            (stem + "_copy" + std::to_string(n) + ext);
                        if(!std::filesystem::exists(candidate, ec))
                        {
                            dst = candidate;
                            break;
                        }
                    }
                }
                std::error_code opEc;
                if(wasMove)
                {
                    std::filesystem::rename(src, dst, opEc);
                }
                else if(std::filesystem::is_directory(src, opEc))
                {
                    std::filesystem::copy(
                        src, dst, std::filesystem::copy_options::recursive,
                        opEc);
                }
                else
                {
                    std::filesystem::copy_file(src, dst, opEc);
                }
                if(opEc)
                    ++failed;
                else
                {
                    ++done;
                    std::string dstStr = file_utils::path_to_utf8_string(
                        dst.lexically_normal());
                    if(wasMove)
                        op.pairs.emplace_back(srcStr, dstStr);
                    else
                        op.pairs.emplace_back(std::string(), dstStr);
                }
            }
            copyBuffer.clear();
            selectedFiles.clear();
            moveMode = false;
            if(!op.pairs.empty())
            {
                undoStack.push_back(std::move(op));
                redoStack.clear();
            }
            loadDirectory(ctx, currentDirectory);
            const char* verb = wasMove ? "Moved" : "Pasted";
            std::string msg = std::string(verb) + " " + std::to_string(done) +
                              " item(s)";
            if(failed > 0)
                msg += ", " + std::to_string(failed) + " failed";
            if(skipped > 0)
                msg += ", " + std::to_string(skipped) + " skipped";
            ctx.setStatusMessage(msg);
        }
    }

    // Undo last file operation
    else if(c == keyCode(typed::TypedKey::KEY_U))
    {
        if(undoStack.empty())
        {
            ctx.setStatusMessage("Nothing to undo");
        }
        else
        {
            FileBrowserOp op = std::move(undoStack.back());
            undoStack.pop_back();
            int ok = 0;
            int failed = 0;
            const char* what = "undo";
            switch(op.kind)
            {
            case FileBrowserOp::Kind::Delete:
                what = "delete";
                for(auto& p : op.pairs)
                {
                    std::error_code ec;
                    std::filesystem::rename(p.first, p.second, ec);
                    if(ec)
                        ++failed;
                    else
                        ++ok;
                }
                break;
            case FileBrowserOp::Kind::Move:
                what = "move";
                for(auto& p : op.pairs)
                {
                    std::error_code ec;
                    std::filesystem::rename(p.second, p.first, ec);
                    if(ec)
                        ++failed;
                    else
                        ++ok;
                }
                break;
            case FileBrowserOp::Kind::Paste:
                what = "paste";
                for(auto& p : op.pairs)
                {
                    p.first = allocateTrashPath(p.second);
                    std::error_code ec = moveToTrash(p.second, p.first);
                    if(ec)
                        ++failed;
                    else
                        ++ok;
                }
                break;
            case FileBrowserOp::Kind::Mkdir:
                what = "mkdir";
                for(auto it = op.pairs.rbegin(); it != op.pairs.rend(); ++it)
                {
                    std::error_code ec;
                    std::filesystem::remove_all(it->second, ec);
                    if(ec)
                        ++failed;
                    else
                        ++ok;
                }
                break;
            }
            redoStack.push_back(std::move(op));
            loadDirectory(ctx, currentDirectory);
            std::string msg =
                "Undo " + std::string(what) + ": " + std::to_string(ok);
            if(failed > 0)
                msg += ", " + std::to_string(failed) + " failed";
            ctx.setStatusMessage(msg);
        }
    }

    // Redo (Ctrl-R, vim-style)
    else if(c == keyCode(control::ControlKey::CTRL_R))
    {
        if(redoStack.empty())
        {
            ctx.setStatusMessage("Nothing to redo");
        }
        else
        {
            FileBrowserOp op = std::move(redoStack.back());
            redoStack.pop_back();
            int ok = 0;
            int failed = 0;
            const char* what = "redo";
            switch(op.kind)
            {
            case FileBrowserOp::Kind::Delete:
                what = "delete";
                for(auto& p : op.pairs)
                {
                    std::error_code ec;
                    std::filesystem::rename(p.second, p.first, ec);
                    if(ec)
                        ++failed;
                    else
                        ++ok;
                }
                break;
            case FileBrowserOp::Kind::Move:
                what = "move";
                for(auto& p : op.pairs)
                {
                    std::error_code ec;
                    std::filesystem::rename(p.first, p.second, ec);
                    if(ec)
                        ++failed;
                    else
                        ++ok;
                }
                break;
            case FileBrowserOp::Kind::Paste:
                what = "paste";
                for(auto& p : op.pairs)
                {
                    std::error_code ec;
                    std::filesystem::rename(p.first, p.second, ec);
                    if(!ec)
                        p.first.clear();
                    if(ec)
                        ++failed;
                    else
                        ++ok;
                }
                break;
            case FileBrowserOp::Kind::Mkdir:
                what = "mkdir";
                for(auto& p : op.pairs)
                {
                    std::error_code ec;
                    std::filesystem::create_directories(p.second, ec);
                    if(ec)
                        ++failed;
                    else
                        ++ok;
                }
                break;
            }
            undoStack.push_back(std::move(op));
            loadDirectory(ctx, currentDirectory);
            std::string msg =
                "Redo " + std::string(what) + ": " + std::to_string(ok);
            if(failed > 0)
                msg += ", " + std::to_string(failed) + " failed";
            ctx.setStatusMessage(msg);
        }
    }

    // Create new directory (open : prompt with "mkdir " prefilled)
    else if(c == keyCode(typed::TypedKey::KEY_CAP_D))
    {
        if(commandPrompt)
        {
            std::optional<ModeState> next;
            (void)commandPrompt->handle(
                ctx, keyCode(command::CommandKey::KEY_COLON),
                [&](std::string_view commandLine)
                { return executeCommand(ctx, commandLine); },
                next);
            commandPrompt->setInput("mkdir ");
            ctx.cancelCommandPopup();
        }
    }

    // Create new file (open : prompt with "new " prefilled)
    else if(c == keyCode(typed::TypedKey::KEY_N))
    {
        if(commandPrompt)
        {
            std::optional<ModeState> next;
            (void)commandPrompt->handle(
                ctx, keyCode(command::CommandKey::KEY_COLON),
                [&](std::string_view commandLine)
                { return executeCommand(ctx, commandLine); },
                next);
            commandPrompt->setInput("new ");
            ctx.cancelCommandPopup();
        }
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

    if(visualMode)
        updateVisualSelection();
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
        "  [Enter: open] [q: quit] [.: hidden] [-: parent] "
        "[^G: gitignore] [^O/^I: back/fwd] [:cmd]";
    output += editor.theme.baseFg();
    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    output += "  [Space: select] [d: delete] [y: yank] [m: move] [p: paste] "
              "[u: undo] [^R: redo] [:/regex ^N: select matches]";
    if(!selectedFiles.empty())
        output += "  (" + std::to_string(selectedFiles.size()) + " selected)";
    if(!copyBuffer.empty())
    {
        output += "  (" + std::to_string(copyBuffer.size());
        output += moveMode ? " cut)" : " yanked)";
    }
    output += editor.theme.baseFg();

    bool hasLiveSearch = false;
    std::string liveSearchPattern;
    bool liveSearchPlainPattern = false;
    std::optional<std::regex> liveSearchRegex;
    std::vector<char> committedSearchHit;
    bool hasCommittedSearch =
        !searchMatches.empty() && !lastSearchPattern.empty();
    if(commandPrompt && commandPrompt->isActive())
    {
        const std::string& input = commandPrompt->getInput();
        if(input.size() > 1 && (input[0] == keyCode(command::CommandKey::KEY_SLASH) || input[0] == keyCode(command::CommandKey::KEY_QUESTION)))
        {
            try
            {
                liveSearchPattern = input.substr(1);
                liveSearchPlainPattern =
                    isPlainSearchPattern(liveSearchPattern);
                if(!liveSearchPlainPattern)
                    liveSearchRegex.emplace(liveSearchPattern,
                                            std::regex::optimize);
                hasLiveSearch = true;
            }
            catch(const std::regex_error&)
            {
            }
        }
    }
    if(!hasLiveSearch && hasCommittedSearch)
    {
        committedSearchHit.assign(fileList.size(), 0);
        for(int idx : searchMatches)
        {
            if(idx >= 0 && idx < static_cast<int>(committedSearchHit.size()))
                committedSearchHit[idx] = 1;
        }
    }
    bool searchVisualActive = hasLiveSearch || hasCommittedSearch;

    int availableRows = editor.screenRows - 3;

    int count = listSize();
    for(int i = 0; i < availableRows && i + browserOffset < count; i++)
    {
        output += Terminal::NEWLINE_CLEAR;

        int index = i + browserOffset;
        const FileEntry* entryPtr = entryAt(index);
        if(!entryPtr)
            break;
        const FileEntry& entry = *entryPtr;

        bool isSelected =
            entry.name != ".." && selectedFiles.count(entry.path) > 0;

        if(index == browserCursor && isSelected)
        {
            output += "\x1b[48;2;56;120;72m";
            output += editor.theme.baseFg();
        }
        else if(index == browserCursor)
        {
            output += std::string(Terminal::ESC_DIM) +
                      (searchVisualActive ? editor.theme.searchMatch()
                                          : editor.theme.selection());
        }
        else if(isSelected)
        {
            output += "\x1b[48;2;24;64;36m";
            output += editor.theme.baseFg();
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
            bool isSearchHit = false;
            if(mappedIndex >= 0)
            {
                if(hasLiveSearch && mappedIndex < static_cast<int>(fileList.size()))
                {
                    const auto& mappedEntry = fileList[mappedIndex];
                    isSearchHit =
                        mappedEntry.name != ".." &&
                        fileBrowserNameMatchesSearch(
                            mappedEntry.name, liveSearchPattern,
                            liveSearchPlainPattern,
                            liveSearchRegex ? &*liveSearchRegex : nullptr);
                }
                else if(!hasLiveSearch &&
                        mappedIndex <
                            static_cast<int>(committedSearchHit.size()))
                {
                    isSearchHit = committedSearchHit[mappedIndex];
                }
            }
            if(isSearchHit)
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
            ensureEntryMetadata(const_cast<FileEntry&>(entry));
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

    std::string status = visualMode ? " V-BROWSE" : " BROWSE";
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

void FileBrowserMode::navigateTo(ModeContext& ctx, std::string pathStr)
{
    std::string oldDir = currentDirectory;
    if(visualMode)
    {
        visualMode = false;
        preVisualSelected.clear();
    }
    loadDirectory(ctx, std::move(pathStr));
    if(!oldDir.empty() && oldDir != currentDirectory)
    {
        historyBack.push_back(oldDir);
        historyForward.clear();
        redoStack.clear();
    }
}

int FileBrowserMode::firstNonDotDotIndex() const
{
    int n = listSize();
    for(int i = 0; i < n; ++i)
    {
        const FileEntry* e = entryAt(i);
        if(e && e->name != "..")
            return i;
    }
    return -1;
}

void FileBrowserMode::updateVisualSelection()
{
    if(!visualMode)
        return;
    selectedFiles = preVisualSelected;
    int a = std::min(visualAnchor, browserCursor);
    int b = std::max(visualAnchor, browserCursor);
    for(int i = a; i <= b; ++i)
    {
        const FileEntry* e = entryAt(i);
        if(e && e->name != "..")
            selectedFiles.insert(e->path);
    }
}

void FileBrowserMode::loadDirectory(ModeContext& ctx, std::string pathStr)
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

    {
        std::string stripped = file_utils::path_to_utf8_string(resolvedDir);
        while(stripped.size() > 1 && stripped.back() == '/')
            stripped.pop_back();
        resolvedDir = std::filesystem::path(stripped);
    }

    currentDirectory = file_utils::path_to_utf8_string(resolvedDir);

    GitIgnore gitignore;
    if(ctx.respectGitignore())
    {
        std::filesystem::path gitignoreRoot;
        if(ctx.editor && !ctx.editor->getProjectRoot().empty())
            gitignoreRoot = ctx.editor->getProjectRoot();
        gitignore.loadRecursive(resolvedDir, gitignoreRoot);
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
        up.metadataLoaded = true;
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
        fe.path = file_utils::path_to_utf8_string(de.path().lexically_normal());
        fe.isDirectory = isDir;

        if(moveMode && !copyBuffer.empty() &&
           std::find(copyBuffer.begin(), copyBuffer.end(), fe.path) !=
               copyBuffer.end())
            continue;

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

        int visible = std::max(1, ctx.screenRows() - 5);
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
    int visible = std::max(1, ctx.screenRows() - 5);
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
                int visible = std::max(1, ctx.screenRows() - 5);
                if(browserCursor < browserOffset)
                    browserOffset = browserCursor;
                if(browserCursor >= browserOffset + visible)
                    browserOffset = browserCursor - visible + 1;

                const FileEntry* currentEntry = entryAt(browserCursor);
                if(!currentEntry)
                    return true;
                if(currentEntry->isDirectory)
                {
                    std::string target = currentEntry->path;
                    navigateTo(ctx, std::move(target));
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
                    ctx.setStatusMessage("Usage: :mkdir <name>");
                }
                else
                {
                    std::filesystem::path dirPath =
                        (std::filesystem::path(currentDirectory) /
                         std::filesystem::path(args))
                            .lexically_normal();
                    std::error_code existsEc;
                    bool alreadyExisted =
                        std::filesystem::exists(dirPath, existsEc);
                    std::vector<std::string> newlyCreated;
                    if(!alreadyExisted)
                    {
                        std::filesystem::path walk;
                        for(const auto& part : dirPath)
                        {
                            walk /= part;
                            std::error_code chkEc;
                            if(!std::filesystem::exists(walk, chkEc))
                            {
                                newlyCreated.push_back(
                                    file_utils::path_to_utf8_string(
                                        walk.lexically_normal()));
                            }
                        }
                    }
                    std::error_code createEc;
                    if(!alreadyExisted)
                        std::filesystem::create_directories(dirPath, createEc);
                    if(!alreadyExisted && createEc)
                    {
                        ctx.setStatusMessage("Failed to create directory: " +
                                             createEc.message());
                    }
                    else
                    {
                        if(!newlyCreated.empty())
                        {
                            FileBrowserOp op;
                            op.kind = FileBrowserOp::Kind::Mkdir;
                            for(const auto& p : newlyCreated)
                                op.pairs.emplace_back(std::string(), p);
                            undoStack.push_back(std::move(op));
                            redoStack.clear();
                        }
                        std::filesystem::path parentPath = dirPath.parent_path();
                        if(parentPath.empty())
                            parentPath = dirPath;
                        if(filterActive)
                        {
                            filterActive = false;
                            filterQuery.clear();
                            filterMatches.clear();
                        }
                        navigateTo(
                            ctx, file_utils::path_to_utf8_string(parentPath));
                        std::string targetPath =
                            file_utils::path_to_utf8_string(dirPath);
                        for(int i = 0; i < (int)fileList.size(); ++i)
                        {
                            if(fileList[i].path == targetPath)
                            {
                                browserCursor = i;
                                int visible =
                                    std::max(1, ctx.screenRows() - 5);
                                if(browserCursor < browserOffset)
                                    browserOffset = browserCursor;
                                if(browserCursor >= browserOffset + visible)
                                    browserOffset =
                                        browserCursor - visible + 1;
                                break;
                            }
                        }
                        if(alreadyExisted)
                            ctx.setStatusMessage("Directory already exists");
                        else
                            ctx.setStatusMessage("Created directory: " + args);
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
                    ctx.setStatusMessage("Usage: :new <name>");
                    return true;
                }
                std::filesystem::path filePath =
                    (std::filesystem::path(currentDirectory) /
                     std::filesystem::path(args))
                        .lexically_normal();
                if(!filePath.has_filename() || filePath.filename().empty())
                {
                    ctx.setStatusMessage("Usage: :new <name>");
                    return true;
                }
                std::filesystem::path parent = filePath.parent_path();
                std::error_code parentEc;
                bool parentExists =
                    !parent.empty() &&
                    std::filesystem::is_directory(parent, parentEc);
                if(!parentExists)
                {
                    std::string rel;
                    size_t lastSlash =
                        args.find_last_of(keyCode(command::CommandKey::KEY_SLASH));
                    if(lastSlash != std::string::npos)
                        rel = args.substr(0, lastSlash);
                    else
                        rel = file_utils::path_to_utf8_string(parent);
                    pendingFilePath =
                        file_utils::path_to_utf8_string(filePath);
                    pendingParentRel = rel;
                    confirmingDirCreate = true;
                    ctx.setStatusMessage(
                        "Directory " + rel +
                        " doesn't exist, create? y/n [n default]");
                    return true;
                }
                std::error_code existsEc;
                if(std::filesystem::exists(filePath, existsEc))
                {
                    pendingFilePath =
                        file_utils::path_to_utf8_string(filePath);
                    confirmingFileReplace = true;
                    ctx.setStatusMessage(
                        "File exists, open or replace? o/r [o default]");
                    return true;
                }
                std::ofstream file(filePath);
                if(!file.is_open())
                {
                    ctx.setStatusMessage("Failed to create file: " + args);
                    return true;
                }
                file.close();
                std::string createdPath =
                    file_utils::path_to_utf8_string(filePath);
                std::string parentStr =
                    file_utils::path_to_utf8_string(parent.lexically_normal());
                if(parentStr != currentDirectory)
                    navigateTo(ctx, parentStr);
                else
                    loadDirectory(ctx, currentDirectory);
                for(int i = 0; i < (int)fileList.size(); ++i)
                {
                    if(fileList[i].path == createdPath)
                    {
                        browserCursor = i;
                        int visible = std::max(1, ctx.screenRows() - 5);
                        if(browserCursor < browserOffset)
                            browserOffset = browserCursor;
                        if(browserCursor >= browserOffset + visible)
                            browserOffset = browserCursor - visible + 1;
                        break;
                    }
                }
                ctx.setStatusMessage("Created file: " + args);
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
                        std::string pathStr =
                            file_utils::path_to_utf8_string(targetPath);
                        navigateTo(ctx, pathStr);
                        browserCursor = 0;
                        browserOffset = 0;
                        std::error_code chEc;
                        std::filesystem::current_path(pathStr, chEc);
                        if(!chEc)
                        {
                            ctx.setGrepFileIndexInitialized(false);
                            std::error_code cwdEc;
                            auto cwd =
                                std::filesystem::current_path(cwdEc);
                            if(!cwdEc)
                                ctx.setStatusMessage(cwd.string());
                        }
                    }
                    else
                    {
                        ctx.setStatusMessage("Not a directory: " + args);
                    }
                }
                return true;
            }
            if(cmd == "pwd")
            {
                std::error_code cwdEc;
                auto cwd = std::filesystem::current_path(cwdEc);
                if(!cwdEc)
                    ctx.setStatusMessage(cwd.string());
                else
                    ctx.setStatusMessage("Unable to read current directory");
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
                std::string rootCopy = root;
                navigateTo(ctx, rootCopy);
                browserCursor = 0;
                browserOffset = 0;
                std::error_code chEc;
                std::filesystem::current_path(rootCopy, chEc);
                if(!chEc)
                {
                    ctx.setGrepFileIndexInitialized(false);
                    std::error_code cwdEc;
                    auto cwd = std::filesystem::current_path(cwdEc);
                    if(!cwdEc)
                        ctx.setStatusMessage(cwd.string());
                }
                return true;
            }

            // =================================================================
            // Run shell command and show output
            // =================================================================
            if(cmd == "run")
            {
                if(args.empty())
                {
                    ctx.setStatusMessage("Usage: :run <shell command>");
                    return true;
                }
                std::string shellCmd = "cd ";
                auto quoteDir = [](const std::string& s) -> std::string
                {
                    std::string out;
                    out.reserve(s.size() + 2);
                    out += '\'';
                    for(char ch : s)
                    {
                        if(ch == '\'')
                            out += "'\\''";
                        else
                            out += ch;
                    }
                    out += '\'';
                    return out;
                };
                shellCmd += quoteDir(currentDirectory);
                shellCmd += " && { ";
                shellCmd += args;
                shellCmd += "; } 2>&1";

                std::vector<std::string> outputLines;
                FILE* pipe = popen(shellCmd.c_str(), "r");
                if(!pipe)
                {
                    ctx.setStatusMessage("Failed to run: " + args);
                    return true;
                }
                std::string current;
                char buf[4096];
                while(true)
                {
                    size_t n = fread(buf, 1, sizeof(buf), pipe);
                    for(size_t i = 0; i < n; ++i)
                    {
                        char ch = buf[i];
                        if(ch == '\n')
                        {
                            outputLines.push_back(std::move(current));
                            current.clear();
                        }
                        else if(ch == '\r')
                        {
                            // strip
                        }
                        else
                        {
                            current += ch;
                        }
                    }
                    if(n < sizeof(buf))
                        break;
                }
                if(!current.empty())
                    outputLines.push_back(std::move(current));
                int status = pclose(pipe);
                (void)status;
                if(outputLines.empty())
                    outputLines.push_back("(no output)");

                CommandOutputMode co(args, std::move(outputLines),
                                     currentDirectory, browserCursor,
                                     browserOffset, previousFile);
                nextState = ModeState{std::move(co)};
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
