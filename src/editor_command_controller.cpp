#include "editor_command_controller.h"
#include "editor.h"
#include "editor_utils.h"
#ifndef UVIM_MINIMAL
#include "widgets/command_history_popup.h"
#include "widgets/command_popup.h"
#endif

EditorCommandController::EditorCommandController(Editor& editor)
    : editor(editor)
{
}

std::optional<std::string> EditorCommandController::commandHistoryUp()
{
    return editor.commandHistoryUpImpl();
}

std::optional<std::string> EditorCommandController::commandHistoryDown()
{
    return editor.commandHistoryDownImpl();
}

void EditorCommandController::startCommandPopup()
{
    editor.startCommandPopupImpl();
}

void EditorCommandController::cancelCommandPopup()
{
    editor.cancelCommandPopupImpl();
}

void EditorCommandController::updateCommandPopup(std::string_view query)
{
    editor.updateCommandPopupImpl(query);
}

void EditorCommandController::moveCommandPopupCursor(int delta)
{
    editor.moveCommandPopupCursorImpl(delta);
}

bool EditorCommandController::isCommandPopupActive() const
{
    return editor.isCommandPopupActiveImpl();
}

std::optional<std::string>
EditorCommandController::commandPopupSelection() const
{
    return editor.commandPopupSelectionImpl();
}

void EditorCommandController::startCommandHistorySearch(std::string_view seed)
{
    editor.startCommandHistorySearchImpl(seed);
}

std::string EditorCommandController::cancelCommandHistorySearch()
{
    return editor.cancelCommandHistorySearchImpl();
}

std::string EditorCommandController::acceptCommandHistorySearch()
{
    return editor.acceptCommandHistorySearchImpl();
}

void EditorCommandController::updateCommandHistorySearchQuery(
    std::string_view query)
{
    editor.updateCommandHistorySearchQueryImpl(query);
}

void EditorCommandController::moveCommandHistorySearchCursor(int delta)
{
    editor.moveCommandHistorySearchCursorImpl(delta);
}

bool EditorCommandController::isCommandHistorySearchActive() const
{
    return editor.isCommandHistorySearchActiveImpl();
}

const std::string& EditorCommandController::commandHistorySearchQuery() const
{
    return editor.commandHistorySearchQueryImpl();
}

void EditorCommandController::drawCommandPopup(std::string& output) const
{
    editor.drawCommandPopupImpl(output);
}

void EditorCommandController::drawCommandHistoryPopup(std::string& output) const
{
    editor.drawCommandHistoryPopupImpl(output);
}

std::vector<std::string>
EditorCommandController::getCommandCompletions(std::string_view prefix)
{
    return editor.getCommandCompletionsImpl(prefix);
}

std::vector<std::string>
EditorCommandController::getCommandCompletions(std::string_view prefix,
                                               Mode mode)
{
    return editor.getCommandCompletionsImpl(prefix, mode);
}

std::vector<std::string>
EditorCommandController::getHelpCompletions(std::string_view prefix)
{
    return editor.getHelpCompletionsImpl(prefix);
}

std::vector<std::string>
EditorCommandController::getSetCompletions(std::string_view prefix)
{
    return editor.getSetCompletionsImpl(prefix);
}

std::vector<std::string>
EditorCommandController::getPathCompletions(std::string_view path)
{
    return editor.getPathCompletionsImpl(path);
}

std::vector<std::string>
EditorCommandController::getPathCompletionsRecursive(std::string_view path)
{
    return editor.getPathCompletionsRecursiveImpl(path);
}

std::vector<std::string>
EditorCommandController::getLocPathCompletions(std::string_view path)
{
    return editor.getLocPathCompletionsImpl(path);
}

std::optional<std::string> Editor::commandHistoryUpImpl()
{
    if(commandHistory.empty())
        return std::nullopt;
    if(commandHistoryIndex < 0)
    {
        commandHistoryIndex = commandHistory.size() - 1;
    }
    else if(commandHistoryIndex > 0)
    {
        commandHistoryIndex--;
    }
    commandInput = commandHistory[commandHistoryIndex];
    return commandInput;
}

std::optional<std::string> Editor::commandHistoryDownImpl()
{
    if(commandHistory.empty() || commandHistoryIndex < 0)
        return std::nullopt;
    if(commandHistoryIndex < (int)commandHistory.size() - 1)
    {
        commandHistoryIndex++;
        commandInput = commandHistory[commandHistoryIndex];
        return commandInput;
    }

    commandHistoryIndex = -1;
    commandInput.clear();
    return commandInput;
}

