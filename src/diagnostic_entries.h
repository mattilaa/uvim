#pragma once

#include <string>

struct DiagnosticEntry
{
    std::string path;
    std::string displayPath;
    int line = 0;
    int col = 0;
    std::string rangeText;
    std::string message;
};
