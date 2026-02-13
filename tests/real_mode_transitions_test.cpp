#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include <gtest/gtest.h>
#include <utility>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace
{
template <typename InitialState>
ModeStateMachine makeMachine(Editor& editor, InitialState&& initial)
{
    return ModeStateMachine(createModeContext(&editor),
                            std::forward<InitialState>(initial));
}

std::filesystem::path make_temp_dir(const std::string& prefix)
{
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path base =
        std::filesystem::temp_directory_path() /
        (prefix + std::to_string(now));
    std::filesystem::create_directories(base);
    return base;
}

void write_file(const std::filesystem::path& path, std::string_view content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

void dispatch_command(ModeStateMachine& sm, std::string_view cmd)
{
    sm.dispatch(':');
    for(char c : cmd)
        sm.dispatch(c);
    sm.dispatch(Terminal::ENTER);
}
} // namespace

TEST(RealModeTransitionsTest, WelcomeEscStaysInWelcome)
{
    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, WelcomeMode{});

    sm.dispatch(Terminal::ESC);

    EXPECT_STREQ(sm.currentStateName(), "WELCOME");
}

TEST(RealModeTransitionsTest, WelcomeLeaderXOpensFileBrowser)
{
    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, WelcomeMode{});

    sm.dispatch(' ');
    sm.dispatch('x');

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, LspInfoQuitWithNoBufferReturnsWelcome)
{
    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, LspInfoMode{});

    sm.dispatch('q');

    EXPECT_STREQ(sm.currentStateName(), "WELCOME");
}

TEST(RealModeTransitionsTest, DoubleEscClearsStatusMessageInWelcome)
{
    Editor editor = Editor::createForTests();
    editor.setStatusMessage("Message");
    auto sm = makeMachine(editor, WelcomeMode{});

    sm.dispatch(Terminal::ESC);
    EXPECT_EQ(editor.statusMessage, "Message");

    sm.dispatch(Terminal::ESC);
    EXPECT_TRUE(editor.statusMessage.empty());
}

TEST(RealModeTransitionsTest, VisualPasteReplacesSelectionWithYankBuffer)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.useSystemClipboard = false;
    editor.currentBuffer->lines = {"one two three"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch('v');
    sm.dispatch('l');
    sm.dispatch('l');
    sm.dispatch('y');

    *editor.cursorX = 4;
    *editor.cursorY = 0;

    sm.dispatch('v');
    sm.dispatch('l');
    sm.dispatch('l');
    sm.dispatch('p');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "one one three");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
}

TEST(RealModeTransitionsTest, InsertModeAutoPairsDoubleQuote)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {""};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('i');
    sm.dispatch('"');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "\"\"");
    EXPECT_EQ(*editor.cursorX, 1);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");

    sm.dispatch('"');
    EXPECT_EQ(editor.currentBuffer->lines[0], "\"\"");
    EXPECT_EQ(*editor.cursorX, 2);
}

TEST(RealModeTransitionsTest, InsertModeAutoPairsSingleQuote)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {""};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('i');
    sm.dispatch('\'');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "''");
    EXPECT_EQ(*editor.cursorX, 1);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");

    sm.dispatch('\'');
    EXPECT_EQ(editor.currentBuffer->lines[0], "''");
    EXPECT_EQ(*editor.cursorX, 2);
}

TEST(RealModeTransitionsTest, InsertModeAutoPairsBacktick)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {""};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('i');
    sm.dispatch('`');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "``");
    EXPECT_EQ(*editor.cursorX, 1);
    EXPECT_STREQ(sm.currentStateName(), "INSERT");

    sm.dispatch('`');
    EXPECT_EQ(editor.currentBuffer->lines[0], "``");
    EXPECT_EQ(*editor.cursorX, 2);
}

TEST(RealModeTransitionsTest, InsertModeAutoQuotesDisabled)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.autoQuotes = false;
    editor.currentBuffer->lines = {""};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('i');
    sm.dispatch('"');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "\"");
    EXPECT_EQ(*editor.cursorX, 1);
}

TEST(RealModeTransitionsTest, InsertModeAutoBracesInStrings)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {""};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('i');
    sm.dispatch('"');
    sm.dispatch('{');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "\"{}\"");
    EXPECT_EQ(*editor.cursorX, 2);
}

