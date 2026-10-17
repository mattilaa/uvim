// Background task plumbing for the editor: submitting work to the TaskQueue,
// keeping the TUI alive while waiting, and drawing progress in the status bar
// and the `:tasks` view.

#include "editor.h"
#include "mode_state_machine.h"
#include "process_pipe.h"
#include "terminal.h"
#include "text_utils.h"
#include "key_enums.h"
#include "widgets/progress_bar.h"

#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif

#include <algorithm>

namespace
{
using Clock = std::chrono::steady_clock;

// Jobs shorter than this never show up, so quick commands do not flicker.
constexpr auto kShowAfter = std::chrono::milliseconds(150);
constexpr auto kRepaintEvery = std::chrono::milliseconds(100);

long long elapsedMs(Clock::time_point since, Clock::time_point now)
{
    if(since == Clock::time_point{})
        return 0;
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - since)
        .count();
}

std::string padToWidth(std::string text, int width)
{
    const int used = text_utils::displayWidth(text);
    if(used < width)
        text.append(width - used, ' ');
    return text;
}

// "textDocument/definition" -> "definition"
[[maybe_unused]] std::string_view shortMethod(std::string_view method)
{
    if(auto slash = method.rfind('/'); slash != std::string_view::npos)
        return method.substr(slash + 1);
    return method;
}
} // namespace

TaskQueue::Submitted Editor::submitTask(std::string label, std::string channel,
                                        TaskQueue::Work work,
                                        TaskQueue::Done done,
                                        TaskQueue::Done onAbort)
{
    std::string shownLabel = label;
    auto submitted = tasks.submit(
        TaskQueue::Job{std::move(label), std::move(channel), std::move(work),
                       std::move(done), std::move(onAbort)});
    if(!submitted.queuedBehind.empty())
    {
        // An older job owns the channel; this one cannot override it.
        setStatusMessage("'" + shownLabel + "' queued: waits for '" +
                         submitted.queuedBehind + "' to finish (:tasks)");
        needsFullRedraw = true;
    }
    return submitted;
}

bool Editor::runTask(std::string label, std::string channel,
                     TaskQueue::Work work)
{
    const std::string shownLabel = label;
    auto submitted = submitTask(std::move(label), std::move(channel),
                                std::move(work));
    return waitTask(submitted.id, shownLabel);
}

bool Editor::waitTask(TaskQueue::TaskId id, const std::string& shownLabel)
{
    const TaskQueue::TaskId previous = foregroundTask_;
    foregroundTask_ = id;
    std::string error;
    const auto result = tasks.wait(id, [this] { pumpTasks(); }, &error);
    foregroundTask_ = previous;
    needsFullRedraw = true;

    switch(result)
    {
    case TaskQueue::WaitResult::Completed:
        return true;
    case TaskQueue::WaitResult::Cancelled:
        setStatusMessage("'" + shownLabel + "' cancelled");
        return false;
    case TaskQueue::WaitResult::Failed:
        setStatusMessage("'" + shownLabel + "' failed: " + error);
        return false;
    }
    return false;
}

void Editor::enableInteractiveTasks()
{
    tasksInteractive_ = true;
    ProcessPipe::setForegroundRunner(
        [this](const std::string& label, const std::string& channel,
               const ProcessPipe::BlockingWait& wait)
        {
            return runTask(label, channel,
                           [&wait](TaskQueue::Context& ctx)
                           { wait([&ctx] { return ctx.cancelled(); }); });
        });
}

void Editor::pumpTasks()
{
    if(!tasksInteractive_)
        return;
    // Never dispatch keys here: we are inside some handler waiting for a
    // result. Park them so the main loop replays them in order afterwards.
    Terminal::bufferPendingInput();
    if(foregroundTask_ != 0 &&
       Terminal::takeBufferedKey(keyCode(control::ControlKey::CTRL_C)))
    {
        tasks.cancel(foregroundTask_);
    }

    const auto now = Clock::now();
    if(now - lastTaskPaint_ >= kRepaintEvery)
    {
        lastTaskPaint_ = now;
        paintTaskStatus();
    }
}

void Editor::addStartupStep(std::string label, std::function<void()> step,
                            bool runLast)
{
    auto& queue = runLast ? lateStartupSteps_ : startupSteps_;
    queue.push_back(StartupStep{std::move(label), std::move(step)});
    ++startupStepsTotal_;
}

