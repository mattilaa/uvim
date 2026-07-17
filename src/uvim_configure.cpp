#include "color_constant.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <conio.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#ifndef UVIM_SOURCE_DIR
#define UVIM_SOURCE_DIR "."
#endif

namespace fs = std::filesystem;

namespace
{
constexpr std::string_view kAnsiReset =
    color::ansi(color::AnsiColor::Reset);
constexpr std::string_view kAnsiFgDefault =
    color::ansi(color::AnsiColor::FgDefault);
constexpr std::string_view kAnsiBlue = color::ansi(color::AnsiColor::FgBlue);
constexpr std::string_view kAnsiGreen =
    color::ansi(color::AnsiColor::FgGreen);
constexpr std::string_view kAnsiDim = color::ansi(color::AnsiColor::Dim);
constexpr std::string_view kAnsiEditField =
    color::ansi(color::AnsiColor::StyleEditField);
const std::string kAnsiCurrentLineBg = color::rgbBg(24, 64, 36);

constexpr int kKeyEsc = 27;
constexpr int kKeyUp = 1001;
constexpr int kKeyDown = 1002;
constexpr int kKeyLeft = 1003;
constexpr int kKeyRight = 1004;
constexpr int kKeyResize = 1005;

int gPendingKey = 0;

#ifdef _WIN32
DWORD gOriginalOutMode = 0;
bool gOriginalOutModeSet = false;

HANDLE stdout_handle() noexcept
{
    return GetStdHandle(STD_OUTPUT_HANDLE);
}

void enable_console_output_mode()
{
    DWORD outMode = 0;
    HANDLE out = stdout_handle();
    if(GetConsoleMode(out, &outMode))
    {
        gOriginalOutMode = outMode;
        gOriginalOutModeSet = true;
        outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
#ifdef ENABLE_WRAP_AT_EOL_OUTPUT
        outMode &= ~ENABLE_WRAP_AT_EOL_OUTPUT;
#endif
        SetConsoleMode(out, outMode);
    }
}

void restore_console_output_mode()
{
    if(gOriginalOutModeSet)
        SetConsoleMode(stdout_handle(), gOriginalOutMode);
}

bool write_console_utf8(std::string_view text) noexcept
{
    if(text.empty())
        return true;

    DWORD mode = 0;
    HANDLE out = stdout_handle();
    if(!GetConsoleMode(out, &mode))
        return false;

    if(text.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return false;

    int wideLen =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), nullptr, 0);
    if(wideLen <= 0)
        return false;

    std::wstring wide(static_cast<size_t>(wideLen), L'\0');
    wideLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                  static_cast<int>(text.size()), wide.data(),
                                  wideLen);
    if(wideLen <= 0)
        return false;

    DWORD written = 0;
    return WriteConsoleW(out, wide.data(), static_cast<DWORD>(wide.size()),
                         &written, nullptr) != 0;
}
#endif

