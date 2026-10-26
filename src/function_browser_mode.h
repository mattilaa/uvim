#pragma once

#include "mode_context.h"
#include "mode_state.h"
#include "structural_navigation.h"

#include <optional>
#include <string>
#include <vector>

struct Buffer;

namespace editor::statemachine
{
struct FunctionBrowserMode
{
    static constexpr const char* name()
    {
        return "FUNCTIONS";
    }

    std::vector<structural_navigation::FunctionSymbol> symbols;
    std::vector<int> matches;
    std::string query;
    int cursor = 0;
    int offset = 0;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);
    std::optional<ModeState> handle(
        ModeContext& ctx,
        const ModeKeyEvent& event);
    void draw(Editor& editor);

private:
    int sourceIndex = -1;
    const Buffer* sourceBuffer = nullptr;
    std::string sourceName;
    std::string emptyMessage;

    void updateMatches();
    void ensureVisible(const Editor& editor);
};
} // namespace editor::statemachine
