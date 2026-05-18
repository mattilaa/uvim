#include "editor.h"
#include "ascii.h"
#include "constants.h"
#include "cpp_navigation_utilities.h"
#include "editor_buffer_controller.h"
#include "editor_command_controller.h"
#include "editor_cursor_controller.h"
#include "editor_drawing_controller.h"
#include "editor_editing_controller.h"
#include "editor_file_controller.h"
#include "editor_file_type_controller.h"
#include "editor_git_controller.h"
#include "editor_indent_controller.h"
#include "editor_lsp_controller.h"
#include "editor_mode_controller.h"
#include "editor_operator_controller.h"
#include "editor_path_utilities.h"
#include "editor_references_controller.h"
#include "editor_settings_controller.h"
#include "editor_split_controller.h"
#include "editor_utils.h"
#include "editor_visual_controller.h"
#include "enablelog.h"
#include "formatter.h"
#include "gitignore.h"
#include "mlang_utilities.h"
#include "mode_state_machine.h"
#include "robot_utilities.h"
#include "stdlib_goto.h"
#include "symbol_popup_utilities.h"
#include "syntax_highlighter.h"
#include "terminal.h"
#include "text_utils.h"
#include "widgets/command_history_popup.h"
#include "widgets/command_popup.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif
#include "os_compat.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef UVIM_ENABLE_CLANGD_LSP
static int utf16ToUtf8ByteOffset(const std::string& line, int utf16Offset);
#endif

namespace fs = std::filesystem;

#ifdef UVIM_ENABLE_CLANGD_LSP
template <FileType... Types, typename EnableFn>
void ensure_lsp_for_file_type(Editor& editor, bool enabled,
                              const std::string& path,
                              const std::vector<std::string>& args,
                              EnableFn&& enableFn)
{
    if(enabled || !((editor.isFileType<Types>()) || ...))
        return;

    std::string resolved = EditorPathUtilities::resolveExecutablePath(path);
    if(resolved.empty())
        return;

    enableFn(true, resolved, args);
}
#endif

using editor::helper::ascii_lower;
using editor::helper::collect_js_ts_imports;
using editor::helper::collectLocFiles;
using editor::helper::css_import_path_under_cursor;
using editor::helper::expand_tsconfig_paths;
using editor::helper::expandTildePath;
using editor::helper::extract_html_stylesheets;
using editor::helper::extract_js_ts_module_specifier;
using editor::helper::find_css_selector_in_file;
using editor::helper::find_js_ts_def_in_file;
using editor::helper::find_python_def_in_file;
using editor::helper::find_robot_keyword_in_file;
using editor::helper::find_ts_member_in_type;
using editor::helper::find_ts_type_definition;
using editor::helper::find_ts_type_for_identifier;
using editor::helper::find_word_pos;
using editor::helper::fuzzyScore;
using editor::helper::getLocPathCompletions;
using editor::helper::getPathCompletions;
using editor::helper::getRecursivePathCompletions;
using editor::helper::hash_lines;
using editor::helper::html_path_under_cursor;
using editor::helper::infer_ts_type_from_array_method_line;
using editor::helper::is_skip_dir;
using editor::helper::js_ts_decl_line;
using editor::helper::load_json_file;
using editor::helper::load_tsconfig_paths;
using editor::helper::locCommentRulesForPath;
using editor::helper::locCountInFile;
using editor::helper::locCountInLines;
using editor::helper::locIsTextFile;
using editor::helper::longestCommonPrefix;
using editor::helper::parse_int;
using editor::helper::parse_token_type;
using editor::helper::parse_ts_type_name;
using editor::helper::parseYamlMap;
using editor::helper::python_def_line;
using editor::helper::resolve_js_ts_from_dir;
using editor::helper::resolve_js_ts_module;
using editor::helper::resolve_js_ts_module_path;
using editor::helper::resolve_node_module;
using editor::helper::robot_first_cell;
using editor::helper::robot_is_keyword_def;
using editor::helper::robot_keyword_section;
using editor::helper::robot_section_header;
using editor::helper::split_csv;
using editor::helper::token_type_name;
using editor::helper::trim_ascii_ws;
using editor::helper::trim_view;
using editor::helper::TsConfigPaths;

#if defined(UVIM_TERMINAL_POSIX)
static volatile sig_atomic_t g_pending_resize = 0;

static void handle_sigwinch(int)
{
    g_pending_resize = 1;
}
#endif

Editor::Editor(bool skipInitialBuffer, const std::string& configPath,
               const std::string& themePath)
{
    Terminal::enableRawMode();
    Terminal::getWindowSize(screenRows, screenCols);
    screenRows -= 2; // Status bar and message bar
    theme = Theme::defaults();
    this->configPath = configPath;
    robotKeywordSet = RobotUtilities::defaultKeywords();
    robotCustomKeywordSet = RobotUtilities::defaultCustomKeywords();
    robotSettingSet = RobotUtilities::defaultSettings();
    mlangTokenCache = std::make_shared<MlangTokenCache>();
    commandPrompt = std::make_shared<CommandPrompt>();
    if(!configPath.empty())
    {
        std::ifstream in(configPath);
        if(in.is_open())
        {
            std::ostringstream buf;
            buf << in.rdbuf();
            auto values = parseYamlMap(buf.str());
            std::string resolvedThemePath = themePath;
            if(resolvedThemePath.empty())
            {
                auto itThemeFile = values.find("theme.file");
                if(itThemeFile == values.end())
                    itThemeFile = values.find("theme.path");
                if(itThemeFile != values.end())
                    resolvedThemePath = itThemeFile->second;
                auto itThemeName = values.find("theme.name");
                if(resolvedThemePath.empty() && itThemeName != values.end())
                {
                    std::string name = itThemeName->second;
                    if(!name.empty())
                    {
                        bool hasExt = name.size() >= 5 &&
                                      (name.rfind(".yaml") == name.size() - 5 ||
                                       name.rfind(".yml") == name.size() - 4);
                        if(!hasExt)
                            name += ".yaml";
                        std::string dir =
                            EditorPathUtilities::defaultThemeDir();
                        if(!dir.empty())
                            resolvedThemePath = dir + "/" + name;
                    }
                }
            }
            if(!resolvedThemePath.empty())
            {
                if(!theme.loadFromFile(resolvedThemePath))
                {
                    std::cerr << "theme: failed to load " << resolvedThemePath
                              << "\n";
                }
            }
            theme.loadFromFile(configPath);
            auto it = values.find("editor.tabspaces");
            if(it == values.end())
                it = values.find("settings.tabspaces");
            if(it == values.end())
                it = values.find("tabspaces");
            if(it != values.end())
            {
                try
                {
                    int v = std::stoi(it->second);
                    if(v >= 1 && v <= 16)
                        tabSpaces = v;
                }
                catch(...)
                {
                }
            }
            auto itb = values.find("editor.autobraces");
            if(itb == values.end())
                itb = values.find("settings.autobraces");
            if(itb == values.end())
                itb = values.find("autobraces");
            if(itb != values.end())
            {
                std::string v = itb->second;
                if(v == "true" || v == "1" || v == "on")
                    autoBraces = true;
                else if(v == "false" || v == "0" || v == "off")
                    autoBraces = false;
            }
            auto itq = values.find("editor.autoquotes");
            if(itq == values.end())
                itq = values.find("settings.autoquotes");
            if(itq == values.end())
                itq = values.find("autoquotes");
            if(itq != values.end())
            {
                std::string v = itq->second;
                if(v == "true" || v == "1" || v == "on")
                    autoQuotes = true;
                else if(v == "false" || v == "0" || v == "off")
                    autoQuotes = false;
            }
            auto itbs = values.find("editor.autobracesinstrings");
            if(itbs == values.end())
                itbs = values.find("settings.autobracesinstrings");
            if(itbs == values.end())
                itbs = values.find("autobracesinstrings");
            if(itbs != values.end())
            {
                std::string v = itbs->second;
                if(v == "true" || v == "1" || v == "on")
                    autoBracesInStrings = true;
                else if(v == "false" || v == "0" || v == "off")
                    autoBracesInStrings = false;
            }
            auto ittg = values.find("editor.autotags");
            if(ittg == values.end())
                ittg = values.find("settings.autotags");
            if(ittg == values.end())
                ittg = values.find("autotags");
            if(ittg != values.end())
            {
                std::string v = ittg->second;
                if(v == "true" || v == "1" || v == "on")
                    autoTags = true;
                else if(v == "false" || v == "0" || v == "off")
                    autoTags = false;
            }
            auto itc = values.find("editor.autocomplete");
            if(itc == values.end())
                itc = values.find("settings.autocomplete");
            if(itc == values.end())
                itc = values.find("autocomplete");
            if(itc != values.end())
            {
                std::string v = itc->second;
                if(v == "true" || v == "1" || v == "on")
                    autoCompletion = true;
                else if(v == "false" || v == "0" || v == "off")
                    autoCompletion = false;
            }
            auto itcap = values.find("editor.completionautoparens");
            if(itcap == values.end())
                itcap = values.find("settings.completionautoparens");
            if(itcap == values.end())
                itcap = values.find("completionautoparens");
            if(itcap != values.end())
            {
                std::string v = itcap->second;
                if(v == "true" || v == "1" || v == "on")
                    completionAutoParens = true;
                else if(v == "false" || v == "0" || v == "off")
                    completionAutoParens = false;
            }
            auto itsc = values.find("editor.usesystemclipboard");
            if(itsc == values.end())
                itsc = values.find("settings.usesystemclipboard");
            if(itsc == values.end())
                itsc = values.find("usesystemclipboard");
            if(itsc != values.end())
            {
                std::string v = itsc->second;
                if(v == "true" || v == "1" || v == "on")
                    useSystemClipboard = true;
                else if(v == "false" || v == "0" || v == "off")
                    useSystemClipboard = false;
            }
            auto itutf = values.find("editor.utf8");
            if(itutf == values.end())
                itutf = values.find("settings.utf8");
            if(itutf == values.end())
                itutf = values.find("utf8");
            if(itutf != values.end())
            {
                std::string v = itutf->second;
                if(v == "true" || v == "1" || v == "on")
                    utf8Mode = true;
                else if(v == "false" || v == "0" || v == "off")
                    utf8Mode = false;
            }
            auto itt = values.find("editor.showtabs");
            if(itt == values.end())
                itt = values.find("settings.showtabs");
            if(itt == values.end())
                itt = values.find("showtabs");
            if(itt != values.end())
            {
                std::string v = itt->second;
                if(v == "true" || v == "1" || v == "on")
                    showTabs = true;
                else if(v == "false" || v == "0" || v == "off")
                    showTabs = false;
            }
            auto ittn = values.find("editor.tabnumbers");
            if(ittn == values.end())
                ittn = values.find("settings.tabnumbers");
            if(ittn == values.end())
                ittn = values.find("tabnumbers");
            if(ittn != values.end())
            {
                std::string v = ittn->second;
                if(v == "true" || v == "1" || v == "on")
                    showTabNumbers = true;
                else if(v == "false" || v == "0" || v == "off")
                    showTabNumbers = false;
            }
            auto itrn = values.find("editor.relativenumber");
            if(itrn == values.end())
                itrn = values.find("settings.relativenumber");
            if(itrn == values.end())
                itrn = values.find("relativenumber");
            if(itrn != values.end())
            {
                std::string v = itrn->second;
                if(v == "true" || v == "1" || v == "on")
                    showRelativeLineNumbers = true;
                else if(v == "false" || v == "0" || v == "off")
                    showRelativeLineNumbers = false;
            }
            auto itgc = values.find("editor.gitdefaultcolors");
            if(itgc == values.end())
                itgc = values.find("settings.gitdefaultcolors");
            if(itgc == values.end())
                itgc = values.find("gitdefaultcolors");
            if(itgc != values.end())
            {
                std::string v = itgc->second;
                if(v == "true" || v == "1" || v == "on")
                    gitUseDefaultColors = true;
                else if(v == "false" || v == "0" || v == "off")
                    gitUseDefaultColors = false;
            }
            auto itctp = values.find("editor.commenttogglepartial");
            if(itctp == values.end())
                itctp = values.find("settings.commenttogglepartial");
            if(itctp == values.end())
                itctp = values.find("commenttogglepartial");
            if(itctp != values.end())
            {
                std::string v = itctp->second;
                if(v == "true" || v == "1" || v == "on")
                    commentTogglePartial = true;
                else if(v == "false" || v == "0" || v == "off")
                    commentTogglePartial = false;
            }
            auto itfol = values.find("editor.formatoninsertleave");
            if(itfol == values.end())
                itfol = values.find("settings.formatoninsertleave");
            if(itfol == values.end())
                itfol = values.find("formatoninsertleave");
            if(itfol != values.end())
            {
                std::string v = itfol->second;
                if(v == "true" || v == "1" || v == "on")
                    formatOnInsertLeave = true;
                else if(v == "false" || v == "0" || v == "off")
                    formatOnInsertLeave = false;
            }
            auto itfbf = values.find("editor.filebrowser.fuzzy");
            if(itfbf == values.end())
                itfbf = values.find("settings.filebrowser.fuzzy");
            if(itfbf == values.end())
                itfbf = values.find("filebrowser.fuzzy");
            if(itfbf != values.end())
            {
                std::string v = itfbf->second;
                if(v == "true" || v == "1" || v == "on")
                    fileBrowserFuzzy = true;
                else if(v == "false" || v == "0" || v == "off")
                    fileBrowserFuzzy = false;
            }
            auto itlspg = values.find("editor.status.lspgap");
            if(itlspg == values.end())
                itlspg = values.find("settings.status.lspgap");
            if(itlspg == values.end())
                itlspg = values.find("status.lspgap");
            if(itlspg != values.end())
            {
                std::string v = itlspg->second;
                try
                {
                    int gap = std::stoi(v);
                    if(gap >= 0 && gap <= 20)
                        lspStatusGap = gap;
                }
                catch(...)
                {
                }
            }
            auto itcmp = values.find("editor.commandline.messageprefix");
            if(itcmp == values.end())
                itcmp = values.find("settings.commandline.messageprefix");
            if(itcmp == values.end())
                itcmp = values.find("commandline.messageprefix");
            if(itcmp != values.end())
            {
                std::string v = itcmp->second;
                if(v == "true" || v == "1" || v == "on")
                    commandLineMessagePrefix = true;
                else if(v == "false" || v == "0" || v == "off")
                    commandLineMessagePrefix = false;
            }
            auto itadl = values.find("editor.autodetectlsps");
            if(itadl == values.end())
                itadl = values.find("settings.autodetectlsps");
            if(itadl == values.end())
                itadl = values.find("autodetectlsps");
            if(itadl != values.end())
            {
                std::string v = itadl->second;
                if(v == "true" || v == "1" || v == "on")
                    autoDetectLsps = true;
                else if(v == "false" || v == "0" || v == "off")
                    autoDetectLsps = false;
            }
            auto itfs = values.find("editor.formatonsave");
            if(itfs == values.end())
                itfs = values.find("settings.formatonsave");
            if(itfs == values.end())
                itfs = values.find("formatonsave");
            if(itfs != values.end())
            {
                std::string v = itfs->second;
                if(v == "true" || v == "1" || v == "on")
                    formatOnSave = true;
                else if(v == "false" || v == "0" || v == "off")
                    formatOnSave = false;
            }
            auto itfmt = values.find("editor.formatondoubleesctimeoutms");
            if(itfmt == values.end())
                itfmt = values.find("settings.formatondoubleesctimeoutms");
            if(itfmt == values.end())
                itfmt = values.find("formatondoubleesctimeoutms");
            if(itfmt != values.end())
            {
                std::string v = itfmt->second;
                try
                {
                    int ms = std::stoi(v);
                    if(ms > 0 && ms <= 5000)
                        formatOnDoubleEscTimeoutMs = ms;
                }
                catch(...)
                {
                }
            }
            auto itgdc = values.find("editor.gdcenter");
            if(itgdc == values.end())
                itgdc = values.find("settings.gdcenter");
            if(itgdc == values.end())
                itgdc = values.find("gdcenter");
            if(itgdc != values.end())
            {
                std::string v = itgdc->second;
                if(v == "true" || v == "1" || v == "on")
                    gdCenterScreen = true;
                else if(v == "false" || v == "0" || v == "off")
                    gdCenterScreen = false;
            }
            auto itj = values.find("editor.syntax.json");
            if(itj == values.end())
                itj = values.find("syntax.json");
            if(itj != values.end())
            {
                std::string v = itj->second;
                syntaxJson = !(v == "false" || v == "0" || v == "off");
            }
            auto ity = values.find("editor.syntax.yaml");
            if(ity == values.end())
                ity = values.find("syntax.yaml");
            if(ity != values.end())
            {
                std::string v = ity->second;
                syntaxYaml = !(v == "false" || v == "0" || v == "off");
            }
            auto itrk = values.find("editor.syntax.robot.keywords");
            if(itrk == values.end())
                itrk = values.find("syntax.robot.keywords");
            if(itrk != values.end())
            {
                std::string v = itrk->second;
                std::string lower = ascii_lower(v);
                if(lower == "false" || lower == "0" || lower == "off" ||
                   lower == "none")
                {
                    syntaxRobotKeywords = false;
                    robotKeywordSet.clear();
                }
                else
                {
                    auto list = split_csv(v);
                    if(!list.empty())
                    {
                        syntaxRobotKeywords = true;
                        robotKeywordSet.clear();
                        for(const auto& item : list)
                            robotKeywordSet.insert(ascii_lower(item));
                    }
                }
            }
            auto itrkt = values.find("editor.syntax.robot.highlight_titles");
            if(itrkt == values.end())
                itrkt = values.find("syntax.robot.highlight_titles");
            if(itrkt != values.end())
            {
                std::string v = ascii_lower(itrkt->second);
                syntaxRobotHighlightTitles =
                    !(v == "false" || v == "0" || v == "off");
            }
            auto itrkc = values.find("editor.syntax.robot.highlight_calls");
            if(itrkc == values.end())
                itrkc = values.find("syntax.robot.highlight_calls");
            if(itrkc != values.end())
            {
                std::string v = ascii_lower(itrkc->second);
                syntaxRobotHighlightCalls =
                    !(v == "false" || v == "0" || v == "off");
            }
            auto itcm = values.find("editor.syntax.cpp.highlight_members");
            if(itcm == values.end())
                itcm = values.find("syntax.cpp.highlight_members");
            if(itcm != values.end())
            {
                std::string v = ascii_lower(itcm->second);
                syntaxCppHighlightMembers =
                    !(v == "false" || v == "0" || v == "off");
            }
            auto itct = values.find("editor.syntax.cpp.highlight_type_names");
            if(itct == values.end())
                itct = values.find("syntax.cpp.highlight_type_names");
            if(itct != values.end())
            {
                std::string v = ascii_lower(itct->second);
                syntaxCppHighlightTypeNames =
                    !(v == "false" || v == "0" || v == "off");
            }
            auto itci =
                values.find("editor.syntax.cpp.highlight_implicit_members");
            if(itci == values.end())
                itci = values.find("syntax.cpp.highlight_implicit_members");
            if(itci != values.end())
            {
                std::string v = ascii_lower(itci->second);
                syntaxCppHighlightImplicitMembers =
                    !(v == "false" || v == "0" || v == "off");
            }
            auto itcp = values.find("editor.syntax.cpp.highlight_param_types");
            if(itcp == values.end())
                itcp = values.find("syntax.cpp.highlight_param_types");
            if(itcp != values.end())
            {
                std::string v = ascii_lower(itcp->second);
                syntaxCppHighlightParamTypes =
                    !(v == "false" || v == "0" || v == "off");
            }
            auto itcs =
                values.find("editor.syntax.cpp.highlight_system_includes");
            if(itcs == values.end())
                itcs = values.find("syntax.cpp.highlight_system_includes");
            if(itcs != values.end())
            {
                std::string v = ascii_lower(itcs->second);
                syntaxCppHighlightSystemIncludes =
                    !(v == "false" || v == "0" || v == "off");
            }
            auto itcst = values.find("editor.syntax.cpp.semantic_tokens");
            if(itcst == values.end())
                itcst = values.find("syntax.cpp.semantic_tokens");
            if(itcst != values.end())
            {
                std::string v = ascii_lower(itcst->second);
                syntaxCppSemanticTokens =
                    !(v == "false" || v == "0" || v == "off");
            }
            auto itmt = values.find("editor.syntax.mlang.highlight_types");
            if(itmt == values.end())
                itmt = values.find("syntax.mlang.highlight_types");
            if(itmt != values.end())
            {
                std::string v = ascii_lower(itmt->second);
                syntaxMlangHighlightTypes =
                    !(v == "false" || v == "0" || v == "off");
            }
            auto itmd =
                values.find("editor.syntax.mlang.highlight_builtin_docs");
            if(itmd == values.end())
                itmd = values.find("syntax.mlang.highlight_builtin_docs");
            if(itmd != values.end())
            {
                std::string v = ascii_lower(itmd->second);
                syntaxMlangHighlightBuiltinDocs =
                    !(v == "false" || v == "0" || v == "off");
            }
            auto itrc = values.find("editor.syntax.robot.custom_keywords");
            if(itrc == values.end())
                itrc = values.find("syntax.robot.custom_keywords");
            if(itrc != values.end())
            {
                auto list = split_csv(itrc->second);
                robotCustomKeywordSet.clear();
                for(const auto& item : list)
                    robotCustomKeywordSet.insert(ascii_lower(item));
            }
            auto itrs = values.find("editor.syntax.robot.settings");
            if(itrs == values.end())
                itrs = values.find("syntax.robot.settings");
            if(itrs != values.end())
            {
                auto list = split_csv(itrs->second);
                if(!list.empty())
                {
                    robotSettingSet.clear();
                    for(const auto& item : list)
                        robotSettingSet.insert(ascii_lower(item));
                }
            }
            auto itpf = values.find("editor.python.formatter");
            if(itpf == values.end())
                itpf = values.find("python.formatter");
            if(itpf != values.end())
            {
                std::string v = ascii_lower(itpf->second);
                if(v == "black" || v == "ruff")
                    pythonFormatter = v;
            }

            auto get = [&](std::string_view key) -> std::optional<std::string>
            {
                auto it = values.find(std::string(key));
                if(it != values.end())
                    return it->second;
                return std::nullopt;
            };

            auto set_token = [&](std::string_view key, TokenType& target)
            {
                auto v = get(key);
                if(v)
                    target = parse_token_type(*v, target);
            };

            set_token("editor.syntax.markup.heading_token", markupHeadingToken);
            set_token("syntax.markup.heading_token", markupHeadingToken);
            set_token("editor.syntax.markup.bold_token", markupBoldToken);
            set_token("syntax.markup.bold_token", markupBoldToken);
            set_token("editor.syntax.markup.italic_token", markupItalicToken);
            set_token("syntax.markup.italic_token", markupItalicToken);
            set_token("editor.syntax.markup.code_token", markupCodeToken);
            set_token("syntax.markup.code_token", markupCodeToken);
            set_token("editor.syntax.markup.blockquote_token",
                      markupBlockquoteToken);
            set_token("syntax.markup.blockquote_token", markupBlockquoteToken);
            set_token("editor.syntax.markup.fence_token", markupFenceToken);
            set_token("syntax.markup.fence_token", markupFenceToken);
            set_token("editor.syntax.markup.link_text_token",
                      markupLinkTextToken);
            set_token("syntax.markup.link_text_token", markupLinkTextToken);
            set_token("editor.syntax.markup.link_url_token",
                      markupLinkUrlToken);
            set_token("syntax.markup.link_url_token", markupLinkUrlToken);
            set_token("editor.syntax.markup.link_title_token",
                      markupLinkTitleToken);
            set_token("syntax.markup.link_title_token", markupLinkTitleToken);
            set_token("editor.syntax.markup.rdoc_topic_token",
                      markupRdocTopicToken);
            set_token("syntax.markup.rdoc_topic_token", markupRdocTopicToken);

            set_token("editor.syntax.cpp.locals_color", syntaxCppLocalToken);
            set_token("syntax.cpp.locals_color", syntaxCppLocalToken);
            set_token("editor.syntax.cpp.member_color", syntaxCppMemberToken);
            set_token("syntax.cpp.member_color", syntaxCppMemberToken);
        }
    }
    if(!themePath.empty())
    {
        if(!theme.loadFromFile(themePath))
            std::cerr << "theme: failed to load " << themePath << "\n";
    }

    // No buffers on start unless files are explicitly opened.
    (void)skipInitialBuffer;

#if defined(UVIM_TERMINAL_POSIX)
    std::signal(SIGWINCH, handle_sigwinch);
#endif

    modeStateMachine =
        std::make_unique<ModeStateMachine>(createModeContext(this));
    settingsController = std::make_unique<EditorSettingsController>(*this);
    bufferController = std::make_unique<EditorBufferController>(*this);
    commandController = std::make_unique<EditorCommandController>(*this);
    drawingController = std::make_unique<EditorDrawingController>(*this);
    cursorController = std::make_unique<EditorCursorController>(*this);
    editingController = std::make_unique<EditorEditingController>(*this);
    fileController = std::make_unique<EditorFileController>(*this);
    fileTypeController = std::make_unique<EditorFileTypeController>(*this);
    gitController = std::make_unique<EditorGitController>(*this);
    indentController = std::make_unique<EditorIndentController>(*this);
    lspController = std::make_unique<EditorLspController>(*this);
    modeController = std::make_unique<EditorModeController>(*this);
    operatorController = std::make_unique<EditorOperatorController>(*this);
    referencesController = std::make_unique<EditorReferencesController>(*this);
    splitController = std::make_unique<EditorSplitController>(*this);
    visualController = std::make_unique<EditorVisualController>(*this);
    syntaxHighlighter = std::make_unique<SyntaxHighlighter>(this);
    formatter = std::make_unique<Formatter>(*this);
}

