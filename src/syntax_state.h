#pragma once

struct CppMethodScanState
{
    bool inBlockComment = false;
    bool inMethod = false;
    bool pendingMethod = false;
    int braceDepth = 0;
    int methodBraceDepth = 0;
};

struct CppFunctionScanState
{
    bool inBlockComment = false;
    bool inFunction = false;
    bool pendingFunction = false;
    int braceDepth = 0;
    int functionBraceDepth = 0;
};

struct CppParamListScanState
{
    bool inBlockComment = false;
    bool inParamList = false;
    int parenDepth = 0;
};
