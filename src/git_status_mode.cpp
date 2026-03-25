#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

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

std::string shell_escape_single(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 8);
    out += keyCode(command::CommandKey::KEY_APOSTROPHE);
    for(char ch : text)
    {
        if(ch == keyCode(command::CommandKey::KEY_APOSTROPHE))
            out += "'\\''";
        else
            out.push_back(ch);
    }
    out += keyCode(command::CommandKey::KEY_APOSTROPHE);
    return out;
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
            if(pos < s.size())
                out.push_back(s.substr(pos));
            break;
        }
        out.push_back(s.substr(pos, next - pos));
        pos = next + 1;
    }
    return out;
}

std::string decode_git_status_path(std::string path)
{
    auto unescape = [](std::string_view raw) -> std::string
    {
        std::string result;
        for(size_t i = 0; i < raw.size(); ++i)
        {
            char c = raw[i];
            if(c == '\\' && i + 1 < raw.size())
            {
                ++i;
                char next = raw[i];
                if(next == 'n')
                    c = '\n';
                else if(next == 't')
                    c = '\t';
                else
                    c = next;
            }
            result.push_back(c);
        }
        return result;
    };

    if(path.size() >= 2 && path.front() == '"' && path.back() == '"')
        return unescape(path.substr(1, path.size() - 2));
    return unescape(path);
}

int git_status_content_rows(int screenRows, int helpRows)
{
    const int headerRows = 1;
    const int statusRows = 1;
    return std::max(0, screenRows - headerRows - helpRows - statusRows);
}

std::string git_status_help_text()
{
    return "  [space: stage/unstage] [j/k: move] [g/G: top/bottom] [q/esc: close]";
}

std::vector<std::string> wrap_help(std::string_view text, int screenCols)
{
    std::vector<std::string> tokens;
    size_t i = 0;
    while(i < text.size())
    {
        while(i < text.size() && text_utils::is_space(text[i]))
            ++i;
        if(i >= text.size())
            break;

        if(text[i] == keyCode(command::CommandKey::KEY_LEFT_BRACKET))
        {
            size_t start = i;
            size_t end = text.find(keyCode(command::CommandKey::KEY_RIGHT_BRACKET), i);
            if(end == std::string::npos)
            {
                tokens.emplace_back(text.substr(start));
                break;
            }
            tokens.emplace_back(text.substr(start, end - start + 1));
            i = end + 1;
            continue;
        }

        size_t start = i;
        while(i < text.size() && !text_utils::is_space(text[i]) &&
              text[i] != keyCode(command::CommandKey::KEY_LEFT_BRACKET))
            ++i;
        tokens.emplace_back(text.substr(start, i - start));
    }

    std::vector<std::string> lines;
    std::string current;
    int currentW = 0;
    for(const auto& tok : tokens)
    {
        int tokW = text_utils::utf8DisplayWidth(tok);
        int spaceW = current.empty() ? 0 : 1;
        if(currentW + spaceW + tokW > screenCols)
        {
            if(!current.empty())
            {
                lines.push_back(current);
                current.clear();
                currentW = 0;
                spaceW = 0;
            }
        }

        if(spaceW)
        {
            current.push_back(keyCode(control::ControlKey::SPACE));
            currentW += 1;
        }
        current.append(tok);
        currentW += tokW;
    }
    if(!current.empty())
        lines.push_back(current);
    return lines;
}

std::string utf8PrefixByWidth(std::string_view text, int maxWidth)
{
    if(maxWidth <= 0)
        return "";
    int width = 0;
    int pos = 0;
    while(pos < (int)text.size())
    {
        int next = text_utils::nextUtf8CharStart(text, pos);
        int charWidth =
            text_utils::utf8DisplayWidth(text.substr(pos, next - pos));
        if(width + charWidth > maxWidth)
            break;
        width += charWidth;
        pos = next;
    }
    return std::string(text.substr(0, pos));
}

std::string utf8SuffixByWidth(std::string_view text, int maxWidth)
{
    if(maxWidth <= 0)
        return "";
    int width = 0;
    int pos = (int)text.size();
    while(pos > 0)
    {
        int prev = text_utils::prevUtf8CharStart(text, pos);
        int charWidth =
            text_utils::utf8DisplayWidth(text.substr(prev, pos - prev));
        if(width + charWidth > maxWidth)
            break;
        width += charWidth;
        pos = prev;
    }
    return std::string(text.substr(pos));
}
} // namespace

