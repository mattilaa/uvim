#pragma once

#include "file_type.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class Editor;

class EditorFileTypeController
{
public:
    explicit EditorFileTypeController(Editor& editor);

    bool isFileType(FileType type) const;
    // `contentLines` (default: the current buffer) are sniffed for files
    // whose extension is not conclusive.
    uint64_t detectFileTypeMask(
        std::string_view pathSv,
        const std::vector<std::string>* contentLines = nullptr) const;

private:
    Editor& editor;

    size_t fileTypeProbeHash() const;
};
