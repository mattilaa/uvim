#include "constants.h"
#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>

namespace
{
int computeCountWidth(const std::vector<Editor::LocEntry>& entries)
{
    int width = 1;
    for(const auto& entry : entries)
    {
        int digits = (int)std::to_string(std::max(0, entry.loc)).size();
        width = std::max(width, digits);
    }
    return width;
}

std::string_view locFileColor(const Theme& theme, std::string_view path)
{
    if(constants::is_filetype<constants::no_pattern, constants::cpp_suffixes>(
           path) ||
       constants::is_filetype<constants::no_pattern, constants::rust_suffixes>(
           path) ||
       constants::is_filetype<constants::no_pattern, constants::go_suffixes>(
           path) ||
       constants::is_filetype<constants::no_pattern, constants::mla_suffixes>(
           path))
    {
        return theme.uiInfo();
    }

    if(constants::is_filetype<constants::no_pattern, constants::python_suffixes>(
           path) ||
       constants::is_filetype<constants::no_pattern, constants::robot_suffixes>(
           path))
    {
        return theme.uiSuccess();
    }

    if(constants::is_filetype<constants::no_pattern,
                              constants::javascript_suffixes>(path) ||
       constants::is_filetype<constants::no_pattern,
                              constants::typescript_suffixes>(path))
    {
        return theme.uiWarning();
    }

    if(constants::is_filetype<constants::no_pattern, constants::html_suffixes>(
           path) ||
       constants::is_filetype<constants::no_pattern, constants::xml_suffixes>(
           path))
    {
        return theme.uiAccent();
    }

    if(constants::is_filetype<constants::no_pattern, constants::css_suffixes>(
           path))
    {
        return theme.uiDirectory();
    }

    if(constants::is_filetype<constants::no_pattern, constants::json_suffixes>(
           path) ||
       constants::is_filetype<constants::no_pattern, constants::yaml_suffixes>(
           path) ||
       constants::is_filetype<constants::no_pattern, constants::toml_suffixes>(
           path))
    {
        return theme.uiPrompt();
    }

    if(constants::is_filetype<constants::cmake_prefixes,
                              constants::cmake_suffixes>(path) ||
       constants::is_filetype<constants::no_pattern,
                              constants::shell_suffixes>(path))
    {
        return theme.uiWarning();
    }

    return theme.uiDim();
}
} // namespace

// ============================================================================
// LocListMode Implementation
// ============================================================================

void LocListMode::on_enter(ModeContext& ctx)
{
    cursor = 0;
    offset = 0;
    sortMode = SortMode::Normal;
    countWidth = computeCountWidth(ctx.editor->locList);
    ctx.requestFullRedraw();
}

void LocListMode::on_exit(ModeContext& /* ctx */)
{
    Terminal::setCursorBlock();
}

std::optional<ModeState> LocListMode::handle(ModeContext& ctx,
                                             int key)
{
    Editor* ed = ctx.editor;
    int c = keyCode(key);

    if(c == keyCode(control::ControlKey::ESC) || c == keyCode(typed::TypedKey::KEY_Q))
    {
        if(c == keyCode(control::ControlKey::ESC))
        {
            if(sortMode != SortMode::Normal)
            {
                std::sort(ed->locList.begin(), ed->locList.end(),
                          [](const Editor::LocEntry& a,
                             const Editor::LocEntry& b)
                          { return a.displayPath < b.displayPath; });
                sortMode = SortMode::Normal;
                countWidth = computeCountWidth(ed->locList);
                ed->needsFullRedraw = true;
                return std::nullopt;
            }
            ed->noteDoubleEscStatusClear();
        }
        if(returnMode.has_value() && returnMode.value() == FILE_BROWSER)
        {
            FileBrowserMode fb{returnBrowseDirectory};
            fb.browserCursor = returnBrowseCursor;
            fb.browserOffset = returnBrowseOffset;
            return fb;
        }
        return defaultExitMode(ed);
    }

    if(c == keyCode(control::ControlKey::ENTER))
    {
        if(cursor >= 0 && cursor < (int)ed->locList.size())
        {
            ed->openFile(ed->locList[cursor].path);
            return NormalMode{};
        }
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_S))
    {
        if(sortMode == SortMode::Normal || sortMode == SortMode::Asc)
        {
            std::sort(ed->locList.begin(), ed->locList.end(),
                      [](const Editor::LocEntry& a,
                         const Editor::LocEntry& b)
                      {
                          if(a.loc != b.loc)
                              return a.loc > b.loc;
                          return a.displayPath < b.displayPath;
                      });
            sortMode = SortMode::Desc;
        }
        else
        {
            std::sort(ed->locList.begin(), ed->locList.end(),
                      [](const Editor::LocEntry& a,
                         const Editor::LocEntry& b)
                      {
                          if(a.loc != b.loc)
                              return a.loc < b.loc;
                          return a.displayPath < b.displayPath;
                      });
            sortMode = SortMode::Asc;
        }
        cursor = std::clamp(cursor, 0, (int)ed->locList.size() - 1);
        int visible = ed->screenRows - 3;
        offset = std::clamp(offset, 0,
                            std::max(0, (int)ed->locList.size() - visible));
        countWidth = computeCountWidth(ed->locList);
        ed->needsFullRedraw = true;
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_J) || c == keyCode(control::ControlKey::CTRL_N) || c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        if(cursor < (int)ed->locList.size() - 1)
        {
            cursor++;
            int visible = ed->screenRows - 3;
            if(cursor >= offset + visible)
                offset = cursor - visible + 1;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_K) || c == keyCode(control::ControlKey::CTRL_P) || c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        if(cursor > 0)
        {
            cursor--;
            if(cursor < offset)
                offset = cursor;
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_D) || c == keyCode(navigation::NavigationKey::PAGE_DOWN))
    {
        int half = (ed->screenRows - 3) / 2;
        cursor += half;
        if(cursor >= (int)ed->locList.size())
            cursor = std::max(0, (int)ed->locList.size() - 1);
        int visible = ed->screenRows - 3;
        if(cursor >= offset + visible)
            offset = cursor - visible + 1;
    }
    else if(c == keyCode(control::ControlKey::CTRL_U) || c == keyCode(navigation::NavigationKey::PAGE_UP))
    {
        int half = (ed->screenRows - 3) / 2;
        cursor -= half;
        if(cursor < 0)
            cursor = 0;
        if(cursor < offset)
            offset = cursor;
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
        cursor = std::max(0, (int)ed->locList.size() - 1);
        int visible = ed->screenRows - 3;
        offset = std::max(0, cursor - visible + 1);
    }

    ed->needsFullRedraw = true;
    return std::nullopt;
}

void LocListMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::cursorPos(1, 1);
    output += editor.theme.reset();

    // Header
    output += editor.theme.panel();
    std::string header = " LOC (" + std::to_string(editor.locList.size()) +
                         " files, " + std::to_string(editor.locListTotal) +
                         " loc)";
    if(!editor.locListRoot.empty())
        header += " - " + editor.locListRoot;
    if((int)header.length() < editor.screenCols)
        header += std::string(editor.screenCols - header.length(), keyCode(control::ControlKey::SPACE));
    output += header;
    output += editor.theme.reset();
    output += "\r\n";

    int availableRows = editor.screenRows - 3;
    int idx = offset;

    for(int row = 0;
        row < availableRows && idx < (int)editor.locList.size(); row++, idx++)
    {
        const auto& entry = editor.locList[idx];
        bool isSelected = (idx == cursor);

        output += Terminal::ESC_CLEAR_LINE;
        if(isSelected)
            output += editor.theme.selection();

        std::string countStr = std::to_string(std::max(0, entry.loc));
        if((int)countStr.size() < countWidth)
            countStr.insert(0, countWidth - countStr.size(), keyCode(control::ControlKey::SPACE));

        output += " ";
        output += editor.theme.baseFg();
        output += countStr;
        output += " ";

        std::string displayPath = entry.displayPath.empty() ? entry.path
                                                            : entry.displayPath;
        int maxPathLen = std::max(0, editor.screenCols - (countWidth + 3));
        if((int)displayPath.length() > maxPathLen)
        {
            if(maxPathLen > 3)
            {
                displayPath =
                    "..." +
                    displayPath.substr(displayPath.length() - maxPathLen + 3);
            }
            else
            {
                displayPath = displayPath.substr(0, maxPathLen);
            }
        }

        output += locFileColor(editor.theme, entry.path);
        output += displayPath;

        output += editor.theme.reset();
        output += "\r\n";
    }

    for(int row = (int)editor.locList.size() - offset; row < availableRows;
        row++)
    {
        output += Terminal::ESC_CLEAR_LINE;
        output += editor.theme.uiGutter();
        output += "~";
        output += editor.theme.baseFg();
        output += "\r\n";
    }

    // Status bar
    output += editor.theme.statusBar();
    int displayCursor = editor.locList.empty() ? 0 : (cursor + 1);
    std::string status = " [" + std::to_string(displayCursor) + "/" +
                         std::to_string(editor.locList.size()) + "]";
    status += " <Enter> open  <q/Esc> close  <j/k> navigate";
    if((int)status.length() < editor.screenCols)
        status += std::string(editor.screenCols - status.length(), keyCode(control::ControlKey::SPACE));
    output += status;
    output += editor.theme.reset();

    // Message bar
    output += "\r\n";
    output += Terminal::ESC_CLEAR_LINE;
    if(!editor.statusMessage.empty())
        output += editor.statusMessage;

    Terminal::write(output);
    Terminal::flush();
}
