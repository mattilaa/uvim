#include "widgets/completion_popup.h"
#include "ascii.h"

#include "editor.h"
#include "terminal.h"
#include "text_utils.h"
#include "theme.h"
#include "token_type.h"
#include <algorithm>
#include <cctype>

namespace widgets
{
static std::string buildCompletionExtras(const CompletionEntry& entry)
{
    std::string extra;
    if(!entry.labelDetail.empty())
    {
        bool needsSpace = entry.labelDetail[0] != '(' &&
                          !text_utils::is_space(entry.labelDetail[0]);
        if(needsSpace)
            extra.push_back(' ');
        extra += entry.labelDetail;
    }
    if(!entry.labelDescription.empty())
    {
        extra.push_back(' ');
        extra += entry.labelDescription;
    }
    return extra;
}

static std::string truncateToWidth(std::string text, int width)
{
    while(text_utils::displayWidth(text) > width)
        text.pop_back();
    return text;
}

static std::string firstLine(std::string text)
{
    auto pos = text.find('\n');
    if(text_utils::is_found(pos))
        text.resize(pos);
    return text;
}

static std::string trimAndCollapse(std::string text)
{
    std::string out;
    out.reserve(text.size());

    bool sawSpace = true;
    for(char c : text)
    {
        if(std::isspace((unsigned char)c))
        {
            if(!sawSpace)
            {
                out.push_back(' ');
                sawSpace = true;
            }
            continue;
        }
        out.push_back(c);
        sawSpace = false;
    }

    while(!out.empty() && out.back() == ' ')
        out.pop_back();
    if(!out.empty() && out.front() == ' ')
        out.erase(out.begin());

    return out;
}

static std::string buildCompletionBrief(const CompletionEntry& entry)
{
    if(!entry.labelDescription.empty())
        return trimAndCollapse(entry.labelDescription);
    if(!entry.labelDetail.empty())
        return trimAndCollapse(entry.labelDetail);
    if(!entry.detail.empty())
        return trimAndCollapse(entry.detail);
    if(!entry.documentation.empty())
        return trimAndCollapse(firstLine(entry.documentation));
    return {};
}

static std::vector<std::string> wrapTextLines(const std::string& text,
                                              int width, int maxLines)
{
    std::vector<std::string> out;
    if(text.empty() || width <= 4 || maxLines <= 0)
        return out;

    std::string line;
    line.reserve(text.size());
    int lineW = 0;
    int start = 0;

    auto flushLine = [&]()
    {
        if(!line.empty())
        {
            out.push_back(trimAndCollapse(line));
            line.clear();
            lineW = 0;
        }
    };

    for(size_t i = 0; i <= text.size(); ++i)
    {
        bool atEnd = (i == text.size());
        char c = atEnd ? ' ' : text[i];
        bool sep = atEnd || c == '\n' || std::isspace((unsigned char)c);
        if(!sep)
            continue;

        if((int)i > start)
        {
            std::string word = text.substr(start, i - (size_t)start);
            int ww = text_utils::displayWidth(word);
            if(line.empty())
            {
                if(ww > width)
                    word = truncateToWidth(word, width);
                line = word;
                lineW = text_utils::displayWidth(line);
            }
            else
            {
                if(lineW + 1 + ww > width)
                {
                    flushLine();
                    if((int)out.size() >= maxLines)
                        break;
                    if(ww > width)
                        word = truncateToWidth(word, width);
                    line = word;
                    lineW = text_utils::displayWidth(line);
                }
                else
                {
                    line += " ";
                    line += word;
                    lineW += 1 + ww;
                }
            }
        }

        if(c == '\n')
        {
            flushLine();
            if((int)out.size() >= maxLines)
                break;
        }

        start = (int)i + 1;
    }

    if((int)out.size() < maxLines && !line.empty())
        flushLine();

    if((int)out.size() > maxLines)
        out.resize((size_t)maxLines);
    return out;
}

std::string buildCompletionRowForTest(const CompletionEntry& entry, int width)
{
    std::string label = entry.label;
    std::string extra = buildCompletionExtras(entry);
    std::string left = label + extra;
    std::string right = buildCompletionBrief(entry);

    if(right.empty() || width < 20)
    {
        if(text_utils::displayWidth(left) > width)
            left = truncateToWidth(left, width);
        return left;
    }

    int rightW = std::min(24, std::max(8, width / 3));
    int leftW = width - rightW - 3;
    if(leftW < 6)
    {
        if(text_utils::displayWidth(left) > width)
            left = truncateToWidth(left, width);
        return left;
    }

    if(text_utils::displayWidth(left) > leftW)
        left = truncateToWidth(left, leftW);
    if(text_utils::displayWidth(right) > rightW)
        right = truncateToWidth(right, rightW);

    std::string row = left;
    int pad = leftW - text_utils::displayWidth(left);
    if(pad > 0)
        row.append(pad, ' ');
    row += " | ";
    row += right;
    return row;
}

void drawCompletionPopup(std::string& output, const Editor& editor)
{
    if(!editor.completionActive || editor.completionFiltered.empty())
        return;
    if(editor.currentMode != INSERT)
        return;

    const int maxRows = std::min(
        {8, (int)editor.completionFiltered.size(), editor.screenRows - 2});
    if(maxRows <= 0)
        return;

    int cy = (*editor.cursorY - *editor.offsetY) + 1 + editor.tabBarRows();
    int cx = (*editor.cursorX - *editor.offsetX) + 1 + editor.gutterWidth();
    cy = std::clamp(cy, 1, editor.screenRows);
    cx = std::clamp(cx, 1, editor.screenCols);

    int maxLeftW = 0;
    int maxBriefW = 0;
    const int cap = std::min((int)editor.completionFiltered.size(), 500);
    for(int i = 0; i < cap; ++i)
    {
        const auto& e = editor.completionAll[editor.completionFiltered[i]];
        maxLeftW = std::max(
            maxLeftW, text_utils::displayWidth(e.label) +
                          text_utils::displayWidth(buildCompletionExtras(e)));

        std::string brief = buildCompletionBrief(e);
        if(!brief.empty())
            maxBriefW = std::max(maxBriefW, text_utils::displayWidth(brief));
    }

    int briefColW = 0;
    if(maxBriefW > 0)
        briefColW = std::clamp(maxBriefW, 12, 36);

    int innerW = std::max(12, maxLeftW + (briefColW > 0 ? (briefColW + 3) : 0));
    int preferredInnerW = std::clamp((editor.screenCols * 2) / 3, 44, 92);
    innerW = std::max(innerW, preferredInnerW);
    int totalW = innerW + 4;
    if(totalW > editor.screenCols)
    {
        totalW = editor.screenCols;
        innerW = std::max(4, totalW - 4);
    }

    int leftColW = innerW;
    if(briefColW > 0)
    {
        if(briefColW > innerW / 2)
            briefColW = innerW / 2;
        leftColW = innerW - briefColW - 3;
        if(leftColW < 8)
        {
            briefColW = 0;
            leftColW = innerW;
        }
    }

    int docRows = 0;
    std::vector<std::string> docLines;
    {
        int sel =
            editor.completionSelected < (int)editor.completionFiltered.size()
                ? editor.completionSelected
                : 0;
        if(!editor.completionFiltered.empty())
        {
            const auto& selEntry =
                editor.completionAll[editor.completionFiltered[sel]];
            std::string docText;
            if(!selEntry.documentation.empty())
            {
                docText = selEntry.documentation;
            }
            else if(!selEntry.detail.empty())
            {
                docText = selEntry.detail;
            }
            int maxDocRows = std::clamp(editor.screenRows / 4, 1, 6);
            docLines = wrapTextLines(docText, innerW, maxDocRows);
            docRows = (int)docLines.size();
        }
    }

    int totalH = maxRows + 2 + docRows;
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
    text_utils::appendU8(output, ascii::BOX_LIGHT_TOP_LEFT);
    text_utils::appendUtf8Repeat(output, ascii::BOX_LIGHT_HORIZONTAL,
                                 innerW + 2);
    text_utils::appendU8(output, ascii::BOX_LIGHT_TOP_RIGHT);

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

    auto appendSyntaxRow = [&](const std::string& label,
                               const std::string& extra, bool selected,
                               int kind)
    {
        if(!editor.isFileType<FileType::Cpp>())
        {
            if(selected)
                output += editor.theme.selection();
            output += kindColor(kind);
            output += label;
            if(!extra.empty())
            {
                output += editor.theme.uiDim();
                output += extra;
            }
            output += editor.theme.reset();
            return;
        }

        bool inBlockComment = false;
        bool inTomlMultiline = false;
        char tomlQuote = 0;
        bool inMarkupFence = false;
        char markupFenceChar = 0;
        std::vector<Token> tokens =
            editor.tokenizeLine(label, inBlockComment, inTomlMultiline,
                                tomlQuote, inMarkupFence, markupFenceChar);
        std::vector<TokenType> colors(label.size(), TOKEN_NORMAL);
        bool hasColor = false;

        for(const auto& token : tokens)
        {
            if(token.type != TOKEN_NORMAL)
                hasColor = true;
            int tokenEnd = token.start + token.length;
            for(int pos = token.start;
                pos < tokenEnd && pos < (int)label.size(); pos++)
            {
                colors[pos] = token.type;
            }
        }

        if(selected)
            output += editor.theme.selection();

        if(!hasColor)
        {
            output += kindColor(kind);
            output += label;
            if(!extra.empty())
            {
                output += editor.theme.uiDim();
                output += extra;
            }
            output += editor.theme.reset();
            return;
        }

        TokenType current = TOKEN_NORMAL;
        for(size_t i = 0; i < label.size(); ++i)
        {
            if(colors[i] != current)
            {
                current = colors[i];
                output += editor.getColorCode(current);
            }
            output += label[i];
        }

        if(!extra.empty())
        {
            output += editor.theme.uiDim();
            output += extra;
        }

        output += editor.theme.reset();
    };

    for(int dr = 0; dr < docRows; ++dr)
    {
        moveTo(top + 1 + dr, left);
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
        output += " ";
        std::string doc = docLines[(size_t)dr];
        if(text_utils::displayWidth(doc) > innerW)
            doc = truncateToWidth(doc, innerW);
        output += editor.theme.uiInfo();
        output += doc;
        output += editor.theme.reset();
        int pad = innerW - text_utils::displayWidth(doc);
        if(pad > 0)
            output.append(pad, ' ');
        output += " ";
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
    }

    for(int i = 0; i < maxRows; ++i)
    {
        int fidx = editor.completionScroll + i;
        if(fidx >= (int)editor.completionFiltered.size())
            break;
        const auto& e = editor.completionAll[editor.completionFiltered[fidx]];

        moveTo(top + 1 + docRows + i, left);
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
        output += " ";

        bool sel = (fidx == editor.completionSelected);

        std::string label = e.label;
        std::string extra = buildCompletionExtras(e);
        std::string leftText = label;
        leftText += extra;
        if(text_utils::displayWidth(leftText) > leftColW)
        {
            leftText = truncateToWidth(leftText, leftColW);
            label = leftText;
            extra.clear();
        }

        appendSyntaxRow(label, extra, sel, e.kind);

        int leftUsed =
            text_utils::displayWidth(label) + text_utils::displayWidth(extra);
        int leftPad = leftColW - leftUsed;
        if(leftPad > 0)
            output.append(leftPad, ' ');

        if(briefColW > 0)
        {
            output += editor.theme.uiDim();
            output += ascii::utf8(ascii::BOX_LIGHT_VERTICAL_PADDED);

            std::string brief = buildCompletionBrief(e);
            if(text_utils::displayWidth(brief) > briefColW)
                brief = truncateToWidth(brief, briefColW);

            output += editor.theme.uiInfo();
            output += brief;
            output += editor.theme.reset();

            int pad = briefColW - text_utils::displayWidth(brief);
            if(pad > 0)
                output.append(pad, ' ');
        }

        if(sel)
            output += editor.theme.reset();

        output += " ";
        text_utils::appendU8(output, ascii::BOX_LIGHT_VERTICAL);
    }

    moveTo(top + 1 + docRows + maxRows, left);
    text_utils::appendU8(output, ascii::BOX_LIGHT_BOTTOM_LEFT);
    text_utils::appendUtf8Repeat(output, ascii::BOX_LIGHT_HORIZONTAL,
                                 innerW + 2);
    text_utils::appendU8(output, ascii::BOX_LIGHT_BOTTOM_RIGHT);
}
} // namespace widgets