#ifdef UVIM_TESTING
Editor::Editor(TestTag /* tag */, int rows, int cols)
{
    screenRows = std::max(1, rows - 2);
    screenCols = std::max(1, cols);
    theme = Theme::defaults();
    configPath.clear();
    robotKeywordSet = RobotUtilities::defaultKeywords();
    robotCustomKeywordSet = RobotUtilities::defaultCustomKeywords();
    robotSettingSet = RobotUtilities::defaultSettings();
    mlangTokenCache = std::make_shared<MlangTokenCache>();
    commandPrompt = std::make_shared<CommandPrompt>();
    settingsController = std::make_unique<EditorSettingsController>(*this);
    bufferController = std::make_unique<EditorBufferController>(*this);
    commandController = std::make_unique<EditorCommandController>(*this);
    drawingController = std::make_unique<EditorDrawingController>(*this);
    cursorController = std::make_unique<EditorCursorController>(*this);
    editingController = std::make_unique<EditorEditingController>(*this);
    fileController = std::make_unique<EditorFileController>(*this);
    fileTypeController = std::make_unique<EditorFileTypeController>(*this);
    gitController = std::make_unique<EditorGitController>(*this);
    indentController = std::make_unique<EditorIndentController>(*this);
    lspController = std::make_unique<EditorLspController>(*this);
    modeController = std::make_unique<EditorModeController>(*this);
    operatorController = std::make_unique<EditorOperatorController>(*this);
    referencesController = std::make_unique<EditorReferencesController>(*this);
    splitController = std::make_unique<EditorSplitController>(*this);
    visualController = std::make_unique<EditorVisualController>(*this);
    syntaxHighlighter = std::make_unique<SyntaxHighlighter>(this);
    formatter = std::make_unique<Formatter>(*this);
}

Editor Editor::createForTests(int rows, int cols)
{
    return Editor(TestTag{}, rows, cols);
}

void Editor::setModeStateMachineForTests(std::unique_ptr<ModeStateMachine> sm)
{
    modeStateMachine = std::move(sm);
}

std::string
Editor::testInferTsTypeForIdentifier(const std::vector<std::string>& lines,
                                     std::string_view ident, int startY)
{
    return find_ts_type_for_identifier(lines, ident, startY);
}

std::string Editor::testInferTsTypeFromArrayMethodLine(
    std::string_view line, std::string_view param,
    const std::vector<std::string>& lines, int lineNo)
{
    return infer_ts_type_from_array_method_line(line, param, lines, lineNo);
}

bool Editor::testFindTsTypeDefinition(const std::vector<std::string>& lines,
                                      std::string_view typeName, int& outY,
                                      int& outX)
{
    return find_ts_type_definition(lines, typeName, outY, outX);
}

bool Editor::testFindTsMemberInType(const std::vector<std::string>& lines,
                                    int typeStartY, std::string_view member,
                                    int& outY, int& outX)
{
    return find_ts_member_in_type(lines, typeStartY, member, outY, outX);
}

std::string Editor::testResolveJsTsModule(const std::string& fromFile,
                                          std::string_view module)
{
    return resolve_js_ts_module(fromFile, module);
}

std::string Editor::testResolveMlangModule(const std::string& fromFile,
                                           std::string_view modulePath)
{
    std::string out;
    if(MlangUtilities::resolveModuleFile(modulePath, fromFile, out))
        return out;
    return {};
}

#endif

bool Editor::isFileType(FileType type) const
{
    return fileTypeController && fileTypeController->isFileType(type);
}

std::optional<FileType> Editor::getFormatterFileType() const
{
    if(isFileType<FileType::Cpp>())
    {
        return FileType::Cpp;
    }
    else if(isFileType<FileType::Mla>())
    {
        return FileType::Mla;
    }
    else if(isFileType<FileType::Robot>())
    {
        return FileType::Robot;
    }
    else if(isFileType<FileType::Python>())
    {
        return FileType::Python;
    }
    else if(isFileType<FileType::Json>())
    {
        return FileType::Json;
    }
    else if(isFileType<FileType::Yaml>())
    {
        return FileType::Yaml;
    }
    else if(isFileType<FileType::Toml>())
    {
        return FileType::Toml;
    }
    else if(isFileType<FileType::Css>())
    {
        return FileType::Css;
    }
    else if(isFileType<FileType::JavaScript>())
    {
        return FileType::JavaScript;
    }
    else if(isFileType<FileType::TypeScript>())
    {
        return FileType::TypeScript;
    }
    else if(isFileType<FileType::Xml>())
    {
        return FileType::Xml;
    }

    return {};
}

bool Editor::formatBuffer()
{
    if(auto type = getFormatterFileType(); type.has_value())
    {
        return formatter->format(*type, currentMode);
    }
    return false;
}

std::string Editor::resolveEditorPathString(const std::string& input) const
{
    return EditorPathUtilities::resolveEditorPath(fs::path(input)).string();
}

void Editor::ensureMlangTokensLoaded() const
{
    if(syntaxHighlighter)
        syntaxHighlighter->ensureMlangTokensLoaded();
}

std::optional<TokenType>
Editor::lookupMlangTokenType(std::string_view word) const
{
    if(!syntaxHighlighter)
        return std::nullopt;
    return syntaxHighlighter->lookupMlangTokenType(word);
}

std::string Editor::getColorCode(TokenType type) const
{
    if(!syntaxHighlighter)
        return {};
    return syntaxHighlighter->getColorCode(type);
}

std::vector<Token>
Editor::tokenizeLine(const std::string& line, bool& inBlockComment,
                     bool& inTomlMultiline, char& tomlQuote,
                     bool& inMarkupFence, char& markupFenceChar,
                     bool inCppMethodContext, bool inCppFunctionContext,
                     bool inCppParamListContext) const
{
    if(!syntaxHighlighter)
        return {};
    return syntaxHighlighter->tokenizeLine(
        line, inBlockComment, inTomlMultiline, tomlQuote, inMarkupFence,
        markupFenceChar, inCppMethodContext, inCppFunctionContext,
        inCppParamListContext);
}

void Editor::renderLineWithSyntax(std::string& output, const std::string& line,
                                  int start, int len, int fileRow)
{
    if(syntaxHighlighter)
        syntaxHighlighter->renderLineWithSyntax(output, line, start, len,
                                                fileRow);
}

bool Editor::isRobotKeyword(std::string_view word) const
{
    if(!syntaxRobotKeywords || robotKeywordSet.empty())
        return false;
    return robotKeywordSet.find(ascii_lower(word)) != robotKeywordSet.end();
}

bool Editor::isRobotCustomKeyword(std::string_view word) const
{
    if(robotCustomKeywordSet.empty())
        return false;
    return robotCustomKeywordSet.find(ascii_lower(word)) !=
           robotCustomKeywordSet.end();
}

bool Editor::isRobotSetting(std::string_view cell) const
{
    if(robotSettingSet.empty())
        return false;
    return robotSettingSet.find(ascii_lower(cell)) != robotSettingSet.end();
}

Editor::~Editor()
{
    Terminal::clearScreen();
    Terminal::moveCursor(1, 1);
}

void Editor::enableClangdLsp(bool enable, const std::string& compileCommandsDir,
                             const std::string& clangdPath,
                             const std::string& queryDriverAllowList)
{
    lspController->enableClangdLsp(enable, compileCommandsDir, clangdPath,
                                   queryDriverAllowList);
}

bool Editor::isClangdLspEnabled() const
{
    return lspController->isClangdLspEnabled();
}

void Editor::enableRobotLsp(bool enable, const std::string& robotLspPath,
                            const std::vector<std::string>& robotLspArgs)
{
    lspController->enableRobotLsp(enable, robotLspPath, robotLspArgs);
}

bool Editor::isRobotLspEnabled() const
{
    return lspController->isRobotLspEnabled();
}

void Editor::enablePythonLsp(bool enable, const std::string& pythonLspPath,
                             const std::vector<std::string>& pythonLspArgs)
{
    lspController->enablePythonLsp(enable, pythonLspPath, pythonLspArgs);
}

bool Editor::isPythonLspEnabled() const
{
    return lspController->isPythonLspEnabled();
}

void Editor::enableMlangLsp(bool enable, const std::string& mlangLspPath,
                            const std::vector<std::string>& mlangLspArgs)
{
    lspController->enableMlangLsp(enable, mlangLspPath, mlangLspArgs);
}

bool Editor::isMlangLspEnabled() const
{
    return lspController->isMlangLspEnabled();
}

void Editor::findReferences()
{
    referencesController->findReferences();
}

void Editor::clearReferences()
{
    referencesController->clearReferences();
}

bool Editor::selectReference()
{
    return referencesController->selectReference();
}

void Editor::openReferencePreview()
{
    referencesController->openReferencePreview();
}

void Editor::referencesUp()
{
    referencesController->referencesUp();
}

void Editor::referencesDown()
{
    referencesController->referencesDown();
}

void Editor::referencesHalfPageUp()
{
    referencesController->referencesHalfPageUp();
}

void Editor::referencesHalfPageDown()
{
    referencesController->referencesHalfPageDown();
}

void Editor::referencesFirst()
{
    referencesController->referencesFirst();
}

void Editor::referencesLast()
{
    referencesController->referencesLast();
}

void Editor::toggleReferencesPreview()
{
    referencesController->toggleReferencesPreview();
}

void Editor::drawReferences()
{
    referencesController->drawReferences();
}

bool Editor::hasReferences() const
{
    return referencesController->hasReferences();
}

void Editor::enableHtmlLsp(bool enable, const std::string& htmlLspPath,
                           const std::vector<std::string>& htmlLspArgs)
{
    lspController->enableHtmlLsp(enable, htmlLspPath, htmlLspArgs);
}

bool Editor::isHtmlLspEnabled() const
{
    return lspController->isHtmlLspEnabled();
}

void Editor::enableCssLsp(bool enable, const std::string& cssLspPath,
                          const std::vector<std::string>& cssLspArgs)
{
    lspController->enableCssLsp(enable, cssLspPath, cssLspArgs);
}

bool Editor::isCssLspEnabled() const
{
    return lspController->isCssLspEnabled();
}

void Editor::enableJsonLsp(bool enable, const std::string& jsonLspPath,
                           const std::vector<std::string>& jsonLspArgs)
{
    lspController->enableJsonLsp(enable, jsonLspPath, jsonLspArgs);
}

bool Editor::isJsonLspEnabled() const
{
    return lspController->isJsonLspEnabled();
}

void Editor::enableTsLsp(bool enable, const std::string& tsLspPath,
                         const std::vector<std::string>& tsLspArgs)
{
    lspController->enableTsLsp(enable, tsLspPath, tsLspArgs);
}

bool Editor::isTsLspEnabled() const
{
    return lspController->isTsLspEnabled();
}

void Editor::enterOperatorPending(char op)
{
    operatorController->enterOperatorPending(op);
}

bool Editor::getTextObjectRange(char objChar, bool around, int& outStartY,
                                int& outStartX, int& outEndY, int& outEndX)
{
    return operatorController->getTextObjectRange(objChar, around, outStartY,
                                                  outStartX, outEndY, outEndX);
}

void Editor::applyOperatorToRange(char op, int startY, int startX, int endY,
                                  int endX)
{
    operatorController->applyOperatorToRange(op, startY, startX, endY, endX);
}

// yankRange and deleteRange are now in text_operations.cpp

void Editor::setMode(Mode mode)
{
    ensureBufferForMode(mode);
    currentMode = mode;
    needsFullRedraw = true;

    if(!modeStateMachine)
    {
        if(mode == INSERT)
        {
            Terminal::setCursorBarBlinking();
        }
        else
        {
            Terminal::setCursorBlock();
        }
        return;
    }

    switch(mode)
    {
    case WELCOME:
        modeStateMachine->transitionTo(WelcomeMode{});
        break;
    case NORMAL:
        modeStateMachine->transitionTo(NormalMode{});
        break;
    case INSERT:
        modeStateMachine->transitionTo(InsertMode{});
        break;
    case REPLACE:
        modeStateMachine->transitionTo(ReplaceMode{});
        break;
    case VISUAL:
        modeStateMachine->transitionTo(VisualMode{});
        break;
    case VISUAL_LINE:
        modeStateMachine->transitionTo(VisualLineMode{});
        break;
    case VISUAL_BLOCK:
        modeStateMachine->transitionTo(VisualBlockMode{});
        break;
    case COMMAND:
        modeStateMachine->transitionTo(CommandMode{});
        break;
    case SEARCH_FORWARD:
        modeStateMachine->transitionTo(SearchForwardMode{});
        break;
    case SEARCH_BACKWARD:
        modeStateMachine->transitionTo(SearchBackwardMode{});
        break;
    case FILE_BROWSER:
        modeStateMachine->transitionTo(FileBrowserMode{});
        break;
    case FUZZY_FIND:
        modeStateMachine->transitionTo(FuzzyFindMode{});
        break;
    case BUFFER_BROWSER:
        modeStateMachine->transitionTo(BufferBrowserMode{});
        break;
    case GREP_SEARCH:
        modeStateMachine->transitionTo(GrepSearchMode{});
        break;
    case REGEX_SEARCH:
        modeStateMachine->transitionTo(RegexSearchMode{});
        break;
    case OP_PENDING:
        modeStateMachine->transitionTo(
            OperatorPendingMode{pendingOperator, pendingCount});
        break;
    case REFERENCES:
        modeStateMachine->transitionTo(ReferencesMode{});
        break;
    case LSP_INFO:
        modeStateMachine->transitionTo(LspInfoMode{});
        break;
    case LOC_LIST:
        modeStateMachine->transitionTo(LocListMode{});
        break;
    case HELP:
        modeStateMachine->transitionTo(HelpMode{});
        break;
    case GIT_SHOW:
        modeStateMachine->transitionTo(GitShowCommitMode{});
        break;
    case GIT_LOG:
        modeStateMachine->transitionTo(GitLogMode{});
        break;
    case GIT_STAGE:
        modeStateMachine->transitionTo(GitStageMode{});
        break;
    case GIT_COMMIT:
        modeStateMachine->transitionTo(GitCommitMode{});
        break;
    case GIT_FIXUP:
        modeStateMachine->transitionTo(GitFixupMode{});
        break;
    case GIT_PATCH:
        modeStateMachine->transitionTo(GitPatchMode{});
        break;
    case COMMAND_OUTPUT:
        modeStateMachine->transitionTo(CommandOutputMode{});
        break;
    }

    modeController->syncModeFromStateMachine();
}