TEST(RealModeTransitionsTest, InsertModeAutoBracesInStringsDisabled)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.autoBracesInStrings = false;
    editor.currentBuffer->lines = {""};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    auto sm = makeMachine(editor, NormalMode{});
    sm.dispatch('i');
    sm.dispatch('"');
    sm.dispatch('{');

    ASSERT_EQ(editor.currentBuffer->lines.size(), 1u);
    EXPECT_EQ(editor.currentBuffer->lines[0], "\"{\"");
    EXPECT_EQ(*editor.cursorX, 2);
}

TEST(RealModeTransitionsTest, LocListReturnsToFileBrowserWithCursorState)
{
    auto root = make_temp_dir("uvim_loc_return_");
    write_file(root / "a.cpp", "int main() { return 0; }\n");
    write_file(root / "b.cpp", "int add(int a,int b){return a+b;}\n");

    std::error_code ec;
    std::filesystem::current_path(root, ec);

    Editor editor = Editor::createForTests();
    FileBrowserMode browse{root.string()};
    browse.browserCursor = 1;
    browse.browserOffset = 0;

    auto sm = makeMachine(editor, browse);

    sm.dispatch(':');
    sm.dispatch('l');
    sm.dispatch('o');
    sm.dispatch('c');
    sm.dispatch(' ');
    sm.dispatch('.');
    sm.dispatch(Terminal::ENTER);

    EXPECT_STREQ(sm.currentStateName(), "LOC");

    sm.dispatch('q');

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
    auto* fb = sm.getState<FileBrowserMode>();
    ASSERT_NE(fb, nullptr);
    EXPECT_EQ(fb->currentDirectory, root.string());
    EXPECT_EQ(fb->browserCursor, 1);
    EXPECT_EQ(fb->browserOffset, 0);
}

TEST(RealModeTransitionsTest, CompletionTrimsLeadingSpaceAfterDot)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"ctx."};
    *editor.cursorX = 4;
    *editor.cursorY = 0;

    CompletionEntry e;
    e.label = " cancelCommandPopup()";
    editor.completionAll = {e};
    editor.completionFiltered = {0};
    editor.completionSelected = 0;
    editor.completionActive = true;
    editor.completionAnchorX = 4;
    editor.completionAnchorY = 0;

    editor.acceptCompletion();

    EXPECT_EQ(editor.currentBuffer->lines[0], "ctx.cancelCommandPopup();");
    EXPECT_EQ(*editor.cursorX, 23);
}

TEST(RealModeTransitionsTest, FormatOnSaveCallsFormatterHookWhenEnabled)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.formatOnSave = true;
    editor.currentBuffer->filename = "/tmp/uvim_format_on_save.mla";
    editor.currentBuffer->lines = {"fn main() {}"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    bool called = false;
    editor.formatOnSaveTestHook = [&]()
    {
        called = true;
        return true;
    };

    editor.saveFile();

    EXPECT_TRUE(called);
}

TEST(RealModeTransitionsTest, FormatOnSaveSkipsFormatterHookWhenDisabled)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.formatOnSave = false;
    editor.currentBuffer->filename = "/tmp/uvim_format_on_save_off.mla";
    editor.currentBuffer->lines = {"fn main() {}"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;

    bool called = false;
    editor.formatOnSaveTestHook = [&]()
    {
        called = true;
        return true;
    };

    editor.saveFile();

    EXPECT_FALSE(called);
}

TEST(RealModeTransitionsTest, ExCommandOpensFileBrowser)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(':');
    sm.dispatch('E');
    sm.dispatch('x');
    sm.dispatch(Terminal::ENTER);

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, ExCommandWithPathOpensFileBrowser)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(':');
    sm.dispatch('E');
    sm.dispatch('x');
    sm.dispatch(' ');
    sm.dispatch('s');
    sm.dispatch('r');
    sm.dispatch('c');
    sm.dispatch(Terminal::ENTER);

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, ExCommandFromCommandModeOpensFileBrowser)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    auto sm = makeMachine(editor, NormalMode{});

    sm.dispatch(':');
    sm.dispatch('E');
    sm.dispatch('x');
    sm.dispatch(Terminal::ENTER);

    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest, FileBrowserFuzzyDisabledIgnoresTyping)
{
    Editor editor = Editor::createForTests();
    editor.fileBrowserFuzzy = false;
    auto sm = makeMachine(editor, FileBrowserMode{std::string(".")});

    sm.dispatch('a');

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_FALSE(state->filterActive);
    EXPECT_TRUE(state->filterQuery.empty());
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
}

