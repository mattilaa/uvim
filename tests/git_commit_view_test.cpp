#include "editor.h"
#include "git_commit_mode.h"
#include <algorithm>
#include <cstdio>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace editor::statemachine;

namespace
{
// Replay the drawing commands, including terminal wrapping and scrolling.
struct Screen
{
    std::vector<std::string> lines;
    int row = 0;
    int col = 0;
    int width;

    Screen(int height, int columns)
        : lines(height, std::string(columns, ' ')), width(columns)
    {
    }

    void newline()
    {
        if(++row == (int)lines.size())
        {
            lines.erase(lines.begin());
            lines.emplace_back(width, ' ');
            --row;
        }
    }

    void replay(const std::string& output)
    {
        for(size_t i = 0; i < output.size(); ++i)
        {
            char ch = output[i];
            if(ch == '\x1b' && i + 1 < output.size() && output[i + 1] == '[')
            {
                size_t start = i + 2;
                i = output.find_first_of(
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
                    start);
                ASSERT_NE(i, std::string::npos);
                if(output[i] == 'H')
                {
                    int r = 1, c = 1;
                    std::sscanf(output.substr(start, i - start).c_str(),
                                "%d;%d", &r, &c);
                    row = std::clamp(r - 1, 0, (int)lines.size() - 1);
                    col = std::clamp(c - 1, 0, width - 1);
                }
                else if(output[i] == 'K')
                    lines[row].assign(width, ' ');
                else if(output[i] == 'J')
                    for(auto& line : lines)
                        line.assign(width, ' ');
            }
            else if(ch == '\r')
                col = 0;
            else if(ch == '\n')
                newline();
            else if(ch >= ' ')
            {
                if(col == width)
                {
                    col = 0;
                    newline();
                }
                lines[row][col++] = ch;
            }
        }
    }
};
} // namespace

TEST(GitCommitViewTest, CursorStaysOnMessageWhenHelpAndPathsExceedWidth)
{
    for(int width : {40, 80, 160})
    {
        for(auto action : {GitCommitMode::Action::CommitStaged,
                           GitCommitMode::Action::RevertCommit,
                           GitCommitMode::Action::RebaseTodo})
        {
            for(int topRow : {0, 5})
            {
                Editor editor = Editor::createForTests();
                editor.screenRows = 10;
                editor.screenCols = width;
                GitCommitMode mode;
                mode.action = action;
                mode.repoRoot = std::string(200, 'p');
                mode.messageLines.assign(6, "other");
                mode.messageLines[topRow] = "message";
                mode.messageTopRow = topRow;
                mode.messageCursorRow = topRow;
                mode.messageCursorCol = 3;
                mode.insertMode = true;
                testing::internal::CaptureStdout();
                mode.draw(editor);
                Screen screen(editor.screenRows + 2, width);
                screen.replay(testing::internal::GetCapturedStdout());
                EXPECT_EQ(screen.lines[2].substr(2, 7), "message");
                EXPECT_EQ(screen.row, 2);
                EXPECT_EQ(screen.col, 5);
                EXPECT_EQ(screen.lines[screen.row][screen.col], 's');

                mode.commandActive = true;
                mode.commandLine = "wq";
                testing::internal::CaptureStdout();
                mode.draw(editor);
                screen.replay(testing::internal::GetCapturedStdout());
                EXPECT_EQ(screen.lines.back().substr(0, 3), ":wq");
                EXPECT_EQ(screen.row, editor.screenRows + 1);
                EXPECT_EQ(screen.col, 3);
            }
        }
    }
}