std::string Editor::getModeString() const
{
    switch(currentMode)
    {
    case WELCOME:
        return "WELCOME";
    case NORMAL:
        return "NORMAL";
    case INSERT:
        return "INSERT";
    case REPLACE:
        return "REPLACE";
    case VISUAL:
        return "VISUAL";
    case VISUAL_LINE:
        return "VISUAL LINE";
    case VISUAL_BLOCK:
        return "VISUAL BLOCK";
    case COMMAND:
        return "COMMAND";
    case SEARCH_FORWARD:
        return "/";
    case SEARCH_BACKWARD:
        return "?";
    case FILE_BROWSER:
        return "BROWSE";
    case FUZZY_FIND:
        return "FUZZY";
    case BUFFER_BROWSER:
        return "BUFFERS";
    case GREP_SEARCH:
        return "GREP";
    case REGEX_SEARCH:
        return "REGEX";
    case LSP_INFO:
        return "LSP INFO";
    case REFERENCES:
        return "REFERENCES";
    case LOC_LIST:
        return "LOC";
    case OP_PENDING:
        return "OP_PENDING";
    case HELP:
        return "HELP";
    case GIT_SHOW:
        return "GITSHOW";
    case GIT_LOG:
        return "GITLOG";
    case GIT_STAGE:
        return "GIT STAGE";
    case GIT_COMMIT:
        return "GIT COMMIT";
    case GIT_FIXUP:
        return "GIT FIXUP";
    case GIT_PATCH:
        return "GIT PATCH";
    case COMMAND_OUTPUT:
        return "RUN";
    }
    return "";
}

void Editor::openFile(std::string_view fname, bool notifyLspOnOpen)
{
    if(deferredStartupAction)
    {
        auto action = std::move(deferredStartupAction);
        deferredStartupAction = {};
        action(*this);
    }

    locMessage.clear();
    // Normalize path (CRITICAL for buffer matching). Resolve relative paths
    // against the editor's logical working directory instead of changing the
    // process-wide current directory.
    fs::path requestedPath =
        EditorPathUtilities::resolveEditorPath(fs::path(std::string(fname)));
    std::string path = requestedPath.string();
    try
    {
        path = fs::canonical(requestedPath).string();
    }
    catch(...)
    {
        std::error_code ec;
        fs::path absolutePath = fs::absolute(requestedPath, ec);
        if(!ec)
            path = absolutePath.string();
    }

    // Check if file already open
    int existing = findBufferByFilename(path);
    if(existing >= 0)
    {
        switchToBuffer(existing);
        //        setStatusMessage("Buffer " + std::to_string(existing + 1) +
        //        "/" +
        //                         std::to_string(buffers.size()));
        return;
    }

    // Reuse a clean unnamed buffer if it exists, otherwise create a new one.
    bool reused = false;
    for(size_t i = 0; i < buffers.size(); ++i)
    {
        Buffer* buf = buffers[i].get();
        if(buf->filename.empty() && !buf->dirty && buf->lines.size() == 1 &&
           buf->lines[0].empty())
        {
            switchToBuffer((int)i);
            reused = true;
            break;
        }
    }
    if(!reused)
        createNewBuffer();

    *filename = path;
    lines->clear();

    std::ifstream file(path);
    if(file.is_open())
    {
        std::string line;
        while(std::getline(file, line))
        {
            if(!line.empty() && line.back() == '\r')
                line.pop_back();
            lines->push_back(line);
        }
        file.close();
    }

    if(lines->empty())
        lines->push_back("");

    *dirty = false;
    *cursorX = *cursorY = 0;
    *offsetX = *offsetY = 0;
    currentBuffer->lspSyncNeeded = !notifyLspOnOpen;
    currentBuffer->lspHashValid = false;
    currentBuffer->lspDiagnosticsSeenValid = false;
    currentBuffer->lspDiagnosticsSeenRevision = 0;
    currentBuffer->lspSemanticTokensValid = false;
    currentBuffer->lspSemanticTokensRevision = 0;
    currentBuffer->lspSemanticTokensHash = 0;
    currentBuffer->lspSemanticTokens.clear();
    currentBuffer->clangIndentWidthValid = false;
    currentBuffer->clangIndentWidth = -1;
    currentBuffer->clangBraceStyleValid = false;
    currentBuffer->clangBraceNewLine = false;
    currentBuffer->savedContentHash = hash_lines(*lines);
    currentBuffer->savedContentHashValid = true;

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(notifyLspOnOpen)
    {
        ensure_lsp_for_file_type<FileType::Html>(
            *this, isHtmlLspEnabled(), htmlLspPath, htmlLspArgs,
            [&](bool on, const std::string& p,
                const std::vector<std::string>& a)
            { enableHtmlLsp(on, p, a); });
        ensure_lsp_for_file_type<FileType::Css>(
            *this, isCssLspEnabled(), cssLspPath, cssLspArgs,
            [&](bool on, const std::string& p,
                const std::vector<std::string>& a)
            { enableCssLsp(on, p, a); });
        ensure_lsp_for_file_type<FileType::Json>(
            *this, isJsonLspEnabled(), jsonLspPath, jsonLspArgs,
            [&](bool on, const std::string& p,
                const std::vector<std::string>& a)
            { enableJsonLsp(on, p, a); });
        ensure_lsp_for_file_type<FileType::JavaScript, FileType::TypeScript>(
            *this, isTsLspEnabled(), tsLspPath, tsLspArgs,
            [&](bool on, const std::string& p,
                const std::vector<std::string>& a)
            { enableTsLsp(on, p, a); });
    }
#endif

    // Record file modification time for external change detection
    std::error_code ec;
    auto ftime = std::filesystem::last_write_time(path, ec);
    if(!ec)
    {
        currentBuffer->lastModificationTime = ftime;
    }

    // Reset undo state cleanly
    currentBuffer->undoStack.clear();
    currentBuffer->undoIndex = -1;
    saveState();
    currentBuffer->savedUndoIndex = currentBuffer->undoIndex;

    needsFullRedraw = true;

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(notifyLspOnOpen)
    {
        // Notify LSP about the newly opened file so gd works from system
        // headers.
        if(isClangdLspEnabled() && isFileType<FileType::Cpp>() &&
           !isFileType<FileType::Mla>() && lspClient)
        {
            // Build text content from loaded lines
            std::string text;
            text.reserve(lines->size() * 80);
            for(size_t i = 0; i < lines->size(); ++i)
            {
                text += (*lines)[i];
                if(i + 1 < lines->size())
                    text.push_back('\n');
            }
            // didChange will call didOpen if needed
            lspClient->didChange(path, text, "cpp");
            currentBuffer->lspSyncNeeded = false;
            if(syntaxCppSemanticTokens)
                lspClient->requestSemanticTokens(path);
        }
        if(isRobotLspEnabled() && isFileType<FileType::Robot>() &&
           robotLspClient)
        {
            std::string text;
            text.reserve(lines->size() * 80);
            for(size_t i = 0; i < lines->size(); ++i)
            {
                text += (*lines)[i];
                if(i + 1 < lines->size())
                    text.push_back('\n');
            }
            robotLspClient->didChange(path, text, "robotframework");
        }
        if(isPythonLspEnabled() && isFileType<FileType::Python>() &&
           pythonLspClient)
        {
            std::string text;
            text.reserve(lines->size() * 80);
            for(size_t i = 0; i < lines->size(); ++i)
            {
                text += (*lines)[i];
                if(i + 1 < lines->size())
                    text.push_back('\n');
            }
            pythonLspClient->didChange(path, text, "python");
        }
        if(isMlangLspEnabled() && isFileType<FileType::Mla>() && mlangLspClient)
        {
            std::string text;
            text.reserve(lines->size() * 80);
            for(size_t i = 0; i < lines->size(); ++i)
            {
                text += (*lines)[i];
                if(i + 1 < lines->size())
                    text.push_back('\n');
            }
            mlangLspClient->didChange(path, text, "mlang");
            mlangLspClient->requestSemanticTokens(path);
        }
        if(isHtmlLspEnabled() && isFileType<FileType::Html>() && htmlLspClient)
        {
            std::string text;
            text.reserve(lines->size() * 80);
            for(size_t i = 0; i < lines->size(); ++i)
            {
                text += (*lines)[i];
                if(i + 1 < lines->size())
                    text.push_back('\n');
            }
            htmlLspClient->didChange(path, text, "html");
        }
        if(isCssLspEnabled() && isFileType<FileType::Css>() && cssLspClient)
        {
            std::string text;
            text.reserve(lines->size() * 80);
            for(size_t i = 0; i < lines->size(); ++i)
            {
                text += (*lines)[i];
                if(i + 1 < lines->size())
                    text.push_back('\n');
            }
            cssLspClient->didChange(path, text, "css");
        }
        if(isJsonLspEnabled() && isFileType<FileType::Json>() && jsonLspClient)
        {
            std::string text;
            text.reserve(lines->size() * 80);
            for(size_t i = 0; i < lines->size(); ++i)
            {
                text += (*lines)[i];
                if(i + 1 < lines->size())
                    text.push_back('\n');
            }
            jsonLspClient->didChange(path, text, "json");
        }
        if(isTsLspEnabled() &&
           (isFileType<FileType::JavaScript>() ||
            isFileType<FileType::TypeScript>()) &&
           tsLspClient)
        {
            std::string text;
            text.reserve(lines->size() * 80);
            for(size_t i = 0; i < lines->size(); ++i)
            {
                text += (*lines)[i];
                if(i + 1 < lines->size())
                    text.push_back('\n');
            }
            const char* lang = isFileType<FileType::TypeScript>()
                                   ? "typescript"
                                   : "javascript";
            tsLspClient->didChange(path, text, lang);
        }
    }
#endif

    //    setStatusMessage("Buffer " + std::to_string(currentBufferIndex + 1) +
    //    "/" +
    //                     std::to_string(buffers.size()) + " " +
    //                     std::to_string(lines->size()) + " lines");
}

void Editor::openFileBrowser(std::string_view path)
{
    std::string prev;
    if(currentMode != FILE_BROWSER && currentBuffer != nullptr && filename)
    {
        prev = *filename;
    }

    if(modeStateMachine)
    {
        modeStateMachine->transitionTo(
            FileBrowserMode{std::string(path), prev});
        modeController->syncModeFromStateMachine();
    }
    else
    {
        setMode(FILE_BROWSER);
    }
}

bool Editor::formatBufferForSave()
{
#ifdef UVIM_TESTING
    if(formatOnSaveTestHook)
        return formatOnSaveTestHook();
#endif
    return formatBuffer();
}

void Editor::saveFile()
{
    fileController->saveFile();
}

void Editor::checkFileChanges()
{
    fileController->checkFileChanges();
}

void Editor::reloadCurrentFile()
{
    fileController->reloadCurrentFile();
}

// Jump between header and source file
bool Editor::fileExists(const std::string& path)
{
    return fileController->fileExists(path);
}

std::string Editor::getSymbolUnderCursor()
{
    return fileController->getSymbolUnderCursor();
}

std::string Editor::findAlternateFile(const std::string& currentFile)
{
    return fileController->findAlternateFile(currentFile);
}

void Editor::jumpToAlternateFile()
{
    fileController->jumpToAlternateFile();
}

// Movement implementations are delegated to EditorCursorController.

bool Editor::isWordChar(char c) const
{
    if(std::isspace((unsigned char)c))
        return false;
    if(std::isalnum((unsigned char)c) || c == '_')
        return true; // letters/numbers
    // punctuation counts as “word” for w/dw/cw
    return true;
}

// Editing operations

// Text operations (insertChar through pasteBefore) are now in
// text_operations.cpp

void Editor::startVisualMode()
{
    visualController->startVisualMode();
}

void Editor::startVisualLineMode()
{
    visualController->startVisualLineMode();
}

void Editor::startVisualBlockMode()
{
    visualController->startVisualBlockMode();
}

void Editor::updateVisualSelection()
{
    visualController->updateVisualSelection();
}

void Editor::updateVisualBlockSelection()
{
    visualController->updateVisualBlockSelection();
}

bool Editor::isInSelection(int row, int col)
{
    return visualController->isInSelection(row, col);
}

bool Editor::isInVisualBlock(int row, int col)
{
    return visualController->isInVisualBlock(row, col);
}

void Editor::getVisualBlockBounds(int& startY, int& startX, int& endY,
                                  int& endX)
{
    visualController->getVisualBlockBounds(startY, startX, endY, endX);
}

void Editor::getSelectionBounds(int& startY, int& startX, int& endY, int& endX)
{
    visualController->getSelectionBounds(startY, startX, endY, endX);
}

// deleteVisualBlock, yankVisualBlock, changeVisualBlock,
// applyVisualBlockInsert, deleteSelection are now in text_operations.cpp

std::string Editor::toLowerCase(const std::string& str)
{
    return indentController->toLowerCase(str);
}

int Editor::getLineIndent(int line)
{
    return indentController->getLineIndent(line);
}

void Editor::indentLine(int line, int spaces)
{
    indentController->indentLine(line, spaces);
}

void Editor::autoIndentLine(int line)
{
    indentController->autoIndentLine(line);
}

void Editor::autoIndentRange(int startLine, int endLine)
{
    indentController->autoIndentRange(startLine, endLine);
}

