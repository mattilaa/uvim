#pragma once

#include <string>
#include <vector>

struct CppSizeProbeOptions
{
    std::string directory;
    std::vector<std::string> flags;
};

CppSizeProbeOptions cppSizeProbeOptions(const std::string& sourcePath);
