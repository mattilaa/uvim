#pragma once

#include "mode.h"
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// ============================================================================
// Editor Context - Shared state accessible by all mode handlers
// ============================================================================
class Editor;

namespace editor::statemachine
{
struct CommandPrompt;

struct ModeContext
{
    Editor* editor;

    // Command/search buffer
    std::string& commandBuffer;

    // Repeat count for normal mode commands
    int& repeatCount;

    // Pending operator state
    char& pendingOperator;
    bool& pendingAwaitingObject;
    char& pendingObjectType;
    int& pendingCount;

    // Search state
    std::string& searchQuery;
    bool& searchForward;
    int& savedCursorX;
    int& savedCursorY;
    bool& needsFullRedraw;

    // Status message
    void setStatusMessage(const std::string& msg);

    // Cursor access (delegates to editor's current buffer)
    int& cursorX();
    int& cursorY();
    int& offsetX();
    int& offsetY();
    int& wantedX();

    // Lines access
    std::vector<std::string>& lines();
    const std::vector<std::string>& lines() const;

    // Buffer dirty flag
    bool& dirty();

    // Screen dimensions
    int screenRows() const;
    int screenCols() const;

    // UI helpers
    void requestFullRedraw();
    void forceFullRedraw();

    // Editor state helpers
    bool hasBuffer() const;
    bool hasCurrentBuffer() const;
    bool hasFilename() const;
    std::string_view currentFilename() const;
    Mode currentMode() const;
    void setCurrentMode(Mode mode);

    std::chrono::steady_clock::time_point& lastEscTime();
    bool hasSearchMatches() const;
    bool hasSearchQuery() const;

    bool completionActive() const;
    bool completionFromLsp() const;
    bool autoCompletion() const;
    bool autoBraces() const;
    int tabSpaces() const;
    bool respectGitignore() const;
    void setRespectGitignore(bool value);
    void setGrepFileIndexInitialized(bool value);

    // Command helpers
    void executeCommand(std::string_view cmd);
    bool takeCommandRequest(Mode& mode, std::string& path);
    std::optional<std::string> commandHistoryUp();
    std::optional<std::string> commandHistoryDown();
    std::vector<std::string> getSetCompletions(std::string_view prefix);
    std::vector<std::string> getHelpCompletions(std::string_view prefix);
    void startCommandPopup();
    void cancelCommandPopup();
    void updateCommandPopup(std::string_view query);
    void moveCommandPopupCursor(int delta);
    bool isCommandPopupActive() const;
    std::optional<std::string> commandPopupSelection() const;
    void startCommandHistorySearch(std::string_view seed);
    std::string cancelCommandHistorySearch();
    std::string acceptCommandHistorySearch();
    void updateCommandHistorySearchQuery(std::string_view query);
    void moveCommandHistorySearchCursor(int delta);
    bool isCommandHistorySearchActive() const;
    std::string_view commandHistorySearchQuery() const;
    std::vector<std::string> getCommandCompletions(std::string_view prefix);
    std::shared_ptr<CommandPrompt> commandPrompt() const;
    std::vector<std::string> getPathCompletions(std::string_view path);
    std::vector<std::string> getPathCompletionsRecursive(std::string_view path);
    std::vector<std::string> getLocPathCompletions(std::string_view path);

    // Buffer/file helpers
    void openFile(std::string_view path);
    void openFileBrowser(std::string_view path);
    void switchToBuffer(int index);
    void closeCurrentBuffer();
    void saveFile();
    void deleteFilePrompt();
    void renameFilePrompt();
    void createNewFilePrompt();
    void createNewDirectoryPrompt();

    // Search helpers
    void performSearch();
    void performIncrementalSearch(const std::string& query, bool forward);
    void addSearchToHistory(const std::string& query);
    std::string getPreviousSearch();
    std::string getNextSearch();
    void findAllMatches();
    void jumpToMatch(int index);
    void searchNext();
    void searchPrevious();
    void searchWordUnderCursor(bool forward);
    void clearSearch();

