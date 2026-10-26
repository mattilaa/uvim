#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Editor;
class LspClient;

class EditorLspController
{
public:
    explicit EditorLspController(Editor& editor);

    // Only completion callbacks publish clients to the editor. Workers own
    // clients exclusively until initialization has finished.
    void start(
        const std::string& label,
        std::unique_ptr<LspClient>& slot,
        bool& enabled,
        std::function<bool(LspClient&)> launch,
        std::function<void(bool, const std::string&)> completed = {});
    void disable(std::unique_ptr<LspClient>& slot, bool& enabled);
    bool starting(const std::unique_ptr<LspClient>& slot) const;

    void enableClangdLsp(
        bool enable,
        const std::string& compileCommandsDir,
        const std::string& clangdPath,
        const std::string& queryDriverAllowList);
    bool isClangdLspEnabled() const;
    void enableRobotLsp(
        bool enable,
        const std::string& robotLspPath,
        const std::vector<std::string>& robotLspArgs);
    bool isRobotLspEnabled() const;
    void enablePythonLsp(
        bool enable,
        const std::string& pythonLspPath,
        const std::vector<std::string>& pythonLspArgs);
    bool isPythonLspEnabled() const;
    void enableMlangLsp(
        bool enable,
        const std::string& mlangLspPath,
        const std::vector<std::string>& mlangLspArgs);
    bool isMlangLspEnabled() const;
    void enableHtmlLsp(
        bool enable,
        const std::string& htmlLspPath,
        const std::vector<std::string>& htmlLspArgs);
    bool isHtmlLspEnabled() const;
    void enableCssLsp(
        bool enable,
        const std::string& cssLspPath,
        const std::vector<std::string>& cssLspArgs);
    bool isCssLspEnabled() const;
    void enableJsonLsp(
        bool enable,
        const std::string& jsonLspPath,
        const std::vector<std::string>& jsonLspArgs);
    bool isJsonLspEnabled() const;
    void enableTsLsp(
        bool enable,
        const std::string& tsLspPath,
        const std::vector<std::string>& tsLspArgs);
    bool isTsLspEnabled() const;
    void enableCSharpLsp(
        bool enable,
        const std::string& csharpLspPath,
        const std::vector<std::string>& csharpLspArgs);
    bool isCSharpLspEnabled() const;

private:
    Editor& editor;
    std::unordered_map<const void*, size_t> generations;
    std::unordered_map<const void*, bool> pending;
    void retire(std::unique_ptr<LspClient> client);
};
