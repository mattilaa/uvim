#include "command_palette_mode.h"

#include "command_mode.h"
#include "editor.h"
#include "header_help.h"
#include "key_enums.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include "widgets/command_popup.h"
#include "widgets/picker_frame.h"
#include "widgets/status_bar.h"

#include <algorithm>

namespace editor::statemachine
{
namespace
{
std::vector<std::string> paletteHelpTokens()
{
    return {"[Enter: run]", "[Esc: cancel]", "[Ctrl+J/K: navigate]",
            "[Ctrl+U: clear]", "[\xE2\x80\xA6: asks for an argument]"};
}

// Clips UTF-8 `text` to `width` columns (one per code point).
std::string clipToWidth(const std::string& text, int width)
{
    std::string out;
    int used = 0;
    for(size_t i = 0; i < text.size() && used < width;)
    {
        const auto lead = static_cast<unsigned char>(text[i]);
        const size_t len = lead < 0xC0 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        out.append(text, i, std::min(len, text.size() - i));
        ++used;
        i += len;
    }
    return out;
}

ModeState leave(ModeContext& ctx)
{
    return ctx.hasBuffer() ? ModeState{NormalMode{}} : ModeState{WelcomeMode{}};
}
} // namespace

void CommandPaletteMode::on_enter(ModeContext& ctx)
{
    entries = command_palette::buildEntries(
        ctx.getCommandCompletions(""),
        [](std::string_view command)
        {
            if(command == "w ")
                return std::string("Write the buffer to a file (:w <file>)");
            return std::string(widgets::commandDocumentation(command));
        });
    query.clear();
    refilter();
    ctx.setStatusMessage("");
    ctx.requestFullRedraw();
}

void CommandPaletteMode::on_exit(ModeContext& ctx)
{
    ctx.requestFullRedraw();
}

void CommandPaletteMode::refilter()
{
    matches = command_palette::rank(entries, query);
    cursor = 0;
    offset = 0;
}

std::optional<ModeState> CommandPaletteMode::handle(ModeContext& ctx,
                                                    const ModeKeyEvent& event)
{
    const int c = event.key;
    ctx.requestFullRedraw();

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(control::ControlKey::CTRL_C))
        return leave(ctx);

    if(c == keyCode(control::ControlKey::ENTER))
    {
        if(matches.empty())
            return std::nullopt;
        const command_palette::Entry& entry =
            entries[matches[cursor].entry];
        if(entry.takesArgument)
        {
            // ":e " with the cursor after it, ready for the file name.
            CommandMode commandLine;
            commandLine.initialInput = entry.names.front() + " ";
            return commandLine;
        }
        // Run it exactly like typing ":<command>" and Enter, including the
        // views some commands open (:help, :tasks, :git log, ...).
        ctx.commandBuffer = ":" + entry.names.front();
        CommandMode runner;
        std::optional<ModeState> next =
            runner.handle(ctx, ModeKeyEvent{keyCode(control::ControlKey::ENTER)});
        return next ? next : std::optional<ModeState>(leave(ctx));
    }

    if(c == keyCode(control::ControlKey::CTRL_J) ||
       c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        if(cursor + 1 < (int)matches.size())
            ++cursor;
        return std::nullopt;
    }
    if(c == keyCode(control::ControlKey::CTRL_K) ||
       c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        if(cursor > 0)
            --cursor;
        return std::nullopt;
    }
    if(c == keyCode(navigation::NavigationKey::PAGE_DOWN))
    {
        cursor = std::min((int)matches.size() - 1, cursor + 10);
        cursor = std::max(cursor, 0);
        return std::nullopt;
    }
    if(c == keyCode(navigation::NavigationKey::PAGE_UP))
    {
        cursor = std::max(0, cursor - 10);
        return std::nullopt;
    }

    if(c == keyCode(control::ControlKey::BACKSPACE) || c == 127 ||
       c == keyCode(control::ControlKey::CTRL_H))
    {
        if(query.empty())
            return leave(ctx);
        query.pop_back();
        // Drop a whole UTF-8 character.
        while(!query.empty() &&
              (static_cast<unsigned char>(query.back()) & 0xC0) == 0x80)
            query.pop_back();
        refilter();
        return std::nullopt;
    }
    if(c == keyCode(control::ControlKey::CTRL_U))
    {
        query.clear();
        refilter();
        return std::nullopt;
    }

    if(c >= 32 && c < 256 && c != 127)
    {
        query += static_cast<char>(c);
        refilter();
    }
    return std::nullopt;
}