void Editor::startCommandPopupImpl()
{
#ifdef UVIM_MINIMAL
    commandPopupActive = false;
    commandPopupQuery.clear();
    commandPopupFiltered.clear();
    commandPopupCursor = 0;
    commandPopupOffset = 0;
    return;
#else
    commandPopupActive = true;
    commandPopupQuery.clear();
    commandPopupCursor = 0;
    commandPopupOffset = 0;
    commandPopupAll = getCommandCompletions("");
    updateCommandPopup("");
#endif
}

void Editor::cancelCommandPopupImpl()
{
    commandPopupActive = false;
    commandPopupQuery.clear();
    commandPopupFiltered.clear();
    commandPopupCursor = 0;
    commandPopupOffset = 0;
    needsFullRedraw = true;
}

void Editor::updateCommandPopupImpl(std::string_view query)
{
    if(!commandPopupActive)
        return;

    auto isLineJumpQuery = [](std::string_view q) -> bool
    {
        if(q.empty())
            return false;
        size_t i = 0;
        while(i < q.size() && (q[i] == ' ' || q[i] == '\t'))
            ++i;
        if(i >= q.size())
            return false;
        if(q[i] == '+' || q[i] == '-')
            ++i;
        size_t digitsStart = i;
        while(i < q.size() && q[i] >= '0' && q[i] <= '9')
            ++i;
        return i > digitsStart;
    };

    std::string_view effectiveQuery = query;
    if(effectiveQuery.empty() && !commandBuffer.empty() &&
       commandBuffer.front() == ':')
    {
        effectiveQuery = std::string_view(commandBuffer).substr(1);
    }

    if(isLineJumpQuery(effectiveQuery))
    {
        cancelCommandPopup();
        return;
    }

    commandPopupQuery = std::string(effectiveQuery);
    commandPopupFiltered.clear();
    commandPopupCursor = 0;
    commandPopupOffset = 0;

    bool isSetQuery = commandPopupQuery.rfind("set", 0) == 0;
    bool isHelpQuery = commandPopupQuery == "help" ||
                       commandPopupQuery == "h" ||
                       commandPopupQuery.rfind("help ", 0) == 0 ||
                       commandPopupQuery.rfind("h ", 0) == 0;
    if(isSetQuery)
    {
        commandPopupAll = getSetCompletions("");
    }
    else if(isHelpQuery)
    {
        std::string cmd = (commandPopupQuery.rfind("h", 0) == 0 &&
                           commandPopupQuery.rfind("help", 0) != 0)
                              ? "h"
                              : "help";
        std::string topicPrefix;
        if(commandPopupQuery.size() > cmd.size() &&
           commandPopupQuery[cmd.size()] == ' ')
        {
            topicPrefix = commandPopupQuery.substr(cmd.size() + 1);
        }
        auto topics = getHelpCompletions(topicPrefix);
        commandPopupAll.clear();
        for(const auto& topic : topics)
            commandPopupAll.push_back(cmd + " " + topic);
    }
    else
    {
        commandPopupAll = getCommandCompletions("");
    }

    if(commandPopupQuery.empty())
    {
        for(int i = 0; i < (int)commandPopupAll.size(); ++i)
            commandPopupFiltered.push_back(i);
        needsFullRedraw = true;
        return;
    }

    if(isHelpQuery)
    {
        std::string prefix = commandPopupQuery;
        for(int i = 0; i < (int)commandPopupAll.size(); ++i)
        {
            if(commandPopupAll[i].rfind(prefix, 0) == 0)
                commandPopupFiltered.push_back(i);
        }
        needsFullRedraw = true;
        return;
    }

    std::vector<std::pair<int, int>> scored;
    std::vector<int> positions;
    scored.reserve(commandPopupAll.size());

    for(int i = 0; i < (int)commandPopupAll.size(); ++i)
    {
        int score = editor::helper::fuzzyScoreWithPositions(
            commandPopupQuery, commandPopupAll[i], positions);
        if(score >= 0)
            scored.emplace_back(i, score);
    }

    std::stable_sort(
        scored.begin(), scored.end(),
        [](const std::pair<int, int>& left, const std::pair<int, int>& right)
        {
            if(left.second != right.second)
                return left.second > right.second;
            return left.first < right.first;
        });

    for(const auto& entry : scored)
        commandPopupFiltered.push_back(entry.first);

    needsFullRedraw = true;
}

