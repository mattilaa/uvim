#include "editor.h"
#include "editor_execution_commands.h"
#include "editor_path_utilities.h"
#include "editor_utils.h"
#include "gitignore.h"
#include "substitute_command.h"
#include "terminal.h"
#include "text_utils.h"
#include "widgets/picker_frame.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>

namespace command::execution
{
namespace
{
size_t commandOffset(std::string_view input)
{
    size_t pos = 0;
    while(pos < input.size())
    {
        const char ch = input[pos];
        if(std::isdigit(static_cast<unsigned char>(ch)) ||
           std::string_view("%.,$'<>+- \t").find(ch) != std::string_view::npos)
            ++pos;
        else
            break;
    }
    return pos;
}

bool delimiter(char ch)
{
    return std::ispunct(static_cast<unsigned char>(ch)) && ch != '\\';
}

struct Substitution
{
    int first = 0, last = 0;
    std::string pattern, replacement;
    bool global = false, insensitive = false, countOnly = false;
    bool quietNoMatch = false, project = false, confirm = false;
};

bool address(std::string_view& text, int current, int last, int visualFirst,
             int visualLast, int& result)
{
    text = editor::helper::trim_view(text);
    if(text.empty())
        return false;
    size_t consumed = 1;
    if(text.front() == '.')
        result = current;
    else if(text.front() == '$')
        result = last;
    else if(text.starts_with("'<") || text.starts_with("'>"))
    {
        result = text[1] == '<' ? visualFirst : visualLast;
        consumed = 2;
    }
    else
    {
        int number = 0;
        const auto parsed =
            std::from_chars(text.data(), text.data() + text.size(), number);
        if(parsed.ec != std::errc{} || parsed.ptr == text.data() || number < 1)
            return false;
        result = number - 1;
        consumed = static_cast<size_t>(parsed.ptr - text.data());
    }
    text.remove_prefix(consumed);
    text = editor::helper::trim_view(text);
    while(!text.empty() && (text.front() == '+' || text.front() == '-'))
    {
        const bool subtract = text.front() == '-';
        text.remove_prefix(1);
        int offset = 1;
        if(!text.empty() &&
           std::isdigit(static_cast<unsigned char>(text.front())))
        {
            const auto parsed =
                std::from_chars(text.data(), text.data() + text.size(), offset);
            if(parsed.ec != std::errc{})
                return false;
            text.remove_prefix(static_cast<size_t>(parsed.ptr - text.data()));
        }
        const auto next = static_cast<long long>(result) +
                          (subtract ? -static_cast<long long>(offset) : offset);
        if(next < 0 || next > last)
            return false;
        result = static_cast<int>(next);
        text = editor::helper::trim_view(text);
    }
    return result >= 0 && result <= last;
}

bool segment(std::string_view& text, char separator, std::string& out,
             bool pattern)
{
    for(size_t i = 0; i < text.size(); ++i)
    {
        const char ch = text[i];
        if(ch == separator)
        {
            text.remove_prefix(i + 1);
            return true;
        }
        if(ch == '\\' && i + 1 < text.size())
        {
            const char escaped = text[++i];
            // Regex punctuation must remain escaped even when it is the
            // delimiter. Other delimiters (notably '/') need no regex escape.
            if(!pattern || escaped != separator ||
               std::string_view(".^$|()[]{}*+?\\").find(escaped) !=
                   std::string_view::npos)
                out += '\\';
            out += escaped;
        }
        else
            out += ch;
    }
    return false;
}

std::string expandReplacement(const std::smatch& match,
                              std::string_view replacement)
{
    std::string result;
    for(size_t i = 0; i < replacement.size(); ++i)
    {
        const char ch = replacement[i];
        if(ch == '&')
            result += match.str();
        else if(ch == '\\' && i + 1 < replacement.size())
        {
            const char escaped = replacement[++i];
            if(escaped >= '1' && escaped <= '9')
            {
                const size_t group = escaped - '0';
                if(group < match.size())
                    result += match[group].str();
            }
            else if(escaped == 'n' || escaped == 'r')
                result += '\n';
            else if(escaped == 't')
                result += '\t';
            else
                result += escaped;
        }
        else
            result += ch;
    }
    return result;
}

// One session spans every file in a project replacement. Decisions are made
// against snapshots before edits are applied, preserving one undo step per
// file.
struct Confirmation
{
    Editor& editor;
    bool all = false, stopped = false, seen = false, drawn = false;

