#pragma once
#include "buffer.h"
#include "completion_entry.h"
#include "file_entry.h"
#include "file_type.h"
#include "formatter.h"
#include "jump_location.h"
#include "mlang_format_errors.h"
#include "mode.h"
#include "preview_highlight.h"
#include "search_types.h"
#include "task_queue.h"
#include "theme.h"
#include "token_type.h"
#include "visual_color_range.h"
#include "widgets/picker_frame.h"
#include "widgets/progress_bar.h"
#include "widgets/status_bar.h"
#include <chrono>
#include <ctime>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class LspClient;

class GitIgnore;
class SyntaxHighlighter;
class Formatter;
class EditorSettingsController;
class EditorBufferController;
class EditorCommandController;
class EditorDefinitionController;
class EditorDrawingController;
class EditorEditingController;
class EditorFileController;
class EditorFileTypeController;
class EditorGitController;
class EditorCursorController;
class EditorIndentController;
class EditorLspController;
class EditorModeController;
class EditorOperatorController;
class EditorReferencesController;
class EditorSplitController;
class EditorVisualController;

namespace editor::statemachine
{
class ModeStateMachine;
struct CommandPrompt;
struct GitLogMode;
} // namespace editor::statemachine

struct MlangTokenCache
{
    bool loaded = false;
    bool available = false;
    bool caseInsensitive = false;
    std::unordered_map<std::string, TokenType> tokenTypes;

    struct BuiltinTypeDef
    {
        std::string path;
        int line = 0; // 0-based
    };

    bool builtinTypesLoaded = false;
    std::unordered_map<std::string, BuiltinTypeDef> builtinTypes;
    bool builtinMacrosLoaded = false;
    std::unordered_map<std::string, BuiltinTypeDef> builtinMacros;
    bool builtinAttributesLoaded = false;
    std::unordered_map<std::string, BuiltinTypeDef> builtinAttributes;
    bool builtinFunctionsLoaded = false;
    std::unordered_map<std::string, BuiltinTypeDef> builtinFunctions;
    std::string root;
    std::string configPath;
    std::string lspPath;
};

class Editor
{
public:
    class RedrawFlag
    {
    public:
        RedrawFlag() = default;
        explicit RedrawFlag(Editor* editor) : editor(editor) {}

        void bind(Editor* nextEditor) { editor = nextEditor; }
        RedrawFlag& operator=(bool value);
        operator bool() const;

    private:
        Editor* editor = nullptr;
    };

    // Mode enum is now in mode.h
    // JumpLocation struct is now in jump_location.h

    Editor(bool skipInitialBuffer = false, const std::string& configPath = "",
           const std::string& themePath = "");
    ~Editor();
#ifdef UVIM_TESTING
    static Editor createForTests(int rows = 24, int cols = 80);
#endif

    // Optional clangd LSP integration (compiled in when UVIM_ENABLE_CLANGD_LSP
    // is set). Enable at runtime via: uvim --clangd [--ccdir <build_dir>]
    // [files...]
    void enableClangdLsp(bool enable,
                         const std::string& compileCommandsDir = "",
                         const std::string& clangdPath = "clangd",
                         const std::string& queryDriverAllowList = "");
    bool isClangdLspEnabled() const;
    void enableRobotLsp(bool enable,
                        const std::string& robotLspPath = "robotframework-lsp",
                        const std::vector<std::string>& robotLspArgs = {});
    bool isRobotLspEnabled() const;
    void enablePythonLsp(bool enable,
                         const std::string& pythonLspPath = "pylsp",
                         const std::vector<std::string>& pythonLspArgs = {});
    bool isPythonLspEnabled() const;
    void enableMlangLsp(bool enable,
                        const std::string& mlangLspPath = "mlangd-mla",
                        const std::vector<std::string>& mlangLspArgs = {});
    bool isMlangLspEnabled() const;
    void enableHtmlLsp(
        bool enable,
        const std::string& htmlLspPath = "vscode-html-language-server",
        const std::vector<std::string>& htmlLspArgs = {});
    bool isHtmlLspEnabled() const;
    void
    enableCssLsp(bool enable,
                 const std::string& cssLspPath = "vscode-css-language-server",
                 const std::vector<std::string>& cssLspArgs = {});
    bool isCssLspEnabled() const;
    void enableJsonLsp(
        bool enable,
        const std::string& jsonLspPath = "vscode-json-language-server",
        const std::vector<std::string>& jsonLspArgs = {});
    bool isJsonLspEnabled() const;
    void
    enableTsLsp(bool enable,
                const std::string& tsLspPath = "typescript-language-server",
                const std::vector<std::string>& tsLspArgs = {});
    bool isTsLspEnabled() const;
    // C#: csharp-ls (default) or OmniSharp ("-lsp" is added for it).
    void enableCSharpLsp(bool enable,
                         const std::string& csharpLspPath = "csharp-ls",
                         const std::vector<std::string>& csharpLspArgs = {});
    bool isCSharpLspEnabled() const;
    // Sends a changed C# buffer to the server (diagnostics follow edits).
    void syncCSharpLspIfNeeded(bool force);

    void run();
    void openFile(std::string_view filename, bool notifyLspOnOpen = true);
    bool prewarmColdOpenFile(std::string_view filename);
    void prewarmColdOpenFiles(const std::vector<std::string>& filenames);

    // Buffer struct is now in buffer.h
    // JumpLocation struct is now in jump_location.h

    bool visualBlockChanging = false;
    std::unique_ptr<SyntaxHighlighter> syntaxHighlighter;
    std::unique_ptr<Formatter> formatter;
    // Buffer management
    std::vector<std::unique_ptr<Buffer>> buffers;
    std::unordered_map<std::string, int> bufferIndexByFilename;
    struct ColdOpenCacheEntry
    {
        std::vector<std::string> lines;
        std::filesystem::file_time_type lastModificationTime{};
        uintmax_t size = 0;
        std::chrono::steady_clock::time_point lastTouched{};
    };
    std::unordered_map<std::string, ColdOpenCacheEntry> coldOpenCache;
    size_t coldOpenCacheMaxEntries = 32;
    int currentBufferIndex = -1;
    Buffer* currentBuffer = nullptr;

    // References to current buffer's data (for easier access)
    std::vector<std::string>* lines = nullptr;
    std::string* filename = nullptr;
    bool* dirty = nullptr;
    int* cursorX = nullptr;
    int* cursorY = nullptr;
    int* wantedX = nullptr;
    int* offsetX = nullptr;
    int* offsetY = nullptr;

    // In-memory fuzzy file index retained across transient Ctrl-P modes.
    std::vector<FileEntry> fuzzyProjectFiles;
    bool fuzzyFileIndexInitialized = false;
    std::string fuzzyFileIndexCwd;
    bool fuzzyFileIndexRespectGitignore = true;
    bool fuzzyFileIndexUseGit = true;

    // Shared by fuzzy-find and grep for the lifetime of this editor session.
    // 100 is the default high-contrast palette; 0 is the lowest contrast.
    int searchMatchContrast = 100;

