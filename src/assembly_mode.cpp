#include "assembly_mode.h"

#include "ascii.h"
#include "asm_documentation.h"
#include "editor.h"
#include "editor_path_utilities.h"
#include "header_help.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include "widgets/status_bar.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace editor::statemachine
{
namespace
{
std::vector<std::string> helpTokens()
{
    return {"[j/k: rows]",
            "[h/l: horizontal, 0: line start]",
            "[PgUp/PgDn: page]",
            "[v: source split]",
            "[" + std::string(Terminal::shiftChordLabel()) + "-H/L: pane]",
            "[i: inspect]",
            "[Ctrl-J/K: scroll help]",
            "[q/Esc: back]"};
}

int contentRows(const Editor& editor, bool split = false)
{
    return std::max(1,
                    editor.screenRows - 1 - (split ? 1 : 0) -
                        HeaderHelp::lineCount(helpTokens(), editor.screenCols));
}

std::string displayText(std::string_view line)
{
    std::string out;
    for(unsigned char c : line)
    {
        if(c == '\t')
            out.append(4 - out.size() % 4, ' ');
        else
            out += c < 32 || c == 127 ? '?' : static_cast<char>(c);
    }
    return out;
}

std::string clipped(std::string_view line, int columns)
{
    std::string out;
    int width = 0;
    for(int i = 0; i < static_cast<int>(line.size());)
    {
        const int end = text_utils::nextUtf8CharStart(line, i);
        const auto glyph = line.substr(i, end - i);
        const int cells = text_utils::utf8DisplayWidth(glyph);
        if(width + cells > columns)
            break;
        out += glyph;
        width += cells;
        i = end;
    }
    return out;
}

void appendSyntax(std::string& out, const std::string& line,
                  const std::vector<Token>& tokens, int left, int width,
                  const Theme& theme, int dimLevel)
{
    std::vector<TokenType> colors(line.size(), TOKEN_NORMAL);
    for(const auto& token : tokens)
    {
        const int first =
            std::clamp(token.start, 0, static_cast<int>(line.size()));
        const int last = std::clamp(token.start + token.length, first,
                                    static_cast<int>(line.size()));
        std::fill(colors.begin() + first, colors.begin() + last, token.type);
    }
    int column = 0;
    int used = 0;
    TokenType previous = TOKEN_NORMAL;
    out += dimLevel > 0 ? theme.dimmedSyntax(TOKEN_NORMAL, dimLevel)
                        : theme.baseFg();
    for(int i = 0; i < static_cast<int>(line.size());)
    {
        const int end = text_utils::nextUtf8CharStart(line, i);
        const auto glyph = std::string_view(line).substr(i, end - i);
        const int cells = text_utils::utf8DisplayWidth(glyph);
        if(column >= left)
        {
            if(used + cells > width)
                break;
            if(colors[i] != previous)
            {
                previous = colors[i];
                out += dimLevel > 0 ? theme.dimmedSyntax(previous, dimLevel)
                                    : theme.syntax(previous);
            }
            out += glyph;
            used += cells;
        }
        column += cells;
        i = end;
    }
    out.append(std::max(0, width - used), ' ');
}

std::vector<std::string> wrappedDocs(std::string_view text, int width)
{
    std::vector<std::string> lines;
    std::istringstream stream{std::string(text)};
    std::string line;
    while(std::getline(stream, line))
    {
        line = displayText(line);
        while(text_utils::utf8DisplayWidth(line) > width)
        {
            std::string part = clipped(line, width);
            const auto space = part.find_last_of(' ');
            if(space != std::string::npos && space > part.size() / 2)
                part.resize(space);
            if(part.empty())
                break;
            lines.push_back(part);
            line.erase(0, part.size());
            if(!line.empty() && line.front() == ' ')
                line.erase(0, 1);
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

enum class DocumentationBlock
{
    None,
    Text,
    Assembly
};

std::string coloredDocumentation(std::string_view line, const Theme& theme,
                                 DocumentationBlock& block,
                                 const std::vector<Token>& assemblyTokens)
{
    const auto first = line.find_first_not_of(' ');
    const auto text = first == std::string_view::npos ? std::string_view{}
                                                      : line.substr(first);
    if(text.starts_with("```"))
    {
        block = block != DocumentationBlock::None ? DocumentationBlock::None
                : text == "```asm" || text == "```assembly" || text == "```s"
                    ? DocumentationBlock::Assembly
                    : DocumentationBlock::Text;
        return theme.uiDim() + std::string(line);
    }
    if(block == DocumentationBlock::Assembly)
    {
        std::string out;
        appendSyntax(out, std::string(line), assemblyTokens, 0,
                     text_utils::utf8DisplayWidth(line), theme, 0);
        return out;
    }
    // API HTML is cached as plain text, so its section headings have no '#'.
    auto heading = text;
    while(!heading.empty() && (heading.back() == ' ' || heading.back() == ':'))
        heading.remove_suffix(1);
    const bool section =
        heading == "Description" || heading == "Operation" ||
        heading == "Flags Affected" || heading == "Affected Flags" ||
        heading == "Exceptions" || heading == "Protected Mode Exceptions" ||
        heading == "Real-Address Mode Exceptions" ||
        heading == "Virtual-8086 Mode Exceptions" ||
        heading == "Compatibility Mode Exceptions" ||
        heading == "64-Bit Mode Exceptions" ||
        heading == "Intrinsic Equivalent" ||
        heading == "Intel C/C++ Compiler Intrinsic Equivalent" ||
        heading == "Opcode" || heading == "Instruction Operand Encoding" ||
        heading == "Syntax" || heading == "Examples";
    if(text.starts_with('#') || section)
        return theme.uiAccent() + Terminal::ESC_BOLD + std::string(line);

    const bool reference =
        text.starts_with("Documentation:") || text.starts_with("Reference:") ||
        text.starts_with("Source:") || text.starts_with("API:");
    const auto& bodyColor = reference ? theme.uiDim() : theme.baseFg();
    std::string out = bodyColor;
    size_t pos = 0;
    while(pos < line.size())
    {
        if(line.substr(pos).starts_with("https://") ||
           line.substr(pos).starts_with("http://"))
        {
            const auto end = line.find_first_of(" \t)", pos);
            const auto length =
                end == std::string_view::npos ? line.size() - pos : end - pos;
            out += theme.uiDim() + std::string(line.substr(pos, length)) +
                   bodyColor;
            pos += length;
        }
        else if(line[pos] == '`')
        {
            const auto end = line.find('`', pos + 1);
            const auto length = end == std::string_view::npos
                                    ? line.size() - pos
                                    : end - pos + 1;
            out += theme.syntax(TOKEN_KEYWORD) +
                   std::string(line.substr(pos, length)) + bodyColor;
            pos += length;
        }
        else
            out += line[pos++];
    }
    return out;
}
} // namespace

void AssemblyMode::on_enter(ModeContext& ctx)
{
    Editor& editor = *ctx.editor;
    content = std::make_shared<Content>();
    assembly_compiler::Request request;
    std::string error;
    if(!assembly_compiler::parse(arguments, request, error))
    {
        content->compiling = false;
        content->result.diagnostics = std::move(error);
        return;
    }
    split = sourcePane = request.split;
    if(request.path == ".")
    {
        if(!editor.currentBuffer || !editor.lines)
        {
            content->compiling = false;
            content->result.diagnostics =
                "asm: no current buffer; use :asm path/to/file.cpp";
            return;
        }
        request.path = editor.currentBuffer->filename;
        request.sourceLines = *editor.lines;
        sourceCursor = editor.cursorY ? *editor.cursorY : 0;
        sourceName = request.path.empty() ? "[No Name] (buffer snapshot)"
                                          : request.path + " (buffer snapshot)";
    }
    else
        sourceName = request.path;
    if(request.path.empty())
        request.path = "untitled.cpp";
    request.path =
        EditorPathUtilities::resolveEditorPath(request.path).string();
    request.directory = EditorPathUtilities::currentWorkingDirectory().string();
    if(!editor.clangdLspCompileCommandsDir.empty())
        request.compileCommandsDirectory =
            EditorPathUtilities::resolveEditorPath(
                editor.clangdLspCompileCommandsDir)
                .string();
    if(std::filesystem::path(request.compiler).has_parent_path())
        request.compiler =
            EditorPathUtilities::resolveEditorPath(request.compiler).string();
    // A named open buffer is also compiled from its current unsaved contents.
    if(!request.sourceLines)
        for(const auto& buffer : editor.buffers)
            if(!buffer->filename.empty() &&
               EditorPathUtilities::resolveEditorPath(buffer->filename)
                       .lexically_normal() ==
                   std::filesystem::path(request.path).lexically_normal())
            {
                request.sourceLines = buffer->lines;
                sourceName += " (buffer snapshot)";
                break;
            }
    auto result = std::make_shared<assembly_compiler::Result>();
    if(request.sourceLines)
    {
        content->result.sourceLines = *request.sourceLines;
        for(auto& line : content->result.sourceLines)
            line = displayText(line);
    }
    content->result.sourcePath = request.path;
    std::weak_ptr<Content> weak = content;
    compileTask =
        editor
            .submitTask(
                "compile assembly", "assembly",
                [request = std::move(request), result](TaskQueue::Context& task)
                {
                    task.detail("compiling C/C++ to assembly");
                    *result = assembly_compiler::compile(
                        request, [&task] { return task.cancelled(); });
                    for(auto& line : result->lines)
                        line = displayText(line);
                    for(auto& line : result->sourceLines)
                        line = displayText(line);
                },
                [weak, result, &editor]
                {
                    if(auto state = weak.lock(); state && state->active)
                    {
                        state->result = std::move(*result);
                        state->compiling = false;
                        editor.setStatusMessage(
                            state->result.success ? "asm: assembly ready"
                                                  : "asm: compilation failed");
                        editor.needsFullRedraw = true;
                    }
                },
                [weak, &editor]
                {
                    if(auto state = weak.lock(); state && state->active)
                    {
                        state->compiling = false;
                        state->result.diagnostics =
                            "asm: compilation cancelled or failed";
                        editor.needsFullRedraw = true;
                    }
                })
            .id;
    ctx.requestFullRedraw();
}

void AssemblyMode::on_exit(ModeContext& ctx)
{
    if(content)
        content->active = false;
    if(compileTask)
        ctx.editor->tasks.cancel(compileTask);
    if(docsTask)
        ctx.editor->tasks.cancel(docsTask);
    ctx.requestFullRedraw();
}

void AssemblyMode::ensureVisible(const Editor& editor)
{
    if(content && !content->compiling && initialSyncPending)
    {
        synchronizeSelection();
        horizontal = sourceHorizontal = 0;
        initialSyncPending = false;
    }
    const int count =
        content ? static_cast<int>(content->result.lines.size()) : 0;
    cursor = std::clamp(cursor, 0, std::max(0, count - 1));
    const int rows = contentRows(editor, split);
    offset = std::clamp(cursor - rows / 2, 0, std::max(0, count - rows));
    const int sourceCount =
        content ? static_cast<int>(content->result.sourceLines.size()) : 0;
    sourceCursor = std::clamp(sourceCursor, 0, std::max(0, sourceCount - 1));
    sourceOffset = std::clamp(sourceCursor - rows / 2, 0,
                              std::max(0, sourceCount - rows));
}

int AssemblyMode::linkedSourceLine() const
{
    if(!content)
        return 0;
    const auto& mapping = content->result.sourceLineForAssembly;
    if(split && sourcePane)
        return std::find(mapping.begin(), mapping.end(), sourceCursor + 1) !=
                       mapping.end()
                   ? sourceCursor + 1
                   : 0;
    return cursor >= 0 && static_cast<size_t>(cursor) < mapping.size()
               ? mapping[cursor]
               : 0;
}

void AssemblyMode::synchronizeSelection()
{
    if(!split || !content)
        return;
    const auto& mapping = content->result.sourceLineForAssembly;
    if(sourcePane)
    {
        const auto first =
            std::find(mapping.begin(), mapping.end(), sourceCursor + 1);
        if(first != mapping.end())
            cursor = static_cast<int>(first - mapping.begin());
    }
    else if(const int line = linkedSourceLine(); line > 0)
        sourceCursor = line - 1;
}

void AssemblyMode::scheduleInspection(Editor& editor)
{
    // Invalidate the previous response immediately, but keep its text while
    // browsing. Only the final selection after a pause starts a new request.
    ++content->docsGeneration;
    if(docsTask)
        editor.tasks.cancel(docsTask);
    docsTask = 0;
    content->docsLoading = false;
    content->docsIndicator = false;
    docsRefreshAt =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
}

void AssemblyMode::processIdle(Editor& editor,
                               std::chrono::steady_clock::time_point now)
{
    if(!content || !content->active || !inspect)
        return;
    if(docsRefreshAt && now >= *docsRefreshAt)
        inspectLine(editor);
    if(content->docsLoading && !content->docsIndicator &&
       now - docsStarted >= std::chrono::milliseconds(300))
    {
        content->docsIndicator = true;
        editor.needsFullRedraw = true;
    }
}

void AssemblyMode::inspectLine(Editor& editor)
{
    docsRefreshAt.reset();
    content->docsIndicator = false;
    editor.needsFullRedraw = true;
    ++content->docsGeneration;
    if(docsTask)
        editor.tasks.cancel(docsTask);
    docsTask = 0;
    content->docsLoading = false;
    docsOffset = 0;
    if(!inspect || content->compiling || content->result.lines.empty())
        return;
    if(split && sourcePane && linkedSourceLine() == 0)
    {
        content->docsLoading = false;
        content->docsTitle = "Source line " + std::to_string(sourceCursor + 1);
        content->docsText =
            "No assembly instruction is mapped to this source line. "
            "It may be a declaration, a comment, or optimized away.";
        return;
    }
#ifndef UVIM_ENABLE_ASM_DOCS
    content->docsText =
        "Instruction documentation is not compiled into this build.";
    content->docsTitle = "Instruction help";
#else
    const auto help =
        asm_documentation::embedded(content->result.lines[cursor]);
    if(!help)
    {
        content->docsTitle = "Instruction help";
        content->docsText =
            "No instruction documentation for this row "
            "(labels, directives and comments have no instruction help).";
        return;
    }
    const std::string& instruction = content->result.lines[cursor];
    const auto start = instruction.find_first_not_of(" \t");
    const std::string example =
        "```asm\n" +
        instruction.substr(start == std::string::npos ? 0 : start) +
        "\n```\n\n";
    content->docsTitle = help->title;
    content->docsText = example + help->text;
    if(!editor.asmDocsApi)
        return;
    content->docsLoading = true;
    docsStarted = std::chrono::steady_clock::now();
    auto text = std::make_shared<std::string>();
    auto title = std::make_shared<std::string>();
    const auto generation = content->docsGeneration;
    std::weak_ptr<Content> weak = content;
    docsTask =
        editor.tasks
            .submit(TaskQueue::Job{
                "assembly instruction help", "asm-docs",
                [line = content->result.lines[cursor], example, text,
                 title](TaskQueue::Context& task)
                {
                    if(task.cancelled())
                        return;
                    auto location = asm_documentation::find(line, 0, true);
                    if(!location)
                        return;
                    *title = location->mnemonic + " (" + location->arch + ")";
                    std::ifstream file(location->path);
                    std::string row;
                    for(int i = 0; std::getline(file, row); ++i)
                    {
                        if(i < location->line)
                            continue;
                        if(location->line > 0 && i > location->line &&
                           row.rfind("## ", 0) == 0)
                            break;
                        *text += row + '\n';
                    }
                    // Cached API documents include a summary and fetch
                    // metadata. Put the API's operand forms/examples first in
                    // the small popup.
                    if(location->line == 0)
                    {
                        for(std::string_view marker :
                            {"Compiler Explorer documentation:",
                             "Source documentation fallback:"})
                        {
                            const auto start = text->find(marker);
                            if(start == std::string::npos)
                                continue;
                            text->erase(0, start + marker.size());
                            const auto first = text->find_first_not_of("\r\n");
                            if(first != std::string::npos)
                                text->erase(0, first);
                            break;
                        }
                    }
                    if(!text->empty())
                        *text = example + *text;
                },
                [weak, generation, text, title, &editor]
                {
                    if(auto state = weak.lock();
                       state && state->active &&
                       state->docsGeneration == generation)
                    {
                        state->docsLoading = false;
                        state->docsIndicator = false;
                        if(!text->empty())
                        {
                            state->docsText = std::move(*text);
                            if(!title->empty())
                                state->docsTitle = std::move(*title);
                        }
                        editor.needsFullRedraw = true;
                    }
                },
                [weak, generation, &editor]
                {
                    if(auto state = weak.lock();
                       state && state->active &&
                       state->docsGeneration == generation)
                    {
                        state->docsLoading = false;
                        state->docsIndicator = false;
                        editor.needsFullRedraw = true;
                    }
                },
                false})
            .id;
#endif
}

std::optional<ModeState> AssemblyMode::handle(ModeContext& ctx,
                                              const ModeKeyEvent& event)
{
    Editor& editor = *ctx.editor;
    const int key = event.key;
    if(key == 'q' || key == keyCode(control::ControlKey::ESC))
        return defaultExitMode(&editor);
    ensureVisible(editor);
    const int before = cursor;
    const int sourceBefore = sourceCursor;
    const bool wasSource = sourcePane;
    const bool wasSplit = split;
    bool browsedRow = false;
    if(key == keyCode(control::ControlKey::SHIFT_CTRL_H) && split)
        sourcePane = true;
    else if(key == keyCode(control::ControlKey::SHIFT_CTRL_L) && split)
        sourcePane = false;
    else if(key == 'v')
    {
        split = !split;
        if(!split)
            sourcePane = false;
        else
            synchronizeSelection();
    }
    else if(key == 'i')
    {
        inspect = !inspect;
        inspectLine(editor);
    }
    else if(inspect && key == keyCode(control::ControlKey::CTRL_J))
        ++docsOffset;
    else if(inspect && key == keyCode(control::ControlKey::CTRL_K))
        docsOffset = std::max(0, docsOffset - 1);
    else
    {
        int& row = split && sourcePane ? sourceCursor : cursor;
        int& left = split && sourcePane ? sourceHorizontal : horizontal;
        browsedRow = key == 'j' || key == 'k' || key == 'g' || key == 'G' ||
                     key == keyCode(navigation::NavigationKey::ARROW_DOWN) ||
                     key == keyCode(navigation::NavigationKey::ARROW_UP) ||
                     key == keyCode(navigation::NavigationKey::PAGE_DOWN) ||
                     key == keyCode(navigation::NavigationKey::PAGE_UP) ||
                     key == keyCode(navigation::NavigationKey::HOME) ||
                     key == keyCode(navigation::NavigationKey::END) ||
                     key == keyCode(control::ControlKey::CTRL_D) ||
                     key == keyCode(control::ControlKey::CTRL_U);
        if(key == '0')
            left = 0;
        else if(key == 'j' ||
                key == keyCode(navigation::NavigationKey::ARROW_DOWN))
            ++row;
        else if(key == 'k' ||
                key == keyCode(navigation::NavigationKey::ARROW_UP))
            --row;
        else if(key == 'h' ||
                key == keyCode(navigation::NavigationKey::ARROW_LEFT))
            left = std::max(0, left - 4);
        else if(key == 'l' ||
                key == keyCode(navigation::NavigationKey::ARROW_RIGHT))
            left += 4;
        else if(key == keyCode(navigation::NavigationKey::PAGE_DOWN))
            row += contentRows(editor, split);
        else if(key == keyCode(navigation::NavigationKey::PAGE_UP))
            row -= contentRows(editor, split);
        else if(key == keyCode(control::ControlKey::CTRL_D))
            row += std::max(1, contentRows(editor, split) / 2);
        else if(key == keyCode(control::ControlKey::CTRL_U))
            row -= std::max(1, contentRows(editor, split) / 2);
        else if(key == 'g' || key == keyCode(navigation::NavigationKey::HOME))
            row = 0;
        else if(key == 'G' || key == keyCode(navigation::NavigationKey::END))
            row = static_cast<int>((split && sourcePane
                                        ? content->result.sourceLines
                                        : content->result.lines)
                                       .size()) -
                  1;
    }
    ensureVisible(editor);
    // Switching focus preserves the selected instruction among multiple
    // matches.
    if((sourcePane != wasSource && !sourcePane) ||
       (sourcePane == wasSource &&
        (sourceCursor != sourceBefore || cursor != before)))
        synchronizeSelection();
    if(browsedRow || cursor != before || sourceCursor != sourceBefore ||
       sourcePane != wasSource || split != wasSplit)
        horizontal = sourceHorizontal = 0;
    ensureVisible(editor);
    if(inspect && (cursor != before || sourceCursor != sourceBefore ||
                   sourcePane != wasSource))
        scheduleInspection(editor);
    ctx.requestFullRedraw();
    return std::nullopt;
}

void AssemblyMode::draw(Editor& editor)
{
    ensureVisible(editor);
    if(inspect && !content->compiling && content->docsText.empty() &&
       !content->docsLoading && !docsRefreshAt)
        inspectLine(editor);
    std::string out = Terminal::ESC_HIDE_CURSOR;
    out += Terminal::ESC_CURSOR_HOME;
    out += editor.theme.reset();
    out += Terminal::ESC_CLEAR_LINE;
    out += editor.theme.uiPrompt();
    const auto& result = content->result;
    const std::string architecture =
        result.architecture.empty()
            ? (content->compiling ? "target pending" : "unknown")
            : result.architecture;
    out +=
        clipped(displayText(" Assembly: [" + architecture + "] " + sourceName),
                editor.screenCols);
    out += editor.theme.reset();
    HeaderHelp::append(out, editor.theme, editor.screenCols, helpTokens());
    const int rows = contentRows(editor, split);
    const bool sideBySide = split && editor.screenCols >= 40;
    const int sourceWidth =
        sideBySide ? (editor.screenCols - 1) / 2 : editor.screenCols;
    const int assemblyWidth =
        sideBySide ? editor.screenCols - sourceWidth - 1 : editor.screenCols;
    const int linked = linkedSourceLine();
    if(result.success)
        editor.highlightPreviewLines(
            highlight, "assembly.s", result.lines,
            std::min(result.lines.size(), static_cast<size_t>(offset + rows)));
    if(split)
        editor.highlightPreviewLines(
            sourceHighlight, result.sourcePath, result.sourceLines,
            std::min(result.sourceLines.size(),
                     static_cast<size_t>(sourceOffset + rows)));
    const auto errors =
        wrappedDocs(result.diagnostics, std::max(1, assemblyWidth - 2));
    auto separator = [&]
    {
        out += editor.theme.reset() + editor.theme.uiGutter();
        if(editor.utf8Mode)
            text_utils::appendU8(out, ascii::BOX_LIGHT_VERTICAL);
        else
            out += '|';
    };
    auto heading = [&](bool source, int width)
    {
        const bool active = source == sourcePane;
        out += editor.theme.reset() +
               (active ? editor.theme.uiAccent() : editor.theme.uiDim());
        const auto text =
            clipped((source ? std::string(" Source")
                            : " Assembly [" + architecture + "]") +
                        (active ? " [active]" : ""),
                    width);
        out += text;
        out.append(std::max(0, width - text_utils::utf8DisplayWidth(text)),
                   ' ');
    };
    if(split)
    {
        out += Terminal::NEWLINE_CLEAR;
        if(sideBySide)
        {
            heading(true, sourceWidth);
            separator();
            heading(false, assemblyWidth);
        }
        else
            heading(sourcePane, editor.screenCols);
    }
    const bool hasOrigins = result.sourceOrigins.size() == result.lines.size();
    const bool hasDirectMappings =
        result.sourceLineForAssembly.size() == result.lines.size();
    auto paneRow = [&](bool source, int row, int width)
    {
        out += editor.theme.reset();
        const auto& lines = source ? result.sourceLines : result.lines;
        const auto& tokens = source ? sourceHighlight.tokens : highlight.tokens;
        const int index = (source ? sourceOffset : offset) + row;
        const bool active = !split || source == sourcePane;
        const bool current = index == (source ? sourceCursor : cursor);
        if((source || result.success) && index < static_cast<int>(lines.size()))
        {
            const bool mapped =
                split && linked > 0 &&
                (source ? index + 1 == linked
                        : static_cast<size_t>(index) <
                                  result.sourceLineForAssembly.size() &&
                              result.sourceLineForAssembly[index] == linked);
            if(mapped)
                out += editor.theme.sourceMapping();
            else if(active && current)
                out += editor.theme.selection();
            else if(!source && hasOrigins &&
                    !result.sourceOrigins[index].mainSource &&
                    !result.sourceOrigins[index].path.empty())
                out += editor.theme.externalSource();
            int dimLevel = 0;
            if(!source && editor.asmDimUnmapped &&
               !(hasDirectMappings && result.sourceLineForAssembly[index] > 0))
            {
                dimLevel =
                    hasOrigins && result.sourceOrigins[index].mainSource ? 1
                    : hasOrigins && !result.sourceOrigins[index].path.empty()
                        ? 2
                        : 3;
            }
            out += dimLevel > 0 ? editor.theme.dimmedGutter(dimLevel)
                                : editor.theme.uiGutter();
            const int gutter =
                static_cast<int>(std::to_string(lines.size()).size()) + 2;
            std::string number = std::to_string(index + 1);
            number.insert(
                0, std::max(0, gutter - 2 - static_cast<int>(number.size())),
                ' ');
            out +=
                clipped((active && current ? ">" : " ") + number + " ", width);
            appendSyntax(out, lines[index], tokens[index],
                         source ? sourceHorizontal : horizontal,
                         std::max(0, width - gutter), editor.theme, dimLevel);
        }
        else
        {
            std::string message = "~";
            out += editor.theme.uiGutter();
            if(!source && content->compiling && row == 0)
            {
                out += editor.theme.uiInfo();
                message = " Compiling... q/Esc returns immediately.";
            }
            else if(!source && !result.success &&
                    row < static_cast<int>(errors.size()))
            {
                out += editor.theme.uiError();
                message = " " + errors[row];
            }
            const auto text = clipped(message, width);
            out += text;
            out.append(std::max(0, width - text_utils::utf8DisplayWidth(text)),
                       ' ');
        }
    };
    for(int row = 0; row < rows; ++row)
    {
        out += Terminal::NEWLINE_CLEAR;
        if(sideBySide)
        {
            paneRow(true, row, sourceWidth);
            separator();
            paneRow(false, row, assemblyWidth);
        }
        else
            paneRow(split && sourcePane, row, editor.screenCols);
        out += editor.theme.reset();
    }
    widgets::ViewStatusBarView status{.theme = editor.theme};
    status.modeLabel = name();
    status.context = result.compiler.empty() ? sourceName : result.compiler;
    status.state = content->compiling ? "compiling"
                   : result.success   ? result.options
                                      : "compile failed";
    if(inspect)
        status.flags.push_back("inspect");
    if(!result.compilationDatabase.empty())
        status.flags.push_back("compile_commands");
    if(split)
        status.flags.push_back(linked > 0 ? "source L" + std::to_string(linked)
                                          : "no source mapping");
    if(!(split && sourcePane) && hasOrigins && cursor >= 0 &&
       static_cast<size_t>(cursor) < result.sourceOrigins.size())
    {
        const auto& origin = result.sourceOrigins[cursor];
        if(!origin.mainSource && !origin.path.empty())
            status.flags.push_back(
                "from " +
                std::filesystem::path(origin.path).filename().string() + ":" +
                std::to_string(origin.line));
        else if(!origin.mainSource && !split)
            status.flags.push_back("no source mapping");
    }
    const auto& activeLines =
        split && sourcePane ? result.sourceLines : result.lines;
    status.index = activeLines.empty()
                       ? 0
                       : (split && sourcePane ? sourceCursor : cursor) + 1;
    status.count = static_cast<int>(activeLines.size());
    // Compilation databases commonly contain hundreds of flags. Keep their
    // summary within the status row instead of wrapping over the source view.
    const auto progress = editor.taskStatusText();
    const int rulerWidth = std::max(
        12, 8 + 2 * static_cast<int>(
                        std::to_string(std::max(1, status.count)).size()));
    const int budget = std::max(
        0, editor.screenCols - static_cast<int>(status.modeLabel.size()) - 5 -
               rulerWidth -
               (progress.empty() ? 0
                                 : text_utils::utf8DisplayWidth(progress) + 1));
    auto flagWidth = [&]
    {
        int width = 0;
        for(const auto& flag : status.flags)
            width += text_utils::utf8DisplayWidth(flag) + 3;
        return width;
    };
    while(!status.flags.empty() && flagWidth() > budget)
        status.flags.erase(status.flags.begin());
    const int stateWidth =
        std::max(0, budget - flagWidth() - std::min(20, budget / 2));
    if(text_utils::utf8DisplayWidth(status.state) > stateWidth)
        status.state = stateWidth > 3
                           ? clipped(status.state, stateWidth - 3) + "..."
                           : clipped(status.state, stateWidth);
    editor.appendViewStatusBar(out, std::move(status));

    if(inspect && editor.screenRows >= 8 && editor.screenCols >= 20)
    {
        const int width = std::max(12, std::min(84, editor.screenCols - 4));
        const int height = std::max(4, std::min(15, editor.screenRows - 5));
        const int left = editor.screenCols - width;
        const int top = editor.screenRows - height - 1;
        const auto docs = wrappedDocs(content->docsText, width - 4);
        docsOffset =
            std::clamp(docsOffset, 0,
                       std::max(0, static_cast<int>(docs.size()) - height + 2));
        std::vector<std::string> coloredDocs;
        PreviewHighlight instructionHighlight;
        editor.highlightPreviewLines(instructionHighlight, "inspection.s", docs,
                                     docs.size());
        const std::vector<Token> noTokens;
        auto block = DocumentationBlock::None;
        for(size_t i = 0; i < docs.size(); ++i)
            coloredDocs.push_back(
                coloredDocumentation(docs[i], editor.theme, block,
                                     i < instructionHighlight.tokens.size()
                                         ? instructionHighlight.tokens[i]
                                         : noTokens));
        for(int row = 0; row < height; ++row)
        {
            out += Terminal::cursorPos(top + row, left);
            out += editor.theme.panel();
            out += editor.theme.uiAccent();
            if(row == 0 || row == height - 1)
            {
                std::string title =
                    row == 0 ? " Inspect: " + content->docsTitle +
                                   (content->docsIndicator ? " ... " : " ")
                             : " i: close | Ctrl-J/K: scroll ";
                title = clipped(title, width - 2);
                text_utils::appendU8(out, row == 0
                                              ? ascii::BOX_ROUNDED_TOP_LEFT
                                              : ascii::BOX_ROUNDED_BOTTOM_LEFT);
                out += Terminal::ESC_BOLD;
                out += title;
                out += editor.theme.reset() + editor.theme.panel() +
                       editor.theme.uiAccent();
                text_utils::appendUtf8Repeat(
                    out, ascii::BOX_LIGHT_HORIZONTAL,
                    std::max(0,
                             width - 2 - text_utils::utf8DisplayWidth(title)));
                text_utils::appendU8(
                    out, row == 0 ? ascii::BOX_ROUNDED_TOP_RIGHT
                                  : ascii::BOX_ROUNDED_BOTTOM_RIGHT);
            }
            else
            {
                const int index = docsOffset + row - 1;
                const std::string text =
                    index < static_cast<int>(docs.size()) ? docs[index] : "";
                text_utils::appendU8(out, ascii::BOX_LIGHT_VERTICAL_RIGHT_PAD);
                out += index < static_cast<int>(coloredDocs.size())
                           ? coloredDocs[index]
                           : editor.theme.baseFg();
                out += editor.theme.reset() + editor.theme.panel();
                out.append(
                    std::max(0, width - 3 - text_utils::utf8DisplayWidth(text)),
                    ' ');
                out += editor.theme.uiAccent();
                text_utils::appendU8(out, ascii::BOX_LIGHT_VERTICAL);
            }
            out += editor.theme.reset();
        }
    }
    Terminal::write(out);
    Terminal::flush();
}
} // namespace editor::statemachine
