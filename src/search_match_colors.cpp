#include "search_match_colors.h"

#include "color_constant.h"

#include <algorithm>
#include <cmath>

namespace editor
{
std::string SearchMatchColors::selectedRowBackground()
{
    return color::rgbBg(56, 120, 72);
}

std::string SearchMatchColors::markedRowBackground()
{
    return color::rgbBg(24, 64, 36);
}

double SearchMatchColors::relevanceStrength(double coverage,
                                            double secondaryQuality)
{
    return std::clamp(0.15 + std::clamp(coverage, 0.0, 1.0) * 0.65 +
                          std::clamp(secondaryQuality, 0.0, 1.0) * 0.20,
                      0.0, 1.0);
}

std::string SearchMatchColors::matchBackground(double strength,
                                               int contrast, bool marked)
{
    strength = std::clamp(strength, 0.0, 1.0);
    const double amount = std::clamp(contrast, 0, 100) / 100.0;
    strength = std::pow(strength, 1.0 + amount);

    const auto blend = [amount](double low, double high)
    { return low + (high - low) * amount; };
    int red = static_cast<int>(blend(7.0, 1.0) +
                               blend(13.0, 19.0) * strength);
    int green = static_cast<int>(blend(28.0, 10.0) +
                                 blend(38.0, 58.0) * strength);
    int blue = static_cast<int>(blend(10.0, 3.0) +
                                blend(17.0, 21.0) * strength);
    if(marked)
    {
        red += 3;
        green += 7;
        blue += 3;
    }
    return color::rgbBg(red, green, blue);
}
} // namespace editor
