#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editor::statemachine
{
struct ModeContext;

struct DebuggerProcess
{
    int pid = 0;
    std::string name;
    std::string executable;
};

struct DebuggerProcessList
{
    std::vector<DebuggerProcess> entries;
    std::string error;
};

std::vector<DebuggerProcess> parseDebuggerProcesses(std::string_view listing);
DebuggerProcessList listDebuggerProcesses();

struct DebuggerProcessPicker
{
    enum class Action
    {
        Unhandled,
        Handled,
        Accept
    };
    // The loader can also supply recorded process lists for command tests.
    std::function<DebuggerProcessList()> loader = listDebuggerProcesses;
    bool active = false;
    bool update(ModeContext& ctx, std::string_view input, bool refresh = false);
    Action handle(ModeContext& ctx, int key, std::string& input);
    void close(ModeContext& ctx);

private:
    DebuggerProcessList processes;
    std::string commandPrefix;
    std::optional<std::string> selection(ModeContext& ctx) const;
};
} // namespace editor::statemachine