void Editor::runNextStartupStep()
{
    if(!firstFrameDrawn_ || foregroundTask_ != 0)
        return;
    auto& queue = !startupSteps_.empty() ? startupSteps_ : lateStartupSteps_;
    if(queue.empty())
        return;

    StartupStep step = std::move(queue.front());
    queue.pop_front();
    startupStepLabel_ = step.label;
    startupStepStarted_ = Clock::now();
    lastTaskPaint_ = {};
    paintTaskStatus(); // show "(i/n) label" before the step blocks
    if(step.run)
        step.run();
    startupStepLabel_.clear();
    ++startupStepsDone_;
    if(startupSteps_.empty() && lateStartupSteps_.empty())
    {
        startupStepsTotal_ = 0;
        startupStepsDone_ = 0;
    }
    needsFullRedraw = true;
}

void Editor::serviceTasks()
{
    if(tasks.pollCompletions() > 0)
        needsFullRedraw = true;
    runNextStartupStep();
    for(const auto& error : tasks.takeErrors())
    {
        setStatusMessage("Task failed: " + error);
        needsFullRedraw = true;
    }

    const bool shown = !taskStatusText().empty();
    const auto now = Clock::now();
    if(shown != taskStatusShown_)
    {
        // Appearing or disappearing changes the status bar layout.
        taskStatusShown_ = shown;
        lastTaskPaint_ = now;
        needsFullRedraw = true;
        return;
    }

    if(currentMode == TASK_LIST)
    {
        // The list view is the progress display; refresh it live.
        const auto revision = tasks.revision();
        if(revision != lastTaskRevision_ ||
           (shown && now - lastTaskPaint_ >= kRepaintEvery))
        {
            lastTaskRevision_ = revision;
            lastTaskPaint_ = now;
            needsFullRedraw = true;
        }
        return;
    }

    if(shown && now - lastTaskPaint_ >= kRepaintEvery)
    {
        lastTaskPaint_ = now;
        paintTaskStatus();
    }
}

void Editor::cancelRgCacheIndexing()
{
    ++rgCacheGeneration;
    if(rgCacheIndexTask != 0)
    {
        tasks.cancel(rgCacheIndexTask);
        rgCacheIndexTask = 0;
    }
}

widgets::PickerLayout Editor::pickerLayout(bool previewWanted) const
{
    widgets::PickerLayoutOptions options;
    options.screenRows = screenRows;
    options.screenCols = screenCols;
    options.floating = pickerFloating;
    options.widthPercent = pickerWidthPercent;
    options.heightPercent = pickerHeightPercent;
    options.preview = pickerPreview && previewWanted;
    return widgets::computePickerLayout(options);
}

void Editor::drawPickerBackground(const widgets::PickerLayout& layout)
{
    const PickerBackground wanted{true,          screenRows,     screenCols,
                                  layout.boxTop, layout.boxLeft, layout.boxRows,
                                  layout.boxCols};
    const PickerBackground& have = pickerBackground_;
    // Geometry changes (resize, picker.width) must repaint what the old box
    // covered, even when the screen was adopted.
    if(have.valid && have.screenRows == wanted.screenRows &&
       have.screenCols == wanted.screenCols && have.boxTop == wanted.boxTop &&
       have.boxLeft == wanted.boxLeft && have.boxRows == wanted.boxRows &&
       have.boxCols == wanted.boxCols)
        return;

    if(pickerBackgroundAdopted_)
        pickerBackgroundAdopted_ = false; // already on screen
    else if(currentBuffer && lines)
        drawFullScreen();
    else
        Terminal::write(std::string(Terminal::ESC_CURSOR_HOME) +
                        Terminal::ESC_CLEAR_SCREEN);
    pickerBackground_ = wanted;
}

int Editor::taskStatusWidth() const
{
    if(screenCols < 40)
        return 0;
    return std::clamp(screenCols * 2 / 5, 24, 64);
}

