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
    return {"[j/k: rows]",  "[h/l: horizontal]",       "[PgUp/PgDn: page]",
            "[i: inspect]", "[Ctrl-J/K: scroll help]", "[q/Esc: back]"};
}

int contentRows(const Editor& editor)
{
    return std::max(1,
                    editor.screenRows - 1 -
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
                  const Theme& theme)
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
    out += theme.baseFg();
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
                out += theme.syntax(previous);
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

std::string coloredDocumentation(std::string_view line, const Theme& theme,
                                 bool& codeBlock)
{
    const auto first = line.find_first_not_of(' ');
    const auto text = first == std::string_view::npos ? std::string_view{}
                                                      : line.substr(first);
    if(text.starts_with("```"))
    {
        codeBlock = !codeBlock;
        return theme.uiDim() + std::string(line);
    }
    if(codeBlock)
        return theme.syntax(TOKEN_STRING) + std::string(line);
    if(text.starts_with('#'))
        return theme.uiAccent() + Terminal::ESC_BOLD + std::string(line);

    std::string out = theme.baseFg();
    size_t pos = 0;
    const auto colon = line.find(':');
    if(colon != std::string_view::npos && colon < 32 &&
       (text.starts_with("Documentation:") || text.starts_with("Reference:") ||
        text.starts_with("Source:")))
    {
        out = theme.uiInfo() + std::string(line.substr(0, colon + 1)) +
              theme.baseFg();
        pos = colon + 1;
    }
    while(pos < line.size())
    {
        if(line.substr(pos).starts_with("https://") ||
           line.substr(pos).starts_with("http://"))
        {
            const auto end = line.find_first_of(" \t)", pos);
            const auto length =
                end == std::string_view::npos ? line.size() - pos : end - pos;
            out += theme.uiDirectory() + std::string(line.substr(pos, length)) +
                   theme.baseFg();
            pos += length;
        }
        else if(line[pos] == '`')
        {
            const auto end = line.find('`', pos + 1);
            const auto length = end == std::string_view::npos
                                    ? line.size() - pos
                                    : end - pos + 1;
            out += theme.syntax(TOKEN_KEYWORD) +
                   std::string(line.substr(pos, length)) + theme.baseFg();
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
    const int count =
        content ? static_cast<int>(content->result.lines.size()) : 0;
    cursor = std::clamp(cursor, 0, std::max(0, count - 1));
    const int rows = contentRows(editor);
    offset = std::clamp(offset, 0, std::max(0, count - rows));
    if(cursor < offset)
        offset = cursor;
    if(cursor >= offset + rows)
        offset = cursor - rows + 1;
}

void AssemblyMode::inspectLine(Editor& editor)
{
    ++content->docsGeneration;
    if(docsTask)
        editor.tasks.cancel(docsTask);
    docsOffset = 0;
    if(!inspect || content->compiling || content->result.lines.empty())
        return;
#ifndef UVIM_ENABLE_ASM_DOCS
    content->docsText =
        "Instruction documentation is not compiled into this build.";
    content->docsTitle = "Instruction help";
#else
    content->docsLoading = true;
    content->docsText.clear();
    content->docsTitle = "Instruction help";
    auto text = std::make_shared<std::string>();
    auto title = std::make_shared<std::string>();
    const auto generation = content->docsGeneration;
    std::weak_ptr<Content> weak = content;
    docsTask =
        editor
            .submitTask(
                "assembly instruction help", "asm-docs",
                [line = content->result.lines[cursor], text,
                 title](TaskQueue::Context& task)
                {
                    if(task.cancelled())
                        return;
                    auto location = asm_documentation::find(line, 0);
                    if(!location)
                    {
                        *text = "No instruction documentation for this row "
                                "(labels, directives and comments have no "
                                "instruction help).";
                        return;
                    }
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
                    if(text->empty())
                        *text = "Instruction documentation is unavailable.";
                },
                [weak, generation, text, title, &editor]
                {
                    if(auto state = weak.lock();
                       state && state->active &&
                       state->docsGeneration == generation)
                    {
                        state->docsLoading = false;
                        state->docsText = std::move(*text);
                        if(!title->empty())
                            state->docsTitle = std::move(*title);
                        editor.needsFullRedraw = true;
                    }
                })
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
    if(key == 'i')
    {
        inspect = !inspect;
        inspectLine(editor);
    }
    else if(inspect && key == keyCode(control::ControlKey::CTRL_J))
        ++docsOffset;
    else if(inspect && key == keyCode(control::ControlKey::CTRL_K))
        docsOffset = std::max(0, docsOffset - 1);
    else if(key == 'j' || key == keyCode(navigation::NavigationKey::ARROW_DOWN))
        ++cursor;
    else if(key == 'k' || key == keyCode(navigation::NavigationKey::ARROW_UP))
        --cursor;
    else if(key == 'h' || key == keyCode(navigation::NavigationKey::ARROW_LEFT))
        horizontal = std::max(0, horizontal - 4);
    else if(key == 'l' ||
            key == keyCode(navigation::NavigationKey::ARROW_RIGHT))
        horizontal += 4;
    else if(key == keyCode(navigation::NavigationKey::PAGE_DOWN))
        cursor += contentRows(editor);
    else if(key == keyCode(navigation::NavigationKey::PAGE_UP))
        cursor -= contentRows(editor);
    else if(key == keyCode(control::ControlKey::CTRL_D))
        cursor += std::max(1, contentRows(editor) / 2);
    else if(key == keyCode(control::ControlKey::CTRL_U))
        cursor -= std::max(1, contentRows(editor) / 2);
    else if(key == 'g' || key == keyCode(navigation::NavigationKey::HOME))
        cursor = offset = 0;
    else if(key == 'G' || key == keyCode(navigation::NavigationKey::END))
        cursor = static_cast<int>(content->result.lines.size()) - 1;
    ensureVisible(editor);
    if(cursor != before && inspect)
        inspectLine(editor);
    ctx.requestFullRedraw();
    return std::nullopt;
}

void AssemblyMode::draw(Editor& editor)
{
    ensureVisible(editor);
    if(inspect && !content->compiling && content->docsText.empty() &&
       !content->docsLoading)
        inspectLine(editor);
    std::string out = Terminal::ESC_HIDE_CURSOR;
    out += Terminal::ESC_CURSOR_HOME;
    out += editor.theme.reset();
    out += Terminal::ESC_CLEAR_LINE;
    out += editor.theme.uiPrompt();
    out += clipped(displayText(" Assembly: " + sourceName), editor.screenCols);
    out += editor.theme.reset();
    HeaderHelp::append(out, editor.theme, editor.screenCols, helpTokens());
    const auto& result = content->result;
    const int rows = contentRows(editor);
    const int gutter =
        static_cast<int>(std::to_string(result.lines.size()).size()) + 2;
    if(result.success)
        editor.highlightPreviewLines(
            highlight, "assembly.s", result.lines,
            std::min(result.lines.size(), static_cast<size_t>(offset + rows)));
    const auto errors =
        wrappedDocs(result.diagnostics, std::max(1, editor.screenCols - 2));
    for(int row = 0; row < rows; ++row)
    {
        out += Terminal::NEWLINE_CLEAR;
        out += editor.theme.reset();
        const int index = offset + row;
        if(result.success && index < static_cast<int>(result.lines.size()))
        {
            if(index == cursor)
                out += editor.theme.selection();
            out += editor.theme.uiGutter();
            std::string number = std::to_string(index + 1);
            number.insert(
                0, std::max(0, gutter - 2 - static_cast<int>(number.size())),
                ' ');
            const std::string prefix =
                (index == cursor ? ">" : " ") + number + " ";
            out += clipped(prefix, editor.screenCols);
            appendSyntax(out, result.lines[index], highlight.tokens[index],
                         horizontal, std::max(0, editor.screenCols - gutter),
                         editor.theme);
        }
        else if(content->compiling && row == 0)
            out += editor.theme.uiInfo() +
                   clipped(" Compiling... q/Esc returns immediately.",
                           editor.screenCols);
        else if(!result.success && row < static_cast<int>(errors.size()))
            out += editor.theme.uiError() +
                   clipped(" " + errors[row], editor.screenCols);
        else
            out += editor.theme.uiGutter() + "~";
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
    status.index = result.lines.empty() ? 0 : cursor + 1;
    status.count = static_cast<int>(result.lines.size());
    editor.appendViewStatusBar(out, std::move(status));

    if(inspect && editor.screenRows >= 8 && editor.screenCols >= 20)
    {
        const int width = std::max(12, std::min(84, editor.screenCols - 4));
        const int height = std::max(4, std::min(15, editor.screenRows - 5));
        const int left = editor.screenCols - width;
        const int top = editor.screenRows - height - 1;
        const auto docs = wrappedDocs(
            content->docsLoading ? "Loading instruction documentation..."
                                 : content->docsText,
            width - 4);
        docsOffset =
            std::clamp(docsOffset, 0,
                       std::max(0, static_cast<int>(docs.size()) - height + 2));
        std::vector<std::string> coloredDocs;
        bool codeBlock = false;
        for(const auto& line : docs)
            coloredDocs.push_back(
                coloredDocumentation(line, editor.theme, codeBlock));
        for(int row = 0; row < height; ++row)
        {
            out += Terminal::cursorPos(top + row, left);
            out += editor.theme.panel();
            out += editor.theme.uiAccent();
            if(row == 0 || row == height - 1)
            {
                std::string title =
                    row == 0 ? " Inspect: " + content->docsTitle + " "
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
