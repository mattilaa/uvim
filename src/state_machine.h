#pragma once

#include "key_enums.h"
#include <optional>
#include <type_traits>
#include <variant>

// ============================================================================
// Generic State Machine Template
// ============================================================================
//
// Usage:
//   1. Define your states as structs with:
//      - static constexpr const char* name() { return "STATE_NAME"; }
//      - void on_enter(Context& ctx)
//      - void on_exit(Context& ctx)
//      - std::optional<StateVariant> handle(Context& ctx, const Event& event)
//
//   2. Create a variant of all states: using MyStates = std::variant<A, B, C>;
//
//   3. Create the machine: StateMachine<MyStates, MyContext, MyEvent> sm(ctx,
//   InitialState{});
//
// ============================================================================

template <typename StateVariant, typename Context, typename Event>
class StateMachine
{
public:
    // Construct with initial state
    template <typename InitialState>
    explicit StateMachine(Context ctx, InitialState&& initial)
        : currentState_(std::forward<InitialState>(initial)),
          context_(std::move(ctx))
    {
        // Enter initial state
        std::visit([this](auto& state) { state.on_enter(context_); },
                   currentState_);
    }

    // Dispatch an event to the current state
    void dispatch(const Event& event)
    {
        auto newState = std::visit(
            [this, &event](auto& state) -> std::optional<StateVariant>
            { return state.handle(context_, event); }, currentState_);

        if(newState)
        {
            transition(std::move(*newState));
        }
    }

    // Get current state name
    const char* currentStateName() const
    {
        return std::visit([](const auto& state) { return state.name(); },
                          currentState_);
    }

    // Check if in a specific state
    template <typename State>
    bool isIn() const
    {
        return std::holds_alternative<State>(currentState_);
    }

    // Access current state (for state-specific operations)
    template <typename State>
    State* getState()
    {
        return std::get_if<State>(&currentState_);
    }

    template <typename State>
    const State* getState() const
    {
        return std::get_if<State>(&currentState_);
    }

    // Force transition to a specific state
    template <typename State>
    void transitionTo(State&& newState)
    {
        transition(StateVariant{std::forward<State>(newState)});
    }

    // Access context
    Context& context()
    {
        return context_;
    }
    const Context& context() const
    {
        return context_;
    }

    // Access current state variant
    StateVariant& state()
    {
        return currentState_;
    }
    const StateVariant& state() const
    {
        return currentState_;
    }

private:
    void transition(StateVariant&& newState)
    {
        // Exit current state
        std::visit([this](auto& state) { state.on_exit(context_); },
                   currentState_);

        currentState_ = std::move(newState);

        // Enter new state
        std::visit([this](auto& state) { state.on_enter(context_); },
                   currentState_);
    }

    StateVariant currentState_;
    Context context_;
};

// ============================================================================
// Helper: State with sub-state machine
// ============================================================================
//
// For states that have their own internal state machine (like OperatorPending
// which waits for motion/text-object), inherit from this:
//
// struct MyComplexState : SubStateMachine<MySubStates, MySubContext,
// MySubEvent>
// {
//     // Parent state interface
//     std::optional<ParentVariant> handle(ParentContext& ctx, const
//     ParentEvent& e)
//     {
//         // Delegate to sub-machine or handle directly
//     }
// };
//
// ============================================================================

template <typename SubStateVariant, typename SubContext, typename SubEvent>
class SubStateMachine
{
public:
    using SubMachine = StateMachine<SubStateVariant, SubContext, SubEvent>;

protected:
    std::optional<SubMachine> subMachine_;

    template <typename InitialSubState>
    void initSubMachine(SubContext ctx, InitialSubState&& initial)
    {
        subMachine_.emplace(std::move(ctx),
                            std::forward<InitialSubState>(initial));
    }

    void clearSubMachine()
    {
        subMachine_.reset();
    }

    bool hasSubMachine() const
    {
        return subMachine_.has_value();
    }

    SubMachine& subMachine()
    {
        return *subMachine_;
    }
};

// ============================================================================
// Common Event Types
// ============================================================================

// Character event (for text input)
struct CharEvent
{
    char ch;
    explicit CharEvent(char c) : ch(c) {}
};

// Special events
struct EscapeEvent
{
};
struct EnterEvent
{
};
struct BackspaceEvent
{
};
struct TabEvent
{
};

// Composite event that can be any of the above
using InputEvent = std::variant<int, CharEvent, EscapeEvent, EnterEvent,
                                BackspaceEvent, TabEvent>;

// ============================================================================
// State Concept (C++20) - Documents expected interface
// ============================================================================

#if __cplusplus >= 202002L
template <typename S, typename Context, typename Event, typename StateVariant>
concept State = requires(S s, Context& ctx, const Event& e) {
    { S::name() } -> std::convertible_to<const char*>;
    { s.on_enter(ctx) } -> std::same_as<void>;
    { s.on_exit(ctx) } -> std::same_as<void>;
    { s.handle(ctx, e) } -> std::same_as<std::optional<StateVariant>>;
};
#endif
