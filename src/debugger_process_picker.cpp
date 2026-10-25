#include "debugger_process_picker.h"
#include "editor.h"
#include "editor_utils.h"
#include "mode_context.h"
#include "process_pipe.h"
#include "terminal.h"
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <sstream>
#include <unordered_set>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace editor::statemachine
{
namespace
{
std::string_view trim(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if(first == std::string_view::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string display(std::string text)
{
    for(char& c : text)
        if(static_cast<unsigned char>(c) < 32 || c == 127)
            c = ' ';
    return text;
}

struct PickerInput
{
    std::string prefix, query;
    bool directRegex = false;
};

std::optional<PickerInput> pickerInput(std::string_view input)
{
    input = trim(input);
    if(input.starts_with(':'))
        input.remove_prefix(1);
    if(!input.starts_with("dbgattach "))
        return {};
    input = trim(input.substr(10));
    std::string prefix = "dbgattach";
    if(input.starts_with("--backend "))
    {
        input = trim(input.substr(10));
        auto end = input.find_first_of(" \t");
        if(end == std::string_view::npos)
            return {};
        const auto backend = input.substr(0, end);
        if(backend != "lldb" && backend != "gdb" && backend != "mladbg")
            return {};
        prefix += " --backend " + std::string(backend);
        input = trim(input.substr(end));
    }
    // Explicit regex queries open the picker without the -- marker. In
    // particular, re: is already a valid empty regex while the user types.
    if(editor::helper::isRegexQuery(input))
        return PickerInput{prefix, std::string(input),
                           input.starts_with("re:")};
    // --query works immediately after opening; --backend remains an option.
    if(!input.starts_with("--") || input == "--backend")
        return {};
    return PickerInput{prefix, std::string(trim(input.substr(2))), false};
}
} // namespace

std::vector<DebuggerProcess> parseDebuggerProcesses(std::string_view listing)
{
    std::vector<DebuggerProcess> result;
    std::unordered_set<int> seen;
    std::istringstream stream{std::string(listing)};
    std::string line;
    while(std::getline(stream, line))
    {
        auto text = trim(line);
        const auto end = text.find_first_of(" \t");
        if(end == std::string_view::npos)
            continue;
        int pid = 0;
        const auto number = text.substr(0, end);
        auto parsed =
            std::from_chars(number.data(), number.data() + number.size(), pid);
        const auto path = trim(text.substr(end));
        if(parsed.ec != std::errc{} ||
           parsed.ptr != number.data() + number.size() || pid <= 0 ||
           path.empty() || !seen.insert(pid).second)
            continue;
        std::string executable = display(std::string(path));
        result.push_back({pid,
                          std::filesystem::path(executable).filename().string(),
                          executable});
    }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.pid < b.pid; });
    return result;
}

DebuggerProcessList listDebuggerProcesses()
{
#ifdef _WIN32
    return {{}, "Process browsing currently requires macOS or Linux"};
#else
    ProcessPipe pipe;
    if(!pipe.open({"ps", "-ww", "-axo", "pid=,comm="}, "r", true))
        return {{}, "Cannot start ps to list processes"};
    const auto listing = pipe.readAllBlocking(8192, {});
    const int status = pipe.closeBlocking({});
    if(status != 0)
        return {{},
                "Cannot list processes: " +
                    display(std::string(trim(listing.substr(0, 200))))};
    auto entries = parseDebuggerProcesses(listing);
    const int self = static_cast<int>(getpid());
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [self](const auto& process)
                                 { return process.pid == self; }),
                  entries.end());
    return {std::move(entries), {}};
#endif
}