std::vector<std::pair<std::string, LspClient*>> Editor::activeLspClients() const
{
    std::vector<std::pair<std::string, LspClient*>> out;
#ifdef UVIM_ENABLE_CLANGD_LSP
    auto add = [&](const std::unique_ptr<LspClient>& client,
                   const char* fallback)
    {
        if(!client || !client->running())
            return;
        std::string name = client->serverName();
        out.emplace_back(name.empty() ? fallback : std::move(name),
                         client.get());
    };
    add(lspClient, "clangd");
    add(pythonLspClient, "python");
    add(robotLspClient, "robot");
    add(mlangLspClient, "mlang");
    add(htmlLspClient, "html");
    add(cssLspClient, "css");
    add(jsonLspClient, "json");
    add(tsLspClient, "ts");
#endif
    return out;
}

void Editor::attachLspWaitHook(LspClient& client)
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    LspClient* target = &client;
    client.setWaitHook(
        [this, target](std::string_view method)
        {
            if(method.empty())
            {
                lspWaitLabel_.clear();
                return;
            }
            if(lspWaitLabel_.empty())
            {
                std::string server = target->serverName();
                lspWaitLabel_ = (server.empty() ? std::string("lsp") : server) +
                                ": " + std::string(shortMethod(method));
                lspWaitStarted_ = Clock::now();
            }
            pumpTasks();
        });
#else
    (void)client;
#endif
}

std::string Editor::taskStatusText()
{
    const int width = taskStatusWidth();
    if(width <= 0)
        return {};

    const auto now = Clock::now();
    widgets::ProgressBarView view;
    view.style = utf8Mode ? progressStyle : widgets::ProgressStyle::Ascii;
    view.base = theme.statusBar();
    view.fill = theme.progressFill();
    view.track = theme.progressTrack();
    view.accent = theme.uiAccent();
    view.maxWidth = width;

    std::string label;
    std::string detail;
    bool foregroundWait = false;
    const auto status = tasks.status();
    if(status && (status->waiting || status->batchTotal > 1 ||
                  foregroundTask_ != 0 || now - status->started >= kShowAfter))
    {
        label = status->label;
        detail = status->detail;
        view.done = status->done;
        view.total = status->total;
        view.elapsedMs = status->waiting ? 0 : elapsedMs(status->started, now);
        view.position = status->position;
        view.queued = status->batchTotal;
        view.running = status->running;
        view.waiting = status->waiting;
        foregroundWait = foregroundTask_ != 0;
    }
    else if(!startupStepLabel_.empty())
    {
        // Startup step on the UI thread, e.g. an LSP server initializing.
        label = startupStepLabel_;
        if(!lspWaitLabel_.empty())
            detail = lspWaitLabel_;
        view.elapsedMs = elapsedMs(startupStepStarted_, now);
        view.position = startupStepsDone_ + 1;
        view.queued = startupStepsTotal_;
        foregroundWait = true;
    }
    else if(!lspWaitLabel_.empty() && now - lspWaitStarted_ >= kShowAfter)
    {
        label = lspWaitLabel_;
        view.elapsedMs = elapsedMs(lspWaitStarted_, now);
        foregroundWait = true;
    }
    else
    {
#ifdef UVIM_ENABLE_CLANGD_LSP
        for(const auto& [server, client] : activeLspClients())
        {
            auto work = client->workProgress();
            if(!work)
                continue;
            if(lspWorkStarted_ == Clock::time_point{})
                lspWorkStarted_ = now;
            label = server + ": " +
                    (work->title.empty() ? std::string("working")
                                         : work->title);
            detail = work->message;
            if(work->percent >= 0)
            {
                // Servers report a percentage; the message already carries
                // their own counts (e.g. "12/41").
                view.done = work->percent;
                view.total = 100;
                view.showCounts = false;
            }
            view.elapsedMs = elapsedMs(lspWorkStarted_, now);
            break;
        }
#endif
        if(label.empty())
        {
            lspWorkStarted_ = {};
            return {};
        }
    }

    if(foregroundWait)
        view.queuedKeys = Terminal::bufferedKeyCount();
    view.label = label;
    view.detail = detail;
    return padToWidth(widgets::formatProgressBar(view), width);
}

void Editor::noteTaskStatusSlot(int row, int col)
{
    // col > 0: progress drawn there; col < 0: this status line hosts the
    // progress but had no room; col == 0: nothing hosted (overlay instead).
    taskStatusSlot_.row = row;
    taskStatusSlot_.col = col;
    taskStatusSlot_.mode = currentMode;
    taskStatusSlot_.valid = col > 0;
    taskStatusSlot_.hidden = col < 0;
}

