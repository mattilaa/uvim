#include "task_queue.h"

#include <algorithm>
#include <exception>
#include <unordered_set>

void TaskQueue::Context::progress(long long done, long long total)
{
    std::lock_guard<std::mutex> lk(queue_.mutex_);
    if(Entry* entry = queue_.findLocked(id_))
    {
        entry->done = std::max(0LL, done);
        if(total >= 0)
            entry->total = total;
        ++queue_.revision_;
    }
}

void TaskQueue::Context::detail(std::string text)
{
    std::lock_guard<std::mutex> lk(queue_.mutex_);
    if(Entry* entry = queue_.findLocked(id_); entry && entry->detail != text)
    {
        entry->detail = std::move(text);
        ++queue_.revision_;
    }
}

bool TaskQueue::Context::cancelled() const
{
    if(queue_.stopping_.load(std::memory_order_relaxed))
        return true;
    std::lock_guard<std::mutex> lk(queue_.mutex_);
    const Entry* entry = queue_.findLocked(id_);
    return !entry || entry->cancelRequested.load(std::memory_order_relaxed);
}

TaskQueue::TaskQueue(int maxParallel) : maxParallel_(std::max(1, maxParallel))
{
}

TaskQueue::~TaskQueue()
{
    stopping_.store(true);
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        threads.swap(threads_);
    }
    for(auto& thread : threads)
    {
        if(thread.joinable())
            thread.join();
    }
}

TaskQueue::Submitted TaskQueue::submit(Job job)
{
    std::lock_guard<std::mutex> lk(mutex_);
    Submitted result;
    result.id = nextId_++;
    if(!job.channel.empty())
    {
        for(const auto& entry : entries_)
        {
            if(entry->state != State::Finished &&
               entry->job.channel == job.channel)
            {
                result.queuedBehind = entry->job.label;
                break;
            }
        }
    }

    auto entry = std::make_unique<Entry>();
    entry->id = result.id;
    entry->job = std::move(job);
    entries_.push_back(std::move(entry));
    ++batchTotal_;
    ++revision_;
    scheduleLocked();
    return result;
}

void TaskQueue::scheduleLocked()
{
    if(stopping_.load())
        return;

    // Walk in submission order; the first unfinished job on a channel owns
    // it, so later jobs on that channel cannot start ahead of it.
    std::unordered_set<std::string> claimed;
    for(auto& entry : entries_)
    {
        if(entry->state == State::Finished)
            continue;
        const std::string& channel = entry->job.channel;
        if(!channel.empty() && !claimed.insert(channel).second)
            continue;
        if(entry->state != State::Waiting || running_ >= maxParallel_)
            continue;

        entry->state = State::Running;
        entry->started = Clock::now();
        ++running_;
        ++revision_;
        const TaskId id = entry->id;
        try
        {
            threads_.emplace_back([this, id] { runEntry(id); });
        }
        catch(...)
        {
            entry->state = State::Finished;
            entry->failed = true;
            entry->error = std::current_exception();
            --running_;
            finishedCv_.notify_all();
        }
    }
}

void TaskQueue::runEntry(TaskId id)
{
    Work* work = nullptr;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if(Entry* entry = findLocked(id))
            work = &entry->job.work;
    }

    std::exception_ptr error;
    if(work && *work)
    {
        Context context(*this, id);
        try
        {
            (*work)(context);
        }
        catch(...)
        {
            error = std::current_exception();
        }
    }

    std::lock_guard<std::mutex> lk(mutex_);
    if(Entry* entry = findLocked(id))
    {
        entry->state = State::Finished;
        entry->failed = error != nullptr;
        entry->error = error;
    }
    --running_;
    ++revision_;
    finishedThreads_.push_back(std::this_thread::get_id());
    scheduleLocked();
    finishedCv_.notify_all();
}

TaskQueue::Entry* TaskQueue::findLocked(TaskId id)
{
    for(auto& entry : entries_)
    {
        if(entry->id == id)
            return entry.get();
    }
    return nullptr;
}

const TaskQueue::Entry* TaskQueue::findLocked(TaskId id) const
{
    for(const auto& entry : entries_)
    {
        if(entry->id == id)
            return entry.get();
    }
    return nullptr;
}

void TaskQueue::reapThreads()
{
    std::vector<std::thread> finished;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for(const auto& threadId : finishedThreads_)
        {
            auto it = std::find_if(threads_.begin(), threads_.end(),
                                   [&](const std::thread& thread)
                                   { return thread.get_id() == threadId; });
            if(it == threads_.end())
                continue;
            finished.push_back(std::move(*it));
            threads_.erase(it);
        }
        finishedThreads_.clear();
    }
    for(auto& thread : finished)
        thread.join();
}

static std::string describeError(const std::exception_ptr& error)
{
    try
    {
        std::rethrow_exception(error);
    }
    catch(const std::exception& e)
    {
        return e.what();
    }
    catch(...)
    {
        return "unknown error";
    }
}

