#include "command_palette_mode.h"
#include "editor_utils.h"

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
    return {
        "[Enter: run]",
        "[Esc: cancel]",
        "[Ctrl+J/K: navigate]",
        "[Ctrl+U: clear]",
        "[\xE2\x80\xA6: asks for an argument]"};
}

// Clips UTF-8 `text` to `width` columns (one per code point).
std::string clipToWidth(const std::string& text, int width)
{
    std::string out;
    int used = 0;
    for(size_t i = 0; i < text.size() && used < width;)
    {
        const auto lead = static_cast<unsigned char>(text[i]);
        const size_t len = lead < 0xC0   ? 1
                           : lead < 0xE0 ? 2
                           : lead < 0xF0 ? 3
                                         : 4;
        out.append(text, i, std::min(len, text.size() - i));
        ++used;
        i += len;
    }
    return out;
}

// Feeds `keys` to `mode` as if typed. They are queued in the terminal key
// buffer first: some bindings read their next key themselves (Space g r).
// Only the queued keys are used; keys the user typed ahead stay queued.
template <typename Mode>
std::optional<ModeState> pressKeys(
    Mode& mode,
    ModeContext& ctx,
    const std::vector<int>& keys)
{
    const int alreadyQueued = Terminal::bufferedKeyCount();
    for(auto key = keys.rbegin(); key != keys.rend(); ++key)
        Terminal::unreadKey(*key);
    std::optional<ModeState> next;
    while(!next && Terminal::bufferedKeyCount() > alreadyQueued)
        next = mode.handle(ctx, ModeKeyEvent{Terminal::readKeyTimeout(0)});
    while(Terminal::bufferedKeyCount() > alreadyQueued)
        (void)Terminal::readKeyTimeout(0);
    return next;
}

ModeState leave(ModeContext& ctx)
{
    return ctx.hasBuffer() ? ModeState{NormalMode{}} : ModeState{WelcomeMode{}};
}
} // namespace

void CommandPaletteMode::on_enter(ModeContext& ctx)
{
    // Named actions first, then the ex commands no action covers.
    entries = command_palette::builtinActions();
    command_palette::addExCommands(
        entries,
        ctx.getCommandCompletions(""),
        [](std::string_view command)
        { return std::string(widgets::commandDocumentation(command)); });
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

std::optional<ModeState> CommandPaletteMode::handle(
    ModeContext& ctx,
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
        const command_palette::Entry& entry = entries[matches[cursor].entry];
        if(entry.takesArgument)
        {
            // ":e " with the cursor after it, ready for the file name.
            CommandMode commandLine;
            commandLine.initialInput = entry.names.front() + " ";
            return commandLine;
        }
        if(!entry.keys.empty())
        {
            // Press the action's keys, as in normal mode (or on the welcome
            // screen when no buffer is open).
            ctx.commandBuffer.clear();
            std::optional<ModeState> next;
            if(ctx.hasBuffer())
            {
                NormalMode normal;
                next = pressKeys(normal, ctx, entry.keys);
            }
            else
            {
                WelcomeMode welcome;
                next = pressKeys(welcome, ctx, entry.keys);
            }
            ctx.commandBuffer.clear();
            return next ? next : std::optional<ModeState>(leave(ctx));
        }
        // Run it exactly like typing ":<command>" and Enter, including the
        // views some commands open (:help, :tasks, :git log, ...).
        ctx.commandBuffer = ":" + entry.names.front();
        CommandMode runner;
        std::optional<ModeState> next =
            runner
                .handle(ctx, ModeKeyEvent{keyCode(control::ControlKey::ENTER)});
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

    widgets::PickerCanvas canvas(output, layout, editor.theme, editor.utf8Mode);
    std::string footer;
    if(!matches.empty())
        footer =
            std::to_string(cursor + 1) + " / " + std::to_string(matches.size());
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
            " Type what you want to do\xE2\x80\xA6";
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

    // Title on the left (query letters highlighted), the key or command
    // that does the same dimmed on the right.
    const int cols = layout.cols;
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

        std::string shortcut = entry.shortcut;
        const int shortcutMax = std::max(0, cols / 3);
        if(text_utils::utf8DisplayWidth(shortcut) > shortcutMax)
            shortcut = clipToWidth(shortcut, shortcutMax);
        const int shortcutWidth = text_utils::utf8DisplayWidth(shortcut);
        const int titleMax = std::max(1, cols - 2 - shortcutWidth - 3);
        std::string title = entry.title;
        if(text_utils::utf8DisplayWidth(title) > titleMax)
            title = clipToWidth(title, titleMax);

        output += base;
        output += editor.theme.baseFg();
        output += "  ";
        size_t next = 0;
        for(size_t pos = 0; pos < title.size(); ++pos)
        {
            const bool hit = next < match.positions.size() &&
                             match.positions[next] == (int)pos;
            if(hit)
            {
                ++next;
                output += editor.theme.matchHighlight();
                output += title[pos];
                output += base;
                output += editor.theme.baseFg();
            }
            else
            {
                output += title[pos];
            }
        }
        const int titleWidth = text_utils::utf8DisplayWidth(title);
        int used = 2 + titleWidth;
        const int gap = std::max(1, cols - used - shortcutWidth - 1);
        output.append(gap, ' ');
        used += gap;
        output += selected ? editor.theme.uiInfo() : editor.theme.uiDim();
        output += shortcut;
        used += shortcutWidth;
        canvas.endRow(used);
    }

    widgets::ViewStatusBarView status{.theme = editor.theme};
    status.modeLabel = "PALETTE";
    status.context = matches.empty() && !query.empty()
                         ? std::string("No matches")
                         : std::to_string(matches.size()) +
                               (matches.size() == 1 ? " command" : " commands");
    if(const auto error = editor::helper::regexQueryError(query);
       !error.empty())
        status.context = error;
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
