#include "ascii.h"
#include "editor.h"
#include "editor_utils.h"
#include "header_help.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <sstream>

// ============================================================================
// Help Mode Implementation
// ============================================================================

namespace editor::statemachine
{
namespace
{
std::vector<std::string> helpModeHelpTokens()
{
    return {
        "[Enter: open]",
        "[q: quit]",
        "[j/k: navigate]",
        "[gg/G: top/bottom]",
        "[/: fuzzy help]",
        "[Ctrl-I: doc rows]",
        "[:help <topic>]"};
}

int helpModeContentRows(const Editor& editor)
{
    const int headerRows =
        1 + HeaderHelp::lineCount(helpModeHelpTokens(), editor.screenCols);
    // screenRows already excludes the status and message rows.
    return std::max(1, editor.screenRows - headerRows);
}

int helpModeContentRows(const ModeContext& ctx)
{
    const int headerRows =
        1 + HeaderHelp::lineCount(helpModeHelpTokens(), ctx.screenCols());
    // screenRows already excludes the status and message rows.
    return std::max(1, ctx.screenRows() - headerRows);
}

std::vector<std::string> helpSearchTopics()
{
    return {
        "",
        "commands",
        "modes",
        "navigation",
        "editing",
        "files",
        "filebrowser",
        "run",
        "buffers",
        "windows",
        "search",
        "regex",
        "clipboard",
        "git",
        "ga",
        "gb",
        "gbb",
        "gbl",
        "gj",
        "gbv",
        "gs",
#ifdef UVIM_ENABLE_DEBUGGER
        "dbgrun",
        "dbgattach",
#endif
#ifdef UVIM_ENABLE_ASM_VIEW
        "asm",
#endif
        "emitasm",
        "lsp",
        "toolinfo",
        "tasks",
        "diagnostics",
        "logging",
        "keys",
        "palette",
        "functions"};
}

int helpSearchVisibleRows(const Editor& editor)
{
    // screenRows already excludes the status and message rows.
    constexpr int headerRows = 1;
    return std::max(1, editor.screenRows - headerRows);
}

int helpSearchVisibleMatches(const Editor& editor)
{
    return std::max(1, helpSearchVisibleRows(editor) - 1);
}

std::string trimSearchDisplayLine(std::string line)
{
    while(!line.empty() && text_utils::is_space(line.front()))
        line.erase(line.begin());
    while(!line.empty() && text_utils::is_space(line.back()))
        line.pop_back();
    if(!line.empty() && line.front() == '#')
    {
        line.erase(line.begin());
        while(!line.empty() && text_utils::is_space(line.front()))
            line.erase(line.begin());
    }
    return line;
}

std::string singleLineHelpPasteText(std::string text)
{
    text.erase(
        std::remove_if(
            text.begin(),
            text.end(),
            [](char ch) { return ch == '\n' || ch == '\r'; }),
        text.end());
    return text;
}

std::string helpTopicLabel(const std::string& topic)
{
    return topic.empty() ? ":help" : ":help " + topic;
}

std::string helpTopicDescription(const std::string& topic)
{
    if(topic.empty())
        return "Main help index";
    if(topic == "commands")
        return "List of all commands";
    if(topic == "modes")
        return "Editor modes";
    if(topic == "navigation")
        return "Moving around";
    if(topic == "functions")
        return "Browse function signatures in the current buffer";
    if(topic == "editing")
        return "Editing text";
    if(topic == "files")
        return "File operations";
    if(topic == "filebrowser")
        return "File browser keys and commands";
    if(topic == "run")
        return ":run command and output view";
    if(topic == "buffers")
        return "Buffer management and tab bar";
    if(topic == "windows")
        return "Splits and tabs";
    if(topic == "search")
        return "Searching and replacing";
    if(topic == "regex")
        return "Regex search view";
    if(topic == "clipboard")
        return "Clipboard operations";
    if(topic == "git")
        return "Git integrations";
    if(topic == "ga")
        return "Git stage view";
    if(topic == "gb")
        return "Git blame";
    if(topic == "gbb")
        return "Git blame with date/time";
    if(topic == "gbl")
        return "Git log for blamed commit";
    if(topic == "gj")
        return "Show git commit diff";
    if(topic == "gbv")
        return "Show blamed commit diff";
    if(topic == "gs")
        return "C/C++ symbol size popup";
#ifdef UVIM_ENABLE_ASM_VIEW
    if(topic == "asm")
        return "Compile and browse C/C++ or Mlang assembly";
#endif
#ifdef UVIM_ENABLE_DEBUGGER
    if(topic == "dbgrun" || topic == "dbgattach")
        return "Launch or attach a native debugger";
#endif
    if(topic == "emitasm")
        return "Emit C/C++ assembly";
    if(topic == "lsp")
        return "LSP setup and troubleshooting";
    if(topic == "toolinfo")
        return "External tool setup";
    if(topic == "keys")
        return "Ctrl+Shift keys, Alt alternatives, :set iskitty";
    if(topic == "palette")
        return "Command palette (Space Space): search and run commands";
    if(topic == "tasks")
        return "Background tasks and progress";
    if(topic == "diagnostics")
        return "Diagnostic env vars";
    if(topic == "logging")
        return "Logging";
    return "Help topic";
}

std::string helpLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    return value;
}

bool isMainHelpTopic(const std::string& topic)
{
    const std::string lowered = helpLower(topic);
    return lowered.empty() || lowered == "index" || lowered == "help";
}

std::string helpDisplayLine(const std::string& line, bool preserveCodeColumns)
{
    std::string out;
    const bool isHeading =
        !line.empty() && line[0] == keyCode(command::CommandKey::KEY_HASH);
    const size_t start = isHeading ? 1 : 0;
    out.reserve(line.size());
    for(size_t i = start; i < line.size(); ++i)
    {
        const char ch = line[i];
        if(ch == keyCode(command::CommandKey::KEY_BACKTICK))
        {
            if(preserveCodeColumns)
                out += ' ';
        }
        else
        {
            out += ch;
        }
    }
    return out;
}

bool helpPreservesCodeColumns(const std::string& line)
{
    size_t rowStart = 0;
    while(rowStart < line.size() &&
          (line[rowStart] == ' ' || line[rowStart] == '\t'))
        ++rowStart;
    return rowStart < line.size() &&
           line[rowStart] == keyCode(command::CommandKey::KEY_BACKTICK) &&
           text_utils::is_found(line.find(" - ", rowStart));
}
} // namespace

void HelpMode::on_enter(ModeContext& ctx)
{
    commandPrompt = ctx.commandPrompt();
    if(previousFile.empty() && ctx.hasCurrentBuffer() && ctx.hasFilename())
    {
        previousFile = std::string(ctx.currentFilename());
    }

    loadHelpContent(topic);
    if(isMainHelpTopic(topic) &&
       topicForLine(selectedLine).has_value() == false)
    {
        for(int i = 0; i < (int)lines.size(); ++i)
        {
            if(topicForLine(i).has_value())
            {
                selectedLine = i;
                break;
            }
        }
    }
    ctx.requestFullRedraw();
}

void HelpMode::on_exit(ModeContext& /* ctx */) {}

std::optional<ModeState> HelpMode::handle(
    ModeContext& ctx,
    const ModeKeyEvent& event)
{
    const int key = event.key;
    int c = keyCode(key);
    bool needsRedraw = false;

    std::optional<ModeState> nextState;
    if(commandPrompt &&
       (commandPrompt->isActive() ||
        (!searchActive && c == keyCode(command::CommandKey::KEY_COLON))) &&
       commandPrompt->handle(
           ctx,
           c,
           [&](std::string_view commandLine)
           { return executeCommand(ctx, commandLine); },
           nextState))
    {
        return nextState;
    }

    if(searchActive)
    {
        if(c == keyCode(control::ControlKey::ESC))
        {
            ctx.editor->noteDoubleEscStatusClear();
            cancelSearch(*ctx.editor);
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::ENTER))
        {
            acceptSearch(*ctx.editor);
            return std::nullopt;
        }
        if(c == keyCode(typed::TypedKey::KEY_Q) && searchQuery.empty())
        {
            cancelSearch(*ctx.editor);
            return std::nullopt;
        }
        if(c == keyCode(control::ControlKey::CTRL_N) ||
           c == keyCode(control::ControlKey::CTRL_J) ||
           c == keyCode(navigation::NavigationKey::ARROW_DOWN))
        {
            searchMoveDown(*ctx.editor);
        }
        else if(
            c == keyCode(control::ControlKey::CTRL_P) ||
            c == keyCode(control::ControlKey::CTRL_K) ||
            c == keyCode(navigation::NavigationKey::ARROW_UP))
        {
            searchMoveUp();
        }
        else if(
            c == keyCode(control::ControlKey::CTRL_D) ||
            c == keyCode(navigation::NavigationKey::PAGE_DOWN))
        {
            searchHalfPageDown(*ctx.editor);
        }
        else if(c == keyCode(control::ControlKey::CTRL_U))
        {
            searchQuery.clear();
            updateSearchMatches(*ctx.editor);
        }
        else if(c == keyCode(control::ControlKey::CTRL_I))
        {
            searchDocumentation = !searchDocumentation;
            updateSearchMatches(*ctx.editor);
            ctx.setStatusMessage(
                std::string("Help documentation search ") +
                (searchDocumentation ? "on" : "off"));
        }
        else if(c == keyCode(navigation::NavigationKey::PAGE_UP))
        {
            searchHalfPageUp(*ctx.editor);
        }
        else if(
            c == keyCode(control::ControlKey::BACKSPACE) || c == 127 ||
            c == keyCode(control::ControlKey::CTRL_H))
        {
            if(!searchQuery.empty())
            {
                searchQuery.pop_back();
                updateSearchMatches(*ctx.editor);
            }
        }
        else if(c == keyCode(control::ControlKey::CTRL_W))
        {
            while(!searchQuery.empty() &&
                  searchQuery.back() == keyCode(control::ControlKey::SPACE))
                searchQuery.pop_back();
            while(!searchQuery.empty() &&
                  searchQuery.back() != keyCode(control::ControlKey::SPACE))
                searchQuery.pop_back();
            updateSearchMatches(*ctx.editor);
        }
        else if(c == keyCode(control::ControlKey::PASTE))
        {
            std::string text =
                singleLineHelpPasteText(Terminal::takeLastPasteText());
            if(!text.empty())
            {
                searchQuery += text;
                updateSearchMatches(*ctx.editor);
            }
        }
        else if(c >= 32 && c < 127)
        {
            if(c == keyCode(command::CommandKey::KEY_SLASH) &&
               searchQuery.empty())
            {
                ctx.requestFullRedraw();
                return std::nullopt;
            }
            searchQuery += static_cast<char>(c);
            updateSearchMatches(*ctx.editor);
        }

        ctx.requestFullRedraw();
        return std::nullopt;
    }

    // ========================================================================
    // Exit
    // ========================================================================

    if(c == keyCode(control::ControlKey::ESC) ||
       c == keyCode(typed::TypedKey::KEY_Q))
    {
        if(c == keyCode(control::ControlKey::ESC))
            ctx.editor->noteDoubleEscStatusClear();
        if(!previousFile.empty())
        {
            ctx.openFile(std::string_view(previousFile));
            return NormalMode{};
        }
        return WelcomeMode{};
    }

    // ========================================================================
    // Navigation
    // ========================================================================

    if(c == keyCode(typed::TypedKey::KEY_J) ||
       c == keyCode(navigation::NavigationKey::ARROW_DOWN))
    {
        if(moveSelection(*ctx.editor, 1))
        {
            needsRedraw = true;
        }
        else
        {
            int maxScroll =
                std::max(0, (int)lines.size() - helpModeContentRows(ctx));
            if(scrollOffset < maxScroll)
            {
                scrollOffset++;
                needsRedraw = true;
            }
        }
    }
    else if(
        c == keyCode(typed::TypedKey::KEY_K) ||
        c == keyCode(navigation::NavigationKey::ARROW_UP))
    {
        if(moveSelection(*ctx.editor, -1))
        {
            needsRedraw = true;
        }
        else if(scrollOffset > 0)
        {
            scrollOffset--;
            needsRedraw = true;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_CAP_G))
    {
        int newOffset =
            std::max(0, (int)lines.size() - helpModeContentRows(ctx));
        if(newOffset != scrollOffset)
        {
            scrollOffset = newOffset;
            needsRedraw = true;
        }
    }
    else if(c == keyCode(typed::TypedKey::KEY_G))
    {
        int nextChar = Terminal::readKey();
        if(nextChar == keyCode(typed::TypedKey::KEY_G))
        {
            if(scrollOffset != 0)
            {
                scrollOffset = 0;
                needsRedraw = true;
            }
        }
    }
    else if(c == keyCode(control::ControlKey::CTRL_D))
    {
        int half = helpModeContentRows(ctx) / 2;
        int oldOffset = scrollOffset;
        scrollOffset += half;
        int maxScroll =
            std::max(0, (int)lines.size() - helpModeContentRows(ctx));
        if(scrollOffset > maxScroll)
            scrollOffset = maxScroll;
        if(scrollOffset != oldOffset)
            needsRedraw = true;
    }
    else if(c == keyCode(control::ControlKey::CTRL_U))
    {
        int half = helpModeContentRows(ctx) / 2;
        int oldOffset = scrollOffset;
        scrollOffset -= half;
        if(scrollOffset < 0)
            scrollOffset = 0;
        if(scrollOffset != oldOffset)
            needsRedraw = true;
    }
    else if(c == keyCode(command::CommandKey::KEY_SLASH))
    {
        startSearch(*ctx.editor);
        needsRedraw = true;
    }
    else if(c == keyCode(control::ControlKey::CTRL_I))
    {
        searchDocumentation = !searchDocumentation;
        ctx.setStatusMessage(
            std::string("Help documentation search ") +
            (searchDocumentation ? "on" : "off"));
        needsRedraw = true;
    }
    else if(
        c == keyCode(control::ControlKey::ENTER) ||
        c == keyCode(typed::TypedKey::KEY_L) ||
        c == keyCode(navigation::NavigationKey::ARROW_RIGHT))
    {
        if(acceptSelection(*ctx.editor))
            needsRedraw = true;
    }

    if(needsRedraw)
        ctx.requestFullRedraw();
    return std::nullopt;
}