void GitStatusViewMode::on_enter(ModeContext& ctx)
{
    cursor = 0;
    offset = 0;
    refreshStatus(*ctx.editor);
    ctx.requestFullRedraw();
}

void GitStatusViewMode::on_exit(ModeContext& ctx)
{
    ctx.requestFullRedraw();
}

void GitStatusViewMode::refreshStatus(Editor& editor)
{
    if(repoDir.empty())
        return;

    std::string previousPath;
    if(cursor >= 0 && cursor < (int)entries.size())
        previousPath = entries[cursor].path;

    entries.clear();
    std::string cmd =
        "git -C \"" + repoDir + "\" status --porcelain 2>/dev/null";
    std::string raw =
        run_git_raw(cmd);
    auto lines = split_lines(raw);
    for(const auto& line : lines)
    {
        if(line.size() < 3)
            continue;
        char index = line[0];
        char worktree = line[1];
        if(index == keyCode(control::ControlKey::SPACE) &&
           worktree == keyCode(control::ControlKey::SPACE))
            continue;
        size_t pos = 3;
        while(pos < line.size() && line[pos] == ' ')
            ++pos;
        if(pos >= line.size())
            continue;
        std::string rawPath = line.substr(pos);
        std::string path;
        size_t arrow = rawPath.find(" -> ");
        if(arrow != std::string::npos)
            path = decode_git_status_path(rawPath.substr(arrow + 4));
        else
            path = decode_git_status_path(rawPath);
        if(path.empty())
            continue;

        entries.push_back({path, index, worktree});
    }

    auto helpLines = wrap_help(git_status_help_text(), editor.screenCols);
    int contentRows = git_status_content_rows(editor.screenRows,
                                             (int)helpLines.size());
    if(entries.empty())
    {
        cursor = 0;
        offset = 0;
        return;
    }

    cursor = std::clamp(cursor, 0, (int)entries.size() - 1);
    if(!previousPath.empty())
    {
        auto it = std::find_if(
            entries.begin(), entries.end(),
            [&](const Entry& e) { return e.path == previousPath; });
        if(it != entries.end())
            cursor = (int)std::distance(entries.begin(), it);
    }

    offset = std::clamp(offset, 0,
                        std::max(0, (int)entries.size() - contentRows));
}

