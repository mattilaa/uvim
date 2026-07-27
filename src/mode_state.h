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
#ifdef UVIM_ENABLE_BROWSER_TOOLS
struct FileBrowserMode;
#endif
#ifdef UVIM_ENABLE_SEARCH_TOOLS
struct FuzzyFindMode;
#endif
#ifdef UVIM_ENABLE_BROWSER_TOOLS
struct BufferBrowserMode;
#endif
#ifdef UVIM_ENABLE_SEARCH_TOOLS
struct GrepSearchMode;
struct RegexSearchMode;
#endif
struct OperatorPendingMode;
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
struct ReferencesMode;
struct LspInfoMode;
struct ToolInfoMode;
struct LocListMode;
struct MlangFormatErrorsMode;
struct HelpMode;
#endif
#ifdef UVIM_ENABLE_GIT_TOOLS
struct GitShowCommitMode;
struct GitLogMode;
struct GitStageMode;
struct GitCommitMode;
struct GitFixupMode;
struct GitPatchMode;
#endif
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
struct CommandOutputMode;
struct GlyphSelectMode;
#endif
#ifdef UVIM_ENABLE_COLOR_TOOLS
struct AnsiToolsMode;
struct ColorPickerMode;
struct ColorSelectorMode;
#endif

using ModeState = std::variant<
    WelcomeMode, NormalMode, InsertMode, ReplaceMode, VisualMode,
    VisualLineMode, VisualBlockMode, CommandMode, SearchForwardMode,
    SearchBackwardMode,
#ifdef UVIM_ENABLE_BROWSER_TOOLS
    FileBrowserMode,
#endif
#ifdef UVIM_ENABLE_SEARCH_TOOLS
    FuzzyFindMode,
#endif
#ifdef UVIM_ENABLE_BROWSER_TOOLS
    BufferBrowserMode,
#endif
#ifdef UVIM_ENABLE_SEARCH_TOOLS
    GrepSearchMode, RegexSearchMode,
#endif
    OperatorPendingMode
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
    ,
    ReferencesMode, LspInfoMode, ToolInfoMode, LocListMode,
    MlangFormatErrorsMode, HelpMode
#endif
#ifdef UVIM_ENABLE_GIT_TOOLS
    ,
    GitShowCommitMode, GitLogMode, GitStageMode, GitCommitMode, GitFixupMode,
    GitPatchMode
#endif
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
    ,
    CommandOutputMode, GlyphSelectMode
#endif
#ifdef UVIM_ENABLE_COLOR_TOOLS
    ,
    AnsiToolsMode, ColorPickerMode, ColorSelectorMode
#endif
    >;

ModeState defaultExitMode(const Editor* editor);
} // namespace editor::statemachine
