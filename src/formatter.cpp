#include "formatter.h"
#include "editor.h"
#include "terminal.h"
#include "text_utils.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unistd.h>

Formatter::Formatter(Editor* editor) : editor(editor) {}

size_t Formatter::byteOffsetForPosition(int y, int x) const
{
    if(!editor->lines || editor->lines->empty())
        return 0;

    y = std::clamp(y, 0, (int)editor->lines->size() - 1);
    const std::string& ln = (*editor->lines)[y];
    x = std::clamp(x, 0, (int)ln.size());

    size_t off = 0;
    for(int i = 0; i < y; ++i)
        off += (*editor->lines)[i].size() + 1; // + '\n'
    off += (size_t)x;
    return off;
}

bool Formatter::clangFormatWithArgs(const std::string& extraArgs,
                                 const std::string& successMessage)
{
    if(!editor->lines || !editor->filename)
        return false;

    if(!editor->isFileType<FileType::Cpp>() || editor->isFileType<FileType::Mla>())
    {
        editor->setStatusMessage("clang-format: not a C/C++ file (" + *editor->filename + ")");
        return false;
    }

    const int savedY = editor->cursorY ? *editor->cursorY : 0;
    const int savedX = editor->cursorX ? *editor->cursorX : 0;

    std::string tempPath =
        "/tmp/uvim_format_" + std::to_string(getpid()) + ".tmp";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        editor->setStatusMessage("clang-format: failed to create temp file");
        return false;
    }

    for(size_t i = 0; i < editor->lines->size(); ++i)
        tempFile << (*editor->lines)[i] << '\n';
    tempFile.close();

    std::string absFilename = *editor->filename;
    if(!absFilename.empty() && absFilename[0] != '/')
    {
        char cwd[PATH_MAX];
        if(getcwd(cwd, sizeof(cwd)))
            absFilename = std::string(cwd) + "/" + *editor->filename;
    }

    auto buildCmd = [&](const std::string& exe) -> std::string
    {
        std::string cmd = "cat \"" + tempPath + "\" | " + exe +
                          " -style=file -assume-filename=\"" + absFilename +
                          "\"";
        if(!extraArgs.empty())
            cmd += " " + extraArgs;
        cmd += " 2>/tmp/uvim_clang_err.log";
        return cmd;
    };

    std::string cmd = buildCmd("/opt/homebrew/bin/clang-format");
    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
    {
        cmd = buildCmd("clang-format");
        pipe = popen(cmd.c_str(), "r");
    }

    if(!pipe)
    {
        unlink(tempPath.c_str());
        editor->setStatusMessage("clang-format: failed to run");
        return false;
    }

    std::string formatted;
    char buffer[4096];
    while(fgets(buffer, sizeof(buffer), pipe))
        formatted += buffer;

    int status = pclose(pipe);
    (void)status;
    unlink(tempPath.c_str());

    if(formatted.empty())
    {
        std::ifstream errFile("/tmp/uvim_clang_err.log");
        std::string errMsg;
        if(errFile.is_open())
        {
            std::getline(errFile, errMsg);
            errFile.close();
        }
        if(errMsg.empty())
            errMsg = "no output";
        editor->setStatusMessage("clang-format: " + errMsg.substr(0, 80));
        return false;
    }

    std::vector<std::string> newLines;
    std::istringstream iss(formatted);
    std::string line;
    while(std::getline(iss, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        newLines.push_back(line);
    }

    if(!newLines.empty() && newLines.back().empty())
        newLines.pop_back();
    if(newLines.empty())
        newLines.push_back("");

    if(newLines == *editor->lines)
    {
        editor->setStatusMessage("clang-format: no changes needed");
        return true;
    }

    editor->saveState();
    *editor->lines = std::move(newLines);
    if(editor->dirty)
        *editor->dirty = true;

    if(editor->cursorY && editor->cursorX && editor->lines && !editor->lines->empty())
    {
        *editor->cursorY = std::clamp(savedY, 0, (int)editor->lines->size() - 1);
        *editor->cursorX = std::clamp(savedX, 0, (int)(*editor->lines)[*editor->cursorY].size());
    }

    editor->adjustViewport();
    editor->needsFullRedraw = true;
    editor->setStatusMessage(successMessage);
    return true;
}

