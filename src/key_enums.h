#pragma once

#include <type_traits>

namespace control
{
enum class ControlKey : int
{
    BACKSPACE = 8,
    TAB = 9,
    ENTER = 13,
    ESC = 27,
    SPACE = 32,
    DEL = 127,

    CTRL_A = 1,
    CTRL_B = 2,
    CTRL_C = 3,
    CTRL_D = 4,
    CTRL_E = 5,
    CTRL_F = 6,
    CTRL_G = 7,
    CTRL_H = 8,
    CTRL_I = 9,
    CTRL_J = 10,
    CTRL_K = 11,
    CTRL_L = 12,
    CTRL_M = 13,
    CTRL_N = 14,
    CTRL_O = 15,
    CTRL_P = 16,
    CTRL_Q = 17,
    CTRL_R = 18,
    CTRL_S = 19,
    CTRL_T = 20,
    CTRL_U = 21,
    CTRL_V = 22,
    CTRL_W = 23,
    CTRL_X = 24,
    CTRL_Y = 25,
    CTRL_Z = 26,

    SHIFT_TAB = 1100,
    SHIFT_CTRL_H = 1102,
    SHIFT_CTRL_L = 1103,
    SHIFT_CTRL_X = 1104,
};
} // namespace control

namespace navigation
{
enum class NavigationKey : int
{
    ARROW_UP = 1000,
    ARROW_DOWN = 1001,
    ARROW_LEFT = 1002,
    ARROW_RIGHT = 1003,
    PAGE_UP = 1004,
    PAGE_DOWN = 1005,
    HOME = 1006,
    END = 1007,
    DELETE_KEY = 1008,
};
} // namespace navigation

namespace typed
{
enum class TypedKey : int
{
    KEY_0 = 48,
    KEY_1 = 49,
    KEY_2 = 50,
    KEY_3 = 51,
    KEY_4 = 52,
    KEY_5 = 53,
    KEY_6 = 54,
    KEY_7 = 55,
    KEY_8 = 56,
    KEY_9 = 57,

    KEY_CAP_A = 65,
    KEY_CAP_B = 66,
    KEY_CAP_C = 67,
    KEY_CAP_D = 68,
    KEY_CAP_E = 69,
    KEY_CAP_F = 70,
    KEY_CAP_G = 71,
    KEY_CAP_H = 72,
    KEY_CAP_I = 73,
    KEY_CAP_J = 74,
    KEY_CAP_K = 75,
    KEY_CAP_L = 76,
    KEY_CAP_M = 77,
    KEY_CAP_N = 78,
    KEY_CAP_O = 79,
    KEY_CAP_P = 80,
    KEY_CAP_Q = 81,
    KEY_CAP_R = 82,
    KEY_CAP_S = 83,
    KEY_CAP_T = 84,
    KEY_CAP_U = 85,
    KEY_CAP_V = 86,
    KEY_CAP_W = 87,
    KEY_CAP_X = 88,
    KEY_CAP_Y = 89,
    KEY_CAP_Z = 90,

    KEY_A = 97,
    KEY_B = 98,
    KEY_C = 99,
    KEY_D = 100,
    KEY_E = 101,
    KEY_F = 102,
    KEY_G = 103,
    KEY_H = 104,
    KEY_I = 105,
    KEY_J = 106,
    KEY_K = 107,
    KEY_L = 108,
    KEY_M = 109,
    KEY_N = 110,
    KEY_O = 111,
    KEY_P = 112,
    KEY_Q = 113,
    KEY_R = 114,
    KEY_S = 115,
    KEY_T = 116,
    KEY_U = 117,
    KEY_V = 118,
    KEY_W = 119,
    KEY_X = 120,
    KEY_Y = 121,
    KEY_Z = 122,
};
} // namespace typed

namespace command
{
enum class CommandKey : int
{
    KEY_EXCLAMATION = 33,
    KEY_DOUBLE_QUOTE = 34,
    KEY_HASH = 35,
    KEY_DOLLAR = 36,
    KEY_PERCENT = 37,
    KEY_AMPERSAND = 38,
    KEY_APOSTROPHE = 39,
    KEY_LEFT_PAREN = 40,
    KEY_RIGHT_PAREN = 41,
    KEY_ASTERISK = 42,
    KEY_PLUS = 43,
    KEY_COMMA = 44,
    KEY_MINUS = 45,
    KEY_DOT = 46,
    KEY_SLASH = 47,
    KEY_COLON = 58,
    KEY_SEMICOLON = 59,
    KEY_LESS = 60,
    KEY_EQUAL = 61,
    KEY_GREATER = 62,
    KEY_QUESTION = 63,
    KEY_AT = 64,
    KEY_LEFT_BRACKET = 91,
    KEY_BACKSLASH = 92,
    KEY_RIGHT_BRACKET = 93,
    KEY_CARET = 94,
    KEY_UNDERSCORE = 95,
    KEY_BACKTICK = 96,
    KEY_LEFT_BRACE = 123,
    KEY_PIPE = 124,
    KEY_RIGHT_BRACE = 125,
    KEY_TILDE = 126,
};
} // namespace command

template <typename EnumT,
          typename = std::enable_if_t<std::is_enum_v<EnumT>, int>>
constexpr int keyCode(EnumT key)
{
    return static_cast<int>(key);
}

constexpr int keyCode(int key)
{
    return key;
}
