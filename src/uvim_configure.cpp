#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#else
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
constexpr int kKeyEsc = 27;
constexpr int kKeyUp = 1001;
constexpr int kKeyDown = 1002;
constexpr int kKeyResize = 1003;

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
    Toggle,
};

enum class RowKind
{
    Section,
    Item,
};

struct Config
{
    int featureSet = 3;
    int buildType = 0;
    int platform = 0;
    int optimization = 2;
    bool clangdLsp = false;
    bool asmDocs = true;
    bool gitTools = true;
    bool searchTools = true;
    bool formatters = true;
    bool systemClipboard = true;
    bool structSizePopup = true;
    bool tests = true;
    bool compileCommands = true;
    bool lto = true;
    bool gcSections = true;
    bool stripBinary = false;
    bool autoIncrementBuild = true;
    bool sanitizers = false;
    bool debugLogging = false;
    bool debugLsp = false;
};

struct Item
{
    ItemKind kind;
    std::string label;
    std::string cmakeName;
    bool Config::* flag = nullptr;
    int Config::* choice = nullptr;
    std::vector<std::string> choices;
    std::string help;
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

class TerminalRawMode
{
public:
    TerminalRawMode()
    {
#ifndef _WIN32
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
#ifndef _WIN32
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
#ifdef _WIN32
    int c = _getch();
    if(c == 0 || c == 224)
    {
        int next = _getch();
        if(next == 72)
            return kKeyUp;
        if(next == 80)
            return kKeyDown;
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
        if(::read(STDIN_FILENO, &seq[0], 1) == 1 &&
           ::read(STDIN_FILENO, &seq[1], 1) == 1 && seq[0] == '[')
        {
            if(seq[1] == 'A')
                return kKeyUp;
            if(seq[1] == 'B')
                return kKeyDown;
        }
        return kKeyEsc;
    }
    return c;
#endif
}

TerminalSize terminal_size()
{
#ifdef _WIN32
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

    if(cfg.featureSet == 0)
    {
        cfg.optimization = 5;
        cfg.clangdLsp = false;
        cfg.asmDocs = false;
        cfg.gitTools = false;
        cfg.searchTools = false;
        cfg.formatters = false;
        cfg.systemClipboard = false;
        cfg.structSizePopup = false;
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
        cfg.optimization = 5;
        cfg.clangdLsp = false;
        cfg.asmDocs = false;
        cfg.gitTools = true;
        cfg.searchTools = true;
        cfg.formatters = true;
        cfg.systemClipboard = true;
        cfg.structSizePopup = false;
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
        cfg.optimization = 5;
        cfg.clangdLsp = false;
        cfg.asmDocs = false;
        cfg.gitTools = true;
        cfg.searchTools = true;
        cfg.formatters = true;
        cfg.systemClipboard = true;
        cfg.structSizePopup = false;
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
        cfg.optimization = 2;
        cfg.clangdLsp = false;
        cfg.asmDocs = true;
        cfg.gitTools = true;
        cfg.searchTools = true;
        cfg.formatters = true;
        cfg.systemClipboard = true;
        cfg.structSizePopup = true;
        cfg.tests = true;
        cfg.compileCommands = true;
        cfg.lto = true;
        cfg.gcSections = true;
        cfg.stripBinary = false;
        cfg.autoIncrementBuild = true;
        return;
    }

    cfg.optimization = 2;
    cfg.clangdLsp = true;
    cfg.asmDocs = true;
    cfg.gitTools = true;
    cfg.searchTools = true;
    cfg.formatters = true;
    cfg.systemClipboard = true;
    cfg.structSizePopup = true;
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
         "High-level build profiles. vi-real is the strictest vi-style build "
         "with optional git/search tooling compiled out. vi-min keeps more "
         "editor conveniences while staying size-oriented.",
         true,
         {{ItemKind::FeatureSet,
           "Feature set",
           "",
           nullptr,
           nullptr,
           {},
           "vi-real compiles out git, fuzzy, grep, and regex tools. vi-min is "
           "small but keeps normal file/search tooling. Minimal keeps the "
           "normal editor tools but removes docs/tests/LSP. Basic is the "
           "default developer build. Full also enables LSP."}}},
        {"Target",
         "Platform and compiler mode. Release defaults to -O2 unless another "
         "optimization level is selected.",
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
           "minimum binary size on Clang-style toolchains."}}},
        {"Editor Features",
         "User-facing features that can be disabled for very small builds. "
         "Some switches are currently compile definitions for the next source "
         "split pass.",
         true,
         {{ItemKind::Toggle,
           "clangd/LSP support",
           "UVIM_ENABLE_CLANGD_LSP",
           &Config::clangdLsp,
           nullptr,
           {},
           "Compiles in the LSP client and language-server integrations."},
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
           "Build switch for clang-based variable and struct size popups."}}},
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
           "Runs the platform strip tool after linking uvim."}}},
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
           "Enables debug logging to /tmp/uvim_debug.txt."},
          {ItemKind::Toggle,
           "LSP debug logging",
           "UVIM_DEBUG_LSP",
           &Config::debugLsp,
           nullptr,
           {},
           "Enables verbose LSP logging."}}},
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
    return cfg.*(item.flag) ? "ON" : "OFF";
}

