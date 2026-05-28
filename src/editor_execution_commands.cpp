#include "editor_execution_commands.h"

#include <memory>
#include <vector>

namespace command::execution
{
std::vector<std::unique_ptr<EditorExecutionCommand>> buildCommands()
{
    std::vector<std::unique_ptr<EditorExecutionCommand>> commands;
    commands.push_back(std::make_unique<SearchCommand>());
    commands.push_back(std::make_unique<SetCommand>());
    commands.push_back(std::make_unique<ReloadCurrentFileCommand>());
    commands.push_back(std::make_unique<LspInfoCommand>());
    commands.push_back(std::make_unique<ExploreCommand>());
    commands.push_back(std::make_unique<FormatCommand>());
    commands.push_back(std::make_unique<EmitAsmCommand>());
    commands.push_back(std::make_unique<EmojiCommand>());
    commands.push_back(std::make_unique<HelpCommand>());
#ifdef UVIM_ENABLE_GIT_TOOLS
    commands.push_back(std::make_unique<GitStageCommand>());
#endif
#ifdef UVIM_ENABLE_SEARCH_TOOLS
    commands.push_back(std::make_unique<RefreshFileSearchCommand>());
#endif
#ifdef UVIM_ENABLE_GIT_TOOLS
    commands.push_back(std::make_unique<GitAddCommand>());
    commands.push_back(std::make_unique<GitBlameCommand>());
    commands.push_back(std::make_unique<GitLogCommand>());
    commands.push_back(std::make_unique<GitPrettyLogCommand>());
    commands.push_back(std::make_unique<GitDiffCommand>());
    commands.push_back(std::make_unique<GitCommitCommand>());
    commands.push_back(std::make_unique<GitFixupCommand>());
    commands.push_back(std::make_unique<GitStashPopCommand>());
    commands.push_back(std::make_unique<GitStashCommand>());
#endif
    commands.push_back(std::make_unique<LocTotalCommand>());
    commands.push_back(std::make_unique<LocCommand>());
    commands.push_back(std::make_unique<QuitWithoutBufferCommand>());
    commands.push_back(std::make_unique<NoBufferOnlyCommand>());
    commands.push_back(std::make_unique<SplitExplorerCommand>());
    commands.push_back(std::make_unique<NewBufferCommand>());
    commands.push_back(std::make_unique<EditCommand>());
    commands.push_back(std::make_unique<TabNewCommand>());
    commands.push_back(std::make_unique<PwdCommand>());
    commands.push_back(std::make_unique<CdRootCommand>());
    commands.push_back(std::make_unique<CdCommand>());
    commands.push_back(std::make_unique<RequiresBufferCommand>());
    commands.push_back(std::make_unique<BufferNextCommand>());
    commands.push_back(std::make_unique<BufferPreviousCommand>());
    commands.push_back(std::make_unique<BufferDeleteCommand>());
    commands.push_back(std::make_unique<BufferDeleteForceCommand>());
    commands.push_back(std::make_unique<BufferListCommand>());
    commands.push_back(std::make_unique<BufferSelectCommand>());
    commands.push_back(std::make_unique<WriteAllCommand>());
    commands.push_back(std::make_unique<QuitAllCommand>());
    commands.push_back(std::make_unique<WriteQuitAllCommand>());
    commands.push_back(std::make_unique<WriteCommand>());
    commands.push_back(std::make_unique<SplitCommand>());
    commands.push_back(std::make_unique<QuitCommand>());
    commands.push_back(std::make_unique<WriteQuitCommand>());
    commands.push_back(std::make_unique<WriteAsCommand>());
    commands.push_back(std::make_unique<TabNextCommand>());
    commands.push_back(std::make_unique<TabPreviousCommand>());
    commands.push_back(std::make_unique<LineJumpCommand>());
    commands.push_back(std::make_unique<UnknownCommand>());
    return commands;
}
} // namespace command::execution
