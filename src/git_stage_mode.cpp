#include "ascii.h"
#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unistd.h>

namespace
{
struct StatusEntry
{
    std::string path;
    char indexStatus = keyCode(control::ControlKey::SPACE);
    char worktreeStatus = keyCode(control::ControlKey::SPACE);
};

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

std::string trim_newline(std::string s)
{
    while(!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

std::string git_show_toplevel(const std::string& dir)
{
    std::string cmd =
        "git -C \"" + dir + "\" rev-parse --show-toplevel 2>/dev/null";
    return trim_newline(run_git_raw(cmd));
}

std::string git_branch_name(const std::string& dir)
{
    std::string cmd =
        "git -C \"" + dir +
        "\" branch --show-current 2>/dev/null";
    return trim_newline(run_git_raw(cmd));
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

std::string decode_git_path(std::string_view raw)
{
    auto unescape = [](std::string_view in) -> std::string
    {
        std::string out;
        out.reserve(in.size());
        for(size_t i = 0; i < in.size(); ++i)
        {
            char c = in[i];
            if(c == '\\' && i + 1 < in.size())
            {
                ++i;
                char next = in[i];
                if(next == 'n')
                    c = '\n';
                else if(next == 't')
                    c = '\t';
                else
                    c = next;
            }
            out.push_back(c);
        }
        return out;
    };

    if(raw.size() >= 2 &&
       raw.front() == keyCode(command::CommandKey::KEY_DOUBLE_QUOTE) &&
       raw.back() == keyCode(command::CommandKey::KEY_DOUBLE_QUOTE))
    {
        return unescape(raw.substr(1, raw.size() - 2));
    }
    return unescape(raw);
}

std::vector<StatusEntry> parse_status_porcelain_z(const std::string& raw)
{
    std::vector<StatusEntry> out;
    size_t i = 0;
    while(i + 2 < raw.size())
    {
        StatusEntry entry;
        entry.indexStatus = raw[i];
        entry.worktreeStatus = raw[i + 1];
        i += 2;
        if(i < raw.size() && raw[i] == keyCode(control::ControlKey::SPACE))
            ++i;

        size_t end = raw.find('\0', i);
        if(end == std::string::npos)
            break;
        std::string path = std::string(raw.substr(i, end - i));
        i = end + 1;

        if(entry.indexStatus == keyCode(typed::TypedKey::KEY_CAP_R) ||
           entry.indexStatus == keyCode(typed::TypedKey::KEY_CAP_C) ||
           entry.worktreeStatus == keyCode(typed::TypedKey::KEY_CAP_R) ||
           entry.worktreeStatus == keyCode(typed::TypedKey::KEY_CAP_C))
        {
            size_t end2 = raw.find('\0', i);
            if(end2 == std::string::npos)
                break;
            path = std::string(raw.substr(i, end2 - i));
            i = end2 + 1;
        }

        path = decode_git_path(path);
        if(path.empty())
            continue;
        entry.path = std::move(path);
        out.push_back(std::move(entry));
    }
    return out;
}

bool is_untracked(const StatusEntry& entry)
{
    return entry.indexStatus == keyCode(command::CommandKey::KEY_QUESTION) &&
           entry.worktreeStatus == keyCode(command::CommandKey::KEY_QUESTION);
}

bool has_staged(const StatusEntry& entry)
{
    return entry.indexStatus != keyCode(control::ControlKey::SPACE) &&
           entry.indexStatus != keyCode(command::CommandKey::KEY_QUESTION);
}

bool has_unstaged(const StatusEntry& entry)
{
    return entry.worktreeStatus != keyCode(control::ControlKey::SPACE) &&
           entry.worktreeStatus != keyCode(command::CommandKey::KEY_QUESTION);
}

std::string staged_label(char status)
{
    switch(status)
    {
    case keyCode(typed::TypedKey::KEY_CAP_A):
        return "new file:   ";
    case keyCode(typed::TypedKey::KEY_CAP_D):
        return "deleted:    ";
    case keyCode(typed::TypedKey::KEY_CAP_R):
        return "renamed:    ";
    case keyCode(typed::TypedKey::KEY_CAP_C):
        return "copied:     ";
    case keyCode(typed::TypedKey::KEY_CAP_M):
    default:
        return "modified:   ";
    }
}

std::string unstaged_label(char status)
{
    switch(status)
    {
    case keyCode(typed::TypedKey::KEY_CAP_D):
        return "deleted:    ";
    case keyCode(typed::TypedKey::KEY_CAP_M):
    default:
        return "modified:   ";
    }
}

std::string git_stage_help_text()
{
    return "  [space: stage/unstage] [j/k: move files] [d: diff] "
           "[ctrl-j/k: scroll diff] [ctrl-h/l: pan diff] [enter: open] "
           "[p: patch stage file] [f: fixup staged] [m: mark fixup] [g f: fixup marked] [r: refresh] [q/esc: close]";
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
            size_t end =
                text.find(keyCode(command::CommandKey::KEY_RIGHT_BRACKET), i);
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

int git_stage_content_rows(int screenRows, int helpRows)
{
    const int headerRows = 1;
    const int statusRows = 1;
    return std::max(0, screenRows - headerRows - helpRows - statusRows);
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

std::string bg_rgb(int r, int g, int b)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\x1b[48;2;%d;%d;%dm", r, g, b);
    return buf;
}

std::string status_path_bg(GitStageMode::FileGroup group)
{
    switch(group)
    {
    case GitStageMode::FileGroup::Staged:
        return bg_rgb(24, 64, 36);
    case GitStageMode::FileGroup::Unstaged:
        return bg_rgb(78, 26, 26);
    case GitStageMode::FileGroup::Untracked:
        return bg_rgb(42, 42, 42);
    case GitStageMode::FileGroup::None:
    default:
        return "";
    }
}

bool is_ansi_start(std::string_view text, size_t i)
{
    return i + 1 < text.size() && text[i] == '\x1b' &&
           text[i + 1] == keyCode(command::CommandKey::KEY_LEFT_BRACKET);
}

size_t skip_ansi(std::string_view text, size_t i)
{
    i += 2;
    while(i < text.size())
    {
        char c = text[i++];
        if((c >= keyCode(typed::TypedKey::KEY_CAP_A) &&
            c <= keyCode(typed::TypedKey::KEY_CAP_Z)) ||
           (c >= keyCode(typed::TypedKey::KEY_A) &&
            c <= keyCode(typed::TypedKey::KEY_Z)))
            break;
    }
    return i;
}

int max_diff_width(const std::vector<std::string>& lines, bool useDefaultColors)
{
    int maxW = 0;
    for(const auto& line : lines)
    {
        int w = useDefaultColors ? text_utils::displayWidth(line)
                                 : text_utils::utf8DisplayWidth(line);
        if(w > maxW)
            maxW = w;
    }
    return maxW;
}

std::string slice_plain(std::string_view text, int startCol, int width)
{
    if(width <= 0 || text.empty())
        return "";
    if(startCol < 0)
        startCol = 0;

    std::string out;
    int col = 0;
    int pos = 0;
    while(pos < (int)text.size())
    {
        int next = text_utils::nextUtf8CharStart(text, pos);
        int w = text_utils::utf8DisplayWidth(text.substr(pos, next - pos));
        if(col + w <= startCol)
        {
            col += w;
            pos = next;
            continue;
        }
        if(col >= startCol + width)
            break;
        if(col >= startCol && col + w <= startCol + width)
            out.append(text.substr(pos, next - pos));
        col += w;
        pos = next;
    }
    return out;
}

std::string slice_with_ansi(std::string_view text, int startCol, int width)
{
    if(width <= 0 || text.empty())
        return "";
    if(startCol < 0)
        startCol = 0;

    std::string out;
    int col = 0;
    size_t i = 0;
    while(i < text.size())
    {
        if(is_ansi_start(text, i))
        {
            size_t end = skip_ansi(text, i);
            out.append(text.substr(i, end - i));
            i = end;
            continue;
        }

        int next = text_utils::nextUtf8CharStart(text, (int)i);
        int w = text_utils::utf8DisplayWidth(text.substr(i, next - (int)i));
        if(col + w <= startCol)
        {
            col += w;
            i = next;
            continue;
        }
        if(col >= startCol + width)
            break;
        if(col >= startCol && col + w <= startCol + width)
            out.append(text.substr(i, next - (int)i));
        col += w;
        i = next;
    }
    return out;
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
    else if(!line.empty() && line[0] == keyCode(command::CommandKey::KEY_PLUS))
    {
        output += editor.theme.uiSuccess();
    }
    else if(!line.empty() && line[0] == keyCode(command::CommandKey::KEY_MINUS))
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

GitStageMode::GitStageMode(std::vector<Node> items, std::string root,
                           std::string dir)
    : nodes(std::move(items)), repoRoot(std::move(root)),
      repoDir(std::move(dir))
{
}

int GitStageMode::selectedRowIndex() const
{
    if(cursor < 0 || cursor >= (int)fileRows.size())
        return -1;
    return fileRows[cursor];
}

int GitStageMode::maxListHorizontalOffset(int listWidth) const
{
    int maxWidth = 0;
    for(const auto& row : rows)
    {
        int width = 0;
        if(row.kind == RowKind::File)
            width = text_utils::utf8DisplayWidth(row.prefix) +
                    text_utils::utf8DisplayWidth(row.path);
        else
            width = text_utils::utf8DisplayWidth(row.prefix);
        maxWidth = std::max(maxWidth, width);
    }
    return std::max(0, maxWidth - std::max(0, listWidth));
}

void GitStageMode::clampCursor()
{
    if(fileRows.empty())
    {
        cursor = 0;
        offset = 0;
        return;
    }
    cursor = std::clamp(cursor, 0, (int)fileRows.size() - 1);
}

void GitStageMode::keepCursorVisible(const Editor& editor)
{
    auto helpLines = wrap_help(git_stage_help_text(), editor.screenCols);
    int contentRows =
        git_stage_content_rows(editor.screenRows, (int)helpLines.size());
    int rowIndex = selectedRowIndex();
    if(rowIndex < 0)
    {
        offset = 0;
        return;
    }
    if(rowIndex < offset)
        offset = rowIndex;
    else if(rowIndex >= offset + contentRows)
        offset = rowIndex - contentRows + 1;
    offset = std::clamp(offset, 0, std::max(0, (int)rows.size() - contentRows));
}

void GitStageMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;

    char cwd[PATH_MAX];
    if(getcwd(cwd, sizeof(cwd)))
        viewRoot = cwd;
    else if(viewRoot.empty())
        viewRoot = ".";

    if(repoRoot.empty())
    {
        std::string root = git_show_toplevel(viewRoot);
        if(root.empty())
        {
            ctx.setStatusMessage("git stage: not a repo");
            rows.clear();
            fileRows.clear();
            ctx.requestFullRedraw();
            return;
        }
        repoRoot = root;
        repoDir = root;
    }

    if(viewRoot.empty())
        viewRoot = repoRoot;

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

    std::string previousPath;
    FileGroup previousGroup = FileGroup::None;
    int previousRow = selectedRowIndex();
    if(previousRow >= 0 && previousRow < (int)rows.size())
    {
        previousPath = rows[previousRow].path;
        previousGroup = rows[previousRow].group;
    }

    rows.clear();
    fileRows.clear();

    std::string statusCmd =
        "git -C \"" + repoDir + "\" status --porcelain -z 2>/dev/null";
    std::vector<StatusEntry> entries =
        parse_status_porcelain_z(run_git_raw(statusCmd));

    std::unordered_set<std::string> validPaths;
    for(const auto& entry : entries)
        validPaths.insert(entry.path);
    for(auto it = fixupMarked.begin(); it != fixupMarked.end();)
    {
        if(validPaths.find(*it) == validPaths.end())
            it = fixupMarked.erase(it);
        else
            ++it;
    }

    std::vector<StatusEntry> stagedEntries;
    std::vector<StatusEntry> unstagedEntries;
    std::vector<StatusEntry> untrackedEntries;
    for(const auto& entry : entries)
    {
        if(is_untracked(entry))
        {
            untrackedEntries.push_back(entry);
            continue;
        }
        if(has_staged(entry))
            stagedEntries.push_back(entry);
        if(has_unstaged(entry))
            unstagedEntries.push_back(entry);
    }

    std::sort(stagedEntries.begin(), stagedEntries.end(),
              [](const StatusEntry& a, const StatusEntry& b)
              { return a.path < b.path; });
    std::sort(unstagedEntries.begin(), unstagedEntries.end(),
              [](const StatusEntry& a, const StatusEntry& b)
              { return a.path < b.path; });
    std::sort(untrackedEntries.begin(), untrackedEntries.end(),
              [](const StatusEntry& a, const StatusEntry& b)
              { return a.path < b.path; });

    auto add_row = [&](StatusRow row)
    {
        if(row.kind == RowKind::File)
            fileRows.push_back((int)rows.size());
        rows.push_back(std::move(row));
    };

    std::string branch = git_branch_name(repoDir);
    if(!branch.empty())
        add_row({RowKind::Header, FileGroup::None, "On branch " + branch, ""});

    if(entries.empty())
    {
        add_row({RowKind::Blank, FileGroup::None, "", ""});
        add_row(
            {RowKind::Summary, FileGroup::None, "nothing to commit, working tree clean", ""});
    }
    else
    {
        if(!stagedEntries.empty())
        {
            add_row({RowKind::Blank, FileGroup::None, "", ""});
            add_row({RowKind::Header, FileGroup::None,
                     "Changes to be committed:", ""});
            add_row({RowKind::Hint, FileGroup::None,
                     "  (use \"git restore --staged <file>...\" to unstage)",
                     ""});
            for(const auto& entry : stagedEntries)
            {
                add_row({RowKind::File, FileGroup::Staged,
                         "        " + staged_label(entry.indexStatus),
                         entry.path, entry.indexStatus, entry.worktreeStatus});
            }
        }

        if(!unstagedEntries.empty())
        {
            add_row({RowKind::Blank, FileGroup::None, "", ""});
            add_row({RowKind::Header, FileGroup::None,
                     "Changes not staged for commit:", ""});
            add_row({RowKind::Hint, FileGroup::None,
                     "  (use \"git add <file>...\" to update what will be committed)",
                     ""});
            add_row({RowKind::Hint, FileGroup::None,
                     "  (use \"git restore <file>...\" to discard changes in working directory)",
                     ""});
            for(const auto& entry : unstagedEntries)
            {
                add_row({RowKind::File, FileGroup::Unstaged,
                         "        " + unstaged_label(entry.worktreeStatus),
                         entry.path, entry.indexStatus, entry.worktreeStatus});
            }
        }

        if(!untrackedEntries.empty())
        {
            add_row({RowKind::Blank, FileGroup::None, "", ""});
            add_row({RowKind::Header, FileGroup::None, "Untracked files:", ""});
            add_row({RowKind::Hint, FileGroup::None,
                     "  (use \"git add <file>...\" to include in what will be committed)",
                     ""});
            for(const auto& entry : untrackedEntries)
            {
                add_row({RowKind::File, FileGroup::Untracked, "        ",
                         entry.path, entry.indexStatus, entry.worktreeStatus});
            }
        }
    }

    clampCursor();
    if(!previousPath.empty())
    {
        for(size_t i = 0; i < fileRows.size(); ++i)
        {
            const StatusRow& row = rows[fileRows[i]];
            if(row.path == previousPath && row.group == previousGroup)
            {
                cursor = (int)i;
                break;
            }
        }
    }
    clampCursor();
    diffDirty = true;
    int listWidth = editor.screenCols;
    if(diffVisible)
        listWidth = std::max(36, editor.screenCols / 2);
    listHorizontalOffset =
        std::min(listHorizontalOffset, maxListHorizontalOffset(listWidth));
    keepCursorVisible(editor);
    return true;
}

static std::vector<std::string> collect_staged_paths(
    const std::vector<GitStageMode::StatusRow>& rows)
{
    std::vector<std::string> files;
    for(const auto& row : rows)
    {
        if(row.kind == GitStageMode::RowKind::File &&
           row.group == GitStageMode::FileGroup::Staged && !row.path.empty())
        {
            files.push_back(row.path);
        }
    }
    return files;
}

void GitStageMode::refreshDiff(Editor& editor)
{
    if(!diffDirty)
        return;

    diffLines.clear();
    diffPath.clear();
    diffStaged = false;
    diffOffset = 0;
    diffHorizontalOffset = 0;

    int rowIndex = selectedRowIndex();
    if(rowIndex < 0 || rowIndex >= (int)rows.size())
    {
        diffLines.push_back("(clean)");
        diffDirty = false;
        return;
    }

    const StatusRow& row = rows[rowIndex];
    diffPath = row.path;
    diffStaged = row.group == FileGroup::Staged;

    if(row.group == FileGroup::Untracked)
    {
        diffLines.push_back("(untracked file)");
        diffDirty = false;
        return;
    }

    std::string cacheKey =
        row.path + (row.group == FileGroup::Staged ? "|staged" : "|unstaged");
    auto cacheIt = diffCache.find(cacheKey);
    if(cacheIt != diffCache.end())
    {
        diffLines = cacheIt->second;
        diffDirty = false;
        return;
    }

    std::string cmd = "git -C \"" + repoDir + "\" --no-pager diff ";
    if(row.group == FileGroup::Staged)
        cmd += "--cached ";
    cmd += std::string(editor.gitUseDefaultColors ? "--color=always "
                                                  : "--no-color ");
    cmd += "-- \"" + row.path + "\" 2>/dev/null";

    std::string raw = run_git_raw(cmd);
    size_t pos = 0;
    while(pos <= raw.size())
    {
        size_t next = raw.find('\n', pos);
        if(next == std::string::npos)
        {
            if(pos < raw.size())
                diffLines.push_back(raw.substr(pos));
            break;
        }
        diffLines.push_back(raw.substr(pos, next - pos));
        pos = next + 1;
    }

    if(diffLines.empty())
        diffLines.push_back("(no diff)");

    diffCache.emplace(cacheKey, diffLines);
    diffCacheOrder.push_back(cacheKey);
    constexpr size_t kDiffCacheMax = 64;
    if(diffCacheOrder.size() > kDiffCacheMax)
    {
        const std::string oldest = diffCacheOrder.front();
        diffCache.erase(oldest);
        diffCacheOrder.erase(diffCacheOrder.begin());
    }

    diffDirty = false;
}

std::optional<ModeState> GitStageMode::handle(ModeContext& ctx, int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(typed::TypedKey::KEY_Q))
    {
        if(c == keyCode(control::ControlKey::ESC))
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

    if(c == keyCode(typed::TypedKey::KEY_R))
    {
        refreshStatus(*ed);
        if(diffVisible)
            refreshDiff(*ed);
        ed->needsFullRedraw = true;
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_M))
    {
        int rowIndex = selectedRowIndex();
        if(rowIndex >= 0 && rowIndex < (int)rows.size())
        {
            const std::string& path = rows[rowIndex].path;
            if(fixupMarked.find(path) == fixupMarked.end())
                fixupMarked.insert(path);
            else
                fixupMarked.erase(path);
            ed->needsFullRedraw = true;
        }
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_F))
    {
        std::vector<std::string> files = collect_staged_paths(rows);
        if(files.empty())
        {
            ed->setStatusMessage("fixup: no staged files");
            return std::nullopt;
        }
        GitFixupMode fixup{{}, repoRoot, repoDir, std::move(files), *this};
        return fixup;
    }

    if(c == keyCode(typed::TypedKey::KEY_P))
    {
        int rowIndex = selectedRowIndex();
        if(rowIndex < 0 || rowIndex >= (int)rows.size())
            return std::nullopt;
        const StatusRow& row = rows[rowIndex];
        if(row.kind != RowKind::File || row.path.empty())
            return std::nullopt;
        if(row.group == FileGroup::Untracked)
        {
            ed->setStatusMessage("git add -p: untracked file");
            return std::nullopt;
        }
        std::vector<std::string> files = {row.path};
        return GitPatchMode{{}, repoRoot, repoDir, "", std::move(files), *this};
    }

    if(c == keyCode(typed::TypedKey::KEY_D))
    {
        diffVisible = !diffVisible;
        diffDirty = true;
        if(diffVisible)
            refreshDiff(*ed);
        ed->needsFullRedraw = true;
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::ENTER))
    {
        int rowIndex = selectedRowIndex();
        if(rowIndex >= 0 && rowIndex < (int)rows.size())
        {
            std::string openPath = rows[rowIndex].path;
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

    if(c == keyCode(control::ControlKey::SPACE))
    {
        int rowIndex = selectedRowIndex();
        if(rowIndex >= 0 && rowIndex < (int)rows.size() && !repoDir.empty())
        {
            const StatusRow& row = rows[rowIndex];
            std::string repoDirEsc = shell_escape_single(repoDir);
            std::string pathEsc = shell_escape_single(row.path);
            std::string cmd;
            if(row.group == FileGroup::Staged)
            {
                cmd = "git -C " + repoDirEsc +
                      " restore --staged -- " + pathEsc + " 2>/dev/null";
            }
            else if(row.group == FileGroup::Unstaged ||
                    row.group == FileGroup::Untracked)
            {
                cmd = "git -C " + repoDirEsc + " add -- " + pathEsc +
                      " 2>/dev/null";
            }

            if(!cmd.empty())
            {
                std::system(cmd.c_str());
                refreshStatus(*ed);
                if(diffVisible)
                    refreshDiff(*ed);
                ed->needsFullRedraw = true;
            }
        }
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_J) ||
       c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        if(cursor < (int)fileRows.size() - 1)
        {
            ++cursor;
            diffDirty = true;
            if(diffVisible)
                refreshDiff(*ed);
            keepCursorVisible(*ed);
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_K) ||
            c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        if(cursor > 0)
        {
            --cursor;
            diffDirty = true;
            if(diffVisible)
                refreshDiff(*ed);
            keepCursorVisible(*ed);
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_J))
    {
        if(diffVisible)
        {
            int maxScroll = std::max(0, (int)diffLines.size() -
                                            git_stage_content_rows(
                                                ed->screenRows,
                                                (int)wrap_help(
                                                    git_stage_help_text(),
                                                    ed->screenCols)
                                                    .size()));
            if(diffOffset < maxScroll)
                ++diffOffset;
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_K))
    {
        if(diffVisible && diffOffset > 0)
            --diffOffset;
    }
    else if(c == keyCode(typed::TypedKey::KEY_H))
    {
        listHorizontalOffset = std::max(0, listHorizontalOffset - 1);
    }
    else if(c == keyCode(typed::TypedKey::KEY_L))
    {
        int listWidth = ed->screenCols;
        if(diffVisible)
        {
            listWidth = std::max(36, ed->screenCols / 2);
            int diffWidth = ed->screenCols - listWidth - 1;
            if(diffWidth < 20)
                listWidth = ed->screenCols;
        }
        listHorizontalOffset =
            std::min(maxListHorizontalOffset(listWidth), listHorizontalOffset + 1);
    }
    else if(c == keyCode(control::ControlKey::CTRL_H))
    {
        if(diffVisible)
            diffHorizontalOffset = std::max(0, diffHorizontalOffset - 1);
    }
    else if(c == keyCode(control::ControlKey::CTRL_L))
    {
        if(diffVisible)
        {
            int listWidth = std::max(36, ed->screenCols / 2);
            int diffWidth = ed->screenCols - listWidth - 1;
            if(diffWidth >= 20)
            {
                int diffViewWidth = std::max(0, diffWidth - 1);
                int maxDiffOffset =
                    std::max(0, max_diff_width(diffLines, ed->gitUseDefaultColors) -
                                    diffViewWidth);
                diffHorizontalOffset =
                    std::min(maxDiffOffset, diffHorizontalOffset + 1);
            }
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_G))
    {
        int nextChar = Terminal::readKey();
        if(nextChar == keyCode(typed::TypedKey::KEY_G))
        {
            cursor = 0;
            diffDirty = true;
            if(diffVisible)
                refreshDiff(*ed);
            keepCursorVisible(*ed);
        }
        else if(nextChar == keyCode(typed::TypedKey::KEY_F))
        {
            std::vector<std::string> files;
            files.reserve(fixupMarked.size());
            for(const auto& path : fixupMarked)
                files.push_back(path);
            if(files.empty())
            {
                ed->setStatusMessage("fixup: no files marked");
                return std::nullopt;
            }
            GitFixupMode fixup{{}, repoRoot, repoDir, std::move(files), *this};
            return fixup;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_CAP_G))
    {
        if(!fileRows.empty())
        {
            cursor = std::max(0, (int)fileRows.size() - 1);
            diffDirty = true;
            if(diffVisible)
                refreshDiff(*ed);
            keepCursorVisible(*ed);
        }
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}

void GitStageMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);
    auto* self = const_cast<GitStageMode*>(this);
    if(self->diffVisible && self->diffDirty && !Terminal::hasBufferedKeys())
        self->refreshDiff(editor);

    auto helpLines = wrap_help(git_stage_help_text(), editor.screenCols);
    if(helpLines.empty())
        helpLines.push_back("");

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::cursorPos(1, 1);
    if(!Terminal::isTmux())
        output += Terminal::ESC_CLEAR_SCREEN;
    output += editor.theme.reset();

    output += Terminal::ESC_CLEAR_LINE;
    output += Terminal::ESC_BOLD;
    std::string header = "  GIT STAGE";
    if(!repoRoot.empty())
        header += " - " + repoRoot;
    output += header;
    output += editor.theme.reset();

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    for(size_t i = 0; i < helpLines.size(); ++i)
    {
        output += helpLines[i];
        if(i + 1 < helpLines.size())
            output += Terminal::NEWLINE_CLEAR;
    }
    output += editor.theme.baseFg();

    int contentRows =
        git_stage_content_rows(editor.screenRows, (int)helpLines.size());
    int listWidth = editor.screenCols;
    int diffWidth = 0;
    if(diffVisible)
    {
        listWidth = std::max(36, editor.screenCols / 2);
        diffWidth = editor.screenCols - listWidth - 1;
        if(diffWidth < 20)
        {
            listWidth = editor.screenCols;
            diffWidth = 0;
        }
    }
    int maxListOffset = maxListHorizontalOffset(listWidth);
    int listOff = std::min(listHorizontalOffset, maxListOffset);
    self->listHorizontalOffset = listOff;
    int diffViewWidth = std::max(0, diffWidth - 1);
    int maxDiffOffset = std::max(
        0, max_diff_width(diffLines, editor.gitUseDefaultColors) - diffViewWidth);
    int hOff = std::min(diffHorizontalOffset, maxDiffOffset);
    self->diffHorizontalOffset = hOff;

    int selected = selectedRowIndex();
    for(int screenRow = 0; screenRow < contentRows; ++screenRow)
    {
        output += Terminal::NEWLINE_CLEAR;
        output += editor.theme.base();
        int rowIndex = offset + screenRow;
        if(rowIndex < 0 || rowIndex >= (int)rows.size())
        {
            output += editor.theme.uiGutter();
            output += "~";
            output += editor.theme.baseFg();
            if(listWidth > 1)
                output.append(listWidth - 1, ' ');
        }
        else
        {
            const StatusRow& row = rows[rowIndex];
            bool isSelected = rowIndex == selected;

            if(row.kind == RowKind::Blank)
            {
                output.append(listWidth, ' ');
            }
            else if(row.kind == RowKind::Header)
            {
                std::string clipped =
                    slice_plain(row.prefix, listOff, listWidth);
                output += editor.theme.baseFg();
                output += clipped;
                int used = text_utils::utf8DisplayWidth(clipped);
                if(used < listWidth)
                {
                    output += editor.theme.base();
                    output.append(listWidth - used, ' ');
                }
            }
            else if(row.kind == RowKind::Hint)
            {
                std::string clipped =
                    slice_plain(row.prefix, listOff, listWidth);
                output += editor.theme.uiDim();
                output += clipped;
                output += editor.theme.baseFg();
                int used = text_utils::utf8DisplayWidth(clipped);
                if(used < listWidth)
                {
                    output += editor.theme.base();
                    output.append(listWidth - used, ' ');
                }
            }
            else if(row.kind == RowKind::Summary)
            {
                std::string clipped =
                    slice_plain(row.prefix, listOff, listWidth);
                output += editor.theme.uiDim();
                output += clipped;
                output += editor.theme.baseFg();
                int used = text_utils::utf8DisplayWidth(clipped);
                if(used < listWidth)
                {
                    output += editor.theme.base();
                    output.append(listWidth - used, ' ');
                }
            }
            else
            {
                std::string prefix = row.prefix;
                if(fixupMarked.find(row.path) != fixupMarked.end() &&
                   prefix.size() >= 8)
                    prefix[7] = '*';

                int prefixWidth = text_utils::utf8DisplayWidth(prefix);
                std::string visiblePrefix =
                    listOff < prefixWidth ? slice_plain(prefix, listOff, listWidth)
                                          : "";
                int used = text_utils::utf8DisplayWidth(visiblePrefix);
                int remaining = std::max(0, listWidth - used);
                int pathOffset = std::max(0, listOff - prefixWidth);
                std::string visiblePath =
                    slice_plain(row.path, pathOffset, remaining);
                int pathWidth = text_utils::utf8DisplayWidth(visiblePath);

                output += editor.theme.baseFg();
                output += visiblePrefix;

                if(!visiblePath.empty())
                {
                    if(isSelected)
                        output += editor.theme.searchMatch();
                    else
                        output += status_path_bg(row.group) +
                                  editor.theme.baseFg();
                    output += visiblePath;
                }
                output += editor.theme.base();

                used += pathWidth;
                if(used < listWidth)
                    output.append(listWidth - used, ' ');
            }
        }

        if(diffWidth > 0)
        {
            output += editor.theme.uiGutter();
            output += ascii::utf8(ascii::BOX_HEAVY_VERTICAL);
            output += editor.theme.base();

            int diffIdx = diffOffset + screenRow;
            int diffViewWidth = std::max(0, diffWidth - 1);
            if(diffIdx >= 0 && diffIdx < (int)diffLines.size())
            {
                output += " ";
                if(editor.gitUseDefaultColors)
                {
                    std::string clipped =
                        slice_with_ansi(diffLines[diffIdx], hOff,
                                              diffViewWidth);
                    output += clipped;
                    output += editor.theme.reset();
                    output += editor.theme.base();
                    int visibleWidth = text_utils::displayWidth(clipped);
                    if(visibleWidth + 1 < diffWidth)
                        output.append(diffWidth - visibleWidth - 1, ' ');
                }
                else
                {
                    std::string clipped =
                        slice_plain(diffLines[diffIdx], hOff, diffViewWidth);
                    append_diff_line(output, editor,
                                     clipped);
                    output += editor.theme.base();
                    int visibleWidth = text_utils::utf8DisplayWidth(clipped);
                    if(visibleWidth + 1 < diffWidth)
                        output.append(diffWidth - visibleWidth - 1, ' ');
                }
            }
            else
            {
                output += editor.theme.uiGutter();
                output += " ~";
                output += editor.theme.base();
                if(diffWidth > 2)
                    output.append(diffWidth - 2, ' ');
            }
        }
    }

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.statusBar();
    std::string status = " GIT STAGE";
    if(!repoRoot.empty())
        status += " | " + repoRoot;
    std::string right =
        " " + std::to_string(fileRows.size()) + " files ";
    output += status;
    int statusWidth = text_utils::utf8DisplayWidth(status);
    int rightWidth = text_utils::utf8DisplayWidth(right);
    int padding = editor.screenCols - statusWidth - rightWidth;
    if(padding > 0)
        output.append(padding, ' ');
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

#ifdef UVIM_TESTING
int GitStageMode::testContentRows(int screenRows, int screenCols)
{
    return git_stage_content_rows(
        screenRows, (int)wrap_help(git_stage_help_text(), screenCols).size());
}
#endif