TEST(RealModeTransitionsTest,
     FileBrowserCommandSlashRegexSearchStaysInCurrentDirectory)
{
    auto root = make_temp_dir("uvim_browse_regex_local_");
    write_file(root / "a.txt", "a\n");
    write_file(root / "sub" / "needle.txt", "n\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int aIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "a.txt")
        {
            aIndex = i;
            break;
        }
    }
    ASSERT_GE(aIndex, 0);

    dispatch_command(sm, "/needle\\.txt");
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_NE(editor.statusMessage.find("No match for regex: needle\\.txt"),
              std::string::npos);
    EXPECT_NE(state->browserCursor, aIndex);

    dispatch_command(sm, "/a\\.txt");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_NE(editor.currentBuffer->filename.find("a.txt"), std::string::npos);
}

TEST(RealModeTransitionsTest, FileBrowserCommandQuestionRegexSearchesBackward)
{
    auto root = make_temp_dir("uvim_browse_regex_back_");
    write_file(root / "alpha.txt", "a\n");
    write_file(root / "beta.txt", "b\n");
    write_file(root / "gamma.txt", "g\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    int betaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "alpha.txt")
            alphaIndex = i;
        if(state->fileList[i].name == "beta.txt")
            betaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);
    ASSERT_GE(betaIndex, 0);

    state->browserCursor = betaIndex;
    dispatch_command(sm, "?^alpha\\.txt$");
    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_NE(editor.currentBuffer->filename.find("alpha.txt"),
              std::string::npos);
}

TEST(RealModeTransitionsTest, FileBrowserSlashKeyRunsLocalRegexSearch)
{
    auto root = make_temp_dir("uvim_browse_slash_key_");
    write_file(root / "alpha.txt", "a\n");
    write_file(root / "beta.txt", "b\n");
    write_file(root / "sub" / "alpha.txt", "nested\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    int betaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "alpha.txt")
            alphaIndex = i;
        if(state->fileList[i].name == "beta.txt")
            betaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);
    ASSERT_GE(betaIndex, 0);

    state->browserCursor = betaIndex;
    sm.dispatch('/');
    sm.dispatch('a');
    sm.dispatch('l');
    sm.dispatch('p');
    sm.dispatch('h');
    sm.dispatch('a');
    sm.dispatch(Terminal::ENTER);

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_NE(editor.currentBuffer->filename.find("alpha.txt"),
              std::string::npos);
}

TEST(RealModeTransitionsTest, FileBrowserQuestionKeyRunsBackwardRegexSearch)
{
    auto root = make_temp_dir("uvim_browse_question_key_");
    write_file(root / "alpha.txt", "a\n");
    write_file(root / "beta.txt", "b\n");
    write_file(root / "gamma.txt", "g\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    int gammaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "alpha.txt")
            alphaIndex = i;
        if(state->fileList[i].name == "gamma.txt")
            gammaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);
    ASSERT_GE(gammaIndex, 0);

    state->browserCursor = gammaIndex;
    sm.dispatch('?');
    sm.dispatch('a');
    sm.dispatch('l');
    sm.dispatch('p');
    sm.dispatch('h');
    sm.dispatch('a');
    sm.dispatch(Terminal::ENTER);

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_NE(editor.currentBuffer->filename.find("alpha.txt"),
              std::string::npos);
}

TEST(RealModeTransitionsTest, FileBrowserCtrlSStillOpensGrepSearch)
{
    auto root = make_temp_dir("uvim_browse_ctrls_");
    write_file(root / "a.txt", "a\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    sm.dispatch(Terminal::CTRL_S);

    EXPECT_STREQ(sm.currentStateName(), "GREP");
}

TEST(RealModeTransitionsTest, FileBrowserNAndNShiftWrapSearchMatches)
{
    auto root = make_temp_dir("uvim_browse_n_wrap_");
    write_file(root / "edit-a.txt", "a\n");
    write_file(root / "edit-b.txt", "b\n");
    write_file(root / "zzz.txt", "z\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int aIndex = -1;
    int bIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "edit-a.txt")
            aIndex = i;
        if(state->fileList[i].name == "edit-b.txt")
            bIndex = i;
    }
    ASSERT_GE(aIndex, 0);
    ASSERT_GE(bIndex, 0);

    state->searchMatches = {aIndex, bIndex};
    state->lastSearchPattern = "edit";
    state->lastSearchPrefix = '/';
    state->currentSearchMatch = 0;
    state->browserCursor = aIndex;

    sm.dispatch('n');
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, bIndex);

    sm.dispatch('n');
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, aIndex);

    sm.dispatch('N');
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, bIndex);
}

