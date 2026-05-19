#pragma once

#include "mode.h"

#include <string>
#include <string_view>

class Editor;

namespace command::execution::detail
{
void requestMode(Editor& editor, Mode mode, std::string path = {});
bool hasDirtyBuffers(const Editor& editor);
[[noreturn]] void clearAndExit();
bool saveAllBuffers(Editor& editor, bool forceExit);
bool openEditTarget(Editor& editor, std::string path);
bool openTabTarget(Editor& editor, std::string_view cmd);
bool setCwd(Editor& editor, std::string path);
} // namespace command::execution::detail
