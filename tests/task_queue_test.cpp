#include "picker_query.h"
#include "task_queue.h"
#include "theme.h"
#include "widgets/picker_frame.h"
#include "widgets/progress_bar.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace std::chrono_literals;

// Drains the queue from the "main thread" until it is idle.
void drain(TaskQueue& queue)
{
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while(!queue.idle() && std::chrono::steady_clock::now() < deadline)
    {
        queue.pollCompletions();
        std::this_thread::sleep_for(1ms);
    }
    queue.pollCompletions();
}

TEST(TaskQueueTest, SameChannelRunsInSubmissionOrder)
{
    TaskQueue queue(4);
    std::vector<int> ran;
    std::mutex ranMutex;
    std::vector<int> completed;

    for(int i = 0; i < 5; ++i)
    {
        queue.submit(
            {"job" + std::to_string(i),
             "git",
             [&, i](TaskQueue::Context&)
             {
                 std::this_thread::sleep_for(2ms);
                 std::lock_guard<std::mutex> lk(ranMutex);
                 ran.push_back(i);
             },
             [&, i] { completed.push_back(i); }});
    }
    drain(queue);

    EXPECT_EQ(ran, (std::vector<int>{0, 1, 2, 3, 4}));
    EXPECT_EQ(completed, (std::vector<int>{0, 1, 2, 3, 4}));
}

