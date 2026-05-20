#pragma once

#include <optional>
#include <variant>

class Editor;
class Theme;

namespace editor::statemachine
{
struct ModeContext;
struct NormalMode;
struct WelcomeMode;
struct InsertMode;
struct ReplaceMode;
struct VisualMode;
struct VisualLineMode;
struct VisualBlockMode;
struct CommandMode;
struct SearchForwardMode;
struct SearchBackwardMode;
struct FileBrowserMode;
struct FuzzyFindMode;
struct BufferBrowserMode;
struct GrepSearchMode;
struct RegexSearchMode;
struct OperatorPendingMode;
struct ReferencesMode;
struct LspInfoMode;
struct LocListMode;
struct HelpMode;
struct GitShowCommitMode;
struct GitLogMode;
struct GitStageMode;
struct GitCommitMode;
struct GitFixupMode;
struct GitPatchMode;
struct CommandOutputMode;

using ModeState = std::variant<
    WelcomeMode, NormalMode, InsertMode, ReplaceMode, VisualMode,
    VisualLineMode, VisualBlockMode, CommandMode, SearchForwardMode,
    SearchBackwardMode, FileBrowserMode, FuzzyFindMode, BufferBrowserMode,
    GrepSearchMode, RegexSearchMode, OperatorPendingMode, ReferencesMode,
    LspInfoMode, LocListMode, HelpMode, GitShowCommitMode, GitLogMode,
    GitStageMode, GitCommitMode, GitFixupMode, GitPatchMode, CommandOutputMode>;

ModeState defaultExitMode(const Editor* editor);
} // namespace editor::statemachine
