#include "widgets/completion_popup.h"

#include "editor.h"
#include "terminal.h"
#include "text_utils.h"
#include "theme.h"
#include "token_type.h"
#include <algorithm>
#include <cctype>

namespace widgets
{
void drawCompletionPopup(std::string& output, const Editor& editor)
{
    if(!editor.completionActive || editor.completionFiltered.empty())
        return;
    if(editor.currentMode != INSERT)
        return;

    const int maxRows = std::min({8, (int)editor.completionFiltered.size(),
                                  editor.screenRows - 2});
    if(maxRows <= 0)
        return;

    int cy = (*editor.cursorY - *editor.offsetY) + 1 + editor.tabBarRows();
    int cx = (*editor.cursorX - *editor.offsetX) + 1 + editor.gutterWidth();
    cy = std::clamp(cy, 1, editor.screenRows);
    cx = std::clamp(cx, 1, editor.screenCols);

    int maxW = 0;
    const int cap = std::min((int)editor.completionFiltered.size(), 500);
    for(int i = 0; i < cap; ++i)
    {
        const auto& e =
            editor.completionAll[editor.completionFiltered[i]];
        maxW = std::max(maxW, text_utils::displayWidth(e.label));
    }
    int innerW = std::max(12, maxW);
    int totalW = innerW + 4;
    if(totalW > editor.screenCols)
    {
        totalW = editor.screenCols;
        innerW = std::max(4, totalW - 4);
    }

    int totalH = maxRows + 2;
    int top = cy + 1;
    if(top + totalH - 1 > editor.screenRows)
        top = cy - totalH + 1;
    if(top < 1)
        top = 1;

    int left = cx;
    if(left + totalW - 1 > editor.screenCols)
        left = std::max(1, editor.screenCols - totalW + 1);

    auto moveTo = [&](int r, int c) { output += Terminal::cursorPos(r, c); };

    moveTo(top, left);
    text_utils::appendU8(output, u8"┌");
    text_utils::appendUtf8Repeat(output, u8"─", innerW + 2);
    text_utils::appendU8(output, u8"┐");

    auto kindColor = [&](int kind) -> const std::string&
    {
        switch(kind)
        {
        case 2:
        case 3:
        case 4:
        case 23:
            return editor.theme.syntax(TOKEN_FUNCTION);
        case 5:
        case 6:
        case 10:
        case 18:
        case 25:
            return editor.theme.uiInfo();
        case 7:
        case 8:
        case 13:
        case 22:
            return editor.theme.syntax(TOKEN_TYPE);
        case 14:
            return editor.theme.syntax(TOKEN_KEYWORD);
        case 11:
        case 12:
        case 20:
        case 21:
            return editor.theme.uiWarning();
        case 24:
            return editor.theme.syntax(TOKEN_OPERATOR);
        case 15:
        case 16:
            return editor.theme.uiSuccess();
        case 17:
        case 19:
            return editor.theme.uiDim();
        default:
            return editor.theme.baseFg();
        }
    };

    auto appendSyntaxRow = [&](const std::string& text, bool selected, int kind)
    {
        if(!editor.isFileType<FileType::Cpp>())
        {
            if(selected)
                output += editor.theme.selection();
            output += kindColor(kind);
            output += text;
            output += editor.theme.reset();
            return;
        }

        bool inBlockComment = false;
        bool inTomlMultiline = false;
        char tomlQuote = 0;
        bool inMarkupFence = false;
        char markupFenceChar = 0;
        std::vector<Token> tokens =
            editor.tokenizeLine(text, inBlockComment, inTomlMultiline, tomlQuote,
                                inMarkupFence, markupFenceChar);
        std::vector<TokenType> colors(text.size(), TOKEN_NORMAL);
        bool hasColor = false;

        for(const auto& token : tokens)
        {
            if(token.type != TOKEN_NORMAL)
                hasColor = true;
            int tokenEnd = token.start + token.length;
            for(int pos = token.start; pos < tokenEnd && pos < (int)text.size();
                pos++)
            {
                colors[pos] = token.type;
            }
        }

        if(selected)
            output += editor.theme.selection();

        if(!hasColor)
        {
            output += kindColor(kind);
            output += text;
            output += editor.theme.reset();
            return;
        }

        TokenType current = TOKEN_NORMAL;
        for(size_t i = 0; i < text.size(); ++i)
        {
            if(colors[i] != current)
            {
                current = colors[i];
                output += editor.getColorCode(current);
            }
            output += text[i];
        }

        output += editor.theme.reset();
    };

    for(int i = 0; i < maxRows; ++i)
    {
        int fidx = editor.completionScroll + i;
        if(fidx >= (int)editor.completionFiltered.size())
            break;
        const auto& e = editor.completionAll[editor.completionFiltered[fidx]];

        moveTo(top + 1 + i, left);
        text_utils::appendU8(output, u8"│");
        output += " ";

        bool sel = (fidx == editor.completionSelected);

        std::string row = e.label;
        while(text_utils::displayWidth(row) > innerW)
            row.pop_back();
        appendSyntaxRow(row, sel, e.kind);

        int pad = innerW - text_utils::displayWidth(row);
        if(pad > 0)
            output.append(pad, ' ');

        if(sel)
            output += editor.theme.reset();

        output += " ";
        text_utils::appendU8(output, u8"│");
    }

    moveTo(top + 1 + maxRows, left);
    text_utils::appendU8(output, u8"└");
    text_utils::appendUtf8Repeat(output, u8"─", innerW + 2);
    text_utils::appendU8(output, u8"┘");
}
} // namespace widgets