    // Hot file-browser listing. The active browser takes ownership of this
    // vector and returns it on exit, avoiding an O(n) filename copy each time
    // leader-x reopens the same directory.
    std::vector<FileEntry> fileBrowserHotListing;
    bool fileBrowserHotListingInitialized = false;
    std::string fileBrowserHotListingKey;
    std::filesystem::file_time_type fileBrowserHotListingModTime{};

    // Grep file index
    std::vector<FileEntry> grepProjectFiles; // All files in project
    bool grepFileIndexInitialized = false;
#ifdef UVIM_ENABLE_RG_CACHE
    struct RgCachedFile
    {
        std::string path;
        uintmax_t size = 0;
        long long mtime = 0;
        std::vector<std::string> lines;
        std::vector<std::string> lowerLines;
    };
    struct RgCacheLineRef
    {
        int fileIndex = 0;
        int lineIndex = 0;
    };
    bool rgCacheEnabled = true;
    int rgUpdateMs = 300;
    bool rgCacheLoaded = false;
    std::vector<RgCachedFile> rgCachedFiles;
    // Background rg cache indexing job (0 when idle). Bumping the generation
    // invalidates whatever an in-flight job would install.
    std::uint64_t rgCacheIndexTask = 0;
    std::uint64_t rgCacheGeneration = 0;
    // Set when background indexing installed a fresh cache; the grep view
    // re-runs its query against it.
    bool rgCacheJustLoaded = false;
    // Finished background grep search, adopted by the grep view whose
    // activeSearch matches (results for superseded queries are dropped).
    struct GrepBackgroundResult
    {
        std::uint64_t searchId = 0;
        bool completed = false;
        std::vector<GrepMatch> matches;
    };
    std::optional<GrepBackgroundResult> grepBackgroundResult;
    // Stops background indexing and makes its result void.
    void cancelRgCacheIndexing();
    std::unordered_map<std::string, std::vector<RgCacheLineRef>>
        rgCacheLineIndex;
#endif

    bool respectGitignore = true;
    // Terminal reports Ctrl+Shift+letter (kitty keyboard protocol). When
    // false, Alt+h/j/k/l/x stand in for Ctrl+Shift+h/j/k/l/x. Detected at
    // startup; `iskitty` in the config or `:set iskitty=` overrides it.
    bool isKitty = true;
    void setIsKitty(bool value);
    bool useGitFileIndex = true;
    bool gitignoreLockedOff = false;
    bool gitFileIndexLockedOff = false;
    // References browser (LSP find references)
    struct ReferenceEntry
    {
        std::string path;        // Full file path
        std::string displayPath; // Relative/shortened path for display
        int line = 0;            // 0-based line number
        int col = 0;             // 0-based column
        std::string lineContent; // Content of the line for preview
    };

    std::vector<ReferenceEntry> referencesList;
    int referencesCursor = 0;
    int referencesOffset = 0;
    bool referencesPreview = true;

    // LOC results
    struct LocEntry
    {
        std::string path;        // Full file path
        std::string displayPath; // Relative/shortened path for display
        int loc = 0;             // Lines of code (non-empty, non-comment)
    };

    std::vector<LocEntry> locList;
    int locListTotal = 0;
    std::string locListRoot;
    std::string locMessage;

    // Mlang formatter error browser
    std::vector<MlangFormatErrorEntry> mlangFormatErrors;

    // LSP info panel
    std::vector<std::string> lspInfoLines;
    int lspInfoScrollOffset = 0;

    // External tool info panel
    std::vector<std::string> toolInfoLines;
    int toolInfoScrollOffset = 0;

    // Screen
    int screenRows;
    int screenCols;

    // Operator-pending state
    char pendingOperator = 0;           // 'd', 'c', 'y', etc.
    bool pendingAwaitingObject = false; // After 'd' then 'i'/'a'
    char pendingObjectType = 0; // 'i' or 'a' when awaiting a text object
    int pendingCount = 0;       // support counts like 2dw if present

    // Drawing state is owned by EditorDrawingController. This forwarding flag
    // preserves existing call sites while the rest of the editor is migrated.
    RedrawFlag needsFullRedraw{this};
    std::string projectRoot;

    // Modes
    Mode currentMode = NORMAL;
    std::string commandBuffer;
    std::shared_ptr<editor::statemachine::CommandPrompt> commandPrompt;
    std::string statusMessage;
    bool commandRequestedModeSet = false;
    Mode commandRequestedMode = NORMAL;
    std::string commandRequestedPath;
    std::optional<Mode> commandRequestedReturnMode;
    int commandRequestedBrowseCursor = 0;
    int commandRequestedBrowseOffset = 0;
    std::string commandRequestedBrowseDirectory;
    std::string symbolPrefix;

    // clangd LSP (optional, runtime-enabled)
    bool clangdLspEnabled = false;
    std::string clangdLspCompileCommandsDir;
    std::string clangdLspPath = "clangd";
    std::string clangdLspQueryDriverAllowList;
    bool clangdLspStartupAttempted = false;
    std::string clangdLspLastError;
    bool robotLspEnabled = false;
    std::string robotLspPath = "robotframework-lsp";
    std::vector<std::string> robotLspArgs;
    bool pythonLspEnabled = false;
    std::string pythonLspPath = "pylsp";
    std::vector<std::string> pythonLspArgs;
    bool mlangLspEnabled = false;
    std::string mlangLspPath = "mlangd-mla";
    std::vector<std::string> mlangLspArgs;
    bool htmlLspEnabled = false;
    std::string htmlLspPath = "vscode-html-language-server";
    std::vector<std::string> htmlLspArgs;
    bool cssLspEnabled = false;
    std::string cssLspPath = "vscode-css-language-server";
    std::vector<std::string> cssLspArgs;
    bool jsonLspEnabled = false;
    std::string jsonLspPath = "vscode-json-language-server";
    std::vector<std::string> jsonLspArgs;
    bool tsLspEnabled = false;
    std::string tsLspPath = "typescript-language-server";
    std::vector<std::string> tsLspArgs;
    bool csharpLspEnabled = false;
    std::string csharpLspPath = "csharp-ls";
    std::vector<std::string> csharpLspArgs;
    std::shared_ptr<MlangTokenCache> mlangTokenCache;
#ifdef UVIM_ENABLE_CLANGD_LSP
    std::unique_ptr<LspClient> lspClient;
    std::unique_ptr<LspClient> robotLspClient;
    std::unique_ptr<LspClient> pythonLspClient;
    std::unique_ptr<LspClient> mlangLspClient;
    std::unique_ptr<LspClient> htmlLspClient;
    std::unique_ptr<LspClient> cssLspClient;
    std::unique_ptr<LspClient> jsonLspClient;
    std::unique_ptr<LspClient> tsLspClient;
    std::unique_ptr<LspClient> csharpLspClient;
#endif

    Theme theme;

