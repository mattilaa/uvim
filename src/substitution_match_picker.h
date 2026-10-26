#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class Editor;

namespace command::execution
{
std::string substitutionSeparator(const Editor& editor);
std::string substitutionPreviewText(std::string_view text, int width);
std::string substitutionDisplayPath(
    const Editor& editor,
    std::string_view path);

// Collect occurrences against snapshots, let the user select them, then replay
// the same traversal to accept only the selected occurrences.
class SubstitutionMatchPicker
{
public:
    explicit SubstitutionMatchPicker(Editor& editor) : editor(editor) {}

    bool consider(
        std::string_view path,
        int row,
        size_t column,
        size_t length,
        const std::string& source,
        const std::string& replacement);
    bool choose();

    size_t size() const
    {
        return entries.size();
    }

private:
    struct Entry
    {
        std::string path;
        int row;
        size_t column;
        std::string source, matched, replacement;
        size_t matchOffset = 0;
        bool selected = false;
    };

    Editor& editor;
    std::vector<Entry> entries;
    std::string cachedPath, cachedDisplayPath;
    bool collecting = true;
    size_t next = 0, cursor = 0, offset = 0, selectedCount = 0;
    void draw(bool backdrop, int& visibleRows);
};
} // namespace command::execution
