#include "git_status_mode.h"

#include "editor.h"
#include "mode_state_machine.h"
#include "process_pipe.h"
#include "terminal.h"
#include "text_utils.h"

#include <algorithm>
#include <string_view>

namespace editor::statemachine
{
namespace
{
int visibleRows(const Editor& editor)
{
    return std::max(1, editor.screenRows - 2);
}

std::string_view lineColor(
    const Theme& theme,
    std::string_view line,
    int& section)
{
    if(line.starts_with("Changes to be committed:"))
    {
        section = 1;
        return theme.uiSuccess();
    }
    if(line.starts_with("Changes not staged for commit:"))
    {
        section = 2;
        return theme.uiError();
    }
    if(line.starts_with("Untracked files:"))
    {
        section = 3;
        return theme.uiWarning();
    }
    if(line.starts_with("Ignored files:"))
    {
        section = 4;
        return theme.uiDim();
    }
    if(line.starts_with("On branch ") || line.starts_with("HEAD detached"))
    {
        section = 0;
        return theme.uiAccent();
    }
    if(line.starts_with("nothing to commit") ||
       line.starts_with("nothing added to commit"))
        return theme.uiSuccess();
    if(line.find("(use ") != std::string_view::npos)
        return theme.uiDim();
    if(line.empty() || line.front() != ' ')
        return theme.baseFg();
    switch(section)
    {
    case 1:
        return theme.uiSuccess();
    case 2:
        return theme.uiError();
    case 3:
        return theme.uiWarning();
    case 4:
        return theme.uiDim();
    default:
        return theme.baseFg();
    }
}

std::string clipLine(std::string_view line, int width)
{
    if(width <= 0)
        return {};
    size_t end = 0;
    int used = 0;
    while(end < line.size())
    {
        const int next = text_utils::nextUtf8CharStart(line, (int)end);
        const int charWidth = text_utils::utf8DisplayWidth(
            line.substr(end, (size_t)next - end));
        if(used + charWidth > width)
            break;
        used += charWidth;
        end = (size_t)next;
    }
    return std::string(line.substr(0, end));
}
} // namespace

void GitStatusMode::on_enter(ModeContext& ctx)
{
    refresh(*ctx.editor);
    ctx.requestFullRedraw();
}

void GitStatusMode::on_exit(ModeContext& ctx)
{
    ctx.requestFullRedraw();
}

void GitStatusMode::refresh(Editor& editor)
{
    ProcessPipe pipe;
    if(!pipe.open(
           {"git", "-C", repoDir, "-c", "color.status=false", "status"},
           "r", true))
    {
        lines = {"(could not start git status)"};
        offset = 0;
        editor.needsFullRedraw = true;
        return;
    }
    const std::string raw = pipe.readAll();
    if(pipe.close() != 0)
    {
        lines = {"(git status failed)"};
        offset = 0;
        editor.needsFullRedraw = true;
        return;
    }

    lines.clear();
    size_t pos = 0;
    while(pos < raw.size())
    {
        const size_t end = raw.find('\n', pos);
        if(end == std::string::npos)
        {
            lines.push_back(raw.substr(pos));
            break;
        }
        lines.push_back(raw.substr(pos, end - pos));
        pos = end + 1;
    }
    if(lines.empty())
        lines.push_back("(no status output)");
    offset = std::clamp(offset, 0,
                        std::max(0, (int)lines.size() - visibleRows(editor)));
    editor.needsFullRedraw = true;
}

std::optional<ModeState> GitStatusMode::handle(
    ModeContext& ctx,
    const ModeKeyEvent& event)
{
    const int key = keyCode(event.key);
    if(key == keyCode(control::ControlKey::ESC) ||
       key == keyCode(typed::TypedKey::KEY_Q))
    {
#ifdef UVIM_ENABLE_BROWSER_TOOLS
        if(returnMode == FILE_BROWSER)
        {
            FileBrowserMode browser{returnBrowseDirectory};
            browser.browserCursor = returnBrowseCursor;
            browser.browserOffset = returnBrowseOffset;
            return browser;
        }
#endif
        if(returnMode == WELCOME)
            return WelcomeMode{};
        if(returnMode == NORMAL)
            return NormalMode{};
        return defaultExitMode(ctx.editor);
    }
    if(key == keyCode(typed::TypedKey::KEY_R))
    {
        refresh(*ctx.editor);
        return std::nullopt;
    }

    const int maximum =
        std::max(0, (int)lines.size() - visibleRows(*ctx.editor));
    if(key == keyCode(typed::TypedKey::KEY_J) ||
       key == keyCode(navigation::NavigationKey::ARROW_DOWN))
        offset = std::min(maximum, offset + 1);
    else if(key == keyCode(typed::TypedKey::KEY_K) ||
            key == keyCode(navigation::NavigationKey::ARROW_UP))
        offset = std::max(0, offset - 1);
    else if(key == keyCode(control::ControlKey::CTRL_D) ||
            key == keyCode(navigation::NavigationKey::PAGE_DOWN))
        offset = std::min(maximum, offset + visibleRows(*ctx.editor) / 2);
    else if(key == keyCode(control::ControlKey::CTRL_U) ||
            key == keyCode(navigation::NavigationKey::PAGE_UP))
        offset = std::max(0, offset - visibleRows(*ctx.editor) / 2);
    else if(key == keyCode(typed::TypedKey::KEY_G))
        offset = 0;
    else if(key == keyCode(typed::TypedKey::KEY_CAP_G))
        offset = maximum;
    ctx.requestFullRedraw();
    return std::nullopt;
}

void GitStatusMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve((size_t)editor.screenRows * editor.screenCols * 2);
    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::ESC_CURSOR_HOME;
    output += editor.theme.reset();
    output += Terminal::ESC_CLEAR_LINE;
    output += editor.theme.uiAccent();
    output += Terminal::ESC_BOLD;
    output += "  GIT STATUS";
    output += editor.theme.reset();
    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    output += clipLine(
        "  [j/k: scroll] [g/G: top/bottom] [r: refresh] [q/esc: close]",
        editor.screenCols);
    output += editor.theme.reset();

    int section = 0;
    for(int i = 0; i < offset && i < (int)lines.size(); ++i)
        (void)lineColor(editor.theme, lines[i], section);
    for(int row = 0; row < visibleRows(editor); ++row)
    {
        output += Terminal::NEWLINE_CLEAR;
        const int index = offset + row;
        if(index < (int)lines.size())
        {
            const std::string_view color =
                lineColor(editor.theme, lines[index], section);
            output.append(color.data(), color.size());
            output += clipLine(lines[index], editor.screenCols);
            output += editor.theme.reset();
        }
        else
        {
            output += editor.theme.uiGutter();
            output += "~";
            output += editor.theme.reset();
        }
    }
    output += Terminal::NEWLINE_CLEAR;
    editor.appendStatusLine(
        output, " GIT STATUS | " + repoDir,
        " " + std::to_string(std::min((int)lines.size(), offset + 1)) +
            "/" + std::to_string(lines.size()) + " ");
    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.baseFg();
    if(!editor.statusMessage.empty())
        output += clipLine(editor.statusMessage, editor.screenCols);
    Terminal::write(output);
    Terminal::flush();
}
} // namespace editor::statemachine