    // LSP completion popup (CompletionEntry struct is now in
    // completion_entry.h)
    bool completionActive = false;
    bool completionFromLsp = false;
    std::vector<CompletionEntry> completionAll; // full list from clangd
    std::vector<int> completionFiltered; // indices into completionAll (sorted
                                         // by fuzzy score)
    std::string completionQuery; // derived from buffer [anchorX, cursorX)
    int completionSelected = 0;  // index into completionFiltered
    int completionScroll = 0;  // first visible index (into completionFiltered)
    int completionAnchorX = 0; // start of current identifier/prefix
    int completionAnchorY = 0;

    void requestCompletion();
    void cancelCompletion();
    void acceptCompletion();
    void completionNext();
    void completionPrev();
    void rebuildCompletionFilter();
    void drawCompletionPopup(std::string& output) const;

    struct EmojiPopupEntry
    {
        std::string emoji;
        std::string emojiDisplay;
        std::string name;
        std::string label;
    };

    bool emojiPopupActive = false;
    std::vector<EmojiPopupEntry> emojiEntries;
    std::vector<int> emojiFiltered;
    std::string emojiQuery;
    int emojiSelected = 0;
    int emojiScroll = 0;
    mutable int emojiPopupLastTop = 0;
    mutable int emojiPopupLastLeft = 0;
    mutable int emojiPopupLastWidth = 0;
    mutable int emojiPopupLastHeight = 0;
    mutable bool emojiPopupLastValid = false;

    void openEmojiPopup();
    void cancelEmojiPopup();
    void acceptEmoji();
    void emojiNext();
    void emojiPrev();
    void rebuildEmojiFilter();
    void drawEmojiPopup(std::string& output) const;
    static constexpr int kDiagnosticGutterWidth = 1;
    static constexpr int kGitBlameMaxWidth = 30;
    static constexpr int kGitBlameDateTimeMaxWidth = 48;
    int lineNumberWidth() const;
    int gitBlameWidth() const;
    int gutterWidth() const;

    struct LspDiagnosticSummary
    {
        int severity = 0;
        int character = 0;
        std::string message;
    };

    std::unordered_map<int, LspDiagnosticSummary>
    getClangdDiagnosticsByLine() const;
    std::optional<LspDiagnosticSummary>
    getClangdDiagnosticForLine(int line) const;
    void drawDiagnosticPopup(std::string& output) const;
    void drawSymbolPopup(std::string& output) const;
    void openDiagnosticPopupForCursor();
    void closeDiagnosticPopup();
    void openSymbolPopupForCursor();
    void openSizePopupForCursor();
    void openAsmDocumentationPopupForCursor();
    void closeSymbolPopup();
    void syncClangdDiagnosticsIfNeeded(bool force,
                                       bool updateDiagnosticPopup = true);
    void syncMlangSemanticTokensIfNeeded(bool force,
                                         bool updateDiagnosticPopup = true);

    bool diagnosticPopupActive = false;
    int diagnosticPopupLine = -1;
    int diagnosticPopupCursorX = -1;
    int diagnosticPopupCursorY = -1;
    LspDiagnosticSummary diagnosticPopupData{};

    struct DiagnosticFixEdit
    {
        int startLine = 0;
        int startCharacter = 0;
        int endLine = 0;
        int endCharacter = 0;
        std::string newText;
    };

    struct DiagnosticFix
    {
        std::string title;
        std::vector<DiagnosticFixEdit> edits;
        std::string command;
        std::vector<std::string> commandArgsJson;
    };

    std::vector<DiagnosticFix> diagnosticPopupFixes;
    int diagnosticPopupFixIndex = 0;
    int diagnosticPopupFixScroll = 0;

    bool symbolPopupActive = false;
    bool symbolPopupModal = false;
    int symbolPopupCursorX = -1;
    int symbolPopupCursorY = -1;
    int symbolPopupScroll = 0;
    std::string symbolPopupText;

    struct RenameEdit
    {
        std::string path;
        int startLine = 0;
        int startCharacter = 0;
        int endLine = 0;
        int endCharacter = 0;
        std::string newText;
        bool applied = false;
    };

    struct RenameFileEdits
    {
        std::string path;
        std::vector<RenameEdit> edits;
    };

    struct RenameUndoFileSnapshot
    {
        std::string path;
        std::vector<std::string> lines;
        bool hadOpenBuffer = false;
        bool fileExisted = false;
        bool dirty = false;
        int cursorX = 0;
        int cursorY = 0;
        int offsetX = 0;
        int offsetY = 0;
    };

    bool renamePopupActive = false;
    bool renamePopupReady = false;
    std::string renameOriginal;
    std::string renameInput;
    std::string renameStatus;
    int renameCursorX = -1;
    int renameCursorY = -1;
    int renameCurrentFile = 0;
    int renameCurrentEdit = 0;
    std::vector<RenameFileEdits> renameFiles;
    bool renameUndoAvailable = false;
    std::vector<RenameUndoFileSnapshot> renameUndoFiles;

    void openRenamePopupForCursor();
    void closeRenamePopup();
    bool handleRenamePopupKey(int key);
    void drawRenamePopup(std::string& output) const;
    void clearRenameUndoSnapshot();
    bool restoreRenameUndoSnapshot();

    void applyDiagnosticFix(int index);
    void updateClangFormatIndentWidth();
    int indentWidthForBraces() const;
    bool braceNewLineForAutoBraces() const;
    void commentLines(int startY, int endY);
    void commentBlock(int startY, int endY);
    void commentBlockRange(int startY, int startX, int endY, int endX);
    bool insertTodoLineComment(int row);
    bool insertTodoBlockComment(int row);
    void toggleGitBlame(bool includeDateTime = false);
    void updateGitBlameForVisibleRange();
    std::string blameDisplayForLine(int row) const;
    std::string blameFullForLine(int row) const;
    void openGitShowCommitMode();
    std::vector<std::string> loadGitShowLines(const std::string& hash);
    void openGitLogMode();
    void openGitLogModeForBlameLine();
    void openGitPrettyLogMode();
    void openGitLogModeForFile();
    void openGitStageMode();
    void openGitDiffMode();
    void openUnsavedChangesMode();
    // Reloads a git log's entries (commit hashes change after a rewrite).
    void reloadGitLog(editor::statemachine::GitLogMode& log);
    std::vector<std::string> unsavedChangesDiff(std::string& title,
                                                std::string& message);
    void openGitCommitMode();
    void openGitFixupMode();
    void addCurrentBuffer();
    bool runGitStash(std::string& outMessage);
    bool runGitStashPop(std::string& outMessage);
    // Search (global state) - SearchMatch struct is now in search_types.h
    std::string searchQuery;
    bool searchForward = true;
    bool searchRegexError = false;
    bool searchMatchesPartial = false;
    std::vector<SearchMatch> searchMatches;
    int currentMatchIndex = -1;
    int savedCursorX = 0;
    int savedCursorY = 0;

    // Double ESC detection
    std::chrono::steady_clock::time_point lastEscTime;
    static constexpr int DOUBLE_ESC_TIMEOUT_MS = 300;