    static std::string previewText(std::string_view text, int width)
    {
        std::string out;
        int used = 0;
        for(size_t i = 0; i < text.size() && used < width;)
        {
            const auto ch = static_cast<unsigned char>(text[i]);
            if(ch < 32 || ch == 127 || (ch >= 0x80 && ch < 0xa0))
            {
                const std::string escaped = ch == '\n'   ? "\\n"
                                            : ch == '\t' ? "\\t"
                                                         : "?";
                if(used + static_cast<int>(escaped.size()) > width)
                    break;
                out += escaped;
                used += static_cast<int>(escaped.size());
                ++i;
            }
            else
            {
                const size_t length = ch < 0xc0   ? 1
                                      : ch < 0xe0 ? 2
                                      : ch < 0xf0 ? 3
                                                  : 4;
                out.append(text.substr(i, length));
                i += length;
                ++used;
            }
        }
        return out;
    }

    bool accept(std::string_view path, int row, const std::string& line,
                const std::smatch& match, const std::string& replacement)
    {
        if(stopped)
            return false;
        seen = true;
        if(all)
            return true;
        auto layout = widgets::computePickerLayout(
            {std::max(1, editor.screenRows - 2), editor.screenCols, true, 90,
             40, false});
        Terminal::beginCapture();
        if(!drawn)
            editor.drawFullScreen();
        drawn = true;
        std::string out;
        widgets::PickerCanvas canvas(out, layout, editor.theme,
                                     editor.utf8Mode);
        canvas.frame("Confirm replacement",
                     "y yes | n skip | a all | l last | q/Esc stop", 0);
        const auto put =
            [&](int index, std::string_view text, const std::string& color)
        {
            if(index >= canvas.bodyRows())
                return;
            canvas.beginRow(index);
            const auto clipped = previewText(text, layout.cols);
            out += color;
            out += clipped;
            out += editor.theme.reset();
            canvas.endRow(text_utils::utf8DisplayWidth(clipped));
        };
        std::string displayPath =
            path.empty() ? "[No Name]" : std::string(path);
        if(!path.empty())
        {
            std::error_code ec;
            const auto root =
                editor.projectRoot.empty()
                    ? EditorPathUtilities::currentWorkingDirectory()
                    : EditorPathUtilities::resolveEditorPath(
                          editor.projectRoot);
            const auto relative = std::filesystem::relative(
                std::filesystem::path(path), root, ec);
            if(!ec && !relative.empty() && relative.begin()->string() != "..")
                displayPath = relative.generic_string();
            else
                displayPath = std::filesystem::path(path).filename().string() +
                              " | " + std::string(path);
        }
        const std::string location =
            "Line " + std::to_string(row + 1) + ", column " +
            std::to_string(match.position() + 1) + " | " + displayPath;
        put(0, location, editor.theme.uiAccent());
        // Center long source excerpts on the occurrence being considered.
        const size_t start = static_cast<size_t>(match.position());
        size_t context = start > 20 ? start - 20 : 0;
        while(context > 0 &&
              (static_cast<unsigned char>(line[context]) & 0xc0) == 0x80)
            --context;
        put(1, "Source: " + line.substr(context), editor.theme.uiDim());
        put(2, "Match: " + match.str(), editor.theme.uiWarning());
        put(3, "Replace with: " + replacement, editor.theme.uiSuccess());
        put(4, "y:yes n:skip a:all l:last q/Esc:stop", editor.theme.uiInfo());
        Terminal::write(out);
        Terminal::endCapture();
        editor.needsFullRedraw = true;
        for(;;)
        {
            switch(Terminal::readKey())
            {
            case 'y':
            case 'Y':
                return true;
            case 'n':
            case 'N':
                return false;
            case 'a':
            case 'A':
                all = true;
                return true;
            case 'l':
            case 'L':
                stopped = true;
                return true;
            case 'q':
            case 'Q':
            case 27:
            case 3:
                stopped = true;
                return false;
            default:
                break;
            }
        }
    }
};

std::vector<std::string> replaceLines(const std::vector<std::string>& source,
                                      const Substitution& substitution,
                                      const std::regex& expression,
                                      size_t& matches, size_t& matchedLines,
                                      Confirmation* confirmation = nullptr,
                                      std::string_view path = {})
{
    std::vector<std::string> result;
    result.reserve(source.size());

    for(int row = 0; row < static_cast<int>(source.size()); ++row)
    {
        const auto& line = source[row];
        if(row < substitution.first || row > substitution.last ||
           (confirmation && confirmation->stopped))
        {
            result.push_back(line);
            continue;
        }
        std::string replaced;
        size_t copied = 0, lineMatches = 0;
        for(std::sregex_iterator it(line.begin(), line.end(), expression), end;
            it != end; ++it)
        {
            const auto& match = *it;
            const std::string replacement =
                substitution.countOnly
                    ? std::string{}
                    : expandReplacement(match, substitution.replacement);
            const bool accepted =
                !confirmation || substitution.countOnly ||
                confirmation->accept(path, row, line, match, replacement);
            if(!accepted)
            {
                if(!substitution.global || confirmation->stopped)
                    break;
                continue;
            }
            ++lineMatches;
            if(!substitution.countOnly)
            {
                const auto start = static_cast<size_t>(match.position());
                replaced.append(line, copied, start - copied);
                replaced += replacement;
                copied = start + static_cast<size_t>(match.length());
            }
            if(!substitution.global || (confirmation && confirmation->stopped))
                break;
        }
        matches += lineMatches;
        matchedLines += lineMatches != 0;
        if(substitution.countOnly || !lineMatches)
        {
            result.push_back(line);
            continue;
        }
        replaced.append(line, copied, std::string::npos);
        size_t start = 0;
        for(;;)
        {
            const auto newline = replaced.find('\n', start);
            result.push_back(replaced.substr(start, newline - start));
            if(newline == std::string::npos)
                break;
            start = newline + 1;
        }
    }
    return result;
}

void applyReplacement(Editor& editor, std::vector<std::string> result)
{
    if(result != *editor.lines)
    {
        const int oldX = *editor.cursorX, oldY = *editor.cursorY;
        editor.saveState();
        *editor.lines = std::move(result);
        *editor.cursorY =
            std::clamp(oldY, 0, static_cast<int>(editor.lines->size()) - 1);
        *editor.cursorX = std::clamp(
            oldX, 0, static_cast<int>((*editor.lines)[*editor.cursorY].size()));
        // Keep a clamped byte cursor on a UTF-8 character boundary.
        const auto& cursorLine = (*editor.lines)[*editor.cursorY];
        while(*editor.cursorX > 0 &&
              *editor.cursorX < static_cast<int>(cursorLine.size()) &&
              (static_cast<unsigned char>(cursorLine[*editor.cursorX]) &
               0xc0) == 0x80)
            --*editor.cursorX;
        if(editor.wantedX)
            *editor.wantedX = *editor.cursorX;
        *editor.dirty = true;
        editor.currentBuffer->blameValid = false;
        editor.saveState();
        editor.currentBuffer->undoStack[editor.currentBuffer->undoIndex - 1]
            .cursorX = oldX;
        editor.currentBuffer->undoStack[editor.currentBuffer->undoIndex - 1]
            .cursorY = oldY;
        editor.searchMatches.clear();
        editor.currentMatchIndex = -1;
        editor.adjustViewport();
        editor.needsFullRedraw = true;
    }
}

bool replaceProject(Editor& editor, const Substitution& substitution,
                    const std::regex& expression)
{
    namespace fs = std::filesystem;
    const auto fail = [&](const std::string& message)
    {
        editor.setStatusMessage("substitute: " + message);
        return true;
    };
    const fs::path requestedRoot = EditorPathUtilities::resolveEditorPath(
        editor.projectRoot.empty()
            ? EditorPathUtilities::currentWorkingDirectory()
            : fs::path(editor.projectRoot));
    std::error_code ec;
    const auto root = fs::weakly_canonical(requestedRoot, ec);
    if(ec)
        return fail("Invalid project root: " + ec.message());
    if(!fs::is_directory(root, ec) || ec)
        return fail("Invalid project root: " + root.string());
    GitIgnore ignore;
    if(editor.respectGitignore)
        ignore.loadRecursive(root.string());

    std::set<std::string> paths;
    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec),
        end;
    for(; !ec && it != end; it.increment(ec))
    {
        const auto& entry = *it;
        const auto name = entry.path().filename().string();
        const auto status = entry.symlink_status(ec);
        if(ec)
            break;
        const bool directory = fs::is_directory(status);
        // Keep traversal inside the root and consistent with project pickers.
        if(fs::is_symlink(status) || name.starts_with('.') ||
           ignore.isIgnored(entry.path().string(), directory))
        {
            if(directory)
                it.disable_recursion_pending();
            continue;
        }
        if(fs::is_regular_file(status))
            paths.insert(entry.path().lexically_normal().string());
    }
    if(ec)
        return fail("Cannot list project: " + ec.message());

