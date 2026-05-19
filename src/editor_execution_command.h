#pragma once

#include <string>
#include <string_view>

class Editor;

namespace command::execution
{
struct CommandRequest
{
    std::string text;
    std::string_view trimmed;
};

class EditorExecutionCommand
{
public:
    virtual ~EditorExecutionCommand() = default;
    virtual bool execute(Editor& editor,
                         const CommandRequest& request) const = 0;
};
} // namespace command::execution