    // Vim-like features (global)
    std::string yankBuffer;
    int repeatCount = 0;
    char lastFindChar = 0;
    bool lastFindForward = true;
    bool recordingChange = false;
    bool replayingChange = false;
    bool deferChangeCommit = false;
    int pendingChangeCount = 1;
    int lastChangeCount = 1;
    std::vector<int> pendingChangeKeys;
    std::vector<int> lastChangeKeys;
    bool autoBraces = true;
    bool autoQuotes = true;
    bool autoBracesInStrings = true;
    bool splitBraces = true;
    bool autoBrackets = true;
    bool splitBrackets = true;
    bool autoTags = true;
    bool autoCompletion = true;
    bool completionAutoParens = true;

    struct PaneState
    {
        int bufferIndex = -1;
        int cursorX = 0;
        int cursorY = 0;
        int wantedX = 0;
        int offsetX = 0;
        int offsetY = 0;
    };

    struct PaneLayout
    {
        int x = 0;
        int y = 0;
        int rows = 0;
        int cols = 0;
    };

    struct SplitNode
    {
        bool leaf = true;
        bool vertical = true;
        int pane = -1;
        int first = -1;
        int second = -1;
    };

    bool splitActive = false;
    bool splitVertical = true;
    int activePane = 0;
    std::vector<PaneState> splitPanes;
    std::vector<int> splitTabBarOffset;
    std::vector<SplitNode> splitNodes;
    int splitRoot = -1;
    mutable std::vector<PaneLayout> splitPaneLayouts;
    bool useSystemClipboard = true;
    bool showRelativeLineNumbers = true;
    bool showGitBlame = false;
    bool showGitBlameDateTime = false;
    bool showGitBlameInfo = true;
    bool gitUseDefaultColors = true;
    bool commentTogglePartial = false;
    bool formatOnInsertLeave = true;
    bool formatOnSave = true;
    bool autoDetectLsps = true;
    bool emitLspDiagnostics = true;
    bool fileBrowserFuzzy = false;
    bool commandLineMessagePrefix = false;
    int lspStatusGap = 7;
    bool formatOnDoubleEscPending = false;
    int formatOnDoubleEscTimeoutMs = DOUBLE_ESC_TIMEOUT_MS;
    bool gdCenterScreen = true;
    int tabSpaces = 4;
    bool showTabs = true;
    bool showTabNumbers = true;
    bool utf8Mode = true;
    int tabBarOffset = 0;
    mutable int maxLineCountSeen = 1;
    std::string configPath;
    bool syntaxJson = true;
    bool syntaxYaml = true;
    bool syntaxRobotKeywords = true;
    bool syntaxRobotHighlightTitles = true;
    bool syntaxRobotHighlightCalls = true;
    bool syntaxCppHighlightMembers = true;
    bool syntaxCppHighlightTypeNames = true;
    bool syntaxCppHighlightImplicitMembers = true;
    bool syntaxCppHighlightParamTypes = true;
    bool syntaxCppHighlightSystemIncludes = true;
    bool syntaxCppSemanticTokens = true;
    TokenType syntaxCppLocalToken = TOKEN_NORMAL;
    TokenType syntaxCppMemberToken = TOKEN_MEMBER;
#ifndef UVIM_MLANG_SEMANTIC_TOKENS_DEFAULT
#define UVIM_MLANG_SEMANTIC_TOKENS_DEFAULT 1
#endif
    bool syntaxMlangSemanticTokens = UVIM_MLANG_SEMANTIC_TOKENS_DEFAULT != 0;
    bool syntaxMlangHighlightTypes = true;
    bool syntaxMlangHighlightBuiltinDocs = true;
    std::unordered_set<std::string> robotKeywordSet;
    std::unordered_set<std::string> robotCustomKeywordSet;
    std::string pythonFormatter = "ruff";
    std::unordered_set<std::string> robotSettingSet;
    TokenType markupHeadingToken = TOKEN_KEYWORD;
    TokenType markupBoldToken = TOKEN_KEYWORD;
    TokenType markupItalicToken = TOKEN_TYPE;
    TokenType markupCodeToken = TOKEN_STRING;
    TokenType markupBlockquoteToken = TOKEN_COMMENT;
    TokenType markupFenceToken = TOKEN_PREPROCESSOR;
    TokenType markupLinkTextToken = TOKEN_TYPE;
    TokenType markupLinkUrlToken = TOKEN_STRING;
    TokenType markupLinkTitleToken = TOKEN_TYPE;
    TokenType markupRdocTopicToken = TOKEN_FUNCTION;

    // Drawing - Original functions
    void draw();
    void drawRows();
    void drawStatusBar();
    void drawMessageBar();
    void drawBufferView();
    void refreshScreen();
    void drawIncrementalUpdate();
    void drawFullScreen();
    void drawSplitFullScreen();
    void drawFullScreenSingle();
    void updateCursorPosition(bool flushNow = true);
    std::string buildTabBarLine(int width);

    // Drawing - NEW OPTIMIZATION FUNCTIONS
    void drawScrollUpdate(int scrollDelta);
    void drawGutterQuick();
    void drawStatusBarQuick();
    void drawMessageBarQuick(); // Add this to redraw message bar
    bool handleSetCommand(std::string_view cmd);
    int tabBarRows() const;
    int contentRows() const;
    PaneLayout getPaneLayout(int pane) const;
    void setPanePointers(int pane);
    void enableSplit(bool vertical);
    void enableBrowserSplit(bool vertical);
    void closeSplit();
    void switchPane();
    void switchPaneDirection(int dx, int dy);
    void syncBufferStateFromActivePane();
    void initSplitPanesFromBuffer();
    void switchToBufferInActivePane(int index);
    void adjustViewportForPane(PaneState& pane, int rows, int cols);
    bool canSplit() const;

    // Mode handling
    void handleResize();
    bool isRobotKeyword(std::string_view word) const;
    bool isRobotCustomKeyword(std::string_view word) const;
    bool isRobotSetting(std::string_view cell) const;

    // Buffer management functions
    void createNewBuffer();
    void switchToBuffer(int index);
    void closeCurrentBuffer();
    void nextBuffer();
    void previousBuffer();
    void moveBufferLeft();
    void moveBufferRight();
    void listBuffers();
    int findBufferByFilename(const std::string& filename);
    void updateCurrentBufferPointers();
    void clearCurrentBufferPointers();
    bool hasBuffer() const;
    void ensureBufferForMode(Mode mode);
    void saveBufferState();
    void restoreBufferState();
    bool searchDefinitionInBuffer(Buffer* buf, const std::string& symbol,
                                  int& outY, int& outX);

    // Operator-pending / text-object support
    void enterOperatorPending(char op);
    bool getTextObjectRange(char objChar, bool around, int& outStartY,
                            int& outStartX, int& outEndY, int& outEndX);
    void applyOperatorToRange(char op, int startY, int startX, int endY,
                              int endX);
    void deleteRange(int startY, int startX, int endY, int endX);
    void yankRange(int startY, int startX, int endY, int endX);

    // File browser functions
    void openFileBrowser(std::string_view path = ".",
                         bool focusCurrentFile = false,
                         bool acceptSecondLeaderX = false);
    std::string getFilePermissions(const std::string& path);
    std::string getRelativePath(const std::string& path);
    void createNewFile();
    void deleteCurrentFile();

