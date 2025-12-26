#pragma once

// Syntax highlighting token types (extracted from Editor class)
enum TokenType
{
    TOKEN_NORMAL,
    TOKEN_KEYWORD,
    TOKEN_TYPE,
    TOKEN_STRING,
    TOKEN_CHAR,
    TOKEN_COMMENT,
    TOKEN_PREPROCESSOR,
    TOKEN_NUMBER,
    TOKEN_OPERATOR,
    TOKEN_FUNCTION
};

// Token structure for syntax highlighting
struct Token
{
    TokenType type;
    int start;
    int length;
};
