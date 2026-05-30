#include "editor_lsp_controller.h"
#include "editor.h"
#include "enablelog.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif
#include <filesystem>
#include <memory>

EditorLspController::EditorLspController(Editor& editor) : editor(editor) {}

void EditorLspController::enableClangdLsp(
    bool enable, const std::string& compileCommandsDir,
    const std::string& clangdPath, const std::string& queryDriverAllowList)
{
    editor.enableClangdLspImpl(enable, compileCommandsDir, clangdPath,
                               queryDriverAllowList);
}

bool EditorLspController::isClangdLspEnabled() const
{
    return editor.isClangdLspEnabledImpl();
}

void EditorLspController::enableRobotLsp(
    bool enable, const std::string& robotLspPath,
    const std::vector<std::string>& robotLspArgs)
{
    editor.enableRobotLspImpl(enable, robotLspPath, robotLspArgs);
}

bool EditorLspController::isRobotLspEnabled() const
{
    return editor.isRobotLspEnabledImpl();
}

void EditorLspController::enablePythonLsp(
    bool enable, const std::string& pythonLspPath,
    const std::vector<std::string>& pythonLspArgs)
{
    editor.enablePythonLspImpl(enable, pythonLspPath, pythonLspArgs);
}

bool EditorLspController::isPythonLspEnabled() const
{
    return editor.isPythonLspEnabledImpl();
}

void EditorLspController::enableMlangLsp(
    bool enable, const std::string& mlangLspPath,
    const std::vector<std::string>& mlangLspArgs)
{
    editor.enableMlangLspImpl(enable, mlangLspPath, mlangLspArgs);
}

bool EditorLspController::isMlangLspEnabled() const
{
    return editor.isMlangLspEnabledImpl();
}

void EditorLspController::enableHtmlLsp(
    bool enable, const std::string& htmlLspPath,
    const std::vector<std::string>& htmlLspArgs)
{
    editor.enableHtmlLspImpl(enable, htmlLspPath, htmlLspArgs);
}

bool EditorLspController::isHtmlLspEnabled() const
{
    return editor.isHtmlLspEnabledImpl();
}

void EditorLspController::enableCssLsp(
    bool enable, const std::string& cssLspPath,
    const std::vector<std::string>& cssLspArgs)
{
    editor.enableCssLspImpl(enable, cssLspPath, cssLspArgs);
}

bool EditorLspController::isCssLspEnabled() const
{
    return editor.isCssLspEnabledImpl();
}

void EditorLspController::enableJsonLsp(
    bool enable, const std::string& jsonLspPath,
    const std::vector<std::string>& jsonLspArgs)
{
    editor.enableJsonLspImpl(enable, jsonLspPath, jsonLspArgs);
}

bool EditorLspController::isJsonLspEnabled() const
{
    return editor.isJsonLspEnabledImpl();
}

void EditorLspController::enableTsLsp(bool enable, const std::string& tsLspPath,
                                      const std::vector<std::string>& tsLspArgs)
{
    editor.enableTsLspImpl(enable, tsLspPath, tsLspArgs);
}

bool EditorLspController::isTsLspEnabled() const
{
    return editor.isTsLspEnabledImpl();
}

