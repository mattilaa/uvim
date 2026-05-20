#include "mode_state_machine.h"
#include <gtest/gtest.h>

using namespace editor::statemachine;

TEST(GitStageLayoutTest, ContentRowsForKnownViewport)
{
    // Regression case from user report.
    EXPECT_EQ(GitStageMode::testContentRows(35, 156), 32);
}

TEST(GitStageLayoutTest, ContentRowsDecreaseWhenHelpWraps)
{
    int wide = GitStageMode::testContentRows(35, 156);
    int narrow = GitStageMode::testContentRows(35, 80);

    EXPECT_GT(wide, narrow);
    EXPECT_GE(narrow, 0);
}
