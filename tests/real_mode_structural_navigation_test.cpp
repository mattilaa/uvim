#include "real_mode_test_utils.h"

using namespace uvim_test;

namespace
{
class StructuralNavigationModeTest : public ::testing::Test
{
protected:
    Editor editor = Editor::createForTests();
    std::unique_ptr<ModeStateMachine> machine;

    void SetUp() override
    {
        editor.createNewBuffer();
        set_buffer_filename(editor, "/tmp/uvim_structure.py");
        *editor.lines =
            {"# needle",
             "def first():",
             "    value = compute()",
             "    consume(value)",
             "",
             "def second():",
             "    other = consume()",
             "# needle"};
        machine = std::make_unique<
            ModeStateMachine>(createModeContext(&editor), NormalMode{});
    }

    void keys(std::string_view sequence)
    {
        for(unsigned char c : sequence)
            machine->dispatch(static_cast<int>(c));
    }

    void position(int row, int column)
    {
        EXPECT_EQ(*editor.cursorY, row);
        EXPECT_EQ(*editor.cursorX, column);
    }
};
} // namespace

TEST_F(
    StructuralNavigationModeTest,
    FunctionJumpsRepeatBothDirectionsAndKeepHistory)
{
    keys("]f");
    position(1, 4);
    keys("n");
    position(5, 4);
    keys("N");
    position(1, 4);
    EXPECT_EQ(editor.jumpBackStack.size(), 3u);
    keys("\x0f"); // Ctrl-O returns to the prior function.
    position(5, 4);
    keys("\x09"); // Ctrl-I returns forward.
    position(1, 4);
    EXPECT_EQ(*editor.wantedX, 4);
}

TEST_F(StructuralNavigationModeTest, CountsClampAtBoundariesWithoutWrapping)
{
    keys("2]f");
    position(5, 4);
    const size_t jumps = editor.jumpBackStack.size();
    keys("9n");
    position(5, 4);
    EXPECT_EQ(editor.jumpBackStack.size(), jumps);
    keys("9N");
    position(1, 4);
    keys("[f");
    position(1, 4);
    EXPECT_EQ(editor.repeatCount, 0);
}

TEST_F(
    StructuralNavigationModeTest,
    LocalStepsUseSourceOrderAndStayInTheirFunction)
{
    keys("]f]s");
    position(2, 4); // declaration
    keys("n");
    position(2, 12); // initializer call on the same line
    keys("n");
    position(3, 4);
    keys("n");
    position(3, 4); // cannot enter second()
    keys("N");
    position(2, 12);
    keys("3N");
    position(2, 4);
    keys("[s");
    position(2, 4);
}

TEST_F(StructuralNavigationModeTest, EscapeAndNewSearchRestoreSearchRepeats)
{
    editor.performSearch("needle", true);
    *editor.cursorY = 0;
    *editor.cursorX = 0;
    keys("]f");
    position(1, 4);
    keys("\x1b");
    EXPECT_EQ(editor.searchQuery, "needle");
    keys("n");
    position(7, 2);
    keys("N");
    position(0, 2);
    keys("]f");
    dispatch_command(*machine, "/needle");
    EXPECT_STREQ(machine->currentStateName(), "NORMAL");
    keys("n");
    position(0, 2);
}

TEST_F(
    StructuralNavigationModeTest,
    CancelledPrefixDoesNotSwallowFollowingMovement)
{
    keys("]\x1bj");
    position(1, 0);
    keys("2]s");
    position(2, 12);
}

TEST_F(StructuralNavigationModeTest, BufferSwitchDoesNotCarryStructuralRepeat)
{
    keys("]f");
    editor.createNewBuffer();
    set_buffer_filename(editor, "/tmp/uvim_other.py");
    *editor.lines = {"# needle", "# needle"};
    editor.performSearch("needle", true);
    keys("n");
    position(1, 2);
}
