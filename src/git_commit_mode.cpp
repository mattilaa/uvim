#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace
{
constexpr int COMMIT_SOFT_LIMIT = 52;

std::string trim_newline(std::string s)
{
    while(!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

std::vector<std::string> run_git_lines(const std::string& cmd)
{
    std::vector<std::string> out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
        return out;

    std::string output;
    char buffer[1024];
    while(fgets(buffer, sizeof(buffer), pipe))
        output += buffer;
    pclose(pipe);

    size_t pos = 0;
    while(pos <= output.size())
    {
        size_t next = output.find('\n', pos);
        if(next == std::string::npos)
        {
            if(pos < output.size())
                out.push_back(output.substr(pos));
            break;
        }
        out.push_back(output.substr(pos, next - pos));
        pos = next + 1;
    }
    return out;
}

bool reload_git_log_mode(GitLogMode& mode)
{
    const std::string repoDir = !mode.repoDir.empty() ? mode.repoDir : mode.repoRoot;
    if(repoDir.empty())
        return false;

    std::string cmd = "git -C \"" + repoDir +
                      "\" --no-pager log --no-color --pretty=format:%H\\\t%s";
    if(mode.fileOnly && !mode.filePath.empty())
        cmd += " -- \"" + mode.filePath + "\"";
    cmd += " 2>/dev/null";

    auto lines = run_git_lines(cmd);
    std::vector<GitLogMode::Entry> entries;
    entries.reserve(lines.size());
    for(const auto& raw : lines)
    {
        std::string line = trim_newline(raw);
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
        return false;

    mode.entries = std::move(entries);
    mode.filtered.clear();
    mode.cursor = 0;
    mode.scrollOffset = 0;
    mode.diffOffset = 0;
    mode.diffHorizontalOffset = 0;
    mode.previewLines.clear();
    mode.previewHash.clear();
    mode.diffDirty = true;
    return true;
}

std::string join_lines(const std::vector<std::string>& lines)
{
    std::string out;
    for(size_t i = 0; i < lines.size(); ++i)
    {
        out += lines[i];
        if(i + 1 < lines.size())
            out += '\n';
    }
    return out;
}

bool is_comment_line(const std::string& line)
{
    size_t i = 0;
    while(i < line.size() &&
          std::isspace(static_cast<unsigned char>(line[i])))
        ++i;
    return i < line.size() && line[i] == '#';
}

bool is_blank_line(const std::string& line)
{
    for(char ch : line)
    {
        if(!std::isspace(static_cast<unsigned char>(ch)))
            return false;
    }
    return true;
}

std::vector<std::string> build_comment_lines(
    const std::vector<std::string>& stagedLines, bool hasStagedFiles,
    const std::string& branch, GitCommitMode::Action action,
    const std::string& revertHash, const std::string& revertSubject)
{
    std::vector<std::string> out;
    out.push_back(
        "# Please enter the commit message for your changes. Lines starting");
    out.push_back(
        "# with '#' will be ignored, and an empty message aborts the commit.");
    out.push_back("#");
    out.push_back("# On branch " + (branch.empty() ? std::string("(unknown)") : branch));
    out.push_back("#");
    if(action == GitCommitMode::Action::RevertCommit)
    {
        out.push_back("# You are currently reverting commit " + revertHash + ".");
        out.push_back("#");
        out.push_back("# Commit to revert:");
        out.push_back("#   " + revertSubject);
        out.push_back("#");
    }
    out.push_back("# Changes to be committed:");
    if(!hasStagedFiles)
    {
        out.push_back("#   (no staged files)");
        return out;
    }

    for(const std::string& line : stagedLines)
    {
        if(line.empty())
            continue;

        char status = line[0];
        std::string path = line;
        if(line.size() > 1)
        {
            size_t tab = line.find('\t');
            size_t splitPos =
                (tab != std::string::npos) ? tab : line.find(' ');
            if(splitPos != std::string::npos && splitPos + 1 < line.size())
                path = line.substr(splitPos + 1);
        }

        std::string kind = "modified";
        if(status == 'A')
            kind = "new file";
        else if(status == 'D')
            kind = "deleted";
        else if(status == 'R')
            kind = "renamed";
        else if(status == 'C')
            kind = "copied";
        else if(status == 'M')
            kind = "modified";

        out.push_back("#   " + kind + ":   " + path);
    }
    return out;
}
} // namespace

void GitCommitMode::refreshStaged()
{
    stagedLines.clear();
    currentBranch.clear();
    hasStagedFiles = false;
    if(repoDir.empty())
    {
        stagedLines.push_back("(git commit: repo unavailable)");
        stagedDirty = false;
        return;
    }

    {
        std::string branchCmd = "git -C \"" + repoDir +
                                "\" rev-parse --abbrev-ref HEAD 2>/dev/null";
        auto branchLines = run_git_lines(branchCmd);
        if(!branchLines.empty())
            currentBranch = trim_newline(branchLines.front());
    }

    std::string cmd =
        "git -C \"" + repoDir +
        "\" --no-pager diff --cached --name-status --no-color 2>/dev/null";
    auto lines = run_git_lines(cmd);
    for(auto& line : lines)
    {
        line = trim_newline(line);
        if(!line.empty())
        {
            hasStagedFiles = true;
            stagedLines.push_back(line);
        }
    }

    if(stagedLines.empty())
        stagedLines.push_back("(no staged files)");

    stagedDirty = false;
}

void GitCommitMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    if(ed)
        ed->cancelCommandPopup();
    if(stagedDirty)
        refreshStaged();
    if(action == Action::RevertCommit &&
       (messageLines.empty() ||
        (messageLines.size() == 1 && messageLines[0].empty())))
    {
        messageLines = {"Revert \"" + revertSubject + "\"",
                        "",
                        "This reverts commit " + revertHash + "."};
    }
    if(messageLines.empty())
        messageLines.push_back("");
    messageCursorRow = std::clamp(messageCursorRow, 0,
                                  std::max(0, (int)messageLines.size() - 1));
    messageCursorCol = std::clamp(
        messageCursorCol, 0, (int)messageLines[messageCursorRow].size());
    ctx.requestFullRedraw();
}

void GitCommitMode::on_exit(ModeContext& ctx)
{
    ctx.requestFullRedraw();
}

std::optional<ModeState> GitCommitMode::handle(ModeContext& ctx,
                                               const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    int contentRows = std::max(1, ed->screenRows - 2);
    auto clamp_cursor = [&]()
    {
        if(messageLines.empty())
            messageLines.push_back("");
        messageCursorRow = std::clamp(messageCursorRow, 0,
                                      std::max(0, (int)messageLines.size() - 1));
        messageCursorCol = std::clamp(
            messageCursorCol, 0, (int)messageLines[messageCursorRow].size());

        if(messageCursorRow < messageTopRow)
            messageTopRow = messageCursorRow;
        if(messageCursorRow >= messageTopRow + contentRows)
            messageTopRow = messageCursorRow - contentRows + 1;
        messageTopRow = std::max(0, messageTopRow);
    };

    auto commit_now = [&]() -> bool
    {
        if(stagedDirty)
            refreshStaged();
        if(action == Action::CommitStaged && !hasStagedFiles)
        {
            ed->setStatusMessage("git commit: no staged files");
            return false;
        }

        std::vector<std::string> commitLines;
        commitLines.reserve(messageLines.size());
        for(const std::string& line : messageLines)
        {
            if(!is_comment_line(line))
                commitLines.push_back(line);
        }
        while(!commitLines.empty() && is_blank_line(commitLines.back()))
            commitLines.pop_back();

        std::string msg = join_lines(commitLines);
        if(msg.empty())
        {
            ed->setStatusMessage("git commit: message required");
            return false;
        }

        if(action == Action::RevertCommit)
        {
            if(revertHash.empty())
            {
                ed->setStatusMessage("git revert: missing target");
                return false;
            }

            std::string revertCmd = "git -C \"" + repoDir +
                                    "\" revert --no-commit \"" + revertHash +
                                    "\" 2>/dev/null";
            int revertStatus = system(revertCmd.c_str());
            if(revertStatus != 0)
            {
                ed->setStatusMessage("git revert: failed");
                return false;
            }

            std::string commitCmd =
                "git -C \"" + repoDir + "\" commit -F - 2>/dev/null";
            FILE* pipe = popen(commitCmd.c_str(), "w");
            if(!pipe)
            {
                system(("git -C \"" + repoDir + "\" revert --abort 2>/dev/null")
                           .c_str());
                ed->setStatusMessage("git revert: failed");
                return false;
            }
            fwrite(msg.data(), 1, msg.size(), pipe);
            fwrite("\n", 1, 1, pipe);
            int commitStatus = pclose(pipe);
            if(commitStatus != 0)
            {
                system(("git -C \"" + repoDir + "\" revert --abort 2>/dev/null")
                           .c_str());
                ed->setStatusMessage("git revert: failed");
                return false;
            }

            ed->setStatusMessage("git revert: done");
            return true;
        }
        else
        {
            std::string cmd = "git -C \"" + repoDir + "\" commit -F - 2>/dev/null";
            FILE* pipe = popen(cmd.c_str(), "w");
            if(!pipe)
            {
                ed->setStatusMessage("git commit: failed");
                return false;
            }
            fwrite(msg.data(), 1, msg.size(), pipe);
            fwrite("\n", 1, 1, pipe);
            int status = pclose(pipe);
            if(status != 0)
            {
                ed->setStatusMessage("git commit: failed");
                return false;
            }

            ed->setStatusMessage("git commit: done");
            return true;
        }
    };

    auto return_after_done = [&](bool refreshLog) -> ModeState
    {
        if(returnLog.has_value())
        {
            if(refreshLog)
                reload_git_log_mode(*returnLog);
            return *returnLog;
        }
        return GitStageMode{{}, repoRoot, repoDir};
    };

    if(commandActive)
    {
        if(c == Terminal::ESC)
        {
            commandActive = false;
            commandLine.clear();
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
        {
            if(!commandLine.empty())
                commandLine.pop_back();
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == Terminal::ENTER)
        {
            std::string cmd = trim_newline(commandLine);
            commandActive = false;
            commandLine.clear();
            if(cmd == "q" || cmd == "q!")
                return return_after_done(false);
            if(cmd == "wq" || cmd == "x")
            {
                if(commit_now())
                    return return_after_done(true);
            }
            else
            {
                ed->setStatusMessage("git commit: unknown command");
            }
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c >= 32 && c <= 126)
        {
            commandLine.push_back((char)c);
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        return std::nullopt;
    }

    if(c == Terminal::ESC && insertMode)
    {
        insertMode = false;
        ed->needsFullRedraw = true;
        return std::nullopt;
    }
    if(c == ':' && !insertMode)
    {
        commandActive = true;
        commandLine.clear();
        ed->needsFullRedraw = true;
        return std::nullopt;
    }
    if(c == Terminal::CTRL_R)
    {
        stagedDirty = true;
        refreshStaged();
        ed->needsFullRedraw = true;
        return std::nullopt;
    }

    if(!insertMode)
    {
        if(c == 'q')
            return return_after_done(false);
        if(c == 'h')
        {
            if(messageCursorCol > 0)
                messageCursorCol--;
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == 'l')
        {
            if(messageCursorCol < (int)messageLines[messageCursorRow].size())
                messageCursorCol++;
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == '0')
        {
            messageCursorCol = 0;
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == '$')
        {
            messageCursorCol = (int)messageLines[messageCursorRow].size();
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == 'j')
        {
            if(messageCursorRow + 1 < (int)messageLines.size())
            {
                messageCursorRow++;
                messageCursorCol = std::min(
                    messageCursorCol, (int)messageLines[messageCursorRow].size());
                clamp_cursor();
            }
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == 'k')
        {
            if(messageCursorRow > 0)
            {
                messageCursorRow--;
                messageCursorCol = std::min(
                    messageCursorCol, (int)messageLines[messageCursorRow].size());
                clamp_cursor();
            }
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == 'g')
        {
            int next = Terminal::readKey();
            if(next == 'g')
            {
                messageCursorRow = 0;
                messageCursorCol = std::min(
                    messageCursorCol, (int)messageLines[messageCursorRow].size());
                clamp_cursor();
                ed->needsFullRedraw = true;
            }
            return std::nullopt;
        }
        if(c == 'G')
        {
            messageCursorRow = std::max(0, (int)messageLines.size() - 1);
            messageCursorCol = std::min(
                messageCursorCol, (int)messageLines[messageCursorRow].size());
            clamp_cursor();
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == 'x')
        {
            std::string& line = messageLines[messageCursorRow];
            if(messageCursorCol < (int)line.size())
                line.erase(messageCursorCol, 1);
            messageCursorCol = std::min(messageCursorCol, (int)line.size());
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == 'O')
        {
            messageLines.insert(messageLines.begin() + messageCursorRow, "");
            messageCursorCol = 0;
            insertMode = true;
            clamp_cursor();
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        auto is_word_char = [](char ch)
        {
            return std::isalnum((unsigned char)ch) || ch == '_';
        };
        if(c == 'b' || c == 'B' || c == 'e' || c == 'E')
        {
            const std::string& line = messageLines[messageCursorRow];
            if(!line.empty())
            {
                int pos = messageCursorCol;
                int n = (int)line.size();
                bool bigWord = (c == 'B' || c == 'E');
                bool endMotion = (c == 'e' || c == 'E');

                auto is_space = [](char ch)
                { return ch == ' ' || ch == '\t'; };

                if(c == 'b' || c == 'B')
                {
                    if(pos > 0)
                        --pos;
                    while(pos > 0 && is_space(line[pos]))
                        --pos;
                    if(bigWord)
                    {
                        while(pos > 0 && !is_space(line[pos - 1]))
                            --pos;
                    }
                    else
                    {
                        bool inWord = is_word_char(line[pos]);
                        while(pos > 0 && !is_space(line[pos - 1]) &&
                              is_word_char(line[pos - 1]) == inWord)
                            --pos;
                    }
                    messageCursorCol = pos;
                }
                else if(endMotion)
                {
                    if(pos >= n)
                        pos = n - 1;
                    while(pos < n && is_space(line[pos]))
                        ++pos;
                    if(pos < n)
                    {
                        if(bigWord)
                        {
                            while(pos + 1 < n && !is_space(line[pos + 1]))
                                ++pos;
                        }
                        else
                        {
                            bool inWord = is_word_char(line[pos]);
                            while(pos + 1 < n && !is_space(line[pos + 1]) &&
                                  is_word_char(line[pos + 1]) == inWord)
                                ++pos;
                        }
                        messageCursorCol = pos;
                    }
                }
            }
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == 'i')
        {
            insertMode = true;
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == 'A')
        {
            messageCursorCol = (int)messageLines[messageCursorRow].size();
            insertMode = true;
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == 'o')
        {
            messageLines.insert(messageLines.begin() + messageCursorRow + 1, "");
            messageCursorRow++;
            messageCursorCol = 0;
            insertMode = true;
            clamp_cursor();
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == 'd')
        {
            int next = Terminal::readKey();
            if(next == 'd')
            {
                if(!messageLines.empty())
                {
                    messageLines.erase(messageLines.begin() + messageCursorRow);
                    if(messageLines.empty())
                        messageLines.push_back("");
                    messageCursorRow = std::clamp(
                        messageCursorRow, 0, (int)messageLines.size() - 1);
                    messageCursorCol = std::min(
                        messageCursorCol,
                        (int)messageLines[messageCursorRow].size());
                    clamp_cursor();
                }
                ed->needsFullRedraw = true;
            }
            return std::nullopt;
        }
    }

    if(c == Terminal::ARROW_LEFT)
    {
        if(messageCursorCol > 0)
            messageCursorCol--;
        else if(messageCursorRow > 0)
        {
            messageCursorRow--;
            messageCursorCol = (int)messageLines[messageCursorRow].size();
        }
        clamp_cursor();
        ed->needsFullRedraw = true;
        return std::nullopt;
    }
    if(c == Terminal::ARROW_RIGHT)
    {
        if(messageCursorCol < (int)messageLines[messageCursorRow].size())
            messageCursorCol++;
        else if(messageCursorRow + 1 < (int)messageLines.size())
        {
            messageCursorRow++;
            messageCursorCol = 0;
        }
        clamp_cursor();
        ed->needsFullRedraw = true;
        return std::nullopt;
    }
    if(c == Terminal::ARROW_UP)
    {
        if(messageCursorRow > 0)
        {
            messageCursorRow--;
            messageCursorCol = std::min(
                messageCursorCol, (int)messageLines[messageCursorRow].size());
        }
        clamp_cursor();
        ed->needsFullRedraw = true;
        return std::nullopt;
    }
    if(c == Terminal::ARROW_DOWN)
    {
        if(messageCursorRow + 1 < (int)messageLines.size())
        {
            messageCursorRow++;
            messageCursorCol = std::min(
                messageCursorCol, (int)messageLines[messageCursorRow].size());
        }
        clamp_cursor();
        ed->needsFullRedraw = true;
        return std::nullopt;
    }

    if(c == Terminal::BACKSPACE || c == 127 || c == Terminal::CTRL_H)
    {
        if(!insertMode)
            return std::nullopt;
        if(messageCursorCol > 0)
        {
            std::string& line = messageLines[messageCursorRow];
            line.erase(messageCursorCol - 1, 1);
            messageCursorCol--;
        }
        else if(messageCursorRow > 0)
        {
            int prevLen = (int)messageLines[messageCursorRow - 1].size();
            messageLines[messageCursorRow - 1] += messageLines[messageCursorRow];
            messageLines.erase(messageLines.begin() + messageCursorRow);
            messageCursorRow--;
            messageCursorCol = prevLen;
        }
        clamp_cursor();
        ed->needsFullRedraw = true;
        return std::nullopt;
    }

    if(c == Terminal::ENTER)
    {
        if(!insertMode)
            return std::nullopt;
        std::string& line = messageLines[messageCursorRow];
        std::string tail = line.substr(messageCursorCol);
        line.erase(messageCursorCol);
        messageLines.insert(messageLines.begin() + messageCursorRow + 1, tail);
        messageCursorRow++;
        messageCursorCol = 0;
        clamp_cursor();
        ed->needsFullRedraw = true;
        return std::nullopt;
    }

    if(c >= 32 && c <= 126)
    {
        if(!insertMode)
            return std::nullopt;
        std::string& line = messageLines[messageCursorRow];
        line.insert(line.begin() + messageCursorCol, (char)c);
        messageCursorCol++;
        clamp_cursor();
        ed->needsFullRedraw = true;
        return std::nullopt;
    }

    return std::nullopt;
}

void GitCommitMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    int messageStartRow = 3;
    int contentRows = std::max(1, editor.screenRows - 2);
    auto commentLines =
        build_comment_lines(stagedLines, hasStagedFiles, currentBranch, action,
                            revertHash, revertSubject);
    int totalVirtualRows = (int)messageLines.size() + (int)commentLines.size();
    int viewTop = std::clamp(messageTopRow, 0, std::max(0, totalVirtualRows - 1));

    output += Terminal::ESC_SHOW_CURSOR;
    output += Terminal::ESC_CURSOR_HOME;
    output += Terminal::ESC_CLEAR_SCREEN;
    output += Terminal::cursorPos(1, 1);
    output += editor.theme.reset();

    output += Terminal::ESC_CLEAR_LINE;
    output += Terminal::ESC_BOLD;
    output += (action == Action::RevertCommit) ? "  GIT REVERT" : "  GIT COMMIT";
    if(!repoRoot.empty())
        output += " - " + repoRoot;
    output += editor.theme.reset();

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    output += std::string("  [") + (insertMode ? "INSERT" : "NORMAL") +
              "] [:wq " +
              std::string(action == Action::RevertCommit ? "revert+close"
                                                         : "commit+close") +
              "] [:q close] [enter newline] "
              "[ctrl-r refresh staged]";
    output += editor.theme.baseFg();

    for(int row = 0; row < contentRows; ++row)
    {
        output += Terminal::NEWLINE_CLEAR;
        int lineIdx = viewTop + row;

        if(lineIdx >= 0 && lineIdx < (int)messageLines.size())
        {
            output += "  ";
            const std::string& line = messageLines[lineIdx];
            int maxW = std::max(0, editor.screenCols - 2);
            int normalLen = std::min((int)line.size(), std::min(COMMIT_SOFT_LIMIT, maxW));
            if(normalLen > 0)
                output += line.substr(0, normalLen);

            int overflowStart = std::min(COMMIT_SOFT_LIMIT, maxW);
            if((int)line.size() > overflowStart && overflowStart < maxW)
            {
                output += Terminal::BG_RED;
                output += Terminal::FG_WHITE;
                int overLen = std::min((int)line.size() - overflowStart,
                                       maxW - overflowStart);
                output += line.substr(overflowStart, overLen);
                output += editor.theme.reset();
            }
        }
        else
        {
            int commentIdx = lineIdx - (int)messageLines.size();
            if(commentIdx >= 0 && commentIdx < (int)commentLines.size())
            {
                const std::string& line = commentLines[commentIdx];
                output += editor.theme.uiDim();
                output += "  #";

                std::string body;
                if(!line.empty() && line[0] == '#')
                {
                    if(line.size() >= 2 && line[1] == ' ')
                        body = line.substr(2);
                    else
                        body = line.substr(1);
                }
                else
                {
                    body = line;
                }

                size_t labelPos = body.find(":");
                bool fileEntry =
                    body.find("new file:") != std::string::npos ||
                    body.find("modified:") != std::string::npos ||
                    body.find("deleted:") != std::string::npos ||
                    body.find("renamed:") != std::string::npos ||
                    body.find("copied:") != std::string::npos;
                if(fileEntry && labelPos != std::string::npos)
                {
                    output += body.substr(0, labelPos + 1);
                    output += Terminal::FG_GREEN;
                    if(labelPos + 1 < body.size())
                        output += body.substr(labelPos + 1);
                    output += editor.theme.uiDim();
                }
                else
                {
                    output += body;
                }
                output += editor.theme.baseFg();
            }
            else
            {
                output += editor.theme.uiGutter();
                output += "~";
                output += editor.theme.baseFg();
            }
        }
    }

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.statusBar();
    std::string status =
        (action == Action::RevertCommit) ? " GIT REVERT" : " GIT COMMIT";
    if(!repoRoot.empty())
        status += " | " + repoRoot;
    std::string right = " " + std::to_string(hasStagedFiles ? stagedLines.size() : 0) +
                        " staged ";
    output += status;
    int padding = editor.screenCols - (int)status.size() - (int)right.size();
    if(padding > 0)
        output.append(padding, ' ');
    output += right;
    output += editor.theme.reset();

    output += Terminal::NEWLINE_CLEAR;
    if(commandActive)
    {
        output += ":" + commandLine;
    }
    else if(!editor.statusMessage.empty())
    {
        output += editor.statusMessage.substr(
            0, std::min((size_t)editor.screenCols, editor.statusMessage.size()));
    }

    if(commandActive)
    {
        int col = std::clamp(2 + (int)commandLine.size(), 1, editor.screenCols);
        output += Terminal::cursorPos(editor.screenRows + 2, col);
    }
    else
    {
        int visualRow = messageStartRow + (messageCursorRow - messageTopRow);
        visualRow =
            std::clamp(visualRow, messageStartRow, messageStartRow + contentRows - 1);
        int visualCol = std::clamp(3 + messageCursorCol, 1, editor.screenCols);
        output += Terminal::cursorPos(visualRow, visualCol);
    }

    Terminal::write(output);
    Terminal::flush();
}
