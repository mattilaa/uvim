#pragma once

#include "file_type.h"
#include <cstddef>
#include <cstdint>
#include <string_view>

class Editor;

class EditorFileTypeController
{
public:
    explicit EditorFileTypeController(Editor& editor);

    bool isFileType(FileType type) const;
    uint64_t detectFileTypeMask(std::string_view pathSv) const;

private:
    Editor& editor;

    size_t fileTypeProbeHash() const;
};