void Editor::moveCommandPopupCursorImpl(int delta)
{
    if(!commandPopupActive || commandPopupFiltered.empty())
        return;

    int next = commandPopupCursor + delta;
    if(next < 0)
        next = 0;
    if(next >= (int)commandPopupFiltered.size())
        next = (int)commandPopupFiltered.size() - 1;
    commandPopupCursor = next;

    const int window = std::min(8, (int)commandPopupFiltered.size());
    if(commandPopupCursor < commandPopupOffset)
        commandPopupOffset = commandPopupCursor;
    else if(commandPopupCursor >= commandPopupOffset + window)
        commandPopupOffset = commandPopupCursor - window + 1;

    needsFullRedraw = true;
}

bool Editor::isCommandPopupActiveImpl() const
{
    return commandPopupActive;
}

std::optional<std::string> Editor::commandPopupSelectionImpl() const
{
    if(!commandPopupActive || commandPopupFiltered.empty())
        return std::nullopt;
    int idx = commandPopupFiltered[commandPopupCursor];
    if(idx < 0 || idx >= (int)commandPopupAll.size())
        return std::nullopt;
    return commandPopupAll[idx];
}

void Editor::startCommandHistorySearchImpl(std::string_view seed)
{
    commandHistorySearchActive = true;
    commandHistorySearchOriginal = std::string(seed);
    commandHistorySearchQueryValue = std::string(seed);
    commandHistorySearchCursor = 0;
    commandHistorySearchOffset = 0;
    updateCommandHistorySearchQuery(commandHistorySearchQueryValue);
}

std::string Editor::cancelCommandHistorySearchImpl()
{
    std::string restored = commandHistorySearchOriginal;
    commandHistorySearchActive = false;
    commandHistorySearchQueryValue.clear();
    commandHistorySearchOriginal.clear();
    commandHistorySearchMatches.clear();
    commandHistorySearchCursor = 0;
    commandHistorySearchOffset = 0;
    needsFullRedraw = true;
    return restored;
}

std::string Editor::acceptCommandHistorySearchImpl()
{
    std::string selected;
    if(!commandHistorySearchMatches.empty() &&
       commandHistorySearchCursor >= 0 &&
       commandHistorySearchCursor < (int)commandHistorySearchMatches.size())
    {
        int idx = commandHistorySearchMatches[commandHistorySearchCursor];
        if(idx >= 0 && idx < (int)commandHistory.size())
            selected = commandHistory[idx];
    }
    if(selected.empty())
        selected = commandHistorySearchQueryValue;

    commandHistorySearchActive = false;
    commandHistorySearchQueryValue.clear();
    commandHistorySearchOriginal.clear();
    commandHistorySearchMatches.clear();
    commandHistorySearchCursor = 0;
    commandHistorySearchOffset = 0;
    needsFullRedraw = true;
    return selected;
}

void Editor::updateCommandHistorySearchQueryImpl(std::string_view query)
{
    commandHistorySearchQueryValue = std::string(query);
    commandHistorySearchMatches.clear();
    commandHistorySearchCursor = 0;
    commandHistorySearchOffset = 0;

    if(commandHistory.empty())
    {
        needsFullRedraw = true;
        return;
    }

    if(commandHistorySearchQueryValue.empty())
    {
        for(int i = (int)commandHistory.size() - 1; i >= 0; --i)
            commandHistorySearchMatches.push_back(i);
        needsFullRedraw = true;
        return;
    }

    std::vector<std::pair<int, int>> scored;
    scored.reserve(commandHistory.size());
    std::vector<int> positions;

    for(int i = 0; i < (int)commandHistory.size(); ++i)
    {
        int score = editor::helper::fuzzyScoreWithPositions(
            commandHistorySearchQueryValue, commandHistory[i], positions);
        if(score >= 0)
            scored.emplace_back(i, score);
    }

    if(!scored.empty())
    {
        std::stable_sort(scored.begin(), scored.end(),
                         [](const std::pair<int, int>& left,
                            const std::pair<int, int>& right)
                         {
                             if(left.second != right.second)
                                 return left.second > right.second;
                             return left.first > right.first;
                         });
        for(const auto& entry : scored)
            commandHistorySearchMatches.push_back(entry.first);
    }

    needsFullRedraw = true;
}

