#pragma once

#include "file_entry.h"
#include "mode.h"
#include "mode_commands.h"
#include "mode_context.h"
#include "mode_state.h"

#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace editor::statemachine
{
struct RegexSearchMatch
{
    std::string filename;
    std::string filepath;
    int lineNumber = 0;
    std::string lineContent;
    std::string matchText;
    std::vector<std::pair<int, int>> highlightRanges;
};

struct RegexSearchMode
{
    static constexpr const char* name()
    {
        return "REGEX";
    }

    std::vector<RegexSearchMatch> matches;
    std::string query;
    std::string regexError;
    int cursor = 0;
    int offset = 0;
    bool allFiles = false;
    bool searching = false;

    void on_enter(ModeContext& ctx);
    void on_exit(ModeContext& ctx);

    std::optional<ModeState> handle(ModeContext& ctx,
                                    const ModeKeyEvent& event);

    void draw(Editor& editor) const;
    void refreshFileIndex(Editor& editor);

private:
    void loadFileIndex(Editor& editor);
    void initialize(Editor& editor);
    void performSearch(Editor& editor);
    void searchCurrentBuffer(Editor& editor, const std::regex& pattern);
    void searchInFile(const std::string& filepath, const std::regex& pattern);
    bool isTextFile(const std::string& filepath) const;
    bool isBinaryFile(const std::string& filepath) const;
    void addLineMatches(const std::string& filepath, int lineNumber,
                        const std::string& line, const std::regex& pattern);
    void moveDown(Editor& editor);
    void moveUp(Editor& editor);
    void halfPageDown(Editor& editor);
    void halfPageUp(Editor& editor);
    void addChar(Editor& editor, char c);
    void backspace(Editor& editor);
    void deleteWord(Editor& editor);
    void clearQuery();
    void toggleScope(Editor& editor);
    bool select(Editor& editor);
};
} // namespace editor::statemachine
