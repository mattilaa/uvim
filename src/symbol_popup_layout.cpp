#include "symbol_popup_layout.h"
#include "editor.h"
#include "text_utils.h"
#include "widgets/popup_base.h"

#include <sstream>

namespace
{
std::string printable(std::string_view text)
{
    std::string result;
    for(unsigned char ch : text)
    {
        if(ch == '\t')
            result.append(4 - text_utils::utf8DisplayWidth(result) % 4, ' ');
        else if(ch < 32 || ch == 127)
            result += '?';
        else
            result += static_cast<char>(ch);
    }
    return result;
}

void wrapRow(SymbolPopupLayout& layout, SymbolPopupRow row)
{
    row.text = printable(row.text);
    if(row.heading && row.text.find_first_not_of('-') == std::string::npos)
        row.text.resize(
            std::min(row.text.size(), static_cast<size_t>(layout.innerWidth)));
    if(row.padding)
    {
        const auto last = row.text.find_last_not_of(' ');
        row.text.resize(last == std::string::npos ? 0 : last + 1);
    }
    if(row.text.empty())
    {
        layout.rows.push_back(std::move(row));
        return;
    }
    size_t position = 0;
    bool first = true;
    while(position < row.text.size())
    {
        const int indent = first ? 0
                           : row.table && row.memberStart > 0
                               ? std::min(
                                     static_cast<int>(row.memberStart),
                                     std::max(0, layout.innerWidth - 2))
                               : std::min(2, layout.innerWidth / 3);
        std::string prefix(static_cast<size_t>(indent), ' ');
        const size_t begin = position;
        int cells = indent;
        bool replacement = false;
        size_t lastSpace = std::string::npos;
        while(position < row.text.size())
        {
            const auto next = static_cast<size_t>(text_utils::nextUtf8CharStart(
                row.text,
                static_cast<int>(position)));
            const int glyphWidth = text_utils::utf8DisplayWidth(
                std::string_view(row.text).substr(position, next - position));
            if(cells + glyphWidth > layout.innerWidth)
            {
                if(position == begin)
                {
                    position = next;
                    replacement = true;
                }
                break;
            }
            if(row.text[position] == ' ')
                lastSpace = position;
            cells += glyphWidth;
            position = next;
        }
        const size_t minimumBreak =
            begin + (first && row.table ? row.memberStart : 0);
        if(!replacement && position < row.text.size() &&
           lastSpace != std::string::npos && lastSpace >= minimumBreak &&
           lastSpace > begin &&
           row.text.substr(begin, lastSpace - begin).find_first_not_of(' ') !=
               std::string::npos)
            position = lastSpace + 1;
        SymbolPopupRow part = row;
        part.text =
            prefix +
            (replacement ? "?" : row.text.substr(begin, position - begin));
        part.memberStart =
            row.table ? first ? row.memberStart : prefix.size() : 0;
        part.colon =
            row.table || row.heading ? std::string::npos : part.text.find(':');
        layout.rows.push_back(std::move(part));
        first = false;
    }
}
} // namespace

std::vector<std::string> wrapSymbolPopupLines(
    const std::vector<std::string>& lines,
    int width)
{
    SymbolPopupLayout layout;
    layout.innerWidth = std::max(1, width);
    for(const auto& text : lines)
    {
        SymbolPopupRow row;
        row.text = text;
        wrapRow(layout, std::move(row));
    }
    std::vector<std::string> result;
    for(auto& row : layout.rows)
        result.push_back(std::move(row.text));
    return result;
}