    // Include new or unsaved named buffers as well as files found on disk.
    std::unordered_map<std::string, int> existingBuffers;
    for(size_t index = 0; index < editor.buffers.size(); ++index)
    {
        const auto& buffer = editor.buffers[index];
        if(buffer->filename.empty())
            continue;
        const auto path = fs::weakly_canonical(
            EditorPathUtilities::resolveEditorPath(buffer->filename), ec);
        if(ec)
        {
            ec.clear();
            continue;
        }
        const auto relative = path.lexically_relative(root);
        if(relative.empty() || relative.begin()->string() == "..")
            continue;
        bool hidden = false;
        fs::path ancestor = root;
        for(const auto& part : relative)
        {
            ancestor /= part;
            hidden |= part.string().starts_with('.');
            hidden |= fs::is_symlink(ancestor, ec);
            ec.clear();
        }
        if(hidden || ignore.isIgnored(path.string(), false) ||
           fs::is_symlink(path, ec))
            continue;
        ec.clear();
        paths.insert(path.string());
        existingBuffers[path.string()] = static_cast<int>(index);
    }

    struct Change
    {
        std::string path;
        std::vector<std::string> before, after;
        int index = -1;
    };

    std::vector<Change> changes;
    size_t matches = 0, matchedLines = 0, matchedFiles = 0;
    Confirmation confirmation{editor};
    auto* session = substitution.confirm && !substitution.countOnly
                        ? &confirmation
                        : nullptr;
    for(const auto& path : paths)
    {
        if(session && session->stopped)
            break;
        std::vector<std::string> source;
        const auto found = existingBuffers.find(path);
        const int existing =
            found == existingBuffers.end() ? -1 : found->second;
        if(existing >= 0)
            source = editor.buffers[existing]->lines;
        else
        {
            if(!editor::helper::locIsTextFile(path) ||
               editor::helper::locIsBinaryFile(path))
                continue;
            std::ifstream file(path, std::ios::binary);
            if(!file)
                return fail("Cannot read project file: " + path);
            std::string line;
            bool binary = false;
            while(std::getline(file, line))
            {
                if(line.find('\0') != std::string::npos)
                    binary = true;
                if(!line.empty() && line.back() == '\r')
                    line.pop_back();
                source.push_back(line);
            }
            if(file.bad())
                return fail("Cannot read project file: " + path);
            if(binary)
                continue;
            if(source.empty())
                source.emplace_back();
        }
        if(std::any_of(source.begin(), source.end(), [](const auto& line)
                       { return line.find('\0') != std::string::npos; }))
            continue;
        Substitution allLines = substitution;
        allLines.first = 0;
        allLines.last = static_cast<int>(source.size()) - 1;
        const auto previousMatches = matches;
        auto result = replaceLines(source, allLines, expression, matches,
                                   matchedLines, session, path);
        matchedFiles += previousMatches != matches;
        if(!substitution.countOnly && result != source)
            changes.push_back(
                {path, std::move(source), std::move(result), existing});
    }
    if(!matches && !substitution.quietNoMatch && !confirmation.seen)
        return fail("Pattern not found in project: " + substitution.pattern);

