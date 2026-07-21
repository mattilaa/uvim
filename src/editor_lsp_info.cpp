#include "editor.h"
#include "enablelog.h"
#include "terminal.h"
#include "text_utils.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace fs = std::filesystem;

namespace
{
mla::log::FileLogger CLANGD_LOG("CLANGD");
}

static bool binaryExists(const std::string& pathOrExe)
{
    std::string path = pathOrExe;
    while(path.size() >= 2)
    {
        const char first = path.front();
        const char last = path.back();
        if((first == '"' && last == '"') || (first == '\'' && last == '\''))
            path = path.substr(1, path.size() - 2);
        else
            break;
    }

    if(path.empty())
        return false;

    if(text_utils::contains(path, '/') || text_utils::contains(path, '\\'))
    {
        std::error_code ec;
        return fs::exists(path, ec) && fs::is_regular_file(path, ec);
    }

    const char* envPath = std::getenv("PATH");
    if(!envPath || !*envPath)
        return false;

    std::string_view pathView{envPath};
    size_t start = 0;
    while(start < pathView.size())
    {
#ifdef _WIN32
        size_t end = pathView.find(';', start);
#else
        size_t end = pathView.find(':', start);
#endif
        if(text_utils::is_not_found(end))
            end = pathView.size();
        if(end > start)
        {
            fs::path candidate =
                fs::path(std::string(pathView.substr(start, end - start))) /
                path;
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

static std::string lspLogDetailSuffix()
{
#if defined(UVIM_DEBUG_LOGGING) || defined(UVIM_DEBUG_LSP)
    return " (details logged to " + mla::log::getLogFilePath() + ")";
#else
    return {};
#endif
}

void Editor::clearLspInfo()
{
    lspInfoLines.clear();
    lspInfoScrollOffset = 0;
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
                         const std::string& version = std::string(),
                         const std::string& statusDetail = std::string())
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
        if(!statusDetail.empty())
            lspInfoLines.push_back("  status: " + statusDetail);
    };

    const bool clangdRunning = isClangdLspEnabled();
    std::string clangdError = clangdLspLastError;
    if(clangdError.empty() && clangdLspStartupAttempted && !clangdRunning)
    {
        if(lspClient)
            clangdError = lspClient->lastError();
        if(clangdError.empty())
            clangdError = "server is not running after startup request";
    }
    if(!clangdError.empty())
    {
        LOG_ERROR(CLANGD_LOG, "clangd LSP status detail: {}", clangdError);
    }
    const std::string clangdStatusDetail =
        clangdError.empty()
            ? std::string{}
            : "startup failed: " + clangdError + lspLogDetailSuffix();
    appendLsp("clangd", clangdRunning, isFileType<FileType::Cpp>(),
              clangdLspPath, false, std::string(), clangdStatusDetail);
    if(lspClient)
    {
        int workers = lspClient->workerCount();
        if(workers > 0)
            lspInfoLines.push_back("  workers: " + std::to_string(workers));
        if(clangdRunning)
        {
            std::string indexing =
                lspClient->indexingInProgress() ? "active" : "idle";
            std::string detail = lspClient->indexingStatus();
            if(!detail.empty())
                indexing += " (" + detail + ")";
            lspInfoLines.push_back("  indexing: " + indexing);
        }
    }
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
        if(text_utils::contains(serverName, "mlangd-mla") ||
           text_utils::contains(mlangLspPath, "mlangd-mla"))
            mlangLabel = "mlang (MLA)";
    }
    appendLsp(mlangLabel, isMlangLspEnabled(), isFileType<FileType::Mla>(),
              mlangLspPath, false, mlangVersion);
    appendLsp("html", isHtmlLspEnabled(), isFileType<FileType::Html>(),
              htmlLspPath, true);
    appendLsp("css", isCssLspEnabled(), isFileType<FileType::Css>(), cssLspPath,
              true);
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

    int visibleRows = std::max(0, screenRows - 3);
    int maxOffset = std::max(0, (int)lspInfoLines.size() - visibleRows);
    lspInfoScrollOffset = std::clamp(lspInfoScrollOffset, 0, maxOffset);
}

void Editor::scrollLspInfo(int delta)
{
    int visibleRows = std::max(0, screenRows - 3);
    int maxOffset = std::max(0, (int)lspInfoLines.size() - visibleRows);
    lspInfoScrollOffset =
        std::clamp(lspInfoScrollOffset + delta, 0, maxOffset);
    needsFullRedraw = true;
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

    int visibleRows = std::max(0, screenRows - 3);
    int maxOffset = std::max(0, (int)lspInfoLines.size() - visibleRows);
    lspInfoScrollOffset = std::clamp(lspInfoScrollOffset, 0, maxOffset);
    int row = 0;
    int idx = lspInfoScrollOffset;

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
            if(text_utils::contains(value, "not found"))
                output += theme.uiError();
            else
                output += theme.uiSuccess();
            output += std::string(value);
            output += theme.reset();
        }
        else if(line.rfind("  workers:", 0) == 0)
        {
            output += "  ";
            renderKeyValue("workers:", std::string_view(line).substr(10),
                           theme.uiDim());
        }
        else if(line.rfind("  indexing:", 0) == 0)
        {
            output += "  ";
            std::string_view value = std::string_view(line).substr(11);
            output += theme.uiDim();
            output += "indexing:";
            output += theme.reset();
            output += " ";
            output += text_utils::contains(value, "active")
                          ? theme.uiInfo()
                          : theme.uiDim();
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
            if(text_utils::contains(value, "not found"))
                output += theme.uiError();
            else
                output += theme.uiWarning();
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
    std::string status = " <j/k> scroll  <q/Esc> close  <r> refresh ";
    if(maxOffset > 0)
        status += " " + std::to_string(lspInfoScrollOffset + 1) + "/" +
                  std::to_string(maxOffset + 1) + " ";
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
