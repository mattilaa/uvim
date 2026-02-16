#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string_view>

// ============================================================================
// GitLogMode Implementation
// ============================================================================

namespace
{
void append_highlighted(std::string& out, std::string_view text,
                        std::string_view query, const std::string& normalSeq,
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
        if(found == std::string_view::npos)
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

std::vector<std::string> run_git_lines(const std::string& cmd)
{
    std::vector<std::string> out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
        return out;

    std::string output;
    char buffer[2048];
    while(fgets(buffer, sizeof(buffer), pipe))
        output += buffer;
    pclose(pipe);

    size_t pos = 0;
    while(pos <= output.size())
    {
        size_t next = output.find('\n', pos);
        if(next == std::string::npos)
        {
            if(pos < output.size())
                out.push_back(output.substr(pos));
            break;
        }
        out.push_back(output.substr(pos, next - pos));
        pos = next + 1;
    }
    return out;
}

bool is_ansi_start(std::string_view text, size_t i)
{
    return i + 1 < text.size() && text[i] == '\x1b' && text[i + 1] == keyCode(command::CommandKey::KEY_LEFT_BRACKET);
}

size_t skip_ansi(std::string_view text, size_t i)
{
    i += 2;
    while(i < text.size())
    {
        char c = text[i++];
        if((c >= keyCode(typed::TypedKey::KEY_CAP_A) && c <= keyCode(typed::TypedKey::KEY_CAP_Z)) || (c >= keyCode(typed::TypedKey::KEY_A) && c <= keyCode(typed::TypedKey::KEY_Z)))
            break;
    }
    return i;
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

void append_pretty_diff_line(std::string& output, const Editor& editor,
                             const std::string& line)
{
    if(editor.gitUseDefaultColors)
    {
        output += line;
        output += editor.theme.reset();
        return;
    }

    auto append_colored = [&](std::string_view txt, const std::string& color)
    {
        output += color;
        output.append(txt.data(), txt.size());
        output += editor.theme.reset();
    };

    if(line.rfind("commit ", 0) == 0)
    {
        append_colored("commit ", editor.theme.uiAccent());
        std::string hashAndRefs = line.substr(7);
        size_t refsPos = hashAndRefs.find(" (");
        if(refsPos == std::string::npos)
        {
            append_colored(hashAndRefs, editor.theme.uiWarning());
        }
        else
        {
            append_colored(hashAndRefs.substr(0, refsPos), editor.theme.uiWarning());
            append_colored(hashAndRefs.substr(refsPos), editor.theme.uiError());
        }
        return;
    }
    if(line.rfind("Author:", 0) == 0 || line.rfind("Commit:", 0) == 0 ||
       line.rfind("AuthorDate:", 0) == 0 || line.rfind("CommitDate:", 0) == 0 ||
       line.rfind("Date:", 0) == 0)
    {
        size_t colon = line.find(keyCode(command::CommandKey::KEY_COLON));
        if(colon == std::string::npos)
        {
            append_colored(line, editor.theme.uiAccent());
            return;
        }
        append_colored(std::string_view(line.data(), colon + 1), editor.theme.uiAccent());
        if(colon + 1 < line.size())
            append_colored(std::string_view(line.data() + colon + 1, line.size() - colon - 1),
                           editor.theme.uiSuccess());
        return;
    }
    if(line.rfind("diff --git ", 0) == 0)
    {
        std::string_view prefix("diff --git ");
        append_colored(prefix, editor.theme.uiAccent());
        std::string rest = line.substr(prefix.size());
        size_t split = rest.find(keyCode(control::ControlKey::SPACE));
        if(split == std::string::npos)
        {
            append_colored(rest, editor.theme.uiInfo());
            return;
        }
        append_colored(rest.substr(0, split), editor.theme.uiInfo());
        append_colored(" ", editor.theme.baseFg());
        append_colored(rest.substr(split + 1), editor.theme.uiInfo());
        return;
    }
    if(line.rfind("--- ", 0) == 0 || line.rfind("+++ ", 0) == 0 ||
       line.rfind("rename from ", 0) == 0 || line.rfind("rename to ", 0) == 0 ||
       line.rfind("Binary files ", 0) == 0)
    {
        size_t space = line.find(keyCode(control::ControlKey::SPACE));
        if(space == std::string::npos)
        {
            append_colored(line, editor.theme.uiAccent());
            return;
        }
        append_colored(std::string_view(line.data(), space + 1), editor.theme.uiAccent());
        if(space + 1 < line.size())
            append_colored(std::string_view(line.data() + space + 1, line.size() - space - 1),
                           editor.theme.uiInfo());
        return;
    }
    if(line.rfind("index ", 0) == 0 || line.rfind("new file mode ", 0) == 0 ||
       line.rfind("deleted file mode ", 0) == 0 ||
       line.rfind("similarity index ", 0) == 0)
    {
        append_colored(line, editor.theme.uiDim());
        return;
    }
    if(line.find(" | ") != std::string::npos)
    {
        append_colored(line, editor.theme.uiInfo());
        return;
    }
    if(line.find(" changed, ") != std::string::npos ||
       line.find(" insertion") != std::string::npos ||
       line.find(" deletion") != std::string::npos)
    {
        append_colored(line, editor.theme.uiWarning());
        return;
    }
    if(line.rfind("@@ ", 0) == 0)
    {
        append_colored(line, editor.theme.uiInfo());
        return;
    }
    if(!line.empty() && line[0] == keyCode(command::CommandKey::KEY_PLUS))
    {
        append_colored(line, editor.theme.uiSuccess());
        return;
    }
    if(!line.empty() && line[0] == keyCode(command::CommandKey::KEY_MINUS))
    {
        append_colored(line, editor.theme.uiError());
        return;
    }
    if(!line.empty())
    {
        append_colored(line, editor.theme.uiDim());
        return;
    }
    append_colored(line, editor.theme.baseFg());
}
} // namespace

void GitLogMode::rebuildFilter(Editor& editor)
{
    filtered.clear();
    if(entries.empty())
        return;

    if(query.empty())
    {
        filtered.reserve(entries.size());
        for(int i = 0; i < (int)entries.size(); ++i)
            filtered.push_back(i);
    }
    else
    {
        std::vector<std::pair<int, int>> scored;
        scored.reserve(entries.size());
        std::vector<int> positions;
        for(int i = 0; i < (int)entries.size(); ++i)
        {
            std::string text = entries[i].hash + " " + entries[i].subject;
            int score = editor.fuzzyScore(query, text, positions);
            if(score >= 0)
                scored.emplace_back(i, score);
        }
        std::stable_sort(scored.begin(), scored.end(),
                         [](const auto& a, const auto& b)
                         {
                             if(a.second != b.second)
                                 return a.second > b.second;
                             return a.first < b.first;
                         });
        for(const auto& item : scored)
            filtered.push_back(item.first);
    }

    cursor =
        std::clamp(cursor, 0, filtered.empty() ? 0 : (int)filtered.size() - 1);
    int window = std::max(1, editor.screenRows - 2);
    if(cursor < scrollOffset)
        scrollOffset = cursor;
    else if(cursor >= scrollOffset + window)
        scrollOffset = cursor - window + 1;

    if(prettyView)
        diffDirty = true;
}

void GitLogMode::ensurePrettyPreview(Editor& editor)
{
    if(!prettyView || !diffDirty)
        return;

    previewLines.clear();
    diffOffset = 0;
    diffHorizontalOffset = 0;
    previewHash.clear();

    if(filtered.empty())
    {
        previewLines.push_back("(no commits)");
        diffDirty = false;
        return;
    }

    cursor = std::clamp(cursor, 0, (int)filtered.size() - 1);

    int idx = filtered[cursor];
    if(idx < 0 || idx >= (int)entries.size())
    {
        previewLines.push_back("(invalid selection)");
        diffDirty = false;
        return;
    }

    const std::string& hash = entries[idx].hash;
    previewHash = hash;

    auto cacheIt = previewCache.find(hash);
    if(cacheIt != previewCache.end())
    {
        previewLines = cacheIt->second;
        diffDirty = false;
        return;
    }

    const std::string repoDirUse = !repoDir.empty() ? repoDir : repoRoot;
    if(repoDirUse.empty())
    {
        previewLines.push_back("(repo unavailable)");
        diffDirty = false;
        return;
    }

    std::string cmd = "git -C \"" + repoDirUse +
                      "\" --no-pager show --patch --stat ";
    cmd += editor.gitUseDefaultColors ? "--color=always " : "--no-color ";
    cmd += "\"" + hash + "\" 2>/dev/null";

    previewLines = run_git_lines(cmd);
    if(previewLines.empty())
        previewLines.push_back("(no diff output)");

    previewCache.emplace(hash, previewLines);
    previewCacheOrder.push_back(hash);
    constexpr size_t kPreviewCacheMax = 48;
    if(previewCacheOrder.size() > kPreviewCacheMax)
    {
        const std::string& oldest = previewCacheOrder.front();
        previewCache.erase(oldest);
        previewCacheOrder.erase(previewCacheOrder.begin());
    }
    diffDirty = false;
}

void GitLogMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    rebuildFilter(*ed);
    if(filtered.empty())
    {
        cursor = 0;
        scrollOffset = 0;
        rangeSelectActive = false;
        rangeSelectBase.clear();
    }
    else
    {
        cursor = std::clamp(cursor, 0, (int)filtered.size() - 1);
        scrollOffset = std::clamp(scrollOffset, 0, std::max(0, cursor));
        if(rangeSelectActive)
            rangeSelectAnchor = std::clamp(rangeSelectAnchor, 0,
                                           (int)filtered.size() - 1);
    }
    ensurePrettyPreview(*ed);
    ctx.requestFullRedraw();
}

void GitLogMode::on_exit(ModeContext& ctx)
{
    ctx.requestFullRedraw();
}

std::optional<ModeState> GitLogMode::handle(ModeContext& ctx,
                                            int key)
{
    Editor* ed = ctx.editor;
    if(filtered.empty())
    {
        cursor = 0;
        scrollOffset = 0;
        rangeSelectActive = false;
        rangeSelectBase.clear();
    }
    else
    {
        cursor = std::clamp(cursor, 0, (int)filtered.size() - 1);
        int window = std::max(1, ed->screenRows - 2);
        int maxScroll = std::max(0, (int)filtered.size() - window);
        scrollOffset = std::clamp(scrollOffset, 0, maxScroll);
        if(rangeSelectActive)
            rangeSelectAnchor = std::clamp(rangeSelectAnchor, 0,
                                           (int)filtered.size() - 1);
    }

    int c = keyCode(key);
    int prevCursor = cursor;
    auto mark_preview_dirty = [&]()
    {
        if(prettyView)
        {
            diffDirty = true;
            lastCursorMove = std::chrono::steady_clock::now();
        }
    };
    auto apply_range_selection = [&]()
    {
        if(!rangeSelectActive || filtered.empty())
            return;
        rangeSelectAnchor = std::clamp(rangeSelectAnchor, 0,
                                       (int)filtered.size() - 1);
        selectedHashes = rangeSelectBase;
        int lo = std::min(rangeSelectAnchor, cursor);
        int hi = std::max(rangeSelectAnchor, cursor);
        for(int i = lo; i <= hi; ++i)
        {
            int idx = filtered[i];
            if(idx >= 0 && idx < (int)entries.size())
                selectedHashes.insert(entries[idx].hash);
        }
    };

    auto findNextMatch = [&](bool forward)
    {
        if(searchQuery.empty() || filtered.empty())
            return false;
        int start = cursor;
        int found = -1;
        if(forward)
        {
            for(int i = start + 1; i < (int)filtered.size(); ++i)
            {
                int idx = filtered[i];
                std::string text =
                    entries[idx].hash + " " + entries[idx].subject;
                if(text.find(searchQuery) != std::string::npos)
                {
                    found = i;
                    break;
                }
            }
            if(found < 0)
            {
                for(int i = 0; i <= start; ++i)
                {
                    int idx = filtered[i];
                    std::string text =
                        entries[idx].hash + " " + entries[idx].subject;
                    if(text.find(searchQuery) != std::string::npos)
                    {
                        found = i;
                        break;
                    }
                }
            }
        }
        else
        {
            for(int i = start - 1; i >= 0; --i)
            {
                int idx = filtered[i];
                std::string text =
                    entries[idx].hash + " " + entries[idx].subject;
                if(text.find(searchQuery) != std::string::npos)
                {
                    found = i;
                    break;
                }
            }
            if(found < 0)
            {
                for(int i = (int)filtered.size() - 1; i >= start; --i)
                {
                    int idx = filtered[i];
                    std::string text =
                        entries[idx].hash + " " + entries[idx].subject;
                    if(text.find(searchQuery) != std::string::npos)
                    {
                        found = i;
                        break;
                    }
                }
            }
        }
        if(found >= 0)
        {
            cursor = found;
            int window = std::max(1, ed->screenRows - 2);
            if(cursor < scrollOffset)
                scrollOffset = cursor;
            else if(cursor >= scrollOffset + window)
                scrollOffset = cursor - window + 1;
            mark_preview_dirty();
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
                    now - ed->lastEscTime)
                    .count();
            if(timeSinceLastEsc <= Editor::DOUBLE_ESC_TIMEOUT_MS)
            {
                cursor = searchPrevCursor;
                scrollOffset = searchPrevScroll;
                searchQuery.clear();
                ed->setStatusMessage("");
                ed->lastEscTime = std::chrono::steady_clock::time_point();
            }
            else
            {
                ed->lastEscTime = now;
            }
            if(prettyView && cursor != prevCursor)
                mark_preview_dirty();
            searchActive = false;
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ENTER))
        {
            if(searchQuery.empty())
            {
                cursor = searchPrevCursor;
                scrollOffset = searchPrevScroll;
                if(prettyView && cursor != prevCursor)
                    mark_preview_dirty();
                searchActive = false;
                ctx.requestFullRedraw();
                return std::nullopt;
            }
            if(!filtered.empty())
            {
                int start = cursor;
                int found = -1;
                if(searchForward)
                {
                    for(int i = start + 1; i < (int)filtered.size(); ++i)
                    {
                        int idx = filtered[i];
                        std::string text =
                            entries[idx].hash + " " + entries[idx].subject;
                        if(text.find(searchQuery) != std::string::npos)
                        {
                            found = i;
                            break;
                        }
                    }
                }
                else
                {
                    for(int i = start - 1; i >= 0; --i)
                    {
                        int idx = filtered[i];
                        std::string text =
                            entries[idx].hash + " " + entries[idx].subject;
                        if(text.find(searchQuery) != std::string::npos)
                        {
                            found = i;
                            break;
                        }
                    }
                }
                if(found >= 0)
                {
                    cursor = found;
                    int window = std::max(1, ed->screenRows - 2);
                    if(cursor < scrollOffset)
                        scrollOffset = cursor;
                    else if(cursor >= scrollOffset + window)
                        scrollOffset = cursor - window + 1;
                    mark_preview_dirty();
                }
                else
                {
                    ed->setStatusMessage("search: not found");
                }
            }
            searchActive = false;
            ctx.requestFullRedraw();
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 || c == keyCode(control::ControlKey::CTRL_H))
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