void Editor::goToDefinition()
{
    const std::string gdArrow = ascii::utf8(ascii::RIGHT_ARROW_PADDED);

    // First, check if we're on an #include line
    if(*cursorY >= 0 && *cursorY < (int)lines->size())
    {
        const std::string& currentLine = (*lines)[*cursorY];
        auto [includePath, isSystem] =
            CppNavigationUtilities::extractIncludePath(currentLine);

        if(!includePath.empty())
        {
            std::string resolvedPath;

            if(isSystem)
            {
                // System include: search in system paths
                resolvedPath =
                    CppNavigationUtilities::resolveSystemInclude(includePath);
            }
            else
            {
                // Local include: resolve relative to current file's directory
                std::string currentDir = ".";
                if(!filename->empty())
                {
                    size_t lastSlash = filename->rfind('/');
                    if(lastSlash != std::string::npos)
                    {
                        currentDir = filename->substr(0, lastSlash);
                    }
                }

                std::string tryPath = currentDir + "/" + includePath;
                std::error_code ec;
                if(fs::exists(tryPath, ec) && !ec)
                {
                    resolvedPath = tryPath;
                }
            }

            if(!resolvedPath.empty())
            {
                pushJumpLocation();
                openFile(resolvedPath);

                // Show appropriate message
                std::string displayPath = resolvedPath;
                if(isSystem && resolvedPath.length() > 50)
                {
                    // Show shortened path for system headers
                    size_t lastSlash = resolvedPath.rfind('/');
                    if(lastSlash != std::string::npos)
                        displayPath =
                            ".../" + resolvedPath.substr(lastSlash + 1);
                }
                setStatusMessage(std::string("gd") + gdArrow + displayPath);
                return;
            }
            else
            {
                setStatusMessage("gd: include file not found: " + includePath);
                return;
            }
        }
    }

    std::string symbol = getSymbolUnderCursor();
    if(symbol.empty())
    {
        setStatusMessage("gd: no symbol");
        return;
    }

    auto apply_gd_viewport = [&]()
    {
        if(gdCenterScreen)
            centerScreen();
        else
            adjustViewport();
    };

    bool isStdSymbol = false;
    if(*cursorY >= 0 && *cursorY < (int)lines->size())
    {
        const std::string& line = (*lines)[*cursorY];
        int x = *cursorX;
        if(x >= 0 && x < (int)line.size() &&
           CppNavigationUtilities::isIdent(line[x]))
        {
            int l = x;
            while(l > 0 && CppNavigationUtilities::isIdent(line[l - 1]))
                l--;
            if(l >= 5 && line.compare(l - 5, 5, "std::") == 0)
            {
                isStdSymbol = true;
            }
        }
    }

    if((isStdSymbol || symbolPrefix.rfind("std::", 0) == 0) &&
       !isFileType<FileType::Mla>())
    {
        std::string headerName = stdlib_goto::headerForSymbol(symbol);
        if(!headerName.empty())
        {
            std::string header =
                CppNavigationUtilities::resolveSystemInclude(headerName);
            if(!header.empty())
            {
                pushJumpLocation();
                openFile(header);
                setStatusMessage(std::string("gd") + gdArrow + "<sys>/" +
                                 headerName);
                return;
            }
        }
    }

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(isRobotLspEnabled() && isFileType<FileType::Robot>())
    {
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }

        robotLspClient->didChange(currentBuffer->filename, text,
                                  "robotframework");
        auto loc = robotLspClient->definition(currentBuffer->filename, *cursorY,
                                              *cursorX);
        if(loc)
        {
            pushJumpLocation();
            openFile(loc->path);
            *cursorY = loc->line;
            *cursorX = loc->character;

            if(*cursorY >= (int)lines->size())
                *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
            if(*cursorY >= 0 && *cursorX > (int)(*lines)[*cursorY].length())
                *cursorX = (*lines)[*cursorY].length();

            apply_gd_viewport();
            setStatusMessage(std::string("gd (robot)") + gdArrow + loc->path +
                             ":" + std::to_string(loc->line + 1));
            return;
        }

        std::string_view lineView;
        if(*cursorY >= 0 && *cursorY < (int)lines->size())
            lineView = (*lines)[*cursorY];
        std::string_view keyword = robot_first_cell(lineView);
        if(keyword.empty())
        {
            setStatusMessage("gd (robot): no keyword");
            return;
        }

        int defY = -1;
        int defX = 0;
        if(find_robot_keyword_in_file(currentBuffer->filename, keyword, defY,
                                      defX))
        {
            *cursorY = defY;
            *cursorX = defX;
            apply_gd_viewport();
            setStatusMessage(std::string("gd (robot)") + gdArrow + *filename +
                             ":" + std::to_string(defY + 1));
            return;
        }

        std::filesystem::path root = std::filesystem::current_path();
        std::error_code ec;
        for(std::filesystem::recursive_directory_iterator it(
                root,
                std::filesystem::directory_options::skip_permission_denied, ec),
            end;
            it != end; ++it)
        {
            if(it->is_directory(ec) && is_skip_dir(it->path()))
            {
                it.disable_recursion_pending();
                continue;
            }
            if(!it->is_regular_file(ec))
                continue;
            const auto& p = it->path();
            std::string ext = p.extension().string();
            if(ext != ".robot" && ext != ".resource" &&
               ext != ".robotframework")
                continue;
            if(find_robot_keyword_in_file(p.string(), keyword, defY, defX))
            {
                pushJumpLocation();
                openFile(p.string());
                *cursorY = defY;
                *cursorX = defX;
                apply_gd_viewport();
                setStatusMessage(std::string("gd (robot)") + gdArrow +
                                 p.string() + ":" + std::to_string(defY + 1));
                return;
            }
        }

        setStatusMessage("gd (robot): not found");
        return;
    }

    if(isPythonLspEnabled() && isFileType<FileType::Python>())
    {
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }

        pythonLspClient->didChange(currentBuffer->filename, text, "python");
        int lspX = *cursorX;
        if(*cursorY >= 0 && *cursorY < (int)lines->size())
        {
            const std::string& line = (*lines)[*cursorY];
            auto is_sym = [](char ch) -> bool
            {
                unsigned char u = static_cast<unsigned char>(ch);
                return std::isalnum(u) || ch == '_';
            };

            if(!line.empty())
            {
                if(lspX >= (int)line.size())
                    lspX = (int)line.size() - 1;
                if(lspX < 0)
                    lspX = 0;

                if(line[lspX] == '.' && lspX + 1 < (int)line.size() &&
                   is_sym(line[lspX + 1]))
                {
                    lspX = lspX + 1;
                }
                else if(!is_sym(line[lspX]))
                {
                    if(lspX > 0 && is_sym(line[lspX - 1]))
                        lspX = lspX - 1;
                    else if(lspX + 1 < (int)line.size() &&
                            is_sym(line[lspX + 1]))
                        lspX = lspX + 1;
                }
            }
        }

        std::string_view lineForLsp;
        if(*cursorY >= 0 && *cursorY < (int)lines->size())
            lineForLsp = (*lines)[*cursorY];

        auto loc = pythonLspClient->definition(currentBuffer->filename,
                                               *cursorY, lspX, lineForLsp);
        if(!loc)
        {
            // Some Python servers expose stdlib/type-stub targets via
            // declaration when definition is unavailable.
            loc = pythonLspClient->declaration(currentBuffer->filename,
                                               *cursorY, lspX, lineForLsp);
        }
        if(!loc)
        {
            // Additional fallback for servers that only provide type targets.
            loc = pythonLspClient->typeDefinition(currentBuffer->filename,
                                                  *cursorY, lspX, lineForLsp);
        }
        if(loc)
        {
            pushJumpLocation();
            openFile(loc->path);
            *cursorY = loc->line;
            *cursorX = loc->character;

            if(*cursorY >= (int)lines->size())
                *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
            if(*cursorY >= 0 && *cursorX > (int)(*lines)[*cursorY].length())
                *cursorX = (*lines)[*cursorY].length();

            apply_gd_viewport();
            setStatusMessage(std::string("gd (python)") + gdArrow + loc->path +
                             ":" + std::to_string(loc->line + 1));
            return;
        }

        std::string symbol = getSymbolUnderCursor();
        if(symbol.empty())
        {
            setStatusMessage("gd (python): no symbol");
            return;
        }

        int defY = -1;
        int defX = 0;
        if(find_python_def_in_file(currentBuffer->filename, symbol, defY, defX))
        {
            *cursorY = defY;
            *cursorX = defX;
            apply_gd_viewport();
            setStatusMessage(std::string("gd (python)") + gdArrow + *filename +
                             ":" + std::to_string(defY + 1));
            return;
        }

        std::filesystem::path root = std::filesystem::current_path();
        std::error_code ec;
        for(std::filesystem::recursive_directory_iterator it(
                root,
                std::filesystem::directory_options::skip_permission_denied, ec),
            end;
            it != end; ++it)
        {
            if(it->is_directory(ec) && is_skip_dir(it->path()))
            {
                it.disable_recursion_pending();
                continue;
            }
            if(!it->is_regular_file(ec))
                continue;
            const auto& p = it->path();
            if(!constants::is_filetype<constants::no_pattern,
                                       constants::python_suffixes>(p.string()))
                continue;
            if(find_python_def_in_file(p.string(), symbol, defY, defX))
            {
                pushJumpLocation();
                openFile(p.string());
                *cursorY = defY;
                *cursorX = defX;
                apply_gd_viewport();
                setStatusMessage(std::string("gd (python)") + gdArrow +
                                 p.string() + ":" + std::to_string(defY + 1));
                return;
            }
        }

        setStatusMessage("gd (python): not found");
        return;
    }

    auto lsp_gd = [&](LspClient* client, const char* languageId,
                      std::string_view label) -> bool
    {
        if(!client)
            return false;
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }

        client->didChange(currentBuffer->filename, text, languageId);
        auto loc =
            client->definition(currentBuffer->filename, *cursorY, *cursorX);
        if(!loc)
            return false;

        pushJumpLocation();
        openFile(loc->path);
        *cursorY = loc->line;
        *cursorX = loc->character;

        if(*cursorY >= (int)lines->size())
            *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
        if(*cursorY >= 0 && *cursorX > (int)(*lines)[*cursorY].length())
            *cursorX = (*lines)[*cursorY].length();

        apply_gd_viewport();
        setStatusMessage("gd (" + std::string(label) + ")" + gdArrow +
                         loc->path + ":" + std::to_string(loc->line + 1));
        return true;
    };

    if(isHtmlLspEnabled() && isFileType<FileType::Html>())
    {
        if(lsp_gd(htmlLspClient.get(), "html", "html"))
            return;
    }
    if(isCssLspEnabled() && isFileType<FileType::Css>())
    {
        if(lsp_gd(cssLspClient.get(), "css", "css"))
            return;
    }
    if(isJsonLspEnabled() && isFileType<FileType::Json>())
    {
        if(lsp_gd(jsonLspClient.get(), "json", "json"))
            return;
    }
    if(isTsLspEnabled() && (isFileType<FileType::JavaScript>() ||
                            isFileType<FileType::TypeScript>()))
    {
        const char* lang =
            isFileType<FileType::TypeScript>() ? "typescript" : "javascript";
        if(lsp_gd(tsLspClient.get(), lang, "ts"))
            return;
    }

    if(isFileType<FileType::JavaScript>() || isFileType<FileType::TypeScript>())
    {
        std::string_view lineView;
        if(*cursorY >= 0 && *cursorY < (int)lines->size())
            lineView = (*lines)[*cursorY];

        if(!lineView.empty() && *cursorX >= 0 && !symbol.empty())
        {
            int pos = *cursorX;
            if(pos >= (int)lineView.size())
                pos = (int)lineView.size() - 1;
            if(pos >= 0 && !CppNavigationUtilities::isIdent(lineView[pos]) &&
               pos > 0 && CppNavigationUtilities::isIdent(lineView[pos - 1]))
            {
                pos--;
            }
            if(pos >= 0 && CppNavigationUtilities::isIdent(lineView[pos]))
            {
                int symStart = pos;
                int symEnd = pos;
                while(symStart > 0 &&
                      CppNavigationUtilities::isIdent(lineView[symStart - 1]))
                    --symStart;
                while(symEnd + 1 < (int)lineView.size() &&
                      CppNavigationUtilities::isIdent(lineView[symEnd + 1]))
                    ++symEnd;

                int before = symStart - 1;
                while(before >= 0 && text_utils::is_space(lineView[before]))
                    --before;
                if(before >= 0 && lineView[before] == '.')
                {
                    std::string_view member =
                        lineView.substr(symStart, symEnd - symStart + 1);
                    int baseEnd = before - 1;
                    while(baseEnd >= 0 &&
                          text_utils::is_space(lineView[baseEnd]))
                        --baseEnd;
                    int baseStart = baseEnd;
                    while(baseStart >= 0 &&
                          CppNavigationUtilities::isIdent(lineView[baseStart]))
                        --baseStart;
                    ++baseStart;
                    if(baseStart <= baseEnd)
                    {
                        std::string_view base =
                            lineView.substr(baseStart, baseEnd - baseStart + 1);
                        std::string typeName =
                            find_ts_type_for_identifier(*lines, base, *cursorY);
                        if(typeName.empty())
                        {
                            typeName = infer_ts_type_from_array_method_line(
                                lineView, base, *lines, *cursorY);
                        }
                        if(!typeName.empty())
                        {
                            int typeY = -1;
                            int typeX = 0;
                            if(find_ts_type_definition(*lines, typeName, typeY,
                                                       typeX))
                            {
                                int memberY = -1;
                                int memberX = 0;
                                if(find_ts_member_in_type(*lines, typeY, member,
                                                          memberY, memberX))
                                {
                                    pushJumpLocation();
                                    *cursorY = memberY;
                                    *cursorX = memberX;
                                    apply_gd_viewport();
                                    setStatusMessage(
                                        std::string("gd (ts member)") +
                                        gdArrow + *filename + ":" +
                                        std::to_string(memberY + 1));
                                    return;
                                }
                            }
                            std::unordered_map<std::string, std::string>
                                imports;
                            collect_js_ts_imports(*lines, imports);
                            auto itType = imports.find(typeName);
                            if(itType != imports.end())
                            {
                                std::string resolved = resolve_js_ts_module(
                                    currentBuffer->filename, itType->second);
                                if(!resolved.empty())
                                {
                                    std::ifstream in(resolved);
                                    if(in.is_open())
                                    {
                                        std::vector<std::string> fileLines;
                                        std::string fileLine;
                                        while(std::getline(in, fileLine))
                                        {
                                            if(!fileLine.empty() &&
                                               fileLine.back() == '\r')
                                                fileLine.pop_back();
                                            fileLines.push_back(fileLine);
                                        }
                                        int defY = -1;
                                        int defX = 0;
                                        if(find_ts_type_definition(
                                               fileLines, typeName, defY, defX))
                                        {
                                            int memberY = -1;
                                            int memberX = 0;
                                            if(find_ts_member_in_type(
                                                   fileLines, defY, member,
                                                   memberY, memberX))
                                            {
                                                pushJumpLocation();
                                                openFile(resolved);
                                                *cursorY = memberY;
                                                *cursorX = memberX;
                                                apply_gd_viewport();
                                                setStatusMessage(
                                                    std::string(
                                                        "gd (ts member)") +
                                                    gdArrow + resolved + ":" +
                                                    std::to_string(memberY +
                                                                   1));
                                                return;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        std::string_view module;
        if(!lineView.empty() &&
           extract_js_ts_module_specifier(lineView, module))
        {
            int x = *cursorX;
            size_t q = lineView.find_first_of("\"'");
            if(q != std::string_view::npos)
            {
                char quote = lineView[q];
                size_t end = lineView.find(quote, q + 1);
                if(end != std::string_view::npos && x >= (int)q + 1 &&
                   x <= (int)end)
                {
                    std::string resolved =
                        resolve_js_ts_module(currentBuffer->filename, module);
                    if(!resolved.empty())
                    {
                        pushJumpLocation();
                        openFile(resolved);
                        apply_gd_viewport();
                        setStatusMessage(std::string("gd (js/ts import)") +
                                         gdArrow + resolved);
                        return;
                    }
                }
            }
        }

        int defY = -1;
        int defX = 0;
        if(find_js_ts_def_in_file(currentBuffer->filename, symbol, defY, defX))
        {
            *cursorY = defY;
            *cursorX = defX;
            apply_gd_viewport();
            setStatusMessage(std::string("gd (js/ts)") + gdArrow + *filename +
                             ":" + std::to_string(defY + 1));
            return;
        }

        std::unordered_map<std::string, std::string> imports;
        collect_js_ts_imports(*lines, imports);
        auto it = imports.find(symbol);
        if(it != imports.end())
        {
            std::string resolved =
                resolve_js_ts_module(currentBuffer->filename, it->second);
            if(!resolved.empty())
            {
                int defFileY = -1;
                int defFileX = 0;
                bool found = find_js_ts_def_in_file(resolved, symbol, defFileY,
                                                    defFileX);
                pushJumpLocation();
                openFile(resolved);
                if(found)
                {
                    *cursorY = defFileY;
                    *cursorX = defFileX;
                    apply_gd_viewport();
                    setStatusMessage(std::string("gd (js/ts)") + gdArrow +
                                     resolved + ":" +
                                     std::to_string(defFileY + 1));
                }
                else
                {
                    apply_gd_viewport();
                    setStatusMessage(std::string("gd (js/ts import)") +
                                     gdArrow + resolved);
                }
                return;
            }
        }
    }

    if(isFileType<FileType::Html>())
    {
        std::string_view lineView;
        if(*cursorY >= 0 && *cursorY < (int)lines->size())
            lineView = (*lines)[*cursorY];

        std::string_view htmlPath;
        if(!lineView.empty() &&
           html_path_under_cursor(lineView, *cursorX, htmlPath))
        {
            std::string htmlPathStr(htmlPath);
            if(!htmlPathStr.empty() && htmlPathStr.front() != '.' &&
               htmlPathStr.front() != '/')
            {
                htmlPathStr = "./" + htmlPathStr;
            }
            std::string resolved =
                resolve_js_ts_module_path(currentBuffer->filename, htmlPathStr);
            if(!resolved.empty())
            {
                pushJumpLocation();
                openFile(resolved);
                apply_gd_viewport();
                setStatusMessage(std::string("gd (html link)") + gdArrow +
                                 resolved);
                return;
            }
        }

        bool inClass = false;
        bool inId = false;
        if(!lineView.empty())
        {
            std::string_view trimmed = trim_view(lineView);
            auto check_attr = [&](std::string_view attr) -> bool
            {
                size_t pos = trimmed.find(attr);
                if(pos == std::string_view::npos)
                    return false;
                size_t eq = trimmed.find('=', pos + attr.size());
                if(eq == std::string_view::npos)
                    return false;
                size_t q = trimmed.find_first_of("\"'", eq + 1);
                if(q == std::string_view::npos)
                    return false;
                char quote = trimmed[q];
                size_t end = trimmed.find(quote, q + 1);
                if(end == std::string_view::npos || end <= q + 1)
                    return false;
                int startX = static_cast<int>(
                    q + 1 + (trimmed.data() - lineView.data()));
                int endX =
                    static_cast<int>(end + (trimmed.data() - lineView.data()));
                return *cursorX >= startX && *cursorX <= endX;
            };
            inClass = check_attr("class");
            inId = check_attr("id");
        }

        if(!symbol.empty() && (inClass || inId))
        {
            auto sheets = extract_html_stylesheets(*lines);
            for(const auto& sheet : sheets)
            {
                std::string resolved =
                    resolve_js_ts_module_path(currentBuffer->filename, sheet);
                if(resolved.empty())
                    continue;
                std::string selector = inId ? "#" + symbol : "." + symbol;
                int defY = -1;
                int defX = 0;
                if(find_css_selector_in_file(resolved, selector, defY, defX))
                {
                    pushJumpLocation();
                    openFile(resolved);
                    *cursorY = defY;
                    *cursorX = defX;
                    apply_gd_viewport();
                    setStatusMessage(std::string("gd (html css)") + gdArrow +
                                     resolved + ":" + std::to_string(defY + 1));
                    return;
                }
            }
        }
    }

    if(isFileType<FileType::Css>())
    {
        std::string_view lineView;
        if(*cursorY >= 0 && *cursorY < (int)lines->size())
            lineView = (*lines)[*cursorY];

        std::string_view cssPath;
        if(!lineView.empty() &&
           css_import_path_under_cursor(lineView, *cursorX, cssPath))
        {
            std::string cssPathStr(cssPath);
            if(!cssPathStr.empty() && cssPathStr.front() != '.' &&
               cssPathStr.front() != '/')
            {
                cssPathStr = "./" + cssPathStr;
            }
            std::string resolved =
                resolve_js_ts_module_path(currentBuffer->filename, cssPathStr);
            if(!resolved.empty())
            {
                pushJumpLocation();
                openFile(resolved);
                apply_gd_viewport();
                setStatusMessage(std::string("gd (css import)") + gdArrow +
                                 resolved);
                return;
            }
        }
    }

    if(isMlangLspEnabled() && isFileType<FileType::Mla>())
    {
        if(*cursorY >= 0 && *cursorY < (int)lines->size())
        {
            const std::string& line = (*lines)[*cursorY];
            std::string modulePath;
            if(MlangUtilities::moduleDeclUnderCursor(line, *cursorX,
                                                     modulePath))
            {
                std::string moduleFile;
                if(MlangUtilities::resolveModuleFile(
                       modulePath, currentBuffer->filename, moduleFile))
                {
                    pushJumpLocation();
                    openFile(moduleFile);
                    *cursorY = 0;
                    *cursorX = 0;
                    apply_gd_viewport();
                    setStatusMessage(std::string("gd (mlang mod)") + gdArrow +
                                     moduleFile);
                    return;
                }
            }
        }

        int lspX = *cursorX;
        std::vector<int> lspQueryXs;
        if(*cursorY >= 0 && *cursorY < (int)lines->size())
        {
            const std::string& line = (*lines)[*cursorY];
            auto is_sym = [](char ch) -> bool
            {
                unsigned char u = static_cast<unsigned char>(ch);
                return std::isalnum(u) || ch == '_';
            };
            auto push_query_x = [&](int x)
            {
                if(x < 0 || x >= (int)line.size())
                    return;
                for(int existing : lspQueryXs)
                {
                    if(existing == x)
                        return;
                }
                lspQueryXs.push_back(x);
            };

            if(!line.empty())
            {
                if(lspX >= (int)line.size())
                    lspX = (int)line.size() - 1;
                if(lspX < 0)
                    lspX = 0;

                // For member access, keep cursor-side intent:
                // - on lhs object -> query lhs
                // - on separator/rhs -> query rhs member token
                if(is_sym(line[lspX]))
                {
                    int start = lspX;
                    int end = lspX;
                    while(start > 0 && is_sym(line[start - 1]))
                        --start;
                    while(end + 1 < (int)line.size() && is_sym(line[end + 1]))
                        ++end;
                    int p = end + 1;
                    while(p < (int)line.size() &&
                          std::isspace((unsigned char)line[p]))
                        ++p;
                    int sepPos = p;
                    bool hasArrow = (p + 1 < (int)line.size() &&
                                     line[p] == '-' && line[p + 1] == '>');
                    bool hasDot =
                        (!hasArrow && p < (int)line.size() && line[p] == '.');
                    if(hasArrow)
                        p += 2;
                    else if(hasDot)
                        ++p;
                    while(p < (int)line.size() &&
                          std::isspace((unsigned char)line[p]))
                        ++p;
                    if((hasArrow || hasDot) && *cursorX >= sepPos &&
                       p < (int)line.size() && is_sym(line[p]))
                        lspX = p;
                }

                // If cursor is on punctuation/space around a symbol, shift to
                // nearest identifier character so LSP definition works.
                if(!is_sym(line[lspX]))
                {
                    if(line[lspX] == '.' || line[lspX] == ':')
                    {
                        int right = lspX + 1;
                        while(right < (int)line.size() && !is_sym(line[right]))
                            ++right;
                        if(right < (int)line.size() && is_sym(line[right]))
                            lspX = right;
                    }
                    int left = lspX - 1;
                    while(left >= 0 && !is_sym(line[left]))
                        --left;
                    int right = lspX + 1;
                    while(right < (int)line.size() && !is_sym(line[right]))
                        ++right;
                    if(left >= 0 && is_sym(line[left]))
                        lspX = left;
                    else if(right < (int)line.size() && is_sym(line[right]))
                        lspX = right;
                }

                push_query_x(lspX);
                push_query_x(*cursorX);

                // Query token edges around the chosen position.
                if(is_sym(line[lspX]))
                {
                    int start = lspX;
                    int end = lspX;
                    while(start > 0 && is_sym(line[start - 1]))
                        --start;
                    while(end + 1 < (int)line.size() && is_sym(line[end + 1]))
                        ++end;
                    push_query_x(start);
                    push_query_x(end);

                    // Also try RHS member token for obj.field and obj->field.
                    int p = end + 1;
                    while(p < (int)line.size() &&
                          std::isspace((unsigned char)line[p]))
                        ++p;
                    int sepPos = p;
                    bool sawMemberSep = false;
                    if(p + 1 < (int)line.size() && line[p] == '-' &&
                       line[p + 1] == '>')
                    {
                        sawMemberSep = true;
                        p += 2;
                    }
                    else if(p < (int)line.size() && line[p] == '.')
                    {
                        sawMemberSep = true;
                        ++p;
                    }
                    while(p < (int)line.size() &&
                          std::isspace((unsigned char)line[p]))
                        ++p;
                    // When cursor is on the object token, keep definition
                    // anchored to that token instead of jumping to rhs member.
                    if(sawMemberSep && *cursorX > end && *cursorX >= sepPos &&
                       p < (int)line.size() && is_sym(line[p]))
                    {
                        push_query_x(p);
                        int rhs_end = p;
                        while(rhs_end + 1 < (int)line.size() &&
                              is_sym(line[rhs_end + 1]))
                            ++rhs_end;
                        push_query_x(rhs_end);
                    }
                }
            }
            else
            {
                push_query_x(lspX);
                push_query_x(*cursorX);
            }
        }
        if(lspQueryXs.empty())
            lspQueryXs.push_back(lspX);

        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }

        mlangLspClient->didChange(currentBuffer->filename, text, "mlang");
        for(int queryX : lspQueryXs)
        {
            auto loc = mlangLspClient->definition(currentBuffer->filename,
                                                  *cursorY, queryX);
            if(!loc)
                continue;
            pushJumpLocation();
            openFile(loc->path);
            *cursorY = loc->line;
            *cursorX = loc->character;

            if(*cursorY >= (int)lines->size())
                *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
            if(*cursorY >= 0 && *cursorX > (int)(*lines)[*cursorY].length())
                *cursorX = (*lines)[*cursorY].length();

            apply_gd_viewport();
            setStatusMessage(std::string("gd (mlang)") + gdArrow + loc->path +
                             ":" + std::to_string(loc->line + 1));
            return;
        }

        if(syntaxHighlighter)
            syntaxHighlighter->ensureMlangTokensLoaded();
        if(mlangTokenCache)
        {
            std::string key =
                mlangTokenCache->caseInsensitive ? ascii_lower(symbol) : symbol;
            auto it = mlangTokenCache->builtinTypes.find(key);
            if(it != mlangTokenCache->builtinTypes.end())
            {
                pushJumpLocation();
                openFile(it->second.path);
                *cursorY = it->second.line;
                *cursorX = 0;
                if(*cursorY >= (int)lines->size())
                    *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
                apply_gd_viewport();
                setStatusMessage(std::string("gd (mlang builtin)") + gdArrow +
                                 it->second.path + ":" +
                                 std::to_string(it->second.line + 1));
                return;
            }

            std::string macroKey = key;
            if(!macroKey.empty() && macroKey.back() == '!')
                macroKey.pop_back();
            auto mit = mlangTokenCache->builtinMacros.find(macroKey);
            if(mit != mlangTokenCache->builtinMacros.end())
            {
                pushJumpLocation();
                openFile(mit->second.path);
                *cursorY = mit->second.line;
                *cursorX = 0;
                if(*cursorY >= (int)lines->size())
                    *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
                apply_gd_viewport();
                setStatusMessage(std::string("gd (mlang macro)") + gdArrow +
                                 mit->second.path + ":" +
                                 std::to_string(mit->second.line + 1));
                return;
            }

            auto ait = mlangTokenCache->builtinAttributes.find(key);
            if(ait != mlangTokenCache->builtinAttributes.end())
            {
                pushJumpLocation();
                openFile(ait->second.path);
                *cursorY = ait->second.line;
                *cursorX = 0;
                if(*cursorY >= (int)lines->size())
                    *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
                apply_gd_viewport();
                setStatusMessage(std::string("gd (mlang attribute)") + gdArrow +
                                 ait->second.path + ":" +
                                 std::to_string(ait->second.line + 1));
                return;
            }

            auto fit = mlangTokenCache->builtinFunctions.find(key);
            if(fit != mlangTokenCache->builtinFunctions.end())
            {
                pushJumpLocation();
                openFile(fit->second.path);
                *cursorY = fit->second.line;
                *cursorX = 0;
                if(*cursorY >= (int)lines->size())
                    *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
                apply_gd_viewport();
                setStatusMessage(std::string("gd (mlang fn)") + gdArrow +
                                 fit->second.path + ":" +
                                 std::to_string(fit->second.line + 1));
                return;
            }
        }

        {
            std::string builtinPath;
            int builtinLine = 0;
            if(MlangUtilities::findBuiltinType(symbol, builtinPath,
                                               builtinLine))
            {
                pushJumpLocation();
                openFile(builtinPath);
                *cursorY = builtinLine;
                *cursorX = 0;
                if(*cursorY >= (int)lines->size())
                    *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
                apply_gd_viewport();
                setStatusMessage(std::string("gd (mlang builtin)") + gdArrow +
                                 builtinPath + ":" +
                                 std::to_string(builtinLine + 1));
                return;
            }
            std::string macroPath;
            int macroLine = 0;
            std::string macroSym = symbol;
            if(!macroSym.empty() && macroSym.back() == '!')
                macroSym.pop_back();
            if(MlangUtilities::findBuiltinMacro(macroSym, macroPath, macroLine))
            {
                pushJumpLocation();
                openFile(macroPath);
                *cursorY = macroLine;
                *cursorX = 0;
                if(*cursorY >= (int)lines->size())
                    *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
                apply_gd_viewport();
                setStatusMessage(std::string("gd (mlang macro)") + gdArrow +
                                 macroPath + ":" +
                                 std::to_string(macroLine + 1));
                return;
            }
            std::string attrPath;
            int attrLine = 0;
            if(MlangUtilities::findBuiltinAttribute(symbol, attrPath, attrLine))
            {
                pushJumpLocation();
                openFile(attrPath);
                *cursorY = attrLine;
                *cursorX = 0;
                if(*cursorY >= (int)lines->size())
                    *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
                apply_gd_viewport();
                setStatusMessage(std::string("gd (mlang attribute)") + gdArrow +
                                 attrPath + ":" + std::to_string(attrLine + 1));
                return;
            }
            std::string fnPath;
            int fnLine = 0;
            if(MlangUtilities::findBuiltinFunction(symbol, fnPath, fnLine,
                                                   currentBuffer->filename))
            {
                pushJumpLocation();
                openFile(fnPath);
                *cursorY = fnLine;
                *cursorX = 0;
                if(*cursorY >= (int)lines->size())
                    *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
                apply_gd_viewport();
                setStatusMessage(std::string("gd (mlang fn)") + gdArrow +
                                 fnPath + ":" + std::to_string(fnLine + 1));
                return;
            }
        }

        {
            int defY = -1;
            int defX = 0;
            if(MlangUtilities::findTopLevelDefInLines(*lines, symbol, defY,
                                                      defX))
            {
                pushJumpLocation();
                *cursorY = defY;
                *cursorX = defX;
                apply_gd_viewport();
                setStatusMessage(std::string("gd (mlang local)") + gdArrow +
                                 *filename + ":" + std::to_string(defY + 1));
                return;
            }
        }

        {
            int defY = -1;
            int defX = 0;
            if(CppNavigationUtilities::searchLocalDefinition(
                   *lines, symbol, *cursorY, *cursorX, defY, defX))
            {
                if(defY != *cursorY || defX != *cursorX)
                {
                    pushJumpLocation();
                    *cursorY = defY;
                    *cursorX = defX;
                    apply_gd_viewport();
                    setStatusMessage(std::string("gd (mlang local)") + gdArrow +
                                     *filename + ":" +
                                     std::to_string(defY + 1));
                    return;
                }
            }
            if(CppNavigationUtilities::searchMemberDefinition(*lines, symbol,
                                                              defY, defX))
            {
                pushJumpLocation();
                *cursorY = defY;
                *cursorX = defX;
                apply_gd_viewport();
                setStatusMessage(std::string("gd (mlang member)") + gdArrow +
                                 *filename + ":" + std::to_string(defY + 1));
                return;
            }
        }

        setStatusMessage("gd (mlang): not found");
        return;
    }

    // Prefer clangd definition when enabled; fallback to heuristic gd
    // otherwise.
    if(isClangdLspEnabled() && isFileType<FileType::Cpp>() &&
       !isFileType<FileType::Mla>())
    {
        // Sync buffer text (full-text change) before querying.
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }

        // Open or change in LSP.
        // If this file hasn't been seen yet, didOpen is fine; otherwise
        // didChange updates version. We'll conservatively call didChange after
        // didOpen attempt.
        lspClient->didChange(currentBuffer->filename, text, "cpp");

        // LSP uses UTF-16 positions; lsp_client converts from utf8 byte offset.
        auto loc =
            lspClient->definition(currentBuffer->filename, *cursorY, *cursorX);
        if(loc)
        {
            pushJumpLocation();
            openFile(loc->path);

            // Set cursor position from LSP (both are 0-based)
            *cursorY = loc->line;
            *cursorX = loc->character;

            // Ensure cursor is within valid bounds
            if(*cursorY >= (int)lines->size())
                *cursorY = lines->size() > 0 ? lines->size() - 1 : 0;
            if(*cursorY >= 0 && *cursorX > (int)(*lines)[*cursorY].length())
                *cursorX = (*lines)[*cursorY].length();

            apply_gd_viewport();

            // Show a cleaner message for system headers
            std::string displayPath = loc->path;
            bool isSystemHeader =
                (loc->path.find("/usr/") == 0 || loc->path.find("/opt/") == 0 ||
                 loc->path.find("/Library/") == 0 ||
                 loc->path.find("/Applications/") == 0);
            if(isSystemHeader)
            {
                // Show just the filename for system headers
                size_t lastSlash = loc->path.rfind('/');
                if(lastSlash != std::string::npos)
                    displayPath = "<sys>/" + loc->path.substr(lastSlash + 1);
            }
            setStatusMessage(std::string("gd (clangd)") + gdArrow +
                             displayPath + ":" + std::to_string(loc->line + 1));
            return;
        }
    }
#endif

    // std::symbol fallback for C/C++: open matching system header when
    // possible. Keep .mla navigation in mlang domain.
    if(!isFileType<FileType::Mla>() && *cursorY >= 0 &&
       *cursorY < (int)lines->size())
    {
        const std::string& line = (*lines)[*cursorY];
        int x = *cursorX;
        if(x >= 0 && x < (int)line.size() &&
           CppNavigationUtilities::isIdent(line[x]))
        {
            int l = x;
            while(l > 0 && CppNavigationUtilities::isIdent(line[l - 1]))
                l--;

            if(l >= 5 && line.compare(l - 5, 5, "std::") == 0)
            {
                std::string header =
                    CppNavigationUtilities::resolveSystemInclude(symbol);
                if(!header.empty())
                {
                    pushJumpLocation();
                    openFile(header);
                    setStatusMessage(std::string("gd") + gdArrow + "<sys>/" +
                                     symbol);
                    return;
                }
            }
        }
    }

    pushJumpLocation();

    int y, x;
    std::string current = currentBuffer->filename;
    std::string alternate = findAlternateFile(current);

    // 1️⃣ First: Search for LOCAL variable/parameter declaration (backwards from
    // cursor) This handles local variables and function parameters
    if(CppNavigationUtilities::searchLocalDefinition(*lines, symbol, *cursorY,
                                                     *cursorX, y, x))
    {
        // Make sure we're not jumping to ourselves
        if(y != *cursorY || x != *cursorX)
        {
            *cursorY = y;
            *cursorX = x;
            apply_gd_viewport();
            setStatusMessage(std::string("gd") + gdArrow + "local '" + symbol +
                             "' at " + std::to_string(y + 1) + ":" +
                             std::to_string(x + 1));
            return;
        }
    }

    // 2️⃣ Search for member variable in current file (class/struct members)
    if(CppNavigationUtilities::searchMemberDefinition(*lines, symbol, y, x))
    {
        if(y != *cursorY || x != *cursorX)
        {
            *cursorY = y;
            *cursorX = x;
            apply_gd_viewport();
            setStatusMessage(std::string("gd") + gdArrow + "member '" + symbol +
                             "' at " + std::to_string(y + 1) + ":" +
                             std::to_string(x + 1));
            return;
        }
    }

    // 3️⃣ Search for function definition in alternate file (header <-> source)
    if(!alternate.empty())
    {
        openFile(alternate);

        if(searchDefinitionInBuffer(currentBuffer, symbol, y, x))
        {
            *cursorY = y;
            *cursorX = x;
            apply_gd_viewport();
            setStatusMessage(std::string("gd") + gdArrow + alternate);
            return;
        }

        // Not found → go back
        openFile(current);
    }

    // 4️⃣ Fallback: Search for function definition in current file
    if(searchDefinitionInBuffer(currentBuffer, symbol, y, x))
    {
        *cursorY = y;
        *cursorX = x;
        apply_gd_viewport();
        setStatusMessage("gd (same file)");
        return;
    }

    setStatusMessage("gd: '" + symbol +
                     "' not found (curY=" + std::to_string(*cursorY) +
                     " curX=" + std::to_string(*cursorX) + ")");
}

void Editor::refreshScreen()
{
    drawingController->refreshScreen();
    return;

    modeController->syncModeFromStateMachine();

    if(diagnosticPopupActive && (*cursorY != diagnosticPopupCursorY ||
                                 *cursorX != diagnosticPopupCursorX))
    {
        closeDiagnosticPopup();
    }
    if(symbolPopupActive &&
       (*cursorY != symbolPopupCursorY || *cursorX != symbolPopupCursorX))
    {
        closeSymbolPopup();
    }

    if(currentMode == WELCOME)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<WelcomeMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == FILE_BROWSER)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<FileBrowserMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == FUZZY_FIND)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<FuzzyFindMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == BUFFER_BROWSER)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<BufferBrowserMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == GREP_SEARCH)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<GrepSearchMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == REGEX_SEARCH)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<RegexSearchMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == LOC_LIST)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<LocListMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == REFERENCES)
    {
        drawReferences();
        return;
    }
    if(currentMode == LSP_INFO)
    {
        drawLspInfo();
        return;
    }

    if(currentMode == HELP)
    {
        if(!needsFullRedraw)
            return;
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<HelpMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == GIT_SHOW)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<GitShowCommitMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == GIT_LOG)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<GitLogMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == GIT_STAGE)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<GitStageMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == GIT_COMMIT)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<GitCommitMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == GIT_FIXUP)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<GitFixupMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == GIT_PATCH)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<GitPatchMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

    if(currentMode == COMMAND_OUTPUT)
    {
        if(modeStateMachine)
        {
            if(auto* state = modeStateMachine->getState<CommandOutputMode>())
            {
                state->draw(*this);
                return;
            }
        }
        return;
    }

