#pragma once

#include "terminal.h"
#include "text_utils.h"
#include "theme.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

class HeaderHelp
{
public:
    static std::vector<std::string> wrap(const std::vector<std::string>& tokens,
                                         int width, int indent = 2)
    {
        std::vector<std::string> lines;
        const std::string prefix(std::max(0, indent), ' ');
        const int maxWidth = std::max(1, width);

        std::string line = prefix;
        int lineWidth = text_utils::utf8DisplayWidth(line);

        for(const auto& token : tokens)
        {
            const int tokenWidth = text_utils::utf8DisplayWidth(token);
            const int separatorWidth = lineWidth > indent ? 1 : 0;
            if(lineWidth > indent &&
               lineWidth + separatorWidth + tokenWidth > maxWidth)
            {
                lines.push_back(line);
                line = prefix + token;
                lineWidth = indent + tokenWidth;
                continue;
            }

            if(separatorWidth > 0)
            {
                line += ' ';
                lineWidth += separatorWidth;
            }
            line += token;
            lineWidth += tokenWidth;
        }

        if(lineWidth > indent || lines.empty())
            lines.push_back(line);
        return lines;
    }

    static int lineCount(const std::vector<std::string>& tokens, int width,
                         int indent = 2)
    {
        return static_cast<int>(wrap(tokens, width, indent).size());
    }

    static int append(std::string& output, const Theme& theme, int width,
                      const std::vector<std::string>& tokens, int indent = 2)
    {
        auto lines = wrap(tokens, width, indent);
        for(const auto& line : lines)
        {
            output += Terminal::NEWLINE_CLEAR;
            output += theme.uiDim();
            output += line;
            output += theme.baseFg();
        }
        return static_cast<int>(lines.size());
    }
};
