#include "editor_settings_controller.h"
#include "editor.h"
#include "editor_utils.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif

using editor::helper::ascii_lower;
using editor::helper::parse_token_type;
using editor::helper::token_type_name;

EditorSettingsController::EditorSettingsController(Editor& editor)
    : editor(editor)
{
}

bool EditorSettingsController::handleSetCommand(std::string_view cmd)
{
    return editor.handleSetCommandImpl(cmd);
}

bool Editor::handleSetCommandImpl(std::string_view cmd)
{
    if(!cmd.starts_with("set "))
        return false;

    std::string opt = std::string(cmd.substr(4));
    if(opt == "autobraces?")
    {
        setStatusMessage(std::string("autobraces=") +
                         (autoBraces ? "true" : "false"));
        return true;
    }
    if(opt == "autoquotes?")
    {
        setStatusMessage(std::string("autoquotes=") +
                         (autoQuotes ? "true" : "false"));
        return true;
    }
    if(opt == "autobracesinstrings?")
    {
        setStatusMessage(std::string("autobracesinstrings=") +
                         (autoBracesInStrings ? "true" : "false"));
        return true;
    }
    if(opt == "autotags?")
    {
        setStatusMessage(std::string("autotags=") +
                         (autoTags ? "true" : "false"));
        return true;
    }
    if(opt == "tabspaces?")
    {
        setStatusMessage("tabspaces=" + std::to_string(tabSpaces));
        return true;
    }
    if(opt == "autocomplete?")
    {
        setStatusMessage(std::string("autocomplete=") +
                         (autoCompletion ? "true" : "false"));
        return true;
    }
    if(opt == "completionautoparens?")
    {
        setStatusMessage(std::string("completionautoparens=") +
                         (completionAutoParens ? "true" : "false"));
        return true;
    }
    if(opt == "showtabs?")
    {
        setStatusMessage(std::string("showtabs=") +
                         (showTabs ? "true" : "false"));
        return true;
    }
    if(opt == "tabnumbers?")
    {
        setStatusMessage(std::string("tabnumbers=") +
                         (showTabNumbers ? "true" : "false"));
        return true;
    }
    if(opt == "utf8?")
    {
        setStatusMessage(std::string("utf8=") + (utf8Mode ? "true" : "false"));
        return true;
    }
    if(opt == "gitblameinfo?")
    {
        setStatusMessage(std::string("gitblameinfo=") +
                         (showGitBlameInfo ? "true" : "false"));
        return true;
    }
    if(opt == "gitdefaultcolors?")
    {
        setStatusMessage(std::string("gitdefaultcolors=") +
                         (gitUseDefaultColors ? "true" : "false"));
        return true;
    }
    if(opt == "commenttogglepartial?")
    {
        setStatusMessage(std::string("commenttogglepartial=") +
                         (commentTogglePartial ? "true" : "false"));
        return true;
    }
    if(opt == "gitignore?")
    {
        setStatusMessage(std::string("gitignore=") +
                         (respectGitignore ? "true" : "false"));
        return true;
    }
    if(opt == "formatoninsertleave?")
    {
        setStatusMessage(std::string("formatoninsertleave=") +
                         (formatOnInsertLeave ? "true" : "false"));
        return true;
    }
    if(opt == "autodetectlsps?")
    {
        setStatusMessage(std::string("autodetectlsps=") +
                         (autoDetectLsps ? "true" : "false"));
        return true;
    }
    if(opt == "emitlsp?")
    {
        setStatusMessage(std::string("emitlsp=") +
                         (emitLspDiagnostics ? "true" : "false"));
        return true;
    }
    if(opt == "filebrowser.fuzzy?")
    {
        setStatusMessage(std::string("filebrowser.fuzzy=") +
                         (fileBrowserFuzzy ? "true" : "false"));
        return true;
    }
#ifdef UVIM_ENABLE_RG_CACHE
    if(opt == "rgcache?")
    {
        setStatusMessage(std::string("rgcache=") +
                         (rgCacheEnabled ? "true" : "false"));
        return true;
    }
#endif
    if(opt == "status.lspgap?")
    {
        setStatusMessage("status.lspgap=" + std::to_string(lspStatusGap));
        return true;
    }
    if(opt == "commandline.messageprefix?")
    {
        setStatusMessage(std::string("commandline.messageprefix=") +
                         (commandLineMessagePrefix ? "true" : "false"));
        return true;
    }
    if(opt == "formatonsave?")
    {
        setStatusMessage(std::string("formatonsave=") +
                         (formatOnSave ? "true" : "false"));
        return true;
    }
    if(opt == "gdcenter?")
    {
        setStatusMessage(std::string("gdcenter=") +
                         (gdCenterScreen ? "true" : "false"));
        return true;
    }
    if(opt == "formatondoubleesctimeoutms?")
    {
        setStatusMessage("formatondoubleesctimeoutms=" +
                         std::to_string(formatOnDoubleEscTimeoutMs));
        return true;
    }
    if(opt == "python.formatter?")
    {
        setStatusMessage("python.formatter=" + pythonFormatter);
        return true;
    }
    if(opt == "pyfmt?")
    {
        setStatusMessage("python.formatter=" + pythonFormatter);
        return true;
    }
    if(opt == "syntax.cpp.highlight_system_includes?")
    {
        setStatusMessage(std::string("syntax.cpp.highlight_system_includes=") +
                         (syntaxCppHighlightSystemIncludes ? "true" : "false"));
        return true;
    }
    if(opt == "syntax.cpp.highlight_param_types?")
    {
        setStatusMessage(std::string("syntax.cpp.highlight_param_types=") +
                         (syntaxCppHighlightParamTypes ? "true" : "false"));
        return true;
    }
    if(opt == "syntax.cpp.semantic_tokens?")
    {
        setStatusMessage(std::string("syntax.cpp.semantic_tokens=") +
                         (syntaxCppSemanticTokens ? "true" : "false"));
        return true;
    }
    if(opt == "syntax.mlang.semantic_tokens?")
    {
        setStatusMessage(std::string("syntax.mlang.semantic_tokens=") +
                         (syntaxMlangSemanticTokens ? "true" : "false"));
        return true;
    }
    if(opt == "syntax.cpp.locals_color?")
    {
        setStatusMessage("syntax.cpp.locals_color=" +
                         std::string(token_type_name(syntaxCppLocalToken)));
        return true;
    }
    if(opt == "syntax.cpp.member_color?")
    {
        setStatusMessage("syntax.cpp.member_color=" +
                         std::string(token_type_name(syntaxCppMemberToken)));
        return true;
    }
    if(opt == "syntax.mlang.highlight_types?")
    {
        setStatusMessage(std::string("syntax.mlang.highlight_types=") +
                         (syntaxMlangHighlightTypes ? "true" : "false"));
        return true;
    }
    if(opt == "syntax.mlang.highlight_builtin_docs?")
    {
        setStatusMessage(std::string("syntax.mlang.highlight_builtin_docs=") +
                         (syntaxMlangHighlightBuiltinDocs ? "true" : "false"));
        return true;
    }

    auto set_flag = [&](bool value)
    {
        autoBraces = value;
        setStatusMessage(std::string("autobraces=") +
                         (autoBraces ? "true" : "false"));
    };

    auto set_auto_quotes = [&](bool value)
    {
        autoQuotes = value;
        setStatusMessage(std::string("autoquotes=") +
                         (autoQuotes ? "true" : "false"));
    };

    auto set_auto_braces_in_strings = [&](bool value)
    {
        autoBracesInStrings = value;
        setStatusMessage(std::string("autobracesinstrings=") +
                         (autoBracesInStrings ? "true" : "false"));
    };

    auto set_autotags = [&](bool value)
    {
        autoTags = value;
        setStatusMessage(std::string("autotags=") +
                         (autoTags ? "true" : "false"));
    };

    auto set_gdcenter = [&](bool value)
    {
        gdCenterScreen = value;
        setStatusMessage(std::string("gdcenter=") +
                         (gdCenterScreen ? "true" : "false"));
    };

    auto set_emit_lsp = [&](bool value)
    {
        emitLspDiagnostics = value;
#ifdef UVIM_ENABLE_CLANGD_LSP
        if(lspClient)
            lspClient->setDiagnosticsEnabled(value);
        if(robotLspClient)
            robotLspClient->setDiagnosticsEnabled(value);
        if(pythonLspClient)
            pythonLspClient->setDiagnosticsEnabled(value);
        if(mlangLspClient)
            mlangLspClient->setDiagnosticsEnabled(value);
        if(htmlLspClient)
            htmlLspClient->setDiagnosticsEnabled(value);
        if(cssLspClient)
            cssLspClient->setDiagnosticsEnabled(value);
        if(jsonLspClient)
            jsonLspClient->setDiagnosticsEnabled(value);
        if(tsLspClient)
            tsLspClient->setDiagnosticsEnabled(value);
#endif
        if(!value)
            closeDiagnosticPopup();
        needsFullRedraw = true;
        setStatusMessage(std::string("emitlsp=") +
                         (emitLspDiagnostics ? "true" : "false"));
    };

    if(opt == "autobraces")
    {
        set_flag(true);
        return true;
    }
    if(opt == "noautobraces")
    {
        set_flag(false);
        return true;
    }
    if(opt == "autoquotes")
    {
        set_auto_quotes(true);
        return true;
    }
    if(opt == "noautoquotes")
    {
        set_auto_quotes(false);
        return true;
    }
    if(opt == "autobracesinstrings")
    {
        set_auto_braces_in_strings(true);
        return true;
    }
    if(opt == "noautobracesinstrings")
    {
        set_auto_braces_in_strings(false);
        return true;
    }
    if(opt == "syntax.cpp.highlight_system_includes")
    {
        syntaxCppHighlightSystemIncludes = true;
        setStatusMessage("syntax.cpp.highlight_system_includes=true");
        return true;
    }
    if(opt == "nosyntax.cpp.highlight_system_includes")
    {
        syntaxCppHighlightSystemIncludes = false;
        setStatusMessage("syntax.cpp.highlight_system_includes=false");
        return true;
    }
    if(opt == "syntax.cpp.highlight_param_types")
    {
        syntaxCppHighlightParamTypes = true;
        setStatusMessage("syntax.cpp.highlight_param_types=true");
        return true;
    }
    if(opt == "nosyntax.cpp.highlight_param_types")
    {
        syntaxCppHighlightParamTypes = false;
        setStatusMessage("syntax.cpp.highlight_param_types=false");
        return true;
    }
    if(opt == "syntax.cpp.semantic_tokens")
    {
        syntaxCppSemanticTokens = true;
        if(currentBuffer)
            currentBuffer->lspSemanticTokensValid = false;
        needsFullRedraw = true;
        setStatusMessage("syntax.cpp.semantic_tokens=true");
        return true;
    }
    if(opt == "nosyntax.cpp.semantic_tokens")
    {
        syntaxCppSemanticTokens = false;
        if(currentBuffer)
            currentBuffer->lspSemanticTokensValid = false;
        needsFullRedraw = true;
        setStatusMessage("syntax.cpp.semantic_tokens=false");
        return true;
    }
    if(opt == "syntax.mlang.semantic_tokens")
    {
        syntaxMlangSemanticTokens = true;
        if(currentBuffer)
            currentBuffer->lspSemanticTokensValid = false;
        needsFullRedraw = true;
        setStatusMessage("syntax.mlang.semantic_tokens=true");
        return true;
    }
    if(opt == "nosyntax.mlang.semantic_tokens")
    {
        syntaxMlangSemanticTokens = false;
        if(currentBuffer)
        {
            currentBuffer->lspSemanticTokens.clear();
            currentBuffer->lspSemanticTokensValid = false;
            currentBuffer->lspSemanticTokensRevision = 0;
        }
        needsFullRedraw = true;
        setStatusMessage("syntax.mlang.semantic_tokens=false");
        return true;
    }
    if(opt.rfind("syntax.cpp.locals_color=", 0) == 0)
    {
        std::string value =
            opt.substr(std::string("syntax.cpp.locals_color=").length());
        syntaxCppLocalToken = parse_token_type(value, syntaxCppLocalToken);
        setStatusMessage("syntax.cpp.locals_color=" +
                         std::string(token_type_name(syntaxCppLocalToken)));
        needsFullRedraw = true;
        return true;
    }
    if(opt.rfind("syntax.cpp.member_color=", 0) == 0)
    {
        std::string value =
            opt.substr(std::string("syntax.cpp.member_color=").length());
        syntaxCppMemberToken = parse_token_type(value, syntaxCppMemberToken);
        setStatusMessage("syntax.cpp.member_color=" +
                         std::string(token_type_name(syntaxCppMemberToken)));
        needsFullRedraw = true;
        return true;
    }
    if(opt == "syntax.mlang.highlight_types")
    {
        syntaxMlangHighlightTypes = true;
        setStatusMessage("syntax.mlang.highlight_types=true");
        return true;
    }
    if(opt == "nosyntax.mlang.highlight_types")
    {
        syntaxMlangHighlightTypes = false;
        setStatusMessage("syntax.mlang.highlight_types=false");
        return true;
    }
    if(opt == "syntax.mlang.highlight_builtin_docs")
    {
        syntaxMlangHighlightBuiltinDocs = true;
        setStatusMessage("syntax.mlang.highlight_builtin_docs=true");
        return true;
    }
    if(opt == "nosyntax.mlang.highlight_builtin_docs")
    {
        syntaxMlangHighlightBuiltinDocs = false;
        setStatusMessage("syntax.mlang.highlight_builtin_docs=false");
        return true;
    }
    if(opt.rfind("python.formatter=", 0) == 0 || opt.rfind("pyfmt=", 0) == 0)
    {
        std::string value = opt.substr(opt.find('=') + 1);
        std::string v = ascii_lower(value);
        if(v == "black" || v == "ruff")
        {
            pythonFormatter = v;
            setStatusMessage("python.formatter=" + pythonFormatter);
        }
        else
        {
            setStatusMessage("python.formatter: expected black|ruff");
        }
        return true;
    }
    if(opt == "autotags")
    {
        set_autotags(true);
        return true;
    }
    if(opt == "noautotags")
    {
        set_autotags(false);
        return true;
    }
    if(opt == "emitlsp")
    {
        set_emit_lsp(true);
        return true;
    }
    if(opt == "noemitlsp")
    {
        set_emit_lsp(false);
        return true;
    }
    if(opt == "gitblameinfo")
    {
        showGitBlameInfo = true;
        setStatusMessage("gitblameinfo=true");
        return true;
    }
    if(opt == "nogitblameinfo" || opt == "disablegitblame")
    {
        showGitBlameInfo = false;
        setStatusMessage("gitblameinfo=false");
        return true;
    }
    if(opt == "enablegitdefaultcolors")
    {
        gitUseDefaultColors = true;
        setStatusMessage("gitdefaultcolors=true");
        return true;
    }
    if(opt == "disablegitdefaultcolors")
    {
        gitUseDefaultColors = false;
        setStatusMessage("gitdefaultcolors=false");
        return true;
    }
    if(opt == "commenttogglepartial")
    {
        commentTogglePartial = true;
        setStatusMessage("commenttogglepartial=true");
        return true;
    }
    if(opt == "nocommenttogglepartial")
    {
        commentTogglePartial = false;
        setStatusMessage("commenttogglepartial=false");
        return true;
    }
    if(opt == "gitignore")
    {
        respectGitignore = true;
#ifdef UVIM_ENABLE_RG_CACHE
        rgCacheLoaded = false;
        rgCachedFiles.clear();
        rgCacheLineIndex.clear();
#endif
        setStatusMessage("gitignore=true");
        return true;
    }
    if(opt == "nogitignore")
    {
        respectGitignore = false;
#ifdef UVIM_ENABLE_RG_CACHE
        rgCacheLoaded = false;
        rgCachedFiles.clear();
        rgCacheLineIndex.clear();
#endif
        setStatusMessage("gitignore=false");
        return true;
    }
    if(opt == "formatoninsertleave")
    {
        formatOnInsertLeave = true;
        setStatusMessage("formatoninsertleave=true");
        return true;
    }
    if(opt == "gdcenter")
    {
        set_gdcenter(true);
        return true;
    }
    if(opt == "nogdcenter")
    {
        set_gdcenter(false);
        return true;
    }
    if(opt == "noformatoninsertleave")
    {
        formatOnInsertLeave = false;
        setStatusMessage("formatoninsertleave=false");
        return true;
    }
    if(opt == "autodetectlsps")
    {
        autoDetectLsps = true;
        setStatusMessage("autodetectlsps=true");
        return true;
    }
    if(opt == "noautodetectlsps")
    {
        autoDetectLsps = false;
        setStatusMessage("autodetectlsps=false");
        return true;
    }
    if(opt == "filebrowser.fuzzy")
    {
        fileBrowserFuzzy = true;
        setStatusMessage("filebrowser.fuzzy=true");
        return true;
    }
    if(opt == "nofilebrowser.fuzzy")
    {
        fileBrowserFuzzy = false;
        setStatusMessage("filebrowser.fuzzy=false");
        return true;
    }
#ifdef UVIM_ENABLE_RG_CACHE
    if(opt == "rgcache")
    {
        rgCacheEnabled = true;
        rgCacheLoaded = false;
        rgCacheLineIndex.clear();
        setStatusMessage("rgcache=true");
        return true;
    }
    if(opt == "norgcache")
    {
        rgCacheEnabled = false;
        rgCacheLoaded = false;
        rgCachedFiles.clear();
        rgCacheLineIndex.clear();
        setStatusMessage("rgcache=false");
        return true;
    }
    if(opt.rfind("rgcache=", 0) == 0)
    {
        std::string value = opt.substr(std::string("rgcache=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            rgCacheEnabled = true;
            rgCacheLoaded = false;
            rgCacheLineIndex.clear();
            setStatusMessage("rgcache=true");
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            rgCacheEnabled = false;
            rgCacheLoaded = false;
            rgCachedFiles.clear();
            rgCacheLineIndex.clear();
            setStatusMessage("rgcache=false");
        }
        else
        {
            setStatusMessage("rgcache: expected true/false");
        }
        return true;
    }
#endif
    if(opt == "commandline.messageprefix")
    {
        commandLineMessagePrefix = true;
        setStatusMessage("commandline.messageprefix=true");
        return true;
    }
    if(opt == "nocommandline.messageprefix")
    {
        commandLineMessagePrefix = false;
        setStatusMessage("commandline.messageprefix=false");
        return true;
    }
    if(opt == "formatonsave")
    {
        formatOnSave = true;
        setStatusMessage("formatonsave=true");
        return true;
    }
    if(opt == "noformatonsave")
    {
        formatOnSave = false;
        setStatusMessage("formatonsave=false");
        return true;
    }
    if(opt.rfind("formatondoubleesctimeoutms=", 0) == 0)
    {
        std::string value =
            opt.substr(std::string("formatondoubleesctimeoutms=").length());
        try
        {
            int ms = std::stoi(value);
            if(ms > 0 && ms <= 5000)
            {
                formatOnDoubleEscTimeoutMs = ms;
                setStatusMessage("formatondoubleesctimeoutms=" +
                                 std::to_string(formatOnDoubleEscTimeoutMs));
            }
            else
            {
                setStatusMessage("formatondoubleesctimeoutms: expected 1-5000");
            }
        }
        catch(...)
        {
            setStatusMessage("formatondoubleesctimeoutms: expected number");
        }
        return true;
    }
    if(opt.rfind("formatonsave=", 0) == 0)
    {
        std::string value = opt.substr(std::string("formatonsave=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            formatOnSave = true;
            setStatusMessage("formatonsave=true");
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            formatOnSave = false;
            setStatusMessage("formatonsave=false");
        }
        else
        {
            setStatusMessage("formatonsave: expected true/false");
        }
        return true;
    }
    if(opt.rfind("autodetectlsps=", 0) == 0)
    {
        std::string value = opt.substr(std::string("autodetectlsps=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            autoDetectLsps = true;
            setStatusMessage("autodetectlsps=true");
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            autoDetectLsps = false;
            setStatusMessage("autodetectlsps=false");
        }
        else
        {
            setStatusMessage("autodetectlsps: expected true/false");
        }
        return true;
    }
    if(opt.rfind("emitlsp=", 0) == 0)
    {
        std::string value = opt.substr(std::string("emitlsp=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            set_emit_lsp(true);
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            set_emit_lsp(false);
        }
        else
        {
            setStatusMessage("emitlsp: expected true/false");
        }
        return true;
    }
    if(opt.rfind("filebrowser.fuzzy=", 0) == 0)
    {
        std::string value =
            opt.substr(std::string("filebrowser.fuzzy=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            fileBrowserFuzzy = true;
            setStatusMessage("filebrowser.fuzzy=true");
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            fileBrowserFuzzy = false;
            setStatusMessage("filebrowser.fuzzy=false");
        }
        else
        {
            setStatusMessage("filebrowser.fuzzy: expected true/false");
        }
        return true;
    }
    if(opt.rfind("status.lspgap=", 0) == 0)
    {
        std::string value = opt.substr(std::string("status.lspgap=").length());
        try
        {
            int gap = std::stoi(value);
            if(gap >= 0 && gap <= 20)
            {
                lspStatusGap = gap;
                setStatusMessage("status.lspgap=" +
                                 std::to_string(lspStatusGap));
            }
            else
            {
                setStatusMessage("status.lspgap: expected 0-20");
            }
        }
        catch(...)
        {
            setStatusMessage("status.lspgap: expected number");
        }
        return true;
    }
    if(opt.rfind("commandline.messageprefix=", 0) == 0)
    {
        std::string value =
            opt.substr(std::string("commandline.messageprefix=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            commandLineMessagePrefix = true;
            setStatusMessage("commandline.messageprefix=true");
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            commandLineMessagePrefix = false;
            setStatusMessage("commandline.messageprefix=false");
        }
        else
        {
            setStatusMessage("commandline.messageprefix: expected true/false");
        }
        return true;
    }
    if(opt.rfind("gdcenter=", 0) == 0)
    {
        std::string value = opt.substr(std::string("gdcenter=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            set_gdcenter(true);
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            set_gdcenter(false);
        }
        else
        {
            setStatusMessage("gdcenter: expected true/false");
        }
        return true;
    }
    if(opt.rfind("autobraces=", 0) == 0)
    {
        std::string value = opt.substr(std::string("autobraces=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            set_flag(true);
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            set_flag(false);
        }
        else
        {
            setStatusMessage("autobraces: expected true/false");
        }
        return true;
    }
    if(opt.rfind("autoquotes=", 0) == 0)
    {
        std::string value = opt.substr(std::string("autoquotes=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            set_auto_quotes(true);
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            set_auto_quotes(false);
        }
        else
        {
            setStatusMessage("autoquotes: expected true/false");
        }
        return true;
    }
    if(opt.rfind("autobracesinstrings=", 0) == 0)
    {
        std::string value =
            opt.substr(std::string("autobracesinstrings=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            set_auto_braces_in_strings(true);
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            set_auto_braces_in_strings(false);
        }
        else
        {
            setStatusMessage("autobracesinstrings: expected true/false");
        }
        return true;
    }
    if(opt.rfind("autotags=", 0) == 0)
    {
        std::string value = opt.substr(std::string("autotags=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            set_autotags(true);
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            set_autotags(false);
        }
        else
        {
            setStatusMessage("autotags: expected true/false");
        }
        return true;
    }

    auto set_auto_completion = [&](bool value)
    {
        autoCompletion = value;
        setStatusMessage(std::string("autocomplete=") +
                         (autoCompletion ? "true" : "false"));
    };

    auto set_completion_auto_parens = [&](bool value)
    {
        completionAutoParens = value;
        setStatusMessage(std::string("completionautoparens=") +
                         (completionAutoParens ? "true" : "false"));
    };

    if(opt == "autocomplete")
    {
        set_auto_completion(true);
        return true;
    }
    if(opt == "noautocomplete")
    {
        set_auto_completion(false);
        return true;
    }
    if(opt == "completionautoparens")
    {
        set_completion_auto_parens(true);
        return true;
    }
    if(opt == "nocompletionautoparens")
    {
        set_completion_auto_parens(false);
        return true;
    }
    if(opt == "showtabs")
    {
        showTabs = true;
        tabBarOffset = 0;
        setStatusMessage("showtabs=true");
        needsFullRedraw = true;
        return true;
    }
    if(opt == "noshowtabs")
    {
        showTabs = false;
        tabBarOffset = 0;
        setStatusMessage("showtabs=false");
        needsFullRedraw = true;
        return true;
    }
    if(opt == "tabnumbers")
    {
        showTabNumbers = true;
        setStatusMessage("tabnumbers=true");
        needsFullRedraw = true;
        return true;
    }
    if(opt == "notabnumbers")
    {
        showTabNumbers = false;
        setStatusMessage("tabnumbers=false");
        needsFullRedraw = true;
        return true;
    }
    if(opt == "utf8")
    {
        utf8Mode = true;
        setStatusMessage("utf8=true");
        needsFullRedraw = true;
        return true;
    }
    if(opt == "noutf8")
    {
        utf8Mode = false;
        setStatusMessage("utf8=false");
        needsFullRedraw = true;
        return true;
    }
    if(opt.rfind("autocomplete=", 0) == 0)
    {
        std::string value = opt.substr(std::string("autocomplete=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            set_auto_completion(true);
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            set_auto_completion(false);
        }
        else
        {
            setStatusMessage("autocomplete: expected true/false");
        }
        return true;
    }
    if(opt.rfind("completionautoparens=", 0) == 0)
    {
        std::string value =
            opt.substr(std::string("completionautoparens=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            set_completion_auto_parens(true);
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            set_completion_auto_parens(false);
        }
        else
        {
            setStatusMessage("completionautoparens: expected true/false");
        }
        return true;
    }
    if(opt.rfind("showtabs=", 0) == 0)
    {
        std::string value = opt.substr(std::string("showtabs=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            showTabs = true;
            tabBarOffset = 0;
            setStatusMessage("showtabs=true");
            needsFullRedraw = true;
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            showTabs = false;
            tabBarOffset = 0;
            setStatusMessage("showtabs=false");
            needsFullRedraw = true;
        }
        else
        {
            setStatusMessage("showtabs: expected true/false");
        }
        return true;
    }
    if(opt.rfind("tabnumbers=", 0) == 0)
    {
        std::string value = opt.substr(std::string("tabnumbers=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            showTabNumbers = true;
            setStatusMessage("tabnumbers=true");
            needsFullRedraw = true;
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            showTabNumbers = false;
            setStatusMessage("tabnumbers=false");
            needsFullRedraw = true;
        }
        else
        {
            setStatusMessage("tabnumbers: expected true/false");
        }
        return true;
    }
    if(opt.rfind("gitignore=", 0) == 0)
    {
        std::string value = opt.substr(std::string("gitignore=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            respectGitignore = true;
            setStatusMessage("gitignore=true");
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            respectGitignore = false;
            setStatusMessage("gitignore=false");
        }
        else
        {
            setStatusMessage("gitignore: expected true/false");
        }
        return true;
    }
    if(opt.rfind("utf8=", 0) == 0)
    {
        std::string value = opt.substr(std::string("utf8=").length());
        if(value == "true" || value == "1" || value == "on")
        {
            utf8Mode = true;
            setStatusMessage("utf8=true");
            needsFullRedraw = true;
        }
        else if(value == "false" || value == "0" || value == "off")
        {
            utf8Mode = false;
            setStatusMessage("utf8=false");
            needsFullRedraw = true;
        }
        else
        {
            setStatusMessage("utf8: expected true/false");
        }
        return true;
    }

    if(opt.rfind("tabspaces=", 0) == 0)
    {
        std::string value =
            std::string(opt.substr(std::string("tabspaces=").length()));
        try
        {
            int v = std::stoi(value);
            if(v >= 1 && v <= 16)
            {
                tabSpaces = v;
                setStatusMessage("tabspaces=" + std::to_string(tabSpaces));
            }
            else
            {
                setStatusMessage("tabspaces: expected 1-16");
            }
        }
        catch(...)
        {
            setStatusMessage("tabspaces: expected number");
        }
        return true;
    }

    setStatusMessage("Unknown option: " + opt);
    return true;
}
