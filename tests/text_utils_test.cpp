#include "text_utils.h"

#include <gtest/gtest.h>
#include <string>

TEST(TextUtilsTest, Utf8ByteOffsetToUtf16HandlesEmoji)
{
    const std::string line = std::string("a") + "\xF0\x9F\x98\x80" + "b";
    const int emojiEnd =
        (int)(std::string("a") + "\xF0\x9F\x98\x80").size();
    const int emojiMid = emojiEnd - 2; // inside the 4-byte emoji sequence

    EXPECT_EQ(text_utils::utf8ByteOffsetToUtf16(line, emojiEnd), 3);
    EXPECT_EQ(text_utils::utf8ByteOffsetToUtf16(line, (int)line.size()), 4);
    EXPECT_EQ(text_utils::utf8ByteOffsetToUtf16(line, emojiMid), 1);
}
