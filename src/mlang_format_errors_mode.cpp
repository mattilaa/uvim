#include "mlang_format_errors_mode.h"
#include "editor.h"
#include "mlang_format_errors.h"
#include "mode_state_machine.h"
#include "terminal.h"

#include <algorithm>
#include <string>
#include <vector>

namespace editor::statemachine
{
namespace
{
int visibleRows(const Editor& editor)
{
    return std::max(1, editor.screenRows - 3);
}

void clampView(const Editor& editor, int& cursor, int& offset)
{
    const int total = (int)editor.mlangFormatErrors.size();
    if(total <= 0)
    {
        cursor = 0;
        offset = 0;
        return;
    }
    cursor = std::clamp(cursor, 0, total - 1);
    const int rows = visibleRows(editor);
    if(cursor < offset)
        offset = cursor;
    if(cursor >= offset + rows)
        offset = cursor - rows + 1;
    offset = std::clamp(offset, 0, std::max(0, total - rows));
}

bool isWarningSource(MlangFormatErrorsSource source)
{
    return source == MlangFormatErrorsSource::LspWarnings;
}

std::string collectingMessage(MlangFormatErrorsSource source)
{
    switch(source)
    {
    case MlangFormatErrorsSource::FormatErrors:
        return "Collecting mlang format errors...";
    case MlangFormatErrorsSource::LspErrors:
        return "Collecting LSP errors...";
    case MlangFormatErrorsSource::LspWarnings:
        return "Collecting LSP warnings...";
    }
    return "Collecting diagnostics...";
}

std::vector<MlangFormatErrorEntry> collectForSource(Editor& editor,
                                                    MlangFormatErrorsSource source)
{
    switch(source)
    {
    case MlangFormatErrorsSource::FormatErrors:
        return collectMlangFormatErrors(editor);
    case MlangFormatErrorsSource::LspErrors:
        return collectActiveLspDiagnostics(editor, 1);
    case MlangFormatErrorsSource::LspWarnings:
        return collectActiveLspDiagnostics(editor, 2);
    }
    return {};
}

std::string jumpPrefix(MlangFormatErrorsSource source)
{
    switch(source)
    {
    case MlangFormatErrorsSource::FormatErrors:
        return "mlang format error -> ";
    case MlangFormatErrorsSource::LspErrors:
        return "lsp error -> ";
    case MlangFormatErrorsSource::LspWarnings:
        return "lsp warning -> ";
    }
    return "diagnostic -> ";
}

std::string headerForSource(MlangFormatErrorsSource source)
{
    switch(source)
    {
    case MlangFormatErrorsSource::FormatErrors:
        return " Mlang Format Errors (";
    case MlangFormatErrorsSource::LspErrors:
        return " LSP Errors (";
    case MlangFormatErrorsSource::LspWarnings:
        return " LSP Warnings (";
    }
    return " Diagnostics (";
}
} // namespace

void MlangFormatErrorsMode::on_enter(ModeContext& ctx)
{
    Editor* ed = ctx.editor;
    warnings = isWarningSource(source);
    ed->setStatusMessage(collectingMessage(source));
    ed->mlangFormatErrors = collectForSource(*ed, source);
    cursor = 0;
    offset = 0;
    clampView(*ed, cursor, offset);
    ctx.requestFullRedraw();
}

void MlangFormatErrorsMode::on_exit(ModeContext& /* ctx */)
{
    Terminal::setCursorBlock();
}

std::optional<ModeState>
MlangFormatErrorsMode::handle(ModeContext& ctx, const ModeKeyEvent& event)
{
    Editor* ed = ctx.editor;
    const int c = keyCode(event.key);
    const int total = (int)ed->mlangFormatErrors.size();

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(typed::TypedKey::KEY_Q))
    {
        if(c == keyCode(control::ControlKey::ESC))
            ed->noteDoubleEscStatusClear();
        return defaultExitMode(ed);
    }

    if(c == keyCode(control::ControlKey::ENTER) ||
       c == keyCode(typed::TypedKey::KEY_L) ||
       c == keyCode(navigation::NavigationKey::ARROW_RIGHT))
    {
        if(cursor >= 0 && cursor < total)
        {
            const auto& entry = ed->mlangFormatErrors[cursor];
            ed->pushJumpLocation();
            ed->openFile(entry.path);
            if(ed->cursorY && ed->cursorX && ed->lines && !ed->lines->empty())
            {
                *ed->cursorY =
                    std::clamp(entry.line, 0, (int)ed->lines->size() - 1);
                *ed->cursorX =
                    std::clamp(entry.col, 0,
                               (int)(*ed->lines)[*ed->cursorY].size());
                ed->adjustViewport();
            }
            ed->setStatusMessage(jumpPrefix(source) + entry.displayPath + ":" +
                                 std::to_string(entry.line + 1));
            return NormalMode{};
        }
        return std::nullopt;
    }

