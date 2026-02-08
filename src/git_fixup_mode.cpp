#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>
#include <cstdio>
#include <string>

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

std::vector<GitFixupMode::Entry> load_recent_commits(const std::string& repoDir)
{
    std::string cmd =
        "git -C \"" + repoDir +
        "\" --no-pager log --no-color --pretty=format:%h\t%s -n 100 2>/dev/null";
    auto lines = run_git_lines(cmd);
    std::vector<GitFixupMode::Entry> entries;
    for(const auto& line : lines)
    {
        if(line.empty())
            continue;
        size_t tab = line.find('\t');
        if(tab == std::string::npos)
            continue;
        GitFixupMode::Entry entry;
        entry.hash = line.substr(0, tab);
        entry.subject = line.substr(tab + 1);
        entries.push_back(std::move(entry));
    }
    return entries;
}
} // namespace

void GitFixupMode::on_enter(ModeContext& ctx)
{
    if(entries.empty() && !repoDir.empty())
    {
        entries = load_recent_commits(repoDir);
    }
    cursor = std::clamp(cursor, 0, std::max(0, (int)entries.size() - 1));
    offset = 0;
    ctx.requestFullRedraw();
}

void GitFixupMode::on_exit(ModeContext& ctx)
{
    ctx.requestFullRedraw();
}

std::optional<ModeState> GitFixupMode::handle(ModeContext& ctx,
                                              const KeyEvent& event)
{
    Editor* ed = ctx.editor;
    int c = event.key;

    if(confirmActive)
    {
        if(c == 'y' || c == 'Y')
        {
            if(!fixupFiles.empty())
            {
                for(const auto& file : fixupFiles)
                {
                    std::string cmd = "git -C \"" + repoDir + "\" add -- \"" +
                                      file + "\" 2>/dev/null";
                    std::system(cmd.c_str());
                }
            }
            std::string cmd = "git -C \"" + repoDir + "\" commit --fixup " +
                              confirmHash + " 2>/dev/null";
            std::system(cmd.c_str());
            ed->setStatusMessage("fixup commit created");
            return returnStage;
        }
        if(c == 'p' || c == 'P')
        {
            GitPatchMode::Hunk dummy;
            std::vector<GitPatchMode::Hunk> hunks;
            return GitPatchMode{std::move(hunks), repoRoot, repoDir, confirmHash,
                                fixupFiles, returnStage};
        }
        if(c == 'n' || c == 'N' || c == Terminal::ESC)
        {
            confirmActive = false;
            confirmHash.clear();
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        return std::nullopt;
    }

    if(c == Terminal::ESC || c == 'q')
    {
        return returnStage;
    }

    if(c == 'j' || c == Terminal::ARROW_DOWN)
    {
        if(cursor < (int)entries.size() - 1)
        {
            cursor++;
            int visible = ed->screenRows - 3;
            if(cursor >= offset + visible)
                offset = cursor - visible + 1;
        }
    }
    else if(c == 'k' || c == Terminal::ARROW_UP)
    {
        if(cursor > 0)
        {
            cursor--;
            if(cursor < offset)
                offset = cursor;
        }
    }
    else if(c == 'g')
    {
        int nextChar = Terminal::readKey();
        if(nextChar == 'g')
        {
            cursor = 0;
            offset = 0;
        }
    }
    else if(c == 'G')
    {
        if(!entries.empty())
        {
            cursor = std::max(0, (int)entries.size() - 1);
            int visible = ed->screenRows - 3;
            offset = std::max(0, cursor - visible + 1);
        }
    }
    else if(c == 'f' || c == Terminal::ENTER)
    {
        if(cursor >= 0 && cursor < (int)entries.size())
        {
            confirmActive = true;
            confirmHash = entries[cursor].hash;
            ctx.requestFullRedraw();
        }
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}

void GitFixupMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::cursorPos(1, 1);
    output += editor.theme.reset();

    output += Terminal::ESC_CLEAR_LINE;
    output += Terminal::ESC_BOLD;
    output += "  GIT FIXUP";
    if(!repoRoot.empty())
        output += " - " + repoRoot;
    output += editor.theme.reset();
    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    output += "  [j/k: move] [f/enter: fixup] [q/esc: back]";
    output += editor.theme.baseFg();

    int availableRows = editor.screenRows - 3;
    for(int row = 0; row < availableRows; ++row)
    {
        output += Terminal::NEWLINE_CLEAR;
        int idx = offset + row;
        if(idx >= 0 && idx < (int)entries.size())
        {
            const auto& entry = entries[idx];
            if(idx == cursor)
                output += editor.theme.selection();
            output += "  ";
            output += editor.theme.uiAccent();
            output += entry.hash;
            output += editor.theme.baseFg();
            output += " " + entry.subject;
            output += editor.theme.reset();
        }
        else
        {
            output += editor.theme.uiGutter();
            output += "  ~";
            output += editor.theme.baseFg();
        }
    }

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.statusBar();
    std::string status = " FIXUP";
    std::string right =
        " " + std::to_string(entries.empty() ? 0 : cursor + 1) + "/" +
        std::to_string(entries.size()) + " ";
    output += status;
    int padding = editor.screenCols - status.length() - right.length();
    if(padding > 0)
        output.append(padding, ' ');
    output += right;
    output += editor.theme.reset();

    output += Terminal::NEWLINE_CLEAR;
    if(confirmActive)
    {
        output += editor.theme.uiWarning();
        output += "Fixup " + confirmHash + "? [y]es [n]o [p]atch";
        output += editor.theme.baseFg();
    }
    else if(!editor.statusMessage.empty())
    {
        output += editor.statusMessage;
    }

    Terminal::write(output);
    Terminal::flush();
}
