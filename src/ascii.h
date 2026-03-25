#pragma once

#include <string>
#include <string_view>

namespace ascii
{
using Glyph = std::u8string_view;

inline constexpr Glyph BOX_LIGHT_HORIZONTAL = u8"─";
inline constexpr Glyph BOX_LIGHT_VERTICAL = u8"│";
inline constexpr Glyph BOX_LIGHT_VERTICAL_RIGHT_PAD = u8"│ ";
inline constexpr Glyph BOX_LIGHT_VERTICAL_LEFT_PAD = u8" │";
inline constexpr Glyph BOX_LIGHT_VERTICAL_PADDED = u8" │ ";
inline constexpr Glyph BOX_LIGHT_TOP_LEFT = u8"┌";
inline constexpr Glyph BOX_LIGHT_TOP_RIGHT = u8"┐";
inline constexpr Glyph BOX_LIGHT_BOTTOM_LEFT = u8"└";
inline constexpr Glyph BOX_LIGHT_BOTTOM_RIGHT = u8"┘";

inline constexpr Glyph BOX_ROUNDED_TOP_LEFT = u8"╭";
inline constexpr Glyph BOX_ROUNDED_TOP_RIGHT = u8"╮";
inline constexpr Glyph BOX_ROUNDED_BOTTOM_LEFT = u8"╰";
inline constexpr Glyph BOX_ROUNDED_BOTTOM_RIGHT = u8"╯";

inline constexpr Glyph BOX_HEAVY_VERTICAL = u8"┃";

inline constexpr Glyph DISCLOSURE_RIGHT = u8"▸ ";
inline constexpr Glyph DISCLOSURE_DOWN = u8"▾ ";

inline constexpr Glyph FOLDER_ICON = u8"📁 ";
inline constexpr Glyph FILE_ICON = u8"📄 ";

inline constexpr Glyph RIGHT_ARROW = u8"→";
inline constexpr Glyph RIGHT_ARROW_PADDED = u8" → ";
inline constexpr Glyph UP_DOWN_ARROWS = u8"↑↓";

inline std::string utf8(Glyph glyph)
{
    return std::string(reinterpret_cast<const char*>(glyph.data()),
                       glyph.size());
}

inline void append(std::string& out, Glyph glyph)
{
    out.append(reinterpret_cast<const char*>(glyph.data()), glyph.size());
}
} // namespace ascii
