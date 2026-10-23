#include "theme.h"
#include "color_constant.h"
#include "config_parser.h"
#include "terminal.h"
#include "text_utils.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

Theme::Theme() = default;

Theme Theme::defaults()
{
    Theme t;

    t.base_fg_ = {0xE0, 0xDE, 0xF4, true};
    t.base_bg_ = {0x1F, 0x1D, 0x2E, true};

    t.ui_dim_ = {0x6E, 0x6A, 0x86, true};
    t.ui_accent_ = {0xC4, 0xA7, 0xE7, true};
    t.ui_info_ = {0x9C, 0xCF, 0xD8, true};
    t.ui_warning_ = {0xF6, 0xC1, 0x77, true};
    t.ui_success_ = {0x9E, 0xCE, 0x6A, true};
    t.ui_error_ = {0xEB, 0x6F, 0x92, true};
    t.ui_directory_ = {0x3E, 0x8F, 0xB0, true};
    t.ui_gutter_ = {0x3E, 0x8F, 0xB0, true};
    t.ui_prompt_ = {0x9E, 0xCE, 0x6A, true};

    t.status_bar_.fg = t.base_fg_;
    t.status_bar_.bg = {0x26, 0x23, 0x3A, true};

    t.tab_bar_.fg = t.base_fg_;
    t.tab_bar_.bg = {0x2A, 0x27, 0x3F, true};

    t.selection_.fg = t.base_fg_;
    t.selection_.bg = {0x40, 0x3D, 0x52, true};

    t.cursor_.fg = {0x1F, 0x1D, 0x2E, true};
    t.cursor_.bg = {0xF6, 0xC1, 0x77, true};
    t.cursor_.bold = true;

    t.search_.fg = {0x1F, 0x1D, 0x2E, true};
    t.search_.bg = {0xEB, 0xBC, 0xBA, true};

    t.panel_.fg = t.base_fg_;
    t.panel_.bg = {0x2A, 0x28, 0x3E, true};
    t.panel_.bold = true;

    t.syntax_[TOKEN_NORMAL] = t.base_fg_;
    t.syntax_[TOKEN_KEYWORD] = {0xC4, 0xA7, 0xE7, true};
    t.syntax_[TOKEN_TYPE] = {0x9C, 0xCF, 0xD8, true};
    t.syntax_[TOKEN_STRING] = {0x9E, 0xCE, 0x6A, true};
    t.syntax_[TOKEN_CHAR] = {0x9E, 0xCE, 0x6A, true};
    t.syntax_[TOKEN_COMMENT] = {0x6E, 0x6A, 0x86, true};
    t.syntax_[TOKEN_PREPROCESSOR] = {0xF6, 0xC1, 0x77, true};
    t.syntax_[TOKEN_NUMBER] = {0xEB, 0x6F, 0x92, true};
    t.syntax_[TOKEN_OPERATOR] = {0xF6, 0xC1, 0x77, true};
    t.syntax_[TOKEN_FUNCTION] = {0x6B, 0x86, 0xC9, true};
    t.syntax_[TOKEN_MEMBER] = {0x89, 0xDC, 0xEB, true};
    t.syntax_[TOKEN_NAMESPACE_1] = {0x9B, 0x7F, 0xBF, true};
    t.syntax_[TOKEN_NAMESPACE_2] = {0xAA, 0x8D, 0xCE, true};
    t.syntax_[TOKEN_NAMESPACE_3] = {0xB7, 0x9A, 0xDC, true};
    t.syntax_[TOKEN_NAMESPACE_4] = t.syntax_[TOKEN_KEYWORD];

    t.rebuildSequences();
    return t;
}

std::string Theme::defaultConfigPath()
{
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    std::string base;
    if(xdg && *xdg)
        base = xdg;
    else if(home && *home)
        base = std::string(home) + "/.config";
    else
        return "";

    return base + "/uvim/config.toml";
}