    // Completion helpers
    void nextCompletion();
    void previousCompletion();
    void acceptCompletion();
    void cancelCompletion();
    void rebuildCompletionFilter();
    void triggerCompletion();
    void requestCompletion();
    bool shouldTriggerCompletion() const;

    // Change recording helpers
    void beginChangeRecording(int count);
    void recordChangeKey(int key);
    void deferChangeRecordingCommit();
    void finishChangeRecordingIfDeferred();
    bool isRecordingChange() const;
    bool isReplayingChange() const;
    void cancelChangeRecording();
    void commitChangeRecording();
    int readKeyRecorded();
    void repeatLastChange(int times);

    // Movement helpers
    void moveLeft();
    void moveRight();
    void moveUp(int count = 1);
    void moveDown(int count = 1);
    void moveWordForward();
    void moveWordBackward();
    void moveWordForwardBig();
    void moveWordBackwardBig();
    void moveToEndOfWord();
    void moveToEndOfWordBig();
    void moveToLineStart();
    void moveToLineEnd();
    void moveToFirstLine();
    void moveToLastLine();
    void moveToLine(int line);
    void moveToFirstNonBlank();
    void moveToMatchingBracket();
    void moveParagraphForward();
    void moveParagraphBackward();
    void moveToScreenTop();
    void moveToScreenMiddle();
    void moveToScreenBottom();
    void scrollPageUp();
    void scrollPageDown();
    void scrollHalfPageUp();
    void scrollHalfPageDown();
    void scrollToTop();
    void scrollToBottom();
    void centerScreen();

    // Editing helpers
    void insertLineAbove();
    void insertLineBelow();
    void insertNewline();
    void insertTab();
    void insertChar(char c);
    void insertUtf8Char(int c);
    void replaceCharAtCursor(char c);
    void deleteCharAtCursor();
    void deleteCharBeforeCursor();
    void deleteCurrentLine();
    void deleteToEndOfLine();
    void deleteWordBackward();
    void deleteToLineStart();
    void joinLines();
    void toggleCase();
    void pasteAfter();
    void pasteBefore();
    void pasteFromSystemClipboard();
    void yankLine();
    void yankToSystemClipboard();
    void saveState();

    // Visual helpers
    void deleteSelection();
    void yankSelection();
    void deleteVisualBlock();
    void yankVisualBlock();
    void indentSelection();
    void dedentSelection();
    void autoIndentSelection();
    void indentLineSelection();
    void dedentLineSelection();
    void autoIndentLineSelection();
    void lowercaseSelection();
    void uppercaseSelection();
    void toggleCaseSelection();
    void yankLineSelection();
    void deleteLineSelection();
    void prepareBlockInsert(bool atEnd);
    void swapVisualBlockCorner();
    void changeVisualBlock();

    // Operator helpers
    bool getTextObjectRange(char objChar, bool around, int& outStartY,
                            int& outStartX, int& outEndY, int& outEndX);
    void applyOperatorToRange(char op, int startY, int startX, int endY,
                              int endX);
    void handleLinewiseOperator(char op, int count);

    // Navigation / refs / info
    void jumpBack();
    void jumpForward();
    void jumpToAlternateFile();
    void switchToAlternateFile();
    void setMark(char mark);
    void jumpToMark(char mark);
    void goToDefinition();
    void findReferences();
    bool hasReferences() const;
    void clearReferences();
    void referencesUp();
    void referencesDown();
    void referencesFirst();
    void referencesLast();
    void referencesHalfPageUp();
    void referencesHalfPageDown();
    void selectReference();
    void toggleReferencesPreview();
    void openReferencePreview();
    void showFileInfo();
    void goToFile();
    void showLspInfo();
    void clearLspInfo();
    void forceQuit();

    // Language/LSP checks
    bool isClangdLspEnabled() const;
    bool isPythonLspEnabled() const;
    bool isRobotLspEnabled() const;
};
} // namespace editor::statemachine