bool Formatter::pythonFormatBuffer()
{
    if(!editor->lines || !editor->filename)
        return false;
    if(!editor->isFileType<FileType::Python>())
    {
        editor->setStatusMessage("python: not a Python file");
        return false;
    }

    const int savedY = editor->cursorY ? *editor->cursorY : 0;
    const int savedX = editor->cursorX ? *editor->cursorX : 0;

    std::string tempPath =
        "/tmp/uvim_pyfmt_" + std::to_string(getpid()) + ".py";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        editor->setStatusMessage("black: failed to create temp file");
        return false;
    }

    for(size_t i = 0; i < editor->lines->size(); ++i)
        tempFile << (*editor->lines)[i] << '\n';
    tempFile.close();

    std::string errPath = "/tmp/uvim_pyfmt_err.log";
    std::string cmd;
    if(editor->pythonFormatter == "ruff")
    {
        cmd = "ruff format \"" + tempPath + "\" 2>\"" + errPath + "\"";
    }
    else
    {
        cmd = "black --quiet \"" + tempPath + "\" 2>\"" + errPath + "\"";
    }

    int status = std::system(cmd.c_str());
    if(status != 0)
    {
        std::ifstream errFile(errPath);
        std::string errMsg;
        if(errFile.is_open())
        {
            std::getline(errFile, errMsg);
            errFile.close();
        }
        if(errMsg.empty())
            errMsg = editor->pythonFormatter + " failed";
        unlink(tempPath.c_str());
        editor->setStatusMessage(editor->pythonFormatter + ": " +
                                 errMsg.substr(0, 80));
        return false;
    }

    std::ifstream in(tempPath);
    if(!in.is_open())
    {
        unlink(tempPath.c_str());
        editor->setStatusMessage("black: failed to read temp output");
        return false;
    }

    std::vector<std::string> newLines;
    std::string line;
    while(std::getline(in, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        newLines.push_back(line);
    }
    in.close();
    unlink(tempPath.c_str());

    if(!newLines.empty() && newLines.back().empty())
        newLines.pop_back();
    if(newLines.empty())
        newLines.push_back("");

    if(newLines == *editor->lines)
    {
        editor->setStatusMessage(editor->pythonFormatter + ": no changes");
        return true;
    }

    editor->saveState();
    *editor->lines = std::move(newLines);
    if(editor->dirty)
        *editor->dirty = true;

    if(editor->cursorY && editor->cursorX && editor->lines && !editor->lines->empty())
    {
        *editor->cursorY = std::clamp(savedY, 0, (int)editor->lines->size() - 1);
        *editor->cursorX = std::clamp(savedX, 0, (int)(*editor->lines)[*editor->cursorY].size());
    }

    editor->adjustViewport();
    editor->needsFullRedraw = true;
    editor->setStatusMessage(editor->pythonFormatter + ": formatted buffer");
    return true;
}

void Formatter::pythonLintBuffer()
{
    if(!editor->lines || !editor->filename)
        return;
    if(!editor->isFileType<FileType::Python>())
    {
        editor->setStatusMessage("ruff: not a Python file");
        return;
    }

    std::string tempPath = "/tmp/uvim_ruff_" + std::to_string(getpid()) + ".py";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        editor->setStatusMessage("ruff: failed to create temp file");
        return;
    }
    for(size_t i = 0; i < editor->lines->size(); ++i)
        tempFile << (*editor->lines)[i] << '\n';
    tempFile.close();

    std::string cmd =
        "ruff check --quiet \"" + tempPath + "\" 2>/tmp/uvim_ruff_err.log";
    FILE* pipe = popen(cmd.c_str(), "r");
    if(!pipe)
    {
        unlink(tempPath.c_str());
        editor->setStatusMessage("ruff: failed to run");
        return;
    }

    std::string output;
    char buffer[4096];
    while(fgets(buffer, sizeof(buffer), pipe))
        output += buffer;
    pclose(pipe);
    unlink(tempPath.c_str());

    if(output.empty())
    {
        editor->setStatusMessage("ruff: clean");
        return;
    }

    size_t nl = output.find('\n');
    if(nl != std::string::npos)
        output = output.substr(0, nl);
    editor->setStatusMessage("ruff: " + output);
}