bool Theme::loadFromFile(const std::string& path, std::string*)
{
    if(path.empty())
        return false;

    std::ifstream in(path);
    if(!in.is_open())
        return false;

    std::ostringstream buf;
    buf << in.rdbuf();
    auto values = editor::config::parseTomlMap(buf.str());
    applyOverrides(values);
    rebuildSequences();
    return true;
}

const std::string& Theme::base() const
{
    return base_seq_;
}

const std::string& Theme::baseFg() const
{
    return base_fg_seq_;
}

const std::string& Theme::reset() const
{
    return reset_seq_;
}

const std::string& Theme::selection() const
{
    return selection_seq_;
}

#ifdef UVIM_ENABLE_DEBUGGER
const std::string& Theme::debuggerRow(DebuggerRow row, bool alternate) const
{
    return debugger_row_seq_[static_cast<size_t>(row)][alternate ? 1 : 0];
}
#endif

const std::string& Theme::cursor() const
{
    return cursor_seq_;
}

const std::string& Theme::searchMatch() const
{
    return search_seq_;
}

const std::string& Theme::statusBar() const
{
    return status_bar_seq_;
}

const std::string& Theme::tabBar() const
{
    return tab_bar_seq_;
}

const std::string& Theme::panel() const
{
    return panel_seq_;
}

const std::string& Theme::uiDim() const
{
    return ui_dim_seq_;
}

const std::string& Theme::uiAccent() const
{
    return ui_accent_seq_;
}

const std::string& Theme::uiInfo() const
{
    return ui_info_seq_;
}

const std::string& Theme::progressFill() const
{
    return progress_fill_seq_;
}

const std::string& Theme::progressTrack() const
{
    return progress_track_seq_;
}

const std::string& Theme::uiWarning() const
{
    return ui_warning_seq_;
}

const std::string& Theme::uiSuccess() const
{
    return ui_success_seq_;
}

const std::string& Theme::uiError() const
{
    return ui_error_seq_;
}

const std::string& Theme::uiDirectory() const
{
    return ui_directory_seq_;
}

const std::string& Theme::uiGutter() const
{
    return ui_gutter_seq_;
}

const std::string& Theme::uiPrompt() const
{
    return ui_prompt_seq_;
}

const std::string& Theme::matchHighlight() const
{
    return match_highlight_seq_;
}

const std::string& Theme::syntax(TokenType type) const
{
    int idx = static_cast<int>(type);
    if(idx < 0 || idx >= kTokenTypeCount)
        return base_fg_seq_;
    return syntax_seq_[idx];
}

std::string Theme::dimmedFg(const Color& foreground, int level) const
{
    if(base_fg_seq_.empty())
        return {};
    constexpr int visibility[] = {100, 78, 58, 38};
    const int weight = visibility[std::clamp(level, 0, 3)];
    const Color& from = foreground.set ? foreground : base_fg_;
    const Color background = base_bg_.set ? base_bg_ : Color{0, 0, 0, true};
    auto blend = [weight](int fg, int bg)
    { return bg + (fg - bg) * weight / 100; };
    return fgSeq({blend(from.r, background.r), blend(from.g, background.g),
                  blend(from.b, background.b), true});
}

std::string Theme::dimmedSyntax(TokenType type, int level) const
{
    const int index = static_cast<int>(type);
    return dimmedFg(index >= 0 && index < kTokenTypeCount ? syntax_[index]
                                                          : base_fg_,
                    level);
}

std::string Theme::dimmedGutter(int level) const
{
    return dimmedFg(ui_gutter_, level);
}

