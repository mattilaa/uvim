#pragma once

#include <string>
#include <unordered_set>

class RobotUtilities
{
public:
    static std::unordered_set<std::string> defaultKeywords();
    static std::unordered_set<std::string> defaultCustomKeywords();
    static std::unordered_set<std::string> defaultSettings();
};
