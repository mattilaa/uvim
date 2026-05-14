#pragma once

#include "mode_context.h"
#include "mode_state.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct ParsedCommand
{
    std::string cmd;
    std::string args;
};

using ModeCommandCallback =
    std::function<bool(ModeContext& ctx, const ParsedCommand& command,
                       std::optional<ModeState>& nextState)>;

using CommandFallback = std::function<std::optional<ModeState>(
    ModeContext& ctx, std::string_view commandLine)>;

ParsedCommand parseCommandLine(std::string_view commandLine);

std::optional<ModeState>
dispatchCommandLine(ModeContext& ctx, std::string_view commandLine,
                    const ModeCommandCallback& modeHandler,
                    const CommandFallback& fallbackHandler = CommandFallback{});

std::optional<ModeState> dispatchEditorCommand(ModeContext& ctx,
                                               std::string_view commandLine,
                                               std::string_view previousFile,
                                               bool returnToNormalIfBuffer);

struct CommandPrompt
{
    bool isActive() const;
    const std::string& getInput() const;
    void setInput(std::string value);
    bool handle(ModeContext& ctx, int key,
                const std::function<std::optional<ModeState>(std::string_view)>&
                    execute,
                std::optional<ModeState>& nextState);

private:
    bool active = false;
    std::string input;
    std::vector<std::string> completions;
    int completionIndex = -1;
    std::string originalInput;
};
