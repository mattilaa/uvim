#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "os_compat.h"
#include <algorithm>
#include <string>

namespace
{
std::string run_git_raw(const std::string& cmd)
{
    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
        return {};
    std::string output;
    char buffer[4096];
    size_t n = 0;
    while((n = fread(buffer, 1, sizeof(buffer), pipe)) > 0)
        output.append(buffer, n);
    pclose(pipe);
    return output;
}

std::vector<std::string> split_lines(const std::string& s)
{
    std::vector<std::string> out;
    size_t pos = 0;
    while(pos <= s.size())
    {
        size_t next = s.find('\n', pos);
        if(next == std::string::npos)
        {
            out.push_back(s.substr(pos));
            break;
        }
        out.push_back(s.substr(pos, next - pos));
        pos = next + 1;
    }
    return out;
}

std::vector<GitPatchMode::Hunk> parse_hunks(const std::string& diff)
{
    std::vector<GitPatchMode::Hunk> hunks;
    GitPatchMode::Hunk current;
    bool inHunk = false;
    std::string currentFile;
    auto lines = split_lines(diff);
    for(const auto& line : lines)
    {
        if(line.rfind("diff --git ", 0) == 0)
        {
            if(inHunk)
            {
                hunks.push_back(current);
                current = {};
                inHunk = false;
            }
        }
        if(line.rfind("+++ b/", 0) == 0)
        {
            currentFile = line.substr(6);
            continue;
        }
        if(line.rfind("@@ ", 0) == 0)
        {
            if(inHunk)
            {
                hunks.push_back(current);
                current = {};
            }
            inHunk = true;
            current.file = currentFile;
            current.lines.clear();
            current.lines.push_back(line);
            current.patch.clear();
            continue;
        }
        if(inHunk)
        {
            current.lines.push_back(line);
        }
    }
    if(inHunk)
        hunks.push_back(current);

    for(auto& h : hunks)
    {
        std::string patch;
        patch += "diff --git a/" + h.file + " b/" + h.file + "\n";
        patch += "--- a/" + h.file + "\n";
        patch += "+++ b/" + h.file + "\n";
        for(const auto& line : h.lines)
            patch += line + "\n";
        h.patch = patch;
    }

    return hunks;
}
} // namespace

void GitPatchMode::on_enter(ModeContext& ctx)
{
    if(hunks.empty())
    {
        std::string cmd = "git -C \"" + repoDir + "\" --no-pager diff";
        if(!fixupFiles.empty())
        {
            cmd += " --";
            for(const auto& file : fixupFiles)
                cmd += " \"" + file + "\"";
        }
        cmd += " 2>/dev/null";
        std::string diff = run_git_raw(cmd);
        hunks = parse_hunks(diff);
    }
    hunkIndex = std::clamp(hunkIndex, 0, std::max(0, (int)hunks.size() - 1));
    scrollOffset = 0;
    ctx.requestFullRedraw();
}

void GitPatchMode::on_exit(ModeContext& ctx)
{
    ctx.requestFullRedraw();
}