    if(c == keyCode(typed::TypedKey::KEY_R))
    {
        ed->mlangFormatErrors = collectForSource(*ed, source);
        cursor = 0;
        offset = 0;
    }
    else if(c == keyCode(typed::TypedKey::KEY_J) ||
            c == keyCode(control::ControlKey::CTRL_N) ||
            c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        if(cursor < total - 1)
            ++cursor;
    }
    else if(c == keyCode(typed::TypedKey::KEY_K) ||
            c == keyCode(control::ControlKey::CTRL_P) ||
            c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        if(cursor > 0)
            --cursor;
    }
    else if(c == keyCode(control::ControlKey::CTRL_D) ||
            c == keyCode(navigation::NavigationKey::PAGE_DOWN))
    {
        cursor = std::min(std::max(0, total - 1),
                          cursor + std::max(1, visibleRows(*ed) / 2));
    }
    else if(c == keyCode(control::ControlKey::CTRL_U) ||
            c == keyCode(navigation::NavigationKey::PAGE_UP))
    {
        cursor = std::max(0, cursor - std::max(1, visibleRows(*ed) / 2));
    }
    else if(c == keyCode(typed::TypedKey::KEY_G))
    {
        int nextChar = Terminal::readKey();
        if(nextChar == keyCode(typed::TypedKey::KEY_G))
            cursor = 0;
    }
    else if(c == keyCode(typed::TypedKey::KEY_CAP_G))
    {
        cursor = std::max(0, total - 1);
    }

    clampView(*ed, cursor, offset);
    ed->needsFullRedraw = true;
    return std::nullopt;
}

void MlangFormatErrorsMode::draw(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::cursorPos(1, 1);
    output += editor.theme.reset();

    output += editor.theme.panel();
    std::string header = headerForSource(source);
    header += std::to_string(editor.mlangFormatErrors.size()) + ")";
    if((int)header.length() < editor.screenCols)
        header += std::string(editor.screenCols - header.length(), ' ');
    output += header;
    output += editor.theme.reset();
    output += "\r\n";

    int row = 0;
    const int rows = visibleRows(editor);
    for(int idx = offset; row < rows && idx < (int)editor.mlangFormatErrors.size();
        ++idx, ++row)
    {
        const auto& entry = editor.mlangFormatErrors[idx];
        const bool selected = idx == cursor;
        output += Terminal::ESC_CLEAR_LINE;
        if(selected)
            output += editor.theme.selection();

        std::string location = std::to_string(entry.line + 1);
        if(entry.col > 0)
            location += ":" + std::to_string(entry.col + 1);

        output += " ";
        output += editor.theme.uiInfo();
        if(selected)
            output += editor.theme.selection();
        output += entry.displayPath;
        output += editor.theme.reset();
        if(selected)
            output += editor.theme.selection();

        output += ":";
        output += editor.theme.uiWarning();
        if(selected)
            output += editor.theme.selection();
        output += location;
        output += editor.theme.reset();
        if(selected)
            output += editor.theme.selection();

        if(!entry.rangeText.empty())
        {
            output += " ";
            output += editor.theme.uiWarning();
            if(selected)
                output += editor.theme.selection();
            output += entry.rangeText;
            output += editor.theme.reset();
            if(selected)
                output += editor.theme.selection();
        }

        output += "  ";
        output += editor.theme.baseFg();
        if(selected)
            output += editor.theme.selection();
        std::string message = entry.message;
        const int used = 3 + (int)entry.displayPath.size() +
                         (int)location.size() + (int)entry.rangeText.size();
        const int maxMessage = std::max(0, editor.screenCols - used - 4);
        if((int)message.size() > maxMessage && maxMessage > 3)
            message = message.substr(0, maxMessage - 3) + "...";
        output += message;

        if(selected)
            output += editor.theme.reset();
        output += "\r\n";
    }

    for(; row < rows; ++row)
    {
        output += Terminal::ESC_CLEAR_LINE;
        output += editor.theme.uiGutter();
        output += "~";
        output += editor.theme.reset();
        output += "\r\n";
    }

    output += editor.theme.statusBar();
    const int displayCursor = editor.mlangFormatErrors.empty() ? 0 : cursor + 1;
    std::string status = " [" + std::to_string(displayCursor) + "/" +
                         std::to_string(editor.mlangFormatErrors.size()) + "]";
    status += " <Enter> jump  <r> refresh  <q/Esc> close  <j/k> navigate";
    if((int)status.length() < editor.screenCols)
        status += std::string(editor.screenCols - status.length(), ' ');
    output += status;
    output += editor.theme.reset();

    output += "\r\n";
    output += Terminal::ESC_CLEAR_LINE;
    if(!editor.statusMessage.empty())
        output += editor.statusMessage;

    Terminal::write(output);
    Terminal::flush();
}
} // namespace editor::statemachine
