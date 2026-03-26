#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <cstdio>
#include <string>

namespace
{
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
        "\" --no-pager log --no-color --pretty=format:%h%x1f%s -n 100 2>/dev/null";
    auto lines = run_git_lines(cmd);
    std::vector<GitFixupMode::Entry> entries;
    for(const auto& line : lines)
    {
        if(line.empty())
            continue;
        size_t sep = line.find('\x1f');
        if(sep == std::string::npos)
            continue;
        GitFixupMode::Entry entry;
        entry.hash = line.substr(0, sep);
        entry.subject = line.substr(sep + 1);
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::string truncate_utf8_width(std::string_view text, int maxWidth)
{
    if(maxWidth <= 0)
        return "";
    std::string out;
    int width = 0;
    int pos = 0;
    while(pos < (int)text.size())
    {
        int next = text_utils::nextUtf8CharStart(text, pos);
        int charWidth =
            text_utils::utf8DisplayWidth(text.substr(pos, next - pos));
        if(width + charWidth > maxWidth)
            break;
        out.append(text.substr(pos, next - pos));
        width += charWidth;
        pos = next;
    }
    return out;
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
                                              int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    if(c == keyCode(control::ControlKey::ESC) || c == keyCode(typed::TypedKey::KEY_Q))
    {
        return returnStage;
    }

    if(c == keyCode(typed::TypedKey::KEY_J) || c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        if(cursor < (int)entries.size() - 1)
        {
            cursor++;
            int visible = ed->screenRows - 3;
            if(cursor >= offset + visible)
                offset = cursor - visible + 1;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_K) || c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        if(cursor > 0)
        {
            cursor--;
            if(cursor < offset)
                offset = cursor;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_G))
    {
        int nextChar = Terminal::readKey();
        if(nextChar == keyCode(typed::TypedKey::KEY_G))
        {
            cursor = 0;
            offset = 0;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_CAP_G))
    {
        if(!entries.empty())
        {
            cursor = std::max(0, (int)entries.size() - 1);
            int visible = ed->screenRows - 3;
            offset = std::max(0, cursor - visible + 1);
        }
    }
    else if(c == keyCode(control::ControlKey::SPACE) ||
            c == keyCode(typed::TypedKey::KEY_F) ||
            c == keyCode(control::ControlKey::ENTER))
    {
        if(cursor >= 0 && cursor < (int)entries.size())
        {
            const std::string& targetHash = entries[cursor].hash;
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
                              targetHash + " 2>/dev/null";
            if(std::system(cmd.c_str()) != 0)
            {
                ed->setStatusMessage("fixup commit failed");
                return std::nullopt;
            }
            ed->setStatusMessage("fixup commit created");
            return returnStage;
        }
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}

void GitFixupMode::draw(Editor& editor) const
{
    auto* self = const_cast<GitFixupMode*>(this);
    self->cursor = std::clamp(self->cursor, 0,
                              entries.empty() ? 0 : (int)entries.size() - 1);
    int window = std::max(1, editor.screenRows - 2);
    int maxScroll = std::max(0, (int)entries.size() - window);
    self->offset = std::clamp(self->offset, 0, maxScroll);

    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::ESC_CURSOR_HOME;
    output += Terminal::cursorPos(1, 1);
    output += editor.theme.reset();

    output += Terminal::ESC_CLEAR_LINE;
    output += editor.theme.uiAccent();
    output += Terminal::ESC_BOLD;
    output += "  GIT FIXUP";
    if(!repoRoot.empty())
        output += " - " + repoRoot;
    output += editor.theme.reset();
    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    output += "  [space: fixup] [q: back] [j/k: commit]";
    output += editor.theme.baseFg();

    int availableRows = editor.screenRows - 2;
    for(int row = 0; row < availableRows; ++row)
    {
        output += Terminal::NEWLINE_CLEAR;
        int idx = offset + row;
        if(idx >= 0 && idx < (int)entries.size())
        {
            const auto& entry = entries[idx];
            output += "  ";
            bool selected = (idx == cursor);
            std::string hash = entry.hash;
            if(hash.size() > 12)
                hash.resize(12);
            int maxLine = std::max(0, editor.screenCols - 6);
            std::string subject = entry.subject;
            int keep = maxLine - (int)hash.size() - 2;
            if(keep < 0)
                keep = 0;
            if(text_utils::utf8DisplayWidth(subject) > keep)
                subject = truncate_utf8_width(subject, keep);

            if(selected)
            {
                output += editor.theme.selection();
                output += " ";
                output += hash;
                output += " ";
                output += subject;
            }
            else
            {
                output += editor.theme.baseFg();
                output += " ";
                output += editor.theme.uiAccent();
                output += hash;
                output += editor.theme.baseFg();
                output += " ";
                output += subject;
            }
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
        output.append(padding, keyCode(control::ControlKey::SPACE));
    output += right;
    output += editor.theme.reset();

    output += Terminal::NEWLINE_CLEAR;
    if(entries.empty())
    {
        output += editor.theme.uiWarning();
        output += "No commits found";
        output += editor.theme.baseFg();
    }
    else if(!editor.statusMessage.empty())
    {
        output += editor.statusMessage;
    }

    Terminal::write(output);
    Terminal::flush();
}