void Theme::rebuildSequences()
{
#ifndef UVIM_ENABLE_TERMINAL_COLORS
    base_fg_seq_.clear();
    base_seq_.clear();
    reset_seq_.clear();
    selection_seq_.clear();
    cursor_seq_.clear();
    search_seq_.clear();
    source_mapping_seq_.clear();
    external_source_seq_.clear();
    assembly_selection_seq_.clear();
#ifdef UVIM_ENABLE_DEBUGGER
    for(auto& row : debugger_row_seq_)
        for(auto& seq : row)
            seq.clear();
#endif
    status_bar_seq_.clear();
    tab_bar_seq_.clear();
    panel_seq_.clear();
    progress_fill_seq_.clear();
    progress_track_seq_.clear();
    ui_dim_seq_.clear();
    ui_accent_seq_.clear();
    ui_info_seq_.clear();
    ui_warning_seq_.clear();
    ui_success_seq_.clear();
    ui_error_seq_.clear();
    ui_directory_seq_.clear();
    ui_gutter_seq_.clear();
    ui_prompt_seq_.clear();
    match_highlight_seq_.clear();
    for(auto& seq : syntax_seq_)
        seq.clear();
    return;
#endif

    base_fg_seq_ = fgSeq(base_fg_);
    base_seq_ = fgSeq(base_fg_) + bgSeq(base_bg_);
    const Color green{80, 180, 90, true};
    const Color background = base_bg_.set ? base_bg_ : Color{31, 29, 46, true};
    source_mapping_seq_ = bgSeq({(background.r * 3 + green.r) / 4,
                                 (background.g * 3 + green.g) / 4,
                                 (background.b * 3 + green.b) / 4, true});
    const Color blue{70, 125, 205, true};
#ifdef UVIM_ENABLE_DEBUGGER
    const std::array<Color, 4> debuggerTints{
        blue, ui_success_, ui_accent_, ui_success_};
    auto blendBackground = [](const Color& base, const Color& tint, int weight)
    {
        return Color{(base.r * (8 - weight) + tint.r * weight) / 8,
                     (base.g * (8 - weight) + tint.g * weight) / 8,
                     (base.b * (8 - weight) + tint.b * weight) / 8, true};
    };
    for(size_t row = 0; row < debugger_row_seq_.size(); ++row)
    {
        const auto normal =
            debugger_bg_[row].set
                ? debugger_bg_[row]
                : blendBackground(background, debuggerTints[row],
                                  row == 3 ? 3 : 1);
        debugger_row_seq_[row][0] = bgSeq(normal);
        debugger_row_seq_[row][1] =
            bgSeq(blendBackground(normal, debuggerTints[row], 1));
    }
#endif
    assembly_selection_seq_ = bgSeq(
        asm_selection_bg_.set ? asm_selection_bg_
                              : Color{(background.r * 3 + blue.r) / 4,
                                      (background.g * 3 + blue.g) / 4,
                                      (background.b * 3 + blue.b) / 4, true});
    const Color red{160, 45, 45, true};
    external_source_seq_ = bgSeq(
        asm_external_bg_.set ? asm_external_bg_
                             : Color{(background.r * 3 + red.r) / 4,
                                     (background.g * 3 + red.g) / 4,
                                     (background.b * 3 + red.b) / 4, true});
    reset_seq_ = std::string(Terminal::ESC_RESET_ALL) + base_seq_;

    selection_seq_ = fgSeq(selection_.fg) + bgSeq(selection_.bg);
    cursor_seq_ = fgSeq(cursor_.fg) + bgSeq(cursor_.bg) +
                  (cursor_.bold ? Terminal::ESC_BOLD : "");
    search_seq_ = fgSeq(search_.fg) + bgSeq(search_.bg);
    status_bar_seq_ = fgSeq(status_bar_.fg) + bgSeq(status_bar_.bg);
    tab_bar_seq_ = fgSeq(tab_bar_.fg) + bgSeq(tab_bar_.bg);
    panel_seq_ = fgSeq(panel_.fg) + bgSeq(panel_.bg) +
                 (panel_.bold ? Terminal::ESC_BOLD : "");

    ui_dim_seq_ = fgSeq(ui_dim_);
    ui_accent_seq_ = fgSeq(ui_accent_);
    ui_info_seq_ = fgSeq(ui_info_);
    ui_warning_seq_ = fgSeq(ui_warning_);
    ui_success_seq_ = fgSeq(ui_success_);
    ui_error_seq_ = fgSeq(ui_error_);
    ui_directory_seq_ = fgSeq(ui_directory_);
    ui_gutter_seq_ = fgSeq(ui_gutter_);
    ui_prompt_seq_ = fgSeq(ui_prompt_);

    match_highlight_seq_ = fgSeq(ui_accent_) + Terminal::ESC_BOLD;

    const Color fill = progress_.fg.set ? progress_.fg : ui_success_;
    Color track = progress_.bg;
    if(!track.set)
    {
        // A step between the status line and the dim UI colour so the empty
        // part of the bar stays visible on any theme.
        const Color& from = status_bar_.bg.set ? status_bar_.bg : base_bg_;
        track = from;
        if(from.set && ui_dim_.set)
        {
            auto mix = [](int a, int b) { return a + ((b - a) * 2) / 5; };
            track = {mix(from.r, ui_dim_.r), mix(from.g, ui_dim_.g),
                     mix(from.b, ui_dim_.b), true};
        }
    }
    progress_fill_seq_ = fgSeq(fill) + bgSeq(track);
    progress_track_seq_ = fgSeq(ui_dim_) + bgSeq(track);

    for(int i = 0; i < kTokenTypeCount; ++i)
        syntax_seq_[i] = fgSeq(syntax_[i]);
}

