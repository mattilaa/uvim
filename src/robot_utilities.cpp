#include "robot_utilities.h"
#include "editor_utils.h"

#include <string_view>

std::unordered_set<std::string> RobotUtilities::defaultKeywords()
{
    static constexpr std::string_view kKeywords[] = {
        "if",       "else",     "end",       "for",           "while",
        "try",      "except",   "finally",   "return",        "break",
        "continue", "skip",     "fail",      "run",           "keyword",
        "library",  "resource", "variables", "documentation", "tags",
        "metadata", "setup",    "teardown",  "suite",         "test",
        "task",     "template", "timeout",   "default",       "force",
    };
    std::unordered_set<std::string> out;
    out.reserve(std::size(kKeywords));
    for(auto kw : kKeywords)
        out.insert(editor::helper::ascii_lower(kw));
    return out;
}

std::unordered_set<std::string> RobotUtilities::defaultCustomKeywords()
{
    return {};
}

std::unordered_set<std::string> RobotUtilities::defaultSettings()
{
    static constexpr std::string_view kSettings[] = {
        "resource",      "library",      "variables",      "documentation",
        "metadata",      "suite setup",  "suite teardown", "test setup",
        "test teardown", "task setup",   "task teardown",  "test template",
        "task template", "test timeout", "task timeout",   "force tags",
        "default tags",
    };
    std::unordered_set<std::string> out;
    out.reserve(std::size(kSettings));
    for(auto setting : kSettings)
        out.insert(editor::helper::ascii_lower(setting));
    return out;
}
