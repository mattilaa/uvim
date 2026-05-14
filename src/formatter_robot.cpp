#include "editor.h"
#include "formatter.h"
#include "os_compat.h"
#include "text_utils.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

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
                           trimmedLine[restStart] == '\t'))
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
        auto normalized =
            normalize_robot_spacing(*editor->lines, 4, editor->robotSettingSet);
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

    auto normalized =
        normalize_robot_spacing(newLines, 4, editor->robotSettingSet);
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

    if(editor->cursorY && editor->cursorX && editor->lines &&
       !editor->lines->empty())
    {
        *editor->cursorY =
            std::clamp(savedY, 0, (int)editor->lines->size() - 1);
        *editor->cursorX = std::clamp(
            savedX, 0, (int)(*editor->lines)[*editor->cursorY].size());
    }

    editor->adjustViewport();
    editor->needsFullRedraw = true;
    editor->setStatusMessage("robocop: formatted buffer");
    return true;
}
