#pragma once

#include "file_type.h"
#include "token_type.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Editor;

class SyntaxHighlighter
{
public:
    explicit SyntaxHighlighter(Editor* editor);

    bool isFileType(FileType type) const;

    template <FileType Type>
    bool isFileType() const
    {
        return isFileType(Type);
    }

    void ensureMlangTokensLoaded() const;
    std::optional<TokenType> lookupMlangTokenType(std::string_view word) const;
    std::string getColorCode(TokenType type) const;
    std::vector<Token> tokenizeLine(
        const std::string& line,
        bool& inBlockComment,
        bool& inTomlMultiline,
        char& tomlQuote,
        bool& inMarkupFence,
        char& markupFenceChar,
        bool inCppMethodContext = false,
        bool inCppFunctionContext = false,
        bool inCppParamListContext = false) const;
    void renderLineWithSyntax(
        std::string& output,
        const std::string& line,
        int start,
        int len,
        int fileRow) const;
    bool isCppMemberIndexLoaded() const;
    void ensureCppMemberIndexLoaded() const;

private:
    Editor* editor;
    mutable bool cppMemberIndexLoaded = false;
    mutable bool cppMemberIndexPending = false;
    static void collectCppMemberIndex(
        const std::filesystem::path& root,
        std::unordered_set<std::string>& cppMemberNames,
        std::unordered_set<std::string>& cppClassNames,
        const std::function<bool()>& cancelled);
    mutable std::unordered_set<std::string> cppMemberNames;
    mutable std::unordered_set<std::string> cppClassNames;
    mutable bool systemIncludeDirsLoaded = false;
    mutable bool systemIncludeDirsPending = false;
    mutable std::unordered_map<std::string, bool> systemIncludeCache;
    mutable std::unordered_set<std::string> systemIncludePending;
    static void collectSystemIncludeDirs(
        std::filesystem::path baseDir,
        std::vector<std::filesystem::path>& systemIncludeDirs,
        const std::function<bool()>& cancelled);
    mutable std::vector<std::filesystem::path> systemIncludeDirs;

    void ensureCppMemberIndex() const;
    void ensureSystemIncludeDirsLoaded() const;
    bool isSystemInclude(std::string_view header) const;
};