void HelpMode::draw(Editor& editor) const
{
    if(searchActive)
    {
        drawSearch(editor);
        return;
    }

    const bool syncOutput = Terminal::useSynchronizedOutput();
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);

    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_CURSOR_HOME;
    output += editor.theme.reset();

    // Draw header
    output += Terminal::ESC_CLEAR_LINE;
    output += editor.theme.uiAccent();
    output += Terminal::ESC_BOLD;
    output += "  HELP";
    if(!topic.empty())
    {
        output += ": " + topic;
    }
    output += editor.theme.reset();
    HeaderHelp::append(
        output,
        editor.theme,
        editor.screenCols,
        helpModeHelpTokens());

    int availableRows = helpModeContentRows(editor);

    // Draw help content
    for(int i = 0; i < availableRows && i + scrollOffset < (int)lines.size();
        i++)
    {
        output += Terminal::NEWLINE_CLEAR;

        const int lineIndex = i + scrollOffset;
        const std::string& line = lines[i + scrollOffset];
        const bool isSelected = lineIndex == selectedLine &&
                                (jumpHighlight || !isMainHelpTopic(topic) ||
                                 topicForLine(lineIndex).has_value());
        if(isSelected)
        {
            const std::string displayLine =
                helpDisplayLine(line, helpPreservesCodeColumns(line));
            output += editor.theme.selection();
            output += "  ";
            output += displayLine;
            const int usedCols = 2 + text_utils::utf8DisplayWidth(displayLine);
            if(usedCols < editor.screenCols)
                output += std::string(editor.screenCols - usedCols, ' ');
            output += editor.theme.reset();
            continue;
        }

        // Apply syntax highlighting to the line
        output += "  ";

        // Check if line is a topic title (starts with # or all caps)
        if(!line.empty() && line[0] == keyCode(command::CommandKey::KEY_HASH))
        {
            // Topic title
            output += editor.theme.syntax(TOKEN_KEYWORD);
            output += Terminal::ESC_BOLD;
            output += line.substr(1); // Skip #
            output += editor.theme.reset();
        }
        else if(
            !line.empty() && std::isupper(line[0]) &&
            text_utils::contains(
                line,
                keyChar(command::CommandKey::KEY_COLON)) &&
            line.find(keyChar(command::CommandKey::KEY_COLON)) < 30)
        {
            // Section header (e.g., "COMMANDS:")
            output += editor.theme.uiAccent();
            output += Terminal::ESC_BOLD;
            output += line;
            output += editor.theme.reset();
        }
        else
        {
            // Regular line with command highlighting
            std::string processedLine;
            size_t pos = 0;

            while(pos < line.length())
            {
                // Look for commands starting with : or starting with uppercase
                if(line[pos] == keyCode(command::CommandKey::KEY_COLON))
                {
                    // Find end of command
                    size_t end = pos + 1;
                    while(end < line.length() &&
                          (std::isalnum(line[end]) ||
                           line[end] ==
                               keyCode(command::CommandKey::KEY_EXCLAMATION) ||
                           line[end] ==
                               keyCode(command::CommandKey::KEY_QUESTION)))
                    {
                        end++;
                    }

                    // Highlight command
                    processedLine += editor.theme.syntax(TOKEN_STRING);
                    processedLine += Terminal::ESC_BOLD;
                    processedLine += line.substr(pos, end - pos);
                    processedLine += editor.theme.reset();
                    pos = end;
                }
                else if(line[pos] == keyCode(command::CommandKey::KEY_BACKTICK))
                {
                    // Code/command in backticks
                    size_t end = line.find(
                        keyChar(command::CommandKey::KEY_BACKTICK),
                        pos + 1);
                    if(text_utils::is_found(end))
                    {
                        const bool preserveCodeColumns =
                            helpPreservesCodeColumns(line);
                        if(preserveCodeColumns)
                            processedLine += ' ';
                        processedLine += editor.theme.syntax(TOKEN_FUNCTION);
                        processedLine += line.substr(pos + 1, end - pos - 1);
                        processedLine += editor.theme.reset();
                        if(preserveCodeColumns)
                            processedLine += ' ';
                        pos = end + 1;
                    }
                    else
                    {
                        processedLine += line[pos++];
                    }
                }
                else
                {
                    processedLine += line[pos++];
                }
            }

            output += processedLine;
        }

        if(isSelected)
        {
            const int usedCols = 2 + text_utils::utf8DisplayWidth(line);
            if(usedCols < editor.screenCols)
                output += std::string(editor.screenCols - usedCols, ' ');
            output += editor.theme.reset();
        }
        else
        {
            output += editor.theme.reset();
        }
    }

    // Fill remaining lines
    for(int i = std::min((int)lines.size() - scrollOffset, availableRows);
        i < availableRows;
        i++)
    {
        output += Terminal::NEWLINE_CLEAR;
        output += editor.theme.uiGutter();
        output += "  ~";
        output += editor.theme.baseFg();
    }

    // Status bar
    output += Terminal::NEWLINE_CLEAR;

    std::string status = " HELP";
    if(!topic.empty())
        status += " | " + topic;

    std::string right =
        " " + std::to_string(scrollOffset + 1) + "-" +
        std::to_string(
            std::min(scrollOffset + availableRows, (int)lines.size())) +
        "/" + std::to_string(lines.size()) + " ";

    editor.appendStatusLine(output, status, right);

    // Message line
    output += Terminal::NEWLINE_CLEAR;
    if(commandPrompt && commandPrompt->isActive())
    {
        output += ":";
        output += commandPrompt->getInput();
    }
    else if(!editor.statusMessage.empty())
    {
        output += editor.statusMessage.substr(
            0,
            std::min((size_t)editor.screenCols, editor.statusMessage.length()));
    }
    else if(!editor.locMessage.empty())
    {
        output += editor.locMessage.substr(
            0,
            std::min((size_t)editor.screenCols, editor.locMessage.length()));
    }

    editor.drawCommandHistoryPopup(output);
    editor.drawCommandPopup(output);

    if(commandPrompt && commandPrompt->isActive())
    {
        output += Terminal::ESC_SHOW_CURSOR;
        // The prompt lives on the message row, right below the status line.
        int row = editor.screenRows + 2;
        int col = 2 + (int)commandPrompt->getInput().size();
        output += Terminal::cursorPos(row, col);
    }
    else
    {
        output += Terminal::ESC_HIDE_CURSOR;
    }

    Terminal::write(output);
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
    Terminal::flush();
}

std::optional<std::string> HelpMode::topicForLine(int lineIndex) const
{
    if(lineIndex < 0 || lineIndex >= (int)lines.size())
        return std::nullopt;

    const std::string& line = lines[lineIndex];
    size_t rowStart = 0;
    while(rowStart < line.size() &&
          (line[rowStart] == ' ' || line[rowStart] == '\t'))
        ++rowStart;
    if(rowStart >= line.size() || line[rowStart] != '`')
        return std::nullopt;

    size_t pos = line.find(":help", rowStart);
    if(text_utils::is_not_found(pos))
        return std::nullopt;
    if(pos != rowStart + 1)
        return std::nullopt;

    size_t topicStart = pos + 5;
    if(topicStart >= line.size())
        return std::string{};

    if(line[topicStart] == '`' || line[topicStart] == ' ' ||
       line[topicStart] == '\t')
    {
        while(topicStart < line.size() &&
              (line[topicStart] == ' ' || line[topicStart] == '\t'))
            ++topicStart;
        if(topicStart >= line.size() || line[topicStart] == '`')
            return std::string{};
    }
    else
    {
        return std::nullopt;
    }

    size_t topicEnd = topicStart;
    while(topicEnd < line.size() && line[topicEnd] != '`' &&
          line[topicEnd] != ' ' && line[topicEnd] != '\t')
    {
        ++topicEnd;
    }

    if(topicEnd <= topicStart)
        return std::string{};
    return line.substr(topicStart, topicEnd - topicStart);
}

bool HelpMode::moveSelection(Editor& editor, int delta)
{
    if(lines.empty())
        return false;

    if(!isMainHelpTopic(topic))
    {
        selectedLine =
            std::clamp(selectedLine + delta, 0, (int)lines.size() - 1);
        const int visibleRows = helpModeContentRows(editor);
        if(selectedLine < scrollOffset)
            scrollOffset = selectedLine;
        else if(selectedLine >= scrollOffset + visibleRows)
            scrollOffset = selectedLine - visibleRows + 1;
        return true;
    }

    std::vector<int> selectable;
    for(int i = 0; i < (int)lines.size(); ++i)
    {
        if(topicForLine(i).has_value())
            selectable.push_back(i);
    }
    if(selectable.empty())
        return false;

    int pos = 0;
    for(int i = 0; i < (int)selectable.size(); ++i)
    {
        if(selectable[i] == selectedLine)
        {
            pos = i;
            break;
        }
        if(selectable[i] < selectedLine)
            pos = i;
    }

    const int next = std::clamp(pos + delta, 0, (int)selectable.size() - 1);
    selectedLine = selectable[next];

    const int visibleRows = helpModeContentRows(editor);
    if(selectedLine < scrollOffset)
        scrollOffset = selectedLine;
    else if(selectedLine >= scrollOffset + visibleRows)
        scrollOffset = selectedLine - visibleRows + 1;
    return true;
}

bool HelpMode::acceptSelection(Editor& editor)
{
    auto selectedTopic = topicForLine(selectedLine);
    if(!selectedTopic)
        return false;

    topic = *selectedTopic;
    scrollOffset = 0;
    selectedLine = 0;
    jumpHighlight = false;
    loadHelpContent(topic);
    editor.needsFullRedraw = true;
    return true;
}

void HelpMode::startSearch(Editor& editor)
{
    searchActive = true;
    searchQuery.clear();
    searchCursor = 0;
    searchOffset = 0;
    updateSearchMatches(editor);
    Terminal::setCursorBarBlinking();
    editor.needsFullRedraw = true;
}

void HelpMode::updateSearchMatches(Editor&)
{
    searchMatches.clear();
    searchCursor = 0;
    searchOffset = 0;

    std::vector<std::pair<HelpSearchMatch, int>> scored;
    std::vector<int> positions;

    for(const std::string& searchTopic : helpSearchTopics())
    {
        const std::string label = helpTopicLabel(searchTopic);
        const std::string display =
            label + " - " + helpTopicDescription(searchTopic);

        HelpSearchMatch match;
        match.topic = searchTopic;
        match.line = 0;
        match.content = display;
        match.topicOnly = true;

        if(searchQuery.empty())
        {
            match.score = 0;
            scored.emplace_back(std::move(match), (int)scored.size());
            continue;
        }

        positions.clear();
        int score = editor::helper::fuzzyScoreWithPositions(
            searchQuery,
            display,
            positions);
        if(score >= 0)
        {
            match.score = score;
            match.matchPositions = positions;
            scored.emplace_back(std::move(match), (int)scored.size());
        }
    }

    if(searchDocumentation && !searchQuery.empty())
    {
        for(const std::string& searchTopic : helpSearchTopics())
        {
            HelpMode page(searchTopic);
            page.searchDocumentation = searchDocumentation;
            page.loadHelpContent(searchTopic);

            for(int i = 0; i < (int)page.lines.size(); ++i)
            {
                if(page.topicForLine(i).has_value())
                    continue;

                std::string display = trimSearchDisplayLine(page.lines[i]);
                if(display.empty())
                    continue;

                positions.clear();
                int score = editor::helper::fuzzyScoreWithPositions(
                    searchQuery,
                    display,
                    positions);
                if(score < 0)
                    continue;

                HelpSearchMatch match;
                match.topic = searchTopic;
                match.line = i;
                match.score = score;
                match.content = display;
                match.matchPositions = positions;
                match.topicOnly = false;
                scored.emplace_back(std::move(match), (int)scored.size());
            }
        }
    }

    std::stable_sort(
        scored.begin(),
        scored.end(),
        [](const auto& left, const auto& right)
        {
            if(left.first.score != right.first.score)
                return left.first.score > right.first.score;
            return left.second < right.second;
        });

    for(auto& entry : scored)
        searchMatches.push_back(std::move(entry.first));
}

void HelpMode::cancelSearch(Editor& editor)
{
    searchActive = false;
    searchQuery.clear();
    searchMatches.clear();
    searchCursor = 0;
    searchOffset = 0;
    Terminal::setCursorBlock();
    editor.needsFullRedraw = true;
}