const std::string& Theme::sourceMapping() const
{
    return source_mapping_seq_;
}

const std::string& Theme::assemblySelection() const
{
    return assembly_selection_seq_;
}

const std::string& Theme::externalSource() const
{
    return external_source_seq_;
}

std::string Theme::fgSeq(const Color& c)
{
    if(!c.set)
        return "";
    return color::rgbFg(c.r, c.g, c.b);
}

std::string Theme::bgSeq(const Color& c)
{
    if(!c.set)
        return "";
    return color::rgbBg(c.r, c.g, c.b);
}

bool Theme::parseHexColor(std::string_view value, Color& out)
{
    if(value.size() >= 2 && value[0] == '#')
        value.remove_prefix(1);

    if(value.size() == 3)
    {
        auto hex = [](char c) -> int
        {
            if(c >= '0' && c <= '9')
                return c - '0';
            if(c >= 'a' && c <= 'f')
                return 10 + (c - 'a');
            if(c >= 'A' && c <= 'F')
                return 10 + (c - 'A');
            return -1;
        };
        int r = hex(value[0]);
        int g = hex(value[1]);
        int b = hex(value[2]);
        if(r < 0 || g < 0 || b < 0)
            return false;
        out = {r * 17, g * 17, b * 17, true};
        return true;
    }

    if(value.size() != 6)
        return false;

    auto hex2 = [](char hi, char lo) -> int
    {
        auto hex = [](char c) -> int
        {
            if(c >= '0' && c <= '9')
                return c - '0';
            if(c >= 'a' && c <= 'f')
                return 10 + (c - 'a');
            if(c >= 'A' && c <= 'F')
                return 10 + (c - 'A');
            return -1;
        };
        int a = hex(hi);
        int b = hex(lo);
        if(a < 0 || b < 0)
            return -1;
        return (a << 4) | b;
    };

    int r = hex2(value[0], value[1]);
    int g = hex2(value[2], value[3]);
    int b = hex2(value[4], value[5]);
    if(r < 0 || g < 0 || b < 0)
        return false;
    out = {r, g, b, true};
    return true;
}

