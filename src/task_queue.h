#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Background job queue for heavy operations (ripgrep, git, formatters,
// compilers, indexing).
//
// Ordering rules:
//  - Jobs on the same channel run one at a time in submission order, so a
//    newer job can never overtake or override an older one on that channel.
//  - Jobs on different channels run concurrently (up to maxParallel).
//  - Completion callbacks run on the main thread in submission order, from
//    pollCompletions(); a finished job waits until all older jobs finish.
//
// Work functions run on a worker thread: they must not touch the terminal or
// editor state. Capture inputs by value and hand results to `done`.
class TaskQueue
{
public:
    using TaskId = std::uint64_t;
    using Clock = std::chrono::steady_clock;

    // Handed to the work function; safe to use from the worker thread.
    class Context
    {
    public:
        void progress(long long done, long long total = -1);
        void detail(std::string text);
        bool cancelled() const;

    private:
        friend class TaskQueue;
        Context(TaskQueue& queue, TaskId id) : queue_(queue), id_(id)
        {
        }
        TaskQueue& queue_;
        TaskId id_;
    };

    using Work = std::function<void(Context&)>;
    using Done = std::function<void()>;

    struct Job
    {
        std::string label;
        std::string channel; // empty: own channel (never serialized)
        Work work;
        Done done;           // main thread, only when work did not throw
        Done onAbort;        // main thread, when cancelled or failed instead
        bool showProgress = true; // false: listed in :tasks, no status bar
    };

    // What the status bar shows: the oldest running job plus its position in
    // the current batch (a batch lasts until the queue drains).
    struct Status
    {
        std::string label;
        std::string detail;
        long long done = 0;
        long long total = 0;
        Clock::time_point started{};
        int position = 0;   // 1-based
        int batchTotal = 0; // jobs submitted since the queue was last empty
        int running = 0;
        bool waiting = false; // nothing running yet; oldest job is queued
    };

    enum class State
    {
        Waiting,
        Running,
        Finished
    };

    // One row of the task list view.
    struct TaskInfo
    {
        TaskId id = 0;
        std::string label;
        std::string channel;
        std::string detail;
        State state = State::Waiting;
        long long done = 0;
        long long total = 0;
        Clock::time_point started{};
        bool cancelRequested = false;
        bool failed = false;
    };

    struct Submitted
    {
        TaskId id = 0;
        // Label of the older job this one waits behind, if its channel was
        // busy at submit time.
        std::string queuedBehind;
    };

    explicit TaskQueue(int maxParallel = 4);
    ~TaskQueue();
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    Submitted submit(Job job);

    // Main thread. Runs ready completion callbacks in submission order.
    // Returns how many jobs were retired.
    int pollCompletions();

    enum class WaitResult
    {
        Completed,
        Cancelled,
        Failed
    };

    // Main thread. Blocks until `id` finished running, calling `pump` about
    // every 20 ms so the UI can animate and buffer input. Does not run any
    // completion callbacks. On Failed, `error` (if given) gets the message.
    WaitResult wait(TaskId id, const std::function<void()>& pump,
                    std::string* error = nullptr);

    // Every job not yet retired, in submission order.
    std::vector<TaskInfo> snapshot() const;
    // Waiting job: dropped without running. Running job: asks it to stop
    // (Context::cancelled() turns true); its completion callback is skipped.
    bool cancel(TaskId id);
    // Reorders a waiting job past its waiting neighbour. Running and
    // finished jobs keep their place. Returns false when nothing moved.
    bool moveEarlier(TaskId id);
    bool moveLater(TaskId id);

    bool idle() const;
    bool channelBusy(const std::string& channel) const;
    std::optional<Status> status() const;
    // Errors from finished jobs since the last call ("label: what").
    std::vector<std::string> takeErrors();
    // Changes whenever anything visible in status() changes.
    std::uint64_t revision() const;

private:
    struct Entry
    {
        TaskId id = 0;
        Job job;
        State state = State::Waiting;
        bool failed = false;
        std::atomic<bool> cancelRequested{false};
        std::exception_ptr error;
        bool waited = false; // a wait() caller consumes the result
        long long done = 0;
        long long total = 0;
        std::string detail;
        Clock::time_point started{};
    };

    void scheduleLocked();
    void runEntry(TaskId id);
    Entry* findLocked(TaskId id);
    const Entry* findLocked(TaskId id) const;
    bool moveLocked(TaskId id, int direction);
    void reapThreads();

    mutable std::mutex mutex_;
    std::condition_variable finishedCv_;
    std::vector<std::unique_ptr<Entry>> entries_; // submission order
    std::vector<std::thread> threads_;
    std::vector<std::thread::id> finishedThreads_;
    std::vector<std::string> errors_;
    TaskId nextId_ = 1;
    int maxParallel_;
    int running_ = 0;
    int batchTotal_ = 0;
    int batchRetired_ = 0;
    std::uint64_t revision_ = 0;
    std::atomic<bool> stopping_{false};
};