bool HelpMode::acceptSearch(Editor& editor)
{
    if(searchCursor < 0 || searchCursor >= (int)searchMatches.size())
        return false;

    const HelpSearchMatch match = searchMatches[searchCursor];
    searchActive = false;
    searchQuery.clear();
    searchMatches.clear();
    searchCursor = 0;
    searchOffset = 0;
    topic = match.topic;
    loadHelpContent(topic);
    const int visibleRows = helpModeContentRows(editor);
    const int maxScroll = std::max(0, (int)lines.size() - visibleRows);
    scrollOffset = std::clamp(match.line, 0, maxScroll);
    selectedLine =
        std::clamp(match.line, 0, std::max(0, (int)lines.size() - 1));
    jumpHighlight = true;
    Terminal::setCursorBlock();
    editor.needsFullRedraw = true;
    return true;
}

void HelpMode::searchMoveDown(Editor& editor)
{
    if(searchMatches.empty())
        return;

    if(searchCursor < (int)searchMatches.size() - 1)
    {
        ++searchCursor;
        const int visible = helpSearchVisibleMatches(editor);
        if(searchCursor >= searchOffset + visible)
            searchOffset = searchCursor - visible + 1;
    }
}

void HelpMode::searchMoveUp()
{
    if(searchMatches.empty())
        return;

    if(searchCursor > 0)
    {
        --searchCursor;
        if(searchCursor < searchOffset)
            searchOffset = searchCursor;
    }
}

void HelpMode::searchHalfPageDown(Editor& editor)
{
    if(searchMatches.empty())
        return;

    const int visible = helpSearchVisibleMatches(editor);
    searchCursor = std::min(
        (int)searchMatches.size() - 1,
        searchCursor + std::max(1, visible / 2));
    if(searchCursor >= searchOffset + visible)
        searchOffset = searchCursor - visible + 1;
}

void HelpMode::searchHalfPageUp(Editor& editor)
{
    if(searchMatches.empty())
        return;

    const int visible = helpSearchVisibleMatches(editor);
    searchCursor = std::max(0, searchCursor - std::max(1, visible / 2));
    if(searchCursor < searchOffset)
        searchOffset = searchCursor;
}

void HelpMode::drawSearch(Editor& editor) const
{
    std::string output;
    output.reserve(editor.screenRows * editor.screenCols * 2);

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::cursorPos(1, 1);
    output += editor.theme.reset();

    output += editor.theme.panel();
    const std::string headerPrefix =
        " Help Search (" + std::to_string(searchMatches.size()) + ") /";
    std::string header = headerPrefix + searchQuery;
    if((int)header.length() < editor.screenCols)
        header += std::string(editor.screenCols - header.length(), ' ');
    else if((int)header.length() > editor.screenCols)
        header = header.substr(0, std::max(0, editor.screenCols - 3)) + "...";
    output += header;
    output += editor.theme.reset();
    output += "\r\n";

    const int visibleRows = helpSearchVisibleRows(editor);
    bool drewGroupHeader = false;
    int row = 0;
    int idx = searchOffset;

    while(row < visibleRows && idx < (int)searchMatches.size())
    {
        const auto& match = searchMatches[idx];
        if(!drewGroupHeader)
        {
            output += Terminal::ESC_CLEAR_LINE;
            output += editor.theme.uiInfo();
            output += Terminal::ESC_BOLD;
            output += " Help Topics";
            output += editor.theme.reset();
            output += "\r\n";
            drewGroupHeader = true;
            ++row;
            if(row >= visibleRows)
                break;
        }

        output += Terminal::ESC_CLEAR_LINE;
        const bool isSelected = idx == searchCursor;
        if(isSelected)
            output += editor.theme.selection();

        std::string prefix = helpTopicLabel(match.topic);
        if(!match.topicOnly)
            prefix += ":" + std::to_string(match.line + 1);

        output += editor.theme.uiInfo();
        output += Terminal::ESC_BOLD;
        output += " " + prefix;
        output += editor.theme.reset();

        std::string content = match.content;
        const int prefixWidth = 1 + text_utils::utf8DisplayWidth(prefix) + 2;
        const int maxContentLen = std::max(1, editor.screenCols - prefixWidth);
        if((int)content.length() > maxContentLen)
            content = content.substr(0, std::max(0, maxContentLen - 3)) + "...";

        if(isSelected)
            output += editor.theme.selection();
        output += "  ";
        output += editor.theme.baseFg();
        if(!searchQuery.empty() && !match.matchPositions.empty())
        {
            size_t lastPos = 0;
            for(int pos : match.matchPositions)
            {
                if(pos < 0 || pos >= (int)content.size())
                    continue;
                if((size_t)pos > lastPos)
                    output += content.substr(lastPos, pos - lastPos);
                if(!isSelected)
                    output += editor.theme.matchHighlight();
                output += content[pos];
                if(!isSelected)
                    output += editor.theme.baseFg();
                lastPos = (size_t)pos + 1;
            }
            if(lastPos < content.size())
                output += content.substr(lastPos);
        }
        else
        {
            output += content;
        }

        if(isSelected)
            output += editor.theme.reset();

        const int usedCols =
            prefixWidth + text_utils::utf8DisplayWidth(content);
        if(usedCols < editor.screenCols)
            output += std::string(editor.screenCols - usedCols, ' ');
        output += "\r\n";
        ++row;
        ++idx;
    }

    for(; row < visibleRows; ++row)
    {
        output += Terminal::ESC_CLEAR_LINE;
        output += "~\r\n";
    }

    std::string status;
    if(searchMatches.empty())
    {
        status = " [0/0] type to search  <Esc> close";
    }
    else
    {
        status = " [" + std::to_string(searchCursor + 1) + "/" +
                 std::to_string(searchMatches.size()) +
                 "] <Enter> jump  <Esc> close  <j/k> navigate  <Ctrl-I> docs:";
        status += searchDocumentation ? "on" : "off";
    }
    if(const auto error = editor::helper::regexQueryError(searchQuery);
       !error.empty())
        status = error;
    editor.appendStatusLine(output, status, "");

    output += "\r\n";
    output += Terminal::ESC_CLEAR_LINE;
    if(!editor.statusMessage.empty())
        output += editor.statusMessage.substr(
            0,
            std::min((size_t)editor.screenCols, editor.statusMessage.length()));

    output += Terminal::ESC_SHOW_CURSOR;
    output += Terminal::cursorPos(
        1,
        std::min(
            editor.screenCols,
            (int)headerPrefix.size() + (int)searchQuery.size() + 1));

    const bool syncOutput = Terminal::useSynchronizedOutput();
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
    Terminal::write(output);
    if(syncOutput)
        Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
    Terminal::flush();
}