bool DebuggerProcessPicker::update(ModeContext& ctx, std::string_view input,
                                   bool refresh)
{
    const auto parsed = pickerInput(input);
    if(!parsed)
    {
        if(active)
            close(ctx);
        return false;
    }
    if(!active || refresh)
        processes = loader();
    active = true;
    commandPrefix = parsed->prefix;
    directRegex = parsed->directRegex;
    const auto& query = parsed->query;
    auto& editor = *ctx.editor;
    editor.commandPopupActive = true;
    editor.commandPopupAll.clear();
    editor.commandPopupFiltered.clear();
    editor.commandPopupQuery = query;
    editor.commandPopupCursor = editor.commandPopupOffset = 0;
    std::vector<std::pair<int, int>> ranked;
    std::vector<int> positions;
    for(int i = 0; i < static_cast<int>(processes.entries.size()); ++i)
    {
        const auto& process = processes.entries[i];
        const auto pid = std::to_string(process.pid);
        auto label =
            pid +
            std::string(std::max(2, 9 - static_cast<int>(pid.size())), ' ') +
            process.name;
        if(process.name != process.executable)
            label += "  " + process.executable;
        editor.commandPopupAll.push_back(label);
        int score = query.empty() ? 0
                                  : editor::helper::fuzzyScoreWithPositions(
                                        query, label, positions);
        if(editor::helper::isRegexQuery(query))
            for(const auto& field : {pid, process.name, process.executable})
                score = std::max(score, editor::helper::fuzzyScoreWithPositions(
                                            query, field, positions));
        if(query == pid)
            score += 100000;
        if(score >= 0)
            ranked.emplace_back(i, score);
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& a, const auto& b)
                     { return a.second > b.second; });
    for(const auto& [index, score] : ranked)
    {
        (void)score;
        editor.commandPopupFiltered.push_back(index);
    }
    editor.commandPopupTitle = "Processes " + std::to_string(ranked.size()) +
                               "/" + std::to_string(processes.entries.size()) +
                               "  Ctrl-J/K select  Enter attach";
    editor.commandPopupEmptyMessage =
        processes.error.empty() ? "No matching processes" : processes.error;
    if(const auto error = editor::helper::regexQueryError(query);
       !error.empty())
        editor.commandPopupEmptyMessage = error;
    ctx.setStatusMessage("Processes: type name/PID or re:regex | Ctrl-J/K: "
                         "select | Enter: attach | Tab: "
                         "insert PID | Ctrl-R: refresh | Esc: close");
    ctx.requestFullRedraw();
    return true;
}

std::optional<std::string>
DebuggerProcessPicker::selection(ModeContext& ctx) const
{
    const auto& editor = *ctx.editor;
    if(editor.commandPopupCursor < 0 ||
       editor.commandPopupCursor >=
           static_cast<int>(editor.commandPopupFiltered.size()))
        return {};
    const int index = editor.commandPopupFiltered[editor.commandPopupCursor];
    if(index < 0 || index >= static_cast<int>(processes.entries.size()))
        return {};
    return commandPrefix + " " + std::to_string(processes.entries[index].pid);
}

void DebuggerProcessPicker::close(ModeContext& ctx)
{
    active = false;
    processes = {};
    ctx.cancelCommandPopup();
    ctx.setStatusMessage("");
    ctx.requestFullRedraw();
}

DebuggerProcessPicker::Action
DebuggerProcessPicker::handle(ModeContext& ctx, int key, std::string& input)
{
    if(!active)
        return Action::Unhandled;
    if(key == keyCode(control::ControlKey::CTRL_J) ||
       key == keyCode(navigation::NavigationKey::ARROW_DOWN))
        ctx.moveCommandPopupCursor(1);
    else if(key == keyCode(control::ControlKey::CTRL_K) ||
            key == keyCode(navigation::NavigationKey::ARROW_UP) ||
            key == keyCode(control::ControlKey::SHIFT_TAB))
        ctx.moveCommandPopupCursor(-1);
    else if(key == keyCode(control::ControlKey::CTRL_R))
        update(ctx, input, true);
    else if(key == keyCode(control::ControlKey::CTRL_U))
    {
        input = std::string(input.starts_with(':') ? ":" : "") + commandPrefix +
                (directRegex ? " re:" : " -- ");
        update(ctx, input);
    }
    else if(key == keyCode(control::ControlKey::ESC))
    {
        input = std::string(input.starts_with(':') ? ":" : "") + commandPrefix +
                " ";
        close(ctx);
    }
    else if(key == keyCode(control::ControlKey::ENTER) ||
            key == keyCode(control::ControlKey::TAB))
    {
        if(auto selected = selection(ctx))
        {
            input = std::string(input.starts_with(':') ? ":" : "") + *selected;
            close(ctx);
            return key == keyCode(control::ControlKey::ENTER) ? Action::Accept
                                                              : Action::Handled;
        }
        ctx.setStatusMessage(
            processes.error.empty()
                ? "No matching process; edit the filter or Ctrl-R to refresh"
                : processes.error);
    }
    else
    {
        if(key == keyCode(control::ControlKey::CTRL_F))
            close(ctx);
        return Action::Unhandled;
    }
    ctx.requestFullRedraw();
    return Action::Handled;
}
} // namespace editor::statemachine