    // Jump between header and source
    void jumpToAlternateFile();
    std::string findAlternateFile(const std::string& currentFile);
    bool fileExists(const std::string& path);
    std::string getSymbolUnderCursor();

    // Fuzzy finder functions
    // File browser navigation helpers (for mode handlers)
    void deleteFilePrompt();
    void renameFilePrompt();
    void createNewFilePrompt();
    void createNewDirectoryPrompt();

    // Grep search is now handled by GrepSearchMode state.
    void goToDefinition();

    // References browser functions (LSP find references)
    void findReferences();
    void clearReferences();
    bool selectReference();
    void openReferencePreview();
    void referencesUp();
    void referencesDown();
    void referencesHalfPageUp();
    void referencesHalfPageDown();
    void referencesFirst();
    void referencesLast();
    void toggleReferencesPreview();
    void drawReferences();
    bool hasReferences() const;

    // LSP info panel
    void showLspInfo();
    void clearLspInfo();
    void scrollLspInfo(int delta);
    void drawLspInfo();

    // External tool info panel
    void showToolInfo();
    void clearToolInfo();
    void scrollToolInfo(int delta);
    void drawToolInfo();
    void drawTaskList();

    // Movement commands
    void moveCursor(int key);
    void moveLeft(int count = 1);
    void moveRight(int count = 1);
    void moveUp(int count = 1);
    void moveDown(int count = 1);
    void moveWordForward();
    void moveWordBackward();
    void moveToEndOfWord();
    void moveToLineStart();
    void moveToLineEnd();
    void moveToFirstLine();
    void moveToLastLine();
    void moveToLine(int line);
    // Picker "file:415" jumps: moves to 1-based `line`, or to the last/first
    // line with a status message when the file has no such line.
    void jumpToRequestedLine(int line);
    // Extends `cache` with syntax tokens for `lines` of `path` (a file shown
    // in a picker preview, not the current buffer) up to line `upTo`.
    void highlightPreviewLines(PreviewHighlight& cache, const std::string& path,
                               const std::vector<std::string>& lines,
                               size_t upTo);
    void pushJumpLocation();
    void jumpForward();
    void jumpBack();
    void restoreJumpLocation(const JumpLocation& loc);
    void scrollHalfPageDown(bool visual);
    void scrollHalfPageUp(bool visual);

    void scrollHalfPageDown()
    {
        scrollHalfPageDown(false);
    }

    void scrollHalfPageUp()
    {
        scrollHalfPageUp(false);
    }

    void moveToMatchingBracket();
    void findCharForward(char c);
    void findCharBackward(char c);

    // Extended movement commands (for mode handlers)
    void moveToFirstNonBlank();
    void moveParagraphForward();
    void moveParagraphBackward();
    void moveWordForwardBig();
    void moveWordBackwardBig();
    void moveToEndOfWordBig();
    void findCharForwardBefore(char c); // 't' motion
    void findCharBackwardAfter(char c); // 'T' motion

    // Scrolling commands
    void scrollToTop();
    void scrollToBottom();
    void scrollPageUp();
    void scrollPageDown();
    void moveToScreenTop();
    void moveToScreenMiddle();
    void moveToScreenBottom();

    // Search commands
    void startSearchForward();
    void startSearchBackward();
    void performSearch();
    void findAllMatches();
    void searchNext();
    void searchPrevious();
    void clearSearch();
    void highlightMatch(int index);
    bool isInSearchMatch(int row, int col);
    void jumpToMatch(int index);
    void cancelSearch();

    // Extended search commands (for mode handlers)
    void performSearch(const std::string& query, bool forward);
    void performIncrementalSearch(const std::string& query, bool forward);
    void searchWordUnderCursor(bool forward);
    void addSearchToHistory(const std::string& query);
    std::string getPreviousSearch();
    std::string getNextSearch();

    // Search history
    std::vector<std::string> searchHistory;
    int searchHistoryIndex = -1;

    // Editing commands
    void insertChar(char c);
    void insertNewline();
    void insertNewlineAbove();
    void insertNewlineBelow();
    void deleteChar();
    void deleteCharForward();
    void deleteLine();
    void deleteToLineEnd();
    void deleteWord();
    void deleteSelection();
    void changeWord();
    void changeLine();
    void changeToLineEnd();
    void replaceChar(char c);

    // Extended editing commands (for mode handlers)
    void insertTab();
    void toggleCase();
    void joinLines();
    bool moveLineBlock(int startY, int endY, int delta);
    void insertLineAbove(); // Alias for insertNewlineAbove + position
    void insertLineBelow(); // Alias for insertNewlineBelow + position
    void deleteCurrentLine();
    void deleteToLineStart();
    void deleteCharAtCursor();
    void deleteCharBeforeCursor();
    void deleteWordBackward();
    void handleBackspace();
    void replaceCharAtCursor(char c);
    void repeatLastChange(int times = 1);
    void insertUtf8Char(int codepoint);
    void indentCurrentLine();
    void dedentCurrentLine();
    void handleLinewiseOperator(char op, int count);
    void beginChangeRecording(int count = 1);
    void recordChangeKey(int key);
    void deferChangeRecordingCommit();
    void commitChangeRecording();
    void cancelChangeRecording();
    void finishChangeRecordingIfDeferred();
    bool isRecordingChange() const;
    bool isReplayingChange() const;
    int readKeyRecorded();

    // Completion helpers (for insert mode)
    bool shouldTriggerCompletion();
    void triggerCompletion();
    void nextCompletion();
    void previousCompletion();

    // Aliases for compatibility
    void deleteToEndOfLine();     // Alias for deleteToLineEnd
    void switchToAlternateFile(); // Alias for jumpToAlternateFile

    // Copy/Paste
    void yankLine();
    void yankToLineEnd();
    void yankSelection();
    void yankWord();
    void pasteAfter();
    void pasteBefore();

    // System clipboard
    std::string getSystemClipboard();
    void setSystemClipboard(const std::string& text);
    void yankToSystemClipboard();
    void pasteFromSystemClipboard();

    // Visual mode
    std::optional<VisualColorRange> pendingVisualColorRange;

    void startVisualMode();
    void startVisualLineMode();
    void startVisualBlockMode();
    void updateVisualSelection();
    void updateVisualBlockSelection();
    bool isInSelection(int row, int col);
    bool isInVisualBlock(int row, int col);
    void getSelectionBounds(int& startY, int& startX, int& endY, int& endX);
    void getVisualBlockBounds(int& startY, int& startX, int& endY, int& endX);
    void rememberVisualColorRange();
    std::optional<VisualColorRange> takeVisualColorRange();
    void deleteVisualBlock();
    void changeVisualBlock();
    void yankVisualBlock();
    void applyVisualBlockInsert();

    // Extended visual mode commands (for mode handlers)
    void setVisualRange();
    void swapVisualEnds();
    void swapVisualBlockCorner();
    void prepareBlockInsert(bool atEnd);
    void indentSelection();
    void dedentSelection();
    void autoIndentSelection();
    void lowercaseSelection();
    void uppercaseSelection();
    void toggleCaseSelection();
    void yankLineSelection();
    void deleteLineSelection();
    void indentLineSelection();
    void dedentLineSelection();
    void autoIndentLineSelection();

