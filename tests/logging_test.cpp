#include "log.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <regex>
#include <string>

namespace
{
std::string readAll(const std::filesystem::path& path)
{
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}
} // namespace

TEST(LoggingTest, WritesTimestampSignatureAndMessage)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "uvim_logging_test.log";
    std::filesystem::remove(path);
    mla::log::setLogFilePath(path.string());

    mla::log::FileLogger logger("CLANGD");
    LOG_ERROR(logger, "startup failed: {}", "boom");

    const std::string contents = readAll(path);
    EXPECT_TRUE(std::regex_match(
        contents,
        std::regex(
            R"(ERROR \d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{6} \[CLANGD\] startup failed: boom\n)")));
}

TEST(LoggingTest, DefaultPathUsesPlatformLogDirectory)
{
    const std::filesystem::path path(mla::log::defaultLogFilePath());
#ifdef _WIN32
    EXPECT_EQ(path.filename().string(), "uvim.log");
    EXPECT_EQ(path.parent_path().filename().string(), "uvim");
    EXPECT_EQ(path.parent_path().parent_path().filename().string(),
              "Documents");
#else
    EXPECT_EQ(path.string(), "/tmp/uvim.log");
#endif
}
