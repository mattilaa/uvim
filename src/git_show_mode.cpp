#include "editor.h"
#include "git_commit_rewrite.h"
#include "header_help.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <chrono>
#include <string_view>

// ============================================================================
// GitShowCommitMode Implementation
// ============================================================================

namespace editor::statemachine
{
namespace
{
// View kinds: 0 plain diff, 1 :showchanges, 2 commit from git log.
std::vector<std::string> gitShowHelpTokens(int kind)
{
    std::vector<std::string> tokens =
        {"[q: quit]",
         "[j/k: scroll]",
         "[gg/G: top/bottom]",
         "[n/N ]/[: hunks]",
         "[ctrl-h/l: pan]"};
    if(kind == 1)
    {
        tokens.push_back("[d: drop hunk]");
        tokens.push_back("[u: undo drop]");
    }
    else if(kind == 2)
    {
        tokens.push_back("[d: drop hunk]");
        tokens.push_back("[u: undo]");
        tokens.push_back("[v: new/dropped]");
        tokens.push_back("[c: rewrite commit]");
    }
    return tokens;
}

int gitShowContentRows(int screenRows, int screenCols, int kind)
{
    // screenRows already excludes the status and message rows.
    const int headerRows =
        1 + HeaderHelp::lineCount(gitShowHelpTokens(kind), screenCols);
    return std::max(1, screenRows - headerRows);
}

std::string stripAnsi(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for(size_t i = 0; i < text.size(); ++i)
    {
        if(text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[')
        {
            size_t end = i + 2;
            while(end < text.size() &&
                  !((text[end] >= 'A' && text[end] <= 'Z') ||
                    (text[end] >= 'a' && text[end] <= 'z')))
                ++end;
            i = end;
            continue;
        }
        out += text[i];
    }
    return out;
}

// One "@@ -a,b +c,d @@" hunk of a unified diff: its header line index and
// the end (exclusive) of its body in the view's lines.
struct DiffHunk
{
    int header = 0;
    int end = 0;
    int oldStart = 0;
    int oldCount = 1;
    int newStart = 0;
    int newCount = 1;
};

bool parseRange(std::string_view text, size_t& pos, int& start, int& count)
{
    auto number = [&](int& out)
    {
        const size_t begin = pos;
        int value = 0;
        while(pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
            value = value * 10 + (text[pos++] - '0');
        out = value;
        return pos > begin;
    };
    if(!number(start))
        return false;
    count = 1;
    if(pos < text.size() && text[pos] == ',')
    {
        ++pos;
        if(!number(count))
            return false;
    }
    return true;
}

std::vector<DiffHunk> parseHunks(const std::vector<std::string>& lines)
{
    std::vector<DiffHunk> hunks;
    for(int i = 0; i < (int)lines.size(); ++i)
    {
        const std::string text = stripAnsi(lines[i]);
        if(text.rfind("@@ -", 0) == 0)
        {
            DiffHunk hunk;
            hunk.header = i;
            size_t pos = 4;
            if(!parseRange(text, pos, hunk.oldStart, hunk.oldCount) ||
               text.compare(pos, 2, " +") != 0)
                continue;
            pos += 2;
            if(!parseRange(text, pos, hunk.newStart, hunk.newCount))
                continue;
            if(!hunks.empty() && hunks.back().end > i)
                hunks.back().end = i;
            hunk.end = (int)lines.size();
            hunks.push_back(hunk);
        }
        else if(
            text.rfind("diff --git", 0) == 0 && !hunks.empty() &&
            hunks.back().end > i)
        {
            hunks.back().end = i;
        }
    }
    return hunks;
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

int display_width_plain(std::string_view text)
{
    return text_utils::utf8DisplayWidth(text);
}

int display_width_with_ansi(std::string_view text)
{
    return text_utils::displayWidth(text);
}

int max_line_width(const std::vector<std::string>& lines, bool useDefaultColors)
{
    int maxW = 0;
    for(const auto& line : lines)
    {
        int w = useDefaultColors ? display_width_with_ansi(line)
                                 : display_width_plain(line);
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

void append_highlighted(
    std::string& out,
    std::string_view text,
    std::string_view query,
    const std::string& normalSeq,
    const std::string& matchSeq)
{
    if(query.empty())
    {
        out += normalSeq;
        out.append(text.data(), text.size());
        return;
    }

    size_t pos = 0;
    while(pos < text.size())
    {
        size_t found = text.find(query, pos);
        if(text_utils::is_not_found(found))
        {
            out += normalSeq;
            out.append(text.data() + pos, text.size() - pos);
            break;
        }
        if(found > pos)
        {
            out += normalSeq;
            out.append(text.data() + pos, found - pos);
        }
        out += matchSeq;
        out.append(text.data() + found, query.size());
        pos = found + query.size();
    }
}
} // namespace

void GitShowCommitMode::on_enter(ModeContext& ctx)
{
    searchIndex = std::clamp(searchIndex, 0, (int)lines.size());
    ctx.requestFullRedraw();
}

void GitShowCommitMode::on_exit(ModeContext& ctx)
{
    ctx.requestFullRedraw();
}

std::optional<ModeState> GitShowCommitMode::handle(
    ModeContext& ctx,
    const ModeKeyEvent& event)
{
    const int key = event.key;
    int c = keyCode(key);

    auto findNextMatch = [&](bool forward)
    {
        if(searchQuery.empty() || lines.empty())
            return false;
        int found = -1;
        if(forward)
        {
            for(int i = searchIndex + 1; i < (int)lines.size(); ++i)
            {
                if(text_utils::contains(lines[i], searchQuery))
                {
                    found = i;
                    break;
                }
            }
            if(found < 0)
            {
                for(int i = 0; i <= searchIndex; ++i)
                {
                    if(text_utils::contains(lines[i], searchQuery))
                    {
                        found = i;
                        break;
                    }
                }
            }
        }
        else
        {
            for(int i = searchIndex - 1; i >= 0; --i)
            {
                if(text_utils::contains(lines[i], searchQuery))
                {
                    found = i;
                    break;
                }
            }
            if(found < 0)
            {
                for(int i = (int)lines.size() - 1; i >= searchIndex; --i)
                {
                    if(text_utils::contains(lines[i], searchQuery))
                    {
                        found = i;
                        break;
                    }
                }
            }
        }
        if(found >= 0)
        {
            searchIndex = found;
            int maxScroll = std::max(
                0,
                (int)lines.size() - gitShowContentRows(
                                        ctx.screenRows(),
                                        ctx.screenCols(),
                                        viewKind()));
            scrollOffset = std::clamp(found, 0, maxScroll);
            return true;
        }
        return false;
    };

    if(searchActive)
    {
        if(c == keyCode(control::ControlKey::ESC))
        {
            auto now = std::chrono::steady_clock::now();
            auto timeSinceLastEsc =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - ctx.lastEscTime())
                    .count();
            if(timeSinceLastEsc <= Editor::DOUBLE_ESC_TIMEOUT_MS)
            {
                searchIndex = searchPrevIndex;
                scrollOffset = searchPrevScroll;
                searchQuery.clear();
                ctx.editor->setStatusMessage("");
                ctx.lastEscTime() = std::chrono::steady_clock::time_point();
            }
            else
            {
                ctx.lastEscTime() = now;
            }
            searchActive = false;
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ENTER))
        {
            if(searchQuery.empty())
            {
                searchIndex = searchPrevIndex;
                scrollOffset = searchPrevScroll;
                searchActive = false;
                ctx.requestFullRedraw();
                return std::nullopt;
            }
            if(!lines.empty())
            {
                int found = -1;
                if(searchForward)
                {
                    for(int i = searchIndex + 1; i < (int)lines.size(); ++i)
                    {
                        if(text_utils::contains(lines[i], searchQuery))
                        {
                            found = i;
                            break;
                        }
                    }
                }
                else
                {
                    for(int i = searchIndex - 1; i >= 0; --i)
                    {
                        if(text_utils::contains(lines[i], searchQuery))
                        {
                            found = i;
                            break;
                        }
                    }
                }
                if(found >= 0)
                {
                    searchIndex = found;
                    int maxScroll = std::max(
                        0,
                        (int)lines.size() - gitShowContentRows(
                                                ctx.screenRows(),
                                                ctx.screenCols(),
                                                viewKind()));
                    scrollOffset = std::clamp(found, 0, maxScroll);
                }
                else
                {
                    ctx.setStatusMessage("search: not found");
                }
            }
            searchActive = false;
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 ||
           c == keyCode(control::ControlKey::CTRL_H))
        {
            if(!searchQuery.empty())
                searchQuery.pop_back();
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::CTRL_U))
        {
            searchQuery.clear();
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c >= 32 && c < 127)
        {
            searchQuery += static_cast<char>(c);
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        return std::nullopt;
    }

    if(confirmRewrite)
    {
        confirmRewrite = false;
        if(c == keyCode(typed::TypedKey::KEY_Y))
            performRewrite(ctx);
        else
            ctx.setStatusMessage("Rewrite cancelled");
        ctx.requestFullRedraw();
        return std::nullopt;
    }
    if(confirmQuit && c != keyCode(typed::TypedKey::KEY_Q) &&
       c != keyCode(control::ControlKey::ESC))
        confirmQuit = false;

    if(c == keyCode(control::ControlKey::ESC) && !searchQuery.empty())
    {
        // First Esc clears the search, so n/N move between hunks again.
        searchQuery.clear();
        ctx.setStatusMessage("");
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(typed::TypedKey::KEY_Q))
    {
        if(!droppedOriginal.empty() && !confirmQuit)
        {
            // Pending drops would be lost: ask once.
            confirmQuit = true;
            ctx.setStatusMessage(
                "Discard " + std::to_string(droppedOriginal.size()) +
                " pending dropped hunk(s)? q again to quit, "
                "c to rewrite the commit");
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ESC))
            ctx.editor->noteDoubleEscStatusClear();
        if(returnLog.has_value())
        {
            GitLogMode log = *returnLog;
            if(historyChanged)
                ctx.editor->reloadGitLog(log); // the hashes changed
            return log;
        }
        return NormalMode{};
    }

    int maxScroll = std::max(
        0,
        (int)lines.size() -
            gitShowContentRows(ctx.screenRows(), ctx.screenCols(), viewKind()));

    if(c == keyCode(typed::TypedKey::KEY_J) ||
       c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        if(scrollOffset < maxScroll)
            scrollOffset++;
    }
    else if(
        c == keyCode(typed::TypedKey::KEY_K) ||
        c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        if(scrollOffset > 0)
            scrollOffset--;
    }
    else if(c == keyCode(control::ControlKey::CTRL_D))
    {
        int half =
            gitShowContentRows(ctx.screenRows(), ctx.screenCols(), viewKind()) /
            2;
        scrollOffset = std::min(scrollOffset + half, maxScroll);
    }
    else if(c == keyCode(control::ControlKey::CTRL_U))
    {
        int half =
            gitShowContentRows(ctx.screenRows(), ctx.screenCols(), viewKind()) /
            2;
        scrollOffset = std::max(scrollOffset - half, 0);
    }
    else if(c == keyCode(navigation::NavigationKey::PAGE_DOWN))
    {
        scrollOffset = std::min(
            scrollOffset + gitShowContentRows(
                               ctx.screenRows(),
                               ctx.screenCols(),
                               viewKind()),
            maxScroll);
    }
    else if(c == keyCode(navigation::NavigationKey::PAGE_UP))
    {
        scrollOffset = std::max(
            scrollOffset - gitShowContentRows(
                               ctx.screenRows(),
                               ctx.screenCols(),
                               viewKind()),
            0);
    }
    else if(c == keyCode(typed::TypedKey::KEY_CAP_G))
    {
        scrollOffset = maxScroll;
    }
    else if(c == keyCode(typed::TypedKey::KEY_G))
    {
        int nextChar = Terminal::readKey();
        if(nextChar == keyCode(typed::TypedKey::KEY_G))
            scrollOffset = 0;
    }
    else if(
        c == keyCode(command::CommandKey::KEY_SLASH) ||
        c == keyCode(command::CommandKey::KEY_QUESTION))
    {
        searchActive = true;
        searchForward = (c == keyCode(command::CommandKey::KEY_SLASH));
        searchQuery.clear();
        searchPrevIndex = searchIndex;
        searchPrevScroll = scrollOffset;
        ctx.lastEscTime() = std::chrono::steady_clock::time_point();
    }
    else if(
        c == keyCode(control::ControlKey::CTRL_H) ||
        c == keyCode(control::ControlKey::CTRL_L))
    {
        int viewWidth = std::max(0, ctx.screenCols() - 2);
        int maxW = max_line_width(lines, ctx.editor->gitUseDefaultColors);
        int maxOffset = std::max(0, maxW - viewWidth);
        if(c == keyCode(control::ControlKey::CTRL_H))
            horizontalOffset = std::max(0, horizontalOffset - 1);
        else
            horizontalOffset = std::min(maxOffset, horizontalOffset + 1);
    }
    else if(
        (c == keyCode(typed::TypedKey::KEY_N) ||
         c == keyCode(typed::TypedKey::KEY_CAP_N)) &&
        !searchQuery.empty())
    {
        bool forward = (c == keyCode(typed::TypedKey::KEY_N)) ? searchForward
                                                              : !searchForward;
        if(!findNextMatch(forward))
            ctx.setStatusMessage("search: not found");
    }
    else if(
        c == keyCode(typed::TypedKey::KEY_N) ||
        c == keyCode(typed::TypedKey::KEY_CAP_N) ||
        c == keyCode(command::CommandKey::KEY_RIGHT_BRACKET) ||
        c == keyCode(command::CommandKey::KEY_LEFT_BRACKET))
    {
        // Hunk navigation (n/N while no search is active, ]/[ always).
        const bool forward =
            c == keyCode(typed::TypedKey::KEY_N) ||
            c == keyCode(command::CommandKey::KEY_RIGHT_BRACKET);
        jumpToHunk(ctx, forward);
    }
    else if(
        c == keyCode(typed::TypedKey::KEY_D) ||
        c == keyCode(typed::TypedKey::KEY_U) ||
        c == keyCode(typed::TypedKey::KEY_V) ||
        c == keyCode(typed::TypedKey::KEY_C))
    {
        const bool drop = c == keyCode(typed::TypedKey::KEY_D);
        const bool undo = c == keyCode(typed::TypedKey::KEY_U);
        if(unsavedChanges && (drop || undo))
        {
            if(drop)
                dropCurrentHunk(ctx);
            else
                undoDrop(ctx);
        }
        else if(commitEditable())
        {
            if(drop)
                dropCommitHunk(ctx);
            else if(undo)
                undoCommitEdit(ctx);
            else if(c == keyCode(typed::TypedKey::KEY_V))
                toggleDroppedView(ctx);
            else
                startRewrite(ctx);
        }
        else if(drop || undo)
        {
            ctx.setStatusMessage(
                "d/u work in :showchanges and on commits "
                "opened from :git log");
        }
    }

    ctx.requestFullRedraw();
    return std::nullopt;
}

int GitShowCommitMode::viewKind() const
{
    if(unsavedChanges)
        return 1;
    return commitEditable() ? 2 : 0;
}

bool GitShowCommitMode::commitEditable() const
{
    return !repoRoot.empty() && !unsavedChanges && !commitHash.empty();
}

namespace
{
// [start, end) of the file section ("diff --git" ... ) holding `line`.
std::pair<int, int> fileSection(const std::vector<std::string>& lines, int line)
{
    int start = line;
    while(start > 0 && stripAnsi(lines[start]).rfind("diff --git", 0) != 0)
        --start;
    int end = line + 1;
    while(end < (int)lines.size() &&
          stripAnsi(lines[end]).rfind("diff --git", 0) != 0)
        ++end;
    return {start, end};
}

// New/deleted/renamed files can only be dropped as a whole.
bool isWholeFileSection(
    const std::vector<std::string>& lines,
    int start,
    int firstHunk)
{
    for(int i = start; i < firstHunk && i < (int)lines.size(); ++i)
    {
        const std::string text = stripAnsi(lines[i]);
        if(text.rfind("new file mode", 0) == 0 ||
           text.rfind("deleted file mode", 0) == 0 ||
           text.rfind("rename from", 0) == 0 || text.rfind("copy from", 0) == 0)
            return true;
    }
    return false;
}

// Plain-diff lines with git-like colors, for the "dropped changes" view.
std::vector<std::string> colorPlainDiff(
    const std::vector<std::string>& in,
    bool ansi)
{
    if(!ansi)
        return in;
    std::vector<std::string> out;
    out.reserve(in.size());
    for(const auto& line : in)
    {
        const char* color = nullptr;
        if(line.rfind("diff --git", 0) == 0 || line.rfind("--- ", 0) == 0 ||
           line.rfind("+++ ", 0) == 0)
            color = "\x1b[1m";
        else if(line.rfind("@@", 0) == 0)
            color = "\x1b[36m";
        else if(!line.empty() && line[0] == '+')
            color = "\x1b[32m";
        else if(!line.empty() && line[0] == '-')
            color = "\x1b[31m";
        out.push_back(color ? std::string(color) + line + "\x1b[m" : line);
    }
    return out;
}
} // namespace

void GitShowCommitMode::dropCommitHunk(ModeContext& ctx)
{
    if(showDropped)
    {
        ctx.setStatusMessage(
            "Showing dropped changes; v to go back to the "
            "new commit");
        return;
    }
    const auto hunks = parseHunks(lines);
    if(hunks.empty())
    {
        ctx.setStatusMessage("No hunks left in this commit");
        return;
    }
    if(originalPlain.empty())
    {
        // First drop: remember the commit's plain diff (the patch source)
        // and map each shown hunk to it.
        originalPlain = git_rewrite::plainCommitDiff(repoRoot, commitHash);
        if(parseHunks(originalPlain).size() != hunks.size())
        {
            originalPlain.clear();
            ctx.setStatusMessage("Cannot map this commit's hunks for editing");
            return;
        }
        hunkMap.clear();
        for(int i = 0; i < (int)hunks.size(); ++i)
            hunkMap.push_back(i);
    }

    const int index = hunkAtView();
    if(index < 0)
        return;
    pendingUndo.push_back({lines, hunkMap, droppedOriginal});

    const DiffHunk& hunk = hunks[index];
    const auto [secStart, secEnd] = fileSection(lines, hunk.header);
    std::vector<int> inSection;
    for(int i = 0; i < (int)hunks.size(); ++i)
    {
        if(hunks[i].header > secStart && hunks[i].header < secEnd)
            inSection.push_back(i);
    }
    const bool wholeFile =
        inSection.size() <= 1 ||
        isWholeFileSection(lines, secStart, hunks[inSection.front()].header);

    std::vector<int> removed = wholeFile ? inSection : std::vector<int>{index};
    for(int k : removed)
        droppedOriginal.push_back(hunkMap[k]);
    for(auto it = removed.rbegin(); it != removed.rend(); ++it)
        hunkMap.erase(hunkMap.begin() + *it);
    if(wholeFile)
        lines.erase(lines.begin() + secStart, lines.begin() + secEnd);
    else
        lines.erase(lines.begin() + hunk.header, lines.begin() + hunk.end);

    const int left = (int)parseHunks(lines).size();
    currentHunk = left == 0 ? -1 : std::min(index, left - 1);
    if(currentHunk >= 0)
        scrollToHunk(ctx, currentHunk);
    else
        scrollOffset = 0;
    ctx.setStatusMessage(
        std::string(wholeFile ? "Dropped file section" : "Dropped hunk") +
        " (pending: " + std::to_string(droppedOriginal.size()) +
        "). c: rewrite commit, v: view dropped, u: undo");
}

void GitShowCommitMode::undoCommitEdit(ModeContext& ctx)
{
    if(showDropped)
        toggleDroppedView(ctx);
    if(!pendingUndo.empty())
    {
        PendingEdit last = std::move(pendingUndo.back());
        pendingUndo.pop_back();
        lines = std::move(last.lines);
        hunkMap = std::move(last.hunkMap);
        droppedOriginal = std::move(last.dropped);
        currentHunk = -1;
        ctx.setStatusMessage(
            "Restored hunk (pending: " +
            std::to_string(droppedOriginal.size()) + ")");
        return;
    }
    if(!rewriteNewHead.empty())
    {
        std::string message;
        if(git_rewrite::undoDrop(
               repoRoot,
               rewriteOriginalHead,
               rewriteNewHead,
               message))
        {
            commitHash = rewrittenFrom;
            auto restored = ctx.editor->loadGitShowLines(commitHash);
            if(!restored.empty())
                lines = std::move(restored);
            rewriteNewHead.clear();
            rewriteOriginalHead.clear();
            rewrittenFrom.clear();
            originalPlain.clear();
            hunkMap.clear();
            currentHunk = -1;
            scrollOffset = 0;
        }
        ctx.setStatusMessage(message);
        return;
    }
    ctx.setStatusMessage("Nothing to undo");
}

std::string GitShowCommitMode::droppedPatch() const
{
    // The dropped hunks as a patch against the original commit: per file
    // section its header and the dropped hunks (whole section when all of
    // its hunks, or a new/deleted file, are dropped).
    const auto hunks = parseHunks(originalPlain);
    std::string patch;
    int sectionStart = -1;
    for(int i = 0; i < (int)hunks.size(); ++i)
    {
        const auto [start, end] = fileSection(originalPlain, hunks[i].header);
        if(start == sectionStart)
            continue;
        sectionStart = start;
        std::vector<int> inSection;
        for(int j = i; j < (int)hunks.size() && hunks[j].header < end; ++j)
            inSection.push_back(j);
        std::vector<int> dropped;
        for(int j : inSection)
        {
            if(std::find(droppedOriginal.begin(), droppedOriginal.end(), j) !=
               droppedOriginal.end())
                dropped.push_back(j);
        }
        if(dropped.empty())
            continue;
        const int firstHunk = hunks[inSection.front()].header;
        for(int line = start; line < firstHunk; ++line)
            patch += originalPlain[line] + "\n";
        for(int j : dropped)
        {
            for(int line = hunks[j].header; line < hunks[j].end; ++line)
                patch += originalPlain[line] + "\n";
        }
    }
    return patch;
}

void GitShowCommitMode::toggleDroppedView(ModeContext& ctx)
{
    if(showDropped)
    {
        lines = std::move(newCommitLines);
        newCommitLines.clear();
        showDropped = false;
        currentHunk = -1;
        scrollOffset = 0;
        ctx.setStatusMessage("New commit diff");
        return;
    }
    if(droppedOriginal.empty())
    {
        ctx.setStatusMessage("No dropped hunks yet (d drops one)");
        return;
    }
    std::vector<std::string> patchLines;
    const std::string patch = droppedPatch();
    size_t pos = 0;
    while(pos < patch.size())
    {
        size_t next = patch.find('\n', pos);
        if(next == std::string::npos)
            next = patch.size();
        patchLines.push_back(patch.substr(pos, next - pos));
        pos = next + 1;
    }
    newCommitLines = std::move(lines);
    lines = colorPlainDiff(patchLines, ctx.editor->gitUseDefaultColors);
    showDropped = true;
    currentHunk = -1;
    scrollOffset = 0;
    ctx.setStatusMessage(
        "Dropped changes (they stay in the working tree); "
        "v: back to the new commit");
}

void GitShowCommitMode::startRewrite(ModeContext& ctx)
{
    if(droppedOriginal.empty())
    {
        ctx.setStatusMessage("Nothing to rewrite: drop hunks with d first");
        return;
    }
    if(std::string reason = git_rewrite::checkRewritable(repoRoot, commitHash);
       !reason.empty())
    {
        ctx.setStatusMessage("Cannot rewrite: " + reason);
        return;
    }
    confirmRewrite = true;
    ctx.setStatusMessage(
        "Rewrite " + commitHash.substr(0, 8) + " without " +
        std::to_string(droppedOriginal.size()) +
        " hunk(s)? Dropped changes stay unstaged in the "
        "working tree. y/n");
}

void GitShowCommitMode::performRewrite(ModeContext& ctx)
{
    if(showDropped)
    {
        lines = std::move(newCommitLines);
        newCommitLines.clear();
        showDropped = false;
    }
    const git_rewrite::Result result =
        git_rewrite::dropFromCommit(repoRoot, commitHash, droppedPatch());
    if(result.ok)
    {
        // Files changed on disk; reload open buffers now so the editor's
        // own "reloaded" notice does not replace the rewrite summary.
        ctx.editor->checkFileChanges();
    }
    ctx.setStatusMessage(result.message);
    if(!result.ok)
        return;

    rewrittenFrom = commitHash;
    rewriteOriginalHead = result.originalHead;
    rewriteNewHead = result.newHead;
    commitHash = result.newHash;
    historyChanged = true;
    auto fresh = ctx.editor->loadGitShowLines(commitHash);
    if(!fresh.empty())
        lines = std::move(fresh);
    originalPlain.clear();
    hunkMap.clear();
    droppedOriginal.clear();
    pendingUndo.clear();
    currentHunk = -1;
    scrollOffset = 0;
}

int GitShowCommitMode::hunkAtView() const
{
    // The selected hunk, or else the first one starting in/after the view.
    const auto hunks = parseHunks(lines);
    if(hunks.empty())
        return -1;
    if(currentHunk >= 0 && currentHunk < (int)hunks.size())
        return currentHunk;
    for(int i = 0; i < (int)hunks.size(); ++i)
    {
        if(hunks[i].end > scrollOffset)
            return i;
    }
    return (int)hunks.size() - 1;
}

void GitShowCommitMode::scrollToHunk(ModeContext& ctx, int index)
{
    const auto hunks = parseHunks(lines);
    if(index < 0 || index >= (int)hunks.size())
        return;
    currentHunk = index;
    const int rows =
        gitShowContentRows(ctx.screenRows(), ctx.screenCols(), viewKind());
    const int maxScroll = std::max(0, (int)lines.size() - rows);
    // Keep one line of context (the file header) above the hunk.
    scrollOffset = std::clamp(hunks[index].header - 1, 0, maxScroll);
}

void GitShowCommitMode::jumpToHunk(ModeContext& ctx, bool forward)
{
    const auto hunks = parseHunks(lines);
    if(hunks.empty())
    {
        ctx.setStatusMessage("No hunks");
        return;
    }
    int target = -1;
    if(currentHunk < 0 || currentHunk >= (int)hunks.size())
    {
        // Nothing selected yet: start from what is on screen.
        for(int i = 0; i < (int)hunks.size() && target < 0; ++i)
        {
            if(hunks[i].header >= scrollOffset)
                target = i;
        }
        if(target < 0)
            target = (int)hunks.size() - 1;
        if(!forward && hunks[target].header > scrollOffset && target > 0)
            --target;
    }
    std::string wrapped;
    if(currentHunk >= 0 && currentHunk < (int)hunks.size())
    {
        // Past either end, wrap around and say so.
        const int count = static_cast<int>(hunks.size());
        target = currentHunk + (forward ? 1 : -1);
        if(target >= count)
        {
            target = 0;
            wrapped = " (last hunk reached, wrapped to first)";
        }
        else if(target < 0)
        {
            target = count - 1;
            wrapped = " (first hunk reached, wrapped to last)";
        }
    }
    scrollToHunk(ctx, target);
    ctx.setStatusMessage(
        "Hunk " + std::to_string(target + 1) + "/" +
        std::to_string(hunks.size()) + wrapped);
}

void GitShowCommitMode::reloadChanges(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    std::string title;
    std::string message;
    std::vector<std::string> diff = ed->unsavedChangesDiff(title, message);
    if(diff.empty())
    {
        lines =
            {"No unsaved changes left.",
             "",
             "u: undo the last drop    q: back to the buffer"};
        currentHunk = -1;
        scrollOffset = 0;
        return;
    }
    lines = std::move(diff);
    commitHash = title;
    const int count = (int)parseHunks(lines).size();
    if(count == 0)
        currentHunk = -1;
    else
        scrollToHunk(ctx, std::clamp(currentHunk, 0, count - 1));
}

void GitShowCommitMode::dropCurrentHunk(ModeContext& ctx)
{
    const auto hunks = parseHunks(lines);
    const int index = hunkAtView();
    if(index < 0)
    {
        ctx.setStatusMessage("No hunk to drop");
        return;
    }
    const DiffHunk& hunk = hunks[index];
    Editor* ed = ctx.editor;
    if(!ed || !ed->lines)
        return;

    // Put the saved text back: context and removed ("-") lines of the hunk.
    std::vector<std::string> savedSide;
    for(int i = hunk.header + 1; i < hunk.end; ++i)
    {
        const std::string text = stripAnsi(lines[i]);
        if(text.empty())
        {
            savedSide.emplace_back();
            continue;
        }
        if(text[0] == ' ' || text[0] == '-')
            savedSide.push_back(text.substr(1));
    }

    std::vector<std::string>& buffer = *ed->lines;
    // An empty new range starts *after* line newStart.
    const int start = hunk.newCount == 0 ? hunk.newStart : hunk.newStart - 1;
    if(start < 0 || start + hunk.newCount > (int)buffer.size())
    {
        ctx.setStatusMessage("Cannot drop hunk: buffer changed");
        return;
    }
    buffer
        .erase(buffer.begin() + start, buffer.begin() + start + hunk.newCount);
    buffer.insert(buffer.begin() + start, savedSide.begin(), savedSide.end());
    if(buffer.empty())
        buffer.emplace_back();
    *ed->cursorY = std::clamp(start, 0, (int)buffer.size() - 1);
    *ed->cursorX = 0;
    ed->saveState(); // one undo step, also undoable later with u in the buffer
    ++droppedHunks;

    currentHunk = index;
    reloadChanges(ctx);
    ctx.setStatusMessage(
        "Dropped hunk " + std::to_string(index + 1) + " (u to undo)");
}

void GitShowCommitMode::undoDrop(ModeContext& ctx)
{
    if(droppedHunks == 0)
    {
        ctx.setStatusMessage("No dropped hunk to undo");
        return;
    }
    ctx.editor->undo();
    --droppedHunks;
    reloadChanges(ctx);
    ctx.setStatusMessage("Restored hunk");
}

void GitShowCommitMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::ESC_CURSOR_HOME;
    output += editor.theme.reset();

    output += Terminal::ESC_CLEAR_LINE;
    output += editor.theme.uiAccent();
    output += Terminal::ESC_BOLD;
    output += "  GITSHOW";
    if(!commitHash.empty())
        output += " " + commitHash;
    output += editor.theme.reset();
    HeaderHelp::append(
        output,
        editor.theme,
        editor.screenCols,
        gitShowHelpTokens(viewKind()));

    int availableRows =
        gitShowContentRows(editor.screenRows, editor.screenCols, viewKind());
    int viewWidth = std::max(0, editor.screenCols - 2);
    int maxW = max_line_width(lines, editor.gitUseDefaultColors);
    int maxOffset = std::max(0, maxW - viewWidth);
    int hOff = std::min(horizontalOffset, maxOffset);
    const auto hunks = parseHunks(lines);
    const int selectedHunk = hunkAtView();
    const int selectedHeader =
        selectedHunk >= 0 && currentHunk >= 0 ? hunks[selectedHunk].header : -1;
    for(int i = 0; i < availableRows; ++i)
    {
        output += Terminal::NEWLINE_CLEAR;
        int idx = i + scrollOffset;
        if(idx >= 0 && idx < (int)lines.size())
        {
            // Mark the selected hunk's header line.
            output += idx == selectedHeader ? editor.theme.selection() + "> "
                                            : std::string("  ");
            if(editor.gitUseDefaultColors)
            {
                output += slice_with_ansi(lines[idx], hOff, viewWidth);
                output += editor.theme.reset();
            }
            else
            {
                const std::string& line = lines[idx];
                const std::string* lineSeq = &editor.theme.baseFg();
                if(line.rfind("diff --git", 0) == 0 ||
                   line.rfind("--- ", 0) == 0 || line.rfind("+++ ", 0) == 0)
                    lineSeq = &editor.theme.uiAccent();
                else if(
                    line.rfind("index ", 0) == 0 ||
                    line.rfind("commit ", 0) == 0)
                    lineSeq = &editor.theme.uiDim();
                else if(line.rfind("@@ ", 0) == 0)
                    lineSeq = &editor.theme.uiInfo();
                else if(
                    line.rfind("Author:", 0) == 0 ||
                    line.rfind("Date:", 0) == 0)
                    lineSeq = &editor.theme.uiDim();
                else if(
                    !line.empty() &&
                    line[0] == keyCode(command::CommandKey::KEY_PLUS))
                    lineSeq = &editor.theme.uiSuccess();
                else if(
                    !line.empty() &&
                    line[0] == keyCode(command::CommandKey::KEY_MINUS))
                    lineSeq = &editor.theme.uiError();

                std::string normalSeq = editor.theme.reset() + *lineSeq;
                if(idx == selectedHeader)
                    normalSeq = editor.theme.reset() +
                                editor.theme.selection() + *lineSeq;
                std::string sliced = slice_plain(line, hOff, viewWidth);
                append_highlighted(
                    output,
                    sliced,
                    searchQuery,
                    normalSeq,
                    editor.theme.searchMatch());
                output += editor.theme.reset();
            }
        }
        else
        {
            output += editor.theme.uiGutter();
            output += "  ~";
            output += editor.theme.baseFg();
        }
    }

    output += Terminal::NEWLINE_CLEAR;

    std::string status = " GITSHOW";
    if(!commitHash.empty())
        status += " | " + commitHash;
    if(showDropped)
        status += " | DROPPED CHANGES";
    else if(!droppedOriginal.empty())
        status += " | NEW COMMIT (" + std::to_string(droppedOriginal.size()) +
                  " dropped, c: rewrite)";

    std::string right =
        " " + std::to_string(scrollOffset + 1) + "-" +
        std::to_string(
            std::min(scrollOffset + availableRows, (int)lines.size())) +
        "/" + std::to_string(lines.size()) + " ";
    if(!hunks.empty())
        right = " hunk " +
                (currentHunk >= 0 ? std::to_string(selectedHunk + 1) : "-") +
                "/" + std::to_string(hunks.size()) + " |" + right;

    editor.appendStatusLine(output, status, right);

    output += Terminal::NEWLINE_CLEAR;
    if(!editor.statusMessage.empty())
    {
        output += editor.statusMessage.substr(
            0,
            std::min((size_t)editor.screenCols, editor.statusMessage.length()));
    }
    else if(searchActive)
    {
        output += (searchForward ? "/" : "?") + searchQuery;
    }

    const bool syncOutput = Terminal::useSynchronizedOutput();
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
    Terminal::write(output);
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
    Terminal::flush();
}

#ifdef UVIM_TESTING
std::string GitShowCommitMode::testRenderLine(
    const Theme& theme,
    std::string_view line,
    std::string_view query,
    bool useDefaultColors)
{
    if(useDefaultColors)
        return std::string(line);

    const std::string* lineSeq = &theme.baseFg();
    if(line.rfind("diff --git", 0) == 0 || line.rfind("--- ", 0) == 0 ||
       line.rfind("+++ ", 0) == 0)
        lineSeq = &theme.uiAccent();
    else if(line.rfind("index ", 0) == 0 || line.rfind("commit ", 0) == 0)
        lineSeq = &theme.uiDim();
    else if(line.rfind("@@ ", 0) == 0)
        lineSeq = &theme.uiInfo();
    else if(line.rfind("Author:", 0) == 0 || line.rfind("Date:", 0) == 0)
        lineSeq = &theme.uiDim();
    else if(!line.empty() && line[0] == keyCode(command::CommandKey::KEY_PLUS))
        lineSeq = &theme.uiSuccess();
    else if(!line.empty() && line[0] == keyCode(command::CommandKey::KEY_MINUS))
        lineSeq = &theme.uiError();

    std::string normalSeq = theme.reset() + *lineSeq;
    std::string output;
    output.reserve(line.size() + 32);
    append_highlighted(output, line, query, normalSeq, theme.searchMatch());
    output += theme.reset();
    return output;
}
#endif
} // namespace editor::statemachine