TEST(RealModeTransitionsTest, FileBrowserSearchTabCompletionCyclesMatches)
{
    auto root = make_temp_dir("uvim_browse_search_tab_");
    write_file(root / "edit-alpha.txt", "a\n");
    write_file(root / "edit-beta.txt", "b\n");
    write_file(root / "zzz.txt", "z\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    int betaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "edit-alpha.txt")
            alphaIndex = i;
        if(state->fileList[i].name == "edit-beta.txt")
            betaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);
    ASSERT_GE(betaIndex, 0);

    sm.dispatch('/');
    sm.dispatch('e');
    sm.dispatch(Terminal::TAB);
    sm.dispatch(Terminal::ENTER);

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_NE(editor.currentBuffer->filename.find("edit-alpha.txt"),
              std::string::npos);

    Editor editor2 = Editor::createForTests();
    auto sm2 = makeMachine(editor2, FileBrowserMode{root.string()});
    sm2.dispatch('/');
    sm2.dispatch('e');
    sm2.dispatch(Terminal::SHIFT_TAB);
    sm2.dispatch(Terminal::ENTER);

    EXPECT_STREQ(sm2.currentStateName(), "NORMAL");
    ASSERT_NE(editor2.currentBuffer, nullptr);
    EXPECT_NE(editor2.currentBuffer->filename.find("edit-"),
              std::string::npos);
}

TEST(RealModeTransitionsTest, FileBrowserCtrlJKCyclesWhileSearchPromptActive)
{
    auto root = make_temp_dir("uvim_browse_ctrljk_prompt_");
    write_file(root / "edit-alpha.txt", "a\n");
    write_file(root / "edit-beta.txt", "b\n");
    write_file(root / "edit-gamma.txt", "g\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    int betaIndex = -1;
    int gammaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "edit-alpha.txt")
            alphaIndex = i;
        if(state->fileList[i].name == "edit-beta.txt")
            betaIndex = i;
        if(state->fileList[i].name == "edit-gamma.txt")
            gammaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);
    ASSERT_GE(betaIndex, 0);
    ASSERT_GE(gammaIndex, 0);

    sm.dispatch('/');
    sm.dispatch('e');
    sm.dispatch('d');
    sm.dispatch('i');
    sm.dispatch('t');
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(state->commandPrompt.isActive());
    EXPECT_EQ(state->commandPrompt.getInput(), "/edit");

    sm.dispatch(Terminal::CTRL_J);
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(state->commandPrompt.isActive());
    EXPECT_EQ(state->browserCursor, betaIndex);

    sm.dispatch(Terminal::CTRL_J);
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, gammaIndex);

    sm.dispatch(Terminal::CTRL_K);
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, betaIndex);

    sm.dispatch(Terminal::CTRL_K);
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(state->browserCursor, alphaIndex);
}