void Editor::beginTaskStatusFrame()
{
    if(!taskStatusOverlay_ || taskStatusShown_)
        return;
    // The overlay sits on rows that self-drawn views may never repaint.
    const int width = taskStatusWidth();
    std::string out = Terminal::cursorPos(screenRows + 1,
                                          std::max(1, screenCols - width + 1));
    out += theme.reset();
    out.append(width, ' ');
    Terminal::write(out);
    taskStatusOverlay_ = false;
}

void Editor::finishTaskStatusFrame()
{
    if(!taskStatusShown_ || currentMode == TASK_LIST)
        return;
    if((taskStatusSlot_.valid || taskStatusSlot_.hidden) &&
       taskStatusSlot_.mode == currentMode)
        return; // the status bar already drew it in place (or had no room)
    paintTaskStatus();
}

void Editor::paintTaskStatus()
{
    if(currentMode == TASK_LIST && foregroundTask_ == 0)
        return;
    if(taskStatusSlot_.hidden && taskStatusSlot_.mode == currentMode)
        return;
    const std::string text = taskStatusText();
    const bool hosted =
        taskStatusSlot_.valid && taskStatusSlot_.mode == currentMode;
    if(text.empty())
    {
        if(taskStatusOverlay_)
        {
            taskStatusShown_ = false;
            beginTaskStatusFrame();
        }
        return;
    }

    int row = taskStatusSlot_.row;
    int col = taskStatusSlot_.col;
    if(!hosted)
    {
        // Views that draw their own screen: overlay the right end of the
        // status bar row so progress always sits in the same place.
        row = screenRows + 1;
        col = std::max(1, screenCols - taskStatusWidth() + 1);
        taskStatusOverlay_ = true;
    }

    std::string out = "\x1b" "7"; // save cursor (DEC)
    out += Terminal::cursorPos(row, col);
    out += theme.statusBar();
    out += text;
    out += theme.reset();
    out += "\x1b" "8"; // restore cursor
    Terminal::write(out);
    Terminal::flush();
}