TEST(TaskQueueTest, DifferentChannelsRunConcurrentlyButCompleteInOrder)
{
    TaskQueue queue(4);
    std::atomic<bool> release{false};
    std::atomic<bool> secondRan{false};
    std::vector<std::string> completed;

    queue.submit(
        {"slow",
         "a",
         [&](TaskQueue::Context&)
         {
             while(!release.load())
                 std::this_thread::sleep_for(1ms);
         },
         [&] { completed.push_back("slow"); }});
    queue.submit(
        {"fast",
         "b",
         [&](TaskQueue::Context&) { secondRan = true; },
         [&] { completed.push_back("fast"); }});

    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while(!secondRan.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    ASSERT_TRUE(secondRan.load()) << "channel b must not wait for channel a";

    // "fast" finished first but must not be applied before "slow".
    queue.pollCompletions();
    EXPECT_TRUE(completed.empty());

    release = true;
    drain(queue);
    EXPECT_EQ(completed, (std::vector<std::string>{"slow", "fast"}));
}

TEST(TaskQueueTest, ReportsQueuePositionAndQueuedBehind)
{
    TaskQueue queue(4);
    std::atomic<bool> release{false};
    auto block = [&](TaskQueue::Context& ctx)
    {
        ctx.progress(3, 10);
        while(!release.load())
            std::this_thread::sleep_for(1ms);
    };

    queue.submit({"first", "rg", block, {}});
    auto second = queue.submit({"second", "rg", block, {}});
    queue.submit({"third", "rg", block, {}});
    EXPECT_EQ(second.queuedBehind, "first");

    std::optional<TaskQueue::Status> status;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    do
    {
        status = queue.status();
        std::this_thread::sleep_for(1ms);
    } while((!status || status->total == 0) &&
            std::chrono::steady_clock::now() < deadline);

    ASSERT_TRUE(status);
    EXPECT_EQ(status->label, "first");
    EXPECT_EQ(status->position, 1);
    EXPECT_EQ(status->batchTotal, 3);
    EXPECT_EQ(status->done, 3);
    EXPECT_EQ(status->total, 10);

    release = true;
    drain(queue);
    EXPECT_FALSE(queue.status());
}

TEST(TaskQueueTest, WaitingJobsCanBeReorderedAndCancelled)
{
    TaskQueue queue(4);
    std::atomic<bool> release{false};
    std::vector<std::string> completed;
    auto job = [&](const std::string& name)
    {
        return TaskQueue::Job{
            name,
            "git",
            [&](TaskQueue::Context&)
            {
                while(!release.load())
                    std::this_thread::sleep_for(1ms);
            },
            [&, name] { completed.push_back(name); }};
    };

    auto a = queue.submit(job("a"));
    auto b = queue.submit(job("b"));
    auto c = queue.submit(job("c"));
    auto d = queue.submit(job("d"));

    EXPECT_FALSE(queue.moveEarlier(a.id)) << "running jobs keep their place";
    EXPECT_FALSE(queue.moveEarlier(b.id)) << "cannot jump a running job";
    EXPECT_TRUE(queue.moveEarlier(d.id)); // a, b, d, c
    EXPECT_TRUE(queue.cancel(c.id));      // a, b, d
    EXPECT_FALSE(queue.cancel(c.id));

    const auto rows = queue.snapshot();
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(rows[2].label, "d");
    EXPECT_TRUE(rows[3].cancelRequested);

    release = true;
    drain(queue);
    EXPECT_EQ(completed, (std::vector<std::string>{"a", "b", "d"}));
}

TEST(TaskQueueTest, WaitPumpsAndReportsOutcome)
{
    TaskQueue queue(2);
    // The job finishes only after the waiter pumped once, so the test does
    // not depend on how fast a (slow CI) machine runs a sleeping job.
    std::atomic<int> pumps{0};
    auto ok = queue.submit(
        {"ok",
         "",
         [&](TaskQueue::Context&)
         {
             while(pumps.load() == 0)
                 std::this_thread::sleep_for(1ms);
         },
         {}});
    EXPECT_EQ(
        queue.wait(ok.id, [&] { ++pumps; }),
        TaskQueue::WaitResult::Completed);
    EXPECT_GT(pumps.load(), 0);

    auto bad = queue.submit(
        {"bad",
         "",
         [](TaskQueue::Context&) { throw std::runtime_error("x"); },
         {}});
    std::string error;
    EXPECT_EQ(queue.wait(bad.id, {}, &error), TaskQueue::WaitResult::Failed);
    EXPECT_EQ(error, "x");
    drain(queue);
    EXPECT_TRUE(queue.takeErrors().empty()) << "waited errors go to caller";
}

TEST(TaskQueueTest, RunningJobSeesCancellation)
{
    TaskQueue queue(1);
    std::atomic<bool> sawCancel{false};
    bool doneRan = false;
    auto job = queue.submit(
        {"loop",
         "",
         [&](TaskQueue::Context& ctx)
         {
             while(!ctx.cancelled())
                 std::this_thread::sleep_for(1ms);
             sawCancel = true;
         },
         [&] { doneRan = true; }});
    std::this_thread::sleep_for(5ms);
    EXPECT_TRUE(queue.cancel(job.id));
    drain(queue);
    EXPECT_TRUE(sawCancel.load());
    EXPECT_FALSE(doneRan);
}

TEST(TaskQueueTest, AbortCallbackRunsForCancelledAndFailedJobs)
{
    TaskQueue queue(1);
    std::vector<std::string> events;
    std::atomic<bool> release{false};
    queue.submit(
        {"blocker",
         "c",
         [&](TaskQueue::Context&)
         {
             while(!release.load())
                 std::this_thread::sleep_for(1ms);
         },
         {},
         {}});
    auto waiting = queue.submit(
        {"waiting",
         "c",
         {},
         [&] { events.push_back("waiting done"); },
         [&] { events.push_back("waiting abort"); }});
    queue.submit(
        {"thrower",
         "",
         [](TaskQueue::Context&) { throw std::runtime_error("boom"); },
         [&] { events.push_back("thrower done"); },
         [&] { events.push_back("thrower abort"); }});
    queue.cancel(waiting.id);
    release = true;
    drain(queue);
    EXPECT_EQ(
        events,
        (std::vector<std::string>{"waiting abort", "thrower abort"}));
    auto errors = queue.takeErrors();
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0], "thrower: boom");
}

TEST(ProgressBarTest, AsciiStyleShowsQueueAndPercent)
{
    widgets::ProgressBarView view;
    view.label = "rg cache";
    view.done = 5;
    view.total = 10;
    view.position = 2;
    view.queued = 5;
    view.style = widgets::ProgressStyle::Ascii;
    view.elapsedMs = 3200;
    view.maxWidth = 80;
    EXPECT_EQ(
        widgets::formatProgressBar(view),
        "(2/5) | rg cache [=====.....] 50% 5/10 3s");
}

