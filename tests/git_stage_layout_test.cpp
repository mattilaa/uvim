#include "mode_state_machine.h"
#include <gtest/gtest.h>

using namespace editor::statemachine;

TEST(GitStageLayoutTest, ContentRowsForKnownViewport)
{
    // Regression case from user report. screenRows (35) excludes the status
    // and message rows; header + one help line leave 33 content rows.
    EXPECT_EQ(GitStageMode::testContentRows(35, 156), 33);
}

TEST(GitStageLayoutTest, ContentRowsDecreaseWhenHelpWraps)
{
    int wide = GitStageMode::testContentRows(35, 156);
    int narrow = GitStageMode::testContentRows(35, 80);

    EXPECT_GT(wide, narrow);
    EXPECT_GE(narrow, 0);
}
