#include "git_handler.h"
#include "editor.h"
#include "mode_state_machine.h"
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits.h>
#include <string>
#include <unistd.h>

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

bool is_inside_git_repo(const std::string& filePath)
{
    fs::path path(filePath);
    std::string dir =
        path.has_parent_path() ? path.parent_path().string() : std::string(".");
    std::string cmd =
        "git -C \"" + dir + "\" rev-parse --is-inside-work-tree 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
        return false;
    char buffer[128];
    std::string out;
    if(fgets(buffer, sizeof(buffer), pipe))
        out = trim_newline(buffer);
    pclose(pipe);
    return out == "true";
}

std::string git_root_for_dir(const std::string& dir)
{
    std::string cmd =
        "git -C \"" + dir + "\" rev-parse --show-toplevel 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
        return "";
    char buffer[512];
    std::string out;
    if(fgets(buffer, sizeof(buffer), pipe))
        out = trim_newline(buffer);
    pclose(pipe);
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
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
            baseDir = cwd;
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
    if(!localtime_r(&t, &tm))
        return "";
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
    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
        return "";
    char buffer[256];
    std::string firstLine;
    if(fgets(buffer, sizeof(buffer), pipe))
        firstLine = trim_newline(buffer);
    pclose(pipe);
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

    FILE* pipe = popen(cmd.c_str(), "r");
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
    while(fgets(buffer, sizeof(buffer), pipe))
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
    pclose(pipe);
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
    std::string out = isUncommitted(entry) ? "not committed" : hash;
    if(!entry.author.empty())
        out += " " + entry.author;
    if(!entry.date.empty())
        out += " " + entry.date;
    if((int)out.size() > Editor::kGitBlameWidth)
        out.resize(Editor::kGitBlameWidth);
    return out;
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
        editor->syncModeFromStateMachine();
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

    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
    {
        editor->setStatusMessage("git diff: failed");
        return;
    }

    std::string output;
    char buffer[1024];
    while(fgets(buffer, sizeof(buffer), pipe))
        output += buffer;
    pclose(pipe);

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
        editor->syncModeFromStateMachine();
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
        editor->syncModeFromStateMachine();
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

    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
        return {};

    std::string output;
    char buffer[1024];
    while(fgets(buffer, sizeof(buffer), pipe))
        output += buffer;
    pclose(pipe);

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
        "\" --no-pager log --no-color --pretty=format:%H\\\t%s 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
    {
        editor->setStatusMessage("git log: failed to run");
        return;
    }

    std::vector<GitLogMode::Entry> entries;
    char buffer[1024];
    while(fgets(buffer, sizeof(buffer), pipe))
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
    pclose(pipe);

    if(entries.empty())
    {
        editor->setStatusMessage("git log: no output");
        return;
    }

    if(editor->modeStateMachine)
    {
        editor->modeStateMachine->transitionTo(
            GitLogMode{std::move(entries), false, repoRoot, repoRoot, {}});
        editor->syncModeFromStateMachine();
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
        "\" --no-pager log --no-color --pretty=format:%H\\\t%s 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
    {
        editor->setStatusMessage("git prettylog: failed to run");
        return;
    }

    std::vector<GitLogMode::Entry> entries;
    char buffer[1024];
    while(fgets(buffer, sizeof(buffer), pipe))
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
    pclose(pipe);

    if(entries.empty())
    {
        editor->setStatusMessage("git prettylog: no output");
        return;
    }

    if(editor->modeStateMachine)
    {
        editor->modeStateMachine->transitionTo(
            GitLogMode{std::move(entries), false, repoRoot, repoRoot, {}, true});
        editor->syncModeFromStateMachine();
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

    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
    {
        editor->setStatusMessage("git log: failed to run");
        return;
    }

    std::vector<GitLogMode::Entry> entries;
    char buffer[1024];
    while(fgets(buffer, sizeof(buffer), pipe))
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
    pclose(pipe);

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
        editor->syncModeFromStateMachine();
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
        editor->syncModeFromStateMachine();
        editor->needsFullRedraw = true;
    }
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
    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
    {
        outMessage = "git stash: failed";
        return false;
    }
    char buffer[512];
    std::string output;
    if(fgets(buffer, sizeof(buffer), pipe))
        output = trim_newline(buffer);
    pclose(pipe);

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
    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
    {
        outMessage = "git stash pop: failed";
        return false;
    }
    char buffer[512];
    std::string output;
    if(fgets(buffer, sizeof(buffer), pipe))
        output = trim_newline(buffer);
    pclose(pipe);

    if(output.empty())
        outMessage = "git stash pop: done";
    else
        outMessage = output;
    return true;
}
