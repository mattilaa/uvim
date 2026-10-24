#pragma once
#include "task_queue.h"
#include <chrono>
#include <memory>
#include <string>
#include <string_view>

class Editor;

namespace assembly_instruction_help
{
struct State
{
    bool active = true;
    bool docsLoading = false, docsIndicator = false;
    size_t docsGeneration = 0;
    std::string docsTitle, docsText;
    std::chrono::steady_clock::time_point docsStarted{};
};

// The caller invalidates docsGeneration and cancels its old task before
// loading.
TaskQueue::TaskId load(Editor& editor, const std::shared_ptr<State>& state,
                       const std::string& instruction);
void draw(std::string& out, Editor& editor, const State& state, int& offset,
          std::string_view controls = "i: close | Ctrl-J/K: scroll");
} // namespace assembly_instruction_help
