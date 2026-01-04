// ============================================================================
// Add these declarations to editor.h
// ============================================================================

// In the public section, add these member variables:

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

// In the public section, add these method declarations:

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

// Helper to read a single line from a file
std::string readLineFromFile(const std::string& path, int lineNum);

// ============================================================================
// In draw() method, add handling for REFERENCES mode:
// ============================================================================
/*
    // In the draw() or refreshScreen() method, add:
    if(hasReferences() && currentMode == REFERENCES) // or check state machine
   state
    {
        drawReferences();
        return;
    }
*/

// ============================================================================
// Add REFERENCES to the Mode enum if using legacy mode system:
// ============================================================================
/*
    enum Mode
    {
        NORMAL,
        INSERT,
        VISUAL,
        VISUAL_LINE,
        VISUAL_BLOCK,
        COMMAND,
        SEARCH,
        FILE_BROWSER,
        FUZZY_FIND,
        BUFFER_BROWSER,
        GREP_SEARCH,
        OPERATOR_PENDING,
        REFERENCES    // <-- Add this
    };
*/