void Theme::applyOverrides(
    const std::unordered_map<std::string, std::string>& values)
{
    auto set_color = [&](const std::string& key, Color& dst)
    {
        auto it = values.find(key);
        if(it == values.end())
            return;
        Color c;
        if(parseHexColor(it->second, c))
            dst = c;
    };

    set_color("theme.base.fg", base_fg_);
    set_color("theme.base.foreground", base_fg_);
    set_color("theme.base.bg", base_bg_);
    set_color("theme.base.background", base_bg_);

    set_color("theme.ui.dim", ui_dim_);
    set_color("theme.ui.accent", ui_accent_);
    set_color("theme.ui.info", ui_info_);
    set_color("theme.ui.warning", ui_warning_);
    set_color("theme.ui.success", ui_success_);
    set_color("theme.ui.error", ui_error_);
    set_color("theme.asm.external_bg", asm_external_bg_);
    set_color("theme.asm.selection_bg", asm_selection_bg_);
#ifdef UVIM_ENABLE_DEBUGGER
    set_color("theme.debugger.source_bg", debugger_bg_[0]);
    set_color("theme.debugger.variables_bg", debugger_bg_[1]);
    set_color("theme.debugger.threads_bg", debugger_bg_[2]);
    set_color("theme.debugger.execution_bg", debugger_bg_[3]);
#endif
    set_color("theme.ui.directory", ui_directory_);
    set_color("theme.ui.gutter", ui_gutter_);
    set_color("theme.ui.prompt", ui_prompt_);

    set_color("theme.statusline.fg", status_bar_.fg);
    set_color("theme.statusline.bg", status_bar_.bg);
    set_color("theme.tabbar.fg", tab_bar_.fg);
    set_color("theme.tabbar.bg", tab_bar_.bg);
    set_color("theme.selection.fg", selection_.fg);
    set_color("theme.selection.bg", selection_.bg);
    set_color("theme.cursor.fg", cursor_.fg);
    set_color("theme.cursor.bg", cursor_.bg);
    set_color("theme.search.fg", search_.fg);
    set_color("theme.search.bg", search_.bg);
    set_color("theme.panel.fg", panel_.fg);
    set_color("theme.progress.fg", progress_.fg);
    set_color("theme.progress.bg", progress_.bg);
    set_color("theme.panel.bg", panel_.bg);

    set_color("theme.syntax.normal", syntax_[TOKEN_NORMAL]);
    set_color("theme.syntax.keyword", syntax_[TOKEN_KEYWORD]);
    set_color("theme.syntax.type", syntax_[TOKEN_TYPE]);
    set_color("theme.syntax.string", syntax_[TOKEN_STRING]);
    set_color("theme.syntax.char", syntax_[TOKEN_CHAR]);
    set_color("theme.syntax.comment", syntax_[TOKEN_COMMENT]);
    set_color("theme.syntax.preprocessor", syntax_[TOKEN_PREPROCESSOR]);
    set_color("theme.syntax.number", syntax_[TOKEN_NUMBER]);
    set_color("theme.syntax.operator", syntax_[TOKEN_OPERATOR]);
    set_color("theme.syntax.function", syntax_[TOKEN_FUNCTION]);
    set_color("theme.syntax.member", syntax_[TOKEN_MEMBER]);
    set_color("theme.syntax.namespace1", syntax_[TOKEN_NAMESPACE_1]);
    set_color("theme.syntax.namespace2", syntax_[TOKEN_NAMESPACE_2]);
    set_color("theme.syntax.namespace3", syntax_[TOKEN_NAMESPACE_3]);
    set_color("theme.syntax.namespace4", syntax_[TOKEN_NAMESPACE_4]);
    set_color("theme.syntax.namespace_1", syntax_[TOKEN_NAMESPACE_1]);
    set_color("theme.syntax.namespace_2", syntax_[TOKEN_NAMESPACE_2]);
    set_color("theme.syntax.namespace_3", syntax_[TOKEN_NAMESPACE_3]);
    set_color("theme.syntax.namespace_4", syntax_[TOKEN_NAMESPACE_4]);
}
