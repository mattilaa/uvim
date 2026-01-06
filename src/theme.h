#pragma once

#include "token_type.h"
#include <string>
#include <unordered_map>

class Theme
{
public:
    struct Color
    {
        int r = 0;
        int g = 0;
        int b = 0;
        bool set = false;
    };

    struct Pair
    {
        Color fg;
        Color bg;
        bool bold = false;
        bool italic = false;
        bool underline = false;
    };

    static constexpr int kTokenTypeCount = TOKEN_FUNCTION + 1;

    Theme();

    static Theme defaults();
    static std::string defaultConfigPath();

    bool loadFromFile(const std::string& path, std::string* err = nullptr);

    const std::string& base() const;
    const std::string& baseFg() const;
    const std::string& reset() const;

    const std::string& selection() const;
    const std::string& cursor() const;
    const std::string& searchMatch() const;
    const std::string& statusBar() const;
    const std::string& panel() const;

    const std::string& uiDim() const;
    const std::string& uiAccent() const;
    const std::string& uiInfo() const;
    const std::string& uiWarning() const;
    const std::string& uiSuccess() const;
    const std::string& uiError() const;
    const std::string& uiDirectory() const;
    const std::string& uiGutter() const;
    const std::string& uiPrompt() const;

    const std::string& matchHighlight() const;
    const std::string& syntax(TokenType type) const;

private:
    Color base_fg_;
    Color base_bg_;

    Color ui_dim_;
    Color ui_accent_;
    Color ui_info_;
    Color ui_warning_;
    Color ui_success_;
    Color ui_error_;
    Color ui_directory_;
    Color ui_gutter_;
    Color ui_prompt_;

    Pair status_bar_;
    Pair selection_;
    Pair cursor_;
    Pair search_;
    Pair panel_;

    Color syntax_[kTokenTypeCount]{};

    std::string base_seq_;
    std::string base_fg_seq_;
    std::string reset_seq_;
    std::string selection_seq_;
    std::string cursor_seq_;
    std::string search_seq_;
    std::string status_bar_seq_;
    std::string panel_seq_;
    std::string ui_dim_seq_;
    std::string ui_accent_seq_;
    std::string ui_info_seq_;
    std::string ui_warning_seq_;
    std::string ui_success_seq_;
    std::string ui_error_seq_;
    std::string ui_directory_seq_;
    std::string ui_gutter_seq_;
    std::string ui_prompt_seq_;
    std::string match_highlight_seq_;
    std::string syntax_seq_[kTokenTypeCount]{};

    void rebuildSequences();

    static std::string fgSeq(const Color& c);
    static std::string bgSeq(const Color& c);
    static bool parseHexColor(std::string_view value, Color& out);

    void applyOverrides(
        const std::unordered_map<std::string, std::string>& values);
};
