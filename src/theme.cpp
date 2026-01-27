#include "theme.h"
#include "terminal.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace
{
static std::string trim(const std::string& s)
{
    size_t start = 0;
    while(start < s.size() && std::isspace((unsigned char)s[start]))
        ++start;
    size_t end = s.size();
    while(end > start && std::isspace((unsigned char)s[end - 1]))
        --end;
    return s.substr(start, end - start);
}

static std::unordered_map<std::string, std::string>
parseYamlMap(const std::string& input)
{
    std::unordered_map<std::string, std::string> out;
    std::vector<std::pair<int, std::string>> stack;

    std::istringstream stream(input);
    std::string line;
    while(std::getline(stream, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();

        size_t first = 0;
        while(first < line.size() &&
              (line[first] == ' ' || line[first] == '\t'))
        {
            ++first;
        }

        if(first >= line.size())
            continue;
        if(line[first] == '#')
            continue;

        int indent = 0;
        for(size_t i = 0; i < first; ++i)
        {
            indent += (line[i] == '\t') ? 4 : 1;
        }

        std::string rest = line.substr(first);
        size_t colon = rest.find(':');
        if(colon == std::string::npos)
            continue;

        std::string key = trim(rest.substr(0, colon));
        std::string value = trim(rest.substr(colon + 1));

        if(value.empty())
        {
            while(!stack.empty() && indent <= stack.back().first)
                stack.pop_back();
            stack.emplace_back(indent, key);
            continue;
        }

        if(!value.empty() && value[0] != '#')
        {
            for(size_t i = 0; i < value.size(); ++i)
            {
                if(value[i] == '#' &&
                   (i == 0 || std::isspace((unsigned char)value[i - 1])))
                {
                    value = trim(value.substr(0, i));
                    break;
                }
            }
        }

        if(value.size() >= 2)
        {
            char q = value.front();
            if((q == '"' || q == '\'') && value.back() == q)
            {
                value = value.substr(1, value.size() - 2);
            }
        }

        while(!stack.empty() && indent <= stack.back().first)
            stack.pop_back();

        std::string full;
        for(const auto& part : stack)
        {
            if(!full.empty())
                full += '.';
            full += part.second;
        }
        if(!full.empty())
            full += '.';
        full += key;

        out[full] = value;
    }

    return out;
}
} // namespace

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
    t.syntax_[TOKEN_FUNCTION] = {0x7A, 0xA2, 0xF7, true};
    t.syntax_[TOKEN_MEMBER] = t.syntax_[TOKEN_FUNCTION];

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

    return base + "/uvim/config.yaml";
}

bool Theme::loadFromFile(const std::string& path, std::string* err)
{
    if(path.empty())
        return false;

    std::ifstream in(path);
    if(!in.is_open())
        return false;

    std::ostringstream buf;
    buf << in.rdbuf();
    auto values = parseYamlMap(buf.str());
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

void Theme::rebuildSequences()
{
    base_fg_seq_ = fgSeq(base_fg_);
    base_seq_ = fgSeq(base_fg_) + bgSeq(base_bg_);
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

    for(int i = 0; i < kTokenTypeCount; ++i)
        syntax_seq_[i] = fgSeq(syntax_[i]);
}

std::string Theme::fgSeq(const Color& c)
{
    if(!c.set)
        return "";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\x1b[38;2;%d;%d;%dm", c.r, c.g, c.b);
    return std::string(buf);
}

std::string Theme::bgSeq(const Color& c)
{
    if(!c.set)
        return "";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\x1b[48;2;%d;%d;%dm", c.r, c.g, c.b);
    return std::string(buf);
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
}
