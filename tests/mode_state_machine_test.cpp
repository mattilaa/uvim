// Unit tests for ModeStateMachine
//
// These tests verify the editor mode state machine behavior.
// Since testing the full mode machine requires the Editor class,
// we test only the generic StateMachine behavior here.
//
// ModeStateMachine inherits from the generic StateMachine template,
// so these tests validate the inheritance works correctly.

#include "state_machine.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
// Test State Machine (mirrors ModeStateMachine structure)
// ============================================================================

namespace mode_test
{

// Simplified context that doesn't need Editor
struct TestModeContext
{
    std::string commandBuffer;
    int repeatCount = 0;
    int cursorX = 0;
    int cursorY = 0;
    std::string statusMessage;

    void setStatusMessage(const std::string& msg)
    {
        statusMessage = msg;
    }
};

// Forward declare states
struct NormalMode;
struct InsertMode;
struct VisualMode;
struct CommandMode;
struct OperatorPendingMode;

using TestModeState = std::variant<NormalMode, InsertMode, VisualMode,
                                   CommandMode, OperatorPendingMode>;

// State definitions
struct NormalMode
{
    static constexpr const char* name()
    {
        return "NORMAL";
    }
    void on_enter(TestModeContext& ctx)
    {
        ctx.statusMessage = "-- NORMAL --";
    }
    void on_exit(TestModeContext&) {}
    std::optional<TestModeState> handle(TestModeContext& ctx,
                                        const KeyEvent& event);
};

struct InsertMode
{
    static constexpr const char* name()
    {
        return "INSERT";
    }
    void on_enter(TestModeContext& ctx)
    {
        ctx.statusMessage = "-- INSERT --";
    }
    void on_exit(TestModeContext&) {}
    std::optional<TestModeState> handle(TestModeContext& ctx,
                                        const KeyEvent& event);
};

struct VisualMode
{
    static constexpr const char* name()
    {
        return "VISUAL";
    }
    void on_enter(TestModeContext& ctx)
    {
        ctx.statusMessage = "-- VISUAL --";
    }
    void on_exit(TestModeContext&) {}
    std::optional<TestModeState> handle(TestModeContext& ctx,
                                        const KeyEvent& event);
};

struct CommandMode
{
    static constexpr const char* name()
    {
        return "COMMAND";
    }
    std::string input;
    void on_enter(TestModeContext& ctx)
    {
        ctx.statusMessage = ":";
        input.clear();
    }
    void on_exit(TestModeContext&) {}
    std::optional<TestModeState> handle(TestModeContext& ctx,
                                        const KeyEvent& event);
};

struct OperatorPendingMode
{
    static constexpr const char* name()
    {
        return "OP_PENDING";
    }
    char op = 0;
    int count = 1;

    explicit OperatorPendingMode(char o = 0, int c = 1) : op(o), count(c) {}

