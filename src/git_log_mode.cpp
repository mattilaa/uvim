#include "editor.h"
#include "editor_utils.h"
#include "header_help.h"
#include "mode_state_machine.h"
#include "process_pipe.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string_view>

// ============================================================================
// GitLogMode Implementation
// ============================================================================

namespace editor::statemachine
{
namespace
{
std::vector<std::string> gitLogHelpTokens(bool prettyView)
{
    if(prettyView)
        return {"[^V: range]",
                "[space: select]",
                "[q: quit]",
                "[j/k: commit]",
                "[ctrl-j/k: diff scroll]",
                "[gf: rebase]",
                "[enter: show]",
                "[gr: revert]"};
    return {"[^V: range]",      "[space: select]", "[q: quit]",
            "[ctrl-j/k: move]", "[gf: rebase]",    "[enter: show]",
            "[gr: revert]",     "[type: filter]"};
}

int gitLogContentRows(int screenRows, int screenCols, bool prettyView)
{
    const int headerRows =
        1 + HeaderHelp::lineCount(gitLogHelpTokens(prettyView), screenCols);
    constexpr int footerRows = 2;
    return std::max(1, screenRows - headerRows - footerRows);
}

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
        if(text_utils::is_not_found(next))
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

std::string trim_copy(std::string_view value)
{
    size_t begin = 0;
    while(begin < value.size() &&
          std::isspace(static_cast<unsigned char>(value[begin])))
        ++begin;
    size_t end = value.size();
    while(end > begin &&
          std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    return std::string(value.substr(begin, end - begin));
}

std::string format_git_refs(std::string_view refs)
{
    std::vector<std::string> locals;
    std::vector<std::string> remotes;
    std::vector<std::string> tags;
    size_t pos = 0;
    while(pos <= refs.size())
    {
        size_t comma = refs.find(',', pos);
        std::string ref = trim_copy(text_utils::is_not_found(comma)
                                        ? refs.substr(pos)
                                        : refs.substr(pos, comma - pos));
        if(!ref.empty() && ref != "HEAD")
        {
            if(ref.rfind("HEAD -> ", 0) == 0)
                ref = ref.substr(8);

            if(ref.rfind("tag: ", 0) == 0)
                tags.push_back("<" + ref.substr(5) + ">");
            else if(text_utils::is_found(ref.find('/')))
                remotes.push_back("{" + ref + "}");
            else
                locals.push_back("[" + ref + "]");
        }
        if(text_utils::is_not_found(comma))
            break;
        pos = comma + 1;
    }

    std::string out;
    auto append_group = [&](const std::vector<std::string>& group)
    {
        for(const auto& item : group)
        {
            if(!out.empty())
                out += " ";
            out += item;
        }
    };
    append_group(locals);
    append_group(remotes);
    append_group(tags);
    return out;
}

bool has_multiple_parents(std::string_view parents)
{
    bool seenParent = false;
    for(char c : parents)
    {
        if(std::isspace(static_cast<unsigned char>(c)))
        {
            if(seenParent)
                return true;
            continue;
        }
        seenParent = true;
    }
    return false;
}

std::string render_tig_graph(std::string_view rawGraph, bool merge)
{
    (void)merge;
    std::string out(rawGraph);
    return out.empty() ? "*" : out;
}

std::string relative_age(std::string_view date)
{
    std::tm tm{};
    std::istringstream in(std::string(date.substr(0, 16)));
    in >> std::get_time(&tm, "%Y-%m-%d %H:%M");
    if(in.fail())
        return std::string(date);

    std::time_t then = std::mktime(&tm);
    if(then == -1)
        return std::string(date);

    auto now = std::chrono::system_clock::now();
    auto thenTime = std::chrono::system_clock::from_time_t(then);
    auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(now - thenTime)
            .count();
    if(seconds < 0)
        seconds = 0;

    const long long days = seconds / (60 * 60 * 24);
    if(days < 1)
        return "(today)";
    if(days == 1)
        return "(1 day)";
    if(days < 14)
        return "(" + std::to_string(days) + " days)";
    const long long weeks = days / 7;
    if(weeks == 1)
        return "(1 week)";
    if(weeks < 9)
        return "(" + std::to_string(weeks) + " weeks)";
    const long long months = days / 30;
    if(months <= 1)
        return "(1 month)";
    if(months < 24)
        return "(" + std::to_string(months) + " months)";
    const long long years = days / 365;
    return "(" + std::to_string(std::max<long long>(1, years)) + " years)";
}

std::string entry_search_text(const GitLogMode::Entry& entry)
{
    std::string refs = format_git_refs(entry.refs);
    std::string text = entry.hash + " " + entry.date + " " + entry.author +
                       " " + entry.graph + " " + refs + " " + entry.subject;
    return text;
}

void append_padded_field(std::string& out, std::string_view text, int width,
                         const std::string& normalSeq,
                         const std::string& matchSeq, std::string_view query)
{
    if(width <= 0)
        return;
    std::string clipped = slice_plain(text, 0, width);
    append_highlighted(out, clipped, query, normalSeq, matchSeq);
    int pad = width - text_utils::utf8DisplayWidth(clipped);
    if(pad > 0)
    {
        out += normalSeq;
        out.append(pad, ' ');
    }
}

void append_graph_field(std::string& out, const Theme& theme,
                        std::string_view graph, int width,
                        const std::string& selectedSeq)
{
    if(width <= 0)
        return;

    std::string clipped = slice_plain(graph, 0, width);
    int used = 0;
    for(size_t i = 0; i < clipped.size();)
    {
        int next = text_utils::nextUtf8CharStart(clipped, (int)i);
        std::string_view ch =
            std::string_view(clipped).substr(i, next - (int)i);
        if(!selectedSeq.empty())
            out += selectedSeq;
        else if(ch == "/" || ch == "\\" || ch == "*")
            out += theme.reset() + theme.uiError();
        else if(ch == "|")
            out += theme.reset() + theme.uiAccent();
        else
            out += theme.reset() + theme.uiWarning();
        out.append(ch.data(), ch.size());
        used += text_utils::utf8DisplayWidth(ch);
        i = next;
    }

    int pad = width - used;
    if(pad > 0)
    {
        out +=
            selectedSeq.empty() ? theme.reset() + theme.baseFg() : selectedSeq;
        out.append(pad, ' ');
    }
}

void append_git_log_entry_line(std::string& output, const Theme& theme,
                               const GitLogMode::Entry& entry,
                               std::string_view query, bool selected,
                               bool marked, bool inRange, int width)
{
    if(width <= 0)
        return;

    const std::string selectedSeq =
        selected ? theme.selection()
                 : (marked ? std::string(Terminal::ESC_DIM) + theme.selection()
                           : std::string{});
    auto color = [&](const std::string& seq) -> std::string
    { return selectedSeq.empty() ? theme.reset() + seq : selectedSeq; };

    const std::string& matchSeq = theme.searchMatch();
    int used = 0;
    auto append_fixed =
        [&](std::string_view text, int fieldWidth, const std::string& seq)
    {
        int actualWidth = std::min(fieldWidth, std::max(0, width - used));
        append_padded_field(output, text, actualWidth, seq, matchSeq, query);
        used += actualWidth;
    };
    auto append_text = [&](std::string_view text, const std::string& seq)
    {
        int remain = std::max(0, width - used);
        if(remain <= 0 || text.empty())
            return;
        std::string clipped = slice_plain(text, 0, remain);
        append_highlighted(output, clipped, query, seq, matchSeq);
        used += text_utils::utf8DisplayWidth(clipped);
    };

    std::string mark = marked ? "*" : (inRange ? "+" : " ");
    append_fixed(mark, 1, color(theme.baseFg()));
    std::string graph = entry.graph.empty() ? "*" : entry.graph;
    int graphWidth = std::min(10, std::max(0, width - used));
    append_graph_field(output, theme, graph, graphWidth, selectedSeq);
    used += graphWidth;
    std::string hash =
        entry.hash.substr(0, std::min<size_t>(8, entry.hash.size()));
    append_fixed(hash, 9, color(theme.uiWarning()));
    append_fixed(relative_age(entry.date), 11, color(theme.uiSuccess()));
    append_fixed("<" + entry.author + ">", 20, color(theme.uiInfo()));

    std::string refs = format_git_refs(entry.refs);
    if(!refs.empty())
    {
        append_text(refs, color(theme.uiWarning()));
        append_text(" ", color(theme.baseFg()));
    }
    append_text(entry.subject, color(theme.baseFg()));

    if(used < width)
    {
        output += color(theme.baseFg());
        output.append(width - used, ' ');
    }
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
        if(text_utils::is_not_found(refsPos))
        {
            append_colored(hashAndRefs, editor.theme.uiWarning());
        }
        else
        {
            append_colored(hashAndRefs.substr(0, refsPos),
                           editor.theme.uiWarning());
            append_colored(hashAndRefs.substr(refsPos), editor.theme.uiError());
        }
        return;
    }
    if(line.rfind("Author:", 0) == 0 || line.rfind("Commit:", 0) == 0 ||
       line.rfind("AuthorDate:", 0) == 0 || line.rfind("CommitDate:", 0) == 0 ||
       line.rfind("Date:", 0) == 0)
    {
        size_t colon = line.find(keyCode(command::CommandKey::KEY_COLON));
        if(text_utils::is_not_found(colon))
        {
            append_colored(line, editor.theme.uiAccent());
            return;
        }
        append_colored(std::string_view(line.data(), colon + 1),
                       editor.theme.uiAccent());
        if(colon + 1 < line.size())
            append_colored(std::string_view(line.data() + colon + 1,
                                            line.size() - colon - 1),
                           editor.theme.uiSuccess());
        return;
    }
    if(line.rfind("diff --git ", 0) == 0)
    {
        std::string_view prefix("diff --git ");
        append_colored(prefix, editor.theme.uiAccent());
        std::string rest = line.substr(prefix.size());
        size_t split = rest.find(keyCode(control::ControlKey::SPACE));
        if(text_utils::is_not_found(split))
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
        if(text_utils::is_not_found(space))
        {
            append_colored(line, editor.theme.uiAccent());
            return;
        }
        append_colored(std::string_view(line.data(), space + 1),
                       editor.theme.uiAccent());
        if(space + 1 < line.size())
            append_colored(std::string_view(line.data() + space + 1,
                                            line.size() - space - 1),
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
    if(text_utils::contains(line, " | "))
    {
        append_colored(line, editor.theme.uiInfo());
        return;
    }
    if(text_utils::contains(line, " changed, ") ||
       text_utils::contains(line, " insertion") ||
       text_utils::contains(line, " deletion"))
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

const char* GitLogMode::graphPrettyFormatArg()
{
    return "--pretty=format:%x1e%H%x1f%ad%x1f%an%x1f%D%x1f%P%x1f%s";
}

std::optional<GitLogMode::Entry>
GitLogMode::parseGraphEntry(std::string_view line)
{
    constexpr char recordSep = '\x1e';
    constexpr char fieldSep = '\x1f';

    size_t marker = line.find(recordSep);
    std::string graph;
    std::string_view payload = line;
    if(text_utils::is_found(marker))
    {
        graph = std::string(line.substr(0, marker));
        payload = line.substr(marker + 1);
    }

    std::vector<std::string> fields;
    size_t pos = 0;
    while(pos <= payload.size())
    {
        size_t next = payload.find(fieldSep, pos);
        if(text_utils::is_not_found(next))
        {
            fields.emplace_back(payload.substr(pos));
            break;
        }
        fields.emplace_back(payload.substr(pos, next - pos));
        pos = next + 1;
    }

    if(fields.size() < 2 || fields[0].empty())
        return std::nullopt;

    Entry entry;
    entry.hash = fields[0];
    if(fields.size() >= 6)
    {
        entry.date = fields[1];
        entry.author = fields[2];
        entry.refs = fields[3];
        entry.merge = has_multiple_parents(fields[4]);
        entry.subject = fields[5];
    }
    else if(fields.size() >= 4)
    {
        entry.date = fields[1];
        entry.author = fields[2];
        entry.subject = fields[3];
    }
    else
    {
        entry.subject = fields[1];
    }
    entry.graph = render_tig_graph(graph, entry.merge);
    return entry;
}

void GitLogMode::applyGraphConnector(Entry& entry,
                                     std::string_view connectorLine)
{
    std::string connector = trim_copy(connectorLine);
    if(connector.empty() || (text_utils::is_not_found(connector.find('/')) &&
                             text_utils::is_not_found(connector.find('\\'))))
        return;

    entry.graph = std::move(connector);
}

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
            std::string text = entry_search_text(entries[i]);
            int score =
                editor::helper::fuzzyScoreWithPositions(query, text, positions);
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
    int window =
        gitLogContentRows(editor.screenRows, editor.screenCols, prettyView);
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

    previewLines = run_git_lines(
        {"git", "-C", repoDirUse, "--no-pager", "show", "--patch", "--stat",
         editor.gitUseDefaultColors ? "--color=always" : "--no-color", hash});
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
            rangeSelectAnchor =
                std::clamp(rangeSelectAnchor, 0, (int)filtered.size() - 1);
    }
    ensurePrettyPreview(*ed);
    ctx.requestFullRedraw();
}

void GitLogMode::on_exit(ModeContext& ctx)
{
    ctx.requestFullRedraw();
}

std::optional<ModeState> GitLogMode::handle(ModeContext& ctx,
                                            const ModeKeyEvent& event)
{
    const int key = event.key;
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
        int window =
            gitLogContentRows(ed->screenRows, ed->screenCols, prettyView);
        int maxScroll = std::max(0, (int)filtered.size() - window);
        scrollOffset = std::clamp(scrollOffset, 0, maxScroll);
        if(rangeSelectActive)
            rangeSelectAnchor =
                std::clamp(rangeSelectAnchor, 0, (int)filtered.size() - 1);
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
        rangeSelectAnchor =
            std::clamp(rangeSelectAnchor, 0, (int)filtered.size() - 1);
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
                std::string text = entry_search_text(entries[idx]);
                if(text_utils::contains(text, searchQuery))
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
                    std::string text = entry_search_text(entries[idx]);
                    if(text_utils::contains(text, searchQuery))
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
                std::string text = entry_search_text(entries[idx]);
                if(text_utils::contains(text, searchQuery))
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
                    std::string text = entry_search_text(entries[idx]);
                    if(text_utils::contains(text, searchQuery))
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
            int window =
                gitLogContentRows(ed->screenRows, ed->screenCols, prettyView);
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
                        std::string text = entry_search_text(entries[idx]);
                        if(text_utils::contains(text, searchQuery))
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
                        std::string text = entry_search_text(entries[idx]);
                        if(text_utils::contains(text, searchQuery))
                        {
                            found = i;
                            break;
                        }
                    }
                }
                if(found >= 0)
                {
                    cursor = found;
                    int window = gitLogContentRows(ed->screenRows,
                                                   ed->screenCols, prettyView);
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

    if(c == keyCode(control::ControlKey::ESC) && rangeSelectActive)
    {
        rangeSelectActive = false;
        rangeSelectBase.clear();
        ctx.requestFullRedraw();
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(typed::TypedKey::KEY_Q))
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

    int window = gitLogContentRows(ed->screenRows, ed->screenCols, prettyView);
    int maxScroll = std::max(0, (int)filtered.size() - window);

    if(prettyView && c == keyCode(control::ControlKey::CTRL_J))
    {
        int maxDiffScroll =
            std::max(0, (int)previewLines.size() -
                            gitLogContentRows(ed->screenRows, ed->screenCols,
                                              prettyView));
        if(diffOffset < maxDiffScroll)
            diffOffset++;
    }
    else if(prettyView && c == keyCode(control::ControlKey::CTRL_K))
    {
        if(diffOffset > 0)
            diffOffset--;
    }
    else if(c == keyCode(navigation::NavigationKey::ARROW_DOWN) ||
            c == keyCode(typed::TypedKey::KEY_J) ||
            (!prettyView && c == keyCode(control::ControlKey::CTRL_J)))
    {
        if(cursor + 1 < (int)filtered.size())
        {
            cursor++;
            if(cursor >= scrollOffset + window)
                scrollOffset = cursor - window + 1;
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_K) ||
            c == keyCode(navigation::NavigationKey::ARROW_UP) ||
            c == keyCode(typed::TypedKey::KEY_K))
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
    else if(c == keyCode(control::ControlKey::CTRL_V))
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
    else if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 ||
            c == keyCode(control::ControlKey::CTRL_H))
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
    else if(c == keyCode(command::CommandKey::KEY_SLASH) ||
            c == keyCode(command::CommandKey::KEY_QUESTION))
    {
        searchActive = true;
        searchForward = (c == keyCode(command::CommandKey::KEY_SLASH));
        searchQuery.clear();
        searchPrevCursor = cursor;
        searchPrevScroll = scrollOffset;
        ed->lastEscTime = std::chrono::steady_clock::time_point();
    }
    else if((c == keyCode(typed::TypedKey::KEY_N) ||
             c == keyCode(typed::TypedKey::KEY_CAP_N)) &&
            !searchQuery.empty())
    {
        bool forward = (c == keyCode(typed::TypedKey::KEY_N)) ? searchForward
                                                              : !searchForward;
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
        auto sinceMove = std::chrono::duration_cast<std::chrono::milliseconds>(
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
        self->cursor =
            std::clamp(self->cursor, 0, (int)self->filtered.size() - 1);
        int window = gitLogContentRows(editor.screenRows, editor.screenCols,
                                       self->prettyView);
        int maxScroll = std::max(0, (int)self->filtered.size() - window);
        self->scrollOffset = std::clamp(self->scrollOffset, 0, maxScroll);
        if(self->rangeSelectActive)
            self->rangeSelectAnchor = std::clamp(
                self->rangeSelectAnchor, 0, (int)self->filtered.size() - 1);
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
    HeaderHelp::append(output, editor.theme, editor.screenCols,
                       gitLogHelpTokens(prettyView));

    int availableRows =
        gitLogContentRows(editor.screenRows, editor.screenCols, prettyView);
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
                    std::string leftTrim =
                        slice_plain(leftText, 0, leftContentWidth);
                    int leftDisplay = text_utils::utf8DisplayWidth(leftTrim);
                    if(leftDisplay < leftContentWidth)
                        leftTrim.append(leftContentWidth - leftDisplay,
                                        keyCode(control::ControlKey::SPACE));
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
                    auto append_part =
                        [&](const std::string& text, const std::string& color)
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
                        append_part(entry.author + " ",
                                    editor.theme.uiAccent());
                    append_part(entry.subject, editor.theme.baseFg());

                    if(usedWidth < leftContentWidth)
                        output.append(leftContentWidth - usedWidth,
                                      keyCode(control::ControlKey::SPACE));
                    output += editor.theme.reset();
                }
            }
            else
            {
                int leftContentWidth = std::max(1, leftWidth - 2);
                output += editor.theme.uiGutter();
                output += " ~";
                if(leftContentWidth > 1)
                    output.append(leftContentWidth - 1,
                                  keyCode(control::ControlKey::SPACE));
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
                    std::string sliced =
                        slice_plain(diffLine, diffHorizontalOffset,
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
                append_git_log_entry_line(
                    output, editor.theme, entry, searchQuery, selected, marked,
                    inRange, std::max(0, editor.screenCols - 2));
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
    std::string right = " " +
                        std::to_string(filtered.empty() ? 0 : cursor + 1) +
                        "/" + std::to_string(filtered.size());
    right += " | sel " + std::to_string(selectedHashes.size());
    if(prettyView)
    {
        int maxDiffScroll =
            std::max(0, (int)previewLines.size() -
                            gitLogContentRows(editor.screenRows,
                                              editor.screenCols, prettyView));
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
        std::string shortHash =
            hash.substr(0, std::min<size_t>(12, hash.size()));
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
    append_git_log_entry_line(output, theme, entry, query, selected, false,
                              false, std::max(0, screenCols));
    output += theme.reset();
    return output;
}
#endif
} // namespace editor::statemachine
