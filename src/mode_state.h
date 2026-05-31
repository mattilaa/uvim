#pragma once

#include "mode_events.h"

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
#ifdef UVIM_ENABLE_COLOR_TOOLS
struct ColorPickerMode;
struct ColorSelectorMode;
#endif

using ModeState = std::variant<
    WelcomeMode, NormalMode, InsertMode, ReplaceMode, VisualMode,
    VisualLineMode, VisualBlockMode, CommandMode, SearchForwardMode,
    SearchBackwardMode, FileBrowserMode, FuzzyFindMode, BufferBrowserMode,
    GrepSearchMode, RegexSearchMode, OperatorPendingMode, ReferencesMode,
    LspInfoMode, LocListMode, HelpMode, GitShowCommitMode, GitLogMode,
    GitStageMode, GitCommitMode, GitFixupMode, GitPatchMode, CommandOutputMode
#ifdef UVIM_ENABLE_COLOR_TOOLS
    ,
    ColorPickerMode, ColorSelectorMode
#endif
    >;

ModeState defaultExitMode(const Editor* editor);
} // namespace editor::statemachine
