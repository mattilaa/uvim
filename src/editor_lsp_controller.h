#pragma once

#include <string>
#include <vector>

class Editor;

class EditorLspController
{
public:
    explicit EditorLspController(Editor& editor);

    void enableClangdLsp(bool enable, const std::string& compileCommandsDir,
                         const std::string& clangdPath,
                         const std::string& queryDriverAllowList);
    bool isClangdLspEnabled() const;
    void enableRobotLsp(bool enable, const std::string& robotLspPath,
                        const std::vector<std::string>& robotLspArgs);
    bool isRobotLspEnabled() const;
    void enablePythonLsp(bool enable, const std::string& pythonLspPath,
                         const std::vector<std::string>& pythonLspArgs);
    bool isPythonLspEnabled() const;
    void enableMlangLsp(bool enable, const std::string& mlangLspPath,
                        const std::vector<std::string>& mlangLspArgs);
    bool isMlangLspEnabled() const;
    void enableHtmlLsp(bool enable, const std::string& htmlLspPath,
                       const std::vector<std::string>& htmlLspArgs);
    bool isHtmlLspEnabled() const;
    void enableCssLsp(bool enable, const std::string& cssLspPath,
                      const std::vector<std::string>& cssLspArgs);
    bool isCssLspEnabled() const;
    void enableJsonLsp(bool enable, const std::string& jsonLspPath,
                       const std::vector<std::string>& jsonLspArgs);
    bool isJsonLspEnabled() const;
    void enableTsLsp(bool enable, const std::string& tsLspPath,
                     const std::vector<std::string>& tsLspArgs);
    bool isTsLspEnabled() const;

private:
    Editor& editor;
};