void Editor::moveCommandHistorySearchCursorImpl(int delta)
{
    if(!commandHistorySearchActive || commandHistorySearchMatches.empty())
        return;
    int next = commandHistorySearchCursor + delta;
    if(next < 0)
        next = 0;
    if(next >= (int)commandHistorySearchMatches.size())
        next = (int)commandHistorySearchMatches.size() - 1;
    commandHistorySearchCursor = next;

    const int window = std::min(8, (int)commandHistorySearchMatches.size());
    if(commandHistorySearchCursor < commandHistorySearchOffset)
        commandHistorySearchOffset = commandHistorySearchCursor;
    else if(commandHistorySearchCursor >= commandHistorySearchOffset + window)
        commandHistorySearchOffset = commandHistorySearchCursor - window + 1;

    needsFullRedraw = true;
}

bool Editor::isCommandHistorySearchActiveImpl() const
{
    return commandHistorySearchActive;
}

const std::string& Editor::commandHistorySearchQueryImpl() const
{
    return commandHistorySearchQueryValue;
}

void Editor::drawCommandPopupImpl(std::string& output) const
{
#ifdef UVIM_MINIMAL
    (void)output;
    return;
#else
    if(!commandPopupActive)
        return;
    if(commandHistorySearchActive)
        return;

    auto isLineJumpQuery = [](std::string_view q) -> bool
    {
        if(q.empty())
            return false;
        size_t i = 0;
        while(i < q.size() && (q[i] == ' ' || q[i] == '\t'))
            ++i;
        if(i >= q.size())
            return false;
        if(q[i] == '+' || q[i] == '-')
            ++i;
        size_t digitsStart = i;
        while(i < q.size() && q[i] >= '0' && q[i] <= '9')
            ++i;
        return i > digitsStart;
    };

    if(currentMode == COMMAND && !commandBuffer.empty() &&
       commandBuffer.front() == ':')
    {
        std::string_view q(commandBuffer);
        q.remove_prefix(1);
        if(isLineJumpQuery(q))
            return;
    }

    widgets::CommandPopupView view{
        .frame = {.theme = theme,
                  .screenRows = screenRows,
                  .screenCols = screenCols},
        .entries = commandPopupAll,
        .filtered = commandPopupFiltered,
        .offset = commandPopupOffset,
        .cursor = commandPopupCursor,
    };
    widgets::drawCommandPopup(output, view);
#endif
}

void Editor::drawCommandHistoryPopupImpl(std::string& output) const
{
#ifdef UVIM_MINIMAL
    (void)output;
    return;
#else
    if(!commandHistorySearchActive)
        return;
    widgets::CommandHistoryPopupView view{
        .frame = {.theme = theme,
                  .screenRows = screenRows,
                  .screenCols = screenCols},
        .history = commandHistory,
        .matches = commandHistorySearchMatches,
        .offset = commandHistorySearchOffset,
        .cursor = commandHistorySearchCursor,
    };
    widgets::drawCommandHistoryPopup(output, view);
#endif
}

std::vector<std::string>
Editor::getCommandCompletionsImpl(std::string_view prefix)
{
    return getCommandCompletions(prefix, currentMode);
}

