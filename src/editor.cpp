#include "editor.h"
#include "ascii.h"
#include "constants.h"
#include "cpp_navigation_utilities.h"
#include "editor_buffer_controller.h"
#include "editor_command_controller.h"
#include "editor_cursor_controller.h"
#include "editor_definition_controller.h"
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
using namespace editor::statemachine;
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
    definitionController = std::make_unique<EditorDefinitionController>(*this);
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
    definitionController = std::make_unique<EditorDefinitionController>(*this);
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

std::optional<FileType> Editor::getFileType() const
{
    if(isFileType<FileType::Cpp>())
    {
        return FileType::Cpp;
    }
    else if(isFileType<FileType::Mla>())
    {
        return FileType::Mla;
    }
    else if(isFileType<FileType::Asm>())
    {
        return FileType::Asm;
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
    else if(isFileType<FileType::Html>())
    {
        return FileType::Html;
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

std::optional<FileType> Editor::getFormatterFileType() const
{
    return getFileType();
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
    case COLOR_PICKER:
        modeStateMachine->transitionTo(ColorPickerMode{});
        break;
    case COLOR_SELECTOR:
        modeStateMachine->transitionTo(ColorSelectorMode{});
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
    case COLOR_PICKER:
        return "COLOR";
    case COLOR_SELECTOR:
        return "RGB";
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
                const std::vector<std::string>& a) { enableCssLsp(on, p, a); });
        ensure_lsp_for_file_type<FileType::Json>(
            *this, isJsonLspEnabled(), jsonLspPath, jsonLspArgs,
            [&](bool on, const std::string& p,
                const std::vector<std::string>& a)
            { enableJsonLsp(on, p, a); });
        ensure_lsp_for_file_type<FileType::JavaScript, FileType::TypeScript>(
            *this, isTsLspEnabled(), tsLspPath, tsLspArgs,
            [&](bool on, const std::string& p,
                const std::vector<std::string>& a) { enableTsLsp(on, p, a); });
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

void Editor::openFileBrowser(std::string_view path, bool focusCurrentFile)
{
    std::string prev;
    if(currentMode != FILE_BROWSER && currentBuffer != nullptr && filename)
    {
        prev = *filename;
    }

    if(modeStateMachine)
    {
        modeStateMachine->transitionTo(
            FileBrowserMode{std::string(path), prev, focusCurrentFile});
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

void Editor::refreshScreen()
{
    drawingController->refreshScreen();
}

void Editor::updateCursorPosition(bool flushNow)
{
    drawingController->updateCursorPosition(flushNow);
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

void Editor::toggleGitBlame(bool includeDateTime)
{
    gitController->toggleGitBlame(includeDateTime);
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

void Editor::openGitLogModeForBlameLine()
{
    gitController->openGitLogModeForBlameLine();
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

void Editor::commentBlock(int startY, int endY)
{
    indentController->commentBlock(startY, endY);
}

void Editor::commentBlockRange(int startY, int startX, int endY, int endX)
{
    indentController->commentBlockRange(startY, startX, endY, endX);
}

bool Editor::insertTodoLineComment(int row)
{
    return indentController->insertTodoLineComment(row);
}

bool Editor::insertTodoBlockComment(int row)
{
    return indentController->insertTodoBlockComment(row);
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
    const bool signalPending = g_pending_resize != 0;
    g_pending_resize = 0;

    int rows = 0;
    int cols = 0;
    Terminal::getWindowSize(rows, cols);
    const int newScreenRows = std::max(1, rows - 2);
    const int newScreenCols = std::max(1, cols);

    if(!signalPending && newScreenRows == screenRows &&
       newScreenCols == screenCols)
        return;

    screenRows = newScreenRows;
    screenCols = newScreenCols;
    needsFullRedraw = true;

    std::string output;
    output += Terminal::ESC_RESET_SCROLL_REGION;
    output += Terminal::ESC_CURSOR_HOME;
    output += Terminal::ESC_CLEAR_SCREEN;
    Terminal::write(output);
    Terminal::flush();
#endif
}

#ifdef UVIM_TESTING
int Editor::testCountLocForFile(const std::string& filepath)
{
    auto rules = editor::helper::locCommentRulesForPath(filepath);
    return editor::helper::locCountInFile(filepath, rules);
}
#endif

void Editor::executeCommand(std::string_view cmd)
{
    commandController->executeCommand(cmd);
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
            if(text_utils::is_found(pos))
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
        if(text_utils::is_not_found(eq))
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
    symbolPopupModal = false;
    symbolPopupCursorX = *cursorX;
    symbolPopupCursorY = *cursorY;
    needsFullRedraw = true;
}

void Editor::closeSymbolPopup()
{
    symbolPopupActive = false;
    symbolPopupModal = false;
    symbolPopupCursorX = -1;
    symbolPopupCursorY = -1;
    symbolPopupScroll = 0;
    symbolPopupText.clear();
    needsFullRedraw = true;
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