static std::string read_first_line(const std::string& path)
{
    std::ifstream in(path);
    if(!in.is_open())
        return {};
    std::string line;
    std::getline(in, line);
    return line;
}

static std::string normalize_robot_line(const std::string& line, int spaceCount)
{
    if(line.empty())
        return line;
    auto first_non_ws = line.find_first_not_of(" \t");
    if(first_non_ws == std::string::npos)
        return line;
    std::string_view trimmed(line.c_str() + first_non_ws,
                             line.size() - first_non_ws);
    if(trimmed.rfind("***", 0) == 0 || trimmed.rfind("#", 0) == 0)
        return line;

    std::vector<std::string> cells;
    size_t i = first_non_ws;
    if(first_non_ws > 0)
        cells.emplace_back("");

    auto push_cell = [&](std::string& cell)
    {
        cells.push_back(cell);
        cell.clear();
    };

    std::string cell;
    while(i < line.size())
    {
        char c = line[i];
        if(c == '\t')
        {
            push_cell(cell);
            ++i;
            while(i < line.size() && line[i] == ' ')
                ++i;
            continue;
        }
        if(c == ' ')
        {
            size_t j = i;
            while(j < line.size() && line[j] == ' ')
                ++j;
            if(j - i >= 2)
            {
                push_cell(cell);
                i = j;
                continue;
            }
        }
        cell.push_back(c);
        ++i;
    }
    push_cell(cell);

    if(cells.size() <= 1)
        return line;

    std::string out;
    for(size_t idx = 0; idx < cells.size(); ++idx)
    {
        if(idx > 0)
            out.append(spaceCount, ' ');
        out += cells[idx];
    }
    return out;
}

static bool has_robot_cell_separator(std::string_view line)
{
    for(size_t i = 0; i < line.size(); ++i)
    {
        if(line[i] == '\t')
            return true;
        if(line[i] == ' ' && i + 1 < line.size() && line[i + 1] == ' ')
            return true;
    }
    return false;
}