std::optional<ModeState> GitPatchMode::handle(ModeContext& ctx,
                                              int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    if(c == keyCode(control::ControlKey::ESC) || c == keyCode(typed::TypedKey::KEY_Q))
    {
        return returnStage;
    }

    if(hunks.empty())
    {
        return returnStage;
    }

    if(c == keyCode(typed::TypedKey::KEY_J) || c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        int maxScroll = std::max(0, (int)hunks[hunkIndex].lines.size() -
                                      (ed->screenRows - 3));
        if(scrollOffset < maxScroll)
            scrollOffset++;
    }
    else if(c == keyCode(typed::TypedKey::KEY_K) || c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        if(scrollOffset > 0)
            scrollOffset--;
    }
    else if(c == keyCode(control::ControlKey::CTRL_J))
    {
        if(hunkIndex < (int)hunks.size() - 1)
        {
            hunkIndex++;
            scrollOffset = 0;
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_K))
    {
        if(hunkIndex > 0)
        {
            hunkIndex--;
            scrollOffset = 0;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_Y))
    {
        std::string cmd =
            "git -C \"" + repoDir + "\" apply --cached --unidiff-zero";
        FILE* pipe = popen(cmd.c_str(), "w");
        if(pipe)
        {
            fwrite(hunks[hunkIndex].patch.data(), 1,
                   hunks[hunkIndex].patch.size(), pipe);
            pclose(pipe);
        }
        if(hunkIndex < (int)hunks.size() - 1)
        {
            hunkIndex++;
            scrollOffset = 0;
        }
        else
        {
            std::string commitCmd =
                "git -C \"" + repoDir + "\" commit --fixup " + targetHash +
                " 2>/dev/null";
            std::system(commitCmd.c_str());
            ed->setStatusMessage("fixup commit created");
            return returnStage;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_N))
    {
        if(hunkIndex < (int)hunks.size() - 1)
        {
            hunkIndex++;
            scrollOffset = 0;
        }
        else
        {
            std::string commitCmd =
                "git -C \"" + repoDir + "\" commit --fixup " + targetHash +
                " 2>/dev/null";
            std::system(commitCmd.c_str());
            ed->setStatusMessage("fixup commit created");
            return returnStage;
        }
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}

void GitPatchMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::cursorPos(1, 1);
    output += editor.theme.reset();

    output += Terminal::ESC_CLEAR_LINE;
    output += Terminal::ESC_BOLD;
    output += "  GIT PATCH";
    output += editor.theme.reset();
    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    output += "  [y: stage] [n: skip] [ctrl-j/k: next/prev] [q/esc: back]";
    output += editor.theme.baseFg();

    int availableRows = editor.screenRows - 3;
    if(hunks.empty())
    {
        for(int i = 0; i < availableRows; ++i)
        {
            output += Terminal::NEWLINE_CLEAR;
            if(i == 0)
            {
                output += editor.theme.uiDim();
                output += "  (no hunks)";
                output += editor.theme.baseFg();
            }
            else
            {
                output += editor.theme.uiGutter();
                output += "  ~";
                output += editor.theme.baseFg();
            }
        }
    }
    else
    {
        const auto& hunk = hunks[hunkIndex];
        for(int i = 0; i < availableRows; ++i)
        {
            output += Terminal::NEWLINE_CLEAR;
            int lineIdx = scrollOffset + i;
            if(lineIdx >= 0 && lineIdx < (int)hunk.lines.size())
            {
                const std::string& line = hunk.lines[lineIdx];
                output += "  ";
                if(!line.empty() && line[0] == keyCode(command::CommandKey::KEY_PLUS))
                    output += editor.theme.uiSuccess();
                else if(!line.empty() && line[0] == keyCode(command::CommandKey::KEY_MINUS))
                    output += editor.theme.uiError();
                else if(line.rfind("@@ ", 0) == 0)
                    output += editor.theme.uiInfo();
                else
                    output += editor.theme.baseFg();
                output += line;
                output += editor.theme.reset();
            }
            else
            {
                output += editor.theme.uiGutter();
                output += "  ~";
                output += editor.theme.baseFg();
            }
        }
    }

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.statusBar();
    std::string status = " PATCH";
    std::string right =
        " " + std::to_string(hunks.empty() ? 0 : hunkIndex + 1) + "/" +
        std::to_string(hunks.size()) + " ";
    output += status;
    int padding = editor.screenCols - status.length() - right.length();
    if(padding > 0)
        output.append(padding, keyCode(control::ControlKey::SPACE));
    output += right;
    output += editor.theme.reset();

    output += Terminal::NEWLINE_CLEAR;
    if(!editor.statusMessage.empty())
        output += editor.statusMessage;

    Terminal::write(output);
    Terminal::flush();
}