void Editor::drawTaskList()
{
#ifdef UVIM_ENABLE_AUXILIARY_VIEWS
    using editor::statemachine::TaskListMode;
    TaskListMode* mode = modeStateMachine
                             ? modeStateMachine->getState<TaskListMode>()
                             : nullptr;
    const auto rows = tasks.snapshot();
    const auto now = Clock::now();
    const auto status = tasks.status();
    if(mode)
    {
        auto it = std::find_if(rows.begin(), rows.end(),
                               [&](const TaskQueue::TaskInfo& row)
                               { return row.id == mode->selected; });
        if(it != rows.end())
            mode->cursor = static_cast<int>(it - rows.begin());
        mode->cursor =
            rows.empty() ? 0 : std::clamp(mode->cursor, 0, (int)rows.size() - 1);
        mode->selected = rows.empty() ? 0 : rows[mode->cursor].id;
    }
    const int cursor = mode ? mode->cursor : 0;

    std::string output;
    output.reserve(screenRows * screenCols * 2);
    output += Terminal::ESC_HIDE_CURSOR;
    output += Terminal::cursorPos(1, 1);
    output += theme.reset();

    auto line = [&](std::string text, const std::string& color = {})
    {
        output += Terminal::NEWLINE_CLEAR;
        output += color;
        output += text;
        output += theme.reset();
    };

    output += theme.panel();
    std::string header = " Tasks";
    if(status && status->batchTotal > 0)
        header += "  (" + std::to_string(status->position) + "/" +
                  std::to_string(status->batchTotal) + ")";
    header += "  " + std::to_string(status ? status->running : 0) +
              " running";
    output += padToWidth(header, screenCols);
    output += theme.reset();

    widgets::ProgressBarView meter;
    meter.style = utf8Mode ? progressStyle : widgets::ProgressStyle::Ascii;
    meter.base = theme.reset();
    meter.fill = theme.progressFill();
    meter.track = theme.progressTrack();
    meter.accent = theme.uiAccent();
    meter.barCells = 16;

    const int labelWidth = std::clamp(screenCols - 62, 12, 48);
    auto column = [](std::string text, int width)
    {
        if(text_utils::displayWidth(text) > width)
            text = text.substr(0, std::max(0, width - 1)) + "~";
        return padToWidth(std::move(text), width);
    };

    line(" " + column("#", 4) + column("STATE", 12) + column("TASK", labelWidth) +
             column("CHANNEL", 10) + "PROGRESS",
         theme.uiDim());

    const int listRows = std::max(0, screenRows - 2);
    const int first =
        std::clamp(cursor - listRows + 1, 0, std::max(0, (int)rows.size() - 1));
    int drawn = 0;
    for(int i = first; i < (int)rows.size() && drawn < listRows; ++i, ++drawn)
    {
        const auto& row = rows[i];
        meter.done = row.done;
        meter.total = row.total;
        meter.elapsedMs = elapsedMs(row.started, now);
        meter.waiting = row.state == TaskQueue::State::Waiting;

        std::string state;
        const std::string* stateColor = &theme.uiInfo();
        if(row.cancelRequested)
        {
            state = row.state == TaskQueue::State::Running ? "cancelling"
                                                           : "cancelled";
            stateColor = &theme.uiError();
        }
        else if(row.state == TaskQueue::State::Running)
        {
            state = "running";
            stateColor = &theme.uiSuccess();
        }
        else if(row.state == TaskQueue::State::Waiting)
        {
            state = "waiting";
            stateColor = &theme.uiWarning();
        }
        else
        {
            state = row.failed ? "failed" : "done";
            stateColor = row.failed ? &theme.uiError() : &theme.uiDim();
        }

        std::string progress;
        if(row.state == TaskQueue::State::Running ||
           row.state == TaskQueue::State::Waiting)
        {
            progress = widgets::formatProgressMeter(meter);
            if(row.total > 0)
                progress += " " +
                            std::to_string(std::clamp(row.done, 0LL, row.total) *
                                           100 / row.total) +
                            "%  " + std::to_string(row.done) + "/" +
                            std::to_string(row.total);
            else if(row.done > 0)
                progress += " " + std::to_string(row.done);
            if(row.state == TaskQueue::State::Running)
                progress += "  " + std::to_string(meter.elapsedMs / 1000) + "s";
        }

        output += Terminal::NEWLINE_CLEAR;
        if(i == cursor)
            output += theme.selection();
        output += " " + column(std::to_string(i + 1), 4);
        output += *stateColor + column(state, 12) + theme.reset();
        if(i == cursor)
            output += theme.selection();
        std::string label = row.label;
        if(!row.detail.empty())
            label += " - " + row.detail;
        output += column(label, labelWidth);
        output += column(row.channel.empty() ? "-" : row.channel, 10);
        output += theme.reset();
        output += progress;
    }

    // LSP servers report their own work; show it read-only under the queue.
    for(const auto& [server, client] : activeLspClients())
    {
#ifdef UVIM_ENABLE_CLANGD_LSP
        auto work = client->workProgress();
        if(!work || drawn >= listRows)
            continue;
        meter.done = std::max(0, work->percent);
        meter.total = work->percent >= 0 ? 100 : 0;
        meter.elapsedMs = elapsedMs(lspWorkStarted_, now);
        meter.waiting = false;
        std::string label = work->title;
        if(!work->message.empty())
            label += " - " + work->message;
        std::string progress = widgets::formatProgressMeter(meter);
        if(work->percent >= 0)
            progress += " " + std::to_string(work->percent) + "%";
        line(" " + column("lsp", 4) + theme.uiAccent() + column("server", 12) +
             theme.reset() + column(label, labelWidth) + column(server, 10) +
             progress);
        ++drawn;
#else
        (void)server;
        (void)client;
#endif
    }

    if(rows.empty() && drawn == 0)
    {
        line("  No background tasks. Heavy operations (grep, git, format,",
             theme.uiDim());
        line("  compile, indexing) appear here while they run.", theme.uiDim());
        drawn += 2;
    }
    for(; drawn < listRows; ++drawn)
        line("~", theme.uiGutter());

    output += Terminal::NEWLINE_CLEAR;
    output += theme.statusBar();
    output += padToWidth(" <j/k> select  <K/J> move waiting task  <x/dd> "
                         "cancel  <q/Esc> close",
                         screenCols);
    output += theme.reset();
    output += Terminal::NEWLINE_CLEAR;
    if(!statusMessage.empty())
        output += statusMessage.substr(0, std::max(0, screenCols));

    Terminal::write(output);
    Terminal::flush();
#endif
}