    if(c == keyCode(control::ControlKey::ESC) && rangeSelectActive)
    {
        rangeSelectActive = false;
        rangeSelectBase.clear();
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::ESC) || c == keyCode(typed::TypedKey::KEY_Q))
    {
        if(c == keyCode(control::ControlKey::ESC))
            ed->noteDoubleEscStatusClear();
        return NormalMode{};
    }

    if(c == keyCode(control::ControlKey::ENTER))
    {
        if(filtered.empty())
            return std::nullopt;
        int idx = filtered[cursor];
        if(idx < 0 || idx >= (int)entries.size())
            return std::nullopt;
        std::vector<std::string> showLines =
            ed->loadGitShowLines(entries[idx].hash);
        if(showLines.empty())
        {
            ed->setStatusMessage("git show: no output");
            return std::nullopt;
        }
        return GitShowCommitMode{entries[idx].hash, std::move(showLines),
                                 *this};
    }

    int window = std::max(1, ed->screenRows - 2);
    int maxScroll = std::max(0, (int)filtered.size() - window);

    if(prettyView && c == keyCode(control::ControlKey::CTRL_J))
    {
        int maxDiffScroll =
            std::max(0, (int)previewLines.size() - std::max(1, ed->screenRows - 2));
        if(diffOffset < maxDiffScroll)
            diffOffset++;
    }
    else if(prettyView && c == keyCode(control::ControlKey::CTRL_K))
    {
        if(diffOffset > 0)
            diffOffset--;
    }
    else if(c == keyCode(navigation::NavigationKey::ARROW_DOWN) || c == keyCode(typed::TypedKey::KEY_J) ||
            (!prettyView && c == keyCode(control::ControlKey::CTRL_J)))
    {
        if(cursor + 1 < (int)filtered.size())
        {
            cursor++;
            if(cursor >= scrollOffset + window)
                scrollOffset = cursor - window + 1;
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_K) || c == keyCode(navigation::NavigationKey::ARROW_UP) || c == keyCode(typed::TypedKey::KEY_K))
    {
        if(cursor > 0)
        {
            cursor--;
            if(cursor < scrollOffset)
                scrollOffset = cursor;
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_D))
    {
        cursor = std::min(cursor + window / 2,
                          filtered.empty() ? 0 : (int)filtered.size() - 1);
        scrollOffset = std::min(scrollOffset + window / 2, maxScroll);
    }
    else if(c == keyCode(control::ControlKey::CTRL_U))
    {
        cursor = std::max(cursor - window / 2, 0);
        scrollOffset = std::max(scrollOffset - window / 2, 0);
    }
    else if(c == keyCode(typed::TypedKey::KEY_CAP_G))
    {
        cursor = filtered.empty() ? 0 : (int)filtered.size() - 1;
        scrollOffset = maxScroll;
    }
    else if(c == keyCode(typed::TypedKey::KEY_G))
    {
        int nextChar = Terminal::readKey();
        if(nextChar == keyCode(typed::TypedKey::KEY_G))
        {
            cursor = 0;
            scrollOffset = 0;
        }
        else if(nextChar == keyCode(typed::TypedKey::KEY_R))
        {
            if(filtered.empty())
                return std::nullopt;
            int idx = filtered[cursor];
            if(idx < 0 || idx >= (int)entries.size())
                return std::nullopt;

            GitCommitMode revertMode{repoRoot, repoDir};
            revertMode.action = GitCommitMode::Action::RevertCommit;
            revertMode.returnLog = *this;
            revertMode.revertHash = entries[idx].hash;
            revertMode.revertSubject = entries[idx].subject;
            revertMode.messageLines = {
                "Revert \"" + entries[idx].subject + "\"",
                "",
                "This reverts commit " + entries[idx].hash + ".",
            };
            revertMode.messageCursorRow = 0;
            revertMode.messageCursorCol = 0;
            revertMode.insertMode = false;
            revertMode.stagedDirty = true;
            return revertMode;
        }
        else if(nextChar == keyCode(typed::TypedKey::KEY_F))
        {
            std::vector<GitLogMode::Entry> picked;
            if(!selectedHashes.empty())
            {
                picked.reserve(entries.size());
                for(const auto& entry : entries)
                {
                    if(selectedHashes.count(entry.hash) != 0)
                        picked.push_back(entry);
                }
            }
            else
            {
                if(filtered.empty())
                    return std::nullopt;
                int idx = filtered[cursor];
                if(idx < 0 || idx >= (int)entries.size())
                    return std::nullopt;
                picked.assign(entries.begin(), entries.begin() + idx + 1);
            }

            if(picked.empty())
            {
                ed->setStatusMessage("git rebase: no commits selected");
                return std::nullopt;
            }

            GitCommitMode rebaseMode{repoRoot, repoDir};
            rebaseMode.action = GitCommitMode::Action::RebaseTodo;
            rebaseMode.returnLog = *this;
            rebaseMode.rebaseHeadHash = picked.front().hash;
            rebaseMode.rebaseBaseHash = picked.back().hash;
            rebaseMode.rebaseCommandCount = (int)picked.size();
            rebaseMode.messageLines.clear();
            for(auto it = picked.rbegin(); it != picked.rend(); ++it)
            {
                const auto& entry = *it;
                std::string verb = "pick";
                if(entry.subject.rfind("fixup!", 0) == 0)
                    verb = "fixup";
                else if(entry.subject.rfind("squash!", 0) == 0)
                    verb = "squash";
                rebaseMode.messageLines.push_back(verb + " " + entry.hash +
                                                  " " + entry.subject);
            }
            if(rebaseMode.messageLines.empty())
                rebaseMode.messageLines.push_back("");
            rebaseMode.messageCursorRow = 0;
            rebaseMode.messageCursorCol = 0;
            rebaseMode.insertMode = false;
            rebaseMode.stagedDirty = false;
            return rebaseMode;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_V))
    {
        if(filtered.empty())
            return std::nullopt;
        if(rangeSelectActive)
        {
            rangeSelectActive = false;
            rangeSelectBase.clear();
        }
        else
        {
            rangeSelectActive = true;
            rangeSelectAnchor = cursor;
            rangeSelectBase = selectedHashes;
            apply_range_selection();
        }
    }
    else if(c == keyCode(control::ControlKey::SPACE))
    {
        if(rangeSelectActive)
        {
            rangeSelectActive = false;
            rangeSelectBase.clear();
        }
        if(filtered.empty())
            return std::nullopt;
        int idx = filtered[cursor];
        if(idx < 0 || idx >= (int)entries.size())
            return std::nullopt;
        const std::string& hash = entries[idx].hash;
        if(selectedHashes.count(hash) != 0)
            selectedHashes.erase(hash);
        else
            selectedHashes.insert(hash);
    }
    else if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 || c == keyCode(control::ControlKey::CTRL_H))
    {
        if(!query.empty())
        {
            query.pop_back();
            rebuildFilter(*ed);
            if(rangeSelectActive)
                apply_range_selection();
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_U))
    {
        if(!query.empty())
        {
            query.clear();
            rebuildFilter(*ed);
            if(rangeSelectActive)
                apply_range_selection();
        }
    }
    else if(c == keyCode(command::CommandKey::KEY_SLASH) || c == keyCode(command::CommandKey::KEY_QUESTION))
    {
        searchActive = true;
        searchForward = (c == keyCode(command::CommandKey::KEY_SLASH));
        searchQuery.clear();
        searchPrevCursor = cursor;
        searchPrevScroll = scrollOffset;
        ed->lastEscTime = std::chrono::steady_clock::time_point();
    }
    else if((c == keyCode(typed::TypedKey::KEY_N) || c == keyCode(typed::TypedKey::KEY_CAP_N)) && !searchQuery.empty())
    {
        bool forward = (c == keyCode(typed::TypedKey::KEY_N)) ? searchForward : !searchForward;
        if(!findNextMatch(forward))
            ed->setStatusMessage("search: not found");
    }
    else if(c >= 32 && c < 127)
    {
        query += static_cast<char>(c);
        rebuildFilter(*ed);
        if(rangeSelectActive)
            apply_range_selection();
    }

    if(rangeSelectActive && cursor != prevCursor)
        apply_range_selection();

    if(prettyView && cursor != prevCursor)
        mark_preview_dirty();

    ctx.requestFullRedraw();
    return std::nullopt;
}

