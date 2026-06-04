#pragma once

#include <cstdio>
#include <string>

namespace color
{
enum class AnsiColor
{
    Reset,
    ResetAll,
    Bold,
    Dim,
    Italic,
    Underline,
    Blink,
    BlinkOff,
    Reverse,
    BoldOff,
    FgBlack,
    FgRed,
    FgGreen,
    FgYellow,
    FgBlue,
    FgMagenta,
    FgCyan,
    FgWhite,
    FgDefault,
    FgBrightBlack,
    FgBrightRed,
    FgBrightGreen,
    FgBrightYellow,
    FgBrightBlue,
    FgBrightMagenta,
    FgBrightCyan,
    FgBrightWhite,
    BgBlack,
    BgRed,
    BgGreen,
    BgYellow,
    BgBlue,
    BgMagenta,
    BgCyan,
    BgWhite,
    BgDefault,
    BgBrightBlack,
    BgBrightRed,
    BgBrightGreen,
    BgBrightYellow,
    BgBrightBlue,
    BgBrightMagenta,
    BgBrightCyan,
    BgBrightWhite,
    StyleSearchMatch,
    StyleSelection,
    StyleGreenBold,
    StyleResetGreenBold,
    StyleCursor,
    StyleEditField,
};

constexpr const char* ansi(AnsiColor color)
{
    switch(color)
    {
    case AnsiColor::Reset:
        return "\x1b[0m";
    case AnsiColor::ResetAll:
        return "\x1b[m";
    case AnsiColor::Bold:
        return "\x1b[1m";
    case AnsiColor::Dim:
        return "\x1b[2m";
    case AnsiColor::Italic:
        return "\x1b[3m";
    case AnsiColor::Underline:
        return "\x1b[4m";
    case AnsiColor::Blink:
        return "\x1b[5m";
    case AnsiColor::BlinkOff:
        return "\x1b[25m";
    case AnsiColor::Reverse:
        return "\x1b[7m";
    case AnsiColor::BoldOff:
        return "\x1b[22m";
    case AnsiColor::FgBlack:
        return "\x1b[30m";
    case AnsiColor::FgRed:
        return "\x1b[31m";
    case AnsiColor::FgGreen:
        return "\x1b[32m";
    case AnsiColor::FgYellow:
        return "\x1b[33m";
    case AnsiColor::FgBlue:
        return "\x1b[34m";
    case AnsiColor::FgMagenta:
        return "\x1b[35m";
    case AnsiColor::FgCyan:
        return "\x1b[36m";
    case AnsiColor::FgWhite:
        return "\x1b[37m";
    case AnsiColor::FgDefault:
        return "\x1b[39m";
    case AnsiColor::FgBrightBlack:
        return "\x1b[90m";
    case AnsiColor::FgBrightRed:
        return "\x1b[91m";
    case AnsiColor::FgBrightGreen:
        return "\x1b[92m";
    case AnsiColor::FgBrightYellow:
        return "\x1b[93m";
    case AnsiColor::FgBrightBlue:
        return "\x1b[94m";
    case AnsiColor::FgBrightMagenta:
        return "\x1b[95m";
    case AnsiColor::FgBrightCyan:
        return "\x1b[96m";
    case AnsiColor::FgBrightWhite:
        return "\x1b[97m";
    case AnsiColor::BgBlack:
        return "\x1b[40m";
    case AnsiColor::BgRed:
        return "\x1b[41m";
    case AnsiColor::BgGreen:
        return "\x1b[42m";
    case AnsiColor::BgYellow:
        return "\x1b[43m";
    case AnsiColor::BgBlue:
        return "\x1b[44m";
    case AnsiColor::BgMagenta:
        return "\x1b[45m";
    case AnsiColor::BgCyan:
        return "\x1b[46m";
    case AnsiColor::BgWhite:
        return "\x1b[47m";
    case AnsiColor::BgDefault:
        return "\x1b[49m";
    case AnsiColor::BgBrightBlack:
        return "\x1b[100m";
    case AnsiColor::BgBrightRed:
        return "\x1b[101m";
    case AnsiColor::BgBrightGreen:
        return "\x1b[102m";
    case AnsiColor::BgBrightYellow:
        return "\x1b[103m";
    case AnsiColor::BgBrightBlue:
        return "\x1b[104m";
    case AnsiColor::BgBrightMagenta:
        return "\x1b[105m";
    case AnsiColor::BgBrightCyan:
        return "\x1b[106m";
    case AnsiColor::BgBrightWhite:
        return "\x1b[107m";
    case AnsiColor::StyleSearchMatch:
        return "\x1b[43m\x1b[30m";
    case AnsiColor::StyleSelection:
        return "\x1b[7m";
    case AnsiColor::StyleGreenBold:
        return "\x1b[32;1m";
    case AnsiColor::StyleResetGreenBold:
        return "\x1b[39;22m";
    case AnsiColor::StyleCursor:
        return "\x1b[103m\x1b[30;1m";
    case AnsiColor::StyleEditField:
        return "\x1b[37;40m";
    }

    return "\x1b[0m";
}

constexpr const char* literal(AnsiColor color)
{
    switch(color)
    {
    case AnsiColor::Reset:
        return "\\x1b[0m";
    case AnsiColor::Bold:
        return "\\x1b[1m";
    case AnsiColor::Italic:
        return "\\x1b[3m";
    case AnsiColor::FgBlack:
        return "\\x1b[30m";
    case AnsiColor::FgRed:
        return "\\x1b[31m";
    case AnsiColor::FgGreen:
        return "\\x1b[32m";
    case AnsiColor::FgYellow:
        return "\\x1b[33m";
    case AnsiColor::FgBlue:
        return "\\x1b[34m";
    case AnsiColor::FgMagenta:
        return "\\x1b[35m";
    case AnsiColor::FgCyan:
        return "\\x1b[36m";
    case AnsiColor::FgWhite:
        return "\\x1b[37m";
    case AnsiColor::FgBrightBlack:
        return "\\x1b[90m";
    case AnsiColor::FgBrightRed:
        return "\\x1b[91m";
    case AnsiColor::FgBrightGreen:
        return "\\x1b[92m";
    case AnsiColor::FgBrightYellow:
        return "\\x1b[93m";
    case AnsiColor::FgBrightBlue:
        return "\\x1b[94m";
    case AnsiColor::FgBrightMagenta:
        return "\\x1b[95m";
    case AnsiColor::FgBrightCyan:
        return "\\x1b[96m";
    case AnsiColor::FgBrightWhite:
        return "\\x1b[97m";
    case AnsiColor::BgBlack:
        return "\\x1b[40m";
    case AnsiColor::BgRed:
        return "\\x1b[41m";
    case AnsiColor::BgGreen:
        return "\\x1b[42m";
    case AnsiColor::BgYellow:
        return "\\x1b[43m";
    case AnsiColor::BgBlue:
        return "\\x1b[44m";
    case AnsiColor::BgMagenta:
        return "\\x1b[45m";
    case AnsiColor::BgCyan:
        return "\\x1b[46m";
    case AnsiColor::BgWhite:
        return "\\x1b[47m";
    case AnsiColor::BgBrightBlack:
        return "\\x1b[100m";
    case AnsiColor::BgBrightRed:
        return "\\x1b[101m";
    case AnsiColor::BgBrightGreen:
        return "\\x1b[102m";
    case AnsiColor::BgBrightYellow:
        return "\\x1b[103m";
    case AnsiColor::BgBrightBlue:
        return "\\x1b[104m";
    case AnsiColor::BgBrightMagenta:
        return "\\x1b[105m";
    case AnsiColor::BgBrightCyan:
        return "\\x1b[106m";
    case AnsiColor::BgBrightWhite:
        return "\\x1b[107m";
    default:
        return "\\x1b[0m";
    }
}

inline std::string rgbFg(int red, int green, int blue)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "\x1b[38;2;%d;%d;%dm", red, green,
                  blue);
    return buffer;
}

inline std::string rgbBg(int red, int green, int blue)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "\x1b[48;2;%d;%d;%dm", red, green,
                  blue);
    return buffer;
}

inline std::string rgbLiteralFg(int red, int green, int blue)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "\\x1b[38;2;%d;%d;%dm", red, green,
                  blue);
    return buffer;
}

inline std::string rgbLiteralBg(int red, int green, int blue)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "\\x1b[48;2;%d;%d;%dm", red, green,
                  blue);
    return buffer;
}
} // namespace color