    // File operations
    void saveFile();
    void executeCommand(std::string_view cmd);
    void refreshFileSearchCaches();
    void forceQuit();
    std::string getAlternateFilePath();
    void checkFileChanges();
    void reloadCurrentFile();

    // Utilities
    void setStatusMessage(const std::string& msg);

    // ---- Background tasks (editor_tasks.cpp) ------------------------------
    // Heavy operations run on TaskQueue workers so the TUI never freezes.
    // Same-channel jobs run in submission order; `done` runs on the main
    // thread in submission order. Work must not touch editor/terminal state.
    TaskQueue tasks;
    widgets::ProgressStyle progressStyle = widgets::ProgressStyle::Color;

    // Ctrl-P / Ctrl-A pickers: a floating box over the code (Telescope
    // style) or the classic fullscreen view (`pickerstyle=fullscreen`).
    bool pickerFloating = true;
    int pickerWidthPercent = 80;
    int pickerHeightPercent = 70;
    bool pickerPreview = true;
    widgets::PickerLayout pickerLayout(bool previewWanted) const;
    // Draws the code underneath a floating picker (or clears the screen
    // when there is no buffer). Call inside a Terminal capture. The code
    // does not change while a picker is open, so it is painted once per
    // box geometry; later frames only repaint the box (no flicker).
    void drawPickerBackground(const widgets::PickerLayout& layout);
    void invalidatePickerBackground()
    {
        pickerBackground_ = {};
    }
    // The current screen already shows the code (e.g. opening a picker from
    // normal mode): the next background paint is skipped.
    void adoptScreenAsPickerBackground()
    {
        pickerBackground_ = {};
        pickerBackgroundAdopted_ = true;
    }
    // The next full redraw repaints in place instead of clearing first.
    void skipNextScreenClear()
    {
        skipNextScreenClear_ = true;
    }
    bool takeScreenClear()
    {
        const bool skip = skipNextScreenClear_;
        skipNextScreenClear_ = false;
        return !skip;
    }

    TaskQueue::Submitted submitTask(std::string label, std::string channel,
                                    TaskQueue::Work work,
                                    TaskQueue::Done done = {},
                                    TaskQueue::Done onAbort = {});
    // Foreground variant for results the caller needs right away: waits for
    // the job while animating the status bar and queueing keystrokes, which
    // replay once it returns. Ctrl-C cancels. False when cancelled/failed.
    bool runTask(std::string label, std::string channel,
                 TaskQueue::Work work);
    // Waits for an already submitted job like runTask does (progress,
    // queued keys, Ctrl-C). False when it was cancelled or failed.
    bool waitTask(TaskQueue::TaskId id, const std::string& label);
    // Startup work that must run on the UI thread (LSP server start, ...):
    // held until the first frame is on screen, then run one per main-loop
    // tick with "(i/n) label" progress, so startup never shows a blank
    // terminal. `runLast` steps go after all regular ones.
    void addStartupStep(std::string label, std::function<void()> step,
                        bool runLast = false);
    // Main loop: applies finished jobs, reports errors, animates progress.
    void serviceTasks();
    // Status bar text for the current job (or LSP work), padded to
    // taskStatusWidth(); empty when nothing is running.
    std::string taskStatusText();
    int taskStatusWidth() const;
    // Status bar renderers report where they placed taskStatusText().
    void noteTaskStatusSlot(int row, int col);
    // Bottom status + message rows for views that draw their own screen;
    // hosts the task progress exactly like the editor status bar.
    void appendViewStatusBar(std::string& output,
                             widgets::ViewStatusBarView view);
    // Status line for views with their own layout ("left ... right"):
    // writes theme colours, fits `left`, puts task progress before `right`
    // and records its slot. `row` is the screen row the line is drawn on.
    void appendStatusLine(std::string& output, std::string left,
                          const std::string& right, int row = -1);
    void beginTaskStatusFrame();
    void finishTaskStatusFrame();
    void attachLspWaitHook(LspClient& client);
    // Called by run(): routes blocking process I/O on the UI thread through
    // runTask and lets waits pump the terminal. Off in tests/batch use.
    void enableInteractiveTasks();
    void markFirstFrameDrawn()
    {
        firstFrameDrawn_ = true;
    }
    // All LSP clients that currently run, for progress display.
    std::vector<std::pair<std::string, LspClient*>> activeLspClients() const;
    bool noteDoubleEscStatusClear();
    void setMode(Mode mode);
    std::string getModeString() const;
    bool isWordChar(char c) const;
    void adjustViewport();
    void centerScreen();
    int getLineIndent(int line);
    void indentLine(int line, int spaces);
    void autoIndentLine(int line);
    void autoIndentRange(int startLine, int endLine);
    std::string toLowerCase(const std::string& str);

    // Marks
    struct MarkLocation
    {
        std::string filename;
        int line = 0;
        int col = 0;
    };

    std::map<char, MarkLocation> marks;
    void setMark(char mark);
    void jumpToMark(char mark);

    // Misc utilities (for mode handlers)
    void goToFile();
    void showFileInfo();
    void forceFullRedraw();
    void setNeedsFullRedraw(bool value);
    bool needsFullRedrawValue() const;
    void setLastCursorScreenPosition(int row, int col);
    void executeOneNormalCommand(int key);

    // Command history
    std::vector<std::string> commandHistory;
    int commandHistoryIndex = -1;
    std::string commandInput;
    bool commandPopupActive = false;
    std::vector<std::string> commandPopupAll;
    std::vector<int> commandPopupFiltered;
    int commandPopupCursor = 0;
    int commandPopupOffset = 0;
    std::string commandPopupQuery;
    bool commandHistorySearchActive = false;
    std::string commandHistorySearchQueryValue;
    std::string commandHistorySearchOriginal;
    std::vector<int> commandHistorySearchMatches;
    int commandHistorySearchCursor = 0;
    int commandHistorySearchOffset = 0;
    std::optional<std::string> commandHistoryUp();
    std::optional<std::string> commandHistoryDown();
    std::vector<std::string> getSetCompletions(std::string_view prefix);
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
    const std::string& commandHistorySearchQuery() const;
    void drawCommandPopup(std::string& output) const;
    void drawCommandHistoryPopup(std::string& output) const;
    std::vector<std::string> getCommandCompletions(std::string_view prefix);
    std::vector<std::string> getCommandCompletions(std::string_view prefix,
                                                   Mode mode);
    std::vector<std::string> getHelpCompletions(std::string_view prefix);
    std::vector<std::string> getPathCompletions(std::string_view path);
    std::vector<std::string> getPathCompletionsRecursive(std::string_view path);
    std::vector<std::string> getLocPathCompletions(std::string_view path);

    // Syntax highlighting (TokenType enum and Token struct are now in
    // token_type.h)
    std::vector<JumpLocation> jumpBackStack;
    std::vector<JumpLocation> jumpForwardStack;

    bool isFileType(FileType type) const;
    std::optional<FileType> getFileType() const;