TEST(ProgressBarTest, GlyphStyleUsesEighthBlocks)
{
    widgets::ProgressBarView view;
    view.label = "index";
    view.done = 55;
    view.total = 100;
    view.style = widgets::ProgressStyle::Glyph;
    view.barCells = 4;
    // 55% of 4 cells = 2.2 cells -> 2 full + 1/8.
    EXPECT_EQ(widgets::formatProgressMeter(view), "▕██▏░▏");
}

TEST(ProgressBarTest, DropsSegmentsToFit)
{
    widgets::ProgressBarView view;
    view.label = "git log";
    view.detail = "reading history";
    view.done = 1;
    view.total = 4;
    view.style = widgets::ProgressStyle::Ascii;
    view.maxWidth = 30;
    const std::string text = widgets::formatProgressBar(view);
    EXPECT_LE(text.size(), 30u);
    EXPECT_NE(text.find("25%"), std::string::npos);
    EXPECT_EQ(text.find("reading"), std::string::npos);
}

TEST(ProgressBarTest, KeepsPhaseDetailOverCounts)
{
    widgets::ProgressBarView view;
    view.label = "rg cache";
    view.detail = "indexing";
    view.done = 3102;
    view.total = 3102;
    view.style = widgets::ProgressStyle::Ascii;
    view.maxWidth = 44;
    const std::string text = widgets::formatProgressBar(view);
    EXPECT_NE(text.find("indexing"), std::string::npos) << text;
    EXPECT_EQ(text.find("3102/3102"), std::string::npos) << text;
}

TEST(ProgressBarTest, ParsesStyleNames)
{
    widgets::ProgressStyle style = widgets::ProgressStyle::Ascii;
    EXPECT_TRUE(widgets::parseProgressStyle("colour", style));
    EXPECT_EQ(style, widgets::ProgressStyle::Color);
    EXPECT_FALSE(widgets::parseProgressStyle("fancy", style));
    EXPECT_EQ(style, widgets::ProgressStyle::Color);
}

TEST(PickerLayoutTest, FloatingBoxIsCenteredWithPreview)
{
    widgets::PickerLayoutOptions options;
    options.screenRows = 40;
    options.screenCols = 160;
    const auto layout = widgets::computePickerLayout(options);
    ASSERT_TRUE(layout.floating);
    EXPECT_EQ(layout.boxCols, 128); // 80%
    EXPECT_EQ(layout.boxRows, 28);  // 70%
    EXPECT_EQ(layout.boxLeft, 1 + (160 - 128) / 2);
    EXPECT_EQ(layout.boxTop, 1 + (40 - 28) / 2);
    EXPECT_TRUE(layout.preview);
    // list | divider | preview fill the inside of the border exactly
    EXPECT_EQ(layout.cols + 1 + layout.previewCols, layout.boxCols - 2);
    EXPECT_EQ(layout.previewLeft, layout.left + layout.cols + 1);
    EXPECT_EQ(layout.headerCols, layout.boxCols - 2);
}

TEST(PickerLayoutTest, NarrowScreensDropPreviewThenFloat)
{
    widgets::PickerLayoutOptions options;
    options.screenRows = 30;
    options.screenCols = 100; // box 80 cols: too narrow for a preview
    auto layout = widgets::computePickerLayout(options);
    EXPECT_TRUE(layout.floating);
    EXPECT_FALSE(layout.preview);
    EXPECT_EQ(layout.cols, layout.boxCols - 2);

    options.screenCols = 40; // no room for a box at all
    layout = widgets::computePickerLayout(options);
    EXPECT_FALSE(layout.floating);
    EXPECT_EQ(layout.cols, 40);
    EXPECT_EQ(layout.rows, 30);
}

TEST(PickerLayoutTest, FullscreenStyleUsesWholeArea)
{
    widgets::PickerLayoutOptions options;
    options.screenRows = 30;
    options.screenCols = 160;
    options.floating = false;
    const auto layout = widgets::computePickerLayout(options);
    EXPECT_FALSE(layout.floating);
    EXPECT_FALSE(layout.preview);
    EXPECT_EQ(layout.top, 1);
    EXPECT_EQ(layout.left, 1);
    EXPECT_EQ(layout.cols, 160);
}

