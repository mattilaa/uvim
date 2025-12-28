// Unit tests for the generic StateMachine template
//
// These tests verify the core state machine functionality independent
// of any specific application (editor modes, etc.)

#include "state_machine.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>

// ============================================================================
// Test Fixtures - Simple state machine for testing
// ============================================================================

namespace test_sm
{

// Simple context for testing
struct TestContext
{
    std::string log;
    int counter = 0;

    void append(const std::string& msg)
    {
        if(!log.empty())
            log += ",";
        log += msg;
    }
};

// Simple event
struct TestEvent
{
    char action;
    explicit TestEvent(char a) : action(a) {}
};

// Forward declare variant first, then define all states completely
// before any method uses std::optional<TestState>

struct StateA;
struct StateB;
struct StateC;

// State variant - forward declared states are OK here
using TestState = std::variant<StateA, StateB, StateC>;

// Now define all states COMPLETELY before any handle() method is defined
// This is required because std::optional<std::variant<...>> needs complete
// types

// State A - Initial state
struct StateA
{
    static constexpr const char* name()
    {
        return "A";
    }

    void on_enter(TestContext& ctx)
    {
        ctx.append("enter_A");
    }
    void on_exit(TestContext& ctx)
    {
        ctx.append("exit_A");
    }

    // Declare but don't define inline - definition comes after all states
    std::optional<TestState> handle(TestContext& ctx, const TestEvent& event);
};

// State B
struct StateB
{
    int data = 0;

    static constexpr const char* name()
    {
        return "B";
    }

    void on_enter(TestContext& ctx)
    {
        ctx.append("enter_B");
    }
    void on_exit(TestContext& ctx)
    {
        ctx.append("exit_B");
    }

    std::optional<TestState> handle(TestContext& ctx, const TestEvent& event);
};

// State C - Terminal state (always stays)
struct StateC
{
    static constexpr const char* name()
    {
        return "C";
    }

    void on_enter(TestContext& ctx)
    {
        ctx.append("enter_C");
    }
    void on_exit(TestContext& ctx)
    {
        ctx.append("exit_C");
    }

    std::optional<TestState> handle(TestContext& ctx, const TestEvent& event);
};

// Now that all states are complete, define the handle methods
inline std::optional<TestState> StateA::handle(TestContext& ctx,
                                               const TestEvent& event)
{
    ctx.append(std::string("A_handle_") + event.action);
    if(event.action == 'b')
        return StateB{};
    if(event.action == 'c')
        return StateC{};
    return std::nullopt;
}

inline std::optional<TestState> StateB::handle(TestContext& ctx,
                                               const TestEvent& event)
{
    ctx.append(std::string("B_handle_") + event.action);
    if(event.action == 'a')
        return StateA{};
    if(event.action == 'c')
        return StateC{};
    if(event.action == '+')
    {
        data++;
        ctx.counter++;
    }
    return std::nullopt;
}

inline std::optional<TestState> StateC::handle(TestContext& ctx,
                                               const TestEvent& event)
{
    ctx.append(std::string("C_handle_") + event.action);
    return std::nullopt; // Never transitions
}

using TestMachine = StateMachine<TestState, TestContext, TestEvent>;

} // namespace test_sm

// ============================================================================
// Basic State Machine Tests
// ============================================================================

class StateMachineTest : public ::testing::Test
{
protected:
    test_sm::TestContext ctx;
};

TEST_F(StateMachineTest, InitialStateEntered)
{
    test_sm::TestMachine sm(ctx, test_sm::StateA{});

    EXPECT_EQ(sm.currentStateName(), "A");
    EXPECT_EQ(ctx.log, "enter_A");
}

TEST_F(StateMachineTest, InitialStateWithDifferentStart)
{
    test_sm::TestMachine sm(ctx, test_sm::StateB{});

    EXPECT_EQ(sm.currentStateName(), "B");
    EXPECT_EQ(ctx.log, "enter_B");
}

