#pragma once

#include "editor_execution_command.h"

#include <memory>
#include <vector>

namespace command::execution
{
class SearchCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class SetCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class ReloadCurrentFileCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;

private:
    static bool matches(std::string_view command, bool& force);
};

class LspInfoCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class ExploreCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;

private:
    static bool parse(std::string_view command, std::string& outPath);
};

class FormatCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class EmitAsmCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class EmojiCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class GlyphSelectCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class ColorPickerCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class AnsiToolsCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class ColorSelectorCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class HelpCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class GitStageCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class RefreshFileSearchCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class GitAddCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class GitBlameCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class GitLogCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class GitPrettyLogCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class GitDiffCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class GitCommitCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class GitFixupCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class GitStashCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class GitStashPopCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class LocTotalCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;

private:
    static bool parse(std::string_view command, std::string& outPath);
};

class LocCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;

private:
    static bool parse(std::string_view command, bool& listView,
                      std::string& outPath);
    static bool run(Editor& editor, bool listView, std::string path);
};

class QuitWithoutBufferCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class NoBufferOnlyCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class RequiresBufferCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class BufferNextCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class BufferPreviousCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class BufferDeleteCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class BufferDeleteForceCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class BufferListCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class BufferSelectCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class NewBufferCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class WriteAllCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class QuitAllCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class WriteQuitAllCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class SplitExplorerCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class WriteCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class SplitCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class QuitCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class WriteQuitCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class WriteAsCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class EditCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class TabNewCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class TabNextCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class TabPreviousCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class PwdCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class CdRootCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class CdCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class LineJumpCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

class UnknownCommand final : public EditorExecutionCommand
{
public:
    bool execute(Editor& editor, const CommandRequest& request) const override;
};

std::vector<std::unique_ptr<EditorExecutionCommand>> buildCommands();
} // namespace command::execution
