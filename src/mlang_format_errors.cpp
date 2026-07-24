#include "mlang_format_errors.h"
#include "editor.h"
#include "editor_path_utilities.h"
#include "lsp_client.h"
#include "text_utils.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;

namespace
{
std::string shellQuote(const std::string& text)
{
    std::string out = "'";
    for(char ch : text)
    {
        if(ch == '\'')
            out += "'\\''";
        else
            out.push_back(ch);
    }
    out.push_back('\'');
    return out;
}

std::string resolveExecutablePath(const std::string& exe)
{
    if(exe.empty())
        return {};
    fs::path exePath(exe);
    if(exePath.has_parent_path())
    {
        std::error_code ec;
        if(fs::exists(exePath, ec) && fs::is_regular_file(exePath, ec))
            return exePath.string();
        return {};
    }

    const char* path = std::getenv("PATH");
    if(!path || !*path)
        return {};

    std::string_view pathView{path};
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
                exe;
            std::error_code ec;
            if(fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
                return candidate.string();
        }
        start = end + 1;
    }
    return {};
}

fs::path makeTempPath(const std::string& stem, const std::string& extension)
{
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec);
    if(ec || dir.empty())
        dir = fs::current_path(ec);

    const auto now =
        std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream name;
    name << stem << '_' << now << '_' << reinterpret_cast<std::uintptr_t>(&dir)
         << extension;
    return dir / name.str();
}

std::string relativeDisplayPath(const fs::path& path, const fs::path& root)
{
    std::error_code ec;
    fs::path rel = fs::relative(path, root, ec);
    if(!ec && !rel.empty())
        return rel.string();
    return path.string();
}

bool shouldSkipDirectory(const fs::path& path)
{
    const std::string name = path.filename().string();
    return name == ".git" || name == "build" || name == ".cache" ||
           name == "cmake-build-debug" || name == "cmake-build-release";
}

