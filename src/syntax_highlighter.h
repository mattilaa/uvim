#pragma once

#include "editor_context.h"
#include <string>
#include <vector>

enum class TokenType
{
    NORMAL,
    KEYWORD,
    TYPE,
    STRING,
    NUMBER,
    COMMENT,
    PREPROCESSOR,
    FUNCTION,
    OPERATOR,
    BRACKET,
    IDENTIFIER
};

struct Token
{
    TokenType type;
    int start;
    int length;
};

class SyntaxHighlighter
{
public:
    explicit SyntaxHighlighter(EditorContext& ctx);

    bool isCppFile() const;
    bool isMlaFile() const;

    std::string getColorCode(TokenType type) const;
    std::vector<Token> tokenizeLine(const std::string& line) const;
    void renderLineWithSyntax(std::string& output, const std::string& line,
                              int lineNum, int startCol, int maxCols,
                              bool inSelection, int selStartCol, int selEndCol,
                              bool inSearchMatch, int searchStartCol,
                              int searchEndCol);

private:
    EditorContext& ctx;

    bool isKeyword(const std::string& word) const;
    bool isType(const std::string& word) const;
    bool isMlaKeyword(const std::string& word) const;
    bool isMlaType(const std::string& word) const;
};