void CommandPaletteMode::draw(Editor& editor) const
{
    const widgets::PickerLayout layout = editor.pickerLayout(false);
    Terminal::beginCapture();
    if(layout.floating)
        editor.drawPickerBackground(layout);

    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);
    output += Terminal::ESC_HIDE_CURSOR;

    widgets::PickerCanvas canvas(output, layout, editor.theme,
                                 editor.utf8Mode);
    std::string footer;
    if(!matches.empty())
        footer = std::to_string(cursor + 1) + " / " +
                 std::to_string(matches.size());
    const auto helpLines =
        HeaderHelp::wrap(paletteHelpTokens(), layout.headerCols);
    canvas.frame("Command Palette", footer, 1 + (int)helpLines.size());

    int headerRow = 0;
    canvas.beginHeaderRow(headerRow++);
    output += Terminal::ESC_BOLD;
    output += "  Command: ";
    output += editor.theme.reset();
    output += editor.theme.uiPrompt();
    output += query;
    output += Terminal::ESC_BLINK;
    output += "_";
    output += Terminal::ESC_BLINK_OFF;
    int promptWidth = 12 + text_utils::utf8DisplayWidth(query);
    if(query.empty())
    {
        // Placeholder: names and descriptions are both searchable.
        static const std::string placeholder =
            " Type a command or what it does\xE2\x80\xA6";
        output += editor.theme.uiDim();
        output += placeholder;
        promptWidth += text_utils::utf8DisplayWidth(placeholder);
    }
    canvas.endHeaderRow(promptWidth);
    for(const auto& help : helpLines)
    {
        canvas.beginHeaderRow(headerRow++);
        output += editor.theme.uiDim();
        output += help;
        canvas.endHeaderRow(text_utils::utf8DisplayWidth(help));
    }

    const int rows = canvas.bodyRows();
    if(cursor < offset)
        offset = cursor;
    else if(cursor >= offset + rows)
        offset = cursor - rows + 1;

    // Command names in a column as wide as the longest visible label (up to
    // a third of the pane), descriptions after it.
    const int cols = layout.cols;
    int labelWidth = 0;
    for(int i = 0; i < rows && i + offset < (int)matches.size(); ++i)
        labelWidth = std::max(
            labelWidth, text_utils::utf8DisplayWidth(
                            entries[matches[i + offset].entry].label()));
    labelWidth = std::min(labelWidth, std::max(8, cols / 3));

    for(int i = 0; i < rows; ++i)
    {
        canvas.beginRow(i);
        const int index = i + offset;
        if(index >= (int)matches.size())
        {
            canvas.endRow(0);
            continue;
        }
        const command_palette::Match& match = matches[index];
        const command_palette::Entry& entry = entries[match.entry];
        const bool selected = index == cursor;
        const std::string base =
            selected ? editor.theme.selection() : editor.theme.reset();

        output += base;
        output += editor.theme.uiInfo();
        output += "  ";
        int used = 2;

        // Label with the matched characters highlighted.
        std::string label = entry.label();
        if(text_utils::utf8DisplayWidth(label) > labelWidth)
            label = clipToWidth(label, labelWidth);
        size_t next = 0;
        for(size_t pos = 0; pos < label.size(); ++pos)
        {
            const bool hit = next < match.positions.size() &&
                             match.positions[next] == (int)pos;
            if(hit)
            {
                ++next;
                output += editor.theme.matchHighlight();
                output += label[pos];
                output += base;
                output += editor.theme.uiInfo();
            }
            else
            {
                output += label[pos];
            }
        }
        const int labelShown = text_utils::utf8DisplayWidth(label);
        used += labelShown;
        output.append(labelWidth - labelShown + 2, ' ');
        used += labelWidth - labelShown + 2;

        const int descWidth = std::max(0, cols - used - 1);
        std::string description = entry.description;
        if(text_utils::utf8DisplayWidth(description) > descWidth)
            description = clipToWidth(description, descWidth);
        output += selected ? editor.theme.baseFg() : editor.theme.uiDim();
        output += description;
        used += text_utils::utf8DisplayWidth(description);
        canvas.endRow(used);
    }

    widgets::ViewStatusBarView status{.theme = editor.theme};
    status.modeLabel = "PALETTE";
    status.context = matches.empty() && !query.empty()
                         ? std::string("No matches")
                         : std::to_string(matches.size()) +
                               (matches.size() == 1 ? " command" : " commands");
    status.index = matches.empty() ? 0 : cursor + 1;
    status.count = static_cast<int>(matches.size());
    editor.appendViewStatusBar(output, std::move(status));

    const bool syncOutput = Terminal::useSynchronizedOutput();
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
    Terminal::write(output);
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
    Terminal::endCapture();
    Terminal::flush();
}
} // namespace editor::statemachine
