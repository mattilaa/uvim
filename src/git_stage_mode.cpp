#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <limits.h>
#include <string>
#include <unistd.h>

namespace
{
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
    char buffer[1024];
    std::string output;
    while(fgets(buffer, sizeof(buffer), pipe))
        output += buffer;
    pclose(pipe);

    size_t pos = 0;
    while(pos <= output.size())
    {
        size_t next = output.find('\n', pos);
        if(next == std::string::npos)
        {
            out.push_back(output.substr(pos));
            break;
        }
        out.push_back(output.substr(pos, next - pos));
        pos = next + 1;
    }
    return out;
}

std::string git_show_toplevel(const std::string& dir)
{
    std::string cmd =
        "git -C \"" + dir + "\" rev-parse --show-toplevel 2>/dev/null";
    std::vector<std::string> lines = run_git_lines(cmd);
    if(lines.empty())
        return "";
    return trim_newline(lines.front());
}

std::vector<GitStageMode::Entry> parse_status_output(
    const std::vector<std::string>& lines)
{
    std::vector<GitStageMode::Entry> out;
    for(const auto& lineRaw : lines)
    {
        std::string line = trim_newline(lineRaw);
        if(line.size() < 3)
            continue;
        char x = line[0];
        char y = line[1];
        if(x == '!' && y == '!')
            continue; // ignored
        std::string path;
        if(line.size() >= 4)
            path = line.substr(3);
        if(path.empty())
            continue;
        std::string displayPath = path;
        size_t arrow = path.find(" -> ");
        if(arrow != std::string::npos)
        {
            displayPath = path;
            path = path.substr(arrow + 4);
        }
        GitStageMode::Entry entry;
        entry.path = path;
        entry.displayPath = displayPath;
        entry.indexStatus = x;
        entry.worktreeStatus = y;
        out.push_back(std::move(entry));
    }
    return out;
}

bool is_untracked(const GitStageMode::Entry& entry)
{
    return entry.indexStatus == '?' && entry.worktreeStatus == '?';
}

bool has_staged(const GitStageMode::Entry& entry)
{
    return entry.indexStatus != ' ' && entry.indexStatus != '?';
}

bool has_unstaged(const GitStageMode::Entry& entry)
{
    return entry.worktreeStatus != ' ' && entry.worktreeStatus != '?';
}

std::string status_color(const Theme& theme, const GitStageMode::Entry& entry)
{
    if(is_untracked(entry))
        return theme.uiInfo();
    if(has_staged(entry) && has_unstaged(entry))
        return theme.uiWarning();
    if(has_staged(entry))
        return theme.uiSuccess();
    if(has_unstaged(entry))
        return theme.uiError();
    return theme.baseFg();
}

void append_diff_line(std::string& output, const Editor& editor,
                      const std::string& line)
{
    if(editor.gitUseDefaultColors)
    {
        output += line;
        output += editor.theme.reset();
        return;
    }

    if(line.rfind("diff --git", 0) == 0 || line.rfind("--- ", 0) == 0 ||
       line.rfind("+++ ", 0) == 0)
    {
        output += editor.theme.uiAccent();
    }
    else if(line.rfind("index ", 0) == 0 || line.rfind("commit ", 0) == 0)
    {
        output += editor.theme.uiDim();
    }
    else if(line.rfind("@@ ", 0) == 0)
    {
        output += editor.theme.uiInfo();
    }
    else if(!line.empty() && line[0] == '+')
    {
        output += editor.theme.uiSuccess();
    }
    else if(!line.empty() && line[0] == '-')
    {
        output += editor.theme.uiError();
    }
    else
    {
        output += editor.theme.baseFg();
    }

    output += line;
    output += editor.theme.reset();
}
} // namespace

GitStageMode::GitStageMode(std::vector<Entry> items, std::string root,
                           std::string dir)
    : entries(std::move(items)), repoRoot(std::move(root)),
      repoDir(std::move(dir))
{
}

void GitStageMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    cursor = std::clamp(cursor, 0, (int)entries.size());
    offset = 0;
    diffOffset = 0;
    diffDirty = true;

    if(repoRoot.empty())
    {
        std::string baseDir = ".";
        if(ed->currentBuffer && !ed->currentBuffer->filename.empty())
        {
            std::filesystem::path path(ed->currentBuffer->filename);
            baseDir = path.has_parent_path() ? path.parent_path().string()
                                             : std::string(".");
        }
        else if(!ed->projectRoot.empty())
        {
            baseDir = ed->projectRoot;
        }
        else
        {
            char cwd[PATH_MAX];
            if(getcwd(cwd, sizeof(cwd)))
                baseDir = cwd;
        }

        std::string root = git_show_toplevel(baseDir);
        if(root.empty())
        {
            ctx.setStatusMessage("git stage: not a repo");
            entries.clear();
            diffLines = {"(not a repo)"};
            diffDirty = false;
            ctx.requestFullRedraw();
            return;
        }

        repoRoot = root;
        repoDir = root;
    }

    refreshStatus(*ed);
    refreshDiff(*ed);
    ctx.requestFullRedraw();
}

