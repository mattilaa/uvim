#include "substitution_match_picker.h"
#include "ascii.h"
#include "color_constant.h"
#include "editor.h"
#include "editor_path_utilities.h"
#include "key_enums.h"
#include "terminal.h"
#include "text_utils.h"
#include "widgets/picker_frame.h"

#include <algorithm>
#include <filesystem>

namespace command::execution
{
std::string substitutionSeparator(const Editor& editor)
{
    return editor.utf8Mode ? ascii::utf8(ascii::BOX_LIGHT_VERTICAL_PADDED)
                           : " | ";
}

std::string substitutionPreviewText(std::string_view text, int width)
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

std::string substitutionDisplayPath(const Editor& editor, std::string_view path)
{
    if(path.empty())
        return "[No Name]";
    std::error_code ec;
    const auto root =
        editor.projectRoot.empty()
            ? EditorPathUtilities::currentWorkingDirectory()
            : EditorPathUtilities::resolveEditorPath(editor.projectRoot);
    const auto relative =
        std::filesystem::relative(std::filesystem::path(path), root, ec);
    if(!ec && !relative.empty() && relative.begin()->string() != "..")
        return relative.generic_string();
    return std::filesystem::path(path).filename().string() +
           substitutionSeparator(editor) + std::string(path);
}

bool SubstitutionMatchPicker::consider(std::string_view path, int row,
                                       size_t column, size_t length,
                                       const std::string& source,
                                       const std::string& replacement)
{
    if(collecting)
    {
        if(entries.empty() || path != cachedPath)
        {
            cachedPath = path;
            cachedDisplayPath = substitutionDisplayPath(editor, path);
        }
        // Store a bounded context excerpt per occurrence so matching a long
        // line many times does not copy the entire line for every list row.
        size_t context = column > 20 ? column - 20 : 0;
        while(context > 0 &&
              (static_cast<unsigned char>(source[context]) & 0xc0) == 0x80)
            --context;
        const std::string excerpt =
            (context ? "... " : "") + source.substr(context, 200);
        entries.push_back({cachedDisplayPath, row, column, excerpt,
                           source.substr(column, length), replacement,
                           column - context + (context ? 4 : 0)});
        return true;
    }
    if(next >= entries.size())
        return false;
    return entries[next++].selected;
}

void SubstitutionMatchPicker::draw(bool backdrop, int& visibleRows)
{
    const auto layout =
        widgets::computePickerLayout({std::max(1, editor.screenRows - 2),
                                      editor.screenCols, true, 90, 75, true});
    Terminal::beginCapture();
    if(backdrop && editor.hasBuffer())
        editor.drawFullScreen();
    std::string out;
    widgets::PickerCanvas canvas(out, layout, editor.theme, editor.utf8Mode);
    const auto separator = substitutionSeparator(editor);
    canvas.frame("Select replacements",
                 "Enter apply selected" + separator + "Esc cancel", 3);
    using Parts = std::vector<std::pair<std::string, std::string>>;
    const auto paint = [&](const Parts& parts, int width)
    {
        int used = 0;
        for(const auto& [text, foreground] : parts)
        {
            if(used >= width)
                break;
            const auto clipped = substitutionPreviewText(text, width - used);
            out += foreground;
            out += clipped;
            used += text_utils::utf8DisplayWidth(clipped);
        }
        return used;
    };
    const auto pathParts = [&](const std::string& path)
    {
        const auto split = path.find_last_of("/\\");
        if(split == std::string::npos)
            return Parts{{path, editor.theme.uiAccent()}};
        return Parts{{path.substr(0, split + 1), editor.theme.uiDirectory()},
                     {path.substr(split + 1), editor.theme.uiAccent()}};
    };
    const auto sourceParts = [&](const Entry& entry)
    {
        const auto start = std::min(entry.matchOffset, entry.source.size());
        const auto length =
            std::min(entry.matched.size(), entry.source.size() - start);
        return Parts{
            {entry.source.substr(0, start), editor.theme.baseFg()},
            {entry.source.substr(start, length), editor.theme.uiWarning()},
            {entry.source.substr(start + length), editor.theme.baseFg()}};
    };
    const auto header = [&](int row, const Parts& parts)
    {
        canvas.beginHeaderRow(row);
        canvas.endHeaderRow(paint(parts, layout.headerCols));
    };
    header(0,
           {{std::to_string(selectedCount), editor.theme.uiSuccess()},
            {" selected / ", editor.theme.uiDim()},
            {std::to_string(entries.size()), editor.theme.uiInfo()},
            {" matches" + separator, editor.theme.uiDim()},
            {std::to_string(cursor + 1) + "/" + std::to_string(entries.size()),
             editor.theme.uiWarning()}});
    header(1,
           {{"j/k move" + separator + "Ctrl-D/U half-page" + separator +
                 "Space toggle" + separator + "r reset",
             editor.theme.uiDim()}});
    const auto& current = entries[cursor];
    header(2, {{"Match: ", editor.theme.uiDim()},
               {current.matched, editor.theme.uiWarning()},
               {" -> ", editor.theme.uiDim()},
               {current.replacement, editor.theme.uiSuccess()}});
    visibleRows = std::max(1, canvas.bodyRows());
    if(cursor < offset)
        offset = cursor;
    else if(cursor >= offset + static_cast<size_t>(visibleRows))
        offset = cursor - visibleRows + 1;
    for(int row = 0; row < visibleRows; ++row)
    {
        canvas.beginRow(row);
        const size_t index = offset + static_cast<size_t>(row);
        int used = 0;
        if(index < entries.size())
        {
            const auto& entry = entries[index];
            const bool focused = index == cursor;
            if(entry.selected)
                out += focused ? color::rgbBg(56, 120, 72)
                               : color::rgbBg(24, 64, 36);
            else if(focused)
                out += editor.theme.selection();
            Parts parts = {
                {focused ? "> " : "  ", editor.theme.uiAccent()},
                {entry.selected
                     ? (editor.utf8Mode ? "✅  " : "[x] ")
                     : (editor.utf8Mode ? "⬜  " : "[ ] "),
                 entry.selected ? editor.theme.baseFg() : editor.theme.uiDim()},
                {std::to_string(entry.row + 1), editor.theme.uiWarning()},
                {":", editor.theme.uiDim()},
                {std::to_string(entry.column + 1), editor.theme.uiInfo()},
                {" ", editor.theme.uiDim()}};
            const auto path = pathParts(entry.path);
            parts.insert(parts.end(), path.begin(), path.end());
            parts.push_back({separator, editor.theme.uiDim()});
            const auto source = sourceParts(entry);
            parts.insert(parts.end(), source.begin(), source.end());
            used = paint(parts, layout.cols);
        }
        // Keep the green selection background through the entire row.
        canvas.endRow(used);
    }
    if(layout.preview)
    {
        Parts file = {{"File: ", editor.theme.uiDim()}};
        const auto path = pathParts(current.path);
        file.insert(file.end(), path.begin(), path.end());
        const std::vector<Parts> details = {
            std::move(file),
            {{"Line ", editor.theme.uiDim()},
             {std::to_string(current.row + 1), editor.theme.uiWarning()},
             {", column ", editor.theme.uiDim()},
             {std::to_string(current.column + 1), editor.theme.uiInfo()}},
            {{"Source excerpt:", editor.theme.uiAccent()}},
            sourceParts(current),
            {{"Match: ", editor.theme.uiDim()},
             {current.matched, editor.theme.uiWarning()}},
            {{"Replace with: ", editor.theme.uiDim()},
             {current.replacement, editor.theme.uiSuccess()}}};
        for(int row = 0; row < visibleRows; ++row)
        {
            canvas.beginPreviewRow(row);
            const int used = row < static_cast<int>(details.size())
                                 ? paint(details[row], layout.previewCols)
                                 : 0;
            canvas.endPreviewRow(used);
        }
    }
    canvas.scrollbar(entries.size(), offset);
    Terminal::write(out);
    Terminal::endCapture();
    editor.needsFullRedraw = true;
}

bool SubstitutionMatchPicker::choose()
{
    if(entries.empty())
        return true;
    int visibleRows = 1;
    bool backdrop = true;
    for(;;)
    {
        draw(backdrop, visibleRows);
        backdrop = false;
        const int key = Terminal::readKey();
        const size_t halfPage = static_cast<size_t>(std::max(1, visibleRows / 2));
        switch(key)
        {
        case keyCode(control::ControlKey::ENTER):
            collecting = false;
            next = 0;
            return true;
        case keyCode(control::ControlKey::ESC):
        case keyCode(control::ControlKey::CTRL_C):
        case 'q':
            return false;
        case keyCode(control::ControlKey::SPACE):
            entries[cursor].selected = !entries[cursor].selected;
            if(entries[cursor].selected)
                ++selectedCount;
            else
                --selectedCount;
            break;
        case 'r':
            for(auto& entry : entries)
                entry.selected = false;
            selectedCount = 0;
            break;
        case 'j':
        case keyCode(navigation::NavigationKey::ARROW_DOWN):
            cursor = std::min(cursor + 1, entries.size() - 1);
            break;
        case 'k':
        case keyCode(navigation::NavigationKey::ARROW_UP):
            if(cursor > 0)
                --cursor;
            break;
        case keyCode(navigation::NavigationKey::PAGE_DOWN):
            cursor = std::min(cursor + static_cast<size_t>(visibleRows),
                              entries.size() - 1);
            break;
        case keyCode(control::ControlKey::CTRL_D):
            cursor = std::min(cursor + halfPage, entries.size() - 1);
            break;
        case keyCode(control::ControlKey::CTRL_U):
            cursor = cursor > halfPage ? cursor - halfPage : 0;
            break;
        case keyCode(navigation::NavigationKey::PAGE_UP):
            cursor = cursor > static_cast<size_t>(visibleRows)
                         ? cursor - visibleRows
                         : 0;
            break;
        case keyCode(navigation::NavigationKey::HOME):
            cursor = 0;
            break;
        case keyCode(navigation::NavigationKey::END):
            cursor = entries.size() - 1;
            break;
        default:
            break;
        }
    }
}
} // namespace command::execution
