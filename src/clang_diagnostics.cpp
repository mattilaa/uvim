#include "clang_diagnostics.h"
#include "diagnostics_common.h"
#include "editor.h"
#include "editor_path_utilities.h"
#include "json_utils.h"
#include "lsp_client.h"
#include "text_utils.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;
namespace ju = json_utils;
namespace dc = diagnostics_common;

namespace
{
struct CompileCommandEntry
{
    fs::path file;
    fs::path directory;
    std::string command;
};

struct CompilerDiagnosticCache
{
    std::unordered_map<std::string, std::vector<DiagnosticEntry>>
        diagnostics;
    std::unordered_set<std::string> validKeys;
};

bool isCppCompileCommandFile(const fs::path& path)
{
    const std::string ext = path.extension().string();
    return ext == ".c" || ext == ".cc" || ext == ".cpp" || ext == ".cxx" ||
           ext == ".h" || ext == ".hh" || ext == ".hpp" || ext == ".hxx";
}

fs::path detectClangdCompileCommandsDir(const Editor& editor,
                                        const fs::path& root)
{
    if(!editor.clangdLspCompileCommandsDir.empty())
        return EditorPathUtilities::resolveEditorPath(
            editor.clangdLspCompileCommandsDir);

    std::error_code ec;
    if(fs::exists(root / "compile_commands.json", ec))
        return root;
    ec.clear();
    if(fs::exists(root / "build" / "compile_commands.json", ec))
        return root / "build";
    return {};
}

std::vector<CompileCommandEntry> collectCompileCommandEntries(
    const fs::path& ccDir)
{
    std::vector<CompileCommandEntry> entries;
    std::ifstream in(ccDir / "compile_commands.json");
    if(!in.is_open())
        return entries;

    ju::Document doc;
    if(!ju::parse(doc, in) || !doc.IsArray())
        return entries;

    std::unordered_set<std::string> seen;
    for(const ju::Value& item : doc.GetArray())
    {
        if(!item.IsObject())
            continue;

        std::string file = ju::get_string(item, "file");
        if(file.empty())
            continue;

        fs::path directory = ju::get_string(item, "directory");
        if(directory.empty())
            directory = ccDir;

        fs::path path(file);
        if(path.is_relative())
            path = directory / path;
        if(!isCppCompileCommandFile(path))
            continue;

        const std::string key = dc::normalizedPathString(path);
        if(seen.insert(key).second)
        {
            CompileCommandEntry entry;
            entry.file = key;
            entry.directory = directory;
            entry.command = ju::get_string(item, "command");
            entries.push_back(std::move(entry));
        }
    }

    std::sort(entries.begin(), entries.end(),
              [](const CompileCommandEntry& a, const CompileCommandEntry& b)
              {
                  return a.file < b.file;
              });
    return entries;
}

bool parseCompilerDiagnosticLine(const std::string& line, int severity,
                                 const fs::path& root, const fs::path& baseDir,
                                 DiagnosticEntry& entry)
{
    static const std::regex diagnosticRe(
        R"(^(.+):([0-9]+):([0-9]+):\s+(warning|error):\s+(.*)$)");
    std::smatch match;
    if(!std::regex_match(line, match, diagnosticRe))
        return false;

    const std::string kind = match.str(4);
    if((severity == 1 && kind != "error") ||
       (severity == 2 && kind != "warning"))
        return false;

    fs::path path = match.str(1);
    if(path.is_relative())
        path = baseDir / path;
    entry.path = dc::normalizedPathString(path);
    entry.displayPath = dc::relativeDisplayPath(entry.path, root);
    entry.line = std::max(0, std::stoi(match.str(2)) - 1);
    entry.col = std::max(0, std::stoi(match.str(3)) - 1);
    entry.rangeText = std::to_string(entry.line + 1) + ":" +
                      std::to_string(entry.col + 1);
    entry.message = match.str(5);
    return true;
}

std::string escapeCacheField(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for(char ch : text)
    {
        switch(ch)
        {
        case '\\':
            out += "\\\\";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

std::string unescapeCacheField(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for(size_t i = 0; i < text.size(); ++i)
    {
        char ch = text[i];
        if(ch != '\\' || i + 1 >= text.size())
        {
            out.push_back(ch);
            continue;
        }
        char escaped = text[++i];
        switch(escaped)
        {
        case 't':
            out.push_back('\t');
            break;
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        default:
            out.push_back(escaped);
            break;
        }
    }
    return out;
}

std::vector<std::string> splitCacheLine(const std::string& line)
{
    std::vector<std::string> fields;
    size_t start = 0;
    while(start <= line.size())
    {
        size_t end = line.find('\t', start);
        if(text_utils::is_not_found(end))
            end = line.size();
        fields.push_back(unescapeCacheField(
            std::string_view(line).substr(start, end - start)));
        if(end == line.size())
            break;
        start = end + 1;
    }
    return fields;
}

std::string fileMtimeKey(const fs::path& path)
{
    std::error_code ec;
    const auto time = fs::last_write_time(path, ec);
    if(ec)
        return "0";
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           time.time_since_epoch())
                           .count();
    return std::to_string(nanos);
}

std::string hashString(std::string_view text)
{
    std::ostringstream out;
    out << std::hex << std::hash<std::string_view>{}(text);
    return out.str();
}

std::string compileCacheEntryKey(const CompileCommandEntry& entry)
{
    return dc::normalizedPathString(entry.file) + "\t" +
           fileMtimeKey(entry.file) + "\t" + hashString(entry.command);
}

fs::path compilerDiagnosticsCachePath(const fs::path& root,
                                      const fs::path& ccDir, int severity)
{
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec);
    if(ec || dir.empty())
        dir = fs::current_path(ec);

    const std::string key =
        dc::normalizedPathString(root) + "\n" +
        dc::normalizedPathString(ccDir) + "\n" + std::to_string(severity);
    return dir / ("uvim_clang_diag_cache_" + hashString(key) + ".tsv");
}

CompilerDiagnosticCache readCompilerDiagnosticCache(const fs::path& cachePath,
                                                    const fs::path& root)
{
    CompilerDiagnosticCache cache;
    for(const std::string& line : dc::readLines(cachePath))
    {
        const std::vector<std::string> fields = splitCacheLine(line);
        if(fields.empty())
            continue;
        if(fields[0] == "F" && fields.size() == 2)
        {
            cache.validKeys.insert(fields[1]);
            continue;
        }
        if(fields[0] != "D" || fields.size() != 7)
            continue;

        DiagnosticEntry entry;
        entry.path = fields[2];
        entry.displayPath = dc::relativeDisplayPath(entry.path, root);
        entry.line = std::max(0, std::atoi(fields[3].c_str()));
        entry.col = std::max(0, std::atoi(fields[4].c_str()));
        entry.rangeText = fields[5];
        entry.message = fields[6];
        cache.validKeys.insert(fields[1]);
        cache.diagnostics[fields[1]].push_back(std::move(entry));
    }
    return cache;
}

void writeCompilerDiagnosticCache(
    const fs::path& cachePath,
    const std::vector<std::pair<std::string, std::vector<DiagnosticEntry>>>&
        rows)
{
    std::ofstream out(cachePath);
    if(!out.is_open())
        return;

    for(const auto& [key, diagnostics] : rows)
    {
        out << "F\t" << escapeCacheField(key) << '\n';
        for(const DiagnosticEntry& entry : diagnostics)
        {
            out << "D\t" << escapeCacheField(key) << '\t'
                << escapeCacheField(entry.path) << '\t' << entry.line << '\t'
                << entry.col << '\t' << escapeCacheField(entry.rangeText)
                << '\t' << escapeCacheField(entry.message) << '\n';
        }
    }
}

std::string resolveClangdExecutable(const Editor& editor)
{
    std::string exe = dc::resolveExecutablePath(editor.clangdLspPath);
    if(!exe.empty())
        return exe;

    std::error_code ec;
    const std::string homebrew = "/opt/homebrew/bin/clangd";
    if(fs::exists(homebrew, ec) && fs::is_regular_file(homebrew, ec))
        return homebrew;
    return dc::resolveExecutablePath("clangd");
}

std::string defaultClangdQueryDriverAllowList()
{
#ifdef _WIN32
    return {};
#else
    return "/usr/bin/*clang*,/usr/bin/*clang++*,/usr/bin/*gcc*,/usr/bin/*g++*,"
           "/bin/*gcc*,/bin/*g++*,"
           "/usr/local/bin/*clang*,/usr/local/bin/*clang++*,/usr/local/bin/"
           "*gcc*,/usr/local/bin/*g++*,"
           "/opt/homebrew/bin/*clang*,/opt/homebrew/bin/*clang++*,/opt/"
           "homebrew/bin/*gcc*,/opt/homebrew/bin/*g++*";
#endif
}

std::vector<DiagnosticEntry>
collectClangdCheckDiagnostics(Editor& editor, int severity,
                              const fs::path& root)
{
    std::vector<DiagnosticEntry> results;
    const std::string clangdExe = resolveClangdExecutable(editor);
    if(clangdExe.empty())
        return results;

    const fs::path ccDir = detectClangdCompileCommandsDir(editor, root);
    if(ccDir.empty())
        return results;

    const std::vector<CompileCommandEntry> entries =
        collectCompileCommandEntries(ccDir);
    if(entries.empty())
        return results;

    std::string queryDriver = editor.clangdLspQueryDriverAllowList;
    if(queryDriver.empty())
        queryDriver = defaultClangdQueryDriverAllowList();

    for(const CompileCommandEntry& entry : entries)
    {
        fs::path logPath = dc::makeTempPath("uvim_clangd_check", ".log");
        std::string cmd = dc::shellQuote(clangdExe) +
                          " --compile-commands-dir=" +
                          dc::shellQuote(ccDir.string()) + " --check=" +
                          dc::shellQuote(entry.file.string());
        if(!queryDriver.empty())
            cmd += " --query-driver=" + dc::shellQuote(queryDriver);
        cmd += " >" + dc::shellQuote(logPath.string()) + " 2>&1";

        (void)std::system(cmd.c_str());
        for(const std::string& line : dc::readLines(logPath))
        {
            DiagnosticEntry parsed;
            if(parseCompilerDiagnosticLine(line, severity, root,
                                           entry.directory, parsed))
                results.push_back(std::move(parsed));
        }

        std::error_code ec;
        fs::remove(logPath, ec);
    }
    return results;
}

std::vector<DiagnosticEntry>
collectCompilerCommandDiagnostics(int severity, const fs::path& root,
                                  const fs::path& ccDir)
{
    std::vector<DiagnosticEntry> results;
    const std::vector<CompileCommandEntry> entries =
        collectCompileCommandEntries(ccDir);
    const fs::path cachePath =
        compilerDiagnosticsCachePath(root, ccDir, severity);
    CompilerDiagnosticCache cache =
        readCompilerDiagnosticCache(cachePath, root);
    std::vector<std::pair<std::string, std::vector<DiagnosticEntry>>>
        cacheRows;
    cacheRows.reserve(entries.size());

    for(const CompileCommandEntry& entry : entries)
    {
        if(entry.command.empty())
            continue;

        const std::string cacheKey = compileCacheEntryKey(entry);
        auto cached = cache.diagnostics.find(cacheKey);
        if(cache.validKeys.find(cacheKey) != cache.validKeys.end())
        {
            std::vector<DiagnosticEntry> diagnostics;
            if(cached != cache.diagnostics.end())
                diagnostics = cached->second;
            results.insert(results.end(), diagnostics.begin(),
                           diagnostics.end());
            cacheRows.emplace_back(cacheKey, std::move(diagnostics));
            continue;
        }

        std::vector<DiagnosticEntry> diagnostics;
        fs::path logPath = dc::makeTempPath("uvim_compile_check", ".log");
        std::string cmd = "(cd " + dc::shellQuote(entry.directory.string()) +
                          " && " + entry.command +
                          " -fsyntax-only) >" +
                          dc::shellQuote(logPath.string()) + " 2>&1";
        (void)std::system(cmd.c_str());

        for(const std::string& line : dc::readLines(logPath))
        {
            DiagnosticEntry parsed;
            if(parseCompilerDiagnosticLine(line, severity, root,
                                           entry.directory, parsed))
                diagnostics.push_back(std::move(parsed));
        }

        std::error_code ec;
        fs::remove(logPath, ec);

        results.insert(results.end(), diagnostics.begin(), diagnostics.end());
        cacheRows.emplace_back(cacheKey, std::move(diagnostics));
    }

    writeCompilerDiagnosticCache(cachePath, cacheRows);
    return results;
}

bool compileCommandsHaveShellCommand(const fs::path& ccDir)
{
    const std::vector<CompileCommandEntry> entries =
        collectCompileCommandEntries(ccDir);
    return std::any_of(entries.begin(), entries.end(),
                       [](const CompileCommandEntry& entry)
                       {
                           return !entry.command.empty();
                       });
}

void mergeAndSortDiagnostics(std::vector<DiagnosticEntry>& target,
                             std::vector<DiagnosticEntry> extra)
{
    std::unordered_set<std::string> seen;
    auto makeKey = [](const DiagnosticEntry& entry)
    {
        return entry.path + ":" + std::to_string(entry.line) + ":" +
               std::to_string(entry.col) + ":" + entry.message;
    };

    std::vector<DiagnosticEntry> merged;
    merged.reserve(target.size() + extra.size());
    for(DiagnosticEntry& entry : target)
    {
        if(seen.insert(makeKey(entry)).second)
            merged.push_back(std::move(entry));
    }
    for(DiagnosticEntry& entry : extra)
    {
        if(seen.insert(makeKey(entry)).second)
            merged.push_back(std::move(entry));
    }

    std::sort(merged.begin(), merged.end(),
              [](const DiagnosticEntry& a, const DiagnosticEntry& b)
              {
                  if(a.displayPath != b.displayPath)
                      return a.displayPath < b.displayPath;
                  if(a.line != b.line)
                      return a.line < b.line;
                  if(a.col != b.col)
                      return a.col < b.col;
                  return a.message < b.message;
              });
    target = std::move(merged);
}

#ifdef UVIM_ENABLE_CLANGD_LSP
std::vector<DiagnosticEntry> collectClangLspDiagnostics(Editor& editor,
                                                        int severity)
{
    std::vector<DiagnosticEntry> results;
    fs::path root;
    if(!editor.projectRoot.empty())
        root = editor.projectRoot;
    else
    {
        std::error_code ec;
        root = fs::current_path(ec);
    }
    root = EditorPathUtilities::resolveEditorPath(root);

    auto all = editor.lspClient->allDiagnostics();
    for(const auto& [path, diagnostics] : all)
    {
        for(const auto& diag : diagnostics)
        {
            if(diag.severity != severity)
                continue;
            DiagnosticEntry entry;
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
              [](const DiagnosticEntry& a, const DiagnosticEntry& b)
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

std::vector<DiagnosticEntry> collectClangDiagnostics(Editor& editor,
                                                     int severity)
{
    std::vector<DiagnosticEntry> results;
#ifdef UVIM_ENABLE_CLANGD_LSP
    const std::string severityName = severity == 1 ? "errors" : "warnings";
    if(!editor.isClangdLspEnabled() || !editor.lspClient)
    {
        editor.setStatusMessage("clangd " + severityName + ": LSP OFF");
        return results;
    }

    editor.syncClangdDiagnosticsIfNeeded(true);
    results = collectClangLspDiagnostics(editor, severity);

    fs::path root;
    if(!editor.projectRoot.empty())
        root = editor.projectRoot;
    else
    {
        std::error_code ec;
        root = fs::current_path(ec);
    }
    root = EditorPathUtilities::resolveEditorPath(root);

    const fs::path ccDir = detectClangdCompileCommandsDir(editor, root);
    if(!ccDir.empty())
    {
        mergeAndSortDiagnostics(
            results, collectCompilerCommandDiagnostics(severity, root, ccDir));
        if(!compileCommandsHaveShellCommand(ccDir))
            mergeAndSortDiagnostics(
                results, collectClangdCheckDiagnostics(editor, severity, root));
    }
    else
    {
        mergeAndSortDiagnostics(
            results, collectClangdCheckDiagnostics(editor, severity, root));
    }

    editor.setStatusMessage("clangd " + severityName + ": " +
                            std::to_string(results.size()));
#else
    (void)editor;
    (void)severity;
#endif
    return results;
}