    const int original = editor.currentBufferIndex;
    // Open only files that will change. Verify every snapshot before applying
    // any replacement; count-only requests never open buffers.
    for(auto& change : changes)
    {
        if(change.index >= 0)
            editor.switchToBuffer(change.index);
        else
            editor.openFile(change.path, false);
        change.index = editor.currentBufferIndex;
        if(!editor.currentBuffer || *editor.lines != change.before)
        {
            if(original >= 0)
                editor.switchToBuffer(original);
            return fail("File changed while preparing replacement: " +
                        change.path);
        }
    }
    for(auto& change : changes)
    {
        editor.switchToBuffer(change.index);
        applyReplacement(editor, std::move(change.after));
        editor.currentBuffer->lastSearchQuery = substitution.pattern;
    }
    if(original >= 0)
        editor.switchToBuffer(original);
    editor.searchQuery = substitution.pattern;
    editor.searchMatches.clear();
    editor.currentMatchIndex = -1;
    editor.searchRegexError = false;
    editor.addSearchToHistory(substitution.pattern);
    editor.needsFullRedraw = true;
    editor.setStatusMessage(
        std::to_string(matches) +
        (substitution.countOnly ? " matches in " : " substitutions in ") +
        std::to_string(matchedFiles) + " project files; " +
        std::to_string(changes.size()) + " modified buffers" +
        (changes.empty() ? "" : "; :wa to save") +
        (confirmation.stopped ? "; stopped" : ""));
    return true;
}

} // namespace

bool isSubstituteCommand(std::string_view input)
{
    input = editor::helper::trim_view(input);
    if(input.starts_with(':'))
        input.remove_prefix(1);
    const size_t pos = commandOffset(input);
    input.remove_prefix(pos);
    if(input.starts_with("substitute"))
        input.remove_prefix(10);
    else if(input.starts_with('s'))
        input.remove_prefix(1);
    else
        return false;
    return input.empty() || delimiter(input.front());
}

