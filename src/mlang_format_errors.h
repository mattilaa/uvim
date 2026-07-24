#pragma once

#include <string>
#include <vector>

class Editor;

struct MlangFormatErrorEntry
{
    std::string path;
    std::string displayPath;
    int line = 0;
    int col = 0;
    std::string rangeText;
    std::string message;
};

std::vector<MlangFormatErrorEntry> collectMlangFormatErrors(Editor& editor);
std::vector<MlangFormatErrorEntry> collectMlangWarnings(Editor& editor);
