#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>
#include <cctype>
#include "os_compat.h"
#include <cstdlib>
#include <sys/stat.h>
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
                      "\" --no-pager log --no-color ";
    if(mode.prettyView)
    {
        cmd += "--date=format:%Y-%m-%d\\ %H:%M\\ %z "
               "--pretty=format:%H%x1f%ad%x1f%an%x1f%s";
    }
    else
    {
        cmd += "--pretty=format:%H%x1f%s";
    }
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
        constexpr char sep = '\x1f';
        size_t tab = line.find(sep);
        if(tab == std::string::npos)
            continue;
        GitLogMode::Entry entry;
        entry.hash = line.substr(0, tab);
        if(mode.prettyView)
        {
            size_t tab2 = line.find(sep, tab + 1);
            size_t tab3 = (tab2 == std::string::npos)
                              ? std::string::npos
                              : line.find(sep, tab2 + 1);
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
        }
        else
        {
            entry.subject = line.substr(tab + 1);
        }
        entries.push_back(std::move(entry));
    }
    if(entries.empty())
        return false;

    mode.entries = std::move(entries);
    mode.filtered.clear();
    mode.selectedHashes.clear();
    mode.rangeSelectActive = false;
    mode.rangeSelectAnchor = 0;
    mode.rangeSelectBase.clear();
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
    return i < line.size() && line[i] == keyCode(command::CommandKey::KEY_HASH);
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

std::string shell_escape_single(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 8);
    out += '\'';
    for(char ch : text)
    {
        if(ch == keyCode(command::CommandKey::KEY_APOSTROPHE))
            out += "'\\''";
        else
            out += ch;
    }
    out += '\'';
    return out;
}

std::string normalize_todo_action(std::string action)
{
    for(char& ch : action)
        ch = (char)std::tolower((unsigned char)ch);
    if(action == "p")
        return "pick";
    if(action == "r")
        return "reword";
    if(action == "e")
        return "edit";
    if(action == "s")
        return "squash";
    if(action == "f")
        return "fixup";
    if(action == "d")
        return "drop";
    if(action == "pick" || action == "reword" || action == "edit" ||
       action == "squash" || action == "fixup" || action == "drop")
        return action;
    return {};
}