TEST_F(StateMachineTest, DispatchWithoutTransition)
{
    test_sm::TestMachine sm(ctx, test_sm::StateA{});
    ctx.log.clear();

    sm.dispatch(test_sm::TestEvent{'x'}); // Unknown event, no transition

    EXPECT_EQ(sm.currentStateName(), "A");
    EXPECT_EQ(ctx.log, "A_handle_x");
}

TEST_F(StateMachineTest, TransitionFromAToB)
{
    test_sm::TestMachine sm(ctx, test_sm::StateA{});
    ctx.log.clear();

    sm.dispatch(test_sm::TestEvent{'b'});

    EXPECT_EQ(sm.currentStateName(), "B");
    EXPECT_EQ(ctx.log, "A_handle_b,exit_A,enter_B");
}

TEST_F(StateMachineTest, TransitionFromAToC)
{
    test_sm::TestMachine sm(ctx, test_sm::StateA{});
    ctx.log.clear();

    sm.dispatch(test_sm::TestEvent{'c'});

    EXPECT_EQ(sm.currentStateName(), "C");
    EXPECT_EQ(ctx.log, "A_handle_c,exit_A,enter_C");
}

TEST_F(StateMachineTest, TransitionChainABCA)
{
    test_sm::TestMachine sm(ctx, test_sm::StateA{});
    ctx.log.clear();

    sm.dispatch(test_sm::TestEvent{'b'}); // A -> B
    sm.dispatch(test_sm::TestEvent{'c'}); // B -> C
    sm.dispatch(test_sm::TestEvent{'a'}); // C stays (terminal)

    EXPECT_EQ(sm.currentStateName(), "C");
}

TEST_F(StateMachineTest, TransitionBackAndForth)
{
    test_sm::TestMachine sm(ctx, test_sm::StateA{});
    ctx.log.clear();

    sm.dispatch(test_sm::TestEvent{'b'}); // A -> B
    sm.dispatch(test_sm::TestEvent{'a'}); // B -> A
    sm.dispatch(test_sm::TestEvent{'b'}); // A -> B

    EXPECT_EQ(sm.currentStateName(), "B");
}

TEST_F(StateMachineTest, IsInChecksCurrentState)
{
    test_sm::TestMachine sm(ctx, test_sm::StateA{});

    EXPECT_TRUE(sm.isIn<test_sm::StateA>());
    EXPECT_FALSE(sm.isIn<test_sm::StateB>());
    EXPECT_FALSE(sm.isIn<test_sm::StateC>());

    sm.dispatch(test_sm::TestEvent{'b'});

    EXPECT_FALSE(sm.isIn<test_sm::StateA>());
    EXPECT_TRUE(sm.isIn<test_sm::StateB>());
    EXPECT_FALSE(sm.isIn<test_sm::StateC>());
}

TEST_F(StateMachineTest, GetStateReturnsPointer)
{
    test_sm::TestMachine sm(ctx, test_sm::StateA{});

    EXPECT_NE(sm.getState<test_sm::StateA>(), nullptr);
    EXPECT_EQ(sm.getState<test_sm::StateB>(), nullptr);
}

TEST_F(StateMachineTest, StateDataIsMutableThroughGetState)
{
    test_sm::TestMachine sm(ctx, test_sm::StateB{});

    auto* stateB = sm.getState<test_sm::StateB>();
    ASSERT_NE(stateB, nullptr);
    EXPECT_EQ(stateB->data, 0);

    sm.dispatch(test_sm::TestEvent{'+'}); // Increments data

    EXPECT_EQ(stateB->data, 1);
    EXPECT_EQ(ctx.counter, 1);
}

TEST_F(StateMachineTest, TransitionToForcesStateChange)
{
    test_sm::TestMachine sm(ctx, test_sm::StateA{});
    ctx.log.clear();

    sm.transitionTo(test_sm::StateC{});

    EXPECT_EQ(sm.currentStateName(), "C");
    EXPECT_EQ(ctx.log, "exit_A,enter_C");
}

