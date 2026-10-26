#include "assembly_instruction_help.h"
#include "ascii.h"
#include "asm_documentation.h"
#include "editor.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace assembly_instruction_help
{
namespace
{
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

void appendSyntax(
    std::string& out,
    const std::string& line,
    const std::vector<Token>& tokens,
    int left,
    int width,
    const Theme& theme,
    int dimLevel)
{
    std::vector<TokenType> colors(line.size(), TOKEN_NORMAL);
    for(const auto& token : tokens)
    {
        const int first =
            std::clamp(token.start, 0, static_cast<int>(line.size()));
        const int last = std::clamp(
            token.start + token.length,
            first,
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

std::string coloredDocumentation(
    std::string_view line,
    const Theme& theme,
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
        appendSyntax(
            out,
            std::string(line),
            assemblyTokens,
            0,
            text_utils::utf8DisplayWidth(line),
            theme,
            0);
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

TaskQueue::TaskId load(
    Editor& editor,
    const std::shared_ptr<State>& state,
    const std::string& instruction)
{
    state->docsLoading = state->docsIndicator = false;
#ifndef UVIM_ENABLE_ASM_DOCS
    (void)editor;
    (void)instruction;
    state->docsTitle = "Instruction help";
    state->docsText =
        "Instruction documentation is not compiled into this build.";
    return 0;
#else
    const auto help = asm_documentation::embedded(instruction);
    if(!help)
    {
        state->docsTitle = "Instruction help";
        state->docsText =
            "No instruction documentation for this row "
            "(labels, directives and comments have no instruction help).";
        return 0;
    }
    const auto start = instruction.find_first_not_of(" \t");
    const std::string example =
        "```asm\n" +
        instruction.substr(start == std::string::npos ? 0 : start) +
        "\n```\n\n";
    state->docsTitle = help->title;
    state->docsText = example + help->text;
    if(!editor.asmDocsApi)
        return 0;
    state->docsLoading = true;
    state->docsStarted = std::chrono::steady_clock::now();
    auto text = std::make_shared<std::string>();
    auto title = std::make_shared<std::string>();
    const auto generation = state->docsGeneration;
    std::weak_ptr<State> weak = state;
    return editor.tasks
        .submit(
            TaskQueue::Job{
                "assembly instruction help",
                "asm-docs",
                [line = std::string(instruction), example, text, title](
                    TaskQueue::Context& task)
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

void draw(
    std::string& out,
    Editor& editor,
    const State& state,
    int& docsOffset,
    std::string_view controls)
{
    if(editor.screenRows < 8 || editor.screenCols < 20)
        return;
    const int width = std::max(12, std::min(84, editor.screenCols - 4));
    const int height = std::max(4, std::min(15, editor.screenRows - 5));
    const int left = editor.screenCols - width;
    const int top = editor.screenRows - height - 1;
    const auto docs = wrappedDocs(state.docsText, width - 4);
    docsOffset = std::clamp(
        docsOffset,
        0,
        std::max(0, static_cast<int>(docs.size()) - height + 2));
    std::vector<std::string> coloredDocs;
    PreviewHighlight instructionHighlight;
    editor.highlightPreviewLines(
        instructionHighlight,
        "inspection.s",
        docs,
        docs.size());
    const std::vector<Token> noTokens;
    auto block = DocumentationBlock::None;
    for(size_t i = 0; i < docs.size(); ++i)
        coloredDocs.push_back(coloredDocumentation(
            docs[i],
            editor.theme,
            block,
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
            std::string title = row == 0
                                    ? " Inspect: " + state.docsTitle +
                                          (state.docsIndicator ? " ... " : " ")
                                    : " " + std::string(controls) + " ";
            title = clipped(title, width - 2);
            text_utils::appendU8(
                out,
                row == 0 ? ascii::BOX_ROUNDED_TOP_LEFT
                         : ascii::BOX_ROUNDED_BOTTOM_LEFT);
            out += Terminal::ESC_BOLD;
            out += title;
            out += editor.theme.reset() + editor.theme.panel() +
                   editor.theme.uiAccent();
            text_utils::appendUtf8Repeat(
                out,
                ascii::BOX_LIGHT_HORIZONTAL,
                std::max(0, width - 2 - text_utils::utf8DisplayWidth(title)));
            text_utils::appendU8(
                out,
                row == 0 ? ascii::BOX_ROUNDED_TOP_RIGHT
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
} // namespace assembly_instruction_help
