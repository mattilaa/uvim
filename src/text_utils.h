#include <cctype>

static bool isIdent(char c)
{
    return std::isalnum((unsigned char)c) || c == '_';
}