#ifdef UVIM_ENABLE_CLANGD_LSP
    if(currentMode != INSERT && !showGitBlame)
    {
        if(currentBuffer && isClangdLspEnabled() &&
           isFileType<FileType::Cpp>() && !isFileType<FileType::Mla>() &&
           lspClient && !currentBuffer->filename.empty())
        {
            size_t revision =
                lspClient->diagnosticsRevision(currentBuffer->filename);
            if(!currentBuffer->lspDiagnosticsSeenValid ||
               revision != currentBuffer->lspDiagnosticsSeenRevision)
            {
                currentBuffer->lspDiagnosticsSeenRevision = revision;
                currentBuffer->lspDiagnosticsSeenValid = true;
                needsFullRedraw = true;
            }
        }
        syncClangdDiagnosticsIfNeeded(false);
        syncMlangSemanticTokensIfNeeded(false);
    }
#endif

    static int lastOffsetY = -1;
    static int lastOffsetX = -1;
    static Mode lastMode = NORMAL;
    static int lastVisualStartY = -1;
    static int lastVisualEndY = -1;
    static int lastCursorY = -1;
    static bool lastCommandPopupActive = false;
    static bool lastCommandHistoryPopupActive = false;

    int prevOffsetY = lastOffsetY;
    adjustViewport();

    if(splitActive)
    {
        drawFullScreen();
        lastOffsetY = *offsetY;
        lastOffsetX = *offsetX;
        lastMode = currentMode;
        lastCursorY = *cursorY;
        lastCommandPopupActive = commandPopupActive;
        lastCommandHistoryPopupActive = commandHistorySearchActive;
        needsFullRedraw = false;
        return;
    }

    bool scrolled = (*offsetY != lastOffsetY || *offsetX != lastOffsetX);
    bool modeChanged = (currentMode != lastMode);
    int scrollDelta = *offsetY - lastOffsetY;
    bool cursorMoved = (*cursorY != lastCursorY);

    if(showGitBlame && currentBuffer && !currentBuffer->blameValid)
        updateGitBlameForVisibleRange();

    bool visualChanged = false;
    if(currentMode == VISUAL || currentMode == VISUAL_LINE ||
       currentMode == VISUAL_BLOCK)
    {
        visualChanged = (currentBuffer->visualStartY != lastVisualStartY ||
                         currentBuffer->visualEndY != lastVisualEndY);
        lastVisualStartY = currentBuffer->visualStartY;
        lastVisualEndY = currentBuffer->visualEndY;
    }
    else
    {
        lastVisualStartY = -1;
        lastVisualEndY = -1;
    }

    bool isBufferEditingMode =
        (currentMode == INSERT || currentMode == REPLACE);
    bool isCommandLikeMode =
        (currentMode == COMMAND || currentMode == SEARCH_FORWARD ||
         currentMode == SEARCH_BACKWARD);
    bool isLiveSearchMode =
        (currentMode == SEARCH_FORWARD || currentMode == SEARCH_BACKWARD);
    bool commandPopupChanged =
        (commandPopupActive != lastCommandPopupActive) ||
        (commandHistorySearchActive != lastCommandHistoryPopupActive);
    bool commandOverlayStable = isCommandLikeMode && !isLiveSearchMode &&
                                !modeChanged &&
                                scrollDelta == 0 && *offsetX == lastOffsetX &&
                                !visualChanged && !commandPopupChanged;

    if(modeChanged || (needsFullRedraw && !commandOverlayStable) ||
       *offsetX != lastOffsetX || abs(scrollDelta) > screenRows / 2 ||
       visualChanged ||
       (currentMode == VISUAL || currentMode == VISUAL_LINE ||
        currentMode == VISUAL_BLOCK) ||
       isBufferEditingMode)
    {
        drawFullScreen();
    }
    else if(scrollDelta == 0 && isCommandLikeMode)
    {
        // Command/search editing only affects overlays (message line, popups,
        // cursor). Keep buffer rows stable to avoid tmux flicker.
        const bool syncOutput = Terminal::useSynchronizedOutput();
        if(syncOutput)
            Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
        Terminal::write(Terminal::ESC_HIDE_CURSOR);
        drawStatusBarQuick();
        drawMessageBarQuick();
        updateCursorPosition(false);
        if(syncOutput)
            Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
        Terminal::flush();
    }
    else if(scrollDelta != 0 && abs(scrollDelta) <= 5 &&
            currentMode == NORMAL && !Terminal::isTmux())
    {
        drawScrollUpdate(scrollDelta);
    }
    else if(scrollDelta == 0 && currentMode == NORMAL)
    {
        const bool syncOutput = Terminal::useSynchronizedOutput();
        if(syncOutput)
            Terminal::write(Terminal::ESC_SYNC_UPDATE_BEGIN);
        Terminal::write(Terminal::ESC_HIDE_CURSOR);
        if(cursorMoved && lineNumberWidth() > 0)
            drawGutterQuick();
        drawStatusBarQuick();
        drawMessageBarQuick(); // Add this
        updateCursorPosition(false);
        if(syncOutput)
            Terminal::write(Terminal::ESC_SYNC_UPDATE_END);
        Terminal::flush();
    }
    else
    {
        drawFullScreen();
    }

    lastOffsetY = *offsetY;
    lastOffsetX = *offsetX;
    lastMode = currentMode;
    lastCursorY = *cursorY;
    lastCommandPopupActive = commandPopupActive;
    lastCommandHistoryPopupActive = commandHistorySearchActive;
    needsFullRedraw = false;
}

void Editor::updateCursorPosition(bool flushNow)
{
    drawingController->updateCursorPosition(flushNow);
    return;

    int cursorRow, cursorCol;

    if(currentMode == COMMAND || currentMode == SEARCH_FORWARD ||
       currentMode == SEARCH_BACKWARD)
    {
        cursorRow = screenRows + 2;
        int promptLen = (int)commandBuffer.length();
        if(commandBuffer.empty())
            promptLen = 1;
        else if(currentMode == COMMAND && commandBuffer[0] != ':')
            promptLen += 1;
        cursorCol = promptLen + 1;
    }
    else
    {
        PaneLayout layout = getPaneLayout(activePane);
        cursorRow = layout.y + (*cursorY - *offsetY) + 1 + tabBarRows();
        if(utf8Mode && *cursorY >= 0 && *cursorY < (int)lines->size())
        {
            const std::string& line = (*lines)[*cursorY];
            int start = std::clamp(*offsetX, 0, (int)line.size());
            int end = std::clamp(*cursorX, 0, (int)line.size());
            if(end < start)
                std::swap(start, end);
            cursorCol = text_utils::utf8DisplayWidth(
                            std::string_view(line).substr(start, end - start)) +
                        1 + gutterWidth() + layout.x;
        }
        else
        {
            cursorCol = layout.x + (*cursorX - *offsetX) + 1 + gutterWidth();
        }
    }

    Terminal::write(Terminal::cursorPos(cursorRow, cursorCol));
    bool hideCursor = (currentMode == VISUAL || currentMode == VISUAL_LINE ||
                       currentMode == VISUAL_BLOCK);
    Terminal::write(hideCursor ? Terminal::ESC_HIDE_CURSOR
                               : Terminal::ESC_SHOW_CURSOR);
    if(flushNow)
        Terminal::flush();

    lastCursorScreenY = cursorRow;
    lastCursorScreenX = cursorCol;
}

void Editor::draw()
{
    drawingController->draw();
}

void Editor::setStatusMessage(const std::string& msg)
{
    statusMessage = msg;
}