TEST(PickerQueryTest, ParsesLineSuffix)
{
    auto q = parsePickerQuery("SomeFil:415");
    EXPECT_EQ(q.pattern, "SomeFil");
    EXPECT_TRUE(q.hasLine);
    EXPECT_EQ(q.line, 415);

    q = parsePickerQuery(":12");
    EXPECT_EQ(q.pattern, "");
    EXPECT_TRUE(q.hasLine);
    EXPECT_EQ(q.line, 12);
}

TEST(PickerQueryTest, LeavesOtherQueriesAlone)
{
    for(const char* text : {"needle", "a:b", "std::", "v1:2x", "", "123"})
    {
        const auto q = parsePickerQuery(text);
        EXPECT_EQ(q.pattern, text) << text;
        EXPECT_FALSE(q.hasLine) << text;
    }
}

TEST(PickerQueryTest, TrailingColonIsAPendingSuffix)
{
    auto q = parsePickerQuery("SomeFil:");
    EXPECT_EQ(q.pattern, "SomeFil");
    EXPECT_FALSE(q.hasLine);

    q = parsePickerQuery("note\\:");
    EXPECT_EQ(q.pattern, "note:");
}

TEST(PickerQueryTest, EscapedColonSearchesLiterally)
{
    const auto q = parsePickerQuery("error\\:42");
    EXPECT_EQ(q.pattern, "error:42");
    EXPECT_FALSE(q.hasLine);
}
} // namespace

namespace
{
bool isValidUtf8(std::string_view text)
{
    size_t i = 0;
    while(i < text.size())
    {
        const auto lead = static_cast<unsigned char>(text[i]);
        size_t len = lead < 0x80                   ? 1
                     : lead >= 0xC2 && lead < 0xE0 ? 2
                     : lead >= 0xE0 && lead < 0xF0 ? 3
                     : lead >= 0xF0 && lead < 0xF5 ? 4
                                                   : 0;
        if(len == 0 || i + len > text.size())
            return false;
        for(size_t k = 1; k < len; ++k)
        {
            if((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80)
                return false;
        }
        i += len;
    }
    return true;
}
} // namespace

TEST(PickerLayoutTest, PreviewSpanEndingInsideACharacterStaysValidUtf8)
{
    // "it’s": ’ is 3 bytes (2..4). A highlight span that ends inside it
    // used to repeat its tail bytes, and on Windows one invalid byte turned
    // the whole frame into code page 850 mojibake.
    widgets::PickerLayoutOptions options;
    options.screenRows = 30;
    options.screenCols = 160;
    const auto layout = widgets::computePickerLayout(options);
    ASSERT_TRUE(layout.preview);

    Theme theme;
    std::string out;
    widgets::PickerCanvas canvas(out, layout, theme, true);
    const std::string text = "it\xE2\x80\x99s";
    canvas.previewRow(0, 1, text, false, {{0, 3, "\x1b[31m"}});

    EXPECT_TRUE(isValidUtf8(out));
    std::string plain;
    for(size_t i = 0; i < out.size(); ++i)
    {
        if(out[i] == '\x1b')
        {
            while(i < out.size() && !std::isalpha((unsigned char)out[i]))
                ++i;
            continue;
        }
        plain += out[i];
    }
    EXPECT_NE(plain.find(text), std::string::npos) << plain;
}

TEST(TaskQueueTest, QuietJobsRemainListedWithoutStatusProgress)
{
    TaskQueue queue(1);
    std::atomic<bool> release{false};
    queue.submit(
        {"documentation",
         "docs",
         [&](TaskQueue::Context& ctx)
         {
             while(!release && !ctx.cancelled())
                 std::this_thread::sleep_for(1ms);
         },
         {},
         {},
         false});
    EXPECT_FALSE(queue.status());
    EXPECT_EQ(queue.snapshot().size(), 1u);
    queue.submit({"compile", "compiler", [](TaskQueue::Context&) {}, {}});
    const auto status = queue.status();
    EXPECT_TRUE(status.has_value());
    if(status)
        EXPECT_EQ(status->label, "compile");
    release = true;
    drain(queue);
}