    template <FileType Type>
    bool isFileType() const
    {
        return isFileType(Type);
    }

    std::optional<FileType> getFormatterFileType() const;

    bool formatBuffer();
    void pythonLintBuffer();

    std::vector<Token> tokenizeLine(const std::string& line,
                                    bool& inBlockComment, bool& inTomlMultiline,
                                    char& tomlQuote, bool& inMarkupFence,
                                    char& markupFenceChar,
                                    bool inCppMethodContext = false,
                                    bool inCppFunctionContext = false,
                                    bool inCppParamListContext = false) const;
    std::string getColorCode(TokenType type) const;
    void renderLineWithSyntax(std::string& output, const std::string& line,
                              int start, int len, int fileRow);
    void ensureMlangTokensLoaded() const;
    std::optional<TokenType> lookupMlangTokenType(std::string_view word) const;

    // Undo/Redo (now per-buffer, accessed through currentBuffer)
    void saveState();
    void undo();
    void redo();
    bool lastFindTill{false};

    void setProjectRoot(const std::string& path)
    {
        projectRoot = path;
    }

    const std::string& getProjectRoot() const
    {
        return projectRoot;
    }

    editor::statemachine::ModeStateMachine* getModeStateMachine()
    {
        return modeStateMachine.get();
    }

    const editor::statemachine::ModeStateMachine* getModeStateMachine() const
    {
        return modeStateMachine.get();
    }

private:
    void paintTaskStatus();
    void pumpTasks();

    struct TaskStatusSlot
    {
        int row = 0;
        int col = 0;
        Mode mode = NORMAL;
        bool valid = false;
        bool hidden = false;
    };
    TaskStatusSlot taskStatusSlot_;
    bool taskStatusShown_ = false;
    bool taskStatusOverlay_ = false;
    TaskQueue::TaskId foregroundTask_ = 0;
    bool tasksInteractive_ = false;
    // Geometry the picker background was last painted for (valid=false
    // forces a repaint).
    struct PickerBackground
    {
        bool valid = false;
        int screenRows = 0;
        int screenCols = 0;
        int boxTop = 0;
        int boxLeft = 0;
        int boxRows = 0;
        int boxCols = 0;
    } pickerBackground_;
    bool pickerBackgroundAdopted_ = false;
    // While set, isFileType() answers for this mask instead of the current
    // buffer (used to tokenize preview files).
    std::optional<uint64_t> fileTypeMaskOverride_;
    std::string lastLineJumpMessage_;
    bool skipNextScreenClear_ = false;
    void runNextStartupStep();
    struct StartupStep
    {
        std::string label;
        std::function<void()> run;
    };
    std::deque<StartupStep> startupSteps_;
    std::deque<StartupStep> lateStartupSteps_;
    int startupStepsTotal_ = 0;
    int startupStepsDone_ = 0;
    std::string startupStepLabel_;
    std::chrono::steady_clock::time_point startupStepStarted_{};
    bool firstFrameDrawn_ = false;
    std::string lspWaitLabel_;
    std::chrono::steady_clock::time_point lspWaitStarted_{};
    std::chrono::steady_clock::time_point lspWorkStarted_{};
    std::chrono::steady_clock::time_point lastTaskPaint_{};
    std::uint64_t lastTaskRevision_ = 0;

    friend class EditorSettingsController;
    friend class EditorBufferController;
    friend class EditorCommandController;
    friend class EditorDefinitionController;
    friend class EditorDrawingController;
    friend class EditorEditingController;
    friend class EditorFileController;
    friend class EditorGitController;
    friend class EditorCursorController;
    friend class EditorIndentController;
    friend class EditorLspController;
    friend class EditorModeController;
    friend class EditorOperatorController;
    friend class EditorReferencesController;
    friend class EditorSplitController;
    friend class EditorVisualController;
    std::string resolveEditorPathString(const std::string& input) const;
#ifdef UVIM_TESTING
    struct TestTag
    {
    };