int TaskQueue::pollCompletions()
{
    reapThreads();

    int retired = 0;
    while(true)
    {
        Done done;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if(entries_.empty() ||
               entries_.front()->state != State::Finished)
                break;

            Entry& entry = *entries_.front();
            const bool cancelled = entry.cancelRequested.load();
            if(entry.failed && !entry.waited && !cancelled)
                errors_.push_back(entry.job.label + ": " +
                                  describeError(entry.error));
            if(!entry.waited)
            {
                done = (entry.failed || cancelled)
                           ? std::move(entry.job.onAbort)
                           : std::move(entry.job.done);
            }
            entries_.erase(entries_.begin());
            ++batchRetired_;
            if(entries_.empty())
            {
                batchTotal_ = 0;
                batchRetired_ = 0;
            }
            ++revision_;
        }
        ++retired;
        if(done)
            done();
    }
    return retired;
}

TaskQueue::WaitResult TaskQueue::wait(TaskId id,
                                     const std::function<void()>& pump,
                                     std::string* error)
{
    std::unique_lock<std::mutex> lk(mutex_);
    Entry* entry = findLocked(id);
    if(!entry)
        return WaitResult::Completed;
    entry->waited = true;
    while(true)
    {
        const bool finished =
            finishedCv_.wait_for(lk, std::chrono::milliseconds(20),
                                 [&]
                                 {
                                     const Entry* e = findLocked(id);
                                     return !e || e->state == State::Finished;
                                 });
        if(finished)
            break;
        lk.unlock();
        if(pump)
            pump();
        lk.lock();
    }

    const Entry* e = findLocked(id);
    if(!e)
        return WaitResult::Completed;
    if(e->cancelRequested.load())
        return WaitResult::Cancelled;
    if(e->failed)
    {
        if(error)
            *error = describeError(e->error);
        return WaitResult::Failed;
    }
    return WaitResult::Completed;
}

std::vector<TaskQueue::TaskInfo> TaskQueue::snapshot() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<TaskInfo> out;
    out.reserve(entries_.size());
    for(const auto& entry : entries_)
    {
        TaskInfo info;
        info.id = entry->id;
        info.label = entry->job.label;
        info.channel = entry->job.channel;
        info.detail = entry->detail;
        info.state = entry->state;
        info.done = entry->done;
        info.total = entry->total;
        info.started = entry->started;
        info.cancelRequested = entry->cancelRequested.load();
        info.failed = entry->failed;
        out.push_back(std::move(info));
    }
    return out;
}

bool TaskQueue::cancel(TaskId id)
{
    std::lock_guard<std::mutex> lk(mutex_);
    Entry* entry = findLocked(id);
    if(!entry || entry->state == State::Finished ||
       entry->cancelRequested.load())
        return false;
    entry->cancelRequested.store(true);
    if(entry->state == State::Waiting)
    {
        // Never started: retire it right away and let the next job on its
        // channel take over.
        entry->state = State::Finished;
        scheduleLocked();
        finishedCv_.notify_all();
    }
    ++revision_;
    return true;
}

bool TaskQueue::moveLocked(TaskId id, int direction)
{
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [id](const auto& entry) { return entry->id == id; });
    if(it == entries_.end() || (*it)->state != State::Waiting)
        return false;

    // Step over retired-but-not-yet-polled entries to the nearest live one.
    auto neighbour = it;
    while(true)
    {
        if(direction < 0)
        {
            if(neighbour == entries_.begin())
                return false;
            --neighbour;
        }
        else
        {
            ++neighbour;
            if(neighbour == entries_.end())
                return false;
        }
        if((*neighbour)->state != State::Finished)
            break;
    }
    if((*neighbour)->state != State::Waiting)
        return false;

    std::iter_swap(it, neighbour);
    ++revision_;
    scheduleLocked();
    return true;
}

bool TaskQueue::moveEarlier(TaskId id)
{
    std::lock_guard<std::mutex> lk(mutex_);
    return moveLocked(id, -1);
}

bool TaskQueue::moveLater(TaskId id)
{
    std::lock_guard<std::mutex> lk(mutex_);
    return moveLocked(id, 1);
}

bool TaskQueue::idle() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return entries_.empty();
}

bool TaskQueue::channelBusy(const std::string& channel) const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return std::any_of(entries_.begin(), entries_.end(),
                       [&](const auto& entry)
                       {
                           return entry->state != State::Finished &&
                                  entry->job.channel == channel;
                       });
}

std::optional<TaskQueue::Status> TaskQueue::status() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    const Entry* shown = nullptr;
    int finishedInList = 0;
    for(const auto& entry : entries_)
    {
        if(!entry->job.showProgress)
            continue;
        if(entry->state == State::Finished)
            ++finishedInList;
        else if(!shown && entry->state == State::Running)
            shown = entry.get();
    }

    bool waiting = false;
    if(!shown)
    {
        for(const auto& entry : entries_)
        {
            if(entry->job.showProgress && entry->state == State::Waiting)
            {
                shown = entry.get();
                waiting = true;
                break;
            }
        }
    }
    if(!shown)
        return std::nullopt;

    Status status;
    status.label = shown->job.label;
    status.detail = shown->detail;
    status.done = shown->done;
    status.total = shown->total;
    status.started = shown->started;
    status.batchTotal = batchTotal_;
    status.position =
        std::min(batchTotal_, batchRetired_ + finishedInList + 1);
    status.running = running_;
    status.waiting = waiting;
    return status;
}

std::vector<std::string> TaskQueue::takeErrors()
{
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<std::string> out;
    out.swap(errors_);
    return out;
}

std::uint64_t TaskQueue::revision() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return revision_;
}