void HelpMode::loadHelpContent(const std::string& helpTopic)
{
    lines.clear();
    jumpHighlight = false;

    // Convert topic to lowercase for matching
    std::string topic_lower = helpTopic;
    while(!topic_lower.empty() && text_utils::is_space(topic_lower.front()))
        topic_lower.erase(topic_lower.begin());
    while(!topic_lower.empty() && text_utils::is_space(topic_lower.back()))
        topic_lower.pop_back();
    std::transform(
        topic_lower.begin(),
        topic_lower.end(),
        topic_lower.begin(),
        ::tolower);

#ifdef UVIM_ENABLE_DEBUGGER
    if(topic_lower == "dbgrun" || topic_lower == "dbgattach")
    {
        lines = {
            "# Debugger",
            "",
            "`:dbgrun path/to/binary [arguments]` launches and stops at entry.",
            "`:dbgattach PID` attaches to a running process.",
            "`:dbgattach re:` opens live process regex filtering without --.",
            "`re:^worker_[0-9]+$` matches names; `re:^[0-9]{4}$` matches PIDs.",
            "Use [A-Za-z0-9] for letters/digits; commas/spaces are literal.",
            "`:dbgattach --` opens the process picker; type name/PID to "
            "filter.",
            "Ctrl-J/K or arrows select; Enter attaches; Tab inserts the PID.",
            "Esc closes the picker, Ctrl-R refreshes, Ctrl-U clears its query.",
            "`:dbgattach --backend gdb -- QUERY` selects a backend first.",
            "Add `--backend lldb|gdb|mladbg` before the binary or PID.",
            "Quote executable paths and arguments containing spaces.",
            "Build with -g -O0 for source and local variable information.",
            "",
            "Source and output are shown in their own panes. At a stop,",
            "variables and threads appear on the right. Small terminals",
            "show the focused pane alone.",
            "",
            "`j/k` selects rows. Tab selects the next pane; Shift-Tab the "
            "previous.",
            "v focuses Variables; t focuses Threads.",
            "Attaching in sleep selects a readable application caller.",
            "The source title identifies it; :frame 0 shows the stop frame.",
            "Use :up/:down to inspect other frames.",
            "Type :frame -- to open a fuzzy picker of the selected thread's "
            "frames.",
            "Filter by number, symbol, file, or address; Ctrl-J/K selects.",
            "Enter selects the frame; Tab inserts its ID without selecting.",
            "* marks the current frame. Ctrl-R refreshes; Ctrl-U clears the "
            "filter.",
            "Esc returns to the frame prompt. Lists up to 256 frames while "
            "paused.",
            "Frames without readable local source are dimmed and marked [no "
            "source].",
            "Highlighting previews the file and target row in Source before "
            "Enter.",
            "Ctrl-D toggles source/library assembly at the highlighted frame's "
            "PC.",
            "Previewing does not select a frame or change its variables.",
            "Esc restores the original source view.",
            "Enter on a thread selects it and refreshes source and locals.",
            "In Variables: p/Enter prints, a disassembles a pointer/address,",
            "m shows raw storage bytes; s shows total size and padding.",
            "Size inspection includes member sizes and nested layouts (i "
            "aliases s).",
            "Outside Variables, s still steps into code.",
            "Printing local variables includes storage/layout information.",
            "Size/layout opens a popup: j/k or Ctrl-J/K scrolls, PgUp/PgDn "
            "pages.",
            "g/G goes first/last; Esc/Enter/q closes and preserves variable "
            "focus.",
            "Values, assembly, and bytes use Output. Function size is code "
            "bytes.",
            "Press v to return to Variables; j/k scrolls Output rows.",
            "`:disasm callback` or `:disasm 0xADDRESS` reads instructions.",
            "`:disasm` alone uses the selected frame's program counter.",
            "`:mem &variable` reads storage; `:mem ptr` reads pointed-to "
            "bytes.",
            "Add --count N before the expression to limit the output:",
            "`:mem --count 16 &count`, `:disasm --count 8 callback`.",
            "Defaults: 64 bytes / 32 instructions; maxima: 4096 / 256.",
            "`:layout item` (or `:p --layout item`) shows field layout.",
            "`:p --asm EXPR` and `:p --memory EXPR` also work.",
            "Unknown ABI or function alignment padding is not guessed.",
            "",
            "Press : for the debugger command menu; type to filter commands.",
            "Ctrl-J/K selects; Tab/Enter chooses. Argument commands insert",
            "their prefix so you can finish typing. Native commands also work.",
            "",
            "`:bps` lists breakpoints. Ctrl-J/K follows source; d deletes.",
            "Esc closes the list and keeps the browsed file and row visible.",
            "Enabled breakpoints have red source line-number backgrounds.",
            "In Source, j/k selects a row and b toggles its breakpoint.",
            "Ctrl-P picks another project file without leaving the debugger.",
            "Use a :LINE suffix to select a row, then b to set its breakpoint.",
            "Esc cancels the picker. A browsed file stays selected during",
            "breakpoint updates; execution commands return to the next stop.",
            "`:delete ID`, `:disable ID`, `:enable ID` manage them directly.",
            "",
            "`:b path/to/file:42` sets a breakpoint. Tab completes the path",
            "and preserves the line suffix; Enter executes the command.",
            "`:c`, `:n`, `:s`, `:f`: continue, step over, into, out.",
            "The same letters work outside the prompt; r restarts.",
            "LLDB/mladbg: n/s from an inspected caller finishes its pending",
            "call and stops at the next source line before normal stepping.",
            "Ctrl-C interrupts; other prompt commands go to the backend.",
            "Esc cancels the prompt. q/Esc outside it closes the session.",
            "Attached processes detach; launched processes terminate.",
            "",
            "Requires macOS/Linux, Python 3, and LLDB bindings or GDB.",
            "MLADBG_PYTHON selects Python; MLADBG_LLDB locates bindings.",
            "For a mladbg checkout, set UVIM_MLADBG_MODULE to its",
            "tools/mladbg/mladbg.py. UVIM_GDB selects a GDB executable.",
            "Locals show top-level values; program stdin is not exposed.",
        };
    }
    else
#endif
        if(topic_lower.empty() || topic_lower == "index" ||
           topic_lower == "help")
    {
        lines = {
            "# uvim Help",
            "",
            "Welcome to uvim! Type `:help <topic>` for specific help.",
            "Press `/` to fuzzy-find across help topics and jump to a row.",
            "",
            "AVAILABLE TOPICS:",
        };
        for(const std::string& topicName : helpSearchTopics())
        {
            lines.push_back(
                "  `" + helpTopicLabel(topicName) + "` - " +
                helpTopicDescription(topicName));
        }
        lines.insert(
            lines.end(),
            {
                "",
                "QUICK START:",
                "  `i`        - Enter insert mode",
                "  `ESC`      - Return to normal mode",
                "  `:w`       - Save file",
                "  `:q`       - Quit",
                "  `:wq`      - Save and quit",
                "",
                "NAVIGATION:",
                "  `h j k l`  - Move left/down/up/right",
                "  `gg`       - Go to top",
                "  `G`        - Go to bottom",
                "  `Ctrl-f`   - Fuzzy file finder",
                "  `Ctrl-x`   - Regex search view",
                "  `Space-e`  - Diagnostic popup on LSP diagnostic row",
                "  `Space-m e/w` - Active-LSP error/warning jump list",
                "  `Space-x`  - File browser",
                "  `Space Space` - Command palette (`:help palette`)",
                "  `Ctrl-w`   - Browse buffers for the active pane",
                "  `Space-w c` - Close active split pane",
#ifdef UVIM_ENABLE_MODERN_KEYBINDINGS
#ifdef UVIM_ENABLE_MULTI_PANE_SPLITS
                "  `Ctrl-Shift-h/j/k/l` - Switch split pane by direction",
#else
                "  `Ctrl-h`/`Ctrl-l` - Prev/next buffer",
#endif
#endif
                "",
                "Press `q` to close this help window.",
            });
    }
    else if(topic_lower == "commands")
    {
        lines = {
            "# Command Reference",
            "",
            "FILE OPERATIONS:",
            "  `:w`              - Write current file",
            "  `:w <file>`       - Save to specific file",
            "  `:e <file>`       - Open file",
            "  `:edit <file>`    - Open file",
            "  `:e .`            - Open file browser for current directory",
            "  `:e%` or `:edit%` - Reload current file",
            "  `:e!%` or `:edit!%` - Force reload current file",
            "  `:q`              - Quit (fails if unsaved changes)",
            "  `:q!`             - Force quit without saving",
            "  `:wq` or `:x`     - Save and quit",
            "  `:qw`             - Save and quit",
            "  `:qw!`            - Force write all buffers and quit",
            "  `:wa`             - Write all buffers",
            "  `:wall`           - Write all buffers",
            "  `:wa!`            - Force write all modified buffers",
            "  `:qa`             - Quit all buffers",
            "  `:qall`           - Quit all buffers",
            "  `:qa!`            - Force quit all",
            "  `:wqa` or `:xa`   - Write all and quit",
            "  `:wqall`          - Write all and quit",
            "  `:wqa!` or `:wqall!` - Force write all and quit",
            "",
            "C/C++:",
#ifdef UVIM_ENABLE_ASM_VIEW
            "  `:asm . [flags]`   - Browse assembly of current buffer",
            "                       Example: `:asm . -O2` (`i`: inspect)",
#endif
            "  `:emitasm [flags]` - Emit current buffer assembly into .s "
            "buffer",
            "                       Example: `:emitasm -O2`",
            "                       `--raw` keeps clang's full assembler "
            "output",
            "  `:format` or `:fmt` - Format current buffer",
            "  `:lspinfo`          - Show LSP status and diagnostics info",
            "  `:toolinfo`         - Show external tool status and paths",
            "  `:tasks`            - Show background task queue and progress",
            "  `:emoji` or `:em`   - Open emoji picker",
            "  `:glyphselect`     - Open glyph selector",
#ifdef UVIM_ENABLE_COLOR_TOOLS
            "  `:ansitools`       - Insert common ANSI control escape",
            "  `:colorpicker`     - Insert an ANSI color escape sequence",
            "  `:colorselect`     - Select or edit RGB ANSI color style",
#endif
            "",
            "BUFFER MANAGEMENT:",
            "  `:bn` or `:bnext`     - Next buffer",
            "  `:bp`/`:bprev`/`:bprevious` - Previous buffer",
            "  `:bd` or `:bdelete`   - Delete buffer",
            "  `Space-b d`           - Delete active buffer",
            "  `:bd!`                - Force delete buffer",
            "  `:ls` or `:buffers`   - List buffers",
            "  `:b <n>`              - Switch to buffer n",
            "  `:buffer <n>`         - Switch to buffer n",
            "  `:enew`               - Create new buffer",
            "  `Ctrl-w`               - Browse buffers for the active pane",
            "  In the browser, `Enter` opens the buffer only in that pane",
            "",
            "FILE BROWSER:",
            "  `:e .`                - Open file browser",
            "  `:se`                 - Open file browser in horizontal split",
            "  `:ve`                 - Open file browser in vertical split",
            "  `:cd <path>`          - Change directory",
            "  `:cd`                 - Change directory to home",
            "  `:cdr`                - Change to project root",
            "  `:pwd`                - Print working directory",
            "  `:rfs`                - Refresh fuzzy find and grep file cache",
            "  Fuzzy finder: `Ctrl-o` toggles filename-first ranking",
            "  `uvim .`              - Start in file browser (defers auto-LSP)",
            "",
            "WINDOWS / TABS:",
            "  `:hs`                          - Horizontal split",
            "  `:vs`                          - Vertical split",
            "  `:sc`                         - Close active split pane",
            "  `Space-w c`                    - Close active split pane",
            "  `Ctrl-w`                       - Browse buffers for the active "
            "(selected) pane",
            "  `:tabnew`/`:tabe <file>`       - New tab / open in tab",
            "  `:tabn`/`:tabnext`             - Next tab",
            "  `:tabp`/`:tabprev`             - Previous tab",
            "",
            "LOC:",
            "  `:loc <path>`         - Count LOC (non-empty, non-comment)",
            "  `:loc! <path>`        - Show LOC list view",
            "  `:loc -l <path>`      - Show LOC list view",
            "  `:loc --list <path>`  - Show LOC list view",
            "  `:loc list <path>`    - Show LOC list view",
            "  `:loc%`               - Count LOC in current buffer",
            "  `:loctotal <path>`    - Count total LOC (respects .gitignore)",
            "  In LOC view: `s` sort asc/desc, `Esc` reset sort",
            "",
            "FILE BROWSER COMMANDS:",
            "  `:q`                  - Exit file browser",
            "  `:cd <path>`          - Change directory",
            "  `:cdr`                - Change to project root",
            "  `:mkdir <name>`       - Create directory",
            "  `:touch <name>`       - Create file",
            "  `:delete` or `:d`     - Delete selected file",
            "  `:rename <name>`      - Rename selected file",
            "  `:r` or `:mv`         - Rename selected file",
            "  `:rm`                  - Delete selected file",
            "  `:md <name>`           - Create directory",
            "  `:new <name>`          - Create file",
            "  `:run <command>`       - Run shell command in current directory",
            "  `:?`                    - Show file browser help",
            "",
            "GIT:",
            "  `ga`      - Open git stage view",
            "  `gb`      - Toggle git blame gutter",
            "  `gbb`     - Toggle git blame gutter with date/time",
            "  `gbl`     - Show git log at commit blamed for cursor line",
            "  `gj`      - Show commit diff for line under cursor in blame "
            "mode",
            "  `gbv`     - Show commit diff for line under cursor",
            "  `gs`      - Show C/C++ symbol size/member layout popup",
            "  `:git blame` - Toggle git blame gutter from command mode",
            "  `:git add`   - Stage current buffer",
            "  `gl`      - Show git log (repo)",
            "  `glf`     - Show git log (current file)",
            "  `:git stage` - Open git staging view",
            "  `:git log`   - Show git log",
            "  `:git rebase --` - Pick interactive/continue/abort/skip/quit",
            "  `:git push` - Push branch; confirm before publishing a new one",
            "  `:git prettylog` - Show formatted git log",
            "  `:git diff`  - Show repository diff",
            "  `:showchanges` - Show unsaved edits of the buffer as a "
            "diff",
            "  `:git commit` - Commit staged files",
            "  `:git fixup` - Fixup staged files into selected commit",
            "  `:git stash` - Stash changes",
            "  `:git stash pop` - Pop latest stash",
            "  Git log: `ctrl-v` range select, `space` mark commit",
            "  Git stage: `j/k` move, `h/l` pan list, `d` toggle diff split",
            "  Git stage: `space` stage/unstage, `ctrl-j/k` scroll diff",
            "  Git stage: `ctrl-h/l` pan diff, `m` mark fixup, `f` fixup",
            "  Git fixup: `space` select commit, `y/n/p` confirm or patch",
            "  Git patch: `y` stage hunk, `n` skip hunk, `ctrl-j/k` next/prev",
            "  `:set disablegitdefaultcolors` - Use editor theme for git views",
            "  `:set enablegitdefaultcolors`  - Use git's default colors",
            "",
            "SETTINGS:",
            "  `:set`                       - Configure editor options",
            "  `:set number` or `:set nu`     - Show line numbers",
            "  `:set nonumber` or `:set nonu` - Hide line numbers",
            "  `:set ignorecase` or `:set ic` - Case insensitive search",
            "  `:set smartcase` or `:set scs` - Smart case search",
            "  `:set iskitty=false`         - Alt keys for Ctrl+Shift keys "
            "(`:help keys`)",
#ifdef UVIM_ENABLE_RG_CACHE
            "  `:set rgcache`               - Use persistent .rg grep cache",
            "  `:set norgcache`             - Disable persistent grep cache",
            "  `:set rgupdate=300`          - Grep update delay in ms",
            "  `:set pickerstyle=float`     - Ctrl-p/Ctrl-a as floating window",
            "  `:set pickerstyle=fullscreen`- Ctrl-p/Ctrl-a fullscreen",
#endif
            "  `:set gdcenter`               - Center view after gd",
            "  `:set nogdcenter`             - Keep view steady after gd",
            "  `:set nocommandline.messageprefix` - Hide "
            "keyCode(command::CommandKey::KEY_COLON) prefix for messages",
            "",
            "STARTUP FLAGS:",
            "  `--no-git-index`      - Disable git-backed fuzzy/grep indexing",
            "  `--no-gitignore`      - Disable .gitignore filtering",
            "",
            "HELP:",
            "  `:help` or `:h`       - Show this help",
            "  `:help <topic>`       - Show help for topic",
            "  `:h <topic>`          - Show help for topic",
        };
    }
    else if(topic_lower == "git")
    {
        lines = {
            "# Git Integrations",
            "",
            "GIT BLAME:",
            "  `gb`   - Toggle git blame gutter",
            "  `gbb`  - Toggle git blame gutter with date/time",
            "  `gbl`  - Show git log at commit blamed for cursor line",
            "  `gj`   - Show commit diff for line under cursor in blame mode",
            "  `gbv`  - Show commit diff for line under cursor",
            "  `:git blame` - Toggle git blame gutter from command mode",
            "",
            "GIT LOG:",
            "  `gl`   - Browse git log (repo)",
            "  `glf`  - Browse git log (current file)",
            "  Use `ctrl-j/k` to move, `enter` to open diff, `q` to quit",
            "  `ctrl-v` range selects commits, `space` marks one commit",
            "  `:git rebase --` opens fuzzy options; Tab inserts, Enter runs",
            "  `:git push` asks [y/N] when this branch has no upstream",
            "  `--interactive` opens the commit tree with the oldest fixup",
            "  target selected; Enter opens a rebase todo through that commit",
            "",
            "GIT STAGE:",
            "  `ga`          - Open git stage view from normal or file browser",
            "  `:git stage` - Browse status, stage/unstage with `space`",
            "  `:git diff`  - View repo diff",
            "  `:showchanges` - Unsaved buffer edits vs the saved file,",
            "                   in the same diff view (works outside repos)",
            "  In diff views: `n`/`N` (or `]`/`[`) jump between hunks,",
            "  wrapping from the last hunk to the first and back;",
            "  after a `/` search `n`/`N` repeat it (Esc clears the search).",
            "  In `:showchanges`: `d` drops the selected hunk from the",
            "  buffer (restores the saved text), `u` undoes the last drop.",
            "  Drops are normal edits: `u` in the buffer also undoes them.",
            "",
            "EDIT A COMMIT (`:git log` -> Enter on a commit):",
            "  `d`  - Drop the selected hunk from the commit (pending). The",
            "         view shows the commit's new diff after your drops.",
            "         New/deleted files are dropped as a whole.",
            "  `v`  - Toggle between the new commit diff and the dropped",
            "         changes, to review before rewriting",
            "  `u`  - Undo the last pending drop; after `c`, undo the",
            "         rewrite (restores the original commit)",
            "  `c`  - Rewrite the commit without the dropped hunks (asks y/n):",
            "         HEAD is amended, older commits are rebased. The dropped",
            "         changes stay unstaged in the working tree.",
            "  Refused for pushed commits, root/merge commits, merges on",
            "  top, or a dirty working tree. If later commits conflict, the",
            "  rebase is aborted and nothing changes. `q` with pending drops",
            "  asks first; after a rewrite the log reloads.",
            "  `:git commit` - Commit staged files",
            "  `:git fixup` - Fixup staged files into selected commit",
            "  `j/k`        - Move between file rows",
            "  `h/l`        - Scroll the left status pane horizontally",
            "  `d`          - Toggle split diff preview",
            "  `ctrl-j/k`   - Scroll split diff vertically",
            "  `ctrl-h/l`   - Scroll split diff horizontally",
            "  `m`          - Mark/unmark file for fixup",
            "  `f`          - Fixup staged/marked files to a commit",
            "  `enter`      - Open file",
            "",
            "GIT FIXUP:",
            "  `space/enter` - Select commit to fixup",
            "  `y/n/p`   - Confirm (y), cancel (n), or patch mode (p)",
            "",
            "GIT PATCH:",
            "  `y`       - Stage current hunk",
            "  `n`       - Skip current hunk",
            "  `ctrl-j/k` - Next/prev hunk",
            "",
            "GIT VIEW COLORS:",
            "  `:set disablegitdefaultcolors` - Use editor theme colors",
            "  `:set enablegitdefaultcolors`  - Use git's default colors",
            "",
            "COMMENT TOGGLE:",
            "  `:set commenttogglepartial`   - Toggle on any commented line",
            "  `:set nocommenttogglepartial` - Toggle only if all commented",
        };
    }
    else if(topic_lower == "gb")
    {
        lines = {
            "# gb",
            "",
            "`gb` toggles the git blame gutter for the current file.",
            "`gbb` shows the same blame gutter with date/time included.",
            "",
            "Related:",
            "  `gbb`        - Show blame gutter with date/time",
            "  `gbl`        - Open git log at the blamed commit",
            "  `gj`         - Open commit diff for the line under cursor",
            "  `gbv`        - Open commit diff for the line under cursor",
            "  `:git blame` - Toggle git blame gutter from command mode",
        };
    }
    else if(topic_lower == "gbb")
    {
        lines = {
            "# gbb",
            "",
            "`gbb` toggles the extended git blame gutter for the current file.",
            "The extended gutter shows commit hash, author, and local "
            "date/time.",
            "",
            "Related:",
            "  `gb`         - Show the compact hash and author blame gutter",
            "  `gbl`        - Open git log at the blamed commit",
            "  `gj`         - Open commit diff for the line under cursor",
            "  `gbv`        - Open commit diff for the line under cursor",
            "  `:git blame` - Toggle compact git blame from command mode",
        };
    }
    else if(topic_lower == "gbl")
    {
        lines = {
            "# gbl",
            "",
            "`gbl` opens the repository git log with the cursor on the commit",
            "that git blame reports for the current line.",
            "",
            "Related:",
            "  `gb`   - Toggle the blame gutter",
            "  `gbb`  - Show blame gutter with date/time",
            "  `gbv`  - Open commit diff for the line under cursor",
            "  `gl`   - Open the repository git log",
        };
    }
    else if(topic_lower == "ga")
    {
        lines = {
            "# ga",
            "",
            "`ga` opens the git stage view from normal mode or the file "
            "browser.",
            "",
            "Inside the git stage view:",
            "  `j/k`       - Move between staged/unstaged/untracked file rows",
            "  `h/l`       - Scroll the left status pane horizontally",
            "  `d`         - Toggle split diff preview on the right",
            "  `ctrl-j/k`  - Scroll the diff preview vertically",
            "  `ctrl-h/l`  - Scroll the diff preview horizontally",
            "  `space`     - Stage or unstage the selected file",
            "  `enter`     - Open the selected file",
            "  `m`         - Mark file for fixup",
            "  `f`         - Start fixup for staged/marked files",
            "  `q`         - Close the git stage view",
            "",
            "Related:",
            "  `:git stage` - Open the same view from command mode",
            "  `:git fixup` - Start fixup from staged files",
            "  `:help git`  - All git integrations",
        };
    }
    else if(topic_lower == "gj")
    {
        lines = {
            "# gj",
            "",
            "`gj` opens the commit diff for the line under cursor when the",
            "git blame gutter is visible.",
            "",
            "Inside the commit view:",
            "  `j/k`   - Scroll",
            "  `/` `?` - Search",
            "  `q`     - Quit",
            "",
            "Related:",
            "  `gb`  - Toggle git blame gutter",
            "  `gbv` - Open commit diff for the line under cursor",
        };
    }
    else if(topic_lower == "gbv")
    {
        lines = {
            "# gbv",
            "",
            "`gbv` opens the commit diff for the line under cursor.",
            "",
            "Related:",
            "  `gb`         - Toggle git blame gutter",
            "  `gj`         - Open commit diff in blame mode",
            "  `:git blame` - Toggle git blame gutter from command mode",
        };
    }
    else if(topic_lower == "gs")
    {
        lines = {
            "# gs",
            "",
            "`gs` opens a modal C/C++ size popup for the symbol under cursor.",
            "The popup shows total size, member sizes, padding, and nested",
            "struct/class members where they can be resolved.",
            "",
            "Nested structs are indented. Expansion is limited to 3 nested",
            "levels; deeper resolved members are shown as `...`.",
            "",
            "Inside the size popup:",
            "  `j/k` - Scroll popup content",
            "  `q`   - Close the popup",
            "  `gs`  - Close the popup",
            "",
            "Notes:",
            "  Requires C/C++ file type and clang/clang++ for probing.",
            "  Local quoted includes are scanned for nested struct bodies.",
        };
    }
#ifdef UVIM_ENABLE_ASM_VIEW
    else if(topic_lower == "asm")
    {
        lines = {
            "# Assembly browser",
            "",
            "Select lines with V (or v), then :asm . [flags] to inspect",
            "only assembly mapped to the selected source lines. The full",
            "buffer is compiled so includes and declarations still work.",
            "The title shows the range; --split keeps original source lines.",
            "The range stays blue; green marks the currently linked line.",
            "Customize the range background with theme.asm.selection_bg.",
            "Normal-mode :asm . shows the full output again.",
            "",
            "`:asm .` / `:asm` compiles C/C++ or Mlang (.mla), including",
            "unsaved edits, into a read-only assembly view. `:asm path.cpp`",
            "compiles another file (using its unsaved buffer if already open).",
            "The source buffer and disk file remain unchanged.",
            "",
            "Examples:",
            "  `:asm . -O2`",
            "  `:asm . --split -O2` opens source/assembly side by side",
            "  `:asm . --` opens common compiler flags before compiling",
            "  `:asm . --compiler gcc -O3 -march=native`",
            "  `:asm \"my file.cpp\" --compiler clang -O2 -I \"include dir\"`",
            "  `:asm . --raw -O0 -g`",
            "",
            "The view title shows the target architecture, including cross "
            "targets.",
            "Only directly source-mapped code stays bright; unresolved rows "
            "dim.",
            "Browsing rows or switching panes resets both panes to line start.",
            "Use h/l to pan the current line; 0 returns it to the beginning.",
            "Inferred labels, external code, and unknown origins get darker.",
            "Known external-source code also has a subtle dark red background.",
            "Customize it with theme.asm.external_bg in the theme/config file.",
            "Select external code for its source filename/line when available.",
            "Unknown origins show no source mapping; no library is guessed.",
            "  `:set asmdimunmapped=false` disables dimming (default true).",
            "",
            "Inspect uses API instruction forms/examples, with built-in "
            "fallback.",
            "  `:set asmdocsapi=false` uses built-in help only (default true).",
            "Mlang: .mla files use mlang from PATH (or --compiler PATH).",
            "  `:asm example.mla --split -O2`",
            "  `:asm . --target-arch aarch64`",
            "Mlang uses -S -g; the flag picker offers Mlang options.",
            "A temporary source snapshot beside the file preserves imports;",
            "its directory must be writable. The snapshot is cleaned up.",
            "Generated indexing files use a temporary cwd, not the project.",
            "C/C++ compile_commands.json is not used for Mlang.",
            "",
            "For C/C++, compile_commands.json supplies compiler, flags and "
            "cwd.",
            "Lookup: configured clangd database, source/ancestor directories",
            "and common build directories, then the editor cwd/ancestors.",
            "Only the requested file's entry is used (first matching entry).",
            "Both arguments and quoted command entries/response files work.",
            "Object and dependency-output arguments are removed. A malformed",
            "database reports an error. Status flag: compile_commands.",
            "Prompt -O2/-std=.../-D.../-U... override inherited settings.",
            "Prompt includes precede inherited paths of the same include kind.",
            "--compiler overrides the database compiler; paths are accepted.",
            "Without a matching entry: Clang preferred, GCC fallback, -O0",
            "unless the prompt supplies another optimization level.",
            "Type -- for a flag picker: up/down or Ctrl-J/K selects; Enter/Tab",
            "inserts a flag without compiling. Enter again compiles; Escape",
            "cancels the whole command. Type -- again for another suggestion.",
            "Quoted paths and flags are supported. Prompt paths use the editor",
            "cwd; inherited paths use the database entry's working directory.",
            "",
            "j/k or arrows: next/previous row in the focused pane",
            "v: toggle the source/assembly split (read-only source snapshot)",
            "Ctrl-Shift-H/L: focus source/assembly; Alt-H/L with iskitty=false",
            "Matching rows in both panes have a subtle green background.",
            "Moving in either pane reveals its match in the other pane.",
            "Compiler DWARF line records provide the mapping. Comments,",
            "optimized-away lines and code from other files may have no match.",
            "h/l or arrows: horizontal scroll",
            "Page Up/Down: page; Ctrl-U/D: half page; g/G: first/last row",
            "i: toggle instruction inspector, following the selected row",
            "Ctrl-J/K: scroll instruction help while the popup is open",
            "q/Escape: exit the view (also while compiling or inspecting)",
            "",
            "Compilation and instruction help load in background tasks.",
            "Compiler errors appear in the view. Instruction help uses the",
            "same x86/x64 and AArch64 docs as assembly gd. Labels/directives",
            "show a message when there is no instruction to explain.",
            "",
            "Requires ASM_VIEW and auxiliary views; help also needs ASM_DOCS.",
            "`:emitasm [flags]` still creates a separate editable .s buffer.",
        };
    }
#endif
    else if(topic_lower == "emitasm")
    {
        lines = {
            "# :emitasm",
            "",
            "`:emitasm` emits assembly for the current C/C++ buffer into a",
            "new `.s` buffer. The command uses the current in-memory buffer,",
            "so unsaved edits are included.",
            "",
            "Usage:",
            "  `:emitasm`       - Emit compact Godbolt-style assembly",
            "  `:emitasm -O2`   - Pass compiler flags before emitting "
            "assembly",
            "  `:emitasm --raw` - Keep clang's full assembler output",
            "",
            "Notes:",
            "  Uses clang for `.c` files and clang++ for other C/C++ files.",
            "  By default clang labels/directives are folded into a compact",
            "  function view. Use `--raw` for the previous unfiltered output.",
            "  The generated `.s` buffer is not marked dirty.",
            "  Assembly mnemonics, directives, and registers are highlighted.",
            "  With `UVIM_ENABLE_ASM_DOCS`, `gd` on an instruction opens a",
            "  cached x86/x64 or AArch64 documentation index generated from",
            "  text packed into the uvim binary.",
            "  `Space-ga` shows the current instruction docs in a modal popup;",
            "  use `j/k` to scroll and `q` to close it.",
            "  Run with `--asm-docs-fetch` to fetch Compiler Explorer/Godbolt",
            "  asm docs into that cache on demand. Requires `curl` at runtime.",
        };
    }
    else if(topic_lower == "modes")
    {
        lines = {
            "# Editor Modes",
            "",
            "NORMAL MODE:",
            "  Default mode for navigation and commands.",
            "  Press `ESC` to return to normal mode from any other mode.",
            "",
            "INSERT MODE:",
            "  `i`     - Insert before cursor",
            "  `I`     - Insert at beginning of line",
            "  `a`     - Append after cursor",
            "  `A`     - Append at end of line",
            "  `o`     - Open new line below",
            "  `O`     - Open new line above",
            "",
            "VISUAL MODE:",
            "  `v`     - Character-wise visual selection",
            "  `V`     - Line-wise visual selection",
            "  `Ctrl-v` - Block visual selection",
            "  `y`     - Yank (copy) selection",
            "  `d`     - Delete selection",
            "  `c`     - Change selection",
            "",
            "COMMAND MODE:",
            "  `:`     - Enter command mode",
            "  Type commands and press Enter to execute.",
            "  `Ctrl-k` - Previous command in history",
            "  `Ctrl-j` - Next command in history",
            "  `Ctrl-f` - Fuzzy history search",
            "  `:/...` - Search forward (regex)",
            "  `:?...` - Search backward (regex)",
            "  Command list shows and filters while you type (fuzzy:",
            "  `lcto` lists `loctotal`)",
            "  `Tab`   - Take the highlighted command into the prompt",
            "            (`:lcto` Tab -> `:loctotal`; also after picking it",
            "            with `Ctrl-j/k`), then keep typing or press Enter.",
            "            After a space Tab completes the argument (paths)",
            "",
            "SEARCH MODE:",
            "  `/`     - Search forward",
            "  `?`     - Search backward",
            "  `n`     - Next match",
            "  `N`     - Previous match",
            "",
            "FILE BROWSER MODE:",
            "  `Space-x` - Open file browser",
            "  `j/k`     - Navigate up/down",
            "  `Enter`   - Open file/directory",
            "  `h` or `-`  - Go to parent directory",
            "  `.`       - Toggle hidden files",
            "  `i`       - Toggle gitignore",
            "  `ga`      - Open git stage view",
            "  `:`       - Enter command mode in browser",
            "  `q`       - Close active browser pane",
            "  `Q`       - Exit browser and save pane layout",
        };
    }
    else if(topic_lower == "functions")
    {
        lines = {
            "# Function signature browser",
            "",
            "`Space-o` or `:functions` opens the current buffer's outline.",
            "The command palette also offers Browse function signatures.",
            "",
            "  Type          - Fuzzy-filter signature text",
            "  Ctrl-j/k      - Next / previous entry (also arrow keys)",
            "  PgUp/PgDn     - Page up / down",
            "  Ctrl-u        - Clear the filter",
            "  Backspace     - Remove the last filter character",
            "  Enter         - Jump to the selected function's name",
            "  Esc           - Cancel without moving the editor cursor",
            "",
            "Each row shows the source line number and function signature.",
            "Multiline signatures are joined into one row. Long rows are",
            "clipped to the terminal width. The list starts near your cursor.",
            "",
            "Unsaved code is included. Reopen the list after editing to",
            "refresh it. Ctrl-o / Ctrl-i return through jump history.",
            "In a split, selecting an entry changes only the active pane.",
            "Opening a function centers it in the code view by default.",
            "`:set gdcenter=false` keeps minimal scrolling; set it to true",
            "to restore centering. `:set gdcenter?` shows the current value.",
            "",
            "Requires browser tools. Uses lexical recognition without LSP:",
            "C/C++, Python, Mlang, JavaScript, TypeScript and C#.",
            "Lists definitions with bodies, not prototypes or macro-generated",
            "functions. See :help navigation for structural repeat motions.",
        };
    }
    else if(topic_lower == "navigation")
    {
        lines = {
            "# Navigation",
            "",
            "BASIC MOVEMENT:",
            "  `h`       - Move left",
            "  `j`       - Move down",
            "  `k`       - Move up",
            "  `l`       - Move right",
            "  `w`       - Move to next word",
            "  `b`       - Move to previous word immediately (`2b`: two words)",
            "  `e`       - Move to end of word",
            "",
            "LINE MOVEMENT:",
            "  `0`       - Move to beginning of line",
            "  `^`       - Move to first non-blank character",
            "  `$`       - Move to end of line",
            "  `gg`      - Go to first line",
            "  `G`       - Go to last line",
            "  `<n>G`    - Go to line n",
            "",
            "SCREEN MOVEMENT:",
            "  `Ctrl-f`  - Page down",
            "  `Ctrl-b`  - Page up",
            "  `Ctrl-d`  - Half page down",
            "  `Ctrl-u`  - Half page up",
            "  `zz`      - Center screen on cursor",
            "",
            "FILE NAVIGATION:",
            "  `Ctrl-p`  - Fuzzy file finder",
            "             `Ctrl-o` toggles filename-first ranking in finder",
            "  `Ctrl-x`  - Regex search view",
            "  `Space-e` - Diagnostic popup on LSP diagnostic row",
            "  `Space-m e/w` - Active-LSP error/warning jump list",
            "  `Space-x` - File browser",
            "  `Space-b d` - Close active buffer",
            "  `Space Space` - Command palette: search and run commands",
            "  `Ctrl-a`  - Grep search",
            "",
            "MARKS & JUMPS:",
            "  `m{a-z}`   - Set mark",
            "  `'`/`` ` `` + {a-z} - Jump to mark",
            "  `Ctrl-o`   - Jump back",
            "  `Ctrl-i`   - Jump forward",
            "",
            "CODE NAVIGATION:",
            "  `Space-o` / `:functions` - Browse function signatures",
            "    Type to filter, Ctrl-j/k to browse, Enter to jump.",
            "  `]f` / `[f` - Next / previous function signature",
            "  `]s` / `[s` - Next / previous local declaration or call",
            "  After either, `n` / `N` repeat forward / backward.",
            "  `Esc`, a new search, or leaving Normal mode restores search.",
            "  Counts work: `3]f`, `2n`. Jumps stop at boundaries.",
            "  Local steps follow source order through nested blocks,",
            "  staying in the current function and skipping nested functions.",
            "  Lexical recognition: C/C++, Python, Mlang, JS/TS and C#.",
            "  No LSP required; comments and strings are ignored.",
            "  Definitions with bodies are recognized, not prototypes.",
            "",
            "  `gd`       - Go to definition",
            "               In `.s`/`.asm`, opens cached x86/x64 or AArch64 "
            "instruction docs",
            "  `Space-ga` - Show asm instruction docs in a scrollable popup",
            "  `gr`       - Find references",
            "  `gf`       - Open file under cursor",
            "  `gs`       - Show C/C++ symbol size/member layout popup",
            "  `ga`       - Open git stage view",
            "",
            "SPLITS / WINDOWS:",
#if defined(UVIM_ENABLE_MODERN_KEYBINDINGS) &&                                 \
    defined(UVIM_ENABLE_MULTI_PANE_SPLITS)
            "  `Ctrl-Shift-h/j/k/l` - Switch active split pane by direction",
#elif defined(UVIM_ENABLE_MODERN_KEYBINDINGS)
            "  `Ctrl-h`/`Ctrl-l` - Prev/next buffer",
            "  Splits use the older single split pair.",
#else
            "  Use `:hs`, `:vs`, and window commands for splits.",
#endif
        };
    }
    else if(topic_lower == "editing")
    {
        lines = {
            "# Editing",
            "",
            "INSERT/APPEND:",
            "  `i`     - Insert before cursor",
            "  `I`     - Insert at line start",
            "  `a`     - Append after cursor",
            "  `A`     - Append at line end",
            "  `o`     - New line below",
            "  `O`     - New line above",
            "",
            "CHANGE/DELETE:",
            "  `x`     - Delete char at cursor",
            "  `X`     - Delete char before cursor",
            "  `s`     - Substitute char (enter insert)",
            "  `S`     - Substitute line (enter insert)",
            "  `C`     - Change to end of line",
            "  `D`     - Delete to end of line",
            "  `J`     - Join lines",
            "  `r`     - Replace single char",
            "  `rn`    - Rename symbol with clangd (normal/visual)",
            "            Popup: `p` patch, `y` file, `a` all, `n`/ESC cancel",
            "  `R`     - Replace mode",
            "  `~`     - Toggle case",
            "",
            "COLLECTION INDENTATION (Normal and Insert modes):",
            "  `Ctrl-t` - Reindent enclosing list/map/arguments; no formatter",
            "  Changes leading whitespace only; nested items included.",
            "  `:set collectionindentkey=ctrl-y` or `off` - Rebind/disable",
            "  `:set collectionindentwidth=2` - Width; 0 uses buffer width",
            "  `:set continuationindent=false` - Disable Enter continuation",
            "  `:set splitbraces=false` / `splitbrackets=false` - Empty pairs",
            "  `:set autobrackets=false` - Disable [] pairing/skip/Backspace",
            "",
            "YANK/PASTE:",
            "  `y`     - Yank selection (visual)",
            "  `Y`     - Yank line",
            "  `p`     - Paste after cursor",
            "  `P`     - Paste before cursor",
            "",
            "TEXT OBJECTS (after `d` `c` `y`, or in visual mode after `v`):",
            "  `iw`/`aw` - Word under the cursor (`aw` adds trailing space)",
            "  `iW`/`aW` - WORD: any non-blank run, e.g. `obj->field`",
            "  `ip`/`ap` - Paragraph",
            "  `i(` `i[` `i{` `i\"` `i'` - Inside brackets/quotes (`a` incl.)",
            "  Examples: `yiw` yank word, `ciw` change word, `viwp`",
            "  replace word with the yank, `di(` delete inside parens.",
            "",
            "UNDO/REDO:",
            "  `u`       - Undo",
            "  `Ctrl-r`  - Redo",
            "",
            "REPEAT:",
            "  `.`     - Repeat last change",
            "",
            "COLOR:",
            "  `Space-cp`  - ANSI color picker",
            "  `Space-cpb` - ANSI background color picker",
            "  `Space-cs`  - RGB color selector",
            "  `Space-csb` - RGB background color selector",
        };
    }
    else if(topic_lower == "files")
    {
        lines = {
            "# Files",
            "",
            "COMMANDS:",
            "  `:w`        - Save file",
            "  `:w <file>` - Save as",
            "  `:q`        - Quit (fails if unsaved)",
            "  `:q!`       - Force quit",
            "  `:wq`/`:x`  - Save and quit",
            "",
            "NORMAL MODE:",
            "  `:e .`           - File browser",
            "  `:hs`/`:vs`      - Split file browser pane",
            "  `Space-x`        - File browser",
            "  `Space-e`        - Diagnostic popup on LSP diagnostic row",
            "  `Space-m e/w`    - Active-LSP error/warning jump list",
            "  `gf`             - Go to file under cursor",
            "  `gh`             - Alternate file (header/source)",
            "  `:vs`/`:hs`      - Open split view",
            "  `Ctrl-Shift-h/j/k/l` - Switch active split pane",
            "",
            "FILE BROWSER:",
            "  `Enter` - Open file/directory",
            "  `h`/`-` - Parent directory",
            "  `.`     - Toggle hidden files",
            "  `i`     - Toggle gitignore",
            "  `:`     - Command mode",
            "  `q`     - Close active browser pane",
            "  `Q`     - Exit browser and save pane layout",
        };
    }
    else if(topic_lower == "buffers")
    {
        lines = {
            "# Buffers And Tab Bar",
            "",
            "COMMANDS:",
            "  `:ls`/`:buffers` - List buffers",
            "  `:b <n>`         - Switch to buffer n",
            "  `:bn`/`:bnext`   - Next buffer",
            "  `:bp`/`:bprev`   - Previous buffer",
            "  `:bd`/`:bdelete` - Delete buffer",
            "  `:bd!`           - Force delete buffer",
            "  `:enew`          - New buffer",
            "",
            "NORMAL MODE:",
            "  `Ctrl-w`          - Browse open buffers for the active pane",
            "  `Space-b d`       - Close active buffer",
            "  `b`               - Previous word",
            "  `Ctrl-h`/`Ctrl-l` - Prev / next buffer",
            "  `Ctrl-Shift-H`    - Move buffer left when no split is active",
            "  `Ctrl-Shift-L`    - Move buffer right when no split is active",
            "  `Ctrl-^`          - Alternate buffer",
#if defined(UVIM_ENABLE_MODERN_KEYBINDINGS) &&                                 \
    defined(UVIM_ENABLE_MULTI_PANE_SPLITS)
            "  `Ctrl-Shift-h/j/k/l` - Switch split pane by direction",
#elif !defined(UVIM_ENABLE_MULTI_PANE_SPLITS)
            "  `Ctrl-h`/`Ctrl-l`    - Prev / next buffer",
#endif
            "",
            "TAB BAR:",
            "  Tabs show `N:filename` (N = buffer index, 1-based).",
            "  `:set tabnumbers`     - Show numbers (default)",
            "  `:set notabnumbers`   - Hide numbers",
            "  `:set tabnumbers?`    - Print current value",
            "  `:set tabnumbers=on`  - Same as `:set tabnumbers`",
            "  `:set tabnumbers=off` - Same as `:set notabnumbers`",
            "  `:set showtabs`/`noshowtabs` - Show/hide the tab bar",
            "  Config key: `editor.tabnumbers` / `editor.showtabs`",
            "",
            "BUFFER BROWSER:",
            "  `j/k`      - Navigate",
            "  `Enter`    - Open selected buffer only in the active pane",
            "  `Ctrl-x`   - Close selected buffer",
            "  `Ctrl-Shift-x` - Close searched buffers",
            "  `q`        - Quit",
        };
    }
    else if(topic_lower == "windows" || topic_lower == "splits")
    {
        lines = {
            "# Windows And Splits",
            "",
            "COMMANDS:",
            "  `:hs`                          - Horizontal split",
            "  `:vs`                          - Vertical split",
            "  `:sc`                         - Close active split pane",
            "  `Space-w c`                    - Close active split pane",
            "  `Ctrl-w`                       - Browse open buffers for the "
            "active pane",
            "  Buffer browser `Enter` changes only the active pane",
#if defined(UVIM_ENABLE_MODERN_KEYBINDINGS) &&                                 \
    defined(UVIM_ENABLE_MULTI_PANE_SPLITS)
            "",
            "PANE NAVIGATION (NORMAL MODE):",
            "  `Ctrl-Shift-h` - Focus pane to the left",
            "  `Ctrl-Shift-l` - Focus pane to the right",
            "  `Ctrl-Shift-k` - Focus pane above",
            "  `Ctrl-Shift-j` - Focus pane below",
            "  `Ctrl-h`/`Ctrl-l` - Prev/next buffer",
            "",
            "  No Ctrl+Shift in your terminal (macOS Terminal, PowerShell)?",
            "  `Alt-h/j/k/l` do the same with `:set iskitty=false`; see",
            "  `:help keys`.",
#elif !defined(UVIM_ENABLE_MULTI_PANE_SPLITS)
            "",
            "PANE NAVIGATION (NORMAL MODE):",
            "  `Ctrl-h` - Previous buffer",
            "  `Ctrl-l` - Next buffer",
            "  Splits use the older single split pair.",
#else
            "",
            "PANE NAVIGATION (NORMAL MODE):",
            "  Modern pane-focus keybindings are disabled in this build.",
#endif
            "",
            "TABS:",
            "  `:tabnew`            - New tab",
            "  `:tabe <file>`       - Open file in new tab",
            "  `:tabn`/`:tabnext`   - Next tab",
            "  `:tabp`/`:tabprev`   - Previous tab",
            "",
            "TIP:",
#ifdef UVIM_ENABLE_MULTI_PANE_SPLITS
            "  Splitting a pane divides only the active pane, so vertical and",
            "  horizontal panes can be nested.",
#ifdef UVIM_ENABLE_PER_PANE_LSP
            "  Each visible pane refreshes and draws its own LSP diagnostics.",
#else
            "  Per-pane LSP refresh is disabled in this build.",
#endif
#else
            "  Multi-pane splits are disabled; opening another split keeps",
            "  the older single split pair layout.",
#endif
        };
    }
    else if(topic_lower == "search")
    {
        lines = {
            "# Search",
            "",
            "IN-BUFFER:",
            "  `/`      - Search forward (regex)",
            "  `?`      - Search backward (regex)",
            "  `n`      - Next match",
            "  `N`      - Previous match",
            "  `*`      - Search word under cursor (forward)",
            "  `#`      - Search word under cursor (backward)",
            "  `Space-n` - Clear search highlights",
            "  After `]f`/`[f` or `]s`/`[s`, n/N repeat code navigation.",
            "  Press Esc or start a new search to restore search repeats.",
            "",
            "PROJECT:",
            "  `Ctrl-p`  - Fuzzy file finder",
            "  `Ctrl-x`  - Regex search view for the current buffer",
            "  Regex view: `Ctrl-s` toggles current buffer / project files",
            "  Regex view: `Enter` opens the selected match",
            "  `Ctrl-a`  - Grep search",
            "  Fuzzy/grep: `Ctrl-t` toggles high/low match contrast",
            "  Fuzzy/grep: end the query with `:415` to open the chosen",
            "  file/result at line 415 (`SomeFil:415`, `needle:415`). A",
            "  missing line jumps to the end or start and says so in the",
            "  status line. `needle\\:415` searches the text literally.",
            "  Fuzzy/grep open as a floating window over the code with a",
            "  preview pane (`Ctrl-v` toggles the preview);",
            "  `Ctrl-Shift-j`/`Ctrl-Shift-k` scroll the previewed file",
            "  (kitty-protocol terminals; elsewhere `Alt-j`/`Alt-k` with",
            "  `:set iskitty=false`, see `:help keys`).",
            "  `:set pickerstyle=fullscreen` restores the fullscreen views;",
            "  `:set pickerstyle=float` brings the floating window back.",
            "  Size: `:set picker.width=80` / `picker.height=70` (percent);",
            "  `:set picker.preview=false` hides previews by default.",
            "  `:set searchcontrast=0-100` sets the shared contrast strength.",
            "  Use `searchcontrast+=10`/`-=10` to adjust it; default is 100.",
            "  The value persists for the running editor session.",
#ifdef UVIM_ENABLE_RG_CACHE
            "  Grep cache: `:set rgcache`/`:set norgcache` (default on)",
            "  Grep update delay: `:set rgupdate=300` (default 300 ms)",
            "  rg cache indexing runs in the background (see `:h tasks`);",
            "  searches use ripgrep until the cache is ready.",
            "  Cache files are stored under the project `.rg/` directory.",
#endif
            "SUBSTITUTION:",
            "  `:s/old/new/` - First match on the current line",
            "  `:%s/old/new/g` - All matches in the current buffer",
            "  `:2,10s/old/new/gi` - Line range, ignoring case",
            "  `:%%s/old/new/g` - Project root; stage modified buffers",
            "  Review project changes and use :wa to save them.",
            "  Add n (gn) to count only; e suppresses no-match errors.",
            "  Add c (gc) to confirm: y yes, n skip, a all, l last.",
            "  q/Esc stops and keeps earlier approvals; n overrides c.",
            "  Add p (gp) for a match list: j/k moves, Space toggles.",
            "  Selected rows are green. r resets; Enter applies; Esc cancels.",
            "  p overrides c; n counts without opening either popup.",
            "  Alternate delimiters work: :%s#old/path#new/path#g",
            "  Replacement: & is the match; \\1..\\9 are captures.",
            "  Each changed buffer gets one undo step (u / Ctrl-r).",
            "",
            "  `:/...`   - Regex search forward (command mode)",
            "  `:? ...`  - Regex search backward (command mode)",
        };
    }
    else if(topic_lower == "regex")
    {
        lines = {
            "# Regex Search View",
            "",
            "NORMAL MODE:",
            "  `Ctrl-x` - Open regex search view",
            "",
            "PROMPT:",
            "  Type a regex pattern to list matching lines.",
            "  Matching text is highlighted in the result list.",
            "",
            "SCOPE:",
            "  Starts by searching the current buffer.",
            "  `Ctrl-s` toggles between current buffer and all project files.",
            "  Project-file search respects `.gitignore` when enabled.",
            "",
            "KEYS:",
            "  `Enter`    - Open selected match",
            "  `Esc`      - Cancel",
            "  `Ctrl-j/k` - Move through results",
            "  `Ctrl-d/u` - Half-page down/up",
            "  `Ctrl-w`   - Delete previous word in the prompt",
        };
    }
    else if(topic_lower == "clipboard")
    {
        lines = {
            "# Clipboard Operations",
            "",
            "YANK (COPY):",
            "  `yy`      - Yank current line",
            "  `<n>yy`   - Yank n lines",
            "  `yw`      - Yank word",
            "  `y$`      - Yank to end of line",
            "  `v<move>y` - Yank visual selection",
            "  `v<move>Y` - Yank the selected lines (whole lines, like Vim)",
            "  `V<move>y` - Yank visual line selection",
            "",
            "PASTE:",
            "  `p`       - Paste after cursor",
            "  `P`       - Paste before cursor",
            "  `v<move>p` - Replace the selection with the yanked text",
            "  `V<move>p` - Replace the selected lines with the yanked text",
            "",
            "REPLACE MANY WITH THE SAME TEXT:",
            "  Visual `p` keeps the yanked text (the replaced text is not",
            "  yanked), so one yank can replace several places:",
            "    `yiw`   - yank the word under the cursor (e.g. `some`)",
            "    move onto another word, `viwp` - replace just that word",
            "    move again, `viwp`             - replace with the same text",
            "  `some->blah` + `viwp` on `someElse->blah` gives `some->blah`:",
            "  `iw` stops at punctuation. Use `iW`/`E` to take `->blah` too",
            "  (`viWp`), or `e` (`vep`) to replace from the cursor onward.",
            "  With the system clipboard on, `p` uses the clipboard; copying",
            "  something in another app in between changes what is pasted.",
            "",
            "SYSTEM CLIPBOARD:",
            "  By default, `useSystemClipboard` is enabled.",
            "  All yank operations automatically copy to system clipboard.",
            "  Paste operations read system clipboard first, then fall back",
            "  to the internal yank buffer.",
            "",
            "  This allows seamless integration with other applications:",
            std::string("  - Yank in uvim ") + ascii::utf8(ascii::RIGHT_ARROW) +
                " Paste in terminal or other apps",
            std::string("  - Copy in other apps ") +
                ascii::utf8(ascii::RIGHT_ARROW) + " Paste in uvim",
            "",
            "DELETE (CUT):",
            "  `dd`      - Delete (cut) current line",
            "  `<n>dd`   - Delete n lines",
            "  `dw`      - Delete word",
            "  `d$`      - Delete to end of line",
        };
    }
    else if(
        topic_lower == "filebrowser" || topic_lower == "browser" ||
        topic_lower == "explore" || topic_lower == "ex")
    {
        lines = {
            "# File Browser",
            "",
            "OPEN:",
            "  `Space-x`              - Open file browser at file's dir",
            "  `:e .`                 - Open file browser",
            "  `:hs`                  - Horizontal split",
            "  `:vs`                  - Vertical split",
            "  `uvim .`               - Start in browser (defers auto-LSP)",
            "",
            "NAVIGATION:",
            "  `j` / `k`              - Move down / up",
            "  `Enter` / `l` / right  - Open file or descend into dir",
            "  `h` / left / `-`       - Parent directory",
            "  `Ctrl-d` / `Ctrl-u`    - Half-page down / up",
            "  `gg` / `G`             - Top / bottom",
            "  `Ctrl-o` / `Tab`       - History back / forward",
            "  `cd`                   - chdir to current directory",
            "  `.`                    - Toggle hidden files",
            "  `Ctrl-g`               - Toggle .gitignore filtering",
            "  `r` / `Ctrl-l`         - Refresh",
            "  `q`                    - Close active browser pane; final pane "
            "exits",
            "  `Q`                    - Exit browser and save pane layout",
            "",
            "SELECTION (multi-file):",
            "  `Space`                - Toggle current row's selection",
            "  `Shift-V`              - Start/stop visual-line selection.",
            "                           Exiting with V keeps the range; V",
            "                           again extends with another segment.",
            "  `Esc` (visual)         - Cancel current segment",
            "  `Esc Esc`              - Clear all selections + cut buffer",
            "  `Enter` (with sel.)    - Open every selected file as buffer",
            "                           (dirs skipped, alphabetical order)",
            "",
            "FILE OPERATIONS:",
            "  `n`                    - Create new file (nested paths OK)",
            "  `Shift-D`              - Create new directory (mkdir -p)",
            "  `d`                    - Delete selection (or cursor entry)",
            "  `y`                    - Yank selection into paste buffer",
            "  `m`                    - Cut selection into paste buffer",
            "  `p`                    - Paste buffer into current dir",
            "  `u` / `Ctrl-r`         - Undo / redo last file op",
            "  `Shift-R`              - Rename cursor entry",
            "",
            "COMMAND-MODE:",
            "  `:q`                   - Exit file browser",
            "  `:cd <path>`           - Change directory",
            "  `:cdr`                 - Jump to project root",
            "  `:pwd`                 - Print working directory",
            "  `:mkdir <name>`/`:md`  - Create directory",
            "  `:new <name>`/`:touch` - Create file (open/replace prompt",
            "                            if it already exists)",
            "  `:delete`/`:d`/`:rm`   - Delete cursor entry",
            "  `:rename <name>`       - Rename (also `:r` / `:mv`)",
            "  `:/<regex>`/`:?<re>`   - Regex match in listing.",
            "                           Ctrl-J/K cycle matches; Ctrl-Space",
            "                           or Ctrl-N toggles selection on all",
            "                           matches.",
            "  `:run <cmd>`           - Run shell cmd in CWD; opens output",
            "                           view. Tab after `run ` appends the",
            "                           selected files (sorted, quoted).",
            "                           See `:help run`.",
        };
    }
    else if(topic_lower == "run")
    {
        lines = {
            "# :run Command And Output View",
            "",
            "INVOKE FROM FILE BROWSER:",
            "  `:run <shell command>` - executes the command in the file",
            "    browser's current directory and opens its combined",
            "    stdout+stderr in a scrollable view.",
            "",
            "  Tab completion: with files selected in the browser, pressing",
            "  Tab on a `run ...` prompt appends the selected paths,",
            "  alphabetical and shell-quoted. So `:run zip a.zip` then Tab",
            "  becomes `:run zip a.zip 'foo bar.txt' baz.txt ...`.",
            "",
            "INSIDE THE OUTPUT VIEW:",
            "  `j` / `k`              - Cursor down / up",
            "  `Ctrl-d` / `Ctrl-u`    - Half-page down / up",
            "  `gg` / `G`             - Top / bottom",
            "  `Space`                - Toggle row selection",
            "  `Shift-V`              - Start/stop visual-line selection.",
            "                           Exiting with V keeps the range; V",
            "                           again extends. Same model as the",
            "                           file browser.",
            "  `Space` while visual   - Commit visual range and exit",
            "  `Esc` while visual     - Cancel current segment",
            "  `Esc Esc`              - Clear all selection",
            "  `y`                    - Yank selected rows (or cursor row",
            "                           if none) to system clipboard +",
            "                           yank buffer; selection preserved.",
            "  `/`                    - Incremental search; matches show",
            "                           with grey/black highlight.",
            "  `n` / `Shift-N`        - Next / previous match",
            "  `q`                    - Clear search highlight if active,",
            "                           else return to file browser.",
            "  `Esc`                  - Same as q at rest.",
            "",
            "Long lines wrap on screen but count as a single logical row",
            "for cursor movement and selection.",
        };
    }
    else if(
        topic_lower == "diagnostics" || topic_lower == "diag" ||
        topic_lower == "keylog")
    {
        lines = {
            "# Diagnostics",
            "",
            "LSP DIAGNOSTICS:",
            "  `Space-e` on a row with an LSP warning/error opens the",
            "  diagnostic popup.",
            "",
            "  `:set emitlsp=false` - Hide/store no LSP diagnostics.",
            "  `:set emitlsp=true`  - Re-enable LSP diagnostics.",
            "  `:set emitlsp?`      - Show current setting.",
            "",
            "CONFIG:",
            "  `editor.emitlsp: false` disables LSP diagnostic markers and",
            "  popup at startup without disabling completion, "
            "go-to-definition,",
            "  formatting, semantic tokens, or other LSP features.",
            "",
            "ENVIRONMENT VARIABLES:",
            "  `UVIM_KEYLOG=<path>`   - Log every byte read from stdin to",
            "                           `<path>` (append mode). Useful to",
            "                           see what your terminal sends for a",
            "                           given key combination.",
            "                           ESC bytes are written as `\\e`,",
            "                           printable bytes as themselves,",
            "                           anything else as `<HH>`.",
            "  `UVIM_DISABLE_SYNC_OUTPUT` - When set/non-empty, disables",
            "                           the synchronized-update terminal",
            "                           protocol (default-on inside tmux).",
            "",
            "EXAMPLE:",
            "  `UVIM_KEYLOG=/tmp/uvim_keys.log uvim somefile`",
            "  Press the key, quit, then `cat /tmp/uvim_keys.log`.",
        };
    }
    else if(
        topic_lower == "lsp" || topic_lower == "lsp-install" ||
        topic_lower == "lspinfo")
    {
        lines = {
            "# LSP Setup And Troubleshooting",
            "",
            "CHECK STATUS:",
            "  `:lspinfo` - Shows ACTIVE/ON/OFF and missing runtime/binary",
            "               details (for example missing `node`)",
            "",
            "REQUIRED BINARIES:",
            "  C/C++: `clangd`",
            "  Python: `pyright-langserver` (or `pylsp`)",
            "  Mlang: `mlangd-mla` (or `mlangd` fallback)",
            "  HTML: `vscode-html-language-server` + `node`",
            "  CSS: `vscode-css-language-server` + `node`",
            "  JSON: `vscode-json-language-server` + `node`",
            "  TS/JS: `typescript-language-server` + `node`",
            "  C#: `csharp-ls` (or OmniSharp) + the .NET SDK",
            "",
            "C# (`.cs`, needs a build with C# LSP; highlighting always works):",
            "  `dotnet tool install -g csharp-ls` (uvim also finds it in",
            "  `~/.dotnet/tools`), or OmniSharp (started with `-lsp`).",
            "  Starts with the first `.cs` file, in the folder of the nearest",
            "  `.sln` (else `.csproj`); `--csharp-lsp` starts it at launch,",
            "  `--csharp-lsp-path` picks another server. Completion, `gd`,",
            "  references, diagnostics (`Space-e`) and `:lspinfo` work like",
            "  for C/C++.",
            "",
            "INSTALL ON MACOS:",
            "  `brew install llvm node pyright`",
            "  `npm install -g vscode-langservers-extracted "
            "typescript typescript-language-server`",
            "  `pip install 'python-lsp-server[all]'`",
            "",
            "INSTALL ON LINUX (APT EXAMPLE):",
            "  `sudo apt install clangd nodejs npm`",
            "  `npm install -g vscode-langservers-extracted "
            "typescript typescript-language-server pyright`",
            "  `pip install 'python-lsp-server[all]'`",
            "",
            "INSTALL ON WINDOWS:",
            "  `winget install OpenJS.NodeJS`",
            "  `winget install LLVM.LLVM`",
            "  `npm install -g vscode-langservers-extracted "
            "typescript typescript-language-server pyright`",
            "  `pip install \"python-lsp-server[all]\"`",
            "",
            "NOTES:",
            "  - If npm global binaries are not found, add npm's bin directory",
            "    to PATH and restart your shell/editor.",
            "  - In uvim config, set explicit `*LspPath` values if needed.",
            "  - `:set emitlsp=false` hides LSP warning/error diagnostics and",
            "    disables the `Space-e` diagnostic popup without stopping "
            "LSPs.",
            "  - Build with `-DUVIM_DEBUG_LSP=ON` to write LSP startup and",
            "    stderr details to uvim.log instead of `:lspinfo`.",
            "  - Log paths: POSIX `/tmp/uvim.log`; Windows",
            "    `%USERPROFILE%\\Documents\\uvim\\uvim.log`.",
            "  - Override with `uvim --log-file <path>` in logging-enabled",
            "    builds.",
        };
    }
    else if(topic_lower == "tasks")
    {
        lines = {
            "# Background Tasks",
            "",
            "Heavy operations (grep/ripgrep, rg cache indexing, git, "
            "formatters, compilers, `:loc`, `:loctotal`, file indexing) run "
            "on background workers so the editor never freezes.",
            "",
            "STATUS BAR:",
            "  `(2/5) ⠹ git log ▕████▌     ▏ 45% 3s` - queue position, "
            "spinner, progress, elapsed",
            "  LSP servers that report `$/progress` (indexing, analysis) and "
            "slow LSP requests are shown the same way.",
            "  Keys typed while a result is awaited are queued and replayed "
            "in order; `[N keys queued]` shows how many.",
            "  `Ctrl-c` cancels the task being waited on.",
            "",
            "ORDERING:",
            "  Tasks on the same channel (e.g. all git commands) run in the "
            "order issued; a newer one waits for older ones and never "
            "overrides them. Different channels run concurrently.",
            "",
            "`:tasks` VIEW:",
            "  `j`/`k`     - Select task",
            "  `K`/`J`     - Move a waiting task earlier/later",
            "  `x` or `dd` - Cancel (waiting: remove, running: stop)",
            "  `q`/`Esc`   - Close",
            "",
            "SETTINGS:",
            "  `:set progressstyle=color`  - Glyph bar with theme colours "
            "(default)",
            "  `:set progressstyle=glyph`  - `▕████▌     ▏` eighth-block bar",
            "  `:set progressstyle=ascii`  - `[=====.....]`",
            "  Theme keys `theme.progress.fg` / `theme.progress.bg` set the "
            "bar fill and track colours.",
        };
    }
    else if(topic_lower == "palette" || topic_lower == "commandpalette")
    {
        lines = {
            "# Command Palette",
            "",
            "`Space Space` (or `:palette`) - in normal and visual modes -",
            "opens a floating picker, like",
            "Ctrl-p and Ctrl-a, listing what uVim can do by name - \"Open",
            "file browser\", \"Change buffer\", \"Save all files\", \"Git: "
            "log\"",
            "- with the key or command that does the same on the right",
            "(`Space x`, `Ctrl-W`, `:wa`). Choosing one does exactly that.",
            "All other `:` commands follow, titled by what they do.",
            "",
            "  Type        - Filter by name, words or command",
            "                (`file br`, `change buf`, `wa`, `git log` ...)",
            "  `Ctrl-j`/`Ctrl-k`, arrows - Move the selection",
            "  `PgDn`/`PgUp` - Move ten rows",
            "  `Enter`     - Run the selected command",
            "  `Ctrl-u`    - Clear the search",
            "  `Esc`       - Close (Backspace on an empty search too)",
            "",
            "Best matches come first: an exact command (`wa`), a name or",
            "command starting with the search, every search word starting",
            "a word of the name (`file br`), names containing the words,",
            "then fuzzy matches.",
            "",
            "Entries marked `…` need an argument (Open file…, Save as…):",
            "Enter opens the command line with the command typed, e.g.",
            "`:e `, ready for the file name. Everything else runs right",
            "away, as if you pressed its keys or typed its command.",
            "",
            "The window size follows `:set picker.width=` /",
            "`picker.height=`; `:set pickerstyle=fullscreen` uses the whole",
            "screen like the other pickers.",
        };
    }
    else if(
        topic_lower == "keys" || topic_lower == "iskitty" ||
        topic_lower == "kitty")
    {
        lines = {
            "# Ctrl+Shift Keys and Alt Alternatives",
            "",
            "Some uvim keys are Ctrl+Shift+letter chords. A terminal can only",
            "send those when it reports modifiers (kitty keyboard protocol or",
            "xterm modifyOtherKeys): kitty, WezTerm, ghostty, foot, alacritty,",
            "recent xterm, iTerm2 with \"Report modifiers using CSI u\". Other",
            "terminals (macOS Terminal, Windows console/PowerShell, ...) send",
            "Ctrl+Shift+J exactly like Ctrl+J, so uvim cannot tell them apart.",
            "",
            "On those terminals use Alt instead - still a single chord:",
            "",
            "  Ctrl+Shift      Alt        Action",
            "  `Ctrl-Shift-h`  `Alt-h`    Focus pane left / move buffer left",
            "  `Ctrl-Shift-l`  `Alt-l`    Focus pane right / move buffer right",
            "  `Ctrl-Shift-k`  `Alt-k`    Focus pane above; scroll preview up",
            "  `Ctrl-Shift-j`  `Alt-j`    Focus pane below; scroll preview "
            "down",
            "  `Ctrl-Shift-x`  `Alt-x`    Close matching buffers (buffer "
            "browser)",
            "",
            "Preview scrolling works in the Ctrl-p / Ctrl-a pickers; pane",
            "focus in normal mode and the file browser.",
            "",
            "SETTING:",
            "  `:set iskitty=false` - Alt-h/j/k/l/x act as the Ctrl+Shift keys",
            "  `:set iskitty=true`  - Alt keys are plain Esc + key again",
            "  `:set iskitty?`      - Show the current mode",
            "  Config file: `iskitty = false` (or `editor.iskitty`).",
            "  Default: detected at startup - true in kitty, WezTerm, ghostty,",
            "  foot and alacritty, false everywhere else.",
            "",
            "HOW IT BEHAVES:",
            "  - Ctrl+Shift keys always work when the terminal reports them,",
            "    whatever `iskitty` says. `iskitty=false` only adds the Alt",
            "    keys, so it is safe in any terminal.",
            "  - Key hints follow the setting, e.g.",
            "    `[Alt+J/K: scroll preview]` or `[Ctrl+Shift+J/K: ...]`.",
            "  - Terminals send Alt+j as Esc and j in one go. uvim treats it",
            "    as the chord only when the letter arrives together with the",
            "    Esc; pressing Esc and then j yourself stays Esc, then j.",
            "",
            "TERMINAL SETUP:",
            "  macOS Terminal: Settings > Profiles > Keyboard > enable",
            "    \"Use Option as Meta key\" (otherwise Option-j types `∆`).",
            "  iTerm2: Profiles > Keys > Left/Right Option key: `Esc+`.",
            "  Windows Terminal / PowerShell: Alt works as is.",
            "  tmux: Alt keys pass through; for Ctrl+Shift set",
            "    `extended-keys on`.",
        };
    }
    else if(topic_lower == "toolinfo")
    {
        lines = {
            "# External Tool Info",
            "",
            "`:toolinfo` shows whether uvim can find external search and "
            "formatter tools enabled in this build.",
            "",
            "TOOLS:",
            "  `fzf`        - Fuzzy finder executable",
            "  `rg/ripgrep` - Ripgrep executable used by grep-style search",
            "  `clang-format` - C/C++ formatter",
            "  `mlang-format` - Mlang formatter (with LSP fallback)",
            "  `ruff`/`black` - Configured Python formatter; ruff also lints",
            "  `robocop`      - Robot Framework formatter",
            "  `python`       - JSON/YAML formatter runtime",
            "",
            "The view reports FOUND or MISSING and prints the resolved binary "
            "path when available.",
            "Tools disabled by the selected build preset are reported as not "
            "compiled.",
            "On Windows, uvim also checks common executable suffixes like "
            "`.exe`, `.cmd`, `.bat`, and `.com`.",
            "",
            "KEYS:",
            "  `j`/`k`   - Scroll",
            "  `r`       - Refresh",
            "  `q`/`Esc` - Close",
        };
    }
    else if(
        topic_lower == "logging" || topic_lower == "logs" ||
        topic_lower == "uvimlog")
    {
        lines = {
            "# uvim Logging",
            "",
            "BUILD OPTIONS:",
            "  `-DUVIM_DEBUG_LSP=ON`     - Enable LSP startup/stderr/debug",
            "                              logging.",
            "  `-DUVIM_DEBUG_LOGGING=ON` - Enable broader editor debug",
            "                              logging.",
            "",
            "DEFAULT LOG FILE:",
            "  POSIX:   `/tmp/uvim.log`",
            "  Windows: `%USERPROFILE%\\Documents\\uvim\\uvim.log`",
            "",
            "RUNTIME OVERRIDE:",
            "  `uvim --log-file <path> file.cpp`",
            "",
            "NOTES:",
            "  - Logging is compiled out by default.",
            "  - `--log-file` is accepted in normal builds, but no uvim log",
            "    rows are written unless logging was enabled at CMake time.",
            "  - LSP rows include a server signature after the timestamp,",
            "    for example `[CLANGD]`, `[PYTHON]`, `[ROBOT]`, `[TS]`.",
        };
    }
    else
    {
        // Unknown topic - show available topics
        lines = {
            "# Unknown topic: " + helpTopic,
            "",
            "Available help topics:",
            "  `:help commands`",
            "  `:help modes`",
            "  `:help navigation`",
            "  `:help editing`",
            "  `:help files`",
            "  `:help filebrowser`",
            "  `:help run`",
            "  `:help windows`",
            "  `:help buffers`",
            "  `:help search`",
            "  `:help clipboard`",
            "  `:help git`",
            "  `:help ga`",
            "  `:help gb`",
            "  `:help gbl`",
            "  `:help gj`",
            "  `:help gbv`",
            "  `:help gs`",
            "  `:help emitasm`",
            "  `:help lsp`",
            "  `:help logging`",
            "  `:help diagnostics`",
            "  `:help keys`",
            "  `:help palette`",
            "",
            "Type `:help` to see the main help page.",
        };
    }

    selectedLine = 0;
    if(isMainHelpTopic(topic_lower))
    {
        for(int i = 0; i < (int)lines.size(); ++i)
        {
            if(topicForLine(i).has_value())
            {
                selectedLine = i;
                break;
            }
        }
    }
}

std::optional<ModeState> HelpMode::executeCommand(
    ModeContext& ctx,
    std::string_view commandLine)
{
    return dispatchCommandLine(
        ctx,
        commandLine,
        [&](ModeContext& ctx,
            const ParsedCommand& command,
            std::optional<ModeState>& nextState) -> bool
        {
            // Handle :help command to navigate to different topics
            if(command.cmd == "help" || command.cmd == "h")
            {
                std::string newTopic = command.args.empty() ? "" : command.args;
                topic = newTopic;
                scrollOffset = 0;
                loadHelpContent(topic);
                ctx.setStatusMessage(
                    "Help: " + (topic.empty() ? "index" : topic));
                ctx.requestFullRedraw();
                return true;
            }

            // Handle :q to exit help
            if(command.cmd == "q" || command.cmd == "q!")
            {
                if(!previousFile.empty())
                {
                    ctx.openFile(std::string_view(previousFile));
                    nextState = NormalMode{};
                }
                else
                {
                    nextState = WelcomeMode{};
                }
                return true;
            }

            // Unknown command
            return false;
        },
        [&](ModeContext& ctx, std::string_view line)
        { return dispatchEditorCommand(ctx, line, previousFile, false); });
}
} // namespace editor::statemachine
