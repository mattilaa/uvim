#include "editor.h"
#include "terminal.h"
#include <algorithm>
#include <string_view>

static std::string filetypeLabel(const Editor& ed)
{
    if(ed.isCppFile())
        return "cpp";
    if(ed.isPythonFile())
        return "python";
    if(ed.isRobotFile())
        return "robot";
    if(ed.isJsonFile())
        return "json";
    if(ed.isYamlFile())
        return "yaml";
    return "text";
}

void Editor::clearLspInfo()
{
    lspInfoLines.clear();
}

void Editor::showLspInfo()
{
    lspInfoLines.clear();

    std::string name = filename && !filename->empty() ? *filename : "[No Name]";
    lspInfoLines.push_back("Buffer: " + name);
    lspInfoLines.push_back("Filetype: " + filetypeLabel(*this));
    lspInfoLines.push_back("");

#ifdef UVIM_ENABLE_CLANGD_LSP
    auto appendLsp = [&](const std::string& label, bool running,
                         bool activeForFile, const std::string& path)
    {
        std::string status =
            running ? (activeForFile ? "ACTIVE" : "ON") : "OFF";
        lspInfoLines.push_back(label + ": " + status);
        lspInfoLines.push_back("  binary: " + path);
    };

    appendLsp("clangd", isClangdLspEnabled(), isCppFile(), clangdLspPath);
    appendLsp("python", isPythonLspEnabled(), isPythonFile(), pythonLspPath);
    appendLsp("robot", isRobotLspEnabled(), isRobotFile(), robotLspPath);
#else
    lspInfoLines.push_back("clangd: not compiled");
    lspInfoLines.push_back("python: not compiled");
    lspInfoLines.push_back("robot: not compiled");
#endif
}

void Editor::drawLspInfo()
{
    std::string output;
    output.reserve(screenRows * screenCols * 2);

    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::cursorPos(1, 1);
    output += theme.reset();

    // Header
    output += theme.panel();
    std::string header = " LSP Info ";
    header += std::string(std::max(0, screenCols - (int)header.length()), ' ');
    output += header;
    output += theme.reset();
    output += "\r\n";

    int visibleRows = screenRows - 3;
    int row = 0;
    int idx = 0;

    auto renderLine = [&](const std::string& line)
    {
        output += Terminal::ESC_CLEAR_LINE;
        if(line.empty())
        {
            output += "\r\n";
            return;
        }

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

        if(line.rfind("Buffer:", 0) == 0)
        {
            renderKeyValue("Buffer:", std::string_view(line).substr(7),
                           theme.uiInfo());
        }
        else if(line.rfind("Filetype:", 0) == 0)
        {
            renderKeyValue("Filetype:", std::string_view(line).substr(9),
                           theme.uiInfo());
        }
        else if(line.rfind("  binary:", 0) == 0)
        {
            output += "  ";
            renderKeyValue("binary:", std::string_view(line).substr(9),
                           theme.uiDim());
        }
        else if(line.rfind("clangd:", 0) == 0 ||
                line.rfind("python:", 0) == 0 || line.rfind("robot:", 0) == 0)
        {
            size_t colon = line.find(':');
            std::string_view label = std::string_view(line).substr(0, colon);
            std::string_view status =
                colon == std::string::npos
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
            if(statusStr == "ACTIVE")
                color = &theme.uiSuccess();
            else if(statusStr == "ON")
                color = &theme.uiInfo();
            else if(statusStr == "OFF")
                color = &theme.uiWarning();
            else if(statusStr.rfind("not", 0) == 0 ||
                    statusStr.rfind("NOT", 0) == 0)
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

        // Pad to end of line
        int visLen = std::min((int)line.length(), screenCols);
        if(visLen < screenCols)
            output.append(screenCols - visLen, ' ');
        output += "\r\n";
    };

    while(row < visibleRows && idx < (int)lspInfoLines.size())
    {
        renderLine(lspInfoLines[idx]);
        row++;
        idx++;
    }

    for(; row < visibleRows; row++)
    {
        output += Terminal::ESC_CLEAR_LINE;
        output += "~\r\n";
    }

    // Status bar
    output += theme.statusBar();
    std::string status = " <q/Esc> close  <r> refresh ";
    if((int)status.length() < screenCols)
        status += std::string(screenCols - status.length(), ' ');
    output += status;
    output += theme.reset();

    // Message bar
    output += "\r\n";
    output += Terminal::ESC_CLEAR_LINE;
    if(!statusMessage.empty())
        output += statusMessage;

    output += Terminal::ESC_SHOW_CURSOR;
    Terminal::write(output);
    Terminal::flush();
}
