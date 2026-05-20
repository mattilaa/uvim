#include "editor.h"
#include "header_help.h"
#include "mode_state_machine.h"
#include "process_pipe.h"
#include "terminal.h"
#include <algorithm>
#include <string>
#include <vector>

namespace editor::statemachine
{
namespace
{
std::vector<std::string> gitFixupHelpTokens()
{
    return {"[j/k: move]", "[space/enter: fixup]", "[q/esc: back]"};
}

int gitFixupContentRows(const Editor& editor)
{
    const int headerRows =
        1 + HeaderHelp::lineCount(gitFixupHelpTokens(), editor.screenCols);
    constexpr int footerRows = 2;
    return std::max(1, editor.screenRows - headerRows - footerRows);
}

std::vector<std::string> run_git_lines(const std::vector<std::string>& args)
{
    std::vector<std::string> out;
    ProcessPipe pipe(args);
    if(!pipe)
        return out;
    std::string output = pipe.readAll();

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
    auto lines =
        run_git_lines({"git", "-C", repoDir, "--no-pager", "log", "--no-color",
                       "--pretty=format:%h\t%s", "-n", "100"});
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

std::optional<ModeState> GitFixupMode::handle(ModeContext& ctx, int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    if(confirmActive)
    {
        if(c == keyCode(typed::TypedKey::KEY_Y) ||
           c == keyCode(typed::TypedKey::KEY_CAP_Y))
        {
            if(!fixupFiles.empty())
            {
                for(const auto& file : fixupFiles)
                {
                    ProcessPipe pipe({"git", "-C", repoDir, "add", "--", file});
                    pipe.close();
                }
            }
            ProcessPipe pipe(
                {"git", "-C", repoDir, "commit", "--fixup", confirmHash});
            pipe.close();
            ed->setStatusMessage("fixup commit created");
            return returnStage;
        }
        if(c == keyCode(typed::TypedKey::KEY_P) ||
           c == keyCode(typed::TypedKey::KEY_CAP_P))
        {
            GitPatchMode::Hunk dummy;
            std::vector<GitPatchMode::Hunk> hunks;
            return GitPatchMode{std::move(hunks), repoRoot,   repoDir,
                                confirmHash,      fixupFiles, returnStage};
        }
        if(c == keyCode(typed::TypedKey::KEY_N) ||
           c == keyCode(typed::TypedKey::KEY_CAP_N) ||
           c == keyCode(control::ControlKey::ESC))
        {
            confirmActive = false;
            confirmHash.clear();
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(typed::TypedKey::KEY_Q))
    {
        return returnStage;
    }

    if(c == keyCode(typed::TypedKey::KEY_J) ||
       c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        if(cursor < (int)entries.size() - 1)
        {
            cursor++;
            int visible = gitFixupContentRows(*ed);
            if(cursor >= offset + visible)
                offset = cursor - visible + 1;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_K) ||
            c == keyCode(navigation::NavigationKey::ARROW_UP))
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
            int visible = gitFixupContentRows(*ed);
            offset = std::max(0, cursor - visible + 1);
        }
    }
    else if(c == keyCode(control::ControlKey::SPACE) ||
            c == keyCode(typed::TypedKey::KEY_F) ||
            c == keyCode(control::ControlKey::ENTER))
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
    HeaderHelp::append(output, editor.theme, editor.screenCols,
                       gitFixupHelpTokens());

    int availableRows = gitFixupContentRows(editor);
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
    std::string right = " " + std::to_string(entries.empty() ? 0 : cursor + 1) +
                        "/" + std::to_string(entries.size()) + " ";
    output += status;
    int padding = editor.screenCols - status.length() - right.length();
    if(padding > 0)
        output.append(padding, keyCode(control::ControlKey::SPACE));
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
} // namespace editor::statemachine
