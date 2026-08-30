#include "mlang_format_errors.h"
#include "clang_diagnostics.h"
#include "diagnostics_common.h"
#include "editor.h"
#include "editor_path_utilities.h"
#include "lsp_client.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
namespace dc = diagnostics_common;

namespace
{
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

std::string readFileOrCurrentBuffer(const fs::path& file, const Editor& editor)
{
    if(editor.currentBuffer && !editor.currentBuffer->filename.empty())
    {
        std::error_code ec;
        fs::path current =
            fs::canonical(EditorPathUtilities::resolveEditorPath(
                              editor.currentBuffer->filename),
                          ec);
        ec.clear();
        fs::path candidate = fs::canonical(file, ec);
        if(!ec && current == candidate && editor.lines)
        {
            std::string text;
            text.reserve(editor.lines->size() * 80);
            for(size_t i = 0; i < editor.lines->size(); ++i)
            {
                text += (*editor.lines)[i];
                if(i + 1 < editor.lines->size())
                    text.push_back('\n');
            }
            return text;
        }
    }

    std::ifstream in(file);
    std::string text;
    std::string line;
    while(std::getline(in, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        text += line;
        if(!in.eof())
            text.push_back('\n');
    }
    return text;
}

void refreshProjectMlangDiagnostics(Editor& editor, const fs::path& root)
{
#ifdef UVIM_ENABLE_MLANG_LSP
    if(!editor.mlangLspClient)
        return;

    const std::vector<fs::path> files = collectMlaFiles(root);
    std::unordered_map<std::string, size_t> previousRevisions;
    previousRevisions.reserve(files.size());

    for(const fs::path& file : files)
    {
        const std::string path = dc::normalizedPathString(file);
        previousRevisions[path] =
            editor.mlangLspClient->diagnosticsRevision(path);
        editor.mlangLspClient->didChange(path, readFileOrCurrentBuffer(file, editor),
                                         "mlang");
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
    while(std::chrono::steady_clock::now() < deadline)
    {
        bool allUpdated = true;
        for(const auto& [path, previous] : previousRevisions)
        {
            if(editor.mlangLspClient->diagnosticsRevision(path) == previous)
            {
                allUpdated = false;
                break;
            }
        }
        if(allUpdated)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
#else
    (void)editor;
    (void)root;
#endif
}

#ifdef UVIM_ENABLE_MLANG_LSP
std::vector<MlangFormatErrorEntry> collectMlangLspDiagnostics(Editor& editor,
                                                              int severity)
{
    std::vector<MlangFormatErrorEntry> results;
    fs::path root;
    if(!editor.projectRoot.empty())
        root = editor.projectRoot;
    else
    {
        std::error_code ec;
        root = fs::current_path(ec);
    }
    root = EditorPathUtilities::resolveEditorPath(root);
    refreshProjectMlangDiagnostics(editor, root);

    auto all = editor.mlangLspClient->allDiagnostics();
    for(const auto& [path, diagnostics] : all)
    {
        for(const auto& diag : diagnostics)
        {
            if(diag.severity != severity)
                continue;
            MlangFormatErrorEntry entry;
            entry.path = path;
            entry.displayPath = dc::relativeDisplayPath(path, root);
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
    return results;
}
#endif
} // namespace

std::vector<MlangFormatErrorEntry> collectMlangFormatErrors(Editor& editor)
{
    std::vector<MlangFormatErrorEntry> results;

    std::string fmtExe = dc::resolveExecutablePath("mlang-format");
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
        fs::path tempPath = dc::makeTempPath("uvim_mlang_check", ".mla");
        fs::path errPath = dc::makeTempPath("uvim_mlang_check_err", ".log");
        copyFileOrBuffer(file, tempPath, editor);

        std::string cmd = dc::shellQuote(fmtExe) +
                          " -i --style file --assume-filename=" +
                          dc::shellQuote(file.string()) + " " +
                          dc::shellQuote(tempPath.string()) + " 2>" +
                          dc::shellQuote(errPath.string());
        int status = std::system(cmd.c_str());
        if(status != 0)
        {
            std::vector<std::string> errLines = dc::readLines(errPath);
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
                entry.displayPath = dc::relativeDisplayPath(file, root);
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
    return collectActiveLspDiagnostics(editor, 2);
}

std::vector<MlangFormatErrorEntry> collectActiveLspDiagnostics(Editor& editor,
                                                               int severity)
{
    std::vector<MlangFormatErrorEntry> results;
#if defined(UVIM_ENABLE_CLANGD_LSP) || defined(UVIM_ENABLE_MLANG_LSP)
    if(!editor.emitLspDiagnostics)
    {
        editor.setStatusMessage("emitlsp=false");
        return results;
    }
    if(severity != 1 && severity != 2)
    {
        editor.setStatusMessage("lsp diagnostics: unsupported severity");
        return results;
    }

    const std::string severityName = severity == 1 ? "errors" : "warnings";
    if(editor.isFileType<FileType::Cpp>())
#ifdef UVIM_ENABLE_CLANGD_LSP
        return collectClangDiagnostics(editor, severity);
#else
    {
        editor.setStatusMessage("clangd " + severityName + ": LSP OFF");
        return results;
    }
#endif

    if(editor.isFileType<FileType::Mla>())
    {
#ifdef UVIM_ENABLE_MLANG_LSP
        if(!editor.isMlangLspEnabled() || !editor.mlangLspClient)
        {
            editor.setStatusMessage("mlangd " + severityName + ": LSP OFF");
            return results;
        }
        editor.syncMlangSemanticTokensIfNeeded(true);
        results = collectMlangLspDiagnostics(editor, severity);
        editor.setStatusMessage("mlangd " + severityName + ": " +
                                std::to_string(results.size()));
        return results;
#else
        editor.setStatusMessage("mlangd " + severityName + ": LSP OFF");
        return results;
#endif
    }

    editor.setStatusMessage("lsp " + severityName + ": unsupported file type");
#else
    (void)severity;
    editor.setStatusMessage("LSP is not compiled in");
#endif
    return results;
}