bool Editor::noteDoubleEscStatusClear()
{
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastEsc =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastEscTime)
            .count();
    if(timeSinceLastEsc <= DOUBLE_ESC_TIMEOUT_MS)
    {
        setStatusMessage("");
        needsFullRedraw = true;
        lastEscTime = std::chrono::steady_clock::time_point();
        return true;
    }
    lastEscTime = now;
    return false;
}

int Editor::tabBarRows() const
{
    return splitController->tabBarRows();
}

int Editor::contentRows() const
{
    return splitController->contentRows();
}

Editor::PaneLayout Editor::getPaneLayout(int pane) const
{
    return splitController->getPaneLayout(pane);
}

void Editor::setPanePointers(int pane)
{
    splitController->setPanePointers(pane);
}

void Editor::enableSplit(bool vertical)
{
    splitController->enableSplit(vertical);
}

void Editor::closeSplit()
{
    splitController->closeSplit();
}

void Editor::switchPane()
{
    splitController->switchPane();
}

void Editor::syncBufferStateFromActivePane()
{
    splitController->syncBufferStateFromActivePane();
}

void Editor::initSplitPanesFromBuffer()
{
    splitController->initSplitPanesFromBuffer();
}

void Editor::switchToBufferInActivePane(int index)
{
    splitController->switchToBufferInActivePane(index);
}

bool Editor::canSplit() const
{
    return splitController->canSplit();
}

int Editor::lineNumberWidth() const
{
    return gitController->lineNumberWidth();
}

int Editor::gitBlameWidth() const
{
    return gitController->gitBlameWidth();
}

int Editor::gutterWidth() const
{
    return gitController->gutterWidth();
}

void Editor::toggleGitBlame()
{
    gitController->toggleGitBlame();
}

void Editor::updateGitBlameForVisibleRange()
{
    gitController->updateGitBlameForVisibleRange();
}

std::string Editor::blameDisplayForLine(int row) const
{
    return gitController->blameDisplayForLine(row);
}

std::string Editor::blameFullForLine(int row) const
{
    return gitController->blameFullForLine(row);
}

void Editor::openGitShowCommitMode()
{
    gitController->openGitShowCommitMode();
}

std::vector<std::string> Editor::loadGitShowLines(const std::string& hash)
{
    return gitController->loadGitShowLines(hash);
}

void Editor::openGitLogMode()
{
    gitController->openGitLogMode();
}

void Editor::openGitPrettyLogMode()
{
    gitController->openGitPrettyLogMode();
}

void Editor::openGitLogModeForFile()
{
    gitController->openGitLogModeForFile();
}

void Editor::openGitStageMode()
{
    gitController->openGitStageMode();
}

void Editor::openGitDiffMode()
{
    gitController->openGitDiffMode();
}

void Editor::openGitCommitMode()
{
    gitController->openGitCommitMode();
}

void Editor::openGitFixupMode()
{
    gitController->openGitFixupMode();
}

void Editor::addCurrentBuffer()
{
    gitController->addCurrentBuffer();
}

bool Editor::runGitStash(std::string& outMessage)
{
    return gitController->runGitStash(outMessage);
}

bool Editor::runGitStashPop(std::string& outMessage)
{
    return gitController->runGitStashPop(outMessage);
}

void Editor::updateClangFormatIndentWidth()
{
    indentController->updateClangFormatIndentWidth();
}

int Editor::indentWidthForBraces() const
{
    return indentController->indentWidthForBraces();
}

bool Editor::braceNewLineForAutoBraces() const
{
    return indentController->braceNewLineForAutoBraces();
}

void Editor::commentLines(int startY, int endY)
{
    indentController->commentLines(startY, endY);
}

void Editor::syncClangdDiagnosticsIfNeeded(bool force)
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!currentBuffer || !isClangdLspEnabled() ||
       !isFileType<FileType::Cpp>() || !lspClient)
        return;

    bool wantSemantic =
        syntaxCppSemanticTokens && !currentBuffer->lspSemanticTokensValid;
    bool shouldCheck = force || currentBuffer->lspSyncNeeded || *dirty;
    if(!shouldCheck && !wantSemantic)
        return;

    size_t newHash = 0;
    if(shouldCheck || wantSemantic)
    {
        newHash = hash_lines(*lines);
    }
    if(shouldCheck && (force || !currentBuffer->lspHashValid ||
                       newHash != currentBuffer->lspContentHash))
    {
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }
        lspClient->didChange(currentBuffer->filename, text, "cpp");
        currentBuffer->lspContentHash = newHash;
        currentBuffer->lspHashValid = true;
    }

    if(syntaxCppSemanticTokens && (wantSemantic || shouldCheck))
    {
        bool refreshSemantic =
            force || !currentBuffer->lspSemanticTokensValid ||
            (newHash != 0 && newHash != currentBuffer->lspSemanticTokensHash);
        if(refreshSemantic)
            lspClient->requestSemanticTokens(currentBuffer->filename);

        size_t semRev =
            lspClient->semanticTokensRevision(currentBuffer->filename);
        if(semRev != currentBuffer->lspSemanticTokensRevision)
        {
            std::vector<LspClient::SemanticToken> tokens =
                lspClient->semanticTokens(currentBuffer->filename);
            currentBuffer->lspSemanticTokens.clear();
            currentBuffer->lspSemanticTokens.resize(lines->size());

            for(const auto& token : tokens)
            {
                if(token.line < 0 || token.line >= (int)lines->size())
                    continue;
                const std::string& line = (*lines)[token.line];
                int startByte = utf16ToUtf8ByteOffset(line, token.character);
                int endByte =
                    utf16ToUtf8ByteOffset(line, token.character + token.length);
                if(endByte <= startByte)
                    continue;
                if(startByte >= (int)line.size())
                    continue;
                if(endByte > (int)line.size())
                    endByte = (int)line.size();
                int length = endByte - startByte;
                if(length <= 0)
                    continue;
                bool isDecl = lspClient->semanticTokenHasModifier(
                    token.modifiers, "declaration");
                bool isDef = lspClient->semanticTokenHasModifier(
                    token.modifiers, "definition");
                currentBuffer->lspSemanticTokens[token.line].push_back(
                    {startByte, length, token.tokenType, isDecl, isDef});
            }
            currentBuffer->lspSemanticTokensRevision = semRev;
            currentBuffer->lspSemanticTokensValid = true;
            if(newHash != 0)
                currentBuffer->lspSemanticTokensHash = newHash;
            needsFullRedraw = true;
        }
    }

    if(diagnosticPopupActive)
    {
        std::optional<LspDiagnosticSummary> diag =
            getClangdDiagnosticForLine(diagnosticPopupLine);
        if(!diag || diag->severity <= 0 || diag->severity > 2)
        {
            closeDiagnosticPopup();
        }
        else
        {
            diagnosticPopupData = *diag;
        }
    }

    currentBuffer->lspSyncNeeded = false;
#else
    (void)force;
#endif
}

void Editor::syncMlangSemanticTokensIfNeeded(bool force)
{
#ifdef UVIM_ENABLE_CLANGD_LSP
    if(!currentBuffer || !isMlangLspEnabled() || !isFileType<FileType::Mla>() ||
       !mlangLspClient)
        return;

    bool wantSemantic = !currentBuffer->lspSemanticTokensValid;
    bool shouldCheck = force || currentBuffer->lspSyncNeeded || *dirty;
    if(!shouldCheck && !wantSemantic)
        return;

    size_t newHash = 0;
    if(shouldCheck || wantSemantic)
        newHash = hash_lines(*lines);

    if(shouldCheck && (force || !currentBuffer->lspHashValid ||
                       newHash != currentBuffer->lspContentHash))
    {
        std::string text;
        text.reserve(lines->size() * 80);
        for(size_t i = 0; i < lines->size(); ++i)
        {
            text += (*lines)[i];
            if(i + 1 < lines->size())
                text.push_back('\n');
        }
        mlangLspClient->didChange(currentBuffer->filename, text, "mlang");
        currentBuffer->lspContentHash = newHash;
        currentBuffer->lspHashValid = true;
    }

    if(wantSemantic || shouldCheck)
    {
        bool refreshSemantic =
            force || !currentBuffer->lspSemanticTokensValid ||
            (newHash != 0 && newHash != currentBuffer->lspSemanticTokensHash);
        if(refreshSemantic)
            mlangLspClient->requestSemanticTokens(currentBuffer->filename);

        size_t semRev =
            mlangLspClient->semanticTokensRevision(currentBuffer->filename);
        if(semRev != currentBuffer->lspSemanticTokensRevision)
        {
            std::vector<LspClient::SemanticToken> tokens =
                mlangLspClient->semanticTokens(currentBuffer->filename);
            currentBuffer->lspSemanticTokens.clear();
            currentBuffer->lspSemanticTokens.resize(lines->size());

            for(const auto& token : tokens)
            {
                if(token.line < 0 || token.line >= (int)lines->size())
                    continue;
                const std::string& line = (*lines)[token.line];
                int startByte = utf16ToUtf8ByteOffset(line, token.character);
                int endByte =
                    utf16ToUtf8ByteOffset(line, token.character + token.length);
                if(endByte <= startByte)
                    continue;
                if(startByte >= (int)line.size())
                    continue;
                if(endByte > (int)line.size())
                    endByte = (int)line.size();
                int length = endByte - startByte;
                if(length <= 0)
                    continue;
                bool isDecl = mlangLspClient->semanticTokenHasModifier(
                    token.modifiers, "declaration");
                bool isDef = mlangLspClient->semanticTokenHasModifier(
                    token.modifiers, "definition");
                currentBuffer->lspSemanticTokens[token.line].push_back(
                    {startByte, length, token.tokenType, isDecl, isDef});
            }
            currentBuffer->lspSemanticTokensRevision = semRev;
            currentBuffer->lspSemanticTokensValid = true;
            if(newHash != 0)
                currentBuffer->lspSemanticTokensHash = newHash;
            needsFullRedraw = true;
        }
    }

    currentBuffer->lspSyncNeeded = false;
#else
    (void)force;
#endif
}

bool Editor::handleSetCommand(std::string_view cmd)
{
    return settingsController->handleSetCommand(cmd);
}

void Editor::handleResize()
{
#if defined(UVIM_TERMINAL_POSIX)
    if(!g_pending_resize)
        return;
    g_pending_resize = 0;

    int rows = 0;
    int cols = 0;
    Terminal::getWindowSize(rows, cols);
    screenRows = std::max(1, rows - 2);
    screenCols = std::max(1, cols);
    needsFullRedraw = true;
#endif
}

#ifdef UVIM_TESTING
int Editor::testCountLocForFile(const std::string& filepath)
{
    auto rules = editor::helper::locCommentRulesForPath(filepath);
    return editor::helper::locCountInFile(filepath, rules);
}
#endif

