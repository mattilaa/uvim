#pragma once

#include "diagnostic_entries.h"

#include <vector>

class Editor;

std::vector<DiagnosticEntry> collectClangDiagnostics(Editor& editor,
                                                     int severity);
