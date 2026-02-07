#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <limits.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <unistd.h>

namespace
{
std::string trim_newline(std::string s)
{
    while(!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

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

std::vector<std::string> split_nul(const std::string& s)
{
    std::vector<std::string> out;
    size_t pos = 0;
    while(pos <= s.size())
    {
        size_t next = s.find('\0', pos);
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

std::string git_show_toplevel(const std::string& dir)
{
    std::string cmd =
        "git -C \"" + dir + "\" rev-parse --show-toplevel 2>/dev/null";
    std::string out = run_git_raw(cmd);
    if(out.empty())
        return "";
    auto lines = split_nul(out);
    if(lines.empty())
        return trim_newline(out);
    return trim_newline(lines.front());
}

struct StatusEntry
{
    char indexStatus = ' ';
    char worktreeStatus = ' ';
};

std::string normalize_repo_path(std::string path)
{
    while(path.rfind("./", 0) == 0)
        path.erase(0, 2);
    while(!path.empty() && path.back() == '/')
        path.pop_back();
    std::string out;
    out.reserve(path.size());
    bool prevSlash = false;
    for(char c : path)
    {
        if(c == '/')
        {
            if(prevSlash)
                continue;
            prevSlash = true;
        }
        else
        {
            prevSlash = false;
        }
        out.push_back(c);
    }
    return out;
}

std::unordered_map<std::string, StatusEntry>
parse_status_z(const std::string& raw)
{
    std::unordered_map<std::string, StatusEntry> out;
    size_t i = 0;
    while(i + 2 < raw.size())
    {
        char x = raw[i];
        char y = raw[i + 1];
        i += 2;
        if(i < raw.size() && raw[i] == ' ')
            ++i;

        size_t end = raw.find('\0', i);
        if(end == std::string::npos)
            break;
        std::string path = raw.substr(i, end - i);
        i = end + 1;

        if(x == 'R' || x == 'C' || y == 'R' || y == 'C')
        {
            size_t end2 = raw.find('\0', i);
            if(end2 == std::string::npos)
                break;
            std::string newPath = raw.substr(i, end2 - i);
            i = end2 + 1;
            if(!newPath.empty())
                path = newPath;
        }

        if(path.empty())
            continue;
        path = normalize_repo_path(path);
        if(!path.empty())
            out[path] = StatusEntry{x, y};
    }
    return out;
}

bool is_untracked(const StatusEntry& entry)
{
    return entry.indexStatus == '?' && entry.worktreeStatus == '?';
}

bool has_staged(const StatusEntry& entry)
{
    return entry.indexStatus != ' ' && entry.indexStatus != '?';
}

bool has_unstaged(const StatusEntry& entry)
{
    return entry.worktreeStatus != ' ' && entry.worktreeStatus != '?';
}

std::string status_color(const Theme& theme, const StatusEntry& entry)
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

std::vector<std::string> split_path(const std::string& path)
{
    std::vector<std::string> parts;
    size_t pos = 0;
    while(pos < path.size())
    {
        size_t next = path.find('/', pos);
        if(next == std::string::npos)
            next = path.size();
        if(next > pos)
            parts.push_back(path.substr(pos, next - pos));
        pos = next + 1;
    }
    return parts;
}
} // namespace

GitStageMode::GitStageMode(std::vector<Node> items, std::string root,
                           std::string dir)
    : nodes(std::move(items)), repoRoot(std::move(root)),
      repoDir(std::move(dir))
{
}

void GitStageMode::rebuildVisible()
{
    visible.clear();
    if(nodes.empty())
        return;

    auto walk = [&](auto&& self, int nodeId, int depth) -> void
    {
        const Node& node = nodes[nodeId];
        visible.push_back({nodeId, depth});
        if(!node.isDir || !node.expanded)
            return;
        for(int child : node.children)
        {
            self(self, child, depth + 1);
        }
    };

    for(int child : nodes[0].children)
    {
        walk(walk, child, 0);
    }
}

int GitStageMode::visibleIndexForNode(int nodeId) const
{
    for(size_t i = 0; i < visible.size(); ++i)
    {
        if(visible[i].node == nodeId)
            return (int)i;
    }
    return -1;
}

void GitStageMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    cursor = std::clamp(cursor, 0, (int)visible.size());
    offset = 0;
    diffOffset = 0;
    diffDirty = true;

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
            nodes.clear();
            visible.clear();
            diffLines = {"(not a repo)"};
            diffDirty = false;
            ctx.requestFullRedraw();
            return;
        }

        repoRoot = root;
        repoDir = root;
    }

    if(viewRoot.empty())
    {
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
            viewRoot = cwd;
        else
            viewRoot = repoRoot;
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

    std::error_code ec;
    std::filesystem::path repoPath = std::filesystem::path(repoRoot);
    std::filesystem::path viewPath = std::filesystem::absolute(viewRoot, ec);
    if(ec)
        viewPath = std::filesystem::path(viewRoot);

    std::filesystem::path relView =
        std::filesystem::relative(viewPath, repoPath, ec);
    if(ec)
    {
        editor.setStatusMessage("git stage: outside repo");
        nodes.clear();
        visible.clear();
        diffLines = {"(outside repo)"};
        diffDirty = false;
        return false;
    }

    std::string relViewStr = normalize_repo_path(relView.string());
    if(relViewStr == ".")
        relViewStr.clear();
    if(!relViewStr.empty() && relViewStr.rfind("..", 0) == 0)
    {
        editor.setStatusMessage("git stage: outside repo");
        nodes.clear();
        visible.clear();
        diffLines = {"(outside repo)"};
        diffDirty = false;
        return false;
    }

    std::string statusCmd =
        "git -C \"" + repoDir + "\" status --porcelain -z 2>/dev/null";
    std::string statusRaw = run_git_raw(statusCmd);
    auto statusMap = parse_status_z(statusRaw);

    std::string lsCmd = "git -C \"" + repoDir + "\" ls-files -z";
    if(!relViewStr.empty())
        lsCmd += " -- \"" + relViewStr + "\"";
    lsCmd += " 2>/dev/null";
    std::string lsRaw = run_git_raw(lsCmd);
    auto tracked = split_nul(lsRaw);
    for(auto& t : tracked)
        t = normalize_repo_path(t);

    std::vector<std::string> untracked;
    if(untrackedMode != UntrackedMode::TrackedOnly)
    {
        std::string untrackedCmd =
            "git -C \"" + repoDir +
            "\" ls-files -z --others --exclude-standard";
        if(!relViewStr.empty())
            untrackedCmd += " -- \"" + relViewStr + "\"";
        untrackedCmd += " 2>/dev/null";
        std::string untrackedRaw = run_git_raw(untrackedCmd);
        untracked = split_nul(untrackedRaw);
        for(auto& u : untracked)
            u = normalize_repo_path(u);
    }

    std::unordered_map<std::string, bool> prevExpanded;
    for(size_t i = 0; i < nodes.size(); ++i)
    {
        if(nodes[i].isDir)
        {
            std::string key = nodes[i].repoPath;
            prevExpanded[key] = nodes[i].expanded;
        }
    }

    nodes.clear();
    visible.clear();

    nodes.push_back(Node{"", "", true, true, ' ', ' ', {}}); // root

    std::unordered_map<std::string, int> dirIndex;
    dirIndex[""] = 0;

    std::unordered_set<std::string> seenFiles;
    auto add_file_node = [&](const std::string& repoFileRaw)
    {
        std::string repoFile = normalize_repo_path(repoFileRaw);
        if(repoFile.empty())
            return;
        if(!seenFiles.emplace(repoFile).second)
            return;

        std::string displayPath = repoFile;
        if(!relViewStr.empty())
        {
            if(displayPath.rfind(relViewStr + "/", 0) != 0)
                return;
            displayPath = displayPath.substr(relViewStr.size() + 1);
        }
        displayPath = normalize_repo_path(displayPath);
        if(displayPath.empty())
            return;

        auto parts = split_path(displayPath);
        std::string dirKey;
        int parent = 0;
        for(size_t i = 0; i + 1 < parts.size(); ++i)
        {
            if(!dirKey.empty())
                dirKey += "/";
            dirKey += parts[i];
            auto it = dirIndex.find(dirKey);
            if(it == dirIndex.end())
            {
                Node node;
                node.name = parts[i];
                node.repoPath = relViewStr.empty()
                                    ? dirKey
                                    : relViewStr + "/" + dirKey;
                node.isDir = true;
                node.expanded = false;
                auto prev = prevExpanded.find(node.repoPath);
                if(prev != prevExpanded.end())
                    node.expanded = prev->second;
                int idx = (int)nodes.size();
                nodes.push_back(std::move(node));
                nodes[parent].children.push_back(idx);
                dirIndex[dirKey] = idx;
                parent = idx;
            }
            else
            {
                parent = it->second;
            }
        }

        Node fileNode;
        fileNode.name = parts.back();
        fileNode.repoPath = repoFile;
        fileNode.isDir = false;
        auto statusIt = statusMap.find(repoFile);
        if(statusIt != statusMap.end())
        {
            fileNode.indexStatus = statusIt->second.indexStatus;
            fileNode.worktreeStatus = statusIt->second.worktreeStatus;
        }
        int idx = (int)nodes.size();
        nodes.push_back(std::move(fileNode));
        nodes[parent].children.push_back(idx);
    };

    if(showChangedOnly)
    {
        for(const auto& kv : statusMap)
        {
            bool isUntracked = kv.second.indexStatus == '?' &&
                               kv.second.worktreeStatus == '?';
            if(untrackedMode == UntrackedMode::TrackedOnly && isUntracked)
                continue;
            if(untrackedMode == UntrackedMode::UntrackedOnly && !isUntracked)
                continue;
            add_file_node(kv.first);
        }
    }
    else
    {
        if(untrackedMode != UntrackedMode::UntrackedOnly)
        {
            for(const auto& repoFile : tracked)
                add_file_node(repoFile);
        }
        if(untrackedMode != UntrackedMode::TrackedOnly)
        {
            for(const auto& repoFile : untracked)
                add_file_node(repoFile);
        }
    }

    if(!nodes.empty())
    {
        auto expand_path = [&](const std::string& repoFileRaw)
        {
            std::string repoFile = normalize_repo_path(repoFileRaw);
            if(repoFile.empty())
                return;
            std::string displayPath = repoFile;
            if(!relViewStr.empty())
            {
                if(displayPath.rfind(relViewStr + "/", 0) != 0)
                    return;
                displayPath = displayPath.substr(relViewStr.size() + 1);
            }
            displayPath = normalize_repo_path(displayPath);
            if(displayPath.empty())
                return;
            auto parts = split_path(displayPath);
            std::string dirKey;
            for(size_t i = 0; i + 1 < parts.size(); ++i)
            {
                if(!dirKey.empty())
                    dirKey += "/";
                dirKey += parts[i];
                auto it = dirIndex.find(dirKey);
                if(it != dirIndex.end())
                    nodes[it->second].expanded = true;
            }
        };

        for(const auto& kv : statusMap)
        {
            bool isUntracked = kv.second.indexStatus == '?' &&
                               kv.second.worktreeStatus == '?';
            if(untrackedMode == UntrackedMode::TrackedOnly && isUntracked)
                continue;
            if(untrackedMode == UntrackedMode::UntrackedOnly && !isUntracked)
                continue;
            expand_path(kv.first);
        }
    }

    rebuildVisible();
    cursor = std::clamp(cursor, 0, std::max(0, (int)visible.size() - 1));
    int visibleRows = editor.screenRows - 3;
    offset = std::clamp(offset, 0,
                        std::max(0, (int)visible.size() - visibleRows));
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

    if(visible.empty())
    {
        diffLines.push_back("(clean)");
        diffDirty = false;
        return;
    }

    cursor = std::clamp(cursor, 0, (int)visible.size() - 1);
    const Node& node = nodes[visible[cursor].node];

    if(node.isDir)
    {
        diffLines.push_back("(directory)");
        diffDirty = false;
        return;
    }

    diffPath = node.repoPath;
    StatusEntry status{node.indexStatus, node.worktreeStatus};

    if(is_untracked(status))
    {
        diffLines.push_back("(untracked file)");
        diffDirty = false;
        return;
    }

    bool useUnstaged = has_unstaged(status);
    bool useStaged = !useUnstaged && has_staged(status);
    diffStaged = useStaged;

    std::string cmd = "git -C \"" + repoDir + "\" --no-pager diff ";
    if(useStaged)
        cmd += "--cached ";
    cmd += std::string(editor.gitUseDefaultColors ? "--color=always "
                                                   : "--no-color ");
    cmd += "-- \"" + node.repoPath + "\" 2>/dev/null";

    std::string raw = run_git_raw(cmd);
    diffLines = split_lines(raw);
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

    if(c == 'c')
    {
        showChangedOnly = !showChangedOnly;
        refreshStatus(*ed);
        refreshDiff(*ed);
        ed->needsFullRedraw = true;
        return std::nullopt;
    }

    if(c == 'u')
    {
        if(untrackedMode == UntrackedMode::UntrackedOnly)
            untrackedMode = UntrackedMode::TrackedOnly;
        else
            untrackedMode = UntrackedMode::UntrackedOnly;
        refreshStatus(*ed);
        refreshDiff(*ed);
        ed->needsFullRedraw = true;
        return std::nullopt;
    }

    if(c == 'b')
    {
        if(untrackedMode == UntrackedMode::Both)
            untrackedMode = UntrackedMode::TrackedOnly;
        else
            untrackedMode = UntrackedMode::Both;
        refreshStatus(*ed);
        refreshDiff(*ed);
        ed->needsFullRedraw = true;
        return std::nullopt;
    }

    if(c == 'h' || c == 'l')
    {
        if(cursor >= 0 && cursor < (int)visible.size())
        {
            int nodeId = visible[cursor].node;
            if(nodes[nodeId].isDir)
            {
                if(c == 'h' && nodes[nodeId].expanded)
                {
                    nodes[nodeId].expanded = false;
                    rebuildVisible();
                    int newIndex = visibleIndexForNode(nodeId);
                    if(newIndex >= 0)
                        cursor = newIndex;
                }
                else if(c == 'l' && !nodes[nodeId].expanded)
                {
                    nodes[nodeId].expanded = true;
                    rebuildVisible();
                    int newIndex = visibleIndexForNode(nodeId);
                    if(newIndex >= 0)
                        cursor = newIndex;
                }
            }
            else if(c == 'h')
            {
                int depth = visible[cursor].depth;
                for(int i = cursor - 1; i >= 0; --i)
                {
                    if(visible[i].depth < depth &&
                       nodes[visible[i].node].isDir)
                    {
                        nodes[visible[i].node].expanded = false;
                        rebuildVisible();
                        int newIndex = visibleIndexForNode(visible[i].node);
                        if(newIndex >= 0)
                            cursor = newIndex;
                        break;
                    }
                }
            }
            diffDirty = true;
            ed->needsFullRedraw = true;
        }
        return std::nullopt;
    }

    if(c == Terminal::ENTER)
    {
        if(cursor >= 0 && cursor < (int)visible.size())
        {
            const Node& node = nodes[visible[cursor].node];
            if(!node.isDir)
            {
                std::string openPath = node.repoPath;
                if(!repoRoot.empty() &&
                   !std::filesystem::path(openPath).is_absolute())
                {
                    openPath =
                        (std::filesystem::path(repoRoot) / openPath).string();
                }
                ed->openFile(openPath);
                return NormalMode{};
            }
        }
        return std::nullopt;
    }

    if(c == ' ')
    {
        if(cursor >= 0 && cursor < (int)visible.size() && !repoDir.empty())
        {
            int nodeId = visible[cursor].node;
            Node& node = nodes[nodeId];
            if(node.isDir)
            {
                node.expanded = !node.expanded;
                rebuildVisible();
                int newIndex = visibleIndexForNode(nodeId);
                if(newIndex >= 0)
                    cursor = newIndex;
            }
            else
            {
                StatusEntry status{node.indexStatus, node.worktreeStatus};
                std::string cmd;
                if(has_unstaged(status) || is_untracked(status))
                {
                    cmd = "git -C \"" + repoDir + "\" add -- \"" +
                          node.repoPath + "\" 2>/dev/null";
                }
                else if(has_staged(status))
                {
                    cmd = "git -C \"" + repoDir +
                          "\" restore --staged -- \"" + node.repoPath +
                          "\" 2>/dev/null";
                }
                if(!cmd.empty())
                {
                    std::system(cmd.c_str());
                    refreshStatus(*ed);
                    int newIndex = visibleIndexForNode(nodeId);
                    if(newIndex >= 0)
                        cursor = newIndex;
                }
            }
            diffDirty = true;
            ed->needsFullRedraw = true;
        }
        return std::nullopt;
    }

    if(c == 'j' || c == Terminal::ARROW_DOWN)
    {
        if(cursor < (int)visible.size() - 1)
        {
            cursor++;
            int visibleRows = ed->screenRows - 3;
            if(cursor >= offset + visibleRows)
                offset = cursor - visibleRows + 1;
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
        if(!visible.empty())
        {
            cursor = std::max(0, (int)visible.size() - 1);
            int visibleRows = ed->screenRows - 3;
            offset = std::max(0, cursor - visibleRows + 1);
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

    output += Terminal::ESC_CLEAR_LINE;
    output += Terminal::ESC_BOLD;
    std::string header = "  GIT STAGE";
    if(!viewRoot.empty())
        header += " - " + viewRoot;
    output += header;
    output += editor.theme.reset();
    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    output += "  [space: toggle/stage] [u: untracked] [b: both] [c: changed] "
              "[h/l: fold] [enter: open] [r: refresh] [q/esc: close]";
    output += editor.theme.baseFg();

    int contentRows = editor.screenRows - 4;
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
        if(idx >= 0 && idx < (int)visible.size())
        {
            const VisibleEntry& vis = visible[idx];
            const Node& node = nodes[vis.node];
            bool isSelected = (idx == cursor);
            if(isSelected)
                output += editor.theme.selection();

            std::string status = "  ";
            StatusEntry entry{node.indexStatus, node.worktreeStatus};
            if(!node.isDir)
            {
                status[0] = entry.indexStatus;
                status[1] = entry.worktreeStatus;
            }

            std::string name = node.name;
            if(node.isDir && name != "..")
                name += "/";

            std::string indent;
            for(int i = 0; i < vis.depth; ++i)
                indent += "  ";
            std::string marker = node.isDir ? (node.expanded ? "▾ " : "▸ ")
                                            : "  ";
            std::string icon = node.isDir ? "📁 " : "📄 ";

            std::string path = indent + marker + icon + name;
            int maxPathLen = std::max(0, listWidth - 4);
            if((int)path.size() > maxPathLen)
            {
                if(maxPathLen > 3)
                    path = "..." + path.substr(path.size() - maxPathLen + 3);
                else
                    path = path.substr(0, maxPathLen);
            }

            output += " ";
            if(node.isDir)
            {
                output += editor.theme.uiDirectory();
            }
            else
            {
                if(entry.indexStatus != ' ' || entry.worktreeStatus != ' ')
                    output += status_color(editor.theme, entry);
                else
                    output += editor.theme.baseFg();
            }
            output += status;
            if(node.isDir)
            {
                output += editor.theme.uiDirectory();
                output += Terminal::ESC_BOLD;
            }
            else
            {
                if(entry.indexStatus != ' ' || entry.worktreeStatus != ' ')
                    output += status_color(editor.theme, entry);
                else
                    output += editor.theme.baseFg();
            }
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

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.statusBar();
    std::string status = " GIT STAGE";
    if(!viewRoot.empty())
        status += " | " + viewRoot;
    std::string right =
        " " + std::to_string(visible.empty() ? 0 : cursor + 1) + "/" +
        std::to_string(visible.size()) + " ";
    output += status;
    int padding = editor.screenCols - status.length() - right.length();
    if(padding > 0)
        output.append(padding, ' ');
    output += right;
    output += editor.theme.reset();

    output += Terminal::NEWLINE_CLEAR;
    if(!editor.statusMessage.empty())
        output += editor.statusMessage;

    Terminal::write(output);
    Terminal::flush();
}