// Command execution
void Editor::executeCommand(std::string_view cmd)
{
    if(!cmd.empty())
    {
        if(commandHistory.empty() || commandHistory.back() != cmd)
            commandHistory.push_back(std::string(cmd));
        commandHistoryIndex = -1;
    }

    if(!cmd.empty() && (cmd.front() == '/' || cmd.front() == '?'))
    {
        if(!hasBuffer())
        {
            setStatusMessage("No buffer");
            return;
        }
        std::string query = std::string(cmd.substr(1));
        if(!query.empty())
        {
            addSearchToHistory(query);
            savedCursorX = *cursorX;
            savedCursorY = *cursorY;
            performSearch(query, cmd.front() == '/');
        }
        return;
    }

    if(handleSetCommand(cmd))
        return;

    auto is_edit_percent = [&](std::string_view command, bool& force) -> bool
    {
        std::string_view trimmed = trim_view(command);
        auto check = [&](std::string_view prefix) -> bool
        {
            if(trimmed.rfind(prefix, 0) != 0)
                return false;
            std::string_view rest = trim_view(trimmed.substr(prefix.size()));
            if(!rest.empty() && rest.front() == '!')
            {
                force = true;
                rest = trim_view(rest.substr(1));
            }
            return rest == "%";
        };
        if(check("e"))
            return true;
        if(check("edit"))
            return true;
        return false;
    };

    bool editForce = false;
    if(is_edit_percent(cmd, editForce))
    {
        if(!hasBuffer() || !filename || filename->empty())
        {
            setStatusMessage("No file to reload");
            return;
        }
        if(*dirty && !editForce)
        {
            setStatusMessage(
                "No write since last change (use :e!% to discard)");
            return;
        }
        reloadCurrentFile();
        return;
    }

    if(cmd == "lspinfo")
    {
        showLspInfo();
        commandRequestedModeSet = true;
        commandRequestedMode = LSP_INFO;
        return;
    }
    auto is_ex_command = [&](std::string_view command,
                             std::string& outPath) -> bool
    {
        auto trim = [](std::string_view value) -> std::string_view
        {
            size_t start = 0;
            while(start < value.size() &&
                  (value[start] == ' ' || value[start] == '\t'))
                ++start;
            size_t end = value.size();
            while(end > start &&
                  (value[end - 1] == ' ' || value[end - 1] == '\t'))
                --end;
            return value.substr(start, end - start);
        };

        auto starts_with = [&](std::string_view prefix) -> bool
        { return command.rfind(prefix, 0) == 0; };

        if(command == "Ex" || command == "ex" || command == "E" ||
           command == "Explore" || command == "explore")
        {
            outPath = ".";
            return true;
        }

        if(starts_with("Ex ") || starts_with("ex ") || starts_with("E ") ||
           starts_with("Explore ") || starts_with("explore "))
        {
            std::string_view rest = command.substr(command.find(' ') + 1);
            rest = trim(rest);
            outPath = rest.empty() ? "." : std::string(rest);
            return true;
        }

        return false;
    };

    std::string exPath;
    if(is_ex_command(cmd, exPath))
    {
        commandRequestedModeSet = true;
        commandRequestedMode = FILE_BROWSER;
        commandRequestedPath = exPath;
        return;
    }
    if(cmd == "format" || cmd == "fmt")
    {
        if(!formatBuffer())
        {
            setStatusMessage("format: unsupported file type");
            return;
        }
    }
    if(cmd == "emoji" || cmd == "em")
    {
        openEmojiPopup();
        return;
    }

    if(cmd == "help" || cmd.rfind("help ", 0) == 0 || cmd.rfind("h ", 0) == 0)
    {
        std::string topic;
        if(cmd == "help" || cmd == "h")
        {
            topic = ""; // Default help
        }
        else if(cmd.rfind("help ", 0) == 0)
        {
            topic = std::string(cmd.substr(5));
        }
        else if(cmd.rfind("h ", 0) == 0)
        {
            topic = std::string(cmd.substr(2));
        }

        commandRequestedModeSet = true;
        commandRequestedMode = HELP;
        commandRequestedPath = topic; // Reuse path field for topic
        return;
    }

    std::string_view trimmedCmd = trim_view(cmd);
    if(trimmedCmd == "git stage")
    {
        commandRequestedModeSet = true;
        commandRequestedMode = GIT_STAGE;
        commandRequestedPath.clear();
        commandRequestedReturnMode = currentMode;
        if(modeStateMachine && currentMode == FILE_BROWSER)
        {
            if(auto* fb = modeStateMachine->getState<FileBrowserMode>())
            {
                commandRequestedBrowseCursor = fb->browserCursor;
                commandRequestedBrowseOffset = fb->browserOffset;
                commandRequestedBrowseDirectory = fb->currentDirectory;
            }
        }
        return;
    }
    if(trimmedCmd == "rfs")
    {
        refreshFileSearchCaches();
        return;
    }
    if(trimmedCmd == "git add")
    {
        addCurrentBuffer();
        return;
    }
    if(trimmedCmd == "git blame")
    {
        toggleGitBlame();
        return;
    }
    if(trimmedCmd == "git log")
    {
        openGitLogMode();
        return;
    }
    if(trimmedCmd == "git prettylog")
    {
        openGitPrettyLogMode();
        return;
    }
    if(trimmedCmd == "git diff")
    {
        openGitDiffMode();
        return;
    }
    if(trimmedCmd == "git commit")
    {
        openGitCommitMode();
        return;
    }
    if(trimmedCmd == "git fixup")
    {
        openGitFixupMode();
        return;
    }
    if(trimmedCmd == "git stash")
    {
        std::string msg;
        runGitStash(msg);
        setStatusMessage(msg);
        return;
    }
    if(trimmedCmd == "git stash pop")
    {
        std::string msg;
        runGitStashPop(msg);
        setStatusMessage(msg);
        return;
    }

    auto parse_loctotal_command = [&](std::string_view command,
                                      std::string& outPath) -> bool
    {
        std::string_view trimmed = trim_view(command);
        if(trimmed.rfind("loctotal", 0) != 0)
            return false;

        std::string_view rest = trim_view(trimmed.substr(8));
        outPath = rest.empty() ? "" : std::string(rest);
        return true;
    };

    auto parse_loc_command = [&](std::string_view command, bool& listView,
                                 std::string& outPath) -> bool
    {
        std::string_view trimmed = trim_view(command);
        if(trimmed.rfind("loc", 0) != 0 || trimmed.rfind("loctotal", 0) == 0)
            return false;

        std::string_view rest = trim_view(trimmed.substr(3));
        listView = false;

        if(!rest.empty() && rest.front() == '!')
        {
            listView = true;
            rest = trim_view(rest.substr(1));
        }

        if(rest.rfind("-l", 0) == 0)
        {
            listView = true;
            rest = trim_view(rest.substr(2));
        }
        else if(rest.rfind("--list", 0) == 0)
        {
            listView = true;
            rest = trim_view(rest.substr(6));
        }
        else if(rest.rfind("list", 0) == 0)
        {
            listView = true;
            rest = trim_view(rest.substr(4));
        }

        outPath = rest.empty() ? "" : std::string(rest);
        return true;
    };

    std::string locTotalPath;
    if(parse_loctotal_command(cmd, locTotalPath))
    {
        if(locTotalPath.empty())
        {
            if(!projectRoot.empty())
                locTotalPath = projectRoot;
            else
                locTotalPath = ".";
        }

        locTotalPath = expandTildePath(locTotalPath);

        std::error_code ec;
        std::filesystem::path targetPath =
            std::filesystem::absolute(locTotalPath, ec);
        if(ec)
            targetPath = std::filesystem::path(locTotalPath);

        if(!std::filesystem::exists(targetPath, ec))
        {
            setStatusMessage("loctotal: path not found: " + locTotalPath);
            return;
        }

        std::vector<std::string> files;
        if(std::filesystem::is_directory(targetPath, ec))
        {
            GitIgnore gitignore;
            if(respectGitignore)
                gitignore.loadRecursive(targetPath.string());
            collectLocFiles(targetPath.string(), 0, gitignore, files);
        }
        else
        {
            files.push_back(targetPath.string());
        }

        int totalLoc = 0;
        for(const auto& file : files)
        {
            if(!locIsTextFile(file))
                continue;
            auto rules = editor::helper::locCommentRulesForPath(file);
            totalLoc += locCountInFile(file, rules);
        }

        statusMessage.clear();
        locMessage = "LOC total " + std::to_string(totalLoc);
        needsFullRedraw = true;
        return;
    }

    bool locListView = false;
    std::string locPath;
    if(parse_loc_command(cmd, locListView, locPath))
    {
        bool locExplicitBuffer = false;
        if(locPath.empty())
        {
            if(hasBuffer() && filename && !filename->empty())
            {
                locPath = *filename;
                locExplicitBuffer = true;
            }
            else
            {
                locPath = ".";
            }
        }
        else if(locPath == "%")
        {
            if(hasBuffer() && filename && !filename->empty())
            {
                locPath = *filename;
                locExplicitBuffer = true;
            }
            else
            {
                setStatusMessage("loc: no current buffer");
                return;
            }
        }

        locPath = expandTildePath(locPath);

        std::error_code ec;
        std::filesystem::path targetPath =
            std::filesystem::absolute(locPath, ec);
        if(ec)
            targetPath = std::filesystem::path(locPath);

        if(!std::filesystem::exists(targetPath, ec))
        {
            setStatusMessage("loc: path not found: " + locPath);
            return;
        }

        std::vector<std::string> files;
        std::filesystem::path rootPath;
        std::string rootDisplay = locPath;
        bool useBufferForSingle = false;
        if(hasBuffer() && filename && !filename->empty())
        {
            std::error_code currErr;
            std::filesystem::path currentPath =
                std::filesystem::absolute(*filename, currErr);
            if(currErr)
                currentPath = std::filesystem::path(*filename);
            std::error_code eqErr;
            if(std::filesystem::equivalent(targetPath, currentPath, eqErr))
                useBufferForSingle = true;
        }
        if(locExplicitBuffer)
            useBufferForSingle = true;

        if(std::filesystem::is_directory(targetPath, ec))
        {
            locListView = true;
            rootPath = targetPath;
            GitIgnore gitignore;
            if(respectGitignore)
                gitignore.loadRecursive(rootPath.string());
            collectLocFiles(rootPath.string(), 0, gitignore, files);
        }
        else
        {
            rootPath = targetPath.parent_path();
            files.push_back(targetPath.string());
        }

        std::vector<LocEntry> entries;
        int totalLoc = 0;

        for(const auto& file : files)
        {
            if(!locIsTextFile(file))
                continue;

            auto rules = editor::helper::locCommentRulesForPath(file);
            int loc = 0;
            if(!locListView && useBufferForSingle)
                loc = locCountInLines(*lines, rules);
            else
                loc = locCountInFile(file, rules);
            totalLoc += loc;

            if(locListView)
            {
                LocEntry entry;
                entry.path = file;

                std::string displayPath = file;
                if(!rootPath.empty())
                {
                    std::error_code relErr;
                    std::filesystem::path rel =
                        std::filesystem::relative(file, rootPath, relErr);
                    if(!relErr)
                        displayPath = rel.string();
                }
                entry.displayPath = displayPath;
                entry.loc = loc;
                entries.push_back(std::move(entry));
            }
        }

        if(locListView)
        {
            std::sort(entries.begin(), entries.end(),
                      [](const LocEntry& a, const LocEntry& b)
                      { return a.displayPath < b.displayPath; });
            locList = std::move(entries);
            locListTotal = totalLoc;
            locListRoot = rootDisplay;
            commandRequestedModeSet = true;
            commandRequestedMode = LOC_LIST;
            commandRequestedPath.clear();
            commandRequestedReturnMode.reset();
            commandRequestedBrowseCursor = 0;
            commandRequestedBrowseOffset = 0;
            commandRequestedBrowseDirectory.clear();
        }

        statusMessage.clear();
        locMessage = "LOC " + std::to_string(totalLoc);
        needsFullRedraw = true;
        return;
    }

    if(!hasBuffer())
    {
        if(cmd == "q" || cmd == "q!" || cmd == "qa" || cmd == "qa!" ||
           cmd == "qall" || cmd == "qall!")
        {
            Terminal::clearScreen();
            exit(0);
        }
        if(cmd == "pwd")
        {
            fs::path cwd = EditorPathUtilities::currentWorkingDirectory();
            if(!cwd.empty())
                setStatusMessage(cwd.string());
            else
                setStatusMessage("Error getting current directory");
            return;
        }
        if(cmd == "cdr")
        {
            if(projectRoot.empty())
            {
                setStatusMessage("Project root not set");
                return;
            }

            std::string displayPath;
            std::string errorMessage;
            if(EditorPathUtilities::setWorkingDirectory(
                   projectRoot, displayPath, errorMessage))
                setStatusMessage(displayPath);
            else
                setStatusMessage("Cannot change to: " + projectRoot + " (" +
                                 errorMessage + ")");
            return;
        }
        if(cmd.rfind("cd ", 0) == 0 || cmd == "cd")
        {
            std::string path =
                (cmd.length() > 3) ? std::string(cmd.substr(3)) : "";
            if(path.empty())
                path = EditorPathUtilities::homeDirectory().string();

            std::string displayPath;
            std::string errorMessage;
            if(EditorPathUtilities::setWorkingDirectory(path, displayPath,
                                                        errorMessage))
                setStatusMessage(displayPath);
            else
                setStatusMessage("Cannot change to: " + path + " (" +
                                 errorMessage + ")");
            return;
        }
        if(cmd == "Sex" || cmd == "Sexplore" || cmd == "Vex" ||
           cmd == "Vexplore")
        {
            setStatusMessage("Split explorer not yet implemented");
            commandRequestedModeSet = true;
            commandRequestedMode = FILE_BROWSER;
            commandRequestedPath = ".";
            return;
        }
        if(cmd == "ls" || cmd == "buffers" || cmd == "bn" || cmd == "bnext" ||
           cmd == "bp" || cmd == "bprev" || cmd == "bprevious" || cmd == "bd" ||
           cmd == "bdelete")
        {
            setStatusMessage("No buffers");
            return;
        }
        if(cmd == "enew")
        {
            createNewBuffer();
            setStatusMessage("New buffer created");
            return;
        }
        if(cmd.rfind("e ", 0) == 0 || cmd.rfind("edit ", 0) == 0)
        {
            std::string path = (cmd.rfind("e ", 0) == 0)
                                   ? std::string(cmd.substr(2))
                                   : std::string(cmd.substr(5));

            if(path == ".")
            {
                commandRequestedModeSet = true;
                commandRequestedMode = FILE_BROWSER;
                commandRequestedPath = ".";
                return;
            }
            else
            {
                std::error_code ec;
                if(std::filesystem::is_directory(path, ec) && !ec)
                {
                    commandRequestedModeSet = true;
                    commandRequestedMode = FILE_BROWSER;
                    commandRequestedPath = path;
                    return;
                }
                openFile(path);
                setMode(NORMAL);
                return;
            }
        }
        if(cmd.rfind("tabnew", 0) == 0 || cmd.rfind("tabe ", 0) == 0)
        {
            std::string fname = "";
            if(cmd.rfind("tabe ", 0) == 0 && cmd.length() > 5)
            {
                fname = std::string(cmd.substr(5));
            }
            else if(cmd.rfind("tabnew ", 0) == 0 && cmd.length() > 7)
            {
                fname = std::string(cmd.substr(7));
            }

            if(!fname.empty())
            {
                openFile(fname);
            }
            else
            {
                createNewBuffer();
                setStatusMessage("New buffer created");
            }
            return;
        }

        setStatusMessage("No buffer");
        return;
    }

    auto saveAllBuffers = [&](bool forceExit) -> bool
    {
        int savedCount = 0;
        int skippedNoName = 0;
        int currentBuf = currentBufferIndex;

        for(size_t i = 0; i < buffers.size(); i++)
        {
            if(buffers[i]->dirty)
            {
                if(buffers[i]->filename.empty())
                {
                    skippedNoName++;
                    continue;
                }
                switchToBuffer(i);
                saveFile();
                savedCount++;
            }
        }

        switchToBuffer(currentBuf);

        if(!forceExit)
        {
            if(skippedNoName > 0)
            {
                setStatusMessage("Saved " + std::to_string(savedCount) +
                                 " buffer(s), " +
                                 std::to_string(skippedNoName) + " unnamed");
            }
            else
            {
                setStatusMessage("Saved " + std::to_string(savedCount) +
                                 " buffer(s)");
            }
        }

        return skippedNoName == 0;
    };

    // Buffer commands
    if(cmd == "bn" || cmd == "bnext")
    {
        nextBuffer();
    }
    else if(cmd == "bp" || cmd == "bprev" || cmd == "bprevious")
    {
        previousBuffer();
    }
    else if(cmd == "bd" || cmd == "bdelete")
    {
        closeCurrentBuffer();
    }
    else if(cmd == "bd!")
    {
        *dirty = false;
        closeCurrentBuffer();
    }
    else if(cmd == "ls" || cmd == "buffers")
    {
        listBuffers();
    }
    else if(cmd.rfind("b ", 0) == 0 || cmd.rfind("buffer ", 0) == 0)
    {
        std::string_view arg =
            (cmd.rfind("b ", 0) == 0) ? cmd.substr(2) : cmd.substr(7);
        arg = trim_view(arg);

        int bufNum = 0;
        if(parse_int(arg, bufNum))
        {
            bufNum -= 1;
            if(bufNum >= 0 && bufNum < (int)buffers.size())
            {
                switchToBuffer(bufNum);
            }
            else
            {
                setStatusMessage("Buffer " + std::string(arg) +
                                 " does not exist");
            }
        }
        else
        {
            std::string needle(arg);
            for(size_t i = 0; i < buffers.size(); i++)
            {
                if(buffers[i]->filename.find(needle) != std::string::npos)
                {
                    switchToBuffer(i);
                    return;
                }
            }
            setStatusMessage("No matching buffer for " + needle);
        }
    }
    else if(cmd == "enew")
    {
        createNewBuffer();
        setStatusMessage("New buffer created");
    }
    else if(cmd == "wall" || cmd == "wa")
    {
        saveAllBuffers(false);
    }
    else if(cmd == "wa!")
    {
        saveAllBuffers(false);
    }
    else if(cmd == "qall" || cmd == "qa")
    {
        bool hasUnsaved = false;
        for(const auto& buf : buffers)
        {
            if(buf->dirty)
            {
                hasUnsaved = true;
                break;
            }
        }

        if(hasUnsaved)
        {
            setStatusMessage(
                "Some buffers have unsaved changes (add ! to override)");
        }
        else
        {
            Terminal::clearScreen();
            exit(0);
        }
    }
    else if(cmd == "qall!" || cmd == "qa!")
    {
        Terminal::clearScreen();
        exit(0);
    }
    else if(cmd == "qw" || cmd == "wqall" || cmd == "wqa" || cmd == "xa")
    {
        if(!saveAllBuffers(false))
        {
            setStatusMessage("Some buffers have no name (use :qw! to force)");
            return;
        }
        Terminal::clearScreen();
        exit(0);
    }
    // File browser commands
    else if(cmd == "Sex" || cmd == "Sexplore" || cmd == "Vex" ||
            cmd == "Vexplore")
    {
        setStatusMessage("Split explorer not yet implemented");
        commandRequestedModeSet = true;
        commandRequestedMode = FILE_BROWSER;
        commandRequestedPath = ".";
        return;
    }
    // Standard commands
    else if(cmd == "w")
    {
        saveFile();
    }
    else if(cmd == "vs" || cmd == "vsplit")
    {
        enableSplit(true);
    }
    else if(cmd == "vh" || cmd == "hs" || cmd == "hsplit")
    {
        enableSplit(false);
    }
    else if(cmd == "q")
    {
        if(splitActive)
        {
            closeSplit();
            return;
        }
        bool anyDirty = false;
        for(const auto& buf : buffers)
        {
            if(buf->dirty)
            {
                anyDirty = true;
                break;
            }
        }
        if(anyDirty)
        {
            setStatusMessage("No write since last change (add ! to override)");
        }
        else
        {
            Terminal::clearScreen();
            exit(0);
        }
    }
    else if(cmd == "q!")
    {
        Terminal::clearScreen();
        exit(0);
    }
    else if(cmd == "qw!" || cmd == "wqall!" || cmd == "wqa!")
    {
        saveAllBuffers(true);
        Terminal::clearScreen();
        exit(0);
    }
    else if(cmd == "wq" || cmd == "x")
    {
        saveFile();
        if(buffers.size() > 1)
        {
            closeCurrentBuffer();
        }
        else
        {
            Terminal::clearScreen();
            exit(0);
        }
    }
    else if(cmd.rfind("w ", 0) == 0)
    {
        *filename = std::string(cmd.substr(2));
        saveFile();
    }
    else if(cmd.rfind("e ", 0) == 0 || cmd.rfind("edit ", 0) == 0)
    {
        std::string path = (cmd.rfind("e ", 0) == 0)
                               ? std::string(cmd.substr(2))
                               : std::string(cmd.substr(5));

        if(path == ".")
        {
            commandRequestedModeSet = true;
            commandRequestedMode = FILE_BROWSER;
            commandRequestedPath = ".";
            return;
        }
        else
        {
            std::error_code ec;
            if(std::filesystem::is_directory(path, ec) && !ec)
            {
                commandRequestedModeSet = true;
                commandRequestedMode = FILE_BROWSER;
                commandRequestedPath = path;
                return;
            }
            openFile(path);
            setMode(NORMAL);
        }
    }
    else if(cmd.rfind("tabnew", 0) == 0 || cmd.rfind("tabe ", 0) == 0)
    {
        std::string fname = "";
        if(cmd.rfind("tabe ", 0) == 0 && cmd.length() > 5)
        {
            fname = std::string(cmd.substr(5));
        }
        else if(cmd.rfind("tabnew ", 0) == 0 && cmd.length() > 7)
        {
            fname = std::string(cmd.substr(7));
        }

        if(!fname.empty())
        {
            openFile(fname);
        }
        else
        {
            createNewBuffer();
            setStatusMessage("New buffer created");
        }
    }
    else if(cmd == "tabn" || cmd == "tabnext")
    {
        nextBuffer();
    }
    else if(cmd == "tabp" || cmd == "tabprev")
    {
        previousBuffer();
    }
    else if(cmd == "pwd")
    {
        fs::path cwd = EditorPathUtilities::currentWorkingDirectory();
        if(!cwd.empty())
            setStatusMessage(cwd.string());
        else
            setStatusMessage("Error getting current directory");
    }
    else if(cmd == "cdr")
    {
        if(projectRoot.empty())
        {
            setStatusMessage("Project root not set");
            return;
        }

        std::string displayPath;
        std::string errorMessage;
        if(EditorPathUtilities::setWorkingDirectory(projectRoot, displayPath,
                                                    errorMessage))
            setStatusMessage(displayPath);
        else
            setStatusMessage("Cannot change to: " + projectRoot + " (" +
                             errorMessage + ")");
    }
    else if(cmd.rfind("cd ", 0) == 0 || cmd == "cd")
    {
        std::string path = (cmd.length() > 3) ? std::string(cmd.substr(3)) : "";
        if(path.empty())
            path = EditorPathUtilities::homeDirectory().string();

        std::string displayPath;
        std::string errorMessage;
        if(EditorPathUtilities::setWorkingDirectory(path, displayPath,
                                                    errorMessage))
            setStatusMessage(displayPath);
        else
            setStatusMessage("Cannot change to: " + path + " (" + errorMessage +
                             ")");
    }
    else
    {
        int line = 0;
        if(parse_int(cmd, line))
        {
            moveToLine(line - 1);
        }
        else
        {
            setStatusMessage("Not an editor command: " + std::string(cmd));
        }
    }
}

void Editor::refreshFileSearchCaches()
{
    grepProjectFiles.clear();
    grepFileIndexInitialized = false;

    if(modeStateMachine)
    {
        if(auto* fuzzy = modeStateMachine->getState<FuzzyFindMode>())
            fuzzy->refreshFileIndex(*this);
        if(auto* grep = modeStateMachine->getState<GrepSearchMode>())
            grep->refreshFileIndex(*this);
        if(auto* regex = modeStateMachine->getState<RegexSearchMode>())
            regex->refreshFileIndex(*this);
    }

    setStatusMessage("File search cache refreshed");
    needsFullRedraw = true;
}

void Editor::forceQuit()
{
    Terminal::restoreTerminal();
    std::exit(0);
}

std::string Editor::getAlternateFilePath()
{
    if(!currentBuffer || currentBuffer->filename.empty())
        return "";

    return findAlternateFile(currentBuffer->filename);
}

void Editor::createNewBuffer()
{
    bufferController->createNewBuffer();
}

void Editor::updateCurrentBufferPointers()
{
    bufferController->updateCurrentBufferPointers();
}

void Editor::clearCurrentBufferPointers()
{
    bufferController->clearCurrentBufferPointers();
}

bool Editor::hasBuffer() const
{
    return bufferController->hasBuffer();
}

void Editor::ensureBufferForMode(Mode mode)
{
    bufferController->ensureBufferForMode(mode);
}

void Editor::switchToBuffer(int index)
{
    bufferController->switchToBuffer(index);
}

void Editor::nextBuffer()
{
    bufferController->nextBuffer();
}

void Editor::previousBuffer()
{
    bufferController->previousBuffer();
}

void Editor::moveBufferLeft()
{
    bufferController->moveBufferLeft();
}

void Editor::moveBufferRight()
{
    bufferController->moveBufferRight();
}

void Editor::closeCurrentBuffer()
{
    bufferController->closeCurrentBuffer();
}

void Editor::listBuffers()
{
    bufferController->listBuffers();
}

int Editor::findBufferByFilename(const std::string& fname)
{
    return bufferController->findBufferByFilename(fname);
}

void Editor::saveBufferState()
{
    bufferController->saveBufferState();
}

void Editor::restoreBufferState()
{
    bufferController->restoreBufferState();
}

bool Editor::searchDefinitionInBuffer(Buffer* buf, const std::string& symbol,
                                      int& outY, int& outX)
{
    for(int y = 0; y < buf->lines.size(); ++y)
    {
        const std::string& line = buf->lines[y];
        if(CppNavigationUtilities::isLikelyDefinition(line, symbol))
        {
            size_t pos = line.find(symbol);
            if(pos != std::string::npos)
            {
                outY = y;
                outX = pos;
                return true;
            }
        }
    }
    return false;
}

