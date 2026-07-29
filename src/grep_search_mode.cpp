#include "ascii.h"
#include "color_constant.h"
#include "constants.h"
#include "editor.h"
#include "editor_utils.h"
#include "gitignore.h"
#include "header_help.h"
#include "mode_state_machine.h"
#include "process_pipe.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits.h>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace editor::statemachine
{
namespace
{
std::vector<std::string> grepSearchHelpTokens()
{
    return {"[Enter: open]", "[Esc: cancel]", "[Ctrl+N: select]",
            "[" + ascii::utf8(ascii::UP_DOWN_ARROWS) + ": navigate]",
            "[Ctrl+I: gitignore]"};
}

int grepSearchHeaderRows(int screenCols)
{
    return 2 + HeaderHelp::lineCount(grepSearchHelpTokens(), screenCols);
}

int grepSearchVisibleRows(const Editor& editor)
{
    return std::max(1, editor.screenRows -
                           grepSearchHeaderRows(editor.screenCols));
}

std::string toLower(std::string_view input)
{
    std::string out(input);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string runCmd(const std::vector<std::string>& args)
{
    ProcessPipe pipe(args);
    if(!pipe)
        return {};
    return pipe.readAll();
}

bool ripgrepAvailable()
{
    static const bool available = [] {
        return !runCmd({"rg", "--version"}).empty();
    }();
    return available;
}

bool isRgCachePath(std::string_view path)
{
    return path == ".rg" || path.rfind(".rg/", 0) == 0 ||
           path.rfind(".rg\\", 0) == 0 ||
           text_utils::is_found(path.find("/.rg/")) ||
           text_utils::is_found(path.find("\\.rg\\"));
}

std::vector<std::string> splitNul(const std::string& s)
{
    std::vector<std::string> out;
    size_t i = 0;
    while(i < s.size())
    {
        size_t j = s.find('\0', i);
        if(text_utils::is_not_found(j))
            j = s.size();
        if(j > i)
            out.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

std::string truncatePathMiddle(std::string path, int width)
{
    if(width <= 0)
        return "";
    if(text_utils::utf8DisplayWidth(path) <= width)
        return path;
    if(width <= 3)
        return std::string(width, '.');

    const std::string prefix = "...";
    const int suffixWidth = width - (int)prefix.size();
    std::string suffix = path;
    while(!suffix.empty() && text_utils::utf8DisplayWidth(suffix) > suffixWidth)
    {
        size_t slash = suffix.find('/');
        if(text_utils::is_not_found(slash) || slash + 1 >= suffix.size())
            suffix.erase(suffix.begin());
        else
            suffix.erase(0, slash + 1);
    }
    return prefix + suffix;
}

bool isAsciiText(std::string_view text)
{
    return std::all_of(text.begin(), text.end(), [](unsigned char ch)
                       { return ch < 0x80; });
}

std::string truncateDisplayText(std::string_view text, int width)
{
    if(width <= 0)
        return "";

    if(isAsciiText(text))
    {
        if((int)text.size() <= width)
            return std::string(text);
        if(width <= 3)
            return std::string(width, '.');
        std::string out(text.substr(0, width - 3));
        out += "...";
        return out;
    }

    if(text_utils::utf8DisplayWidth(text) <= width)
        return std::string(text);
    if(width <= 3)
        return std::string(width, '.');

    const int targetWidth = width - 3;
    std::string out;
    out.reserve(std::min<size_t>(text.size(), (size_t)width));

    std::mbstate_t state{};
    const char* cursor = text.data();
    size_t remaining = text.size();
    int outWidth = 0;

    while(remaining > 0)
    {
        wchar_t wc = 0;
        size_t n = std::mbrtowc(&wc, cursor, remaining, &state);
        if(n == 0)
            break;
        if(n == static_cast<size_t>(-1) || n == static_cast<size_t>(-2))
        {
            n = 1;
            std::mbstate_t reset{};
            state = reset;
        }

        const int charWidth =
            text_utils::utf8DisplayWidth(std::string_view(cursor, n));
        if(outWidth + charWidth > targetWidth)
            break;

        out.append(cursor, n);
        outWidth += charWidth;
        cursor += n;
        remaining -= n;
    }

    out += "...";
    return out;
}

std::string singleLinePasteText(std::string text)
{
    text.erase(std::remove_if(text.begin(), text.end(),
                              [](char ch) { return ch == '\n' || ch == '\r'; }),
               text.end());
    return text;
}

#ifdef UVIM_ENABLE_RG_CACHE
struct RgCacheMeta
{
    uintmax_t size = 0;
    long long mtime = 0;
    std::string cacheName;
};

std::string fnv1aHex(std::string_view input)
{
    uint64_t hash = 1469598103934665603ull;
    for(unsigned char c : input)
    {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << hash;
    return out.str();
}

long long fileMtimeCount(const std::filesystem::path& path)
{
    std::error_code ec;
    auto time = std::filesystem::last_write_time(path, ec);
    if(ec)
        return 0;
    return static_cast<long long>(time.time_since_epoch().count());
}

std::filesystem::path rgCacheRoot()
{
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    if(ec)
        return {};
    return cwd / ".rg";
}

std::unordered_map<std::string, RgCacheMeta>
loadRgCacheIndex(const std::filesystem::path& indexPath)
{
    std::unordered_map<std::string, RgCacheMeta> out;
    std::ifstream file(indexPath);
    if(!file)
        return out;

    std::string line;
    while(std::getline(file, line))
    {
        std::stringstream ss(line);
        std::string path;
        std::string sizeText;
        std::string mtimeText;
        std::string cacheName;
        if(!std::getline(ss, path, '\t') || !std::getline(ss, sizeText, '\t') ||
           !std::getline(ss, mtimeText, '\t') ||
           !std::getline(ss, cacheName, '\t'))
        {
            continue;
        }
        try
        {
            RgCacheMeta meta;
            meta.size = static_cast<uintmax_t>(std::stoull(sizeText));
            meta.mtime = std::stoll(mtimeText);
            meta.cacheName = cacheName;
            out[path] = std::move(meta);
        }
        catch(...)
        {
        }
    }
    return out;
}

bool readCachedLines(const std::filesystem::path& path,
                     std::vector<std::string>& lines)
{
    std::ifstream file(path);
    if(!file)
        return false;
    lines.clear();
    std::string line;
    while(std::getline(file, line))
        lines.push_back(line);
    return true;
}

bool copyFileToCache(const std::filesystem::path& source,
                     const std::filesystem::path& target)
{
    std::error_code ec;
    std::filesystem::copy_file(
        source, target, std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

std::vector<std::string> makeLowerLines(const std::vector<std::string>& lines)
{
    std::vector<std::string> lowered;
    lowered.reserve(lines.size());
    for(const auto& line : lines)
        lowered.push_back(toLower(line));
    return lowered;
}

void addRgCacheLineTokens(Editor& editor, std::string_view line, int fileIndex,
                          int lineIndex)
{
    std::unordered_set<std::string> seen;
    for(size_t i = 0; i < line.size(); ++i)
    {
        const size_t maxLen = std::min<size_t>(3, line.size() - i);
        for(size_t len = 1; len <= maxLen; ++len)
        {
            std::string token(line.substr(i, len));
            if(seen.insert(token).second)
            {
                editor.rgCacheLineIndex[std::move(token)].push_back(
                    Editor::RgCacheLineRef{fileIndex, lineIndex});
            }
        }
    }
}

void rebuildRgCacheLineIndex(Editor& editor)
{
    editor.rgCacheLineIndex.clear();
    for(size_t fileIndex = 0; fileIndex < editor.rgCachedFiles.size();
        ++fileIndex)
    {
        const auto& cached = editor.rgCachedFiles[fileIndex];
        for(size_t lineIndex = 0; lineIndex < cached.lowerLines.size();
            ++lineIndex)
        {
            addRgCacheLineTokens(editor, cached.lowerLines[lineIndex],
                                 static_cast<int>(fileIndex),
                                 static_cast<int>(lineIndex));
        }
    }
}

std::string rgCacheLookupToken(const Editor& editor, std::string_view needle)
{
    if(needle.size() <= 3)
        return std::string(needle);

    std::string best(needle.substr(0, 3));
    size_t bestSize = std::numeric_limits<size_t>::max();
    for(size_t i = 0; i + 3 <= needle.size(); ++i)
    {
        std::string token(needle.substr(i, 3));
        auto it = editor.rgCacheLineIndex.find(token);
        const size_t size =
            it == editor.rgCacheLineIndex.end() ? 0 : it->second.size();
        if(size < bestSize)
        {
            best = std::move(token);
            bestSize = size;
            if(bestSize == 0)
                break;
        }
    }
    return best;
}

void clearGrepCachedQueryState(GrepSearchMode& mode)
{
    mode.lastCachedQuery.clear();
    mode.lastCachedMatches.clear();
}
#endif

void seedEditorSearchFromGrepMatch(Editor& editor, const GrepMatch& match,
                                   std::string_view query)
{
    editor.searchQuery = std::string(query);
    editor.searchRegexError = false;
    editor.searchMatches.clear();
    editor.searchMatchesPartial = true;
    editor.currentMatchIndex = -1;

    if(!editor.lines || match.lineNumber <= 0)
        return;

    const int row = match.lineNumber - 1;
    if(row < 0 || row >= (int)editor.lines->size())
        return;

    const std::string& line = (*editor.lines)[row];
    int col = -1;
    if(!query.empty())
    {
        std::string lowerLine = toLower(line);
        std::string lowerQuery = toLower(query);
        size_t found = lowerLine.find(lowerQuery);
        if(text_utils::is_found(found))
            col = static_cast<int>(found);
    }
    if(col < 0 && !match.highlightRanges.empty())
        col = std::max(0, match.highlightRanges.front().first);
    if(col < 0)
        col = 0;

    SearchMatch seeded;
    seeded.row = row;
    seeded.col = std::min(col, (int)line.size());
    seeded.len = std::max<int>(1, query.size());
    editor.searchMatches.push_back(seeded);
    editor.currentMatchIndex = 0;
}
} // namespace

// ============================================================================
// GrepSearchMode Implementation
// ============================================================================

void GrepSearchMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    initialize(*ed);
    ed->needsFullRedraw = true;

    // Set cursor to bar for input
    Terminal::setCursorBarBlinking();
#ifdef UVIM_ENABLE_RG_CACHE
    if(ed->rgCacheEnabled)
    {
        draw(*ed);
        syncRgCache(*ed, false);
    }
#endif
}

void GrepSearchMode::on_exit(ModeContext& /* ctx */)
{
    // Restore block cursor
    Terminal::setCursorBlock();
}

std::optional<ModeState> GrepSearchMode::handle(ModeContext& ctx,
                                                const ModeKeyEvent& event)
{
    const int key = event.key;
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    // ========================================================================
    // Exit
    // ========================================================================

    if(c == keyCode(control::ControlKey::ESC))
    {
        return defaultExitMode(ed);
    }

    // ========================================================================
    // Selection
    // ========================================================================

    if(c == keyCode(control::ControlKey::ENTER))
    {
        flushPendingSearch(*ed);
        if(!selectedMatches.empty() ? openSelected(*ed) : selectMatch(*ed))
        {
            return defaultExitMode(ed);
        }
        return std::nullopt;
    }

    // ========================================================================
    // Navigation through results
    // ========================================================================

    if(c == keyCode(control::ControlKey::CTRL_N))
    {
        toggleSelection();
    }
    else if(c == keyCode(control::ControlKey::CTRL_J) ||
            c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        resultDown(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_P) ||
            c == keyCode(control::ControlKey::CTRL_K) ||
            c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        resultUp(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_D) ||
            c == keyCode(navigation::NavigationKey::PAGE_DOWN))
    {
        resultHalfPageDown(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_U) ||
            c == keyCode(navigation::NavigationKey::PAGE_UP))
    {
        resultHalfPageUp(*ed);
    }

    // ========================================================================
    // Input Editing
    // ========================================================================

    else if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 ||
            c == keyCode(control::ControlKey::CTRL_H))
    {
        searchBackspace(*ed);
    }
    else if(c == keyCode(control::ControlKey::CTRL_W))
    {
        // Delete word backward
        searchDeleteWord(*ed);
    }
    else if(c == keyCode(control::ControlKey::PASTE))
    {
        std::string text = singleLinePasteText(Terminal::takeLastPasteText());
        if(!text.empty())
        {
            query += text;
            scheduleSearch(*ed);
            cursor = 0;
            offset = 0;
        }
    }
    // ========================================================================
    // Toggles
    // ========================================================================

    else if(c == keyCode(control::ControlKey::CTRL_I))
    {
        toggleGitignore(*ed);
    }
    else if(c == keyCode(control::ControlKey::TAB))
    {
        togglePreview();
    }

    // ========================================================================
    // Mode Switching
    // ========================================================================

    else if(c == keyCode(control::ControlKey::CTRL_B))
    {
        return BufferBrowserMode{};
    }

    // ========================================================================
    // Character Input
    // ========================================================================

    else if(c >= 32 && c < 127)
    {
        searchAddChar(*ed, static_cast<char>(c));
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}

void GrepSearchMode::draw(Editor& editor)
{
    processIdle(editor);

    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;
    output += editor.theme.reset();
    output += Terminal::ESC_CLEAR_LINE;

    output += Terminal::ESC_BOLD;
    output += "  Grep: ";
    output += editor.theme.reset();
    output += editor.theme.uiPrompt();
    output += query;

    output += Terminal::ESC_BLINK;
    output += "_";
    output += Terminal::ESC_BLINK_OFF;
    output += editor.theme.baseFg();

    if(searchPending)
    {
        output += editor.theme.uiWarning();
        output += " (typing...)";
        output += editor.theme.baseFg();
    }
    else if(searching)
    {
        output += editor.theme.uiWarning();
        output += " (searching...)";
        output += editor.theme.baseFg();
    }

    HeaderHelp::append(output, editor.theme, editor.screenCols,
                       grepSearchHelpTokens());

    output += Terminal::NEWLINE_CLEAR;
    output += editor.theme.uiDim();
    if(!matches.empty())
    {
        output += "  " + std::to_string(matches.size());
        if(matches.size() >= 1000)
            output += "+ matches (limited)";
        else
            output += " matches";
    }
    else if(!query.empty() && !searching)
    {
        output += "  No matches";
    }
    if(editor.respectGitignore)
    {
        output += " [gitignore]";
    }
#ifdef UVIM_ENABLE_RG_CACHE
    if(editor.rgCacheEnabled)
    {
        output += " [rgcache ";
        if(rgCacheIndexing)
        {
            const int percent =
                rgCacheTotal > 0 ? (rgCacheIndexed * 100) / rgCacheTotal : 0;
            output += "indexing " + std::to_string(percent) + "%";
            output += " active jobs " + std::to_string(rgCacheJobs);
            if(rgCacheTotal > 0)
            {
                output += " files " + std::to_string(rgCacheIndexed) + "/" +
                          std::to_string(rgCacheTotal);
            }
            else
            {
                output += " discovering";
            }
            if(rgCacheUpdated > 0)
                output += " updated " + std::to_string(rgCacheUpdated);
            if(rgCacheRemoved > 0)
                output += " removed " + std::to_string(rgCacheRemoved);
        }
        else
        {
            output += "idle active jobs 0 indexed files " +
                      std::to_string(editor.rgCachedFiles.size());
            if(rgCacheTotal > 0)
                output += "/" + std::to_string(rgCacheTotal);
        }
        output += "]";
    }
#endif
    if(!selectedMatches.empty())
    {
        output += " (" + std::to_string(selectedMatches.size()) + " selected)";
    }
    output += editor.theme.baseFg();

    int availableRows = grepSearchVisibleRows(editor);
    const std::string lowerQuery = toLower(query);

    std::string cwdPrefix;
    std::error_code cwdEc;
    auto cwd = std::filesystem::current_path(cwdEc);
    if(!cwdEc)
    {
        cwdPrefix = cwd.string();
        if(!cwdPrefix.empty() && cwdPrefix.back() != '/' &&
           cwdPrefix.back() != '\\')
        {
            cwdPrefix += std::filesystem::path::preferred_separator;
        }
    }

    for(int i = 0; i < availableRows && i + offset < (int)matches.size(); i++)
    {
        output += Terminal::NEWLINE_CLEAR;

        int index = i + offset;
        const GrepMatch& match = matches[index];
        bool isSelected = selectedMatches.count(index) > 0;

        if(index == cursor && isSelected)
        {
            output += color::rgbBg(56, 120, 72);
            output += editor.theme.baseFg();
        }
        else if(index == cursor)
        {
            output += editor.theme.selection();
        }
        else if(isSelected)
        {
            output += color::rgbBg(24, 64, 36);
            output += editor.theme.baseFg();
        }

        output += "  ";

        output += editor.theme.uiInfo();
        std::string displayName = match.filepath;
        if(!cwdPrefix.empty() && displayName.rfind(cwdPrefix, 0) == 0)
            displayName = displayName.substr(cwdPrefix.length());

        const std::string lineNumber = std::to_string(match.lineNumber);
        const int rowPrefixWidth = 2;
        const int separatorsWidth = 3; // ":" + ": "
        const int minContentWidth =
            std::min(20, std::max(0, editor.screenCols / 3));
        int pathWidth = editor.screenCols - rowPrefixWidth - separatorsWidth -
                        (int)lineNumber.length() - minContentWidth;
        if(pathWidth < 8)
            pathWidth =
                std::max(1, editor.screenCols - rowPrefixWidth -
                                separatorsWidth - (int)lineNumber.length());

        displayName = truncatePathMiddle(displayName, pathWidth);
        const int displayNameWidth = text_utils::utf8DisplayWidth(displayName);
        output += displayName;
        output += editor.theme.baseFg();

        output += ":";
        output += editor.theme.uiWarning();
        output += lineNumber;
        output += editor.theme.baseFg();
        output += ": ";

        int maxContentLen = editor.screenCols - rowPrefixWidth -
                            displayNameWidth - separatorsWidth -
                            (int)lineNumber.length();
        if(maxContentLen < 0)
            maxContentLen = 0;

        std::string content =
            truncateDisplayText(match.lineContent, maxContentLen);

        if(!match.highlightRanges.empty() && index != cursor)
        {
            std::string lowerContent = toLower(content);

            size_t pos = lowerContent.find(lowerQuery);
            if(text_utils::is_found(pos))
            {
                output += content.substr(0, pos);
                output += editor.theme.matchHighlight();
                output += content.substr(pos, query.length());
                output += editor.theme.baseFg();
                output += content.substr(pos + query.length());
            }
            else
            {
                output += content;
            }
        }
        else
        {
            output += content;
        }

        output += editor.theme.reset();
    }

    for(int i = (int)matches.size() - offset; i < availableRows; i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        output += editor.theme.uiGutter();
        output += "~";
        output += editor.theme.baseFg();
    }

    Terminal::write(output);
    Terminal::flush();
}

bool GrepSearchMode::processIdle(Editor& editor)
{
    if(!searchPending)
        return false;

    const auto now = std::chrono::steady_clock::now();
    if(now < searchDueAt)
        return false;

    flushPendingSearch(editor);
    return true;
}

void GrepSearchMode::loadFileIndex(Editor& editor)
{
    if(!editor.grepFileIndexInitialized)
    {
        editor.grepProjectFiles.clear();
        std::error_code cwdEc;
        auto cwd = std::filesystem::current_path(cwdEc);
        if(!cwdEc)
        {
            const std::string cwdStr = cwd.string();
            if(editor.useGitFileIndex)
            {
                std::string repoRoot = runCmd(
                    {"git", "-C", cwdStr, "rev-parse", "--show-toplevel"});

                if(!repoRoot.empty())
                {
                    const std::string raw =
                        runCmd({"git", "-C", cwdStr, "ls-files", "-z",
                                "--cached", "--others", "--exclude-standard"});
                    const auto relPaths = splitNul(raw);

                    for(const auto& relPath : relPaths)
                    {
                        if(relPath.empty())
                            continue;
                        if(isRgCachePath(relPath))
                            continue;

                        const std::string fullPath = cwdStr + "/" + relPath;
                        std::error_code stEc;
                        auto status = std::filesystem::status(fullPath, stEc);
                        if(stEc)
                            continue;
                        if(std::filesystem::is_directory(status))
                            continue;

                        FileEntry entry;
                        std::filesystem::path fullFs(fullPath);
                        entry.name = fullFs.filename().string();
                        entry.path = relPath;
                        entry.isDirectory = false;
                        std::error_code szEc;
                        entry.size = (uintmax_t)std::filesystem::file_size(
                            fullPath, szEc);
                        if(szEc)
                            entry.size = 0;
                        std::error_code mtEc;
                        auto ftime =
                            std::filesystem::last_write_time(fullPath, mtEc);
                        if(!mtEc)
                        {
                            using namespace std::chrono;
                            auto sctp = time_point_cast<system_clock::duration>(
                                ftime - decltype(ftime)::clock::now() +
                                system_clock::now());
                            entry.modTime = system_clock::to_time_t(sctp);
                        }
                        editor.grepProjectFiles.push_back(std::move(entry));
                    }
                }
            }

            if(editor.grepProjectFiles.empty())
            {
                GitIgnore gitignore;
                if(editor.respectGitignore)
                {
                    gitignore.loadRecursive(cwd);
                }
                editor::helper::collectProjectFileEntries(
                    cwdStr, 0, gitignore, editor.grepProjectFiles);
                editor.grepProjectFiles.erase(
                    std::remove_if(editor.grepProjectFiles.begin(),
                                   editor.grepProjectFiles.end(),
                                   [](const FileEntry& entry)
                                   { return isRgCachePath(entry.path); }),
                    editor.grepProjectFiles.end());
            }
        }
        editor.grepFileIndexInitialized = true;
    }
}

void GrepSearchMode::initialize(Editor& editor)
{
    (void)editor;
    searchClear();
    searching = false;
}

void GrepSearchMode::refreshFileIndex(Editor& editor)
{
    editor.grepFileIndexInitialized = false;
    editor.grepProjectFiles.clear();
#ifdef UVIM_ENABLE_RG_CACHE
    editor.rgCacheLoaded = false;
    editor.rgCachedFiles.clear();
    editor.rgCacheLineIndex.clear();
    clearGrepCachedQueryState(*this);
    lastRgCacheIndexRefresh = {};
#endif
    loadFileIndex(editor);
    cursor = 0;
    offset = 0;
    selectedMatches.clear();
    if(query.empty())
    {
        matches.clear();
        searching = false;
        return;
    }
    performSearch(editor);
}

void GrepSearchMode::performSearch(Editor& editor)
{
    searchPending = false;
    matches.clear();
    selectedMatches.clear();
    searching = true;

    if(query.empty())
    {
        searching = false;
        return;
    }

#ifdef UVIM_ENABLE_RG_CACHE
    if(performCachedSearch(editor))
    {
        searching = false;
        if(cursor >= (int)matches.size())
        {
            cursor = 0;
            offset = 0;
        }
        prewarmAroundCursor(editor);
        return;
    }
#endif

    if(performRipgrepSearch(editor))
    {
        searching = false;
        if(cursor >= (int)matches.size())
        {
            cursor = 0;
            offset = 0;
        }
        prewarmAroundCursor(editor);
        return;
    }

    loadFileIndex(editor);
    for(const auto& file : editor.grepProjectFiles)
    {
        if(file.isDirectory)
            continue;
        if(isRgCachePath(file.path))
            continue;

        searchInFile(file.path, query);

        if(matches.size() >= 1000)
            break;
    }

    searching = false;

    if(cursor >= (int)matches.size())
    {
        cursor = 0;
        offset = 0;
    }
    prewarmAroundCursor(editor);
}

void GrepSearchMode::scheduleSearch(Editor& editor)
{
    if(query.empty())
    {
        performSearch(editor);
        editor.needsFullRedraw = true;
        return;
    }
    searchPending = true;
    searching = false;
    searchDueAt = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(std::max(0, editor.rgUpdateMs));
    editor.needsFullRedraw = true;
}

void GrepSearchMode::flushPendingSearch(Editor& editor)
{
    if(!searchPending)
        return;
    performSearch(editor);
    cursor = 0;
    offset = 0;
    prewarmAroundCursor(editor);
    editor.needsFullRedraw = true;
}

#ifdef UVIM_ENABLE_RG_CACHE
bool GrepSearchMode::syncRgCache(Editor& editor, bool force)
{
    if(!editor.rgCacheEnabled)
        return false;

    auto cacheRoot = rgCacheRoot();
    if(cacheRoot.empty())
        return false;
    const auto filesDir = cacheRoot / "files";
    const auto indexPath = cacheRoot / "index.tsv";

    const bool refreshFileIndex =
        force || !editor.grepFileIndexInitialized || !editor.rgCacheLoaded;

    if(editor.rgCacheLoaded && !refreshFileIndex)
        return true;

    rgCacheIndexing = true;
    rgCacheIndexed = 0;
    rgCacheTotal = 0;
    rgCacheUpdated = 0;
    rgCacheRemoved = 0;
    rgCacheJobs = 1;
    draw(editor);
    Terminal::flush();

    if(refreshFileIndex)
    {
        editor.grepFileIndexInitialized = false;
        editor.grepProjectFiles.clear();
        lastRgCacheIndexRefresh = std::chrono::steady_clock::now();
    }
    loadFileIndex(editor);

    std::error_code ec;
    std::filesystem::create_directories(filesDir, ec);
    if(ec)
        return false;

    auto oldIndex = loadRgCacheIndex(indexPath);
    std::unordered_map<std::string, const Editor::RgCachedFile*> existing;
    for(const auto& cached : editor.rgCachedFiles)
        existing[cached.path] = &cached;

    std::vector<Editor::RgCachedFile> cachedFiles;
    std::vector<std::pair<std::string, RgCacheMeta>> newIndex;
    std::unordered_set<std::string> liveCacheNames;

    struct PendingFile
    {
        std::string path;
        std::filesystem::path sourcePath;
        std::filesystem::path cacheFile;
        uintmax_t size = 0;
        long long mtime = 0;
        std::string cacheName;
    };

    std::vector<PendingFile> pendingFiles;
    for(const auto& file : editor.grepProjectFiles)
    {
        if(file.isDirectory || isRgCachePath(file.path) || !isTextFile(file.path))
            continue;

        const std::filesystem::path displayPath(file.path);
        std::filesystem::path sourcePath = displayPath;
        if(sourcePath.is_relative())
            sourcePath = std::filesystem::current_path() / sourcePath;

        std::error_code stEc;
        const auto status = std::filesystem::status(sourcePath, stEc);
        if(stEc || !std::filesystem::is_regular_file(status))
            continue;

        std::error_code sizeEc;
        const uintmax_t size = std::filesystem::file_size(sourcePath, sizeEc);
        if(sizeEc)
            continue;
        const long long mtime = fileMtimeCount(sourcePath);
        const std::string cacheName = fnv1aHex(file.path) + ".txt";
        const std::filesystem::path cacheFile = filesDir / cacheName;

        pendingFiles.push_back(
            PendingFile{file.path, sourcePath, cacheFile, size, mtime, cacheName});
    }

    rgCacheIndexed = 0;
    rgCacheTotal = static_cast<int>(pendingFiles.size());
    rgCacheJobs = rgCacheTotal > 0 ? 1 : 0;
    draw(editor);
    Terminal::flush();
    auto lastProgressDraw = std::chrono::steady_clock::now();

    for(const auto& pending : pendingFiles)
    {
        rgCacheIndexed++;

        Editor::RgCachedFile cached;
        cached.path = pending.path;
        cached.size = pending.size;
        cached.mtime = pending.mtime;

        bool loaded = false;
        auto current = existing.find(pending.path);
        if(current != existing.end() && current->second->size == pending.size &&
           current->second->mtime == pending.mtime)
        {
            cached.lines = current->second->lines;
            cached.lowerLines = current->second->lowerLines;
            loaded = true;
        }

        auto old = oldIndex.find(pending.path);
        if(!loaded && old != oldIndex.end() && old->second.size == pending.size &&
           old->second.mtime == pending.mtime &&
           old->second.cacheName == pending.cacheName)
        {
            loaded = readCachedLines(pending.cacheFile, cached.lines);
        }

        if(!loaded)
        {
            if(!readCachedLines(pending.sourcePath, cached.lines))
                continue;
            copyFileToCache(pending.sourcePath, pending.cacheFile);
            rgCacheUpdated++;
        }
        if(cached.lowerLines.size() != cached.lines.size())
            cached.lowerLines = makeLowerLines(cached.lines);

        RgCacheMeta meta;
        meta.size = pending.size;
        meta.mtime = pending.mtime;
        meta.cacheName = pending.cacheName;
        newIndex.emplace_back(pending.path, meta);
        liveCacheNames.insert(pending.cacheName);
        cachedFiles.push_back(std::move(cached));

        const auto drawNow = std::chrono::steady_clock::now();
        if(rgCacheIndexed == rgCacheTotal ||
           drawNow - lastProgressDraw >= std::chrono::seconds(1))
        {
            draw(editor);
            Terminal::flush();
            lastProgressDraw = drawNow;
        }
    }

    std::ofstream indexOut(indexPath, std::ios::trunc);
    if(indexOut)
    {
        for(const auto& [path, meta] : newIndex)
        {
            indexOut << path << '\t' << meta.size << '\t' << meta.mtime << '\t'
                     << meta.cacheName << '\n';
        }
    }

    std::error_code iterEc;
    for(const auto& entry : std::filesystem::directory_iterator(filesDir, iterEc))
    {
        if(iterEc)
            break;
        if(!entry.is_regular_file())
            continue;
        const std::string name = entry.path().filename().string();
        if(liveCacheNames.find(name) == liveCacheNames.end())
        {
            std::error_code removeEc;
            std::filesystem::remove(entry.path(), removeEc);
            if(!removeEc)
                rgCacheRemoved++;
        }
    }

    editor.rgCachedFiles = std::move(cachedFiles);
    rebuildRgCacheLineIndex(editor);
    editor.rgCacheLoaded = true;
    rgCacheIndexing = false;
    rgCacheJobs = 0;
    draw(editor);
    Terminal::flush();

    return true;
}

bool GrepSearchMode::performCachedSearch(Editor& editor)
{
    if(!syncRgCache(editor, false))
        return false;

    if(query.size() < 3)
    {
        matches.clear();
        lastCachedQuery = query;
        lastCachedCaseSensitive = caseSensitive;
        lastCachedMatches.clear();
        return true;
    }

    std::string loweredNeedle;
    std::string_view searchNeedle = query;
    if(!caseSensitive)
    {
        loweredNeedle = toLower(query);
        searchNeedle = loweredNeedle;
    }

    if(lastCachedCaseSensitive == caseSensitive &&
       query.size() > lastCachedQuery.size() &&
       query.rfind(lastCachedQuery, 0) == 0 && !lastCachedMatches.empty())
    {
        std::vector<GrepMatch> filtered;
        filtered.reserve(std::min<size_t>(lastCachedMatches.size(), 1000));
        for(auto match : lastCachedMatches)
        {
            std::string haystack =
                caseSensitive ? match.lineContent : toLower(match.lineContent);
            const size_t pos = haystack.find(searchNeedle);
            if(text_utils::is_not_found(pos))
                continue;

            match.highlightRanges.clear();
            match.highlightRanges.push_back(
                std::make_pair(static_cast<int>(pos), (int)query.length()));
            filtered.push_back(std::move(match));
            if(filtered.size() >= 1000)
                break;
        }
        matches = std::move(filtered);
        lastCachedQuery = query;
        lastCachedCaseSensitive = caseSensitive;
        lastCachedMatches = matches;
        return true;
    }

    auto addLineMatches = [&](const auto& cached, int lineIndex,
                              std::string_view haystack) {
        const std::string& line = cached.lines[static_cast<size_t>(lineIndex)];
        auto foundPositions = text_utils::find_cursor(haystack, searchNeedle);
        size_t pos = 0;
        while(foundPositions.next(pos))
        {
            GrepMatch match;
            match.filepath = cached.path;
            match.filename = text_utils::basename(cached.path);
            match.lineNumber = lineIndex + 1;
            match.lineContent = trimString(line);
            match.highlightRanges.push_back(
                std::make_pair((int)pos, (int)query.length()));
            matches.push_back(std::move(match));
            if(matches.size() >= 1000)
                return false;
        }
        return true;
    };

    const std::string lookupToken = rgCacheLookupToken(editor, searchNeedle);
    auto indexed = editor.rgCacheLineIndex.find(lookupToken);
    if(indexed != editor.rgCacheLineIndex.end())
    {
        constexpr size_t kMaxCachedCandidates = 50000;
        constexpr auto kMaxCachedSearchTime = std::chrono::milliseconds(40);
        const auto startTime = std::chrono::steady_clock::now();
        size_t scanned = 0;
        for(const auto& ref : indexed->second)
        {
            if(++scanned > kMaxCachedCandidates ||
               std::chrono::steady_clock::now() - startTime >
                   kMaxCachedSearchTime)
            {
                break;
            }
            if(ref.fileIndex < 0 ||
               ref.fileIndex >= static_cast<int>(editor.rgCachedFiles.size()))
                continue;
            const auto& cached =
                editor.rgCachedFiles[static_cast<size_t>(ref.fileIndex)];
            if(ref.lineIndex < 0 ||
               ref.lineIndex >= static_cast<int>(cached.lines.size()))
                continue;

            std::string_view haystack =
                caseSensitive
                    ? std::string_view(cached.lines[static_cast<size_t>(
                          ref.lineIndex)])
                    : std::string_view(cached.lowerLines[static_cast<size_t>(
                          ref.lineIndex)]);
            if(!addLineMatches(cached, ref.lineIndex, haystack))
            {
                lastCachedQuery = query;
                lastCachedCaseSensitive = caseSensitive;
                lastCachedMatches = matches;
                return true;
            }
        }
        lastCachedQuery = query;
        lastCachedCaseSensitive = caseSensitive;
        lastCachedMatches = matches;
        return true;
    }

    lastCachedQuery = query;
    lastCachedCaseSensitive = caseSensitive;
    lastCachedMatches.clear();
    return true;
}
#endif

bool GrepSearchMode::performRipgrepSearch(Editor& editor)
{
#ifdef _WIN32
    // Windows _popen waits for the child process in _pclose. Because ripgrep's
    // --max-count is per file, uvim can stop reading after 1000 matches while
    // rg keeps scanning the rest of the tree, making interactive Ctrl-S appear
    // frozen. The in-process fallback has a real global match cap.
    (void)editor;
    return false;
#else
    if(!ripgrepAvailable())
        return false;

    std::vector<std::string> args = {
        "rg",
        "--fixed-strings",
        "--line-number",
        "--no-heading",
        "--color",
        "never",
        "--field-match-separator",
        "\t",
        "--hidden",
        "--glob",
        "!.git/*",
        "--glob",
        "!.rg/*",
        "--max-count",
        "1000",
    };
    args.push_back(caseSensitive ? "--case-sensitive" : "--ignore-case");
    if(!editor.respectGitignore)
        args.push_back("--no-ignore");
    args.push_back("--");
    args.push_back(query);
    args.push_back(".");

    ProcessPipe pipe(args);
    if(!pipe)
        return false;

    std::string line;
    while(matches.size() < 1000 && !(line = pipe.readLine(64 * 1024)).empty())
    {
        const size_t pathEnd = line.find('\t');
        if(text_utils::is_not_found(pathEnd))
            continue;
        const size_t lineEnd = line.find('\t', pathEnd + 1);
        if(text_utils::is_not_found(lineEnd))
            continue;

        int lineNumber = 0;
        try
        {
            lineNumber =
                std::stoi(line.substr(pathEnd + 1, lineEnd - pathEnd - 1));
        }
        catch(...)
        {
            continue;
        }

        std::string path = line.substr(0, pathEnd);
        if(path.rfind("./", 0) == 0 || path.rfind(".\\", 0) == 0)
            path.erase(0, 2);

        GrepMatch match;
        match.filepath = path;
        match.filename = text_utils::basename(path);
        match.lineNumber = lineNumber;
        match.lineContent = trimString(line.substr(lineEnd + 1));

        std::string haystack = caseSensitive ? match.lineContent
                                             : toLower(match.lineContent);
        std::string needle = caseSensitive ? query : toLower(query);
        if(auto pos = haystack.find(needle); text_utils::is_found(pos))
            match.highlightRanges.push_back(
                std::make_pair((int)pos, (int)query.length()));

        matches.push_back(std::move(match));
    }

    return true;
#endif
}

void GrepSearchMode::searchInFile(const std::string& filepath,
                                  std::string_view needle)
{
    if(needle.empty())
        return;

    if(!isTextFile(filepath))
        return;

    std::ifstream file(filepath);
    if(!file)
        return;

    std::string line;
    int lineNumber = 0;

    std::string loweredNeedle;
    std::string_view searchNeedle = needle;
    if(!caseSensitive)
    {
        loweredNeedle = toLower(needle);
        searchNeedle = loweredNeedle;
    }

    while(std::getline(file, line))
    {
        lineNumber++;

        std::string_view haystack = line;
        std::string loweredLine;
        if(!caseSensitive)
        {
            loweredLine = toLower(line);
            haystack = loweredLine;
        }

        auto foundPositions = text_utils::find_cursor(haystack, searchNeedle);
        size_t pos = 0;
        while(foundPositions.next(pos))
        {
            GrepMatch match;
            match.filepath = filepath;
            match.filename = text_utils::basename(filepath);

            match.lineNumber = lineNumber;
            match.lineContent = trimString(line);
            match.highlightRanges.push_back(
                std::make_pair((int)pos, (int)needle.length()));

            matches.push_back(match);
            if(matches.size() >= 1000)
                return;
        }
    }
}

bool GrepSearchMode::isTextFile(const std::string& filepath) const
{
    std::string ext;
    size_t dotPos =
        filepath.find_last_of(keyCode(command::CommandKey::KEY_DOT));
    if(text_utils::is_found(dotPos))
    {
        ext = filepath.substr(dotPos);
        bool isPythonExt =
            constants::is_filetype<constants::no_pattern,
                                   constants::python_suffixes>(filepath);
        bool isMlaExt =
            constants::is_filetype<constants::no_pattern,
                                   constants::mla_suffixes>(filepath);

        if(ext == ".txt" || ext == ".cpp" || ext == ".c" || ext == ".h" ||
           ext == ".hpp" || isPythonExt || ext == ".js" || ext == ".ts" ||
           ext == ".jsx" || ext == ".tsx" || ext == ".java" || ext == ".rs" ||
           ext == ".go" || ext == ".rb" || ext == ".php" || ext == ".sh" ||
           ext == ".bash" || ext == ".zsh" || ext == ".vim" || ext == ".lua" ||
           ext == ".md" || ext == ".markdown" || ext == ".rst" ||
           ext == ".tex" || ext == ".css" || ext == ".scss" || ext == ".html" ||
           ext == ".xml" || ext == ".json" || ext == ".yaml" || ext == ".yml" ||
           ext == ".toml" || ext == ".ini" || ext == ".conf" ||
           ext == ".config" || ext == ".log" || ext == ".cmake" ||
           ext == ".make" || ext == ".mk" || ext == ".am" || isMlaExt)
        {
            return true;
        }

        if(ext == ".exe" || ext == ".o" || ext == ".so" || ext == ".a" ||
           ext == ".dll" || ext == ".dylib" || ext == ".bin" || ext == ".dat" ||
           ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" ||
           ext == ".bmp" || ext == ".ico" || ext == ".pdf" || ext == ".doc" ||
           ext == ".docx" || ext == ".xls" || ext == ".xlsx" || ext == ".ppt" ||
           ext == ".pptx" || ext == ".zip" || ext == ".tar" || ext == ".gz" ||
           ext == ".bz2" || ext == ".7z" || ext == ".rar" || ext == ".mp3" ||
           ext == ".mp4" || ext == ".avi" || ext == ".mov" || ext == ".wav" ||
           ext == ".flac" || ext == ".ogg" || ext == ".ttf" || ext == ".otf" ||
           ext == ".woff" || ext == ".woff2" || ext == ".eot")
        {
            return false;
        }
    }

    return !isBinaryFile(filepath);
}

bool GrepSearchMode::isBinaryFile(const std::string& filepath) const
{
    std::ifstream file(filepath, std::ios::binary);
    if(!file)
        return true;

    char buffer[512];
    file.read(buffer, sizeof(buffer));
    std::streamsize bytesRead = file.gcount();

    int nullCount = 0;
    int nonPrintable = 0;

    for(std::streamsize i = 0; i < bytesRead; i++)
    {
        unsigned char c = static_cast<unsigned char>(buffer[i]);

        if(c == 0)
        {
            nullCount++;
            if(nullCount > 1)
                return true;
        }

        if(c < 32 && c != '\n' && c != '\r' && c != '\t' && c != '\f')
        {
            nonPrintable++;
        }
    }

    double nonPrintableRatio =
        bytesRead > 0 ? (double)nonPrintable / bytesRead : 0;
    return nonPrintableRatio > 0.3;
}

std::string GrepSearchMode::trimString(const std::string& str) const
{
    size_t first = str.find_first_not_of(" \t\r\n");
    if(text_utils::is_not_found(first))
        return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

bool GrepSearchMode::selectMatch(Editor& editor)
{
    if(cursor < 0 || cursor >= (int)matches.size())
        return false;

    const GrepMatch& match = matches[cursor];

    prewarmAroundCursor(editor);
    editor.openFile(std::string_view(match.filepath));

    *editor.cursorY = match.lineNumber - 1;
    if(*editor.cursorY >= (int)editor.lines->size())
        *editor.cursorY = editor.lines->size() - 1;
    if(*editor.cursorY < 0)
        *editor.cursorY = 0;

    *editor.cursorX = 0;

    seedEditorSearchFromGrepMatch(editor, match, query);
    editor.centerScreen();

    return true;
}

void GrepSearchMode::resultUp(Editor& editor)
{
    if(cursor > 0)
    {
        cursor--;
        if(cursor < offset)
            offset = cursor;
    }
    prewarmAroundCursor(editor);
}

void GrepSearchMode::resultDown(Editor& editor)
{
    if(cursor < (int)matches.size() - 1)
    {
        cursor++;
        int visible = grepSearchVisibleRows(editor);
        if(cursor >= offset + visible)
            offset = cursor - visible + 1;
    }
    prewarmAroundCursor(editor);
}

void GrepSearchMode::resultHalfPageUp(Editor& editor)
{
    int half = grepSearchVisibleRows(editor) / 2;
    cursor -= half;
    if(cursor < 0)
        cursor = 0;
    if(cursor < offset)
        offset = cursor;
    prewarmAroundCursor(editor);
}

void GrepSearchMode::resultHalfPageDown(Editor& editor)
{
    int half = grepSearchVisibleRows(editor) / 2;
    cursor += half;
    if(cursor >= (int)matches.size())
        cursor = matches.size() - 1;
    int visible = grepSearchVisibleRows(editor);
    if(cursor >= offset + visible)
        offset = cursor - visible + 1;
    prewarmAroundCursor(editor);
}

void GrepSearchMode::searchAddChar(Editor& editor, char c)
{
    query += c;
    while(true)
    {
        const int next = Terminal::readKeyTimeout(0);
        if(next < 0)
            break;
        if(next >= 32 && next < 127)
        {
            query += static_cast<char>(next);
            continue;
        }
        Terminal::unreadKey(next);
        break;
    }
    scheduleSearch(editor);
    cursor = 0;
    offset = 0;
}

void GrepSearchMode::searchBackspace(Editor& editor)
{
    if(!query.empty())
    {
        query.pop_back();
        scheduleSearch(editor);
        cursor = 0;
        offset = 0;
    }
}

void GrepSearchMode::searchDeleteWord(Editor& editor)
{
    while(!query.empty() && query.back() == keyCode(control::ControlKey::SPACE))
        query.pop_back();
    while(!query.empty() && query.back() != keyCode(control::ControlKey::SPACE))
        query.pop_back();
    scheduleSearch(editor);
    cursor = 0;
    offset = 0;
}

void GrepSearchMode::searchClear()
{
    searchPending = false;
    query.clear();
    matches.clear();
    selectedMatches.clear();
    cursor = 0;
    offset = 0;
#ifdef UVIM_ENABLE_RG_CACHE
    clearGrepCachedQueryState(*this);
#endif
}

void GrepSearchMode::toggleGitignore(Editor& editor)
{
    if(editor.gitignoreLockedOff)
        return;
    editor.respectGitignore = !editor.respectGitignore;
    editor.grepFileIndexInitialized = false;
    editor.grepProjectFiles.clear();
#ifdef UVIM_ENABLE_RG_CACHE
    editor.rgCacheLoaded = false;
    editor.rgCachedFiles.clear();
    editor.rgCacheLineIndex.clear();
    clearGrepCachedQueryState(*this);
    lastRgCacheIndexRefresh = {};
#endif
    cursor = 0;
    offset = 0;
    selectedMatches.clear();
    if(query.empty())
    {
        matches.clear();
        searching = false;
        return;
    }
    performSearch(editor);
}

void GrepSearchMode::togglePreview()
{
    previewEnabled = !previewEnabled;
}

void GrepSearchMode::toggleSelection()
{
    if(cursor < 0 || cursor >= (int)matches.size())
        return;

    auto it = selectedMatches.find(cursor);
    if(it != selectedMatches.end())
        selectedMatches.erase(it);
    else
        selectedMatches.insert(cursor);
}

void GrepSearchMode::prewarmAroundCursor(Editor& editor) const
{
    if(cursor < 0 || cursor >= (int)matches.size())
        return;

    std::vector<std::string> paths;
    std::unordered_set<std::string> seen;
    paths.reserve(21);

    auto addPath = [&](int index) {
        if(index < 0 || index >= (int)matches.size())
            return;
        const std::string& path = matches[index].filepath;
        if(seen.insert(path).second)
            paths.push_back(path);
    };

    addPath(cursor);
    for(int distance = 1; distance <= 10; ++distance)
    {
        addPath(cursor + distance);
        addPath(cursor - distance);
    }

    editor.prewarmColdOpenFiles(paths);
}

bool GrepSearchMode::openSelected(Editor& editor)
{
    if(selectedMatches.empty())
        return false;

    std::vector<int> indexes;
    indexes.reserve(selectedMatches.size());
    for(int index : selectedMatches)
    {
        if(index >= 0 && index < (int)matches.size())
            indexes.push_back(index);
    }
    if(indexes.empty())
        return false;

    std::sort(indexes.begin(), indexes.end());
    std::vector<std::string> openedPaths;
    openedPaths.reserve(indexes.size());
    for(int index : indexes)
    {
        const std::string& path = matches[index].filepath;
        if(std::find(openedPaths.begin(), openedPaths.end(), path) ==
           openedPaths.end())
            openedPaths.push_back(path);
    }

    GrepMatch finalMatch = matches[indexes.back()];
    for(auto it = openedPaths.begin(); it != openedPaths.end();)
    {
        if(*it == finalMatch.filepath)
            it = openedPaths.erase(it);
        else
            ++it;
    }
    openedPaths.push_back(finalMatch.filepath);

    editor.prewarmColdOpenFiles(openedPaths);
    for(size_t i = 0; i < openedPaths.size(); ++i)
    {
        bool notifyLsp = (i + 1 == openedPaths.size());
        editor.openFile(std::string_view(openedPaths[i]), notifyLsp);
    }

    *editor.cursorY = finalMatch.lineNumber - 1;
    if(*editor.cursorY >= (int)editor.lines->size())
        *editor.cursorY = editor.lines->size() - 1;
    if(*editor.cursorY < 0)
        *editor.cursorY = 0;
    *editor.cursorX = 0;
    seedEditorSearchFromGrepMatch(editor, finalMatch, query);
    editor.centerScreen();

    selectedMatches.clear();
    return true;
}
} // namespace editor::statemachine