std::optional<ModeState> GitStatusViewMode::handle(ModeContext& ctx, int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(typed::TypedKey::KEY_Q))
    {
        if(c == keyCode(control::ControlKey::ESC))
            ed->noteDoubleEscStatusClear();
        return defaultExitMode(ed);
    }

    if(c == keyCode(typed::TypedKey::KEY_J) ||
       c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        if(cursor < (int)entries.size() - 1)
        {
            cursor++;
            int helpRows =
                (int)wrap_help(git_status_help_text(), ed->screenCols).size();
            int contentRows = git_status_content_rows(ed->screenRows, helpRows);
            if(cursor >= offset + contentRows)
                offset = cursor - contentRows + 1;
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
        cursor = std::max(0, (int)entries.size() - 1);
        int helpRows =
            (int)wrap_help(git_status_help_text(), ed->screenCols).size();
        int contentRows = git_status_content_rows(ed->screenRows, helpRows);
        offset = std::max(0, cursor - contentRows + 1);
    }
    else if(c == keyCode(control::ControlKey::SPACE))
    {
        if(cursor >= 0 && cursor < (int)entries.size() && !repoDir.empty())
        {
            Entry entry = entries[cursor];
            std::string cmd;
            std::string repoDirEsc = shell_escape_single(repoDir);
            std::string pathEsc = shell_escape_single(entry.path);
            if(entry.worktreeStatus != keyCode(control::ControlKey::SPACE) ||
               entry.worktreeStatus == keyCode(command::CommandKey::KEY_QUESTION))
            {
                cmd = "git -C " + repoDirEsc + " add -- " + pathEsc +
                      " 2>/dev/null";
            }
            else if(entry.indexStatus != keyCode(control::ControlKey::SPACE))
            {
                cmd = "git -C " + repoDirEsc + " restore --staged -- " +
                      pathEsc + " 2>/dev/null";
            }
            if(!cmd.empty())
            {
                std::system(cmd.c_str());
                refreshStatus(*ed);
                ed->needsFullRedraw = true;
            }
        }
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}

void GitStatusViewMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols);

    std::string help = git_status_help_text();
    auto helpLines = wrap_help(help, editor.screenCols);

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::cursorPos(1, 1);
    if(!Terminal::isTmux())
        output += Terminal::ESC_CLEAR_SCREEN;
    output += editor.theme.reset();

    output += Terminal::ESC_CLEAR_LINE;
    output += Terminal::ESC_BOLD;
    std::string header = "  GIT STATUS";
    if(!repoRoot.empty())
        header += " - " + repoRoot;
    output += header;
    output += editor.theme.reset();
    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    if(helpLines.empty())
        helpLines.push_back("");
    for(size_t i = 0; i < helpLines.size(); ++i)
    {
        output += helpLines[i];
        if(i + 1 < helpLines.size())
            output += Terminal::NEWLINE_CLEAR;
    }
    output += editor.theme.baseFg();

    int contentRows =
        git_status_content_rows(editor.screenRows, (int)helpLines.size());
    for(int row = 0; row < contentRows; ++row)
    {
        output += Terminal::NEWLINE_CLEAR;
        int idx = offset + row;
        if(idx >= 0 && idx < (int)entries.size())
        {
            const Entry& entry = entries[idx];
            bool isSelected = (idx == cursor);
            bool isUntracked =
                entry.indexStatus == keyCode(command::CommandKey::KEY_QUESTION) &&
                entry.worktreeStatus == keyCode(command::CommandKey::KEY_QUESTION);
            bool hasUnstaged =
                entry.worktreeStatus != keyCode(control::ControlKey::SPACE) &&
                entry.worktreeStatus != keyCode(command::CommandKey::KEY_QUESTION);
            bool hasStaged =
                entry.indexStatus != keyCode(control::ControlKey::SPACE) &&
                entry.indexStatus != keyCode(command::CommandKey::KEY_QUESTION);

            std::string bgSeq;
            if(isSelected)
            {
                bgSeq = editor.theme.selection();
            }
            else if(isUntracked)
            {
                bgSeq = editor.theme.uiDimBg();
            }
            else if(hasUnstaged)
            {
                bgSeq = editor.theme.uiErrorBg();
            }
            else if(hasStaged)
            {
                bgSeq = editor.theme.uiSuccessBg();
            }

            if(!bgSeq.empty())
                output += bgSeq;
            output += editor.theme.baseFg();

            std::string status;
            status.push_back(entry.indexStatus);
            status.push_back(entry.worktreeStatus);
            output += " ";
            output += status;
            output += " ";

            int available = std::max(0, editor.screenCols - 3);
            std::string path = entry.path;
            int pathWidth = text_utils::utf8DisplayWidth(path);
            if(available > 0 && pathWidth > available)
            {
                if(available > 3)
                {
                    std::string tail = utf8SuffixByWidth(path, available - 3);
                    path = "..." + tail;
                }
                else
                {
                    path = utf8PrefixByWidth(path, available);
                }
                pathWidth = text_utils::utf8DisplayWidth(path);
            }
            output += path;
            int used = 3 + pathWidth;
            if(used < editor.screenCols)
                output.append(editor.screenCols - used, ' ');
            output += editor.theme.base();
        }
        else
        {
            output += editor.theme.uiGutter();
            output += "~";
            output += editor.theme.baseFg();
            if(editor.screenCols > 1)
                output.append(editor.screenCols - 1, ' ');
        }
    }

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.statusBar();
    std::string status = " GIT STATUS";
    if(!repoRoot.empty())
        status += " | " + repoRoot;
    std::string right = " " + std::to_string(entries.size()) + " files ";
    output += status;
    int padding = editor.screenCols - status.length() - right.length();
    if(padding > 0)
        output.append(padding, keyCode(control::ControlKey::SPACE));
    output += right;
    output += editor.theme.reset();

    output += Terminal::NEWLINE_CLEAR;
    if(!editor.statusMessage.empty())
        output += editor.statusMessage;

    const bool syncOutput = Terminal::useSynchronizedOutput();
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
    Terminal::write(output);
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
    Terminal::flush();
}
