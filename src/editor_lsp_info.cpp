#include "editor.h"
#include "terminal.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace fs = std::filesystem;

static bool binaryExists(const std::string& pathOrExe)
{
    if(pathOrExe.empty())
        return false;

    if(pathOrExe.find('/') != std::string::npos)
    {
        std::error_code ec;
        return fs::exists(pathOrExe, ec) && fs::is_regular_file(pathOrExe, ec);
    }

    const char* envPath = std::getenv("PATH");
    if(!envPath || !*envPath)
        return false;

    std::string_view pathView{envPath};
    size_t start = 0;
    while(start < pathView.size())
    {
        size_t end = pathView.find(':', start);
        if(end == std::string_view::npos)
            end = pathView.size();
        if(end > start)
        {
            fs::path candidate =
                fs::path(std::string(pathView.substr(start, end - start))) /
                pathOrExe;
            std::error_code ec;
            if(fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
                return true;
        }
        start = end + 1;
    }
    return false;
}

static std::string filetypeLabel(const Editor& ed)
{
    if(ed.isFileType<FileType::Cpp>())
        return "cpp";
    if(ed.isFileType<FileType::Python>())
        return "python";
    if(ed.isFileType<FileType::Mla>())
        return "mlang";
    if(ed.isFileType<FileType::Robot>())
        return "robot";
    if(ed.isFileType<FileType::JavaScript>())
        return "javascript";
    if(ed.isFileType<FileType::TypeScript>())
        return "typescript";
    if(ed.isFileType<FileType::Html>())
        return "html";
    if(ed.isFileType<FileType::Css>())
        return "css";
    if(ed.isFileType<FileType::Json>())
        return "json";
    if(ed.isFileType<FileType::Yaml>())
        return "yaml";
    if(ed.isFileType<FileType::Toml>())
        return "toml";
    if(ed.isFileType<FileType::Xml>())
        return "xml";
    if(ed.isFileType<FileType::MarkupText>())
        return "text";
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
                         bool activeForFile, const std::string& path,
                         bool requiresNode = false,
                         const std::string& version = std::string())
    {
        std::string status =
            running ? (activeForFile ? "ACTIVE" : "ON") : "OFF";
        bool hasBinary = binaryExists(path);
        bool hasNode = !requiresNode || binaryExists("node");

        lspInfoLines.push_back(label + ": " + status);
        lspInfoLines.push_back("  binary: " + path);
        if(requiresNode)
            lspInfoLines.push_back(std::string("  runtime: node ") +
                                   (hasNode ? "found" : "not found"));
        if(!hasBinary)
            lspInfoLines.push_back("  status: binary not found");
        if(requiresNode && !hasNode)
            lspInfoLines.push_back("  status: node runtime not found");
        if(!version.empty())
            lspInfoLines.push_back("  version: " + version);
    };

    appendLsp("clangd", isClangdLspEnabled(), isFileType<FileType::Cpp>(),
              clangdLspPath);
    appendLsp("python", isPythonLspEnabled(), isFileType<FileType::Python>(),
              pythonLspPath);
    appendLsp("robot", isRobotLspEnabled(), isFileType<FileType::Robot>(),
              robotLspPath);
    std::string mlangLabel = "mlang";
    std::string mlangVersion;
    if(mlangLspClient)
    {
        std::string serverName = mlangLspClient->serverName();
        mlangVersion = mlangLspClient->serverVersion();
        if(serverName.find("mlangd_mla") != std::string::npos ||
           mlangLspPath.find("mlangd_mla") != std::string::npos)
            mlangLabel = "mlang (MLA)";
    }
    appendLsp(mlangLabel, isMlangLspEnabled(), isFileType<FileType::Mla>(),
              mlangLspPath, false, mlangVersion);
    appendLsp("html", isHtmlLspEnabled(), isFileType<FileType::Html>(),
              htmlLspPath, true);
    appendLsp("css", isCssLspEnabled(), isFileType<FileType::Css>(),
              cssLspPath, true);
    appendLsp("json", isJsonLspEnabled(), isFileType<FileType::Json>(),
              jsonLspPath, true);
    appendLsp("ts", isTsLspEnabled(),
              (isFileType<FileType::JavaScript>() ||
               isFileType<FileType::TypeScript>()),
              tsLspPath, true);
#else
    lspInfoLines.push_back("clangd: not compiled");
    lspInfoLines.push_back("python: not compiled");
    lspInfoLines.push_back("robot: not compiled");
    lspInfoLines.push_back("mlang: not compiled");
    lspInfoLines.push_back("html: not compiled");
    lspInfoLines.push_back("css: not compiled");
    lspInfoLines.push_back("json: not compiled");
    lspInfoLines.push_back("ts: not compiled");
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

    int visibleRows = screenRows - 3;
    int row = 0;
    int idx = 0;

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
        else if(line.rfind("  version:", 0) == 0)
        {
            output += "  ";
            renderKeyValue("version:", std::string_view(line).substr(10),
                           theme.uiDim());
        }
        else if(line.rfind("  runtime:", 0) == 0)
        {
            output += "  ";
            std::string_view value = std::string_view(line).substr(10);
            output += theme.uiDim();
            output += "runtime:";
            output += theme.reset();
            output += " ";
            if(value.find("not found") != std::string_view::npos)
                output += theme.uiError();
            else
                output += theme.uiSuccess();
            output += std::string(value);
            output += theme.reset();
        }
        else if(line.rfind("  status:", 0) == 0)
        {
            output += "  ";
            std::string_view value = std::string_view(line).substr(9);
            output += theme.uiDim();
            output += "status:";
            output += theme.reset();
            output += " ";
            if(value.find("not found") != std::string_view::npos)
                output += theme.uiError();
            else
                output += theme.uiWarning();
            output += std::string(value);
            output += theme.reset();
        }
        else if(line.find(':') != std::string::npos && line.rfind("  ", 0) != 0)
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
    };

    while(row < visibleRows && idx < (int)lspInfoLines.size())
    {
        output += Terminal::cursorPos(2 + row, 1);
        output += Terminal::ESC_CLEAR_LINE;
        renderLine(lspInfoLines[idx]);
        row++;
        idx++;
    }

    for(; row < visibleRows; row++)
    {
        output += Terminal::cursorPos(2 + row, 1);
        output += Terminal::ESC_CLEAR_LINE;
        output += "~";
    }

    // Status bar
    output += Terminal::cursorPos(screenRows - 1, 1);
    output += theme.statusBar();
    std::string status = " <q/Esc> close  <r> refresh ";
    if((int)status.length() < screenCols)
        status += std::string(screenCols - status.length(), ' ');
    output += status;
    output += theme.reset();

    // Message bar
    output += Terminal::cursorPos(screenRows, 1);
    output += Terminal::ESC_CLEAR_LINE;
    if(!statusMessage.empty())
        output += statusMessage;

    output += Terminal::ESC_SHOW_CURSOR;
    Terminal::write(output);
    Terminal::flush();
}
