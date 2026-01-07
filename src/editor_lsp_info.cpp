#include "editor.h"
#include "terminal.h"
#include <algorithm>

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

    while(row < visibleRows && idx < (int)lspInfoLines.size())
    {
        output += Terminal::ESC_CLEAR_LINE;
        std::string line = lspInfoLines[idx];
        if((int)line.length() > screenCols)
            line = line.substr(0, screenCols);
        output += line;
        if((int)line.length() < screenCols)
            output.append(screenCols - line.length(), ' ');
        output += "\r\n";
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