static std::string_view trim_robot_header(std::string_view line)
{
    while(!line.empty() &&
          (line.front() == ' ' || line.front() == '\t' || line.front() == '\r'))
        line.remove_prefix(1);
    while(!line.empty() &&
          (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
        line.remove_suffix(1);
    return line;
}

static bool ascii_starts_with(std::string_view value,
                              std::string_view prefix) noexcept
{
    if(value.size() < prefix.size())
        return false;
    for(size_t i = 0; i < prefix.size(); ++i)
    {
        if(text_utils::ascii_tolower(value[i]) != prefix[i])
            return false;
    }
    return true;
}

static bool split_robot_first_cell(std::string_view line, size_t start,
                                   std::string_view& first,
                                   std::string_view& rest)
{
    size_t i = start;
    while(i < line.size())
    {
        if(line[i] == '\t')
        {
            first = line.substr(start, i - start);
            size_t j = i + 1;
            while(j < line.size() && line[j] == ' ')
                ++j;
            rest = line.substr(j);
            return true;
        }
        if(line[i] == ' ')
        {
            size_t j = i;
            while(j < line.size() && line[j] == ' ')
                ++j;
            if(j - i >= 2)
            {
                first = line.substr(start, i - start);
                rest = line.substr(j);
                return true;
            }
            i = j;
            continue;
        }
        ++i;
    }
    return false;
}

static bool
match_robot_setting_prefix(std::string_view line,
                           const std::unordered_set<std::string>& settings,
                           size_t& matchLen)
{
    matchLen = 0;
    if(settings.empty())
        return false;

    for(const auto& setting : settings)
    {
        std::string_view prefix(setting);
        if(!ascii_starts_with(line, prefix))
            continue;
        if(line.size() > prefix.size())
        {
            char next = line[prefix.size()];
            if(next != ' ' && next != '\t')
                continue;
        }
        if(prefix.size() > matchLen)
            matchLen = prefix.size();
    }
    return matchLen > 0;
}

static std::vector<std::string>
normalize_robot_spacing(const std::vector<std::string>& input, int spaceCount,
                        const std::unordered_set<std::string>& settings)
{
    enum class Section
    {
        None,
        Settings,
        TestCases,
        Tasks,
        Keywords,
        Other,
    };

    Section section = Section::None;
    size_t settingsFirstWidth = 0;
    for(const auto& setting : settings)
        settingsFirstWidth = std::max(settingsFirstWidth, setting.size());

    for(const auto& raw : input)
    {
        std::string line = normalize_robot_line(raw, spaceCount);
        std::string_view trimmed = trim_robot_header(line);
        if(trimmed.empty() || trimmed.front() == '#')
            continue;

        if(trimmed.rfind("***", 0) == 0 && trimmed.ends_with("***"))
        {
            std::string_view inner = trimmed.substr(3, trimmed.size() - 6);
            inner = trim_robot_header(inner);
            if(text_utils::iequals_ascii(inner, "Settings"))
                section = Section::Settings;
            else if(text_utils::iequals_ascii(inner, "Test Cases"))
                section = Section::TestCases;
            else if(text_utils::iequals_ascii(inner, "Tasks"))
                section = Section::Tasks;
            else if(text_utils::iequals_ascii(inner, "Keywords"))
                section = Section::Keywords;
            else
                section = Section::Other;
            continue;
        }

        if(section == Section::Settings)
        {
            size_t firstNonWs = line.find_first_not_of(" \t");
            if(firstNonWs == std::string_view::npos)
                continue;
            std::string_view lineView(line);
            std::string_view trimmedLine = lineView.substr(firstNonWs);
            std::string_view firstCell;
            std::string_view restCell;
            if(split_robot_first_cell(trimmedLine, 0, firstCell, restCell))
            {
                settingsFirstWidth =
                    std::max(settingsFirstWidth, firstCell.size());
            }
            else
            {
                size_t matchLen = 0;
                if(match_robot_setting_prefix(trimmedLine, settings, matchLen))
                {
                    settingsFirstWidth = std::max(settingsFirstWidth, matchLen);
                }
            }
        }
    }

    section = Section::None;
    bool prevNonEmptyTitle = false;
    std::vector<std::string> out;
    out.reserve(input.size());

    for(const auto& raw : input)
    {
        std::string line = normalize_robot_line(raw, spaceCount);
        std::string_view trimmed = trim_robot_header(line);
        if(trimmed.empty() || trimmed.front() == '#')
        {
            out.push_back(std::move(line));
            continue;
        }

        if(trimmed.rfind("***", 0) == 0 && trimmed.ends_with("***"))
        {
            std::string_view inner = trimmed.substr(3, trimmed.size() - 6);
            inner = trim_robot_header(inner);
            if(text_utils::iequals_ascii(inner, "Settings"))
                section = Section::Settings;
            else if(text_utils::iequals_ascii(inner, "Test Cases"))
                section = Section::TestCases;
            else if(text_utils::iequals_ascii(inner, "Tasks"))
                section = Section::Tasks;
            else if(text_utils::iequals_ascii(inner, "Keywords"))
                section = Section::Keywords;
            else
                section = Section::Other;

            out.push_back(std::move(line));
            prevNonEmptyTitle = false;
            continue;
        }

        if(settingsFirstWidth > 0)
        {
            size_t firstNonWs = line.find_first_not_of("    ");
            if(firstNonWs != std::string_view::npos)
            {
                std::string prefix(line.substr(0, firstNonWs));
                std::string_view lineView(line);
                std::string_view trimmedLine = lineView.substr(firstNonWs);
                size_t matchLen = 0;
                bool match =
                    match_robot_setting_prefix(trimmedLine, settings, matchLen);
                if(match && section == Section::Settings)
                {
                    size_t restStart = matchLen;
                    while(restStart < trimmedLine.size() &&
                          (trimmedLine[restStart] == ' ' ||
                           trimmedLine[restStart] == '  '))
                    {
                        ++restStart;
                    }
                    std::string rebuilt = prefix;
                    std::string_view firstMatch =
                        trimmedLine.substr(0, matchLen);
                    rebuilt += firstMatch;
                    size_t pad = (settingsFirstWidth > matchLen)
                                     ? (settingsFirstWidth - matchLen)
                                     : 0;
                    rebuilt.append(pad + (size_t)spaceCount, ' ');
                    rebuilt += trimmedLine.substr(restStart);
                    line = std::move(rebuilt);
                }
                else if(section == Section::Settings)
                {
                    std::string_view firstCell;
                    std::string_view restCell;
                    if(split_robot_first_cell(trimmedLine, 0, firstCell,
                                              restCell))
                    {
                        std::string rebuilt = prefix;
                        rebuilt += firstCell;
                        size_t pad =
                            (settingsFirstWidth > firstCell.size())
                                ? (settingsFirstWidth - firstCell.size())
                                : 0;
                        rebuilt.append(pad + (size_t)spaceCount, ' ');
                        rebuilt += restCell;
                        line = std::move(rebuilt);
                    }
                }
            }
        }

        bool needsIndent =
            (section == Section::TestCases || section == Section::Tasks ||
             section == Section::Keywords);
        bool hasIndent = !line.empty() && (line[0] == ' ' || line[0] == '\t');
        if(needsIndent && !hasIndent)
        {
            bool shouldIndent = false;
            if(has_robot_cell_separator(line))
                shouldIndent = true;
            else if(prevNonEmptyTitle)
                shouldIndent = true;

            if(shouldIndent)
            {
                line.insert(0, std::string(spaceCount, ' '));
                hasIndent = true;
            }
        }

        prevNonEmptyTitle = !hasIndent;

        out.push_back(std::move(line));
    }

    return out;
}

bool Formatter::robotFormatBuffer()
{
    if(!editor->lines || !editor->filename)
        return false;
    if(!editor->isFileType<FileType::Robot>())
    {
        editor->setStatusMessage("robocop: not a Robot file");
        return false;
    }

    const int savedY = editor->cursorY ? *editor->cursorY : 0;
    const int savedX = editor->cursorX ? *editor->cursorX : 0;

    std::string tempPath =
        "/tmp/uvim_robot_" + std::to_string(getpid()) + ".robot";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        editor->setStatusMessage("robocop: failed to create temp file");
        return false;
    }
    for(size_t i = 0; i < editor->lines->size(); ++i)
        tempFile << (*editor->lines)[i] << '\n';
    tempFile.close();

    const std::string robocopErr = "/tmp/uvim_robocop_err.log";
    const std::string robocopOut = "/tmp/uvim_robocop_out.log";
    const std::string robocopFmt = "/tmp/uvim_robocop_fmt.log";

    auto run_robocop_output = [&](const std::string& extraArgs,
                                  const char* outputFlag) -> bool
    {
        std::string cmd = "robocop format " + extraArgs + " " + outputFlag +
                          " \"" + robocopFmt + "\" \"" + tempPath + "\" >" +
                          robocopOut + " 2>" + robocopErr;
        int status = std::system(cmd.c_str());
        return status == 0;
    };

    auto run_robocop_output_after = [&](const std::string& extraArgs,
                                        const char* outputFlag) -> bool
    {
        std::string cmd = "robocop format " + extraArgs + " \"" + tempPath +
                          "\" " + outputFlag + " \"" + robocopFmt + "\" >" +
                          robocopOut + " 2>" + robocopErr;
        int status = std::system(cmd.c_str());
        return status == 0;
    };

    auto run_robocop_stdout = [&](const std::string& extraArgs) -> bool
    {
        std::string cmd = "robocop format " + extraArgs + " \"" + tempPath +
                          "\" >\"" + robocopFmt + "\" 2>" + robocopErr;
        int status = std::system(cmd.c_str());
        return status == 0;
    };

    bool robocopOk = run_robocop_output("", "--output") ||
                     run_robocop_output("", "--output-file") ||
                     run_robocop_output("", "-o") ||
                     run_robocop_output_after("", "--output") ||
                     run_robocop_output_after("", "--output-file") ||
                     run_robocop_output_after("", "-o") ||
                     run_robocop_stdout("");

    if(!robocopOk)
    {
        std::string err = read_first_line(robocopErr);
        if(err.empty())
            err = "robocop failed";
        editor->setStatusMessage(err);
        unlink(tempPath.c_str());
        unlink(robocopFmt.c_str());
        return false;
    }

    const std::string& readPath =
        std::filesystem::exists(robocopFmt) ? robocopFmt : tempPath;
    std::ifstream in(readPath);
    if(!in.is_open())
    {
        unlink(tempPath.c_str());
        unlink(robocopFmt.c_str());
        editor->setStatusMessage("robocop: failed to read temp output");
        return false;
    }

    std::vector<std::string> newLines;
    std::string line;
    while(std::getline(in, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        newLines.push_back(line);
    }
    in.close();
    unlink(tempPath.c_str());
    unlink(robocopFmt.c_str());

    if(!newLines.empty() && newLines.back().empty())
        newLines.pop_back();
    if(newLines.empty())
        newLines.push_back("");

    auto looks_like_robocop_log = [&]() -> bool
    {
        if(newLines.empty())
            return false;
        if(newLines[0].rfind("Usage: robocop", 0) == 0)
            return true;
        if(newLines[0].rfind("Reformatted ", 0) == 0)
            return true;
        for(const auto& l : newLines)
        {
            if(l.find("file reformatted") != std::string::npos ||
               l.find("files left unchanged") != std::string::npos)
                return true;
        }
        return false;
    };

    if(looks_like_robocop_log())
    {
        auto normalized = normalize_robot_spacing(*editor->lines, 4, editor->robotSettingSet);
        if(normalized != *editor->lines)
        {
            *editor->lines = std::move(normalized);
            editor->saveState();
            if(editor->dirty)
                *editor->dirty = true;
            editor->adjustViewport();
            editor->needsFullRedraw = true;
            editor->setStatusMessage("robocop: normalized spacing");
            return true;
        }
        editor->setStatusMessage("robocop: no changes");
        editor->needsFullRedraw = true;
        return true;
    }

    auto normalized = normalize_robot_spacing(newLines, 4, editor->robotSettingSet);
    if(normalized != newLines)
        newLines = std::move(normalized);

    if(newLines == *editor->lines)
    {
        editor->setStatusMessage("robocop: no changes");
        editor->needsFullRedraw = true;
        return true;
    }

    *editor->lines = std::move(newLines);
    editor->saveState();
    if(editor->dirty)
        *editor->dirty = true;

    if(editor->cursorY && editor->cursorX && editor->lines && !editor->lines->empty())
    {
        *editor->cursorY = std::clamp(savedY, 0, (int)editor->lines->size() - 1);
        *editor->cursorX = std::clamp(savedX, 0, (int)(*editor->lines)[*editor->cursorY].size());
    }

    editor->adjustViewport();
    editor->needsFullRedraw = true;
    editor->setStatusMessage("robocop: formatted buffer");
    return true;
}

bool Formatter::jsonFormatBuffer()
{
    if(!editor->lines || !editor->filename)
        return false;
    if(!editor->isFileType<FileType::Json>())
    {
        editor->setStatusMessage("json.tool: not a JSON file");
        return false;
    }

    const int savedY = editor->cursorY ? *editor->cursorY : 0;
    const int savedX = editor->cursorX ? *editor->cursorX : 0;

    std::string tempPath =
        "/tmp/uvim_json_" + std::to_string(getpid()) + ".json";
    std::string outPath =
        "/tmp/uvim_json_" + std::to_string(getpid()) + "_out.json";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        editor->setStatusMessage("json.tool: failed to create temp file");
        return false;
    }
    for(size_t i = 0; i < editor->lines->size(); ++i)
        tempFile << (*editor->lines)[i] << '\n';
    tempFile.close();

    std::string cmd = "python -m json.tool \"" + tempPath + "\" > \"" +
                      outPath + "\" 2>/tmp/uvim_json_err.log";
    int status = std::system(cmd.c_str());
    if(status != 0)
    {
        std::string err = read_first_line("/tmp/uvim_json_err.log");
        if(err.empty())
            err = "json.tool failed";
        editor->setStatusMessage(err);
        unlink(tempPath.c_str());
        unlink(outPath.c_str());
        return false;
    }

    std::ifstream in(outPath);
    if(!in.is_open())
    {
        unlink(tempPath.c_str());
        unlink(outPath.c_str());
        editor->setStatusMessage("json.tool: failed to read temp output");
        return false;
    }

    std::vector<std::string> newLines;
    std::string line;
    while(std::getline(in, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        newLines.push_back(line);
    }
    in.close();
    unlink(tempPath.c_str());
    unlink(outPath.c_str());

    if(!newLines.empty() && newLines.back().empty())
        newLines.pop_back();
    if(newLines.empty())
        newLines.push_back("");

    if(newLines == *editor->lines)
    {
        editor->setStatusMessage("json.tool: no changes");
        return true;
    }

    editor->saveState();
    *editor->lines = std::move(newLines);
    if(editor->dirty)
        *editor->dirty = true;

    if(editor->cursorY && editor->cursorX && editor->lines && !editor->lines->empty())
    {
        *editor->cursorY = std::clamp(savedY, 0, (int)editor->lines->size() - 1);
        *editor->cursorX = std::clamp(savedX, 0, (int)(*editor->lines)[*editor->cursorY].size());
    }

    editor->adjustViewport();
    editor->needsFullRedraw = true;
    editor->setStatusMessage("json.tool: formatted buffer");
    return true;
}

bool Formatter::yamlFormatBuffer()
{
    if(!editor->lines || !editor->filename)
        return false;
    if(!editor->isFileType<FileType::Yaml>())
    {
        editor->setStatusMessage("yaml: not a YAML file");
        return false;
    }

    const int savedY = editor->cursorY ? *editor->cursorY : 0;
    const int savedX = editor->cursorX ? *editor->cursorX : 0;

    std::string tempPath =
        "/tmp/uvim_yaml_" + std::to_string(getpid()) + ".yml";
    std::string outPath =
        "/tmp/uvim_yaml_" + std::to_string(getpid()) + "_out.yml";
    std::ofstream tempFile(tempPath);
    if(!tempFile.is_open())
    {
        editor->setStatusMessage("yaml: failed to create temp file");
        return false;
    }
    for(size_t i = 0; i < editor->lines->size(); ++i)
        tempFile << (*editor->lines)[i] << '\n';
    tempFile.close();

    std::string cmd = "python -m yaml \"" + tempPath + "\" > \"" + outPath +
                      "\" 2>/tmp/uvim_yaml_err.log";
    int status = std::system(cmd.c_str());
    if(status != 0)
    {
        std::string err = read_first_line("/tmp/uvim_yaml_err.log");
        if(err.empty())
            err = "yaml formatter failed";
        editor->setStatusMessage(err);
        unlink(tempPath.c_str());
        unlink(outPath.c_str());
        return false;
    }

    std::ifstream in(outPath);
    if(!in.is_open())
    {
        unlink(tempPath.c_str());
        unlink(outPath.c_str());
        editor->setStatusMessage("yaml: failed to read temp output");
        return false;
    }

    std::vector<std::string> newLines;
    std::string line;
    while(std::getline(in, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        newLines.push_back(line);
    }
    in.close();
    unlink(tempPath.c_str());
    unlink(outPath.c_str());

    if(!newLines.empty() && newLines.back().empty())
        newLines.pop_back();
    if(newLines.empty())
        newLines.push_back("");

    if(newLines == *editor->lines)
    {
        editor->setStatusMessage("yaml: no changes");
        return true;
    }

    editor->saveState();
    *editor->lines = std::move(newLines);
    if(editor->dirty)
        *editor->dirty = true;

    if(editor->cursorY && editor->cursorX && editor->lines && !editor->lines->empty())
    {
        *editor->cursorY = std::clamp(savedY, 0, (int)editor->lines->size() - 1);
        *editor->cursorX = std::clamp(savedX, 0, (int)(*editor->lines)[*editor->cursorY].size());
    }

    editor->adjustViewport();
    editor->needsFullRedraw = true;
    editor->setStatusMessage("yaml: formatted buffer");
    return true;
}

void Formatter::clangFormatVisualSelection()
{
    if(!editor || (editor->currentMode != VISUAL &&
                   editor->currentMode != VISUAL_LINE))
        return;

    if(!editor->lines || editor->lines->empty())
    {
        editor->setStatusMessage("clang-format: empty buffer");
        return;
    }

    if(editor->currentMode == VISUAL_LINE)
    {
        const int startY =
            std::min(editor->currentBuffer->visualStartY, editor->currentBuffer->visualEndY);
        const int endY =
            std::max(editor->currentBuffer->visualStartY, editor->currentBuffer->visualEndY);

        const int startLine = startY + 1;
        const int endLine = endY + 1;

        const std::string args =
            "-lines=" + std::to_string(startLine) + ":" + std::to_string(endLine);

        clangFormatWithArgs(args, "clang-format: formatted selection (lines)");
        return;
    }

    int startY, startX, endY, endX;
    editor->getSelectionBounds(startY, startX, endY, endX);

    endY = std::clamp(endY, 0, (int)editor->lines->size() - 1);
    const int endLineLen = (int)(*editor->lines)[endY].size();
    const int endXExclusive = std::clamp(endX + 1, 0, endLineLen);

    const size_t startOff = byteOffsetForPosition(startY, startX);
    const size_t endOff = byteOffsetForPosition(endY, endXExclusive);

    if(endOff <= startOff)
    {
        editor->setStatusMessage("clang-format: empty selection");
        return;
    }

    const size_t len = endOff - startOff;
    const std::string args = "-offset=" + std::to_string(startOff) +
                             " -length=" + std::to_string(len);

    clangFormatWithArgs(args, "clang-format: formatted selection");
}

void Formatter::clangFormatVisualBlockSelection()
{
    if(!editor || editor->currentMode != VISUAL_BLOCK)
        return;

    if(!editor->lines || editor->lines->empty())
    {
        editor->setStatusMessage("clang-format: empty buffer");
        return;
    }

    int startY, startX, endY, endX;
    editor->getVisualBlockBounds(startY, startX, endY, endX);

    std::string args;
    for(int y = startY; y <= endY && y < (int)editor->lines->size(); ++y)
    {
        const int lineLen = (int)(*editor->lines)[y].size();
        const int segStart = std::clamp(startX, 0, lineLen);
        const int segEndExclusive = std::clamp(endX + 1, 0, lineLen);

        if(segEndExclusive <= segStart)
            continue;

        const size_t off = byteOffsetForPosition(y, segStart);
        const size_t len = (size_t)(segEndExclusive - segStart);

        args += " -offset=" + std::to_string(off) +
                " -length=" + std::to_string(len);
    }

    if(args.empty())
    {
        editor->setStatusMessage("clang-format: empty visual block");
        return;
    }

    if(!args.empty() && args[0] == ' ')
        args.erase(0, 1);

    clangFormatWithArgs(args, "clang-format: formatted visual block");
}
