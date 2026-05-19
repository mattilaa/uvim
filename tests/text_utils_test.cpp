#include "header_help.h"
#include "text_utils.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

TEST(TextUtilsTest, Utf8ByteOffsetToUtf16HandlesEmoji)
{
    const std::string line = std::string("a") + "\xF0\x9F\x98\x80" + "b";
    const int emojiEnd = (int)(std::string("a") + "\xF0\x9F\x98\x80").size();
    const int emojiMid = emojiEnd - 2; // inside the 4-byte emoji sequence

    EXPECT_EQ(text_utils::utf8ByteOffsetToUtf16(line, emojiEnd), 3);
    EXPECT_EQ(text_utils::utf8ByteOffsetToUtf16(line, (int)line.size()), 4);
    EXPECT_EQ(text_utils::utf8ByteOffsetToUtf16(line, emojiMid), 1);
}

TEST(TextUtilsTest, Utf16OffsetMatchesCursorAfterEmojiInsertPosition)
{
    std::string line = "ab";
    int cursor = 0; // normal mode cursor on 'a'

    // Insert after current char like normal-mode emoji picker.
    int insertPos = text_utils::nextUtf8CharStart(line, cursor);
    const std::string emoji = "\xF0\x9F\x98\x80"; // 😀
    line.insert(insertPos, emoji);
    cursor = insertPos + (int)emoji.size();

    // UTF-16: "a" = 1, emoji surrogate pair = 2 => cursor at 3.
    EXPECT_EQ(text_utils::utf8ByteOffsetToUtf16(line, cursor), 3);
}

TEST(HeaderHelpTest, WrapsTokensToTerminalWidth)
{
    const std::vector<std::string> tokens = {
        "[Enter: open]", "[Ctrl+Shift+X: close matches]", "[Esc: cancel]"};

    const auto lines = HeaderHelp::wrap(tokens, 34);

    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "  [Enter: open]");
    EXPECT_EQ(lines[1], "  [Ctrl+Shift+X: close matches]");
    EXPECT_EQ(lines[2], "  [Esc: cancel]");
    for(const auto& line : lines)
        EXPECT_LE(text_utils::utf8DisplayWidth(line), 34);
}