    void on_enter(TestModeContext& ctx)
    {
        ctx.statusMessage = std::string(1, op);
    }
    void on_exit(TestModeContext&) {}
    std::optional<TestModeState> handle(TestModeContext& ctx,
                                        const KeyEvent& event);
};

// Handle implementations
inline std::optional<TestModeState> NormalMode::handle(TestModeContext& ctx,
                                                       const KeyEvent& event)
{
    int c = event.key;

    // Accumulate repeat count
    if(c >= '1' && c <= '9' && ctx.repeatCount == 0)
    {
        ctx.repeatCount = c - '0';
        return std::nullopt;
    }
    if(c >= '0' && c <= '9' && ctx.repeatCount > 0)
    {
        ctx.repeatCount = ctx.repeatCount * 10 + (c - '0');
        return std::nullopt;
    }

    int count = ctx.repeatCount > 0 ? ctx.repeatCount : 1;
    ctx.repeatCount = 0;

    switch(c)
    {
    case 'i':
        return InsertMode{};
    case 'v':
        return VisualMode{};
    case ':':
        return CommandMode{};
    case 'd':
    case 'y':
    case 'c':
        return OperatorPendingMode{static_cast<char>(c), count};
    case 27: // ESC
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

inline std::optional<TestModeState> InsertMode::handle(TestModeContext&,
                                                       const KeyEvent& event)
{
    if(event.key == 27) // ESC
        return NormalMode{};
    return std::nullopt;
}

inline std::optional<TestModeState> VisualMode::handle(TestModeContext&,
                                                       const KeyEvent& event)
{
    if(event.key == 27) // ESC
        return NormalMode{};
    return std::nullopt;
}

inline std::optional<TestModeState> CommandMode::handle(TestModeContext& ctx,
                                                        const KeyEvent& event)
{
    if(event.key == 27) // ESC
        return NormalMode{};
    if(event.key == 13) // Enter
    {
        ctx.statusMessage = "Executed: " + input;
        return NormalMode{};
    }
    if(event.key >= 32 && event.key < 127)
    {
        input += static_cast<char>(event.key);
        ctx.statusMessage = ":" + input;
    }
    return std::nullopt;
}

inline std::optional<TestModeState>
OperatorPendingMode::handle(TestModeContext& ctx, const KeyEvent& event)
{
    int c = event.key;

    if(c == 27) // ESC
        return NormalMode{};

    // Same operator twice = linewise (dd, yy, cc)
    if(c == op)
    {
        ctx.statusMessage =
            std::string(1, op) + std::string(1, op) + " executed";
        return NormalMode{};
    }

    // Motion
    if(c == 'w' || c == 'b' || c == 'e' || c == '$' || c == '0')
    {
        ctx.statusMessage = std::string(1, op) +
                            std::string(1, static_cast<char>(c)) + " executed";
        return NormalMode{};
    }

    return std::nullopt;
}

// Type alias for the test state machine
using TestModeStateMachineBase =
    StateMachine<TestModeState, TestModeContext, KeyEvent>;

class TestModeStateMachine : public TestModeStateMachineBase
{
public:
    explicit TestModeStateMachine(TestModeContext ctx)
        : TestModeStateMachineBase(std::move(ctx), NormalMode{})
    {
    }

    template <typename InitialState>
    TestModeStateMachine(TestModeContext ctx, InitialState&& initial)
        : TestModeStateMachineBase(std::move(ctx),
                                   std::forward<InitialState>(initial))
    {
    }

    void dispatch(int key)
    {
        TestModeStateMachineBase::dispatch(KeyEvent{key});
    }

    using TestModeStateMachineBase::dispatch;
};

} // namespace mode_test

// ============================================================================
// Test Fixtures
// ============================================================================

class ModeStateMachineTest : public ::testing::Test
{
protected:
    mode_test::TestModeContext ctx;
    std::unique_ptr<mode_test::TestModeStateMachine> sm;

    void SetUp() override
    {
        sm = std::make_unique<mode_test::TestModeStateMachine>(ctx);
    }

    void TearDown() override
    {
        sm.reset();
    }

    void pressKey(int key)
    {
        sm->dispatch(key);
    }

    void pressKeys(const std::string& keys)
    {
        for(char c : keys)
        {
            pressKey(static_cast<int>(c));
        }
    }
};

// ============================================================================
// Inheritance Verification Tests
// ============================================================================

TEST_F(ModeStateMachineTest, InheritsFromStateMachine)
{
    static_assert(
        std::is_base_of_v<mode_test::TestModeStateMachineBase,
                          mode_test::TestModeStateMachine>,
        "TestModeStateMachine must inherit from TestModeStateMachineBase");

    mode_test::TestModeStateMachineBase* basePtr = sm.get();
    EXPECT_STREQ(basePtr->currentStateName(), "NORMAL");
}

TEST_F(ModeStateMachineTest, BaseClassMethodsWork)
{
    EXPECT_TRUE(sm->isIn<mode_test::NormalMode>());
    EXPECT_STREQ(sm->currentStateName(), "NORMAL");

    auto* normalMode = sm->getState<mode_test::NormalMode>();
    EXPECT_NE(normalMode, nullptr);
}

TEST_F(ModeStateMachineTest, DispatchKeyEventDirectly)
{
    sm->dispatch(KeyEvent{'i'});
    EXPECT_TRUE(sm->isIn<mode_test::InsertMode>());
}

TEST_F(ModeStateMachineTest, DispatchIntConvenience)
{
    sm->dispatch('i');
    EXPECT_TRUE(sm->isIn<mode_test::InsertMode>());
}

TEST_F(ModeStateMachineTest, TransitionToFromBase)
{
    sm->transitionTo(mode_test::CommandMode{});
    EXPECT_TRUE(sm->isIn<mode_test::CommandMode>());
}

// ============================================================================
// Mode Transition Tests
// ============================================================================

TEST_F(ModeStateMachineTest, StartsInNormalMode)
{
    EXPECT_STREQ(sm->currentStateName(), "NORMAL");
    EXPECT_TRUE(sm->isIn<mode_test::NormalMode>());
}

TEST_F(ModeStateMachineTest, InsertModeOnI)
{
    pressKey('i');

    EXPECT_STREQ(sm->currentStateName(), "INSERT");
    EXPECT_TRUE(sm->isIn<mode_test::InsertMode>());
}

TEST_F(ModeStateMachineTest, EscapeFromInsertReturnsToNormal)
{
    pressKey('i');
    EXPECT_TRUE(sm->isIn<mode_test::InsertMode>());

    pressKey(27); // ESC

    EXPECT_TRUE(sm->isIn<mode_test::NormalMode>());
}

TEST_F(ModeStateMachineTest, VisualModeOnV)
{
    pressKey('v');

    EXPECT_TRUE(sm->isIn<mode_test::VisualMode>());
    EXPECT_STREQ(sm->currentStateName(), "VISUAL");
}

TEST_F(ModeStateMachineTest, EscapeFromVisualReturnsToNormal)
{
    pressKey('v');
    EXPECT_TRUE(sm->isIn<mode_test::VisualMode>());

    pressKey(27); // ESC

    EXPECT_TRUE(sm->isIn<mode_test::NormalMode>());
}

TEST_F(ModeStateMachineTest, CommandModeOnColon)
{
    pressKey(':');

    EXPECT_TRUE(sm->isIn<mode_test::CommandMode>());
    EXPECT_STREQ(sm->currentStateName(), "COMMAND");
}

TEST_F(ModeStateMachineTest, EscapeFromCommandReturnsToNormal)
{
    pressKey(':');
    EXPECT_TRUE(sm->isIn<mode_test::CommandMode>());

    pressKey(27); // ESC

    EXPECT_TRUE(sm->isIn<mode_test::NormalMode>());
}

// ============================================================================
// Operator Pending Mode Tests
// ============================================================================

TEST_F(ModeStateMachineTest, DeleteOperatorEntersOpPending)
{
    pressKey('d');
    EXPECT_TRUE(sm->isIn<mode_test::OperatorPendingMode>());
}

TEST_F(ModeStateMachineTest, YankOperatorEntersOpPending)
{
    pressKey('y');
    EXPECT_TRUE(sm->isIn<mode_test::OperatorPendingMode>());
}

TEST_F(ModeStateMachineTest, ChangeOperatorEntersOpPending)
{
    pressKey('c');
    EXPECT_TRUE(sm->isIn<mode_test::OperatorPendingMode>());
}

TEST_F(ModeStateMachineTest, EscapeFromOpPendingReturnsToNormal)
{
    pressKey('d');
    EXPECT_TRUE(sm->isIn<mode_test::OperatorPendingMode>());

    pressKey(27); // ESC

    EXPECT_TRUE(sm->isIn<mode_test::NormalMode>());
}

TEST_F(ModeStateMachineTest, OpPendingStoresOperator)
{
    pressKey('d');

    auto* opMode = sm->getState<mode_test::OperatorPendingMode>();
    ASSERT_NE(opMode, nullptr);
    EXPECT_EQ(opMode->op, 'd');
}

TEST_F(ModeStateMachineTest, DoubleOperatorCompletesAndReturns)
{
    pressKey('d');
    EXPECT_TRUE(sm->isIn<mode_test::OperatorPendingMode>());

    pressKey('d'); // dd

    EXPECT_TRUE(sm->isIn<mode_test::NormalMode>());
    EXPECT_EQ(sm->context().statusMessage, "dd executed");
}

TEST_F(ModeStateMachineTest, OperatorWithMotionCompletes)
{
    pressKey('d');
    pressKey('w'); // dw

    EXPECT_TRUE(sm->isIn<mode_test::NormalMode>());
    EXPECT_EQ(sm->context().statusMessage, "dw executed");
}

// ============================================================================
// Repeat Count Tests
// ============================================================================

TEST_F(ModeStateMachineTest, RepeatCountAccumulates)
{
    pressKey('5');
    pressKey('3');

    EXPECT_TRUE(sm->isIn<mode_test::NormalMode>());
    EXPECT_EQ(sm->context().repeatCount, 53);
}

TEST_F(ModeStateMachineTest, RepeatCountWithOperator)
{
    pressKey('3');
    pressKey('d');

    EXPECT_TRUE(sm->isIn<mode_test::OperatorPendingMode>());

    auto* opMode = sm->getState<mode_test::OperatorPendingMode>();
    ASSERT_NE(opMode, nullptr);
    EXPECT_EQ(opMode->count, 3);
}

TEST_F(ModeStateMachineTest, RepeatCountResetAfterCommand)
{
    pressKey('5');
    pressKey('i'); // 5i - insert mode (count reset)
    pressKey(27);  // back to normal

    EXPECT_EQ(sm->context().repeatCount, 0);
}

// ============================================================================
// Command Mode Input Tests
// ============================================================================

TEST_F(ModeStateMachineTest, CommandModeAcceptsInput)
{
    pressKey(':');
    pressKeys("write");

    EXPECT_TRUE(sm->isIn<mode_test::CommandMode>());

    auto* cmdMode = sm->getState<mode_test::CommandMode>();
    ASSERT_NE(cmdMode, nullptr);
    EXPECT_EQ(cmdMode->input, "write");
}

TEST_F(ModeStateMachineTest, CommandModeEnterExecutes)
{
    pressKey(':');
    pressKeys("quit");
    pressKey(13); // Enter

    EXPECT_TRUE(sm->isIn<mode_test::NormalMode>());
    EXPECT_EQ(sm->context().statusMessage, "Executed: quit");
}

// ============================================================================
// Status Message Tests
// ============================================================================

TEST_F(ModeStateMachineTest, StatusMessageUpdatesOnModeChange)
{
    EXPECT_EQ(sm->context().statusMessage, "-- NORMAL --");

    pressKey('i');
    EXPECT_EQ(sm->context().statusMessage, "-- INSERT --");

    pressKey(27);
    EXPECT_EQ(sm->context().statusMessage, "-- NORMAL --");
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(ModeStateMachineTest, RapidModeChanges)
{
    for(int i = 0; i < 100; i++)
    {
        pressKey('i');
        EXPECT_TRUE(sm->isIn<mode_test::InsertMode>());
        pressKey(27);
        EXPECT_TRUE(sm->isIn<mode_test::NormalMode>());
    }
}

TEST_F(ModeStateMachineTest, UnknownKeyInNormalMode)
{
    pressKey(1000);
    EXPECT_TRUE(sm->isIn<mode_test::NormalMode>());
}

// ============================================================================
// Integration-style Tests
// ============================================================================

TEST_F(ModeStateMachineTest, TypicalEditingSession)
{
    // Start in normal
    EXPECT_TRUE(sm->isIn<mode_test::NormalMode>());

    // Enter insert mode
    pressKey('i');
    EXPECT_TRUE(sm->isIn<mode_test::InsertMode>());

    // Back to normal
    pressKey(27);
    EXPECT_TRUE(sm->isIn<mode_test::NormalMode>());

    // Delete word
    pressKey('d');
    pressKey('w');
    EXPECT_TRUE(sm->isIn<mode_test::NormalMode>());

    // Visual select
    pressKey('v');
    EXPECT_TRUE(sm->isIn<mode_test::VisualMode>());

    // Back to normal
    pressKey(27);
    EXPECT_TRUE(sm->isIn<mode_test::NormalMode>());

    // Execute command
    pressKey(':');
    pressKeys("w");
    pressKey(13);
    EXPECT_TRUE(sm->isIn<mode_test::NormalMode>());
}
