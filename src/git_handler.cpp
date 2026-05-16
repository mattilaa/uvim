#include "git_handler.h"
#include "editor.h"
#include "editor_mode_controller.h"
#include "mode_state_machine.h"
#include "process_pipe.h"
#include "text_utils.h"
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits.h>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace
{
bool is_hex_token(const std::string& token)
{
    if(token.empty())
        return false;
    for(char c : token)
    {
        if(!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    }
    return true;
}

std::string trim_newline(std::string s)
{
    while(!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

std::string truncate_with_ellipsis(std::string_view text, int width)
{
    if(width <= 0)
        return "";
    if(text_utils::utf8DisplayWidth(text) <= width)
        return std::string(text);
    if(width <= 3)
        return std::string(width, '.');

    std::string out(text);
    while(!out.empty() && text_utils::utf8DisplayWidth(out) > width - 3)
    {
        int prev = text_utils::prevUtf8CharStart(out, (int)out.size());
        out.resize(prev);
    }
    out += "...";
    return out;
}

bool is_inside_git_repo(const std::string& filePath)
{
    fs::path path(filePath);
    std::string dir =
        path.has_parent_path() ? path.parent_path().string() : std::string(".");
    std::string cmd =
        "git -C \"" + dir + "\" rev-parse --is-inside-work-tree 2>/dev/null";
    ProcessPipe pipe(cmd, "r");
    if(!pipe)
        return false;
    char buffer[128];
    std::string out;
    if(fgets(buffer, sizeof(buffer), pipe.get()))
        out = trim_newline(buffer);
    return out == "true";
}

std::string git_root_for_dir(const std::string& dir)
{
    std::string cmd =
        "git -C \"" + dir + "\" rev-parse --show-toplevel 2>/dev/null";
    ProcessPipe pipe(cmd, "r");
    if(!pipe)
        return "";
    char buffer[512];
    std::string out;
    if(fgets(buffer, sizeof(buffer), pipe.get()))
        out = trim_newline(buffer);
    return out;
}

std::string base_dir_for_editor(const Editor* editor)
{
    std::string baseDir = ".";
    if(editor->currentBuffer && !editor->currentBuffer->filename.empty())
    {
        fs::path path(editor->currentBuffer->filename);
        baseDir = path.has_parent_path() ? path.parent_path().string()
                                         : std::string(".");
    }
    else if(!editor->projectRoot.empty())
    {
        baseDir = editor->projectRoot;
    }
    else
    {
        std::error_code cwdEc;
        auto cwd = std::filesystem::current_path(cwdEc);
        if(!cwdEc)
            baseDir = cwd.string();
    }
    return baseDir;
}

std::string format_git_date(const std::string& secondsText)
{
    if(secondsText.empty())
        return "";
    long long seconds = 0;
    auto begin = secondsText.data();
    auto end = secondsText.data() + secondsText.size();
    auto result = std::from_chars(begin, end, seconds);
    if(result.ec != std::errc())
        return "";
    std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
#ifdef _WIN32
    if(localtime_s(&tm, &t) != 0)
        return "";
#else
    if(!localtime_r(&t, &tm))
        return "";
#endif
    char buf[16];
    if(std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm) == 0)
        return "";
    return std::string(buf);
}

std::string blame_hash_for_line(const std::string& filePath, int line)
{
    fs::path path(filePath);
    std::string dir =
        path.has_parent_path() ? path.parent_path().string() : std::string(".");
    std::string cmd = "git -C \"" + dir + "\" blame --line-porcelain -L " +
                      std::to_string(line + 1) + "," +
                      std::to_string(line + 1) + " -- \"" + filePath +
                      "\" 2>/dev/null";
    ProcessPipe pipe(cmd, "r");
    if(!pipe)
        return "";
    char buffer[256];
    std::string firstLine;
    if(fgets(buffer, sizeof(buffer), pipe.get()))
        firstLine = trim_newline(buffer);
    if(firstLine.empty())
        return "";
    size_t space = firstLine.find(' ');
    std::string token =
        (space == std::string::npos) ? firstLine : firstLine.substr(0, space);
    if(!is_hex_token(token))
        return "";
    return token;
}
} // namespace

GitHandler::GitHandler(Editor* editor) : editor(editor) {}

bool GitHandler::ensureGitAvailable()
{
    if(gitAvailableKnown)
        return gitAvailable;
    gitAvailable = (std::system("git --version > /dev/null 2>&1") == 0);
    gitAvailableKnown = true;
    return gitAvailable;
}

void GitHandler::toggleGitBlame()
{
    editor->showGitBlame = !editor->showGitBlame;
    if(editor->showGitBlame)
    {
        if(!ensureGitAvailable())
        {
            editor->showGitBlame = false;
            editor->setStatusMessage("git not installed");
            return;
        }
        if(!editor->currentBuffer || editor->currentBuffer->filename.empty())
        {
            editor->showGitBlame = false;
            editor->setStatusMessage("git blame: no file");
            return;
        }
        if(!is_inside_git_repo(editor->currentBuffer->filename))
        {
            editor->showGitBlame = false;
            editor->setStatusMessage("git blame: not a repo");
            return;
        }
        if(editor->diagnosticPopupActive)
            editor->closeDiagnosticPopup();
        if(editor->currentBuffer)
            editor->currentBuffer->blameValid = false;
        updateGitBlameForVisibleRange();
    }
    editor->needsFullRedraw = true;
}

void GitHandler::updateGitBlameForVisibleRange()
{
    if(!editor->showGitBlame || !editor->currentBuffer ||
       editor->currentBuffer->filename.empty())
        return;

    fs::path path(editor->currentBuffer->filename);
    std::string dir =
        path.has_parent_path() ? path.parent_path().string() : std::string(".");
    std::string blameTarget = editor->currentBuffer->filename;
    std::string tempPath;
    if(editor->dirty && *editor->dirty)
    {
        tempPath = "/tmp/uvim_blame_" + std::to_string(getpid()) + ".tmp";
        std::ofstream tempFile(tempPath);
        if(tempFile.is_open())
        {
            for(size_t i = 0; i < editor->lines->size(); ++i)
            {
                tempFile << (*editor->lines)[i] << '\n';
            }
            tempFile.close();
        }
    }

    std::string cmd = "git -C \"" + dir + "\" blame --line-porcelain ";
    if(!tempPath.empty())
        cmd += "--contents \"" + tempPath + "\" ";
    cmd += "-- \"" + blameTarget + "\" 2>/dev/null";

    ProcessPipe pipe(cmd, "r");
    if(!pipe)
    {
        editor->setStatusMessage("git blame: failed to run");
        if(!tempPath.empty())
            unlink(tempPath.c_str());
        editor->currentBuffer->blameEntries.clear();
        editor->currentBuffer->blameStart = -1;
        editor->currentBuffer->blameEnd = -1;
        // Avoid retrying every frame on persistent failures.
        editor->currentBuffer->blameValid = true;
        return;
    }

    std::vector<Buffer::BlameEntry> entries;
    entries.reserve(editor->lines->size());
    std::string hash;
    std::string author;
    std::string authorTime;
    char buffer[512];
    while(fgets(buffer, sizeof(buffer), pipe.get()))
    {
        std::string line = trim_newline(buffer);
        if(line.empty())
            continue;
        if(line[0] == '\t')
        {
            Buffer::BlameEntry entry;
            entry.hash = hash;
            entry.author = author;
            entry.date = format_git_date(authorTime);
            entry.valid = !hash.empty();
            entries.push_back(std::move(entry));
            hash.clear();
            author.clear();
            authorTime.clear();
            continue;
        }
        if(line.rfind("author ", 0) == 0)
        {
            author = line.substr(7);
            continue;
        }
        if(line.rfind("author-time ", 0) == 0)
        {
            authorTime = line.substr(12);
            continue;
        }
        size_t space = line.find(' ');
        if(space != std::string::npos)
        {
            std::string token = line.substr(0, space);
            if(is_hex_token(token))
                hash = token;
        }
    }
    if(!tempPath.empty())
        unlink(tempPath.c_str());

    if((int)entries.size() != (int)editor->lines->size())
    {
        editor->setStatusMessage("git blame: failed");
        editor->currentBuffer->blameEntries.clear();
        editor->currentBuffer->blameStart = -1;
        editor->currentBuffer->blameEnd = -1;
        // Avoid retrying every frame on persistent failures.
        editor->currentBuffer->blameValid = true;
        return;
    }

    editor->currentBuffer->blameEntries = std::move(entries);
    editor->currentBuffer->blameStart = 0;
    editor->currentBuffer->blameEnd =
        (int)editor->currentBuffer->blameEntries.size() - 1;
    editor->currentBuffer->blameValid = true;
}

std::string GitHandler::blameDisplayForLine(int row) const
{
    if(!editor->showGitBlame || !editor->currentBuffer)
        return "";
    if(row < 0 || row >= (int)editor->currentBuffer->blameEntries.size())
        return "";
    const auto& entry = editor->currentBuffer->blameEntries[row];
    if(!entry.valid)
        return "";

    auto isUncommitted = [&](const Buffer::BlameEntry& e) -> bool
    {
        if(e.hash.empty())
            return false;
        for(char c : e.hash)
        {
            if(c != '0')
                return false;
        }
        return true;
    };

    std::string hash = entry.hash;
    if(hash.size() > 7)
        hash = hash.substr(0, 7);
    bool uncommitted = isUncommitted(entry);
    std::string out = uncommitted ? "Not committed" : hash;
    if(!uncommitted && !entry.author.empty())
        out += " " + entry.author;
    return truncate_with_ellipsis(out, Editor::kGitBlameMaxWidth);
}

std::string GitHandler::blameFullForLine(int row) const
{
    if(!editor->showGitBlame || !editor->currentBuffer)
        return "";
    if(row < 0 || row >= (int)editor->currentBuffer->blameEntries.size())
        return "";
    const auto& entry = editor->currentBuffer->blameEntries[row];
    if(!entry.valid)
        return "";

    auto isUncommitted = [&](const Buffer::BlameEntry& e) -> bool
    {
        if(e.hash.empty())
            return false;
        for(char c : e.hash)
        {
            if(c != '0')
                return false;
        }
        return true;
    };

    std::string out = isUncommitted(entry) ? "not committed" : entry.hash;
    if(!entry.author.empty())
        out += " " + entry.author;
    if(!entry.date.empty())
        out += " " + entry.date;
    return out;
}

void GitHandler::openGitShowCommitMode()
{
    if(!ensureGitAvailable())
    {
        editor->setStatusMessage("git not installed");
        return;
    }
    if(!editor->currentBuffer || editor->currentBuffer->filename.empty())
    {
        editor->setStatusMessage("git show: no file");
        return;
    }
    if(!is_inside_git_repo(editor->currentBuffer->filename))
    {
        editor->setStatusMessage("git show: not a repo");
        return;
    }

    std::string hash;
    int row = *editor->cursorY;
    if(editor->currentBuffer->blameValid &&
       row >= editor->currentBuffer->blameStart &&
       row <= editor->currentBuffer->blameEnd &&
       row < (int)editor->currentBuffer->blameEntries.size())
    {
        const auto& entry = editor->currentBuffer->blameEntries[row];
        if(entry.valid)
            hash = entry.hash;
    }
    if(hash.empty())
        hash = blame_hash_for_line(editor->currentBuffer->filename, row);
    if(hash.empty())
    {
        editor->setStatusMessage("git show: no blame hash");
        return;
    }

    std::vector<std::string> linesOut = loadGitShowLines(hash);
    if(linesOut.empty())
    {
        editor->setStatusMessage("git show: no output");
        return;
    }

    if(editor->modeStateMachine)
    {
        editor->modeStateMachine->transitionTo(
            GitShowCommitMode{hash, std::move(linesOut)});
        editor->modeController->syncModeFromStateMachine();
        editor->needsFullRedraw = true;
    }
}

void GitHandler::openGitDiffMode()
{
    if(!ensureGitAvailable())
    {
        editor->setStatusMessage("git not installed");
        return;
    }

    std::string dir = base_dir_for_editor(editor);
    std::string repoRoot = git_root_for_dir(dir);
    if(repoRoot.empty())
    {
        editor->setStatusMessage("git diff: not a repo");
        return;
    }

    std::string cmd =
        "git -C \"" + repoRoot + "\" --no-pager diff " +
        std::string(editor->gitUseDefaultColors ? "--color=always "
                                                : "--no-color ") +
        "2>/dev/null";

    ProcessPipe pipe(cmd, "r");
    if(!pipe)
    {
        editor->setStatusMessage("git diff: failed");
        return;
    }

    std::string output;
    char buffer[1024];
    while(fgets(buffer, sizeof(buffer), pipe.get()))
        output += buffer;

    if(output.empty())
    {
        editor->setStatusMessage("git diff: no output");
        return;
    }

    std::vector<std::string> linesOut;
    size_t pos = 0;
    while(pos <= output.size())
    {
        size_t next = output.find('\n', pos);
        if(next == std::string::npos)
        {
            linesOut.push_back(output.substr(pos));
            break;
        }
        linesOut.push_back(output.substr(pos, next - pos));
        pos = next + 1;
    }

    if(editor->modeStateMachine)
    {
        editor->modeStateMachine->transitionTo(
            GitShowCommitMode{"DIFF", std::move(linesOut)});
        editor->modeController->syncModeFromStateMachine();
        editor->needsFullRedraw = true;
    }
}

void GitHandler::openGitCommitMode()
{
    if(!ensureGitAvailable())
    {
        editor->setStatusMessage("git not installed");
        return;
    }

    std::string dir = base_dir_for_editor(editor);
    std::string repoRoot = git_root_for_dir(dir);
    if(repoRoot.empty())
    {
        editor->setStatusMessage("git commit: not a repo");
        return;
    }

    if(editor->modeStateMachine)
    {
        editor->modeStateMachine->transitionTo(
            GitCommitMode{repoRoot, repoRoot});
        editor->modeController->syncModeFromStateMachine();
        editor->needsFullRedraw = true;
    }
}

std::vector<std::string> GitHandler::loadGitShowLines(const std::string& hash)
{
    std::string dir = base_dir_for_editor(editor);
    std::string repoRoot = git_root_for_dir(dir);
    if(repoRoot.empty())
    {
        // Fallback to current buffer directory for non-standard layouts.
        if(!editor->currentBuffer || editor->currentBuffer->filename.empty())
            return {};
        fs::path path(editor->currentBuffer->filename);
        repoRoot = path.has_parent_path() ? path.parent_path().string()
                                          : std::string(".");
    }

    std::string cmd =
        "git -C \"" + repoRoot + "\" --no-pager show " +
        std::string(editor->gitUseDefaultColors ? "--color=always "
                                                : "--no-color ") +
        hash + " 2>/dev/null";

    ProcessPipe pipe(cmd, "r");
    if(!pipe)
        return {};

    std::string output;
    char buffer[1024];
    while(fgets(buffer, sizeof(buffer), pipe.get()))
        output += buffer;

    if(output.empty())
        return {};

    std::vector<std::string> linesOut;
    size_t pos = 0;
    while(pos <= output.size())
    {
        size_t next = output.find('\n', pos);
        if(next == std::string::npos)
        {
            linesOut.push_back(output.substr(pos));
            break;
        }
        linesOut.push_back(output.substr(pos, next - pos));
        pos = next + 1;
    }

    return linesOut;
}

void GitHandler::openGitLogMode()
{
    if(!ensureGitAvailable())
    {
        editor->setStatusMessage("git not installed");
        return;
    }
    std::string dir = base_dir_for_editor(editor);
    std::string repoRoot = git_root_for_dir(dir);
    if(repoRoot.empty())
    {
        editor->setStatusMessage("git log: not a repo");
        return;
    }

    std::string cmd =
        "git -C \"" + repoRoot +
        "\" --no-pager log --no-color --date=format:%Y-%m-%d\\ %H:%M\\ %z "
        "--pretty=format:%H%x1f%ad%x1f%an%x1f%s 2>/dev/null";

    ProcessPipe pipe(cmd, "r");
    if(!pipe)
    {
        editor->setStatusMessage("git log: failed to run");
        return;
    }

    std::vector<GitLogMode::Entry> entries;
    char buffer[1024];
    while(fgets(buffer, sizeof(buffer), pipe.get()))
    {
        std::string line = trim_newline(buffer);
        if(line.empty())
            continue;
        constexpr char sep = '\x1f';
        size_t tab = line.find(sep);
        if(tab == std::string::npos)
            continue;
        GitLogMode::Entry entry;
        size_t tab2 = line.find(sep, tab + 1);
        size_t tab3 = (tab2 == std::string::npos)
                          ? std::string::npos
                          : line.find(sep, tab2 + 1);
        entry.hash = line.substr(0, tab);
        if(tab2 != std::string::npos)
            entry.date = line.substr(tab + 1, tab2 - (tab + 1));
        if(tab2 != std::string::npos && tab3 != std::string::npos)
            entry.author = line.substr(tab2 + 1, tab3 - (tab2 + 1));
        if(tab3 != std::string::npos)
            entry.subject = line.substr(tab3 + 1);
        else if(tab2 != std::string::npos)
            entry.subject = line.substr(tab2 + 1);
        else
            entry.subject = line.substr(tab + 1);
        entries.push_back(std::move(entry));
    }
    if(entries.empty())
    {
        editor->setStatusMessage("git log: no output");
        return;
    }

    if(editor->modeStateMachine)
    {
        editor->modeStateMachine->transitionTo(
            GitLogMode{std::move(entries), false, repoRoot, repoRoot, {}});
        editor->modeController->syncModeFromStateMachine();
        editor->needsFullRedraw = true;
    }
}

void GitHandler::openGitPrettyLogMode()
{
    if(!ensureGitAvailable())
    {
        editor->setStatusMessage("git not installed");
        return;
    }
    std::string dir = base_dir_for_editor(editor);
    std::string repoRoot = git_root_for_dir(dir);
    if(repoRoot.empty())
    {
        editor->setStatusMessage("git prettylog: not a repo");
        return;
    }

    std::string cmd =
        "git -C \"" + repoRoot +
        "\" --no-pager log --no-color --date=format:%Y-%m-%d\\ %H:%M\\ %z "
        "--pretty=format:%H%x1f%ad%x1f%an%x1f%s 2>/dev/null";

    ProcessPipe pipe(cmd, "r");
    if(!pipe)
    {
        editor->setStatusMessage("git prettylog: failed to run");
        return;
    }

    std::vector<GitLogMode::Entry> entries;
    char buffer[1024];
    while(fgets(buffer, sizeof(buffer), pipe.get()))
    {
        std::string line = trim_newline(buffer);
        if(line.empty())
            continue;
        constexpr char sep = '\x1f';
        size_t tab = line.find(sep);
        if(tab == std::string::npos)
            continue;
        GitLogMode::Entry entry;
        size_t tab2 = line.find(sep, tab + 1);
        size_t tab3 = (tab2 == std::string::npos)
                          ? std::string::npos
                          : line.find(sep, tab2 + 1);
        entry.hash = line.substr(0, tab);
        if(tab2 != std::string::npos)
            entry.date = line.substr(tab + 1, tab2 - (tab + 1));
        if(tab2 != std::string::npos && tab3 != std::string::npos)
            entry.author = line.substr(tab2 + 1, tab3 - (tab2 + 1));
        if(tab3 != std::string::npos)
            entry.subject = line.substr(tab3 + 1);
        else if(tab2 != std::string::npos)
            entry.subject = line.substr(tab2 + 1);
        else
            entry.subject = line.substr(tab + 1);
        entries.push_back(std::move(entry));
    }
    if(entries.empty())
    {
        editor->setStatusMessage("git prettylog: no output");
        return;
    }

    if(editor->modeStateMachine)
    {
        editor->modeStateMachine->transitionTo(
            GitLogMode{std::move(entries), false, repoRoot, repoRoot, {}, true});
        editor->modeController->syncModeFromStateMachine();
        editor->needsFullRedraw = true;
    }
}

void GitHandler::openGitLogModeForFile()
{
    if(!ensureGitAvailable())
    {
        editor->setStatusMessage("git not installed");
        return;
    }
    if(!editor->currentBuffer || editor->currentBuffer->filename.empty())
    {
        editor->setStatusMessage("git log: no file");
        return;
    }
    if(!is_inside_git_repo(editor->currentBuffer->filename))
    {
        editor->setStatusMessage("git log: not a repo");
        return;
    }

    fs::path path(editor->currentBuffer->filename);
    std::string dir =
        path.has_parent_path() ? path.parent_path().string() : std::string(".");
    std::string repoRoot = git_root_for_dir(dir);
    if(repoRoot.empty())
    {
        editor->setStatusMessage("git log: not a repo");
        return;
    }
    std::string cmd =
        "git -C \"" + dir +
        "\" --no-pager log --no-color --pretty=format:%H\\\t%s -- \"" +
        editor->currentBuffer->filename + "\" 2>/dev/null";

    ProcessPipe pipe(cmd, "r");
    if(!pipe)
    {
        editor->setStatusMessage("git log: failed to run");
        return;
    }

    std::vector<GitLogMode::Entry> entries;
    char buffer[1024];
    while(fgets(buffer, sizeof(buffer), pipe.get()))
    {
        std::string line = trim_newline(buffer);
        if(line.empty())
            continue;
        size_t tab = line.find('\t');
        if(tab == std::string::npos)
            continue;
        GitLogMode::Entry entry;
        entry.hash = line.substr(0, tab);
        entry.subject = line.substr(tab + 1);
        entries.push_back(std::move(entry));
    }
    if(entries.empty())
    {
        editor->setStatusMessage("git log: no output");
        return;
    }

    if(editor->modeStateMachine)
    {
        editor->modeStateMachine->transitionTo(
            GitLogMode{std::move(entries), true, repoRoot, repoRoot,
                       editor->currentBuffer->filename});
        editor->modeController->syncModeFromStateMachine();
        editor->needsFullRedraw = true;
    }
}

void GitHandler::openGitStageMode()
{
    if(!ensureGitAvailable())
    {
        editor->setStatusMessage("git not installed");
        return;
    }

    std::string baseDir = base_dir_for_editor(editor);
    std::string repoRoot = git_root_for_dir(baseDir);
    if(repoRoot.empty())
    {
        editor->setStatusMessage("git stage: not a repo");
        return;
    }

    if(editor->modeStateMachine)
    {
        editor->modeStateMachine->transitionTo(
            GitStageMode{{}, repoRoot, repoRoot});
        editor->modeController->syncModeFromStateMachine();
        editor->needsFullRedraw = true;
    }
}

void GitHandler::addCurrentBuffer()
{
    if(!ensureGitAvailable())
    {
        editor->setStatusMessage("git not installed");
        return;
    }
    if(!editor->currentBuffer || editor->currentBuffer->filename.empty())
    {
        editor->setStatusMessage("git add: no file");
        return;
    }
    if(!is_inside_git_repo(editor->currentBuffer->filename))
    {
        editor->setStatusMessage("git add: not a repo");
        return;
    }

    std::string dir = base_dir_for_editor(editor);
    std::string repoRoot = git_root_for_dir(dir);
    if(repoRoot.empty())
    {
        editor->setStatusMessage("git add: not a repo");
        return;
    }

    std::string cmd = "git -C \"" + repoRoot + "\" add -- \"" +
                      editor->currentBuffer->filename + "\" 2>/dev/null";
    int result = std::system(cmd.c_str());
    if(result == 0)
        editor->setStatusMessage("git add: added");
    else
        editor->setStatusMessage("git add: failed");
}

std::optional<bool> GitHandler::currentBufferHasChanges()
{
    if(!ensureGitAvailable())
        return std::nullopt;
    if(!editor->currentBuffer || editor->currentBuffer->filename.empty())
        return std::nullopt;
    if(!is_inside_git_repo(editor->currentBuffer->filename))
        return std::nullopt;

    std::string dir = base_dir_for_editor(editor);
    std::string repoRoot = git_root_for_dir(dir);
    if(repoRoot.empty())
        return std::nullopt;

    std::string cmd = "git -C \"" + repoRoot +
                      "\" status --short -- \"" +
                      editor->currentBuffer->filename + "\" 2>/dev/null";
    ProcessPipe pipe(cmd, "r");
    if(!pipe)
        return std::nullopt;

    char buffer[512];
    bool hasChanges = false;
    while(fgets(buffer, sizeof(buffer), pipe.get()))
    {
        std::string line = trim_newline(buffer);
        if(!line.empty())
        {
            hasChanges = true;
            break;
        }
    }
    return hasChanges;
}

bool GitHandler::runGitStash(std::string& outMessage)
{
    if(!ensureGitAvailable())
    {
        outMessage = "git not installed";
        return false;
    }

    std::string baseDir = base_dir_for_editor(editor);
    std::string repoRoot = git_root_for_dir(baseDir);
    if(repoRoot.empty())
    {
        outMessage = "git stash: not a repo";
        return false;
    }

    std::string cmd =
        "git -C \"" + repoRoot + "\" stash 2>/dev/null";
    ProcessPipe pipe(cmd, "r");
    if(!pipe)
    {
        outMessage = "git stash: failed";
        return false;
    }
    char buffer[512];
    std::string output;
    if(fgets(buffer, sizeof(buffer), pipe.get()))
        output = trim_newline(buffer);

    if(output.empty())
        outMessage = "git stash: done";
    else
        outMessage = output;
    return true;
}

bool GitHandler::runGitStashPop(std::string& outMessage)
{
    if(!ensureGitAvailable())
    {
        outMessage = "git not installed";
        return false;
    }

    std::string baseDir = base_dir_for_editor(editor);
    std::string repoRoot = git_root_for_dir(baseDir);
    if(repoRoot.empty())
    {
        outMessage = "git stash pop: not a repo";
        return false;
    }

    std::string cmd =
        "git -C \"" + repoRoot + "\" stash pop 2>/dev/null";
    ProcessPipe pipe(cmd, "r");
    if(!pipe)
    {
        outMessage = "git stash pop: failed";
        return false;
    }
    char buffer[512];
    std::string output;
    if(fgets(buffer, sizeof(buffer), pipe.get()))
        output = trim_newline(buffer);

    if(output.empty())
        outMessage = "git stash pop: done";
    else
        outMessage = output;
    return true;
}