std::vector<fs::path> collectMlaFiles(const fs::path& root)
{
    std::vector<fs::path> files;
    std::error_code ec;
    if(!fs::exists(root, ec))
        return files;

    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    for(; !ec && it != end; it.increment(ec))
    {
        const fs::path path = it->path();
        if(it->is_directory(ec))
        {
            if(shouldSkipDirectory(path))
                it.disable_recursion_pending();
            continue;
        }
        if(!it->is_regular_file(ec))
            continue;
        if(path.extension() == ".mla" || path.extension() == ".mlang")
            files.push_back(path);
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::pair<int, int> parseLocation(std::string_view line,
                                  std::string* rangeText)
{
    static const std::regex rangeRe(
        R"(\[([0-9]+)(?::([0-9]+))?\s*,\s*([0-9]+)(?::([0-9]+))?\))");
    std::cmatch match;
    std::string text(line);
    if(std::regex_search(text.c_str(), match, rangeRe))
    {
        if(rangeText)
            *rangeText = match.str(0);
        int parsedLine = std::max(1, std::stoi(match.str(1))) - 1;
        int parsedCol = 0;
        if(match[2].matched)
            parsedCol = std::max(1, std::stoi(match.str(2))) - 1;
        return {parsedLine, parsedCol};
    }

    static const std::regex fileLineRe(R"(:([0-9]+):([0-9]+):)");
    if(std::regex_search(text.c_str(), match, fileLineRe))
    {
        int parsedLine = std::max(1, std::stoi(match.str(1))) - 1;
        int parsedCol = std::max(1, std::stoi(match.str(2))) - 1;
        return {parsedLine, parsedCol};
    }

    return {0, 0};
}

std::vector<std::string> readLines(const fs::path& path)
{
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string line;
    while(std::getline(in, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

void copyFileOrBuffer(const fs::path& source, const fs::path& target,
                      const Editor& editor)
{
    if(editor.currentBuffer && !editor.currentBuffer->filename.empty())
    {
        std::error_code ec;
        fs::path current =
            fs::canonical(EditorPathUtilities::resolveEditorPath(
                              editor.currentBuffer->filename),
                          ec);
        ec.clear();
        fs::path candidate = fs::canonical(source, ec);
        if(!ec && current == candidate && editor.lines)
        {
            std::ofstream out(target);
            for(const auto& line : *editor.lines)
                out << line << '\n';
            return;
        }
    }

    std::error_code ec;
    fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
}

} // namespace

std::vector<MlangFormatErrorEntry> collectMlangFormatErrors(Editor& editor)
{
    std::vector<MlangFormatErrorEntry> results;

    std::string fmtExe = resolveExecutablePath("mlang-format");
    if(fmtExe.empty())
    {
        std::error_code ec;
        const std::string hb = "/opt/homebrew/bin/mlang-format";
        if(fs::exists(hb, ec) && fs::is_regular_file(hb, ec))
            fmtExe = hb;
    }
    if(fmtExe.empty())
    {
        editor.setStatusMessage("mlang-format not found");
        return results;
    }

    fs::path root;
    if(!editor.projectRoot.empty())
        root = editor.projectRoot;
    else if(editor.currentBuffer && !editor.currentBuffer->filename.empty())
        root = fs::path(editor.currentBuffer->filename).parent_path();
    else
    {
        std::error_code ec;
        root = fs::current_path(ec);
    }
    root = EditorPathUtilities::resolveEditorPath(root);

    const std::vector<fs::path> files = collectMlaFiles(root);
    for(const fs::path& file : files)
    {
        fs::path tempPath = makeTempPath("uvim_mlang_check", ".mla");
        fs::path errPath = makeTempPath("uvim_mlang_check_err", ".log");
        copyFileOrBuffer(file, tempPath, editor);

        std::string cmd = shellQuote(fmtExe) +
                          " -i --style file --assume-filename=" +
                          shellQuote(file.string()) + " " +
                          shellQuote(tempPath.string()) + " 2>" +
                          shellQuote(errPath.string());
        int status = std::system(cmd.c_str());
        if(status != 0)
        {
            std::vector<std::string> errLines = readLines(errPath);
            if(errLines.empty())
                errLines.push_back("mlang-format failed with exit status " +
                                   std::to_string(status));
            for(const std::string& raw : errLines)
            {
                std::string line = raw;
                if(line.empty())
                    continue;
                MlangFormatErrorEntry entry;
                entry.path = file.string();
                entry.displayPath = relativeDisplayPath(file, root);
                entry.message = line;
                std::tie(entry.line, entry.col) =
                    parseLocation(line, &entry.rangeText);
                results.push_back(std::move(entry));
            }
        }

        std::error_code removeEc;
        fs::remove(tempPath, removeEc);
        fs::remove(errPath, removeEc);
    }

    editor.setStatusMessage("mlang format errors: " +
                            std::to_string(results.size()));
    return results;
}

std::vector<MlangFormatErrorEntry> collectMlangWarnings(Editor& editor)
{
    std::vector<MlangFormatErrorEntry> results;
#ifdef UVIM_ENABLE_MLANG_LSP
    if(!editor.emitLspDiagnostics)
    {
        editor.setStatusMessage("emitlsp=false");
        return results;
    }
    if(!editor.isMlangLspEnabled() || !editor.mlangLspClient)
    {
        editor.setStatusMessage("mlangd warnings: LSP OFF");
        return results;
    }

    editor.syncMlangSemanticTokensIfNeeded(true);

    fs::path root;
    if(!editor.projectRoot.empty())
        root = editor.projectRoot;
    else
    {
        std::error_code ec;
        root = fs::current_path(ec);
    }
    root = EditorPathUtilities::resolveEditorPath(root);

    auto all = editor.mlangLspClient->allDiagnostics();
    for(const auto& [path, diagnostics] : all)
    {
        for(const auto& diag : diagnostics)
        {
            if(diag.severity != 2)
                continue;
            MlangFormatErrorEntry entry;
            entry.path = path;
            entry.displayPath = relativeDisplayPath(path, root);
            entry.line = std::max(0, diag.line);
            entry.col = std::max(0, diag.character);
            entry.rangeText = "[" + std::to_string(diag.line + 1) + ":" +
                              std::to_string(diag.character + 1) + ", " +
                              std::to_string(diag.endLine + 1) + ":" +
                              std::to_string(diag.endCharacter + 1) + ")";
            entry.message = diag.message;
            if(!diag.source.empty())
                entry.message = diag.source + ": " + entry.message;
            results.push_back(std::move(entry));
        }
    }

    std::sort(results.begin(), results.end(),
              [](const MlangFormatErrorEntry& a,
                 const MlangFormatErrorEntry& b)
              {
                  if(a.displayPath != b.displayPath)
                      return a.displayPath < b.displayPath;
                  if(a.line != b.line)
                      return a.line < b.line;
                  return a.col < b.col;
              });
    editor.setStatusMessage("mlang warnings: " +
                            std::to_string(results.size()));
#else
    editor.setStatusMessage("mlang LSP is not compiled in");
#endif
    return results;
}