void GitLogMode::draw(Editor& editor) const
{
    auto* self = const_cast<GitLogMode*>(this);
    if(self->prettyView && self->diffDirty)
    {
        auto now = std::chrono::steady_clock::now();
        auto sinceMove =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - self->lastCursorMove)
                .count();
        if(sinceMove > 75)
            self->ensurePrettyPreview(editor);
    }
    if(self->filtered.empty())
    {
        self->cursor = 0;
        self->scrollOffset = 0;
        self->rangeSelectActive = false;
        self->rangeSelectBase.clear();
    }
    else
    {
        self->cursor = std::clamp(self->cursor, 0,
                                  (int)self->filtered.size() - 1);
        int window = std::max(1, editor.screenRows - 2);
        int maxScroll = std::max(0, (int)self->filtered.size() - window);
        self->scrollOffset = std::clamp(self->scrollOffset, 0, maxScroll);
        if(self->rangeSelectActive)
            self->rangeSelectAnchor =
                std::clamp(self->rangeSelectAnchor, 0,
                           (int)self->filtered.size() - 1);
    }

    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::ESC_CURSOR_HOME;
    output += editor.theme.reset();

    output += Terminal::ESC_CLEAR_LINE;
    output += editor.theme.uiAccent();
    output += Terminal::ESC_BOLD;
    output += prettyView ? "  GIT PRETTYLOG" : "  GITLOG";
    if(fileOnly)
        output += " (file)";
    output += editor.theme.reset();
    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    if(prettyView)
    {
        output += "  [v: range] [space: select] [q: quit] [j/k: commit] "
                  "[ctrl-j/k: diff scroll] [gf: rebase] [enter: show] "
                  "[gr: revert]";
    }
    else
    {
        output += "  [v: range] [space: select] [q: quit] [ctrl-j/k: move] "
                  "[gf: rebase] [enter: show] [gr: revert] [type: filter]";
    }
    output += editor.theme.baseFg();

    int availableRows = editor.screenRows - 2;
    if(prettyView)
    {
        int leftWidth = std::clamp(editor.screenCols * 2 / 5, 32,
                                   std::max(32, editor.screenCols - 24));
        int rightWidth = std::max(1, editor.screenCols - leftWidth - 1);

        for(int i = 0; i < availableRows; ++i)
        {
            output += Terminal::NEWLINE_CLEAR;

            int idx = scrollOffset + i;
            bool selected = (idx == cursor);
            if(idx >= 0 && idx < (int)filtered.size())
            {
                int entryIndex = filtered[idx];
                const auto& entry = entries[entryIndex];
                bool marked = selectedHashes.count(entry.hash) != 0;
                bool inRange = false;
                if(rangeSelectActive && !filtered.empty())
                {
                    int lo = std::min(rangeSelectAnchor, cursor);
                    int hi = std::max(rangeSelectAnchor, cursor);
                    inRange = (idx >= lo && idx <= hi);
                }
                int leftContentWidth = std::max(1, leftWidth - 2);
                if(selected || marked)
                {
                    std::string leftText = entry.subject;
                    if(!entry.author.empty() || !entry.date.empty())
                    {
                        leftText.clear();
                        if(!entry.date.empty())
                            leftText += entry.date + " ";
                        if(!entry.author.empty())
                            leftText += entry.author + " ";
                        leftText += entry.subject;
                    }
                    std::string leftTrim = slice_plain(leftText, 0, leftContentWidth);
                    int leftDisplay = text_utils::utf8DisplayWidth(leftTrim);
                    if(leftDisplay < leftContentWidth)
                        leftTrim.append(leftContentWidth - leftDisplay, keyCode(control::ControlKey::SPACE));
                    if(selected)
                        output += editor.theme.selection();
                    else
                        output += std::string(Terminal::ESC_DIM) +
                                  editor.theme.selection();
                    output += marked ? "*" : (inRange ? "+" : " ");
                    output += leftTrim;
                    output += editor.theme.reset();
                }
                else
                {
                    output += editor.theme.baseFg();
                    output += marked ? "*" : (inRange ? "+" : " ");

                    int usedWidth = 0;
                    auto append_part = [&](const std::string& text,
                                           const std::string& color)
                    {
                        if(text.empty() || usedWidth >= leftContentWidth)
                            return;
                        int remain = leftContentWidth - usedWidth;
                        std::string clipped = slice_plain(text, 0, remain);
                        int w = text_utils::utf8DisplayWidth(clipped);
                        if(w <= 0)
                            return;
                        output += color;
                        output += clipped;
                        output += editor.theme.baseFg();
                        usedWidth += w;
                    };

                    if(!entry.date.empty())
                        append_part(entry.date + " ", editor.theme.uiInfo());
                    if(!entry.author.empty())
                        append_part(entry.author + " ", editor.theme.uiAccent());
                    append_part(entry.subject, editor.theme.baseFg());

                    if(usedWidth < leftContentWidth)
                        output.append(leftContentWidth - usedWidth, keyCode(control::ControlKey::SPACE));
                    output += editor.theme.reset();
                }
            }
            else
            {
                int leftContentWidth = std::max(1, leftWidth - 2);
                output += editor.theme.uiGutter();
                output += " ~";
                if(leftContentWidth > 1)
                    output.append(leftContentWidth - 1, keyCode(control::ControlKey::SPACE));
                output += editor.theme.reset();
            }

            output += editor.theme.uiGutter();
            output += "|";
            output += editor.theme.reset();

            int diffIdx = diffOffset + i;
            if(diffIdx >= 0 && diffIdx < (int)previewLines.size())
            {
                const std::string& diffLine = previewLines[diffIdx];
                output += " ";
                if(editor.gitUseDefaultColors)
                {
                    output += slice_with_ansi(diffLine, diffHorizontalOffset,
                                              std::max(0, rightWidth - 1));
                    output += editor.theme.reset();
                }
                else
                {
                    std::string sliced = slice_plain(diffLine, diffHorizontalOffset,
                                                     std::max(0, rightWidth - 1));
                    append_pretty_diff_line(output, editor, sliced);
                }
            }
            else
            {
                output += editor.theme.uiGutter();
                output += " ~";
                output += editor.theme.baseFg();
            }
        }
    }
    else
    {
        for(int i = 0; i < availableRows; ++i)
        {
            output += Terminal::NEWLINE_CLEAR;
            int idx = scrollOffset + i;
            if(idx >= 0 && idx < (int)filtered.size())
            {
                int entryIndex = filtered[idx];
                const auto& entry = entries[entryIndex];
                output += "  ";
                bool selected = (idx == cursor);
                bool marked = selectedHashes.count(entry.hash) != 0;
                bool inRange = false;
                if(rangeSelectActive && !filtered.empty())
                {
                    int lo = std::min(rangeSelectAnchor, cursor);
                    int hi = std::max(rangeSelectAnchor, cursor);
                    inRange = (idx >= lo && idx <= hi);
                }
                std::string normalHash = selected
                                             ? editor.theme.selection()
                                             : (editor.theme.reset() +
                                                editor.theme.uiAccent());
                std::string normalText = selected
                                             ? editor.theme.selection()
                                             : (editor.theme.reset() +
                                                editor.theme.baseFg());
                if(!selected && marked)
                {
                    std::string markedStyle =
                        std::string(Terminal::ESC_DIM) + editor.theme.selection();
                    normalHash = markedStyle;
                    normalText = markedStyle;
                }
                const std::string& matchSeq = editor.theme.searchMatch();
                int maxLine = std::max(0, editor.screenCols - 6);
                std::string hash = entry.hash;
                if(hash.size() > 12)
                    hash.resize(12);
                std::string subject = entry.subject;
                int keep = maxLine - (int)hash.size() - 3;
                if(keep < 0)
                    keep = 0;
                if((int)subject.size() > keep)
                    subject.resize(keep);
                append_highlighted(output, marked ? "*" : (inRange ? "+" : " "),
                                   searchQuery,
                                   normalText, matchSeq);
                append_highlighted(output, hash, searchQuery, normalHash, matchSeq);
                append_highlighted(output, " ", searchQuery, normalText, matchSeq);
                append_highlighted(output, subject, searchQuery, normalText,
                                   matchSeq);
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

    std::string status = prettyView ? " GIT PRETTYLOG" : " GITLOG";
    if(rangeSelectActive)
        status += " [VISUAL]";
    std::string right = " " + std::to_string(filtered.empty() ? 0 : cursor + 1) +
                        "/" + std::to_string(filtered.size());
    right += " | sel " + std::to_string(selectedHashes.size());
    if(prettyView)
    {
        int maxDiffScroll =
            std::max(0, (int)previewLines.size() - std::max(1, editor.screenRows - 2));
        right += " | diff " + std::to_string(diffOffset + 1) + "/" +
                 std::to_string(std::max(1, maxDiffScroll + 1));
    }
    right += " ";

    output += status;
    int padding = editor.screenCols - status.length() - right.length();
    if(padding > 0)
        output.append(padding, keyCode(control::ControlKey::SPACE));
    output += right;
    output += editor.theme.reset();

    output += Terminal::NEWLINE_CLEAR;
    if(prettyView && !searchActive && query.empty() && !filtered.empty())
    {
        int idx = std::clamp(cursor, 0, (int)filtered.size() - 1);
        const auto& hash = entries[filtered[idx]].hash;
        std::string shortHash = hash.substr(0, std::min<size_t>(12, hash.size()));
        output += "commit: " + shortHash;
    }
    else if(searchActive)
        output += (searchForward ? "/" : "?") + searchQuery;
    else if(!query.empty())
        output += "filter: " + query;

    const bool syncOutput = Terminal::useSynchronizedOutput();
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
    Terminal::write(output);
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
    Terminal::flush();
}

#ifdef UVIM_TESTING
std::string GitLogMode::testRenderLine(const Theme& theme, const Entry& entry,
                                       std::string_view query, bool selected,
                                       int screenCols)
{
    std::string output;
    output.reserve(256);
    std::string normalHash =
        selected ? theme.selection() : (theme.reset() + theme.uiAccent());
    std::string normalText =
        selected ? theme.selection() : (theme.reset() + theme.baseFg());
    const std::string& matchSeq = theme.searchMatch();
    int maxLine = std::max(0, screenCols - 4);
    std::string hash = entry.hash;
    std::string subject = entry.subject;
    int keep = maxLine - (int)hash.size() - 1;
    if(keep < 0)
        keep = 0;
    if((int)subject.size() > keep)
        subject.resize(keep);
    append_highlighted(output, hash, query, normalHash, matchSeq);
    append_highlighted(output, " ", query, normalText, matchSeq);
    append_highlighted(output, subject, query, normalText, matchSeq);
    output += theme.reset();
    return output;
}
#endif