void Editor::openSymbolPopupForCursor()
{
    closeSymbolPopup();
    if(!currentBuffer || !lines)
        return;

    std::string symbol = getSymbolUnderCursor();
    if(symbol.empty())
    {
        setStatusMessage("No symbol");
        needsFullRedraw = true;
        return;
    }

    int defY = -1;
    int defX = 0;
    std::string signature;

    bool memberCall = false;
    std::string memberObject;
    {
        const std::string& line = (*lines)[*cursorY];
        int x = *cursorX;
        if(x >= 0 && x < (int)line.size() &&
           CppNavigationUtilities::isIdent(line[x]))
        {
            int l = x;
            while(l > 0 && CppNavigationUtilities::isIdent(line[l - 1]))
                l--;
            int p = l - 1;
            while(p >= 0 && std::isspace((unsigned char)line[p]))
                --p;
            if(p >= 0 && line[p] == '.')
            {
                memberCall = true;
                int end = p - 1;
                while(end >= 0 && std::isspace((unsigned char)line[end]))
                    --end;
                int start = end;
                while(start >= 0 &&
                      CppNavigationUtilities::isIdent(line[start]))
                    --start;
                if(end >= 0)
                    memberObject =
                        line.substr((size_t)start + 1, (size_t)(end - start));
            }
            else if(p >= 1 && line[p] == '>' && line[p - 1] == '-')
            {
                memberCall = true;
                int end = p - 2;
                while(end >= 0 && std::isspace((unsigned char)line[end]))
                    --end;
                int start = end;
                while(start >= 0 &&
                      CppNavigationUtilities::isIdent(line[start]))
                    --start;
                if(end >= 0)
                    memberObject =
                        line.substr((size_t)start + 1, (size_t)(end - start));
            }
        }
    }

    auto resolve_return_type_for_function =
        [&](const std::string& funcName, const std::string& candidate,
            const std::vector<std::string>& currentLines) -> std::string
    {
        if(funcName.empty())
            return "";
        int y = -1;
        int x = 0;
        if(SymbolPopupUtilities::findDeclarationInLines(currentLines, funcName,
                                                        y, x))
        {
            std::string type = SymbolPopupUtilities::extractTypeBeforeName(
                currentLines[y], funcName);
            if(!type.empty())
                return type;
        }

        std::string alternate = findAlternateFile(currentBuffer->filename);
        if(!alternate.empty())
        {
            std::vector<std::string> altLines;
            if(SymbolPopupUtilities::loadFileLines(alternate, altLines))
            {
                if(SymbolPopupUtilities::findDeclarationInLines(altLines,
                                                                funcName, y, x))
                {
                    std::string type =
                        SymbolPopupUtilities::extractTypeBeforeName(altLines[y],
                                                                    funcName);
                    if(!type.empty())
                        return type;
                }
            }
        }

        if(candidate.rfind("std::", 0) == 0)
        {
            std::string base =
                SymbolPopupUtilities::lastQualifier(candidate.substr(5));
            std::string header = stdlib_goto::headerForSymbol(base);
            if(header.empty())
                header = stdlib_goto::headerForSymbol(funcName);
            if(!header.empty())
            {
                std::string headerPath =
                    CppNavigationUtilities::resolveSystemInclude(header);
                if(!headerPath.empty())
                {
                    std::vector<std::string> headerLines;
                    if(SymbolPopupUtilities::loadFileLines(headerPath,
                                                           headerLines))
                    {
                        if(SymbolPopupUtilities::findDeclarationInLines(
                               headerLines, funcName, y, x))
                        {
                            std::string type =
                                SymbolPopupUtilities::extractTypeBeforeName(
                                    headerLines[y], funcName);
                            if(!type.empty())
                                return type;
                        }
                    }
                }
            }
        }

        return "";
    };

    auto infer_auto_type_from_decl =
        [&](const std::string& declLine, const std::string& varName,
            const std::vector<std::string>& currentLines) -> std::string
    {
        size_t eq = declLine.find('=');
        if(eq == std::string::npos)
            return "";
        std::string_view rhs = std::string_view(declLine).substr(eq + 1);
        std::string candidate =
            SymbolPopupUtilities::extractInitializerTypeCandidate(rhs);
        if(candidate.empty())
            return "";
        std::string funcName = SymbolPopupUtilities::lastQualifier(candidate);
        std::string type =
            resolve_return_type_for_function(funcName, candidate, currentLines);
        if(!type.empty())
            return type;
        return candidate;
    };

    if(memberCall && !memberObject.empty())
    {
        int objY = -1;
        int objX = 0;
        if(CppNavigationUtilities::searchLocalDefinition(
               *lines, memberObject, *cursorY, *cursorX, objY, objX) ||
           CppNavigationUtilities::searchMemberDefinition(*lines, memberObject,
                                                          objY, objX))
        {
            std::string declLine = (*lines)[objY];
            std::string typeToken = SymbolPopupUtilities::extractTypeBeforeName(
                declLine, memberObject);
            if(typeToken == "auto")
            {
                typeToken =
                    infer_auto_type_from_decl(declLine, memberObject, *lines);
            }
            if(!typeToken.empty())
            {
                std::string base =
                    SymbolPopupUtilities::lastQualifier(typeToken);
                std::string header = stdlib_goto::headerForSymbol(base);
                if(!header.empty())
                {
                    std::string headerPath =
                        CppNavigationUtilities::resolveSystemInclude(header);
                    if(!headerPath.empty())
                    {
                        std::vector<std::string> headerLines;
                        if(SymbolPopupUtilities::loadFileLines(headerPath,
                                                               headerLines))
                        {
                            if(SymbolPopupUtilities::findDeclarationInLines(
                                   headerLines, symbol, defY, defX))
                            {
                                signature =
                                    SymbolPopupUtilities::collectSignatureLine(
                                        headerLines, defY, 3);
                            }
                        }
                    }
                }
            }
        }
    }

    if(signature.empty() && symbolPrefix.rfind("std::", 0) == 0)
    {
        std::string base =
            SymbolPopupUtilities::lastQualifier(symbolPrefix.substr(5));
        std::string header = stdlib_goto::headerForSymbol(base);
        if(header.empty())
            header = stdlib_goto::headerForSymbol(symbol);
        if(!header.empty())
        {
            std::string headerPath =
                CppNavigationUtilities::resolveSystemInclude(header);
            if(!headerPath.empty())
            {
                std::vector<std::string> headerLines;
                if(SymbolPopupUtilities::loadFileLines(headerPath, headerLines))
                {
                    if(SymbolPopupUtilities::findDeclarationInLines(
                           headerLines, symbol, defY, defX))
                    {
                        signature = SymbolPopupUtilities::collectSignatureLine(
                            headerLines, defY, 3);
                    }
                }
            }
        }
    }

    if(signature.empty())
    {
        std::string alternate = findAlternateFile(currentBuffer->filename);
        if(!alternate.empty())
        {
            std::vector<std::string> altLines;
            if(SymbolPopupUtilities::loadFileLines(alternate, altLines))
            {
                if(SymbolPopupUtilities::findDeclarationInLines(
                       altLines, symbol, defY, defX))
                    signature = SymbolPopupUtilities::collectSignatureLine(
                        altLines, defY, 3);
            }
        }
    }

    if(signature.empty())
    {
        if(SymbolPopupUtilities::findDeclarationInLines(*lines, symbol, defY,
                                                        defX))
        {
            signature =
                SymbolPopupUtilities::collectSignatureLine(*lines, defY, 3);
        }
        else if(CppNavigationUtilities::searchMemberDefinition(*lines, symbol,
                                                               defY, defX))
        {
            signature =
                SymbolPopupUtilities::collectSignatureLine(*lines, defY, 1);
        }
        else if(CppNavigationUtilities::searchLocalDefinition(
                    *lines, symbol, *cursorY, *cursorX, defY, defX))
        {
            signature =
                SymbolPopupUtilities::collectSignatureLine(*lines, defY, 1);
        }
    }

    if(signature.empty())
    {
        std::string qualified =
            symbolPrefix.empty() ? symbol : symbolPrefix + symbol;
        signature = qualified + "()";
    }

    symbolPopupText = std::move(signature);
    symbolPopupActive = true;
    symbolPopupCursorX = *cursorX;
    symbolPopupCursorY = *cursorY;
    needsFullRedraw = true;
}

void Editor::closeSymbolPopup()
{
    symbolPopupActive = false;
    symbolPopupCursorX = -1;
    symbolPopupCursorY = -1;
    symbolPopupText.clear();
}

void Editor::run()
{
    //    setStatusMessage("Welcome to uVim!");
    draw();

    while(true)
    {
        handleResize();
        int c = Terminal::readKeyTimeout(50);
        if(c < 0)
        {
            // No key pressed, check if file has changed externally
            checkFileChanges();
            if(needsFullRedraw)
                draw();
            continue;
        }
        // Apply a burst of ready keys first, then render once.
        modeController->handleKeypress(c);
        while(true)
        {
            int next = Terminal::readKeyTimeout(0);
            if(next < 0)
                break;
            modeController->handleKeypress(next);
        }
        draw();
    }
}

void Editor::insertTab()
{
    editingController->insertTab();
}

void Editor::toggleCase()
{
    editingController->toggleCase();
}

void Editor::joinLines()
{
    editingController->joinLines();
}

void Editor::insertLineAbove()
{
    editingController->insertLineAbove();
}

void Editor::insertLineBelow()
{
    editingController->insertLineBelow();
}

void Editor::deleteCurrentLine()
{
    editingController->deleteCurrentLine();
}

void Editor::deleteToLineStart()
{
    editingController->deleteToLineStart();
}

void Editor::deleteCharAtCursor()
{
    editingController->deleteCharAtCursor();
}

void Editor::deleteCharBeforeCursor()
{
    editingController->deleteCharBeforeCursor();
}

void Editor::deleteWordBackward()
{
    editingController->deleteWordBackward();
}

void Editor::deleteWord()
{
    editingController->deleteWord();
}

void Editor::yankWord()
{
    editingController->yankWord();
}

void Editor::handleBackspace()
{
    editingController->handleBackspace();
}

void Editor::replaceCharAtCursor(char c)
{
    editingController->replaceCharAtCursor(c);
}

void Editor::beginChangeRecording(int count)
{
    editingController->beginChangeRecording(count);
}

void Editor::recordChangeKey(int key)
{
    editingController->recordChangeKey(key);
}

void Editor::deferChangeRecordingCommit()
{
    editingController->deferChangeRecordingCommit();
}

void Editor::commitChangeRecording()
{
    editingController->commitChangeRecording();
}

void Editor::cancelChangeRecording()
{
    editingController->cancelChangeRecording();
}

void Editor::finishChangeRecordingIfDeferred()
{
    editingController->finishChangeRecordingIfDeferred();
}

bool Editor::isRecordingChange() const
{
    return editingController->isRecordingChange();
}

bool Editor::isReplayingChange() const
{
    return editingController->isReplayingChange();
}

int Editor::readKeyRecorded()
{
    return editingController->readKeyRecorded();
}

void Editor::repeatLastChange(int times)
{
    editingController->repeatLastChange(times);
}

void Editor::insertUtf8Char(int c)
{
    editingController->insertUtf8Char(c);
}

void Editor::indentCurrentLine()
{
    editingController->indentCurrentLine();
}

void Editor::dedentCurrentLine()
{
    editingController->dedentCurrentLine();
}

void Editor::handleLinewiseOperator(char op, int count)
{
    editingController->handleLinewiseOperator(op, count);
}

// ============================================================================
// Extended Visual Mode Commands
// ============================================================================

void Editor::setVisualRange()
{
    visualController->setVisualRange();
}

void Editor::swapVisualEnds()
{
    visualController->swapVisualEnds();
}

void Editor::swapVisualBlockCorner()
{
    visualController->swapVisualBlockCorner();
}

void Editor::prepareBlockInsert(bool atEnd)
{
    visualController->prepareBlockInsert(atEnd);
}

void Editor::indentSelection()
{
    visualController->indentSelection();
}

void Editor::dedentSelection()
{
    visualController->dedentSelection();
}

void Editor::autoIndentSelection()
{
    visualController->autoIndentSelection();
}

void Editor::lowercaseSelection()
{
    visualController->lowercaseSelection();
}

void Editor::uppercaseSelection()
{
    visualController->uppercaseSelection();
}

void Editor::toggleCaseSelection()
{
    visualController->toggleCaseSelection();
}

void Editor::yankLineSelection()
{
    visualController->yankLineSelection();
}

void Editor::deleteLineSelection()
{
    visualController->deleteLineSelection();
}

void Editor::indentLineSelection()
{
    visualController->indentLineSelection();
}

void Editor::dedentLineSelection()
{
    visualController->dedentLineSelection();
}

void Editor::autoIndentLineSelection()
{
    visualController->autoIndentLineSelection();
}

void Editor::moveLeft(int count)
{
    cursorController->moveLeft(count);
}

void Editor::moveRight(int count)
{
    cursorController->moveRight(count);
}

void Editor::moveUp(int count)
{
    cursorController->moveUp(count);
}

void Editor::moveDown(int count)
{
    cursorController->moveDown(count);
}

void Editor::moveWordForward()
{
    cursorController->moveWordForward();
}

void Editor::moveWordBackward()
{
    cursorController->moveWordBackward();
}

void Editor::moveToEndOfWord()
{
    cursorController->moveToEndOfWord();
}

void Editor::moveToLineStart()
{
    cursorController->moveToLineStart();
}

void Editor::moveToLineEnd()
{
    cursorController->moveToLineEnd();
}

void Editor::moveToFirstLine()
{
    cursorController->moveToFirstLine();
}

void Editor::moveToLastLine()
{
    cursorController->moveToLastLine();
}

void Editor::moveToLine(int line)
{
    cursorController->moveToLine(line);
}

void Editor::pushJumpLocation()
{
    cursorController->pushJumpLocation();
}

void Editor::jumpForward()
{
    cursorController->jumpForward();
}

void Editor::jumpBack()
{
    cursorController->jumpBack();
}

void Editor::restoreJumpLocation(const JumpLocation& loc)
{
    cursorController->restoreJumpLocation(loc);
}

void Editor::scrollHalfPageDown(bool visual)
{
    cursorController->scrollHalfPageDown(visual);
}

void Editor::scrollHalfPageUp(bool visual)
{
    cursorController->scrollHalfPageUp(visual);
}

void Editor::moveToMatchingBracket()
{
    cursorController->moveToMatchingBracket();
}

void Editor::findCharForward(char c)
{
    cursorController->findCharForward(c);
}

void Editor::findCharBackward(char c)
{
    cursorController->findCharBackward(c);
}

void Editor::moveToFirstNonBlank()
{
    cursorController->moveToFirstNonBlank();
}

void Editor::moveParagraphForward()
{
    cursorController->moveParagraphForward();
}

void Editor::moveParagraphBackward()
{
    cursorController->moveParagraphBackward();
}

void Editor::moveWordForwardBig()
{
    cursorController->moveWordForwardBig();
}

void Editor::moveWordBackwardBig()
{
    cursorController->moveWordBackwardBig();
}

void Editor::moveToEndOfWordBig()
{
    cursorController->moveToEndOfWordBig();
}

void Editor::findCharForwardBefore(char c)
{
    cursorController->findCharForwardBefore(c);
}

void Editor::findCharBackwardAfter(char c)
{
    cursorController->findCharBackwardAfter(c);
}

void Editor::scrollToTop()
{
    cursorController->scrollToTop();
}

void Editor::scrollToBottom()
{
    cursorController->scrollToBottom();
}

void Editor::scrollPageUp()
{
    cursorController->scrollPageUp();
}

void Editor::scrollPageDown()
{
    cursorController->scrollPageDown();
}

void Editor::moveToScreenTop()
{
    cursorController->moveToScreenTop();
}

void Editor::moveToScreenMiddle()
{
    cursorController->moveToScreenMiddle();
}

void Editor::moveToScreenBottom()
{
    cursorController->moveToScreenBottom();
}

void Editor::adjustViewport()
{
    cursorController->adjustViewport();
}

void Editor::adjustViewportForPane(PaneState& pane, int rows, int cols)
{
    cursorController->adjustViewportForPane(pane, rows, cols);
}

void Editor::centerScreen()
{
    cursorController->centerScreen();
}

void Editor::setMark(char mark)
{
    cursorController->setMark(mark);
}

void Editor::jumpToMark(char mark)
{
    cursorController->jumpToMark(mark);
}

// ============================================================================
// Misc Utilities
// ============================================================================

void Editor::goToFile()
{
    fileController->goToFile();
}

void Editor::showFileInfo()
{
    fileController->showFileInfo();
}

void Editor::forceFullRedraw()
{
    drawingController->forceFullRedraw();
}

void Editor::executeOneNormalCommand(int key)
{
    switch(key)
    {
    case keyCode(typed::TypedKey::KEY_W):
        moveWordForward();
        break;
    case keyCode(typed::TypedKey::KEY_B):
        moveWordBackward();
        break;
    case keyCode(typed::TypedKey::KEY_E):
        moveToEndOfWord();
        break;
    case keyCode(typed::TypedKey::KEY_0):
        moveToLineStart();
        break;
    case keyCode(command::CommandKey::KEY_DOLLAR):
        moveToLineEnd();
        break;
    case keyCode(typed::TypedKey::KEY_H):
        moveLeft();
        break;
    case keyCode(typed::TypedKey::KEY_L):
        moveRight();
        break;
    case keyCode(typed::TypedKey::KEY_J):
        moveDown();
        adjustViewport();
        break;
    case keyCode(typed::TypedKey::KEY_K):
        moveUp();
        adjustViewport();
        break;
    default:
        break;
    }
}

// ============================================================================
// Command History
// ============================================================================

std::optional<std::string> Editor::commandHistoryUp()
{
    return commandController->commandHistoryUp();
}

std::optional<std::string> Editor::commandHistoryDown()
{
    return commandController->commandHistoryDown();
}

void Editor::startCommandPopup()
{
    commandController->startCommandPopup();
}

void Editor::cancelCommandPopup()
{
    commandController->cancelCommandPopup();
}

void Editor::updateCommandPopup(std::string_view query)
{
    commandController->updateCommandPopup(query);
}

void Editor::moveCommandPopupCursor(int delta)
{
    commandController->moveCommandPopupCursor(delta);
}

bool Editor::isCommandPopupActive() const
{
    return commandController->isCommandPopupActive();
}

std::optional<std::string> Editor::commandPopupSelection() const
{
    return commandController->commandPopupSelection();
}

void Editor::startCommandHistorySearch(std::string_view seed)
{
    commandController->startCommandHistorySearch(seed);
}

std::string Editor::cancelCommandHistorySearch()
{
    return commandController->cancelCommandHistorySearch();
}

std::string Editor::acceptCommandHistorySearch()
{
    return commandController->acceptCommandHistorySearch();
}

void Editor::updateCommandHistorySearchQuery(std::string_view query)
{
    commandController->updateCommandHistorySearchQuery(query);
}

void Editor::moveCommandHistorySearchCursor(int delta)
{
    commandController->moveCommandHistorySearchCursor(delta);
}

bool Editor::isCommandHistorySearchActive() const
{
    return commandController->isCommandHistorySearchActive();
}

const std::string& Editor::commandHistorySearchQuery() const
{
    return commandController->commandHistorySearchQuery();
}

void Editor::drawCommandPopup(std::string& output) const
{
    commandController->drawCommandPopup(output);
}

void Editor::drawCommandHistoryPopup(std::string& output) const
{
    commandController->drawCommandHistoryPopup(output);
}

std::vector<std::string> Editor::getCommandCompletions(std::string_view prefix)
{
    return commandController->getCommandCompletions(prefix);
}

std::vector<std::string> Editor::getCommandCompletions(std::string_view prefix,
                                                       Mode mode)
{
    return commandController->getCommandCompletions(prefix, mode);
}

std::vector<std::string> Editor::getHelpCompletions(std::string_view prefix)
{
    return commandController->getHelpCompletions(prefix);
}

std::vector<std::string> Editor::getSetCompletions(std::string_view prefix)
{
    return commandController->getSetCompletions(prefix);
}

std::vector<std::string> Editor::getPathCompletions(std::string_view path)
{
    return commandController->getPathCompletions(path);
}

std::vector<std::string>
Editor::getPathCompletionsRecursive(std::string_view path)
{
    return commandController->getPathCompletionsRecursive(path);
}

std::vector<std::string> Editor::getLocPathCompletions(std::string_view path)
{
    return commandController->getLocPathCompletions(path);
}

void Editor::deleteFilePrompt()
{
    fileController->deleteFilePrompt();
}

void Editor::renameFilePrompt()
{
    fileController->renameFilePrompt();
}

void Editor::createNewFilePrompt()
{
    fileController->createNewFilePrompt();
}

void Editor::createNewDirectoryPrompt()
{
    fileController->createNewDirectoryPrompt();
}

// ============================================================================
// Completion Helpers (for insert mode)
// ============================================================================

bool Editor::shouldTriggerCompletion()
{
    // Check if we should auto-trigger completion
    // Typically after typing an identifier character
    if(*cursorY >= (int)lines->size())
        return false;
    const std::string& line = (*lines)[*cursorY];
    if(*cursorX == 0)
        return false;

    char prevChar = line[*cursorX - 1];
    return text_utils::isIdent(prevChar) || prevChar == '-' || prevChar == '.';
}

void Editor::triggerCompletion()
{
    requestCompletion();
}

void Editor::nextCompletion()
{
    completionNext();
}

void Editor::previousCompletion()
{
    completionPrev();
}

#ifdef UVIM_ENABLE_CLANGD_LSP
static int utf16ToUtf8ByteOffset(const std::string& line, int utf16Offset)
{
    if(utf16Offset <= 0)
        return 0;

    int u16 = 0;
    int i = 0;
    while(i < (int)line.size() && u16 < utf16Offset)
    {
        unsigned char c = (unsigned char)line[i];
        int codepoint = 0;
        int len = 1;

        if(c < 0x80)
        {
            codepoint = c;
            len = 1;
        }
        else if((c & 0xE0) == 0xC0 && i + 1 < (int)line.size())
        {
            codepoint = ((c & 0x1F) << 6) | ((unsigned char)line[i + 1] & 0x3F);
            len = 2;
        }
        else if((c & 0xF0) == 0xE0 && i + 2 < (int)line.size())
        {
            codepoint = ((c & 0x0F) << 12) |
                        (((unsigned char)line[i + 1] & 0x3F) << 6) |
                        ((unsigned char)line[i + 2] & 0x3F);
            len = 3;
        }
        else if((c & 0xF8) == 0xF0 && i + 3 < (int)line.size())
        {
            codepoint = ((c & 0x07) << 18) |
                        (((unsigned char)line[i + 1] & 0x3F) << 12) |
                        (((unsigned char)line[i + 2] & 0x3F) << 6) |
                        ((unsigned char)line[i + 3] & 0x3F);
            len = 4;
        }

        int u16len = (codepoint <= 0xFFFF) ? 1 : 2;
        if(u16 + u16len > utf16Offset)
            break;

        u16 += u16len;
        i += len;
    }
    return i;
}
#endif

// ============================================================================
// Compatibility Aliases
// ============================================================================

void Editor::deleteToEndOfLine()
{
    deleteToLineEnd();
}

void Editor::switchToAlternateFile()
{
    jumpToAlternateFile();
}
