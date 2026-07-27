#include "editor.h"
#include "executable_lookup.h"
#include "terminal.h"
#include "text_utils.h"

#include <algorithm>
#include <string_view>

void Editor::clearToolInfo()
{
    toolInfoLines.clear();
    toolInfoScrollOffset = 0;
}

void Editor::showToolInfo()
{
    toolInfoLines.clear();
    toolInfoLines.push_back("External Tools");
    toolInfoLines.push_back("");

    auto appendTool = [&](const std::string& label,
                          executable_lookup::Result result)
    {
        toolInfoLines.push_back(label + ": " +
                                (result.found ? "FOUND" : "MISSING"));
        toolInfoLines.push_back("  binary: " +
                                (result.found ? result.path : "not found"));
        if(!result.found)
            toolInfoLines.push_back("  status: install " + label +
                                    " and make sure it is on PATH");
    };

#ifdef UVIM_ENABLE_SEARCH_TOOLS
    appendTool("fzf", executable_lookup::find("fzf"));
    appendTool("rg/ripgrep", executable_lookup::findAny({"rg", "ripgrep"}));
#else
    toolInfoLines.push_back("fzf: not compiled");
    toolInfoLines.push_back("rg/ripgrep: not compiled");
#endif

    int visibleRows = std::max(0, screenRows - 3);
    int maxOffset = std::max(0, (int)toolInfoLines.size() - visibleRows);
    toolInfoScrollOffset = std::clamp(toolInfoScrollOffset, 0, maxOffset);
}

void Editor::scrollToolInfo(int delta)
{
    int visibleRows = std::max(0, screenRows - 3);
    int maxOffset = std::max(0, (int)toolInfoLines.size() - visibleRows);
    toolInfoScrollOffset =
        std::clamp(toolInfoScrollOffset + delta, 0, maxOffset);
    needsFullRedraw = true;
}

void Editor::drawToolInfo()
{
    std::string output;
    output.reserve(screenRows * screenCols * 2);

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::cursorPos(1, 1);
    output += theme.reset();

    output += theme.panel();
    std::string header = " Tool Info ";
    header += std::string(std::max(0, screenCols - (int)header.length()), ' ');
    output += header;
    output += theme.reset();

    int visibleRows = std::max(0, screenRows - 3);
    int maxOffset = std::max(0, (int)toolInfoLines.size() - visibleRows);
    toolInfoScrollOffset = std::clamp(toolInfoScrollOffset, 0, maxOffset);
    int row = 0;
    int idx = toolInfoScrollOffset;

    auto renderLine = [&](const std::string& line)
    {
        if(line.empty())
            return;

        auto renderKeyValue = [&](const std::string& key,
                                  std::string_view value,
                                  const std::string& keyColor)
        {
            output += keyColor;
            output += key;
            output += theme.reset();
            output += " ";
            output += std::string(value);
        };

        if(line == "External Tools")
        {
            output += theme.uiInfo();
            output += line;
            output += theme.reset();
        }
        else if(line.rfind("  binary:", 0) == 0)
        {
            output += "  ";
            renderKeyValue("binary:", std::string_view(line).substr(9),
                           theme.uiDim());
        }
        else if(line.rfind("  status:", 0) == 0)
        {
            output += "  ";
            std::string_view value = std::string_view(line).substr(9);
            output += theme.uiDim();
            output += "status:";
            output += theme.reset();
            output += " ";
            output += theme.uiError();
            output += std::string(value);
            output += theme.reset();
        }
        else if(text_utils::contains(line, ':') && line.rfind("  ", 0) != 0)
        {
            size_t colon = line.find(':');
            std::string_view label = std::string_view(line).substr(0, colon);
            std::string_view status =
                text_utils::is_not_found(colon)
                    ? std::string_view{}
                    : std::string_view(line).substr(colon + 1);

            output += theme.uiAccent();
            output += std::string(label);
            output += ":";
            output += theme.reset();

            std::string statusStr(status);
            if(!statusStr.empty() && statusStr[0] == ' ')
                statusStr.erase(0, 1);

            const std::string* color = &theme.uiWarning();
            if(statusStr == "FOUND")
                color = &theme.uiSuccess();
            else if(statusStr == "MISSING" ||
                    statusStr.rfind("not", 0) == 0)
                color = &theme.uiError();

            output += " ";
            output += *color;
            output += statusStr;
            output += theme.reset();
        }
        else
        {
            output += line;
        }
    };

    while(row < visibleRows && idx < (int)toolInfoLines.size())
    {
        output += Terminal::cursorPos(2 + row, 1);
        output += Terminal::ESC_CLEAR_LINE;
        renderLine(toolInfoLines[idx]);
        row++;
        idx++;
    }

    for(; row < visibleRows; row++)
    {
        output += Terminal::cursorPos(2 + row, 1);
        output += Terminal::ESC_CLEAR_LINE;
        output += "~";
    }

    output += Terminal::cursorPos(screenRows - 1, 1);
    output += theme.statusBar();
    std::string status = " <j/k> scroll  <q/Esc> close  <r> refresh ";
    if(maxOffset > 0)
        status += " " + std::to_string(toolInfoScrollOffset + 1) + "/" +
                  std::to_string(maxOffset + 1) + " ";
    if((int)status.length() < screenCols)
        status += std::string(screenCols - status.length(), ' ');
    output += status;
    output += theme.reset();

    output += Terminal::cursorPos(screenRows, 1);
    output += Terminal::ESC_CLEAR_LINE;
    if(!statusMessage.empty())
        output += statusMessage;

    output += Terminal::ESC_SHOW_CURSOR;
    Terminal::write(output);
    Terminal::flush();
}