TEST_F(StateMachineTest, ContextIsAccessible)
{
    test_sm::TestMachine sm(ctx, test_sm::StateA{});

    sm.context().counter = 42;

    EXPECT_EQ(ctx.counter, 42);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(StateMachineTest, MultipleDispatchesInSameState)
{
    test_sm::TestMachine sm(ctx, test_sm::StateB{});
    ctx.log.clear();

    for(int i = 0; i < 5; i++)
    {
        sm.dispatch(test_sm::TestEvent{'+'}); // Stays in B, increments
    }

    EXPECT_EQ(sm.currentStateName(), "B");
    EXPECT_EQ(ctx.counter, 5);

    auto* stateB = sm.getState<test_sm::StateB>();
    ASSERT_NE(stateB, nullptr);
    EXPECT_EQ(stateB->data, 5);
}

TEST_F(StateMachineTest, TransitionResetsStateData)
{
    test_sm::TestMachine sm(ctx, test_sm::StateB{});

    sm.dispatch(test_sm::TestEvent{'+'}); // data = 1
    sm.dispatch(test_sm::TestEvent{'+'}); // data = 2
    sm.dispatch(test_sm::TestEvent{'a'}); // B -> A
    sm.dispatch(test_sm::TestEvent{'b'}); // A -> B (new StateB instance)

    auto* stateB = sm.getState<test_sm::StateB>();
    ASSERT_NE(stateB, nullptr);
    EXPECT_EQ(stateB->data, 0); // Fresh state
}

// ============================================================================
// Nested/Hierarchical State Machine Tests
// ============================================================================

namespace nested_test
{

struct InnerContext
{
    std::string innerLog;
    void append(const std::string& msg)
    {
        if(!innerLog.empty())
            innerLog += ",";
        innerLog += msg;
    }
};

struct InnerEvent
{
    int value;
};

// Forward declare
struct InnerStateX;
struct InnerStateY;

using InnerState = std::variant<InnerStateX, InnerStateY>;

// Define states completely
struct InnerStateX
{
    static constexpr const char* name()
    {
        return "X";
    }
    void on_enter(InnerContext& ctx)
    {
        ctx.append("enter_X");
    }
    void on_exit(InnerContext& ctx)
    {
        ctx.append("exit_X");
    }
    std::optional<InnerState> handle(InnerContext& ctx, const InnerEvent& e);
};

struct InnerStateY
{
    static constexpr const char* name()
    {
        return "Y";
    }
    void on_enter(InnerContext& ctx)
    {
        ctx.append("enter_Y");
    }
    void on_exit(InnerContext& ctx)
    {
        ctx.append("exit_Y");
    }
    std::optional<InnerState> handle(InnerContext& ctx, const InnerEvent& e);
};

// Define handle methods after all states are complete
inline std::optional<InnerState> InnerStateX::handle(InnerContext&,
                                                     const InnerEvent& e)
{
    if(e.value > 0)
        return InnerStateY{};
    return std::nullopt;
}

inline std::optional<InnerState> InnerStateY::handle(InnerContext&,
                                                     const InnerEvent& e)
{
    if(e.value < 0)
        return InnerStateX{};
    return std::nullopt;
}

using InnerMachine = StateMachine<InnerState, InnerContext, InnerEvent>;

// Outer state machine
struct OuterContext
{
    std::string outerLog;
};

struct OuterEvent
{
    char action;
    int innerValue;
};

struct OuterStateNormal;
struct OuterStateWithSub;

using OuterState = std::variant<OuterStateNormal, OuterStateWithSub>;

struct OuterStateNormal
{
    static constexpr const char* name()
    {
        return "NORMAL";
    }
    void on_enter(OuterContext&) {}
    void on_exit(OuterContext&) {}
    std::optional<OuterState> handle(OuterContext&, const OuterEvent& e);
};

struct OuterStateWithSub
{
    InnerContext innerCtx;
    std::optional<InnerMachine> innerMachine;

    static constexpr const char* name()
    {
        return "WITH_SUB";
    }

    void on_enter(OuterContext&)
    {
        innerMachine.emplace(innerCtx, InnerStateX{});
    }

    void on_exit(OuterContext&)
    {
        innerMachine.reset();
    }

    std::optional<OuterState> handle(OuterContext&, const OuterEvent& e);
};

inline std::optional<OuterState> OuterStateNormal::handle(OuterContext&,
                                                          const OuterEvent& e)
{
    if(e.action == 's')
        return OuterStateWithSub{};
    return std::nullopt;
}

inline std::optional<OuterState> OuterStateWithSub::handle(OuterContext&,
                                                           const OuterEvent& e)
{
    if(e.action == 'n')
        return OuterStateNormal{};
    if(e.action == 'i' && innerMachine)
    {
        innerMachine->dispatch(InnerEvent{e.innerValue});
    }
    return std::nullopt;
}

using OuterMachine = StateMachine<OuterState, OuterContext, OuterEvent>;

} // namespace nested_test

TEST(NestedStateMachineTest, InnerMachineCreatedOnEnter)
{
    nested_test::OuterContext ctx;
    nested_test::OuterMachine sm(ctx, nested_test::OuterStateNormal{});

    sm.dispatch(nested_test::OuterEvent{'s', 0}); // -> WithSub

    auto* withSub = sm.getState<nested_test::OuterStateWithSub>();
    ASSERT_NE(withSub, nullptr);
    ASSERT_TRUE(withSub->innerMachine.has_value());
    EXPECT_EQ(withSub->innerMachine->currentStateName(), "X");
}

TEST(NestedStateMachineTest, InnerMachineReceivesEvents)
{
    nested_test::OuterContext ctx;
    nested_test::OuterMachine sm(ctx, nested_test::OuterStateNormal{});

    sm.dispatch(nested_test::OuterEvent{'s', 0}); // -> WithSub
    sm.dispatch(nested_test::OuterEvent{'i', 1}); // Inner: X -> Y

    auto* withSub = sm.getState<nested_test::OuterStateWithSub>();
    ASSERT_NE(withSub, nullptr);
    EXPECT_EQ(withSub->innerMachine->currentStateName(), "Y");
}

TEST(NestedStateMachineTest, InnerMachineDestroyedOnExit)
{
    nested_test::OuterContext ctx;
    nested_test::OuterMachine sm(ctx, nested_test::OuterStateNormal{});

    sm.dispatch(nested_test::OuterEvent{'s', 0}); // -> WithSub
    sm.dispatch(nested_test::OuterEvent{'n', 0}); // -> Normal

    EXPECT_TRUE(sm.isIn<nested_test::OuterStateNormal>());
}

// ============================================================================
// Event Types Tests
// ============================================================================

TEST(EventTypesTest, KeyEventHoldsKey)
{
    KeyEvent e(42);
    EXPECT_EQ(e.key, 42);
}

TEST(EventTypesTest, CharEventHoldsChar)
{
    CharEvent e('x');
    EXPECT_EQ(e.ch, 'x');
}

// ============================================================================
// Performance Test (optional - can be slow)
// ============================================================================

TEST(StateMachinePerformanceTest, ManyTransitions)
{
    test_sm::TestContext ctx;
    test_sm::TestMachine sm(ctx, test_sm::StateA{});

    // Perform many transitions
    for(int i = 0; i < 10000; i++)
    {
        sm.dispatch(test_sm::TestEvent{'b'}); // A -> B
        sm.dispatch(test_sm::TestEvent{'a'}); // B -> A
    }

    EXPECT_TRUE(sm.isIn<test_sm::StateA>());
}

TEST(StateMachinePerformanceTest, ManyDispatchesNoTransition)
{
    test_sm::TestContext ctx;
    test_sm::TestMachine sm(ctx, test_sm::StateC{});

    // C is terminal, never transitions
    for(int i = 0; i < 100000; i++)
    {
        sm.dispatch(test_sm::TestEvent{'x'});
    }

    EXPECT_TRUE(sm.isIn<test_sm::StateC>());
}