    Editor(TestTag tag, int rows, int cols);
#endif
    bool formatBufferForSave();
    void enableClangdLspImpl(bool enable, const std::string& compileCommandsDir,
                             const std::string& clangdPath,
                             const std::string& queryDriverAllowList);
    bool isClangdLspEnabledImpl() const;
    void enableRobotLspImpl(bool enable, const std::string& robotLspPath,
                            const std::vector<std::string>& robotLspArgs);
    bool isRobotLspEnabledImpl() const;
    void enablePythonLspImpl(bool enable, const std::string& pythonLspPath,
                             const std::vector<std::string>& pythonLspArgs);
    bool isPythonLspEnabledImpl() const;
    void enableMlangLspImpl(bool enable, const std::string& mlangLspPath,
                            const std::vector<std::string>& mlangLspArgs);
    bool isMlangLspEnabledImpl() const;
    void enableHtmlLspImpl(bool enable, const std::string& htmlLspPath,
                           const std::vector<std::string>& htmlLspArgs);
    bool isHtmlLspEnabledImpl() const;
    void enableCssLspImpl(bool enable, const std::string& cssLspPath,
                          const std::vector<std::string>& cssLspArgs);
    bool isCssLspEnabledImpl() const;
    void enableJsonLspImpl(bool enable, const std::string& jsonLspPath,
                           const std::vector<std::string>& jsonLspArgs);
    bool isJsonLspEnabledImpl() const;
    void enableTsLspImpl(bool enable, const std::string& tsLspPath,
                         const std::vector<std::string>& tsLspArgs);
    bool isTsLspEnabledImpl() const;
    void enableCSharpLspImpl(bool enable, const std::string& csharpLspPath,
                             const std::vector<std::string>& csharpLspArgs);
    bool isCSharpLspEnabledImpl() const;
    bool handleSetCommandImpl(std::string_view cmd);
    void createNewBufferImpl();
    void updateCurrentBufferPointersImpl();
    void clearCurrentBufferPointersImpl();
    bool hasBufferImpl() const;
    void ensureBufferForModeImpl(Mode mode);
    void switchToBufferImpl(int index);
    void nextBufferImpl();
    void previousBufferImpl();
    void moveBufferLeftImpl();
    void moveBufferRightImpl();
    void closeCurrentBufferImpl();
    void listBuffersImpl();
    int findBufferByFilenameImpl(const std::string& filename);
    void saveBufferStateImpl();
    void restoreBufferStateImpl();
    std::optional<std::string> commandHistoryUpImpl();
    std::optional<std::string> commandHistoryDownImpl();
    void startCommandPopupImpl();
    void cancelCommandPopupImpl();
    void updateCommandPopupImpl(std::string_view query);
    void moveCommandPopupCursorImpl(int delta);
    bool isCommandPopupActiveImpl() const;
    std::optional<std::string> commandPopupSelectionImpl() const;
    void startCommandHistorySearchImpl(std::string_view seed);
    std::string cancelCommandHistorySearchImpl();
    std::string acceptCommandHistorySearchImpl();
    void updateCommandHistorySearchQueryImpl(std::string_view query);
    void moveCommandHistorySearchCursorImpl(int delta);
    bool isCommandHistorySearchActiveImpl() const;
    const std::string& commandHistorySearchQueryImpl() const;
    void drawCommandPopupImpl(std::string& output) const;
    void drawCommandHistoryPopupImpl(std::string& output) const;
    std::vector<std::string> getCommandCompletionsImpl(std::string_view prefix);
    std::vector<std::string> getCommandCompletionsImpl(std::string_view prefix,
                                                       Mode mode);
    std::vector<std::string> getHelpCompletionsImpl(std::string_view prefix);
    std::vector<std::string> getSetCompletionsImpl(std::string_view prefix);
    std::vector<std::string> getPathCompletionsImpl(std::string_view path);
    std::vector<std::string>
    getPathCompletionsRecursiveImpl(std::string_view path);
    std::vector<std::string> getLocPathCompletionsImpl(std::string_view path);
    void insertTabImpl();
    void toggleCaseImpl();
    void joinLinesImpl();
    void insertLineAboveImpl();
    void insertLineBelowImpl();
    void deleteCurrentLineImpl();
    void deleteToLineStartImpl();
    void deleteCharAtCursorImpl();
    void deleteCharBeforeCursorImpl();
    void deleteWordBackwardImpl();
    void deleteWordImpl();
    void yankWordImpl();
    void handleBackspaceImpl();
    void replaceCharAtCursorImpl(char c);
    void beginChangeRecordingImpl(int count = 1);
    void recordChangeKeyImpl(int key);
    void deferChangeRecordingCommitImpl();
    void commitChangeRecordingImpl();
    void cancelChangeRecordingImpl();
    void finishChangeRecordingIfDeferredImpl();
    bool isRecordingChangeImpl() const;
    bool isReplayingChangeImpl() const;
    int readKeyRecordedImpl();
    void repeatLastChangeImpl(int times = 1);
    void insertUtf8CharImpl(int c);
    void indentCurrentLineImpl();
    void dedentCurrentLineImpl();
    void handleLinewiseOperatorImpl(char op, int count);
    void enterOperatorPendingImpl(char op);
    bool getTextObjectRangeImpl(char objChar, bool around, int& outStartY,
                                int& outStartX, int& outEndY, int& outEndX);
    void applyOperatorToRangeImpl(char op, int startY, int startX, int endY,
                                  int endX);
    std::string toLowerCaseImpl(const std::string& str);
    int getLineIndentImpl(int line);
    void indentLineImpl(int line, int spaces);
    void autoIndentLineImpl(int line);
    void autoIndentRangeImpl(int startLine, int endLine);
    void updateClangFormatIndentWidthImpl();
    int indentWidthForBracesImpl() const;
    bool braceNewLineForAutoBracesImpl() const;
    void commentLinesImpl(int startY, int endY);
    void commentBlockImpl(int startY, int endY);
    void commentBlockRangeImpl(int startY, int startX, int endY, int endX);
    bool insertTodoLineCommentImpl(int row);
    bool insertTodoBlockCommentImpl(int row);
    void startVisualModeImpl();
    void startVisualLineModeImpl();
    void startVisualBlockModeImpl();
    void updateVisualSelectionImpl();
    void updateVisualBlockSelectionImpl();
    bool isInSelectionImpl(int row, int col);
    bool isInVisualBlockImpl(int row, int col);
    void getSelectionBoundsImpl(int& startY, int& startX, int& endY, int& endX);
    void getVisualBlockBoundsImpl(int& startY, int& startX, int& endY,
                                  int& endX);
    void setVisualRangeImpl();
    void swapVisualEndsImpl();
    void swapVisualBlockCornerImpl();
    void prepareBlockInsertImpl(bool atEnd);
    void indentSelectionImpl();
    void dedentSelectionImpl();
    void autoIndentSelectionImpl();
    void lowercaseSelectionImpl();
    void uppercaseSelectionImpl();
    void toggleCaseSelectionImpl();
    void yankLineSelectionImpl();
    void deleteLineSelectionImpl();
    void indentLineSelectionImpl();
    void dedentLineSelectionImpl();
    void autoIndentLineSelectionImpl();
    int tabBarRowsImpl() const;
    int contentRowsImpl() const;
    PaneLayout getPaneLayoutImpl(int pane) const;
    void setPanePointersImpl(int pane);
    void enableSplitImpl(bool vertical);
    void enableBrowserSplitImpl(bool vertical);
    void closeSplitImpl();
    void switchPaneImpl();
    void switchPaneDirectionImpl(int dx, int dy);
    void syncBufferStateFromActivePaneImpl();
    void initSplitPanesFromBufferImpl();
    void switchToBufferInActivePaneImpl(int index);
    bool canSplitImpl() const;
    void rebuildSplitPaneLayouts() const;
    void collectSplitLeafPanes(int node, std::vector<int>& panes) const;
    int findSplitLeafNode(int node, int pane) const;
    int findSplitParentNode(int node, int child) const;
    int firstSplitLeafPane(int node) const;
#ifdef UVIM_TESTING
public:
    std::function<bool()> formatOnSaveTestHook;
    void setModeStateMachineForTests(
        std::unique_ptr<editor::statemachine::ModeStateMachine> sm);
    static std::string
    testInferTsTypeForIdentifier(const std::vector<std::string>& lines,
                                 std::string_view ident, int startY);
    static std::string testInferTsTypeFromArrayMethodLine(
        std::string_view line, std::string_view param,
        const std::vector<std::string>& lines, int lineNo);
    static bool testFindTsTypeDefinition(const std::vector<std::string>& lines,
                                         std::string_view typeName, int& outY,
                                         int& outX);
    static bool testFindTsMemberInType(const std::vector<std::string>& lines,
                                       int typeStartY, std::string_view member,
                                       int& outY, int& outX);
    static std::string testResolveJsTsModule(const std::string& fromFile,
                                             std::string_view module);
    static std::string testResolveMlangModule(const std::string& fromFile,
                                              std::string_view modulePath);
    static int testCountLocForFile(const std::string& filepath);

private:
#endif
    std::unique_ptr<editor::statemachine::ModeStateMachine> modeStateMachine;
    std::unique_ptr<EditorSettingsController> settingsController;
    std::unique_ptr<EditorBufferController> bufferController;
    std::unique_ptr<EditorCommandController> commandController;
    std::unique_ptr<EditorDefinitionController> definitionController;
    std::unique_ptr<EditorDrawingController> drawingController;
    std::unique_ptr<EditorCursorController> cursorController;
    std::unique_ptr<EditorEditingController> editingController;
    std::unique_ptr<EditorFileController> fileController;
    std::unique_ptr<EditorFileTypeController> fileTypeController;
    std::unique_ptr<EditorGitController> gitController;
    std::unique_ptr<EditorIndentController> indentController;
    std::unique_ptr<EditorLspController> lspController;
    std::unique_ptr<EditorModeController> modeController;
    std::unique_ptr<EditorOperatorController> operatorController;
    std::unique_ptr<EditorReferencesController> referencesController;
    std::unique_ptr<EditorSplitController> splitController;
    std::unique_ptr<EditorVisualController> visualController;
};