std::vector<std::string>
Editor::getCommandCompletionsImpl(std::string_view prefix, Mode mode)
{
    static const std::vector<std::string> baseCommands = {
        "w",
        "write",
        "q",
        "quit",
        "q!",
        "qa",
        "qall",
        "qa!",
        "qall!",
        "wq",
        "x",
        "qw",
        "qw!",
        "wa",
        "wall",
        "wa!",
        "wqa",
        "wqall",
        "wqa!",
        "wqall!",
        "xa",
        "e",
        "edit",
        "e%",
        "edit%",
        "enew",
        "new",
        "vnew",
        "bn",
        "bnext",
        "bp",
        "bprev",
        "bprevious",
        "bd",
        "bd!",
        "bdelete",
        "b ",
        "buffer ",
        "ls",
        "buffers",
        "sp",
        "split",
        "vs",
        "vsplit",
        "vh",
        "hs",
        "hsplit",
        "Sex",
        "Sexplore",
        "Hex",
        "Hexplore",
        "Vex",
        "Vexplore",
        "only",
        "tabnew",
        "tabe",
        "tabn",
        "tabnext",
        "tabp",
        "tabprev",
        "tabc",
        "tabclose",
        "set",
        "format",
        "fmt",
        "syntax",
        "noh",
        "nohlsearch",
        "lspinfo",
        "emitasm",
        "emitasm --raw",
        "emoji",
        "em",
        "glyphselect",
#ifdef UVIM_ENABLE_COLOR_TOOLS
        "ansitools",
        "colorpicker",
        "colorselect",
#endif
        "help",
        "h",
        "cd",
        "cdr",
        "pwd",
        "loc",
        "loc!",
        "loc%",
        "loctotal",
        "rfs",
        "git add",
        "git blame",
        "git stage",
        "git log",
        "git prettylog",
        "git diff",
        "git commit",
        "git fixup",
        "git stash",
        "git stash pop",
    };

    auto hasCommand = [](const std::vector<std::string>& list,
                         const std::string& value) -> bool
    { return std::find(list.begin(), list.end(), value) != list.end(); };

    const std::vector<std::string>* activeList = &baseCommands;
    std::vector<std::string> fileBrowserCommands;
    if(mode == FILE_BROWSER)
    {
        fileBrowserCommands = baseCommands;
        const std::vector<std::string> extras = {
            "delete", "d",  "rm",    "rename", "r", "mv",
            "mkdir",  "md", "touch", "new",    "?",
        };
        for(const auto& extra : extras)
        {
            if(!hasCommand(fileBrowserCommands, extra))
                fileBrowserCommands.push_back(extra);
        }

        const std::vector<std::string> notApplicable = {"wq", "x"};
        fileBrowserCommands.erase(
            std::remove_if(
                fileBrowserCommands.begin(), fileBrowserCommands.end(),
                [&](const std::string& cmd)
                {
                    return std::find(notApplicable.begin(), notApplicable.end(),
                                     cmd) != notApplicable.end();
                }),
            fileBrowserCommands.end());

        activeList = &fileBrowserCommands;
    }

    std::vector<std::string> matches;
    for(const auto& cmd : *activeList)
    {
        if(prefix.size() <= cmd.size() &&
           std::string_view(cmd).substr(0, prefix.size()) == prefix)
        {
            matches.push_back(cmd);
        }
    }
    return matches;
}

std::vector<std::string> Editor::getHelpCompletionsImpl(std::string_view prefix)
{
    static const std::vector<std::string> topics = {
        "commands",    "modes",     "navigation",  "editing", "files",
        "filebrowser", "run",       "buffers",     "windows", "search",
        "regex",       "clipboard", "git",         "ga",      "gb",
        "gbb",         "gbl",       "gj",          "gbv",     "gs",
        "emitasm",     "lsp",       "diagnostics", "help"};

    std::vector<std::string> matches;
    for(const auto& topic : topics)
    {
        if(prefix.size() <= topic.size() &&
           std::string_view(topic).substr(0, prefix.size()) == prefix)
        {
            matches.push_back(topic);
        }
    }
    return matches;
}