void GitStageMode::on_exit(ModeContext& ctx)
{
    ctx.requestFullRedraw();
}

bool GitStageMode::refreshStatus(Editor& editor)
{
    if(repoDir.empty())
        return false;

    std::string cmd =
        "git -C \"" + repoDir + "\" --no-pager status --porcelain 2>/dev/null";
    std::vector<std::string> lines = run_git_lines(cmd);
    std::string keepPath;
    if(cursor >= 0 && cursor < (int)entries.size())
        keepPath = entries[cursor].path;

    entries = parse_status_output(lines);
    if(!keepPath.empty())
    {
        for(size_t i = 0; i < entries.size(); ++i)
        {
            if(entries[i].path == keepPath)
            {
                cursor = (int)i;
                break;
            }
        }
    }
    if(entries.empty())
    {
        cursor = 0;
        offset = 0;
    }
    else
    {
        cursor = std::clamp(cursor, 0, (int)entries.size() - 1);
        int visible = editor.screenRows - 3;
        offset = std::clamp(offset, 0,
                            std::max(0, (int)entries.size() - visible));
    }
    diffDirty = true;
    return true;
}

void GitStageMode::refreshDiff(Editor& editor)
{
    if(!diffDirty)
        return;

    diffLines.clear();
    diffOffset = 0;
    diffPath.clear();
    diffStaged = false;

    if(entries.empty())
    {
        diffLines.push_back("(clean)");
        diffDirty = false;
        return;
    }

    cursor = std::clamp(cursor, 0, (int)entries.size() - 1);
    const Entry& entry = entries[cursor];
    diffPath = entry.path;

    if(is_untracked(entry))
    {
        diffLines.push_back("(untracked file)");
        diffDirty = false;
        return;
    }

    bool useUnstaged = has_unstaged(entry);
    bool useStaged = !useUnstaged && has_staged(entry);
    diffStaged = useStaged;

    std::string cmd = "git -C \"" + repoDir + "\" --no-pager diff ";
    if(useStaged)
        cmd += "--cached ";
    cmd += std::string(editor.gitUseDefaultColors ? "--color=always "
                                                   : "--no-color ");
    cmd += "-- \"" + entry.path + "\" 2>/dev/null";

    diffLines = run_git_lines(cmd);
    if(diffLines.empty())
        diffLines.push_back("(no diff)");

    diffDirty = false;
}

std::optional<ModeState> GitStageMode::handle(ModeContext& ctx,
                                              const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    if(c == Terminal::ESC || c == 'q')
    {
        if(c == Terminal::ESC)
            ed->noteDoubleEscStatusClear();
        if(returnMode.has_value() && returnMode.value() == FILE_BROWSER)
        {
            FileBrowserMode fb{returnBrowseDirectory};
            fb.browserCursor = returnBrowseCursor;
            fb.browserOffset = returnBrowseOffset;
            return fb;
        }
        if(returnMode.has_value())
        {
            if(returnMode.value() == WELCOME)
                return WelcomeMode{};
            if(returnMode.value() == NORMAL)
                return NormalMode{};
        }
        return defaultExitMode(ed);
    }

    if(c == 'r')
    {
        refreshStatus(*ed);
        refreshDiff(*ed);
        ed->needsFullRedraw = true;
        return std::nullopt;
    }

    if(c == Terminal::ENTER)
    {
        if(cursor >= 0 && cursor < (int)entries.size())
        {
            std::string openPath = entries[cursor].path;
            if(!repoRoot.empty() &&
               !std::filesystem::path(openPath).is_absolute())
            {
                openPath =
                    (std::filesystem::path(repoRoot) / openPath).string();
            }
            ed->openFile(openPath);
            return NormalMode{};
        }
        return std::nullopt;
    }

    if(c == ' ')
    {
        if(cursor >= 0 && cursor < (int)entries.size() && !repoDir.empty())
        {
            const Entry& entry = entries[cursor];
            std::string cmd;
            if(has_unstaged(entry) || is_untracked(entry))
            {
                cmd = "git -C \"" + repoDir + "\" add -- \"" + entry.path +
                      "\" 2>/dev/null";
            }
            else if(has_staged(entry))
            {
                cmd = "git -C \"" + repoDir +
                      "\" restore --staged -- \"" + entry.path +
                      "\" 2>/dev/null";
            }
            if(!cmd.empty())
            {
                std::system(cmd.c_str());
                refreshStatus(*ed);
                refreshDiff(*ed);
                ed->needsFullRedraw = true;
            }
        }
        return std::nullopt;
    }

    if(c == 'j' || c == Terminal::ARROW_DOWN)
    {
        if(cursor < (int)entries.size() - 1)
        {
            cursor++;
            int visible = ed->screenRows - 3;
            if(cursor >= offset + visible)
                offset = cursor - visible + 1;
            diffDirty = true;
        }
    }
    else if(c == 'k' || c == Terminal::ARROW_UP)
    {
        if(cursor > 0)
        {
            cursor--;
            if(cursor < offset)
                offset = cursor;
            diffDirty = true;
        }
    }
    else if(c == Terminal::CTRL_J)
    {
        int maxScroll =
            std::max(0, (int)diffLines.size() - (ed->screenRows - 3));
        if(diffOffset < maxScroll)
            diffOffset++;
    }
    else if(c == Terminal::CTRL_K)
    {
        if(diffOffset > 0)
            diffOffset--;
    }
    else if(c == 'g')
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
        {
            cursor = 0;
            offset = 0;
            diffDirty = true;
        }
    }
    else if(c == 'G')
    {
        if(!entries.empty())
        {
            cursor = std::max(0, (int)entries.size() - 1);
            int visible = ed->screenRows - 3;
            offset = std::max(0, cursor - visible + 1);
            diffDirty = true;
        }
    }

    refreshDiff(*ed);
    ed->needsFullRedraw = true;
    return std::nullopt;
}

void GitStageMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::cursorPos(1, 1);
    output += editor.theme.reset();

    output += editor.theme.panel();
    std::string header = " GIT STAGE";
    if(!repoRoot.empty())
        header += " - " + repoRoot;
    if((int)header.size() < editor.screenCols)
        header += std::string(editor.screenCols - header.size(), ' ');
    output += header;
    output += editor.theme.reset();
    output += "\r\n";

    int contentRows = editor.screenRows - 3;
    int listWidth = std::max(24, editor.screenCols / 3);
    int diffWidth = editor.screenCols - listWidth - 1;
    if(diffWidth < 10)
    {
        listWidth = editor.screenCols;
        diffWidth = 0;
    }

    for(int row = 0; row < contentRows; ++row)
    {
        output += Terminal::ESC_CLEAR_LINE;
        int idx = offset + row;
        if(idx >= 0 && idx < (int)entries.size())
        {
            const Entry& entry = entries[idx];
            bool isSelected = (idx == cursor);
            if(isSelected)
                output += editor.theme.selection();

            std::string status;
            status.push_back(entry.indexStatus);
            status.push_back(entry.worktreeStatus);

            std::string path =
                entry.displayPath.empty() ? entry.path : entry.displayPath;
            int maxPathLen = std::max(0, listWidth - 4);
            if((int)path.size() > maxPathLen)
            {
                if(maxPathLen > 3)
                    path = "..." + path.substr(path.size() - maxPathLen + 3);
                else
                    path = path.substr(0, maxPathLen);
            }

            output += " ";
            output += status_color(editor.theme, entry);
            output += status;
            output += editor.theme.baseFg();
            output += " ";
            output += path;
            int used = 1 + 2 + 1 + (int)path.size();
            if(used < listWidth)
                output.append(listWidth - used, ' ');
            output += editor.theme.reset();
        }
        else
        {
            output += editor.theme.uiGutter();
            output += "~";
            output += editor.theme.baseFg();
            if(listWidth > 1)
                output.append(listWidth - 1, ' ');
        }

        if(diffWidth > 0)
        {
            output += editor.theme.uiGutter();
            output += "|";
            output += editor.theme.baseFg();
            int diffIdx = diffOffset + row;
            if(diffIdx >= 0 && diffIdx < (int)diffLines.size())
            {
                output += " ";
                append_diff_line(output, editor, diffLines[diffIdx]);
            }
            else
            {
                output += editor.theme.uiGutter();
                output += " ~";
                output += editor.theme.baseFg();
            }
        }

        output += "\r\n";
    }

    output += editor.theme.statusBar();
    std::string status = " GIT STAGE";
    status += " [space] stage/unstage [enter] open [r] refresh [q/esc] close";
    if((int)status.size() < editor.screenCols)
        status += std::string(editor.screenCols - status.size(), ' ');
    output += status;
    output += editor.theme.reset();

    output += "\r\n";
    output += Terminal::ESC_CLEAR_LINE;
    if(!editor.statusMessage.empty())
        output += editor.statusMessage;

    Terminal::write(output);
    Terminal::flush();
}
