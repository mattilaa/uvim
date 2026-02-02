#pragma once

#include "completion_entry.h"
#include <string>

class Editor;

namespace widgets
{
void drawCompletionPopup(std::string& output, const Editor& editor);
std::string buildCompletionRowForTest(const CompletionEntry& entry, int width);
} // namespace widgets