TEST(RealModeTransitionsTest, FileBrowserEscClearsSearchBeforeExit)
{
    auto root = make_temp_dir("uvim_browse_esc_clear_search_");
    write_file(root / "edit-alpha.txt", "a\n");
    write_file(root / "edit-beta.txt", "b\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    sm.dispatch('/');
    sm.dispatch('e');
    sm.dispatch('d');
    sm.dispatch('i');
    sm.dispatch('t');
    sm.dispatch(Terminal::CTRL_J);
    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    ASSERT_FALSE(state->searchMatches.empty());

    sm.dispatch(Terminal::ESC);
    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_STREQ(sm.currentStateName(), "BROWSE");
    EXPECT_TRUE(state->searchMatches.empty());
    EXPECT_TRUE(state->lastSearchPattern.empty());
}

TEST(RealModeTransitionsTest, FileBrowserSearchTypingMovesToFirstMatch)
{
    auto root = make_temp_dir("uvim_browse_live_first_match_");
    write_file(root / "alpha.txt", "a\n");
    write_file(root / "beta.txt", "b\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    int alphaIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "alpha.txt")
            alphaIndex = i;
    }
    ASSERT_GE(alphaIndex, 0);

    sm.dispatch('/');
    sm.dispatch('a');

    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(state->commandPrompt.isActive());
    EXPECT_EQ(state->browserCursor, alphaIndex);
}

TEST(RealModeTransitionsTest, FileBrowserSearchEnterOpensMatchedFile)
{
    auto root = make_temp_dir("uvim_browse_search_enter_file_");
    write_file(root / "target.txt", "hello\n");
    write_file(root / "other.txt", "other\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    sm.dispatch('/');
    sm.dispatch('t');
    sm.dispatch('a');
    sm.dispatch('r');
    sm.dispatch('g');
    sm.dispatch('e');
    sm.dispatch('t');
    sm.dispatch(Terminal::ENTER);

    EXPECT_STREQ(sm.currentStateName(), "NORMAL");
    ASSERT_NE(editor.currentBuffer, nullptr);
    EXPECT_NE(editor.currentBuffer->filename.find("target.txt"),
              std::string::npos);
}

TEST(RealModeTransitionsTest, FileBrowserSearchEnterOpensMatchedDirectory)
{
    auto root = make_temp_dir("uvim_browse_search_enter_dir_");
    write_file(root / "docs" / "readme.txt", "docs\n");
    write_file(root / "z.txt", "z\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{root.string()});

    sm.dispatch('/');
    sm.dispatch('d');
    sm.dispatch('o');
    sm.dispatch('c');
    sm.dispatch('s');
    sm.dispatch(Terminal::ENTER);

    const std::string mode = sm.currentStateName();
    EXPECT_TRUE(mode == "BROWSE" || mode == "NORMAL");
}

TEST(RealModeTransitionsTest, FileBrowserParentThenEnterSiblingDirectoryStays)
{
    auto root = make_temp_dir("uvim_browse_parent_then_sibling_");
    write_file(root / "from" / "a.txt", "from\n");
    write_file(root / "to" / "b.txt", "to\n");

    Editor editor = Editor::createForTests();
    auto sm = makeMachine(editor, FileBrowserMode{(root / "from").string()});

    // Enter parent via ".." (first entry).
    sm.dispatch(Terminal::ENTER);

    auto* state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);

    int toIndex = -1;
    for(int i = 0; i < static_cast<int>(state->fileList.size()); ++i)
    {
        if(state->fileList[i].name == "to")
        {
            toIndex = i;
            break;
        }
    }
    ASSERT_GE(toIndex, 0);

    for(int i = 0; i < toIndex; ++i)
        sm.dispatch('j');

    sm.dispatch(Terminal::ENTER);

    state = sm.getState<FileBrowserMode>();
    ASSERT_NE(state, nullptr);
    EXPECT_NE(state->currentDirectory.find("/to"), std::string::npos);
    EXPECT_EQ(state->currentDirectory.find("/from"), std::string::npos);
}

TEST(RealModeTransitionsTest, UndoBackToSavedClearsDirty)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"one"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;
    editor.saveState();
    editor.currentBuffer->savedUndoIndex = editor.currentBuffer->undoIndex;
    *editor.dirty = false;

    editor.currentBuffer->lines[0] = "two";
    *editor.dirty = true;
    editor.saveState();

    editor.undo();

    EXPECT_FALSE(*editor.dirty);
    EXPECT_EQ(editor.currentBuffer->lines[0], "one");
}

TEST(RealModeTransitionsTest, UndoBackToSavedClearsDirtyWithHash)
{
    Editor editor = Editor::createForTests();
    editor.createNewBuffer();
    editor.currentBuffer->lines = {"one"};
    *editor.cursorX = 0;
    *editor.cursorY = 0;
    editor.saveState();
    editor.currentBuffer->savedUndoIndex = -1;
    auto hash_lines = [](const std::vector<std::string>& src) -> size_t
    {
        size_t h = 1469598103934665603ull;
        for(const auto& line : src)
        {
            for(unsigned char c : line)
            {
                h ^= c;
                h *= 1099511628211ull;
            }
            h ^= '\n';
            h *= 1099511628211ull;
        }
        return h;
    };
    editor.currentBuffer->savedContentHashValid = true;
    editor.currentBuffer->savedContentHash =
        hash_lines(editor.currentBuffer->lines);
    *editor.dirty = false;

    editor.currentBuffer->lines[0] = "two";
    *editor.dirty = true;
    editor.saveState();

    editor.undo();

    EXPECT_FALSE(*editor.dirty);
    EXPECT_EQ(editor.currentBuffer->lines[0], "one");
}