std::string help_for(const std::vector<Section>& sections,
                     const VisibleRow& row)
{
    if(row.kind == RowKind::Section)
        return sections[row.section].help;
    return sections[row.section].items[row.item].help;
}

std::string truncate_line(std::string line, int cols)
{
    if(cols <= 0)
        return {};
    if(static_cast<int>(line.size()) > cols)
        line.resize(static_cast<size_t>(cols));
    return line;
}

std::string row_text(const Config& cfg, const std::vector<Section>& sections,
                     const VisibleRow& row, bool selected)
{
    std::string out = selected ? "> " : "  ";

    if(row.kind == RowKind::Section)
    {
        const Section& section = sections[row.section];
        out += section.open ? "[-] " : "[+] ";
        out += section.label;
        return out;
    }

    const Item& item = sections[row.section].items[row.item];
    if(item.kind == ItemKind::Toggle)
    {
        out += "  [";
        out += (cfg.*(item.flag)) ? 'x' : ' ';
        out += "] ";
    }
    else
        out += "      ";
    out += item.label;
    const int pad = std::max(1, 31 - static_cast<int>(item.label.size()));
    out += std::string(static_cast<size_t>(pad), ' ');
    out += value_for(cfg, item);
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
                  "s save  q quit",
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

void append_documentation(std::vector<std::string>& out,
                          const std::vector<Section>& sections,
                          const VisibleRow& row, int docHeight, int cols)
{
    out.push_back("\x1b[1mDocumentation\x1b[0m");
    append_wrapped_fixed(out, help_for(sections, row), docHeight, cols);
}

void draw(const Config& cfg, const std::vector<Section>& sections, int cursor,
          int scrollOffset, TerminalSize screen, std::string_view message)
{
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
                         "j/k/arrows move  h close  l open  "
                         "space/enter change  s save  q quit",
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
        std::string line =
            truncate_line(row_text(cfg, sections, rows[static_cast<size_t>(i)],
                                   i == safeCursor),
                          screen.cols);
        if(i == safeCursor)
            line = "\x1b[7m" + line + "\x1b[0m";
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
        append_documentation(screenLines, sections,
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

    std::cout << "\x1b[?25l\x1b[H\x1b[2J";
    for(size_t i = 0; i < screenLines.size(); ++i)
    {
        if(i > 0)
            std::cout << "\n";
        std::cout << screenLines[i];
    }
    std::cout.flush();
}

fs::path source_dir()
{
    return fs::absolute(fs::path(UVIM_SOURCE_DIR));
}

fs::path output_path()
{
    return source_dir() / "build" / "uvim_config_cache.cmake";
}

bool write_cache(const Config& cfg, const std::vector<Section>& sections,
                 std::string& message)
{
    fs::path out = output_path();
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

    file << "# Generated by uvim-config. Pass with: cmake -C " << out.string()
         << " -S " << source_dir().string() << " -B "
         << out.parent_path().string() << "\n";
    for(const auto& section : sections)
    {
        for(const auto& item : section.items)
        {
            if(item.kind == ItemKind::Choice)
            {
                file << "set(" << item.cmakeName << " \""
                     << item.choices[static_cast<size_t>(cfg.*(item.choice))]
                     << "\" CACHE STRING \"\" FORCE)\n";
            }
            else if(item.kind == ItemKind::Toggle)
            {
                file << "set(" << item.cmakeName << " "
                     << ((cfg.*(item.flag)) ? "ON" : "OFF")
                     << " CACHE BOOL \"\" FORCE)\n";
            }
        }
    }

    message = "saved. Run: cmake -C build/uvim_config_cache.cmake -S . -B "
              "build && cmake --build build";
    return true;
}

void activate(Config& cfg, const Item& item)
{
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

int main()
{
    Config cfg;
    auto sections = make_sections();
    int cursor = 0;
    int scrollOffset = 0;
    std::string message;
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
        draw(cfg, sections, cursor, scrollOffset, screen, message);

        int key = read_key();
        if(key == kKeyResize)
            continue;
        message.clear();
        if(key == 'q' || key == 'Q' || key == kKeyEsc)
            break;
        if(key == 'j' || key == kKeyDown)
            cursor = std::min(cursor + 1, (int)rows.size() - 1);
        else if(key == 'k' || key == kKeyUp)
            cursor = std::max(cursor - 1, 0);
        else if(key == 'l')
            open_selected_section(sections, rows[static_cast<size_t>(cursor)]);
        else if(key == 'h')
            close_selected_section(sections, rows[static_cast<size_t>(cursor)]);
        else if(key == ' ' || key == '\n' || key == '\r')
        {
            const VisibleRow& row = rows[static_cast<size_t>(cursor)];
            if(row.kind == RowKind::Section)
                sections[row.section].open = !sections[row.section].open;
            else
                activate(cfg, sections[row.section].items[row.item]);
        }
        else if(key == 's' || key == 'S')
            write_cache(cfg, sections, message);
    }

    std::cout << "\x1b[?25h\x1b[0m\n";
    return 0;
}