std::vector<std::string> build_comment_lines(
    const std::vector<std::string>& stagedLines, bool hasStagedFiles,
    const std::string& branch, GitCommitMode::Action action,
    const std::string& revertHash, const std::string& revertSubject,
    const std::string& rebaseBaseHash, const std::string& rebaseHeadHash,
    int rebaseCommandCount)
{
    std::vector<std::string> out;
    if(action == GitCommitMode::Action::RebaseTodo)
    {
        out.push_back("# Rebase " + rebaseBaseHash + ".." + rebaseHeadHash +
                      " (" + std::to_string(rebaseCommandCount) +
                      " commands)");
        out.push_back("#");
        out.push_back("# Commands:");
        out.push_back("# p, pick <commit> = use commit");
        out.push_back("# r, reword <commit> = use commit, edit message");
        out.push_back("# e, edit <commit> = use commit, stop for amending");
        out.push_back("# s, squash <commit> = meld into previous commit");
        out.push_back("# f, fixup <commit> = like squash, discard this message");
        out.push_back("# d, drop <commit> = remove commit");
        out.push_back("#");
        out.push_back("# Reorder lines to move commits.");
        out.push_back("# Lines starting with keyCode(command::CommandKey::KEY_HASH) will be ignored.");
        out.push_back("# Remove a line here to drop that commit.");
        out.push_back("#");
        out.push_back("# :wq to run rebase, :q to cancel.");
        return out;
    }

    out.push_back(
        "# Please enter the commit message for your changes. Lines starting");
    out.push_back(
        "# with keyCode(command::CommandKey::KEY_HASH) will be ignored, and an empty message aborts the commit.");
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
                (tab != std::string::npos) ? tab : line.find(keyCode(control::ControlKey::SPACE));
            if(splitPos != std::string::npos && splitPos + 1 < line.size())
                path = line.substr(splitPos + 1);
        }

        std::string kind = "modified";
        if(status == keyCode(typed::TypedKey::KEY_CAP_A))
            kind = "new file";
        else if(status == keyCode(typed::TypedKey::KEY_CAP_D))
            kind = "deleted";
        else if(status == keyCode(typed::TypedKey::KEY_CAP_R))
            kind = "renamed";
        else if(status == keyCode(typed::TypedKey::KEY_CAP_C))
            kind = "copied";
        else if(status == keyCode(typed::TypedKey::KEY_CAP_M))
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
    if(action != Action::RebaseTodo && stagedDirty)
        refreshStaged();
    if(action == Action::RebaseTodo)
    {
        stagedLines.clear();
        hasStagedFiles = false;
    }
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
                                               int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

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
        if(action != Action::RebaseTodo && stagedDirty)
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

        if(action == Action::RebaseTodo)
        {
            struct TodoLine
            {
                std::string action;
                std::string hash;
                std::string subject;
            };
            std::vector<TodoLine> todo;
            todo.reserve(commitLines.size());

            for(const std::string& raw : commitLines)
            {
                size_t start = 0;
                while(start < raw.size() &&
                      std::isspace((unsigned char)raw[start]))
                    ++start;
                if(start >= raw.size())
                    continue;
                std::string line = raw.substr(start);
                size_t sp1 = line.find(keyCode(control::ControlKey::SPACE));
                std::string actionToken =
                    (sp1 == std::string::npos) ? line : line.substr(0, sp1);
                std::string actionNorm = normalize_todo_action(actionToken);
                if(actionNorm.empty())
                {
                    ed->setStatusMessage("git rebase: invalid action '" +
                                         actionToken + "'");
                    return false;
                }

                size_t pos = (sp1 == std::string::npos) ? line.size() : sp1;
                while(pos < line.size() && line[pos] == keyCode(control::ControlKey::SPACE))
                    ++pos;
                if(pos >= line.size())
                {
                    ed->setStatusMessage("git rebase: missing commit hash");
                    return false;
                }

                size_t sp2 = line.find(keyCode(control::ControlKey::SPACE), pos);
                std::string hash =
                    (sp2 == std::string::npos) ? line.substr(pos)
                                               : line.substr(pos, sp2 - pos);
                std::string subject =
                    (sp2 == std::string::npos) ? std::string{}
                                               : line.substr(sp2 + 1);
                if(hash.empty())
                {
                    ed->setStatusMessage("git rebase: missing commit hash");
                    return false;
                }
                todo.push_back(TodoLine{std::move(actionNorm), std::move(hash),
                                        std::move(subject)});
            }

            if(todo.empty())
            {
                ed->setStatusMessage("git rebase: todo is empty");
                return false;
            }
            const std::string oldestHash =
                rebaseBaseHash.empty() ? todo.front().hash : rebaseBaseHash;
            std::string baseCmd = "git -C \"" + repoDir + "\" rev-parse \"" +
                                  oldestHash + "^\" 2>/dev/null";
            auto baseLines = run_git_lines(baseCmd);
            std::string base;
            if(!baseLines.empty())
                base = trim_newline(baseLines.front());

            std::string todoText;
            for(const auto& item : todo)
            {
                todoText += item.action + " " + item.hash;
                if(!item.subject.empty())
                    todoText += " " + item.subject;
                todoText += "\n";
            }

            char todoTemplate[] = "/tmp/uvim_rebase_todoXXXXXX";
            int todoFd = mkstemp(todoTemplate);
            if(todoFd < 0)
            {
                ed->setStatusMessage("git rebase: failed");
                return false;
            }
            std::string todoPath = todoTemplate;
            FILE* todoFile = fdopen(todoFd, "w");
            if(!todoFile)
            {
                close(todoFd);
                unlink(todoPath.c_str());
                ed->setStatusMessage("git rebase: failed");
                return false;
            }
            fwrite(todoText.data(), 1, todoText.size(), todoFile);
            fclose(todoFile);

            char scriptTemplate[] = "/tmp/uvim_rebase_editorXXXXXX";
            int scriptFd = mkstemp(scriptTemplate);
            if(scriptFd < 0)
            {
                unlink(todoPath.c_str());
                ed->setStatusMessage("git rebase: failed");
                return false;
            }
            std::string scriptPath = scriptTemplate;
            FILE* scriptFile = fdopen(scriptFd, "w");
            if(!scriptFile)
            {
                close(scriptFd);
                unlink(scriptPath.c_str());
                unlink(todoPath.c_str());
                ed->setStatusMessage("git rebase: failed");
                return false;
            }

            std::string script = "#!/bin/sh\ncat " +
                                 shell_escape_single(todoPath) + " > \"$1\"\n";
            fwrite(script.data(), 1, script.size(), scriptFile);
            fclose(scriptFile);
            chmod(scriptPath.c_str(), 0700);

            std::string cmd = "GIT_SEQUENCE_EDITOR=" +
                              shell_escape_single(scriptPath) +
                              " git -C \"" + repoDir +
                              "\" rebase -i --autosquash ";
            if(base.empty())
                cmd += "--root";
            else
                cmd += shell_escape_single(base);
            cmd += " 2>/dev/null";

            int status = std::system(cmd.c_str());
            unlink(scriptPath.c_str());
            unlink(todoPath.c_str());
            if(status != 0)
            {
                ed->setStatusMessage("git rebase: failed");
                return false;
            }

            ed->setStatusMessage("git rebase: done");
            return true;
        }

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
        if(c == keyCode(control::ControlKey::ESC))
        {
            commandActive = false;
            commandLine.clear();
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 || c == keyCode(control::ControlKey::CTRL_H))
        {
            if(!commandLine.empty())
                commandLine.pop_back();
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ENTER))
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
                if(action == Action::RebaseTodo)
                    ed->setStatusMessage("git rebase: unknown command");
                else
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

    if(c == keyCode(control::ControlKey::ESC) && insertMode)
    {
        insertMode = false;
        ed->needsFullRedraw = true;
        return std::nullopt;
    }
    if(c == keyCode(command::CommandKey::KEY_COLON) && !insertMode)
    {
        commandActive = true;
        commandLine.clear();
        ed->needsFullRedraw = true;
        return std::nullopt;
    }
    if(c == keyCode(control::ControlKey::CTRL_R))
    {
        if(action == Action::RebaseTodo)
            return std::nullopt;
        stagedDirty = true;
        refreshStaged();
        ed->needsFullRedraw = true;
        return std::nullopt;
    }

    if(!insertMode)
    {
        if(c == keyCode(typed::TypedKey::KEY_Q))
            return return_after_done(false);
        if(c == keyCode(typed::TypedKey::KEY_H))
        {
            if(messageCursorCol > 0)
                messageCursorCol--;
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(typed::TypedKey::KEY_L))
        {
            if(messageCursorCol < (int)messageLines[messageCursorRow].size())
                messageCursorCol++;
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(typed::TypedKey::KEY_0))
        {
            messageCursorCol = 0;
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(command::CommandKey::KEY_DOLLAR))
        {
            messageCursorCol = (int)messageLines[messageCursorRow].size();
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(typed::TypedKey::KEY_J))
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
        if(c == keyCode(typed::TypedKey::KEY_K))
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
        if(action == Action::RebaseTodo && (c == keyCode(typed::TypedKey::KEY_CAP_J) || c == keyCode(typed::TypedKey::KEY_CAP_K)))
        {
            if(c == keyCode(typed::TypedKey::KEY_CAP_J) && messageCursorRow + 1 < (int)messageLines.size())
            {
                std::swap(messageLines[messageCursorRow],
                          messageLines[messageCursorRow + 1]);
                messageCursorRow++;
                clamp_cursor();
            }
            else if(c == keyCode(typed::TypedKey::KEY_CAP_K) && messageCursorRow > 0)
            {
                std::swap(messageLines[messageCursorRow],
                          messageLines[messageCursorRow - 1]);
                messageCursorRow--;
                clamp_cursor();
            }
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(typed::TypedKey::KEY_G))
        {
            int next = Terminal::readKey();
            if(next == keyCode(typed::TypedKey::KEY_G))
            {
                messageCursorRow = 0;
                messageCursorCol = std::min(
                    messageCursorCol, (int)messageLines[messageCursorRow].size());
                clamp_cursor();
                ed->needsFullRedraw = true;
            }
            return std::nullopt;
        }
        if(c == keyCode(typed::TypedKey::KEY_CAP_G))
        {
            messageCursorRow = std::max(0, (int)messageLines.size() - 1);
            messageCursorCol = std::min(
                messageCursorCol, (int)messageLines[messageCursorRow].size());
            clamp_cursor();
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(typed::TypedKey::KEY_X))
        {
            std::string& line = messageLines[messageCursorRow];
            if(messageCursorCol < (int)line.size())
                line.erase(messageCursorCol, 1);
            messageCursorCol = std::min(messageCursorCol, (int)line.size());
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(action == Action::RebaseTodo &&
           (c == keyCode(typed::TypedKey::KEY_P) || c == keyCode(typed::TypedKey::KEY_R) || c == keyCode(typed::TypedKey::KEY_E) || c == keyCode(typed::TypedKey::KEY_S) || c == keyCode(typed::TypedKey::KEY_F) ||
            c == keyCode(typed::TypedKey::KEY_D) || c == keyCode(typed::TypedKey::KEY_CAP_P) || c == keyCode(typed::TypedKey::KEY_CAP_R) || c == keyCode(typed::TypedKey::KEY_CAP_E) || c == keyCode(typed::TypedKey::KEY_CAP_S) ||
            c == keyCode(typed::TypedKey::KEY_CAP_F) || c == keyCode(typed::TypedKey::KEY_CAP_D)))
        {
            char alias = (char)std::tolower((unsigned char)c);
            std::string actionWord = normalize_todo_action(std::string(1, alias));
            std::string& line = messageLines[messageCursorRow];
            size_t start = 0;
            while(start < line.size() &&
                  std::isspace((unsigned char)line[start]))
                ++start;
            size_t wordEnd = start;
            while(wordEnd < line.size() && line[wordEnd] != keyCode(control::ControlKey::SPACE))
                ++wordEnd;
            if(start < line.size())
            {
                line.replace(start, wordEnd - start, actionWord);
                messageCursorCol = std::min(messageCursorCol, (int)line.size());
                ed->needsFullRedraw = true;
            }
            return std::nullopt;
        }
        if(c == keyCode(typed::TypedKey::KEY_CAP_O))
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
            return std::isalnum((unsigned char)ch) || ch == keyCode(command::CommandKey::KEY_UNDERSCORE);
        };
        if(c == keyCode(typed::TypedKey::KEY_B) || c == keyCode(typed::TypedKey::KEY_CAP_B) || c == keyCode(typed::TypedKey::KEY_E) || c == keyCode(typed::TypedKey::KEY_CAP_E))
        {
            const std::string& line = messageLines[messageCursorRow];
            if(!line.empty())
            {
                int pos = messageCursorCol;
                int n = (int)line.size();
                bool bigWord = (c == keyCode(typed::TypedKey::KEY_CAP_B) || c == keyCode(typed::TypedKey::KEY_CAP_E));
                bool endMotion = (c == keyCode(typed::TypedKey::KEY_E) || c == keyCode(typed::TypedKey::KEY_CAP_E));

                auto is_space = [](char ch)
                { return ch == keyCode(control::ControlKey::SPACE) || ch == '\t'; };

                if(c == keyCode(typed::TypedKey::KEY_B) || c == keyCode(typed::TypedKey::KEY_CAP_B))
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
        if(c == keyCode(typed::TypedKey::KEY_I))
        {
            insertMode = true;
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(typed::TypedKey::KEY_CAP_A))
        {
            messageCursorCol = (int)messageLines[messageCursorRow].size();
            insertMode = true;
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(typed::TypedKey::KEY_O))
        {
            messageLines.insert(messageLines.begin() + messageCursorRow + 1, "");
            messageCursorRow++;
            messageCursorCol = 0;
            insertMode = true;
            clamp_cursor();
            ed->needsFullRedraw = true;
            return std::nullopt;
        }
        if(c == keyCode(typed::TypedKey::KEY_D))
        {
            int next = Terminal::readKey();
            if(next == keyCode(typed::TypedKey::KEY_D))
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
        if(c == keyCode(typed::TypedKey::KEY_C))
        {
            int next = Terminal::readKey();
            if(next == keyCode(typed::TypedKey::KEY_W))
            {
                std::string& line = messageLines[messageCursorRow];
                int n = (int)line.size();
                int start = std::clamp(messageCursorCol, 0, n);
                int end = start;
                auto is_space = [](char ch)
                { return ch == keyCode(control::ControlKey::SPACE) || ch == '\t'; };

                if(end < n)
                {
                    if(is_space(line[end]))
                    {
                        while(end < n && is_space(line[end]))
                            ++end;
                    }
                    else
                    {
                        bool inWord = is_word_char(line[end]);
                        while(end < n && !is_space(line[end]) &&
                              is_word_char(line[end]) == inWord)
                            ++end;
                    }
                }
                if(end > start)
                    line.erase((size_t)start, (size_t)(end - start));
                messageCursorCol = start;
                insertMode = true;
                clamp_cursor();
                ed->needsFullRedraw = true;
            }
            return std::nullopt;
        }
    }

    if(c == keyCode(navigation::NavigationKey::ARROW_LEFT))
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
    if(c == keyCode(navigation::NavigationKey::ARROW_RIGHT))
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
    if(c == keyCode(navigation::NavigationKey::ARROW_UP))
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
    if(c == keyCode(navigation::NavigationKey::ARROW_DOWN))
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

    if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 || c == keyCode(control::ControlKey::CTRL_H))
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

    if(c == keyCode(control::ControlKey::ENTER))
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
                            revertHash, revertSubject, rebaseBaseHash,
                            rebaseHeadHash, rebaseCommandCount);
    int totalVirtualRows = (int)messageLines.size() + (int)commentLines.size();
    int viewTop = std::clamp(messageTopRow, 0, std::max(0, totalVirtualRows - 1));

    output += Terminal::ESC_SHOW_CURSOR;
    output += Terminal::ESC_CURSOR_HOME;
    output += Terminal::ESC_CLEAR_SCREEN;
    output += Terminal::cursorPos(1, 1);
    output += editor.theme.reset();

    output += Terminal::ESC_CLEAR_LINE;
    output += Terminal::ESC_BOLD;
    if(action == Action::RevertCommit)
        output += "  GIT REVERT";
    else if(action == Action::RebaseTodo)
        output += "  GIT REBASE TODO";
    else
        output += "  GIT COMMIT";
    if(!repoRoot.empty())
        output += " - " + repoRoot;
    output += editor.theme.reset();

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    if(action == Action::RebaseTodo)
    {
        output += std::string("  [") + (insertMode ? "INSERT" : "NORMAL") +
                  "] [:wq rebase+close] [:q cancel] "
                  "[p/r/e/s/f/d set action] [J/K move] [enter newline]";
    }
    else
    {
        output += std::string("  [") + (insertMode ? "INSERT" : "NORMAL") +
                  "] [:wq " +
                  std::string(action == Action::RevertCommit ? "revert+close"
                                                             : "commit+close") +
                  "] [:q close] [enter newline] "
                  "[ctrl-r refresh staged]";
    }
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
            int softLimit =
                (action == Action::RebaseTodo) ? maxW : std::min(COMMIT_SOFT_LIMIT, maxW);
            int normalLen = std::min((int)line.size(), softLimit);
            if(normalLen > 0)
                output += line.substr(0, normalLen);

            int overflowStart = softLimit;
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
                if(!line.empty() && line[0] == keyCode(command::CommandKey::KEY_HASH))
                {
                    if(line.size() >= 2 && line[1] == keyCode(control::ControlKey::SPACE))
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
    std::string status = " GIT COMMIT";
    if(action == Action::RevertCommit)
        status = " GIT REVERT";
    else if(action == Action::RebaseTodo)
        status = " GIT REBASE";
    if(!repoRoot.empty())
        status += " | " + repoRoot;
    std::string right;
    if(action == Action::RebaseTodo)
    {
        int todoCount = 0;
        for(const auto& line : messageLines)
        {
            if(!is_comment_line(line) && !is_blank_line(line))
                ++todoCount;
        }
        right = " " + std::to_string(todoCount) + " commits ";
    }
    else
        right = " " + std::to_string(hasStagedFiles ? stagedLines.size() : 0) +
                " staged ";
    output += status;
    int padding = editor.screenCols - (int)status.size() - (int)right.size();
    if(padding > 0)
        output.append(padding, keyCode(control::ControlKey::SPACE));
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