bool SubstituteCommand::execute(Editor& editor,
                                const CommandRequest& request) const
{
    if(!isSubstituteCommand(request.trimmed))
        return false;
    const auto fail = [&](const std::string& message)
    {
        editor.setStatusMessage("substitute: " + message);
        return true;
    };
    Substitution substitution;
    const bool hasBuffer =
        editor.hasBuffer() && editor.lines && !editor.lines->empty();
    substitution.first = substitution.last = hasBuffer ? *editor.cursorY : 0;
    const int last = hasBuffer ? static_cast<int>(editor.lines->size()) - 1 : 0;
    std::string_view text = request.trimmed;
    const size_t pos = commandOffset(text);
    std::string_view range = editor::helper::trim_view(text.substr(0, pos));
    substitution.project = range == "%%";
    if(!hasBuffer && !substitution.project)
        return fail("No buffer");
    if(substitution.project || range == "%")
    {
        substitution.first = 0;
        substitution.last = last;
    }
    else if(!range.empty())
    {
        const int visualFirst = std::min(editor.currentBuffer->visualStartY,
                                         editor.currentBuffer->visualEndY);
        const int visualLast = std::max(editor.currentBuffer->visualStartY,
                                        editor.currentBuffer->visualEndY);
        if(!address(range, *editor.cursorY, last, visualFirst, visualLast,
                    substitution.first))
            return fail("Invalid line range");
        substitution.last = substitution.first;
        if(range.starts_with(','))
        {
            range.remove_prefix(1);
            if(!address(range, *editor.cursorY, last, visualFirst, visualLast,
                        substitution.last))
                return fail("Invalid line range");
        }
        if(!range.empty() || substitution.first > substitution.last)
            return fail("Invalid line range");
    }
    text.remove_prefix(pos);
    text.remove_prefix(text.starts_with("substitute") ? 10 : 1);
    if(text.empty() || !delimiter(text.front()))
        return fail("Usage: [range]s/pattern/replacement/[gcineI]");
    const char separator = text.front();
    text.remove_prefix(1);
    if(!segment(text, separator, substitution.pattern, true) ||
       !segment(text, separator, substitution.replacement, false))
        return fail("Missing closing delimiter");
    text = editor::helper::trim_view(text);
    for(char flag : text)
    {
        switch(flag)
        {
        case 'c':
            substitution.confirm = true;
            break;
        case 'g':
            substitution.global = true;
            break;
        case 'i':
            substitution.insensitive = true;
            break;
        case 'I':
            substitution.insensitive = false;
            break;
        case 'n':
            substitution.countOnly = true;
            break;
        case 'e':
            substitution.quietNoMatch = true;
            break;
        default:
            return fail("Unsupported flag: " + std::string(1, flag));
        }
    }
    if(substitution.pattern.empty())
        substitution.pattern =
            hasBuffer && !editor.currentBuffer->lastSearchQuery.empty()
                ? editor.currentBuffer->lastSearchQuery
                : editor.searchQuery;
    if(substitution.pattern.empty())
        return fail("No previous search pattern");

    std::regex expression;
    try
    {
        auto flags = std::regex::ECMAScript;
        if(substitution.insensitive)
            flags |= std::regex::icase;
        expression = std::regex(substitution.pattern, flags);
    }
    catch(const std::regex_error& error)
    {
        return fail("Invalid regex: " + std::string(error.what()));
    }

    if(substitution.project)
        return replaceProject(editor, substitution, expression);

    size_t matches = 0, matchedLines = 0;
    Confirmation confirmation{editor};
    auto result = replaceLines(
        *editor.lines, substitution, expression, matches, matchedLines,
        substitution.confirm && !substitution.countOnly ? &confirmation
                                                        : nullptr,
        editor.currentBuffer->filename);
    if(!matches && !substitution.quietNoMatch && !confirmation.seen)
        return fail("Pattern not found: " + substitution.pattern);

    if(!substitution.countOnly)
        applyReplacement(editor, std::move(result));
    editor.currentBuffer->lastSearchQuery = substitution.pattern;
    editor.searchQuery = substitution.pattern;
    editor.searchMatches.clear();
    editor.currentMatchIndex = -1;
    editor.searchRegexError = false;
    editor.addSearchToHistory(substitution.pattern);
    editor.setStatusMessage(
        std::to_string(matches) +
        (substitution.countOnly ? " matches on " : " substitutions on ") +
        std::to_string(matchedLines) + " lines" +
        (confirmation.stopped ? "; stopped" : ""));
    return true;
}
} // namespace command::execution