void write_stdout(std::string_view text)
{
#ifdef _WIN32
    if(write_console_utf8(text))
        return;
#endif
    std::cout.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void flush_stdout()
{
    std::cout.flush();
}

#ifndef _WIN32
volatile std::sig_atomic_t gResizePending = 0;

void handle_resize(int)
{
    gResizePending = 1;
}
#endif

struct TerminalSize
{
    int rows = 24;
    int cols = 80;
};

struct Layout
{
    int titleRows = 1;
    int keyRows = 1;
    int spacerRows = 1;
    int listRows = 1;
    int docTitleRows = 1;
    int docRows = 1;
    int outputRows = 1;
    int messageRows = 3;
};

enum class ItemKind
{
    FeatureSet,
    Choice,
    Text,
    Toggle,
};

enum class RowKind
{
    Section,
    Item,
};

#if defined(__APPLE__)
constexpr bool kStaticLinkAvailable = false;
#else
constexpr bool kStaticLinkAvailable = true;
#endif

struct Config
{
    int featureSet = 4;
    int buildType = 0;
    int platform = 0;
    int optimization = 2;
    bool ninjaGenerator = true;
    std::string installDir;
    std::string jobs;
    std::string logFile;
    bool minimal = false;
    bool browserTools = true;
    bool auxiliaryViews = true;
    bool clangdLsp = false;
    bool robotLsp = false;
    bool pythonLsp = false;
    bool mlangLsp = false;
    bool mlangSemanticTokens = true;
    bool htmlLsp = false;
    bool cssLsp = false;
    bool jsonLsp = false;
    bool tsLsp = false;
    bool asmDocs = true;
    bool gitTools = true;
    bool searchTools = true;
    bool formatters = true;
    bool systemClipboard = true;
    bool structSizePopup = true;
    bool colorTools = true;
    bool terminalColors = true;
    bool modernKeybindings = true;
    bool multiPaneSplits = true;
    bool perPaneLsp = true;
    bool tests = true;
    bool compileCommands = true;
    bool lto = true;
    bool gcSections = true;
    bool stripBinary = false;
    bool staticLink = false;
    bool autoIncrementBuild = true;
    bool sanitizers = false;
    bool debugLogging = false;
    bool debugLsp = false;
};

bool operator==(const Config& lhs, const Config& rhs)
{
    return lhs.featureSet == rhs.featureSet &&
           lhs.buildType == rhs.buildType && lhs.platform == rhs.platform &&
           lhs.optimization == rhs.optimization &&
           lhs.ninjaGenerator == rhs.ninjaGenerator &&
           lhs.installDir == rhs.installDir && lhs.jobs == rhs.jobs &&
           lhs.logFile == rhs.logFile && lhs.minimal == rhs.minimal &&
           lhs.browserTools == rhs.browserTools &&
           lhs.auxiliaryViews == rhs.auxiliaryViews &&
           lhs.clangdLsp == rhs.clangdLsp &&
           lhs.robotLsp == rhs.robotLsp &&
           lhs.pythonLsp == rhs.pythonLsp &&
           lhs.mlangLsp == rhs.mlangLsp &&
           lhs.mlangSemanticTokens == rhs.mlangSemanticTokens &&
           lhs.htmlLsp == rhs.htmlLsp && lhs.cssLsp == rhs.cssLsp &&
           lhs.jsonLsp == rhs.jsonLsp && lhs.tsLsp == rhs.tsLsp &&
           lhs.asmDocs == rhs.asmDocs &&
           lhs.gitTools == rhs.gitTools &&
           lhs.searchTools == rhs.searchTools &&
           lhs.formatters == rhs.formatters &&
           lhs.systemClipboard == rhs.systemClipboard &&
           lhs.structSizePopup == rhs.structSizePopup &&
           lhs.colorTools == rhs.colorTools &&
           lhs.terminalColors == rhs.terminalColors &&
           lhs.modernKeybindings == rhs.modernKeybindings &&
           lhs.multiPaneSplits == rhs.multiPaneSplits &&
           lhs.perPaneLsp == rhs.perPaneLsp && lhs.tests == rhs.tests &&
           lhs.compileCommands == rhs.compileCommands &&
           lhs.lto == rhs.lto && lhs.gcSections == rhs.gcSections &&
           lhs.stripBinary == rhs.stripBinary &&
           lhs.staticLink == rhs.staticLink &&
           lhs.autoIncrementBuild == rhs.autoIncrementBuild &&
           lhs.sanitizers == rhs.sanitizers &&
           lhs.debugLogging == rhs.debugLogging &&
           lhs.debugLsp == rhs.debugLsp;
}

bool operator!=(const Config& lhs, const Config& rhs)
{
    return !(lhs == rhs);
}

struct CliOptions
{
    fs::path sourceDir = fs::absolute(fs::path(UVIM_SOURCE_DIR));
    fs::path buildDir = sourceDir / "build";
    std::optional<fs::path> output;
    bool installAfterBuild = false;
};

bool operator==(const CliOptions& lhs, const CliOptions& rhs)
{
    return lhs.sourceDir == rhs.sourceDir && lhs.buildDir == rhs.buildDir &&
           lhs.output == rhs.output &&
           lhs.installAfterBuild == rhs.installAfterBuild;
}

bool operator!=(const CliOptions& lhs, const CliOptions& rhs)
{
    return !(lhs == rhs);
}

struct Item
{
    ItemKind kind;
    std::string label;
    std::string cmakeName;
    bool Config::* flag = nullptr;
    int Config::* choice = nullptr;
    std::vector<std::string> choices;
    std::string help;
    std::string Config::* text = nullptr;
};

struct Section
{
    std::string label;
    std::string help;
    bool open = true;
    std::vector<Item> items;
};

struct VisibleRow
{
    RowKind kind;
    size_t section = 0;
    size_t item = 0;
};

const std::vector<std::string> kFeatureSets = {"vi-real", "vi-min", "minimal",
                                               "basic", "full"};
const std::vector<std::string> kBuildTypes = {"Release", "Debug",
                                              "RelWithDebInfo", "MinSizeRel"};
const std::vector<std::string> kPlatforms = {"AUTO", "POSIX", "WIN32"};
const std::vector<std::string> kOptimizations = {"O0", "O1", "O2",
                                                 "O3", "Os", "Oz"};

bool equals_ci(std::string_view lhs, std::string_view rhs)
{
    if(lhs.size() != rhs.size())
        return false;
    for(size_t i = 0; i < lhs.size(); ++i)
    {
        if(std::tolower(static_cast<unsigned char>(lhs[i])) !=
           std::tolower(static_cast<unsigned char>(rhs[i])))
            return false;
    }
    return true;
}

std::optional<int> choice_index(std::string_view value,
                                const std::vector<std::string>& choices)
{
    for(size_t i = 0; i < choices.size(); ++i)
    {
        if(equals_ci(value, choices[i]))
            return static_cast<int>(i);
    }
    return std::nullopt;
}

std::string join_choices(const std::vector<std::string>& choices)
{
    std::string out;
    for(size_t i = 0; i < choices.size(); ++i)
    {
        if(i > 0)
            out += ", ";
        out += choices[i];
    }
    return out;
}

std::string trim(std::string_view value)
{
    size_t first = 0;
    while(first < value.size() &&
          std::isspace(static_cast<unsigned char>(value[first])))
        ++first;
    size_t last = value.size();
    while(last > first &&
          std::isspace(static_cast<unsigned char>(value[last - 1])))
        --last;
    return std::string(value.substr(first, last - first));
}

std::string bool_value(bool value)
{
    return value ? "ON" : "OFF";
}

std::string cmake_string_literal(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for(char c : value)
    {
        if(c == '\\' || c == '"')
            out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::optional<bool> parse_bool(std::string_view value)
{
    if(equals_ci(value, "on") || equals_ci(value, "true") ||
       equals_ci(value, "yes") || value == "1")
        return true;
    if(equals_ci(value, "off") || equals_ci(value, "false") ||
       equals_ci(value, "no") || value == "0")
        return false;
    return std::nullopt;
}

std::string default_install_dir()
{
#ifdef _WIN32
    if(const char* localAppData = std::getenv("LOCALAPPDATA"))
        return std::string(localAppData) + "\\uvim\\bin";
    return "C:\\uvim\\bin";
#else
    return "~/.local/bin";
#endif
}

std::string default_log_file()
{
#ifdef _WIN32
    if(const char* userProfile = std::getenv("USERPROFILE"))
        return std::string(userProfile) + "\\Documents\\uvim\\uvim.log";
    return "uvim.log";
#else
    return "/tmp/uvim.log";
#endif
}

std::string default_jobs()
{
    const unsigned int cores = std::thread::hardware_concurrency();
    return std::to_string(cores == 0 ? 1 : cores);
}

class TerminalRawMode
{
public:
    TerminalRawMode()
    {
#ifdef _WIN32
        enable_console_output_mode();
#else
        if(::isatty(STDIN_FILENO) && ::tcgetattr(STDIN_FILENO, &original) == 0)
        {
            termios raw = original;
            raw.c_lflag &= ~(ICANON | ECHO);
            raw.c_cc[VMIN] = 1;
            raw.c_cc[VTIME] = 0;
            active = (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0);
        }
#endif
    }

    TerminalRawMode(const TerminalRawMode&) = delete;
    TerminalRawMode& operator=(const TerminalRawMode&) = delete;

    ~TerminalRawMode()
    {
#ifdef _WIN32
        restore_console_output_mode();
#else
        if(active)
            ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
#endif
    }

private:
#ifndef _WIN32
    termios original{};
#endif
    bool active = false;
};

int read_key()
{
    if(gPendingKey != 0)
    {
        const int key = gPendingKey;
        gPendingKey = 0;
        return key;
    }
#ifdef _WIN32
    int c = _getch();
    if(c == 0 || c == 224)
    {
        int next = _getch();
        if(next == 72)
            return kKeyUp;
        if(next == 80)
            return kKeyDown;
        if(next == 75)
            return kKeyLeft;
        if(next == 77)
            return kKeyRight;
    }
    return c;
#else
    unsigned char c = 0;
    const ssize_t n = ::read(STDIN_FILENO, &c, 1);
    if(n != 1)
    {
        if(n == -1 && errno == EINTR && gResizePending)
        {
            gResizePending = 0;
            return kKeyResize;
        }
        return 0;
    }
    if(c == 27)
    {
        unsigned char seq[2]{};
#ifndef _WIN32
        const int flags = ::fcntl(STDIN_FILENO, F_GETFL, 0);
        if(flags != -1)
            ::fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
#endif
        if(::read(STDIN_FILENO, &seq[0], 1) == 1)
        {
            if(seq[0] != '[')
                gPendingKey = seq[0];
            else if(::read(STDIN_FILENO, &seq[1], 1) == 1)
            {
#ifndef _WIN32
                if(flags != -1)
                    ::fcntl(STDIN_FILENO, F_SETFL, flags);
#endif
                if(seq[1] == 'A')
                    return kKeyUp;
                if(seq[1] == 'B')
                    return kKeyDown;
                if(seq[1] == 'D')
                    return kKeyLeft;
                if(seq[1] == 'C')
                    return kKeyRight;
            }
        }
#ifndef _WIN32
        if(flags != -1)
            ::fcntl(STDIN_FILENO, F_SETFL, flags);
#endif
        return kKeyEsc;
    }
    return c;
#endif
}

TerminalSize terminal_size()
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if(GetConsoleScreenBufferInfo(stdout_handle(), &info))
    {
        TerminalSize result;
        result.cols =
            static_cast<int>(info.srWindow.Right - info.srWindow.Left + 1);
        result.rows =
            static_cast<int>(info.srWindow.Bottom - info.srWindow.Top + 1);
        return result;
    }
    return {};
#else
    winsize size{};
    if(::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0)
    {
        TerminalSize result;
        if(size.ws_row > 0)
            result.rows = size.ws_row;
        if(size.ws_col > 0)
            result.cols = size.ws_col;
        return result;
    }
    return {};
#endif
}

void apply_feature_set(Config& cfg)
{
    cfg.buildType = 0;
    cfg.sanitizers = false;
    cfg.debugLogging = false;
    cfg.debugLsp = false;
    cfg.minimal = false;
    cfg.browserTools = true;
    cfg.auxiliaryViews = true;

    if(cfg.featureSet == 0)
    {
        cfg.minimal = true;
        cfg.browserTools = true;
        cfg.auxiliaryViews = false;
        cfg.optimization = 5;
        cfg.clangdLsp = false;
        cfg.robotLsp = false;
        cfg.pythonLsp = false;
        cfg.mlangLsp = false;
        cfg.htmlLsp = false;
        cfg.cssLsp = false;
        cfg.jsonLsp = false;
        cfg.tsLsp = false;
        cfg.asmDocs = false;
        cfg.gitTools = false;
        cfg.searchTools = false;
        cfg.formatters = false;
        cfg.systemClipboard = false;
        cfg.structSizePopup = false;
        cfg.colorTools = false;
        cfg.terminalColors = false;
        cfg.modernKeybindings = false;
        cfg.multiPaneSplits = false;
        cfg.perPaneLsp = false;
        cfg.tests = false;
        cfg.compileCommands = false;
        cfg.lto = true;
        cfg.gcSections = true;
        cfg.stripBinary = true;
        cfg.autoIncrementBuild = false;
        return;
    }

    if(cfg.featureSet == 1)
    {
        cfg.browserTools = true;
        cfg.auxiliaryViews = false;
        cfg.optimization = 5;
        cfg.clangdLsp = false;
        cfg.robotLsp = false;
        cfg.pythonLsp = false;
        cfg.mlangLsp = false;
        cfg.htmlLsp = false;
        cfg.cssLsp = false;
        cfg.jsonLsp = false;
        cfg.tsLsp = false;
        cfg.asmDocs = false;
        cfg.gitTools = false;
        cfg.searchTools = false;
        cfg.formatters = false;
        cfg.systemClipboard = false;
        cfg.structSizePopup = false;
        cfg.colorTools = true;
        cfg.terminalColors = true;
        cfg.modernKeybindings = true;
        cfg.multiPaneSplits = true;
        cfg.perPaneLsp = true;
        cfg.tests = false;
        cfg.compileCommands = false;
        cfg.lto = true;
        cfg.gcSections = true;
        cfg.stripBinary = true;
        cfg.autoIncrementBuild = false;
        return;
    }

    if(cfg.featureSet == 2)
    {
        cfg.browserTools = true;
        cfg.auxiliaryViews = true;
        cfg.optimization = 5;
        cfg.clangdLsp = false;
        cfg.robotLsp = false;
        cfg.pythonLsp = false;
        cfg.mlangLsp = false;
        cfg.htmlLsp = false;
        cfg.cssLsp = false;
        cfg.jsonLsp = false;
        cfg.tsLsp = false;
        cfg.asmDocs = false;
        cfg.gitTools = true;
        cfg.searchTools = true;
        cfg.formatters = true;
        cfg.systemClipboard = true;
        cfg.structSizePopup = false;
        cfg.colorTools = true;
        cfg.terminalColors = true;
        cfg.modernKeybindings = true;
        cfg.multiPaneSplits = true;
        cfg.perPaneLsp = true;
        cfg.tests = false;
        cfg.compileCommands = false;
        cfg.lto = true;
        cfg.gcSections = true;
        cfg.stripBinary = true;
        cfg.autoIncrementBuild = false;
        return;
    }

    if(cfg.featureSet == 3)
    {
        cfg.browserTools = true;
        cfg.auxiliaryViews = true;
        cfg.optimization = 2;
        cfg.clangdLsp = false;
        cfg.robotLsp = false;
        cfg.pythonLsp = false;
        cfg.mlangLsp = false;
        cfg.htmlLsp = false;
        cfg.cssLsp = false;
        cfg.jsonLsp = false;
        cfg.tsLsp = false;
        cfg.asmDocs = true;
        cfg.gitTools = true;
        cfg.searchTools = true;
        cfg.formatters = true;
        cfg.systemClipboard = true;
        cfg.structSizePopup = true;
        cfg.colorTools = true;
        cfg.terminalColors = true;
        cfg.modernKeybindings = true;
        cfg.multiPaneSplits = true;
        cfg.perPaneLsp = true;
        cfg.tests = true;
        cfg.compileCommands = true;
        cfg.lto = true;
        cfg.gcSections = true;
        cfg.stripBinary = false;
        cfg.autoIncrementBuild = true;
        return;
    }

    cfg.optimization = 2;
    cfg.browserTools = true;
    cfg.auxiliaryViews = true;
    cfg.clangdLsp = true;
    cfg.robotLsp = true;
    cfg.pythonLsp = true;
    cfg.mlangLsp = true;
    cfg.htmlLsp = true;
    cfg.cssLsp = true;
    cfg.jsonLsp = true;
    cfg.tsLsp = true;
    cfg.asmDocs = true;
    cfg.gitTools = true;
    cfg.searchTools = true;
    cfg.formatters = true;
    cfg.systemClipboard = true;
    cfg.structSizePopup = true;
    cfg.colorTools = true;
    cfg.terminalColors = true;
    cfg.modernKeybindings = true;
    cfg.multiPaneSplits = true;
    cfg.perPaneLsp = true;
    cfg.tests = true;
    cfg.compileCommands = true;
    cfg.lto = true;
    cfg.gcSections = true;
    cfg.stripBinary = false;
    cfg.autoIncrementBuild = true;
}

std::vector<Section> make_sections()
{
    return {
        {"Presets",
         "High-level build profiles. vi-real is the strictest vi-style build: "
         "the editor core, welcome screen, tabs, and built-in file/buffer "
         "browser are kept. vi-min keeps the same browser tools while adding "
         "a few small conveniences, but keeps external tools and optional "
         "views out.",
         true,
         {{ItemKind::FeatureSet,
           "Feature set",
           "",
           nullptr,
           nullptr,
           {},
           "vi-real is a hard minimal build that keeps file/buffer browser "
           "support but compiles out optional popups, help views, LSP, git, "
           "fzf/rg-style search views, formatters, clipboard, color tools, "
           "terminal colors, and struct-size probes. It also disables modern "
           "convenience keybindings. "
           "vi-min keeps file/buffer browser, tabs, and color tools but "
           "compiles out git, fzf/rg-style search views, formatters, "
           "clipboard, struct-size probes, docs, tests, and LSP. Minimal keeps "
           "the normal editor tools but removes docs/tests/LSP. Basic is the "
           "default developer build. Full also enables LSP."}}},
        {"Target",
         "Platform, compiler mode, and build parallelism. Release defaults to "
         "-O2 unless another optimization level is selected.",
         true,
         {{ItemKind::Choice, "Build type", "CMAKE_BUILD_TYPE", nullptr,
           &Config::buildType, kBuildTypes,
           "CMake build type. Release is the normal default."},
          {ItemKind::Choice, "Terminal backend", "UVIM_TERMINAL_BACKEND",
           nullptr, &Config::platform, kPlatforms,
           "Select AUTO, POSIX, or WIN32 terminal backend."},
          {ItemKind::Choice, "Optimization", "UVIM_OPTIMIZATION_LEVEL", nullptr,
           &Config::optimization, kOptimizations,
           "Release optimization flag. O2 is the default; Oz is best for "
           "minimum binary size on Clang-style toolchains."},
          {ItemKind::Toggle,
           "Ninja generator",
           "",
           &Config::ninjaGenerator,
           nullptr,
           {},
           "Use CMake's Ninja generator when configuring the build. The "
           "bootstrap and build scripts enable it only when ninja is found."},
          {ItemKind::Text,
           "Build jobs",
           "UVIM_BUILD_JOBS",
           nullptr,
           nullptr,
           {},
           "Parallel build jobs used in the generated cmake --build command. "
           "Defaults to the maximum available hardware threads.",
           &Config::jobs}}},
        {"Install",
         "Installation destination. Press Space or Enter on the install dir "
         "row to edit the executable destination path.",
         true,
         {{ItemKind::Text,
           "Install dir",
           "UVIM_INSTALL_DIR",
           nullptr,
           nullptr,
           {},
           "Executable destination directory used by cmake --install. On "
           "POSIX the default is ~/.local/bin.",
           &Config::installDir}}},
        {"Editor Features",
         "User-facing features that can be disabled for very small builds. "
         "Some switches are currently compile definitions for the next source "
         "split pass.",
         true,
         {{ItemKind::Toggle,
           "Browser tools",
           "",
           &Config::browserTools,
           nullptr,
           {},
           "Build switch for the built-in file browser and buffer browser."},
          {ItemKind::Toggle,
           "Auxiliary views",
           "",
           &Config::auxiliaryViews,
           nullptr,
           {},
           "Build switch for help, loc, lspinfo, glyph select, references, "
           "command output, and other auxiliary views."},
          {ItemKind::Toggle,
           "Assembly docs",
           "UVIM_ENABLE_ASM_DOCS",
           &Config::asmDocs,
           nullptr,
           {},
           "Embeds support for gd and popup documentation on assembly "
           "instructions."},
          {ItemKind::Toggle,
           "Git tools",
           "UVIM_ENABLE_GIT_TOOLS",
           &Config::gitTools,
           nullptr,
           {},
           "Build switch for git views and git commands."},
          {ItemKind::Toggle,
           "Search tools",
           "UVIM_ENABLE_SEARCH_TOOLS",
           &Config::searchTools,
           nullptr,
           {},
           "Build switch for fuzzy, grep, and regex search views."},
          {ItemKind::Toggle,
           "Formatters",
           "UVIM_ENABLE_FORMATTERS",
           &Config::formatters,
           nullptr,
           {},
           "Build switch for external formatter integrations."},
          {ItemKind::Toggle,
           "System clipboard",
           "UVIM_ENABLE_SYSTEM_CLIPBOARD",
           &Config::systemClipboard,
           nullptr,
           {},
           "Build switch for platform clipboard integration hooks."},
          {ItemKind::Toggle,
           "Struct size popup",
           "UVIM_ENABLE_STRUCT_SIZE_POPUP",
           &Config::structSizePopup,
           nullptr,
           {},
           "Build switch for clang-based variable and struct size popups."},
          {ItemKind::Toggle,
           "ANSI color tools",
           "UVIM_ENABLE_COLOR_TOOLS",
           &Config::colorTools,
           nullptr,
           {},
          "Build switch for :ansitools, :colorpicker, :colorselect, and "
           "leader-cp/cs shortcuts."},
          {ItemKind::Toggle,
           "Terminal colors",
           "UVIM_ENABLE_TERMINAL_COLORS",
           &Config::terminalColors,
           nullptr,
           {},
           "Build switch for ANSI UI and syntax coloring. vi-real disables "
           "this for a plain terminal editor."},
          {ItemKind::Toggle,
           "Modern keybindings",
           "UVIM_ENABLE_MODERN_KEYBINDINGS",
           &Config::modernKeybindings,
           nullptr,
           {},
           "Build switch for non-vi convenience keys: Ctrl-Shift-h/j/k/l "
           "pane focus. Disable for a smaller, stricter vi-style keyset."},
          {ItemKind::Toggle,
           "Multi-pane splits",
           "UVIM_ENABLE_MULTI_PANE_SPLITS",
           &Config::multiPaneSplits,
           nullptr,
           {},
          "Build switch for nested split panes. Disable to keep the older "
           "single split pair behavior and normal-mode Ctrl-h/l buffer "
           "navigation."},
          {ItemKind::Toggle,
           "Per-pane LSP",
           "UVIM_ENABLE_PER_PANE_LSP",
           &Config::perPaneLsp,
           nullptr,
           {},
           "Refresh and draw LSP diagnostics and semantic-token state for "
           "each visible split pane. Disable for lower LSP work in large "
           "split layouts."}}},
        {"Language Servers",
         "Per-language LSP integrations. Full enables all of these by "
         "default; smaller presets compile them out unless selected here.",
         true,
         {{ItemKind::Toggle,
           "clangd C/C++",
           "UVIM_ENABLE_CLANGD_LSP",
           &Config::clangdLsp,
           nullptr,
           {},
           "Compiles in clangd integration for C and C++."},
          {ItemKind::Toggle,
           "Robot Framework",
           "UVIM_ENABLE_ROBOT_LSP",
           &Config::robotLsp,
           nullptr,
           {},
           "Compiles in Robot Framework language-server integration."},
          {ItemKind::Toggle,
           "Python",
           "UVIM_ENABLE_PYTHON_LSP",
           &Config::pythonLsp,
           nullptr,
           {},
           "Compiles in Python language-server integration."},
          {ItemKind::Toggle,
           "Mlang",
           "UVIM_ENABLE_MLANG_LSP",
           &Config::mlangLsp,
           nullptr,
           {},
           "Compiles in Mlang language-server integration."},
          {ItemKind::Toggle,
           "Mlang semantic tokens",
           "UVIM_MLANG_SEMANTIC_TOKENS_DEFAULT",
           &Config::mlangSemanticTokens,
           nullptr,
           {},
           "Default-on Mlang LSP semantic-token coloring. Only used when "
           "Mlang LSP is compiled and enabled."},
          {ItemKind::Toggle,
           "HTML",
           "UVIM_ENABLE_HTML_LSP",
           &Config::htmlLsp,
           nullptr,
           {},
           "Compiles in HTML language-server integration."},
          {ItemKind::Toggle,
           "CSS",
           "UVIM_ENABLE_CSS_LSP",
           &Config::cssLsp,
           nullptr,
           {},
           "Compiles in CSS language-server integration."},
          {ItemKind::Toggle,
           "JSON",
           "UVIM_ENABLE_JSON_LSP",
           &Config::jsonLsp,
           nullptr,
           {},
           "Compiles in JSON language-server integration."},
          {ItemKind::Toggle,
           "TypeScript/JavaScript",
           "UVIM_ENABLE_TS_LSP",
           &Config::tsLsp,
           nullptr,
           {},
           "Compiles in TypeScript and JavaScript language-server "
           "integration."}}},
        {"Build Outputs",
         "Generated files and build-only targets. These are useful for "
         "development but usually disabled for embedded-style builds.",
         true,
         {{ItemKind::Toggle,
           "Build tests",
           "UVIM_BUILD_TESTS",
           &Config::tests,
           nullptr,
           {},
           "Builds uvim unit and mode-transition test binaries."},
          {ItemKind::Toggle,
           "compile_commands.json",
           "UVIM_EXPORT_COMPILE_COMMANDS",
           &Config::compileCommands,
           nullptr,
           {},
           "Exports compile_commands.json for clangd and external tools."},
          {ItemKind::Toggle,
           "Auto build number",
           "UVIM_AUTO_INCREMENT_BUILD",
           &Config::autoIncrementBuild,
           nullptr,
           {},
           "Auto-increments the project patch/build number on configure."}}},
        {"Size And Link",
         "Advanced binary-size controls. These are the main knobs for a very "
         "small release binary.",
         true,
         {{ItemKind::Toggle,
           "LTO/IPO",
           "UVIM_ENABLE_LTO",
           &Config::lto,
           nullptr,
           {},
           "Enables link-time optimization in release builds."},
          {ItemKind::Toggle,
           "Dead-code sections",
           "UVIM_ENABLE_GC_SECTIONS",
           &Config::gcSections,
           nullptr,
           {},
           "Builds functions/data into separate sections and asks the linker "
           "to drop unused sections."},
          {ItemKind::Toggle,
           "Strip binary",
           "UVIM_STRIP_BINARY",
           &Config::stripBinary,
           nullptr,
           {},
           "Runs the platform strip tool after linking uvim."},
          {ItemKind::Toggle,
           "Static link",
           "UVIM_STATIC_LINK",
           &Config::staticLink,
           nullptr,
           {},
           "Requests static runtime/library linking where the platform "
           "toolchain supports it. macOS still links system libraries "
           "dynamically."}}},
        {"Diagnostics",
         "Developer diagnostics. Keep these off for size-sensitive builds.",
         true,
         {{ItemKind::Toggle,
           "Sanitizers",
           "UVIM_ENABLE_SANITIZERS",
           &Config::sanitizers,
           nullptr,
           {},
           "Adds AddressSanitizer and UndefinedBehaviorSanitizer."},
          {ItemKind::Toggle,
           "Debug logging",
           "UVIM_DEBUG_LOGGING",
           &Config::debugLogging,
           nullptr,
           {},
           "Enables uvim logging to the default uvim.log path."},
          {ItemKind::Toggle,
           "LSP debug logging",
           "UVIM_DEBUG_LSP",
           &Config::debugLsp,
           nullptr,
           {},
           "Enables verbose LSP logging."},
          {ItemKind::Text,
           "Log file",
           "",
           nullptr,
           nullptr,
           {},
           "Runtime uvim.log location used by the editor logging defaults. "
           "POSIX defaults to /tmp/uvim.log; Windows defaults to "
           "%USERPROFILE%\\Documents\\uvim\\uvim.log. Use uvim --log-file "
           "PATH to override it when launching uvim.",
           &Config::logFile}}},
    };
}

std::vector<VisibleRow> visible_rows(const std::vector<Section>& sections)
{
    std::vector<VisibleRow> rows;
    for(size_t section = 0; section < sections.size(); ++section)
    {
        rows.push_back({RowKind::Section, section, 0});
        if(!sections[section].open)
            continue;
        for(size_t item = 0; item < sections[section].items.size(); ++item)
            rows.push_back({RowKind::Item, section, item});
    }
    return rows;
}

std::string value_for(const Config& cfg, const Item& item)
{
    if(item.kind == ItemKind::FeatureSet)
        return kFeatureSets[static_cast<size_t>(cfg.featureSet)];
    if(item.kind == ItemKind::Choice && item.choice && !item.choices.empty())
        return item.choices[static_cast<size_t>(cfg.*(item.choice))];
    if(item.kind == ItemKind::Text && item.text)
        return cfg.*(item.text);
    return cfg.*(item.flag) ? "ON" : "OFF";
}

std::string preset_help(const Config& cfg)
{
    const std::string_view preset =
        kFeatureSets[static_cast<size_t>(cfg.featureSet)];
    if(cfg.featureSet == 0)
    {
        return std::string(preset) +
               " keeps the strict core editor only: welcome screen, editing, "
               "basic commands, tabs, and built-in file/buffer browser. It "
               "is a hard minimal build and compiles out auxiliary views, "
               "popups, help item views, LSP, git, search tools, formatters, "
               "clipboard, color tools, struct-size probes, docs/tests, "
               "terminal colors, "
               "modern convenience keybindings, and compile_commands.json.";
    }
    if(cfg.featureSet == 1)
    {
        return std::string(preset) +
               " keeps the core editor plus built-in file and buffer browser "
               "support. It still compiles out auxiliary views and popups, "
               "LSP, git, search tools, formatters, clipboard, struct-size "
               "probes, docs/tests, and compile_commands.json. Color tools "
               "are enabled by default.";
    }
    if(cfg.featureSet == 2)
    {
        return std::string(preset) +
               " keeps normal editor tools, browser tools, git, search, "
               "formatters, and clipboard, while disabling LSP, docs/tests, "
               "compile_commands.json and struct-size probes. Color tools are "
               "enabled by default.";
    }
    if(cfg.featureSet == 3)
    {
        return std::string(preset) +
               " is the normal non-LSP developer build: editor tools, browser "
               "tools, auxiliary views, git, search, formatters, clipboard, "
               "color tools, struct-size probes, tests, and "
               "compile_commands.json are enabled, but language servers are "
               "off.";
    }
    return std::string(preset) +
           " enables the complete build profile, including all language "
           "servers, editor/browser tools, auxiliary views, git, search, "
           "formatters, clipboard, color tools, struct-size probes, tests, and "
           "compile_commands.json.";
}

std::string help_for(const Config& cfg, const std::vector<Section>& sections,
                     const VisibleRow& row)
{
    if(row.kind == RowKind::Section)
        return sections[row.section].help;
    if(sections[row.section].items[row.item].kind == ItemKind::FeatureSet)
        return preset_help(cfg);
    return sections[row.section].items[row.item].help;
}

std::string truncate_line(std::string line, int cols)
{
    if(cols <= 0)
        return {};

    std::string out;
    out.reserve(line.size());
    int width = 0;
    for(size_t i = 0; i < line.size() && width < cols;)
    {
        if(line[i] == '\x1b' && i + 1 < line.size() && line[i + 1] == '[')
        {
            const size_t start = i;
            i += 2;
            while(i < line.size() && line[i] != 'm')
                ++i;
            if(i < line.size())
                ++i;
            out.append(line, start, i - start);
            continue;
        }

        out.push_back(line[i]);
        ++width;
        ++i;
    }
    return out;
}

int ansi_visible_width(std::string_view line)
{
    int width = 0;
    for(size_t i = 0; i < line.size();)
    {
        if(line[i] == '\x1b' && i + 1 < line.size() && line[i + 1] == '[')
        {
            i += 2;
            while(i < line.size() && line[i] != 'm')
                ++i;
            if(i < line.size())
                ++i;
            continue;
        }

        ++width;
        ++i;
    }
    return width;
}

std::string pad_ansi_line(std::string line, int cols)
{
    const int width = ansi_visible_width(line);
    if(width < cols)
        line.append(static_cast<size_t>(cols - width), ' ');
    return line;
}

std::string row_text(const Config& cfg, const std::vector<Section>& sections,
                     const VisibleRow& row, bool selected, bool editing)
{
    std::string out = selected ? "> " : "  ";

    if(row.kind == RowKind::Section)
    {
        const Section& section = sections[row.section];
        out += kAnsiBlue;
        out += section.open ? "[-]" : "[+]";
        out += kAnsiFgDefault;
        out += ' ';
        out += section.label;
        return out;
    }

    const Item& item = sections[row.section].items[row.item];
    const bool disabled = item.flag == &Config::staticLink && !kStaticLinkAvailable;
    if(item.kind == ItemKind::Toggle)
    {
        out += "  ";
        out += kAnsiBlue;
        out += '[';
        if(disabled)
        {
            out += kAnsiDim;
            out += '-';
            out += kAnsiReset;
        }
        else if(cfg.*(item.flag))
        {
            out += kAnsiGreen;
            out += 'X';
        }
        else
            out += ' ';
        out += kAnsiBlue;
        out += ']';
        out += kAnsiFgDefault;
        out += ' ';
    }
    else if(item.kind == ItemKind::Text)
        out += "  >   ";
    else
        out += "      ";
    if(disabled)
        out += kAnsiDim;
    out += item.label;
    const int pad = std::max(1, 31 - static_cast<int>(item.label.size()));
    out += std::string(static_cast<size_t>(pad), ' ');
    if(editing && item.kind == ItemKind::Text)
    {
        out += kAnsiEditField;
        out += value_for(cfg, item);
        out += "\x1b[5m_\x1b[25m";
        out += kAnsiReset;
        if(selected)
            out += kAnsiCurrentLineBg;
    }
    else
        out += value_for(cfg, item);
    if(disabled)
        out += kAnsiReset;
    return out;
}

std::vector<std::string> wrap_text(std::string_view text, int width)
{
    width = std::max(1, width);

    std::vector<std::string> lines;
    std::string current;
    size_t pos = 0;
    while(pos < text.size())
    {
        while(pos < text.size() && text[pos] == ' ')
            ++pos;
        size_t start = pos;
        while(pos < text.size() && text[pos] != ' ')
            ++pos;
        if(start == pos)
            break;

        std::string word(text.substr(start, pos - start));
        while(static_cast<int>(word.size()) > width)
        {
            if(!current.empty())
            {
                lines.push_back(current);
                current.clear();
            }
            lines.push_back(word.substr(0, static_cast<size_t>(width)));
            word.erase(0, static_cast<size_t>(width));
        }

        if(word.empty())
            continue;
        const int extra = current.empty() ? 0 : 1;
        if(static_cast<int>(current.size() + word.size() + extra) > width)
        {
            lines.push_back(current);
            current = std::move(word);
        }
        else
        {
            if(!current.empty())
                current += ' ';
            current += word;
        }
    }

    if(!current.empty())
        lines.push_back(current);
    if(lines.empty())
        lines.push_back("");
    return lines;
}

int longest_documentation_height(const std::vector<Section>& sections, int cols)
{
    int height = 1;
    for(const auto& section : sections)
    {
        height = std::max(
            height, static_cast<int>(wrap_text(section.help, cols).size()));
        for(const auto& item : section.items)
            height = std::max(
                height, static_cast<int>(wrap_text(item.help, cols).size()));
    }

    return height;
}

Layout make_layout(const std::vector<Section>& sections, TerminalSize screen)
{
    Layout layout;
    layout.keyRows = static_cast<int>(
        wrap_text("j/k/arrows move  h close  l open  space/enter change  "
                  "s save (Y/n)  q quit",
                  screen.cols)
            .size());
    layout.outputRows = static_cast<int>(
        wrap_text("Output: build/uvim_config_cache.cmake", screen.cols).size());

    const int fixedWithoutListAndDoc = layout.titleRows + layout.keyRows +
                                       layout.spacerRows + layout.docTitleRows +
                                       layout.outputRows;
    layout.messageRows =
        std::min(layout.messageRows,
                 std::max(1, screen.rows - fixedWithoutListAndDoc - 2));

    const int nonDocRows = fixedWithoutListAndDoc + layout.messageRows;
    const int availableForListAndDoc = std::max(2, screen.rows - nonDocRows);
    const int longestDoc = longest_documentation_height(sections, screen.cols);
    const int maxDocRows = std::max(1, availableForListAndDoc / 2);

    layout.docRows = std::clamp(longestDoc, 1, maxDocRows);
    layout.listRows = std::max(1, availableForListAndDoc - layout.docRows);
    return layout;
}

int list_content_rows_for_offset(int scrollOffset, int rowCount, int listHeight)
{
    if(rowCount <= 0 || listHeight <= 0)
        return 0;

    const int topMarkerRows = (scrollOffset > 0 && listHeight > 1) ? 1 : 0;
    int contentRows = std::max(1, listHeight - topMarkerRows);
    const bool needsBottomMarker =
        scrollOffset + contentRows < rowCount && contentRows > 1;
    if(needsBottomMarker)
        --contentRows;
    return std::max(1, contentRows);
}

int clamp_scroll(int scrollOffset, int cursor, int rowCount, int listHeight)
{
    if(rowCount <= 0)
        return 0;

    scrollOffset = std::clamp(scrollOffset, 0, rowCount - 1);
    cursor = std::clamp(cursor, 0, rowCount - 1);

    for(int i = 0; i < rowCount + 2; ++i)
    {
        const int contentRows =
            list_content_rows_for_offset(scrollOffset, rowCount, listHeight);
        if(cursor < scrollOffset)
            scrollOffset = cursor;
        else if(cursor >= scrollOffset + contentRows)
            scrollOffset = cursor - contentRows + 1;
        else
            break;
        scrollOffset = std::clamp(scrollOffset, 0, rowCount - 1);
    }

    return scrollOffset;
}

void append_wrapped_fixed(std::vector<std::string>& out, std::string_view text,
                          int rows, int cols)
{
    std::vector<std::string> lines = wrap_text(text, cols);
    for(int i = 0; i < rows; ++i)
    {
        if(i < static_cast<int>(lines.size()))
            out.push_back(truncate_line(lines[static_cast<size_t>(i)], cols));
        else
            out.push_back("");
    }
}

void append_documentation(std::vector<std::string>& out, const Config& cfg,
                          const std::vector<Section>& sections,
                          const VisibleRow& row, int docHeight, int cols)
{
    out.push_back("\x1b[1mDocumentation\x1b[0m");
    append_wrapped_fixed(out, help_for(cfg, sections, row), docHeight, cols);
}

void draw(const Config& cfg, const std::vector<Section>& sections, int cursor,
          int scrollOffset, TerminalSize screen, std::string_view message,
          bool editingText)
{
    static std::vector<std::string> previousScreenLines;
    static TerminalSize previousScreen{};

    const std::vector<VisibleRow> rows = visible_rows(sections);
    const int safeCursor =
        std::clamp(cursor, 0, std::max(0, static_cast<int>(rows.size()) - 1));
    const Layout layout = make_layout(sections, screen);
    const int listHeight = layout.listRows;

    std::vector<std::string> screenLines;
    screenLines.reserve(static_cast<size_t>(std::max(1, screen.rows)));

    screenLines.push_back(
        "\x1b[1;36m" + truncate_line("uvim build configurator", screen.cols) +
        "\x1b[0m");
    append_wrapped_fixed(screenLines,
                         "j/k/arrows move  h/left close  l/right open  "
                         "space/enter change  s save (Y/n)  q quit",
                         layout.keyRows, screen.cols);
    screenLines.push_back("");

    int listRowsPrinted = 0;
    const int rowCount = static_cast<int>(rows.size());
    if(scrollOffset > 0 && listHeight > 1 && listRowsPrinted < listHeight)
    {
        screenLines.push_back(truncate_line("  ...", screen.cols));
        ++listRowsPrinted;
    }

    int contentRows =
        list_content_rows_for_offset(scrollOffset, rowCount, listHeight);
    int endRow = std::min(rowCount, scrollOffset + contentRows);
    const bool bottomMarker =
        endRow < rowCount && listRowsPrinted + contentRows < listHeight;

    for(int i = scrollOffset; i < endRow && listRowsPrinted < listHeight; ++i)
    {
        std::string line = truncate_line(
            row_text(cfg, sections, rows[static_cast<size_t>(i)],
                     i == safeCursor, editingText && i == safeCursor),
            screen.cols);
        if(i == safeCursor)
        {
            line = pad_ansi_line(std::move(line), screen.cols);
            line = std::string(kAnsiCurrentLineBg) + line +
                   std::string(kAnsiReset);
        }
        screenLines.push_back(std::move(line));
        ++listRowsPrinted;
    }

    if(bottomMarker && listRowsPrinted < listHeight)
    {
        screenLines.push_back(truncate_line("  ...", screen.cols));
        ++listRowsPrinted;
    }

    for(int i = listRowsPrinted; i < listHeight; ++i)
        screenLines.push_back("");

    if(!rows.empty())
        append_documentation(screenLines, cfg, sections,
                             rows[static_cast<size_t>(safeCursor)],
                             layout.docRows, screen.cols);
    append_wrapped_fixed(screenLines, "Output: build/uvim_config_cache.cmake",
                         layout.outputRows, screen.cols);
    if(!message.empty())
    {
        std::vector<std::string> lines = wrap_text(message, screen.cols);
        for(int i = 0; i < layout.messageRows; ++i)
        {
            if(i < static_cast<int>(lines.size()))
            {
                screenLines.push_back(
                    "\x1b[1;32m" +
                    truncate_line(lines[static_cast<size_t>(i)], screen.cols) +
                    "\x1b[0m");
            }
            else
                screenLines.push_back("");
        }
    }
    else
    {
        for(int i = 0; i < layout.messageRows; ++i)
            screenLines.push_back("");
    }

    while(static_cast<int>(screenLines.size()) < screen.rows)
        screenLines.push_back("");
    if(static_cast<int>(screenLines.size()) > screen.rows)
        screenLines.resize(static_cast<size_t>(screen.rows));

    auto cursor_position = [](size_t row) -> std::string
    { return "\x1b[" + std::to_string(row + 1) + ";1H"; };

    const bool fullRedraw =
        previousScreenLines.empty() || previousScreen.rows != screen.rows ||
        previousScreen.cols != screen.cols ||
        previousScreenLines.size() != screenLines.size();

    std::string output;
    output.reserve(static_cast<size_t>(std::max(1, screen.rows)) *
                   static_cast<size_t>(std::max(1, screen.cols + 16)));
    output += "\x1b[?25l";

    if(fullRedraw)
    {
        output += "\x1b[H\x1b[2J";
        for(size_t i = 0; i < screenLines.size(); ++i)
        {
            if(i > 0)
                output += "\n";
            output += screenLines[i];
        }
    }
    else
    {
        for(size_t i = 0; i < screenLines.size(); ++i)
        {
            if(screenLines[i] == previousScreenLines[i])
                continue;
            output += cursor_position(i);
            output += "\x1b[K";
            output += screenLines[i];
        }
    }

    if(output != "\x1b[?25l")
        write_stdout(output);
    flush_stdout();

    previousScreenLines = std::move(screenLines);
    previousScreen = screen;
}

fs::path output_path(const CliOptions& options)
{
    if(options.output)
        return fs::absolute(*options.output);
    return fs::absolute(options.buildDir / "uvim_config_cache.cmake");
}

fs::path config_path(const CliOptions& options)
{
    return fs::absolute(options.buildDir / "uvim-config.conf");
}

fs::path expand_user_path(std::string_view path, const fs::path& base)
{
    std::string value(path);
    if(value == "~" || value.rfind("~/", 0) == 0)
    {
        if(const char* home = std::getenv("HOME"))
            value = std::string(home) + value.substr(1);
    }

    fs::path result(value);
    if(result.is_relative())
        result = base / result;
    return fs::absolute(result).lexically_normal();
}

std::string install_command_suffix(const CliOptions& options)
{
    if(!options.installAfterBuild)
        return {};
    return " && cmake --install " + fs::absolute(options.buildDir).string() +
           " --component uvim";
}

std::string configure_command(const Config& cfg, const CliOptions& options,
                              const fs::path& cacheFile)
{
    std::string command = "cmake -C " + cacheFile.string() + " -S " +
                          fs::absolute(options.sourceDir).string() + " -B " +
                          fs::absolute(options.buildDir).string();
    if(cfg.ninjaGenerator)
        command += " -G Ninja";
    return command;
}

std::string build_command(const Config& cfg, const CliOptions& options)
{
    std::string command =
        "cmake --build " + fs::absolute(options.buildDir).string();
    if(!cfg.jobs.empty())
        command += " --parallel " + cfg.jobs;
    return command;
}

bool apply_config_value(Config& cfg, CliOptions& options,
                        std::string_view rawKey, std::string_view rawValue,
                        std::string& error)
{
    const std::string key = trim(rawKey);
    const std::string value = trim(rawValue);

    if(key == "preset")
    {
        auto index = choice_index(value, kFeatureSets);
        if(!index)
        {
            error = "invalid preset '" + value + "'";
            return false;
        }
        cfg.featureSet = *index;
        apply_feature_set(cfg);
    }
    else if(key == "config")
    {
        auto index = choice_index(value, kBuildTypes);
        if(!index)
        {
            error = "invalid config '" + value + "'";
            return false;
        }
        cfg.buildType = *index;
    }
    else if(key == "platform")
    {
        auto index = choice_index(value, kPlatforms);
        if(!index)
        {
            error = "invalid platform '" + value + "'";
            return false;
        }
        cfg.platform = *index;
    }
    else if(key == "optimization")
    {
        auto index = choice_index(value, kOptimizations);
        if(!index)
        {
            error = "invalid optimization '" + value + "'";
            return false;
        }
        cfg.optimization = *index;
    }
    else if(key == "ninja_generator")
    {
        auto parsed = parse_bool(value);
        if(!parsed)
        {
            error = "invalid ninja_generator value '" + value + "'";
            return false;
        }
        cfg.ninjaGenerator = *parsed;
    }
    else if(key == "jobs")
        cfg.jobs = value;
    else if(key == "install_dir")
        cfg.installDir = value;
    else if(key == "log_file")
        cfg.logFile = value;
    else if(key == "install_after_build")
    {
        auto parsed = parse_bool(value);
        if(!parsed)
        {
            error = "invalid install_after_build value '" + value + "'";
            return false;
        }
        options.installAfterBuild = *parsed;
    }
    else if(key == "clangd_lsp")
        cfg.clangdLsp = parse_bool(value).value_or(cfg.clangdLsp);
    else if(key == "robot_lsp")
        cfg.robotLsp = parse_bool(value).value_or(cfg.robotLsp);
    else if(key == "python_lsp")
        cfg.pythonLsp = parse_bool(value).value_or(cfg.pythonLsp);
    else if(key == "mlang_lsp")
        cfg.mlangLsp = parse_bool(value).value_or(cfg.mlangLsp);
    else if(key == "mlang_semantic_tokens")
        cfg.mlangSemanticTokens =
            parse_bool(value).value_or(cfg.mlangSemanticTokens);
    else if(key == "html_lsp")
        cfg.htmlLsp = parse_bool(value).value_or(cfg.htmlLsp);
    else if(key == "css_lsp")
        cfg.cssLsp = parse_bool(value).value_or(cfg.cssLsp);
    else if(key == "json_lsp")
        cfg.jsonLsp = parse_bool(value).value_or(cfg.jsonLsp);
    else if(key == "ts_lsp")
        cfg.tsLsp = parse_bool(value).value_or(cfg.tsLsp);
    else if(key == "asm_docs")
        cfg.asmDocs = parse_bool(value).value_or(cfg.asmDocs);
    else if(key == "browser_tools")
        cfg.browserTools = parse_bool(value).value_or(cfg.browserTools);
    else if(key == "auxiliary_views")
        cfg.auxiliaryViews = parse_bool(value).value_or(cfg.auxiliaryViews);
    else if(key == "git_tools")
        cfg.gitTools = parse_bool(value).value_or(cfg.gitTools);
    else if(key == "search_tools")
        cfg.searchTools = parse_bool(value).value_or(cfg.searchTools);
    else if(key == "formatters")
        cfg.formatters = parse_bool(value).value_or(cfg.formatters);
    else if(key == "system_clipboard")
        cfg.systemClipboard = parse_bool(value).value_or(cfg.systemClipboard);
    else if(key == "struct_size_popup")
        cfg.structSizePopup = parse_bool(value).value_or(cfg.structSizePopup);
    else if(key == "color_tools")
        cfg.colorTools = parse_bool(value).value_or(cfg.colorTools);
    else if(key == "terminal_colors")
        cfg.terminalColors = parse_bool(value).value_or(cfg.terminalColors);
    else if(key == "modern_keybindings")
        cfg.modernKeybindings =
            parse_bool(value).value_or(cfg.modernKeybindings);
    else if(key == "multi_pane_splits")
        cfg.multiPaneSplits = parse_bool(value).value_or(cfg.multiPaneSplits);
    else if(key == "per_pane_lsp")
        cfg.perPaneLsp = parse_bool(value).value_or(cfg.perPaneLsp);
    else if(key == "tests")
        cfg.tests = parse_bool(value).value_or(cfg.tests);
    else if(key == "compile_commands")
        cfg.compileCommands = parse_bool(value).value_or(cfg.compileCommands);
    else if(key == "auto_increment_build")
        cfg.autoIncrementBuild =
            parse_bool(value).value_or(cfg.autoIncrementBuild);
    else if(key == "lto")
        cfg.lto = parse_bool(value).value_or(cfg.lto);
    else if(key == "gc_sections")
        cfg.gcSections = parse_bool(value).value_or(cfg.gcSections);
    else if(key == "strip_binary")
        cfg.stripBinary = parse_bool(value).value_or(cfg.stripBinary);
    else if(key == "static_link")
    {
        cfg.staticLink =
            kStaticLinkAvailable &&
            parse_bool(value).value_or(cfg.staticLink);
    }
    else if(key == "sanitizers")
        cfg.sanitizers = parse_bool(value).value_or(cfg.sanitizers);
    else if(key == "debug_logging")
        cfg.debugLogging = parse_bool(value).value_or(cfg.debugLogging);
    else if(key == "debug_lsp")
        cfg.debugLsp = parse_bool(value).value_or(cfg.debugLsp);
    else
    {
        error = "unknown config key '" + key + "'";
        return false;
    }

    return true;
}

bool load_config_file(const fs::path& path, Config& cfg, CliOptions& options,
                      std::string& error)
{
    std::ifstream file(path);
    if(!file)
    {
        error = "cannot read " + path.string();
        return false;
    }

    std::string line;
    int lineNo = 0;
    while(std::getline(file, line))
    {
        ++lineNo;
        std::string trimmed = trim(line);
        if(trimmed.empty() || trimmed[0] == '#')
            continue;

        const size_t equals = trimmed.find('=');
        if(equals == std::string::npos)
        {
            error = path.string() + ":" + std::to_string(lineNo) +
                    ": expected key=value";
            return false;
        }

        if(!apply_config_value(cfg, options, trimmed.substr(0, equals),
                               trimmed.substr(equals + 1), error))
        {
            error = path.string() + ":" + std::to_string(lineNo) + ": " + error;
            return false;
        }
    }

    return true;
}

bool try_load_config_file(const fs::path& path, Config& cfg,
                          CliOptions& options, std::string& message)
{
    std::error_code ec;
    if(!fs::exists(path, ec))
        return false;

    std::string error;
    if(!load_config_file(path, cfg, options, error))
    {
        message = error;
        return false;
    }

    message = "loaded " + path.string();
    return true;
}

bool write_config_file(const Config& cfg, const CliOptions& options,
                       std::string& error)
{
    const fs::path path = config_path(options);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if(ec)
    {
        error = "cannot create " + path.parent_path().string();
        return false;
    }

    std::ofstream file(path);
    if(!file)
    {
        error = "cannot write " + path.string();
        return false;
    }

    file << "# Generated by uvim-config.\n";
    file << "preset=" << kFeatureSets[static_cast<size_t>(cfg.featureSet)]
         << "\n";
    file << "config=" << kBuildTypes[static_cast<size_t>(cfg.buildType)]
         << "\n";
    file << "platform=" << kPlatforms[static_cast<size_t>(cfg.platform)]
         << "\n";
    file << "optimization="
         << kOptimizations[static_cast<size_t>(cfg.optimization)] << "\n";
    file << "ninja_generator=" << bool_value(cfg.ninjaGenerator) << "\n";
    file << "jobs=" << cfg.jobs << "\n";
    file << "install_dir=" << cfg.installDir << "\n";
    file << "log_file=" << cfg.logFile << "\n";
    file << "install_after_build=" << bool_value(options.installAfterBuild)
         << "\n";
    file << "clangd_lsp=" << bool_value(cfg.clangdLsp) << "\n";
    file << "robot_lsp=" << bool_value(cfg.robotLsp) << "\n";
    file << "python_lsp=" << bool_value(cfg.pythonLsp) << "\n";
    file << "mlang_lsp=" << bool_value(cfg.mlangLsp) << "\n";
    file << "mlang_semantic_tokens=" << bool_value(cfg.mlangSemanticTokens)
         << "\n";
    file << "html_lsp=" << bool_value(cfg.htmlLsp) << "\n";
    file << "css_lsp=" << bool_value(cfg.cssLsp) << "\n";
    file << "json_lsp=" << bool_value(cfg.jsonLsp) << "\n";
    file << "ts_lsp=" << bool_value(cfg.tsLsp) << "\n";
    file << "asm_docs=" << bool_value(cfg.asmDocs) << "\n";
    file << "browser_tools=" << bool_value(cfg.browserTools) << "\n";
    file << "auxiliary_views=" << bool_value(cfg.auxiliaryViews) << "\n";
    file << "git_tools=" << bool_value(cfg.gitTools) << "\n";
    file << "search_tools=" << bool_value(cfg.searchTools) << "\n";
    file << "formatters=" << bool_value(cfg.formatters) << "\n";
    file << "system_clipboard=" << bool_value(cfg.systemClipboard) << "\n";
    file << "struct_size_popup=" << bool_value(cfg.structSizePopup) << "\n";
    file << "color_tools=" << bool_value(cfg.colorTools) << "\n";
    file << "terminal_colors=" << bool_value(cfg.terminalColors) << "\n";
    file << "modern_keybindings=" << bool_value(cfg.modernKeybindings) << "\n";
    file << "multi_pane_splits=" << bool_value(cfg.multiPaneSplits) << "\n";
    file << "per_pane_lsp=" << bool_value(cfg.perPaneLsp) << "\n";
    file << "tests=" << bool_value(cfg.tests) << "\n";
    file << "compile_commands=" << bool_value(cfg.compileCommands) << "\n";
    file << "auto_increment_build=" << bool_value(cfg.autoIncrementBuild)
         << "\n";
    file << "lto=" << bool_value(cfg.lto) << "\n";
    file << "gc_sections=" << bool_value(cfg.gcSections) << "\n";
    file << "strip_binary=" << bool_value(cfg.stripBinary) << "\n";
    file << "static_link=" << bool_value(cfg.staticLink) << "\n";
    file << "sanitizers=" << bool_value(cfg.sanitizers) << "\n";
    file << "debug_logging=" << bool_value(cfg.debugLogging) << "\n";
    file << "debug_lsp=" << bool_value(cfg.debugLsp) << "\n";
    return true;
}

bool write_cache(const Config& cfg, const std::vector<Section>& sections,
                 const CliOptions& options, std::string& message)
{
    fs::path out = output_path(options);
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    if(ec)
    {
        message = "cannot create " + out.parent_path().string();
        return false;
    }

    std::ofstream file(out);
    if(!file)
    {
        message = "cannot write " + out.string();
        return false;
    }

    file << "# Generated by uvim-config. Pass with: "
         << configure_command(cfg, options, out) << "\n";
    file << "# Runtime uvim log file: " << cfg.logFile << "\n";
    const fs::path installDir =
        expand_user_path(cfg.installDir, fs::absolute(options.sourceDir));
    const fs::path installPrefix =
        fs::absolute(options.buildDir / "install").lexically_normal();
    file << "set(CMAKE_INSTALL_PREFIX \""
         << cmake_string_literal(installPrefix.string())
         << "\" CACHE PATH \"\" FORCE)\n";
    file << "set(UVIM_INSTALL_BINDIR \""
         << cmake_string_literal(installDir.string())
         << "\" CACHE STRING \"\" FORCE)\n";
    file << "set(UVIM_MINIMAL " << (cfg.minimal ? "ON" : "OFF")
         << " CACHE BOOL \"\" FORCE)\n";
    file << "set(UVIM_ENABLE_BROWSER_TOOLS "
         << (cfg.browserTools ? "ON" : "OFF")
         << " CACHE BOOL \"\" FORCE)\n";
    file << "set(UVIM_ENABLE_AUXILIARY_VIEWS "
         << (cfg.auxiliaryViews ? "ON" : "OFF")
         << " CACHE BOOL \"\" FORCE)\n";
    for(const auto& section : sections)
    {
        for(const auto& item : section.items)
        {
            if(item.kind == ItemKind::Choice)
            {
                file << "set(" << item.cmakeName << " \""
                     << cmake_string_literal(
                            item.choices[static_cast<size_t>(
                                cfg.*(item.choice))])
                     << "\" CACHE STRING \"\" FORCE)\n";
            }
            else if(item.kind == ItemKind::Text)
                continue;
            else if(item.cmakeName.empty())
                continue;
            else if(item.kind == ItemKind::Toggle)
            {
                file << "set(" << item.cmakeName << " "
                     << ((cfg.*(item.flag)) ? "ON" : "OFF")
                     << " CACHE BOOL \"\" FORCE)\n";
            }
        }
    }

    message = "saved. Run: " + configure_command(cfg, options, out) + " && " +
              build_command(cfg, options) + install_command_suffix(options);
    std::string configError;
    if(!write_config_file(cfg, options, configError))
    {
        message += ". " + configError;
        return false;
    }
    message += ". Config: " + config_path(options).string();
    return true;
}

void print_help(std::ostream& out)
{
    out << "uvim-config [options]\n\n"
        << "Without options, starts the interactive TUI.\n\n"
        << "Options:\n"
        << "  -h, --help                  Show this help.\n"
        << "  -p, --preset NAME           Build preset: "
        << join_choices(kFeatureSets) << " (default: full).\n"
        << "  -c, --config NAME           CMake build type: "
        << join_choices(kBuildTypes) << " (default: Release).\n"
        << "      --platform NAME         Terminal backend: "
        << join_choices(kPlatforms) << " (default: AUTO).\n"
        << "  -O, --optimization LEVEL    Release optimization: "
        << join_choices(kOptimizations) << " (default: O2).\n"
        << "      --ninja                 Use CMake's Ninja generator "
           "(default: on).\n"
        << "      --no-ninja              Do not request the Ninja generator.\n"
        << "  -j, --jobs N                Parallel build jobs (default: max "
           "hardware cores).\n"
        << "  -S, --source-dir DIR        Source directory for the printed "
           "cmake "
           "command.\n"
        << "  -B, --build-dir DIR         Build directory and default cache "
           "output.\n"
        << "      --import FILE           Load uvim-config.conf before later "
           "CLI overrides.\n"
        << "      --install-dir DIR       Executable install destination "
           "(default: ~/.local/bin on POSIX).\n"
        << "      --log-file FILE         Runtime uvim log path shown in the "
           "generated config (default: platform uvim.log path).\n"
        << "  -i, --install               Include cmake --install in the "
           "printed command.\n"
        << "  -o, --output FILE           Cache file to write.\n"
        << "      --enable NAME           Enable a feature option.\n"
        << "      --disable NAME          Disable a feature option.\n\n"
        << "Feature names for --enable/--disable:\n"
        << "  clangd, robot-lsp, python-lsp, mlang-lsp, "
           "mlang-semantic-tokens, html-lsp,\n"
        << "  css-lsp, json-lsp, ts-lsp, browser-tools, auxiliary-views, "
           "asm-docs,\n"
        << "  git, search, formatters, clipboard,\n"
        << "  struct-size, color-tools, terminal-colors, tests, compile-commands, "
           "modern-keybindings, multi-pane-splits, per-pane-lsp,\n"
        << "  auto-build-number,\n"
        << "  lto, gc-sections, strip, static, static-link, sanitizers, "
           "debug-logging, debug-lsp\n\n"
        << "Examples:\n"
        << "  uvim-config --preset vi-real --config Release -O Oz -j 8\n"
        << "  uvim-config --import build/uvim-config.conf --disable tests\n"
        << "  uvim-config -p full -c RelWithDebInfo --enable clangd\n"
        << "  uvim-config -p vi-real --install-dir ~/.local/bin --install\n";
}

bool set_feature(Config& cfg, std::string_view name, bool enabled,
                 std::string& error)
{
    if(equals_ci(name, "clangd") || equals_ci(name, "lsp") ||
       equals_ci(name, "clangd-lsp"))
        cfg.clangdLsp = enabled;
    else if(equals_ci(name, "robot-lsp") || equals_ci(name, "robot"))
        cfg.robotLsp = enabled;
    else if(equals_ci(name, "python-lsp") || equals_ci(name, "python"))
        cfg.pythonLsp = enabled;
    else if(equals_ci(name, "mlang-lsp") || equals_ci(name, "mlang"))
        cfg.mlangLsp = enabled;
    else if(equals_ci(name, "mlang-semantic-tokens") ||
            equals_ci(name, "mlang-semantic"))
        cfg.mlangSemanticTokens = enabled;
    else if(equals_ci(name, "html-lsp") || equals_ci(name, "html"))
        cfg.htmlLsp = enabled;
    else if(equals_ci(name, "css-lsp") || equals_ci(name, "css"))
        cfg.cssLsp = enabled;
    else if(equals_ci(name, "json-lsp") || equals_ci(name, "json"))
        cfg.jsonLsp = enabled;
    else if(equals_ci(name, "ts-lsp") || equals_ci(name, "typescript-lsp") ||
            equals_ci(name, "typescript") || equals_ci(name, "javascript-lsp"))
        cfg.tsLsp = enabled;
    else if(equals_ci(name, "asm-docs") || equals_ci(name, "asm"))
        cfg.asmDocs = enabled;
    else if(equals_ci(name, "browser-tools") ||
            equals_ci(name, "browser") ||
            equals_ci(name, "file-browser") ||
            equals_ci(name, "buffer-browser"))
        cfg.browserTools = enabled;
    else if(equals_ci(name, "auxiliary-views") ||
            equals_ci(name, "aux-views") ||
            equals_ci(name, "auxiliary") ||
            equals_ci(name, "popups") ||
            equals_ci(name, "help"))
        cfg.auxiliaryViews = enabled;
    else if(equals_ci(name, "git") || equals_ci(name, "git-tools"))
        cfg.gitTools = enabled;
    else if(equals_ci(name, "search") || equals_ci(name, "search-tools"))
        cfg.searchTools = enabled;
    else if(equals_ci(name, "formatters") || equals_ci(name, "formatter"))
        cfg.formatters = enabled;
    else if(equals_ci(name, "clipboard") || equals_ci(name, "system-clipboard"))
        cfg.systemClipboard = enabled;
    else if(equals_ci(name, "struct-size") ||
            equals_ci(name, "struct-size-popup"))
        cfg.structSizePopup = enabled;
    else if(equals_ci(name, "color-tools") || equals_ci(name, "color") ||
            equals_ci(name, "ansitools") || equals_ci(name, "ansi-tools") ||
            equals_ci(name, "colorpicker") || equals_ci(name, "color-picker") ||
            equals_ci(name, "colorselect") ||
            equals_ci(name, "color-selector"))
        cfg.colorTools = enabled;
    else if(equals_ci(name, "terminal-colors") ||
            equals_ci(name, "terminal-color") ||
            equals_ci(name, "ui-colors") || equals_ci(name, "syntax-colors") ||
            equals_ci(name, "colors"))
        cfg.terminalColors = enabled;
    else if(equals_ci(name, "modern-keybindings") ||
            equals_ci(name, "modern-keys") ||
            equals_ci(name, "convenience-keys") ||
            equals_ci(name, "pane-keys") ||
            equals_ci(name, "split-keys"))
        cfg.modernKeybindings = enabled;
    else if(equals_ci(name, "multi-pane-splits") ||
            equals_ci(name, "multi-panes") ||
            equals_ci(name, "nested-splits") ||
            equals_ci(name, "pane-tree"))
        cfg.multiPaneSplits = enabled;
    else if(equals_ci(name, "per-pane-lsp") ||
            equals_ci(name, "pane-lsp") ||
            equals_ci(name, "split-lsp") ||
            equals_ci(name, "lsp-per-pane"))
        cfg.perPaneLsp = enabled;
    else if(equals_ci(name, "tests") || equals_ci(name, "test"))
        cfg.tests = enabled;
    else if(equals_ci(name, "compile-commands") ||
            equals_ci(name, "compile-commands-json"))
        cfg.compileCommands = enabled;
    else if(equals_ci(name, "auto-build-number") ||
            equals_ci(name, "auto-increment-build"))
        cfg.autoIncrementBuild = enabled;
    else if(equals_ci(name, "lto") || equals_ci(name, "ipo"))
        cfg.lto = enabled;
    else if(equals_ci(name, "gc-sections") ||
            equals_ci(name, "dead-code-sections"))
        cfg.gcSections = enabled;
    else if(equals_ci(name, "strip") || equals_ci(name, "strip-binary"))
        cfg.stripBinary = enabled;
    else if(equals_ci(name, "static") || equals_ci(name, "static-link") ||
            equals_ci(name, "static-runtime") ||
            equals_ci(name, "static-libs") ||
            equals_ci(name, "static-libraries"))
    {
        if(enabled && !kStaticLinkAvailable)
        {
            error = "static linking is not available on this platform";
            return false;
        }
        cfg.staticLink = enabled;
    }
    else if(equals_ci(name, "sanitizers") || equals_ci(name, "sanitize"))
        cfg.sanitizers = enabled;
    else if(equals_ci(name, "debug-logging"))
        cfg.debugLogging = enabled;
    else if(equals_ci(name, "debug-lsp") || equals_ci(name, "lsp-debug"))
        cfg.debugLsp = enabled;
    else
    {
        error = "unknown feature '" + std::string(name) + "'";
        return false;
    }
    return true;
}

std::optional<std::string_view> next_arg(int& i, int argc, char** argv,
                                         std::string_view option,
                                         std::string& error)
{
    if(i + 1 >= argc)
    {
        error = "missing value for " + std::string(option);
        return std::nullopt;
    }
    ++i;
    return std::string_view(argv[i]);
}

bool parse_cli(int argc, char** argv, Config& cfg, CliOptions& options,
               bool& showHelp, std::string& error)
{
    showHelp = false;
    for(int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if(arg == "-h" || arg == "--help")
        {
            showHelp = true;
            return true;
        }
        if(arg == "-p" || arg == "--preset")
        {
            auto value = next_arg(i, argc, argv, arg, error);
            if(!value)
                return false;
            auto index = choice_index(*value, kFeatureSets);
            if(!index)
            {
                error = "invalid preset '" + std::string(*value) +
                        "'. Expected one of: " + join_choices(kFeatureSets);
                return false;
            }
            cfg.featureSet = *index;
            apply_feature_set(cfg);
        }
        else if(arg == "-c" || arg == "--config")
        {
            auto value = next_arg(i, argc, argv, arg, error);
            if(!value)
                return false;
            auto index = choice_index(*value, kBuildTypes);
            if(!index)
            {
                error = "invalid config '" + std::string(*value) +
                        "'. Expected one of: " + join_choices(kBuildTypes);
                return false;
            }
            cfg.buildType = *index;
        }
        else if(arg == "--platform")
        {
            auto value = next_arg(i, argc, argv, arg, error);
            if(!value)
                return false;
            auto index = choice_index(*value, kPlatforms);
            if(!index)
            {
                error = "invalid platform '" + std::string(*value) +
                        "'. Expected one of: " + join_choices(kPlatforms);
                return false;
            }
            cfg.platform = *index;
        }
        else if(arg == "-O" || arg == "--optimization")
        {
            auto value = next_arg(i, argc, argv, arg, error);
            if(!value)
                return false;
            auto index = choice_index(*value, kOptimizations);
            if(!index)
            {
                error = "invalid optimization '" + std::string(*value) +
                        "'. Expected one of: " + join_choices(kOptimizations);
                return false;
            }
            cfg.optimization = *index;
        }
        else if(arg == "--ninja")
        {
            cfg.ninjaGenerator = true;
        }
        else if(arg == "--no-ninja")
        {
            cfg.ninjaGenerator = false;
        }
        else if(arg == "-j" || arg == "--jobs")
        {
            auto value = next_arg(i, argc, argv, arg, error);
            if(!value)
                return false;
            cfg.jobs = std::string(*value);
        }
        else if(arg == "-S" || arg == "--source-dir")
        {
            auto value = next_arg(i, argc, argv, arg, error);
            if(!value)
                return false;
            options.sourceDir = fs::absolute(fs::path(std::string(*value)));
        }
        else if(arg == "-B" || arg == "--build-dir")
        {
            auto value = next_arg(i, argc, argv, arg, error);
            if(!value)
                return false;
            options.buildDir = fs::absolute(fs::path(std::string(*value)));
        }
        else if(arg == "--import")
        {
            auto value = next_arg(i, argc, argv, arg, error);
            if(!value)
                return false;
            if(!load_config_file(fs::absolute(fs::path(std::string(*value))),
                                 cfg, options, error))
                return false;
        }
        else if(arg == "--install-dir")
        {
            auto value = next_arg(i, argc, argv, arg, error);
            if(!value)
                return false;
            cfg.installDir = std::string(*value);
        }
        else if(arg == "--log-file")
        {
            auto value = next_arg(i, argc, argv, arg, error);
            if(!value)
                return false;
            cfg.logFile = std::string(*value);
        }
        else if(arg == "-i" || arg == "--install")
        {
            options.installAfterBuild = true;
        }
        else if(arg == "-o" || arg == "--output")
        {
            auto value = next_arg(i, argc, argv, arg, error);
            if(!value)
                return false;
            options.output = fs::absolute(fs::path(std::string(*value)));
        }
        else if(arg == "--enable" || arg == "--disable")
        {
            auto value = next_arg(i, argc, argv, arg, error);
            if(!value)
                return false;
            if(!set_feature(cfg, *value, arg == "--enable", error))
                return false;
        }
        else
        {
            error = "unknown option '" + std::string(arg) + "'";
            return false;
        }
    }
    return true;
}

void activate(Config& cfg, const Item& item)
{
    if(item.flag == &Config::staticLink && !kStaticLinkAvailable)
        return;

    if(item.kind == ItemKind::FeatureSet)
    {
        cfg.featureSet = (cfg.featureSet + 1) % (int)kFeatureSets.size();
        apply_feature_set(cfg);
    }
    else if(item.kind == ItemKind::Choice && item.choice &&
            !item.choices.empty())
    {
        cfg.*(item.choice) =
            (cfg.*(item.choice) + 1) % static_cast<int>(item.choices.size());
    }
    else if(item.flag)
    {
        cfg.*(item.flag) = !(cfg.*(item.flag));
    }
}

bool toggle_section_items(Config& cfg, const Section& section)
{
    bool hasToggle = false;
    bool anyDisabled = false;
    for(const auto& item : section.items)
    {
        if(item.kind != ItemKind::Toggle || !item.flag)
            continue;
        if(item.flag == &Config::staticLink && !kStaticLinkAvailable)
            continue;
        hasToggle = true;
        if(!(cfg.*(item.flag)))
            anyDisabled = true;
    }

    if(!hasToggle)
        return false;

    const bool enabled = anyDisabled;
    for(const auto& item : section.items)
    {
        if(item.kind == ItemKind::Toggle && item.flag)
        {
            if(item.flag == &Config::staticLink && !kStaticLinkAvailable)
                continue;
            cfg.*(item.flag) = enabled;
        }
    }
    return true;
}

void open_selected_section(std::vector<Section>& sections,
                           const VisibleRow& row)
{
    if(row.kind == RowKind::Section)
        sections[row.section].open = true;
    else
        sections[row.section].open = true;
}

void close_selected_section(std::vector<Section>& sections,
                            const VisibleRow& row)
{
    sections[row.section].open = false;
}
} // namespace

int main(int argc, char** argv)
{
    Config cfg;
    cfg.installDir = default_install_dir();
    cfg.jobs = default_jobs();
    cfg.logFile = default_log_file();
    apply_feature_set(cfg);
    CliOptions options;
    auto sections = make_sections();
    std::string startupMessage;

    if(argc > 1)
    {
        bool showHelp = false;
        std::string error;
        if(!parse_cli(argc, argv, cfg, options, showHelp, error))
        {
            std::cerr << "uvim-config: " << error << "\n\n";
            print_help(std::cerr);
            return 2;
        }
        if(showHelp)
        {
            print_help(std::cout);
            return 0;
        }

        std::string message;
        if(!write_cache(cfg, sections, options, message))
        {
            std::cerr << "uvim-config: " << message << "\n";
            return 1;
        }
        std::cout << message << "\n";
        return 0;
    }

    std::error_code configExistsError;
    const fs::path persistentConfigPath = config_path(options);
    const bool hadConfigFile =
        fs::exists(persistentConfigPath, configExistsError);
    if(hadConfigFile)
        try_load_config_file(persistentConfigPath, cfg, options,
                             startupMessage);
    int cursor = 0;
    int scrollOffset = 0;
    std::string message = startupMessage;
    std::optional<Config> savedConfig;
    std::optional<CliOptions> savedOptions;
    if(hadConfigFile && startupMessage.rfind("loaded ", 0) == 0)
    {
        savedConfig = cfg;
        savedOptions = options;
    }

    auto hasUnsavedChanges = [&]() -> bool
    {
        return !savedConfig || !savedOptions || cfg != *savedConfig ||
               options != *savedOptions;
    };

    auto saveCurrentConfig = [&]() -> bool
    {
        if(!write_cache(cfg, sections, options, message))
            return false;
        savedConfig = cfg;
        savedOptions = options;
        return true;
    };

    bool editingText = false;
    std::string textBeforeEdit;
    std::string Config::* editingField = nullptr;
    enum class PendingConfirm
    {
        Save,
        Quit,
    };
    std::optional<PendingConfirm> pendingConfirm;
    TerminalRawMode rawMode;
#ifndef _WIN32
    std::signal(SIGWINCH, handle_resize);
#endif

    while(true)
    {
        const std::vector<VisibleRow> rows = visible_rows(sections);
        cursor = std::clamp(cursor, 0,
                            std::max(0, static_cast<int>(rows.size()) - 1));
        const TerminalSize screen = terminal_size();
        scrollOffset =
            clamp_scroll(scrollOffset, cursor, static_cast<int>(rows.size()),
                         make_layout(sections, screen).listRows);
        std::string drawMessage;
        if(pendingConfirm == PendingConfirm::Save)
            drawMessage = "Save file (Y/n)?";
        else if(pendingConfirm == PendingConfirm::Quit)
            drawMessage = "Save file before quit (Y/n)?";
        else if(editingText)
            drawMessage = "editing value: type text, Enter saves, Esc cancels";
        else
            drawMessage = message;
        draw(cfg, sections, cursor, scrollOffset, screen, drawMessage,
             editingText);

        int key = read_key();
        if(key == kKeyResize)
            continue;
        if(pendingConfirm)
        {
            const bool yes =
                key == '\n' || key == '\r' || key == 'y' || key == 'Y';
            const bool no = key == 'n' || key == 'N' || key == kKeyEsc;
            if(!yes && !no)
                continue;

            const PendingConfirm confirmedAction = *pendingConfirm;
            pendingConfirm.reset();
            if(yes)
            {
                if(saveCurrentConfig() &&
                   confirmedAction == PendingConfirm::Quit)
                    break;
            }
            else if(confirmedAction == PendingConfirm::Save)
                message = "save cancelled";
            else
                break;
            continue;
        }
        if(editingText && editingField)
        {
            if(key == kKeyEsc)
            {
                cfg.*(editingField) = textBeforeEdit;
                editingText = false;
                editingField = nullptr;
                message = "value unchanged";
            }
            else if(key == '\n' || key == '\r')
            {
                editingText = false;
                editingField = nullptr;
                message = "value set";
            }
            else if(key == 127 || key == 8)
            {
                if(!(cfg.*(editingField)).empty())
                    (cfg.*(editingField)).pop_back();
            }
            else if(key == 21)
            {
                (cfg.*(editingField)).clear();
            }
            else if(key >= 32 && key < 127)
            {
                (cfg.*(editingField)).push_back(static_cast<char>(key));
            }
            continue;
        }
        message.clear();
        if(key == 'q' || key == 'Q' || key == kKeyEsc)
        {
            if(hasUnsavedChanges())
                pendingConfirm = PendingConfirm::Quit;
            else
                break;
            continue;
        }
        if(key == 'j' || key == kKeyDown)
            cursor = std::min(cursor + 1, (int)rows.size() - 1);
        else if(key == 'k' || key == kKeyUp)
            cursor = std::max(cursor - 1, 0);
        else if(key == 'l' || key == kKeyRight)
            open_selected_section(sections, rows[static_cast<size_t>(cursor)]);
        else if(key == 'h' || key == kKeyLeft)
            close_selected_section(sections, rows[static_cast<size_t>(cursor)]);
        else if(key == ' ' || key == '\n' || key == '\r')
        {
            const VisibleRow& row = rows[static_cast<size_t>(cursor)];
            if(row.kind == RowKind::Section)
            {
                if(toggle_section_items(cfg, sections[row.section]))
                    message = "section toggled";
                else
                    sections[row.section].open = !sections[row.section].open;
            }
            else
            {
                const Item& item = sections[row.section].items[row.item];
                if(item.kind == ItemKind::Text && item.text)
                {
                    editingText = true;
                    editingField = item.text;
                    textBeforeEdit = cfg.*(item.text);
                }
                else
                    activate(cfg, item);
            }
        }
        else if(key == 's' || key == 'S')
            pendingConfirm = PendingConfirm::Save;
    }

    write_stdout("\x1b[?25h\x1b[0m\n");
    return 0;
}