SymbolPopupLayout layoutSymbolPopup(const Editor& editor)
{
    SymbolPopupLayout layout;
    if(editor.screenCols < 5 || editor.screenRows < 3 ||
       editor.symbolPopupText.empty())
        return layout;
    const auto& symbolPopupText = editor.symbolPopupText;
    const auto& sizePopupEntries = editor.sizePopupEntries;
    const int sizePopupSelection = editor.sizePopupSelection;
    const bool inspector = !sizePopupEntries.empty();
    layout.width = std::min(editor.screenCols, 100);
    layout.height = std::min(editor.screenRows, 26);
    layout.innerWidth = layout.width - 4;
    using PopupRow = SymbolPopupRow;
    auto makeRow = [](std::string text)
    {
        PopupRow row;
        row.text = std::move(text);
        row.byteMap = row.text.starts_with("+0x");
        row.colon = row.text.find(':');
        const size_t firstNonSpace = row.text.find_first_not_of(' ');
        row.padding = firstNonSpace != std::string::npos &&
                      row.text.rfind("pad:", firstNonSpace) == firstNonSpace;
        return row;
    };

    std::vector<PopupRow> rows;
    size_t start = 0;
    while(start <= symbolPopupText.size())
    {
        size_t end = symbolPopupText.find('\n', start);
        if(end == std::string::npos)
        {
            rows.push_back(makeRow(symbolPopupText.substr(start)));
            break;
        }
        rows.push_back(makeRow(symbolPopupText.substr(start, end - start)));
        start = end + 1;
    }
    if(rows.empty())
        return layout;
    for(size_t i = 0; i < rows.size(); ++i)
        rows[i].sourceRow = static_cast<int>(i);

    const bool storageTable = sizePopupEntries.size() > 1;
    if(storageTable)
    {
        auto offsetText = [](const Editor::SizePopupEntry& entry)
        {
            if(!entry.offset)
                return std::string("?");
            std::ostringstream out;
            out << "+0x" << std::hex << *entry.offset;
            return out.str();
        };
        size_t offsetWidth = 6, sizeWidth = 5;
        int memberWidth = 6;
        for(size_t i = 1; i < sizePopupEntries.size(); ++i)
        {
            const auto& entry = sizePopupEntries[i];
            offsetWidth = std::max(offsetWidth, offsetText(entry).size());
            sizeWidth = std::max(
                sizeWidth,
                entry.size ? std::to_string(*entry.size).size() : size_t{1});
            if(!entry.padding)
                memberWidth = std::max(
                    memberWidth,
                    text_utils::utf8DisplayWidth(entry.name));
        }
        auto columns = [&](std::string offset, std::string size)
        {
            return "  " + offset +
                   std::string(offsetWidth - offset.size(), ' ') + "  " +
                   std::string(sizeWidth - size.size(), ' ') + size + "  ";
        };
        for(size_t i = 1; i < sizePopupEntries.size(); ++i)
        {
            const auto& entry = sizePopupEntries[i];
            if(entry.row < 0 || entry.row >= static_cast<int>(rows.size()))
                continue;
            auto& row = rows[entry.row];
            row.text = columns(
                offsetText(entry),
                entry.size ? std::to_string(*entry.size) : "?");
            row.memberStart = row.text.size();
            if(static_cast<int>(row.memberStart) + 4 > layout.innerWidth)
            {
                row.text =
                    entry.padding
                        ? "  " + offsetText(entry) + " " +
                              (entry.size ? std::to_string(*entry.size) : "?") +
                              " B"
                        : "  " + entry.name + " [" + offsetText(entry) + ", " +
                              (entry.size ? std::to_string(*entry.size) : "?") +
                              " B]";
                row.memberStart = 2;
            }
            else
                row.text +=
                    entry.padding ? std::string(memberWidth, ' ') : entry.name;
            row.padding = entry.padding;
            row.table = true;
            row.colon = std::string::npos;
        }
        PopupRow header, divider;
        header.text =
            layout.innerWidth <
                    static_cast<int>(columns("Offset", "Bytes").size()) + 4
                ? "Member / offset / bytes"
                : columns("Offset", "Bytes") + "Member";
        header.heading = divider.heading = true;
        divider.text =
            std::string(columns("Offset", "Bytes").size() + memberWidth, '-');
        rows.insert(rows.begin() + 1, std::move(divider));
        rows.insert(rows.begin() + 1, std::move(header));
    }

    if(sizePopupSelection >= 0 &&
       sizePopupSelection < static_cast<int>(sizePopupEntries.size()))
    {
        const int sourceRow = sizePopupEntries[sizePopupSelection].row;
        const int selectedRow =
            sourceRow + (storageTable && sourceRow > 0 ? 2 : 0);
        if(selectedRow >= 0 && selectedRow < static_cast<int>(rows.size()))
        {
            if(rows[selectedRow].table)
                rows[selectedRow].text.replace(0, 2, "> ");
            else
            {
                rows[selectedRow].text.insert(0, "> ");
                if(rows[selectedRow].colon != std::string::npos)
                    rows[selectedRow].colon += 2;
            }
        }
    }

    if(!inspector)
    {
        int naturalWidth = 1;
        for(const auto& row : rows)
            naturalWidth = std::max(
                naturalWidth,
                text_utils::utf8DisplayWidth(printable(row.text)));
        layout.width = std::min(editor.screenCols, naturalWidth + 4);
        layout.innerWidth = layout.width - 4;
    }
    for(auto& row : rows)
    {
        if(row.text.starts_with("Selected:"))
            layout.detailRow = static_cast<int>(layout.rows.size());
        wrapRow(layout, std::move(row));
    }
    if(!inspector)
        layout.height = std::min(
            editor.screenRows,
            static_cast<int>(layout.rows.size()) + 2);
    layout.visibleRows = layout.height - 2;
    if(inspector)
    {
        layout.top = (editor.screenRows - layout.height) / 2 + 1;
        layout.left = (editor.screenCols - layout.width) / 2 + 1;
    }
    else
    {
        const auto [cursorRow, cursorCol] =
            widgets::editorCursorScreenPosition(editor);
        layout.top =
            std::clamp(cursorRow + 1, 1, editor.screenRows - layout.height + 1);
        layout.left =
            std::clamp(cursorCol, 1, editor.screenCols - layout.width + 1);
    }
    return layout;
}
