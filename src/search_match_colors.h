#pragma once

#include <string>

namespace editor
{
/// Shared result-row palette for fuzzy-find and grep views.
class SearchMatchColors
{
public:
    static std::string selectedRowBackground();
    static std::string markedRowBackground();
    static double relevanceStrength(double coverage, double secondaryQuality);
    static std::string matchBackground(double strength, int contrast,
                                       bool marked);
};
} // namespace editor