std::vector<std::string> Editor::getSetCompletionsImpl(std::string_view prefix)
{
    static const std::vector<std::string> options = {
        "set autobraces",
        "set noautobraces",
        "set autobraces?",
        "set autobraces=",
        "set autoquotes",
        "set noautoquotes",
        "set autoquotes?",
        "set autoquotes=",
        "set autobracesinstrings",
        "set noautobracesinstrings",
        "set autobracesinstrings?",
        "set autobracesinstrings=",
        "set autocomplete",
        "set noautocomplete",
        "set autocomplete?",
        "set autocomplete=",
        "set completionautoparens",
        "set nocompletionautoparens",
        "set completionautoparens?",
        "set completionautoparens=",
        "set showtabs",
        "set noshowtabs",
        "set showtabs?",
        "set showtabs=",
        "set tabnumbers",
        "set notabnumbers",
        "set tabnumbers?",
        "set tabnumbers=",
        "set tabspaces?",
        "set tabspaces=",
        "set tabspaces=2",
        "set tabspaces=4",
        "set tabspaces=8",
        "set tabspaces=1",
        "set tabspaces=3",
        "set tabspaces=5",
        "set tabspaces=6",
        "set tabspaces=7",
        "set tabspaces=9",
        "set tabspaces=10",
        "set tabspaces=12",
        "set tabspaces=16",
        "set commenttogglepartial",
        "set nocommenttogglepartial",
        "set commenttogglepartial?",
        "set gdcenter",
        "set nogdcenter",
        "set gdcenter?",
        "set gdcenter=",
        "set formatoninsertleave",
        "set noformatoninsertleave",
        "set formatoninsertleave?",
        "set formatonsave",
        "set noformatonsave",
        "set formatonsave?",
        "set formatonsave=",
        "set autodetectlsps",
        "set noautodetectlsps",
        "set autodetectlsps?",
        "set autodetectlsps=",
        "set emitlsp",
        "set noemitlsp",
        "set emitlsp?",
        "set emitlsp=",
        "set filebrowser.fuzzy",
        "set nofilebrowser.fuzzy",
        "set filebrowser.fuzzy?",
        "set filebrowser.fuzzy=",
        "set status.lspgap",
        "set status.lspgap?",
        "set status.lspgap=",
        "set commandline.messageprefix",
        "set nocommandline.messageprefix",
        "set commandline.messageprefix?",
        "set commandline.messageprefix=",
        "set formatondoubleesctimeoutms?",
        "set formatondoubleesctimeoutms=",
        "set gitdefaultcolors?",
        "set enablegitdefaultcolors",
        "set disablegitdefaultcolors",
        "set gitignore?",
        "set gitignore",
        "set nogitignore",
        "set gitblameinfo?",
        "set gitblameinfo",
        "set nogitblameinfo",
        "set gitignore=",
        "set syntax.cpp.highlight_system_includes",
        "set nosyntax.cpp.highlight_system_includes",
        "set syntax.cpp.highlight_system_includes?",
        "set syntax.cpp.highlight_param_types",
        "set nosyntax.cpp.highlight_param_types",
        "set syntax.cpp.highlight_param_types?",
        "set syntax.cpp.locals_color",
        "set syntax.cpp.locals_color?",
        "set syntax.cpp.locals_color=normal",
        "set syntax.cpp.locals_color=keyword",
        "set syntax.cpp.locals_color=type",
        "set syntax.cpp.locals_color=string",
        "set syntax.cpp.locals_color=char",
        "set syntax.cpp.locals_color=comment",
        "set syntax.cpp.locals_color=preprocessor",
        "set syntax.cpp.locals_color=number",
        "set syntax.cpp.locals_color=operator",
        "set syntax.cpp.locals_color=function",
        "set syntax.cpp.locals_color=member",
        "set syntax.cpp.member_color",
        "set syntax.cpp.member_color?",
        "set syntax.cpp.member_color=normal",
        "set syntax.cpp.member_color=keyword",
        "set syntax.cpp.member_color=type",
        "set syntax.cpp.member_color=string",
        "set syntax.cpp.member_color=char",
        "set syntax.cpp.member_color=comment",
        "set syntax.cpp.member_color=preprocessor",
        "set syntax.cpp.member_color=number",
        "set syntax.cpp.member_color=operator",
        "set syntax.cpp.member_color=function",
        "set syntax.cpp.member_color=member",
        "set syntax.cpp.semantic_tokens",
        "set nosyntax.cpp.semantic_tokens",
        "set syntax.cpp.semantic_tokens?",
        "set syntax.mlang.semantic_tokens",
        "set nosyntax.mlang.semantic_tokens",
        "set syntax.mlang.semantic_tokens?",
        "set syntax.mlang.highlight_types",
        "set nosyntax.mlang.highlight_types",
        "set syntax.mlang.highlight_types?",
        "set syntax.mlang.highlight_builtin_docs",
        "set nosyntax.mlang.highlight_builtin_docs",
        "set syntax.mlang.highlight_builtin_docs?",
        "set python.formatter?",
        "set python.formatter=ruff",
        "set python.formatter=black",
        "set pyfmt=ruff",
        "set pyfmt=black",
        "set pyfmt?",
        "set utf8",
        "set noutf8",
        "set utf8?",
        "set utf8=",
    };

    std::vector<std::string> matches;
    for(const auto& opt : options)
    {
        if(prefix.size() <= opt.size() &&
           std::string_view(opt).substr(0, prefix.size()) == prefix)
        {
            matches.push_back(opt);
        }
    }
    return matches;
}

std::vector<std::string> Editor::getPathCompletionsImpl(std::string_view path)
{
    return ::editor::helper::getPathCompletions(path);
}

std::vector<std::string>
Editor::getPathCompletionsRecursiveImpl(std::string_view path)
{
    return ::editor::helper::getRecursivePathCompletions(path,
                                                         respectGitignore);
}

std::vector<std::string>
Editor::getLocPathCompletionsImpl(std::string_view path)
{
    return ::editor::helper::getLocPathCompletions(path, respectGitignore);
}
