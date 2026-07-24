#pragma once

#include "diagnostic_entries.h"

#include <vector>

class Editor;

using MlangFormatErrorEntry = DiagnosticEntry;

std::vector<MlangFormatErrorEntry> collectMlangFormatErrors(Editor& editor);
std::vector<MlangFormatErrorEntry> collectActiveLspDiagnostics(Editor& editor,
                                                               int severity);
std::vector<MlangFormatErrorEntry> collectMlangWarnings(Editor& editor);
