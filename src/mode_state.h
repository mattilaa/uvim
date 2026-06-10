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
#ifndef UVIM_MINIMAL
struct FileBrowserMode;
#endif
#ifdef UVIM_ENABLE_SEARCH_TOOLS
struct FuzzyFindMode;
#endif
#ifndef UVIM_MINIMAL
struct BufferBrowserMode;
#endif
#ifdef UVIM_ENABLE_SEARCH_TOOLS
struct GrepSearchMode;
struct RegexSearchMode;
#endif
struct OperatorPendingMode;
#ifndef UVIM_MINIMAL
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
#ifndef UVIM_MINIMAL
    FileBrowserMode,
#endif
#ifdef UVIM_ENABLE_SEARCH_TOOLS
    FuzzyFindMode,
#endif
#ifndef UVIM_MINIMAL
    BufferBrowserMode,
#endif
#ifdef UVIM_ENABLE_SEARCH_TOOLS
    GrepSearchMode, RegexSearchMode,
#endif
    OperatorPendingMode
#ifndef UVIM_MINIMAL
    ,
    ReferencesMode, LspInfoMode, LocListMode, HelpMode, GitShowCommitMode, GitLogMode,
    GitStageMode, GitCommitMode, GitFixupMode, GitPatchMode, CommandOutputMode,
    GlyphSelectMode
#endif
#ifdef UVIM_ENABLE_COLOR_TOOLS
    ,
    AnsiToolsMode, ColorPickerMode, ColorSelectorMode
#endif
    >;

ModeState defaultExitMode(const Editor* editor);
} // namespace editor::statemachine
