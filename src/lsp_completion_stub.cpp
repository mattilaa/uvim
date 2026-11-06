#include "editor.h"

// Minimal builds have no completion, emoji, diagnostic, or rename popups.
void Editor::requestCompletion() {}
void Editor::cancelCompletion() {}
void Editor::completionNext() {}
void Editor::completionPrev() {}
void Editor::acceptCompletion() {}
void Editor::rebuildCompletionFilter() {}
void Editor::openEmojiPopup() {}
void Editor::cancelEmojiPopup() {}
void Editor::acceptEmoji() {}
void Editor::emojiNext() {}
void Editor::emojiPrev() {}
void Editor::rebuildEmojiFilter() {}
void Editor::openDiagnosticPopupForCursor() {}
void Editor::closeDiagnosticPopup() {}
void Editor::closeRenamePopup() {}
void Editor::openRenamePopupForCursor() {}
void Editor::drawCompletionPopup(std::string&) const {}
void Editor::drawEmojiPopup(std::string&) const {}
void Editor::drawDiagnosticPopup(std::string&) const {}
void Editor::drawSymbolPopup(std::string&) const {}
void Editor::drawRenamePopup(std::string&) const {}
bool Editor::handleRenamePopupKey(int) { return false; }
void Editor::applyDiagnosticFix(int) {}

std::unordered_map<int, Editor::LspDiagnosticSummary>
Editor::getClangdDiagnosticsByLine() const
{
    return {};
}

std::optional<Editor::LspDiagnosticSummary>
Editor::getClangdDiagnosticForLine(int) const
{
    return std::nullopt;
}
