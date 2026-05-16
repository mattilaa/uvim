#pragma once

#include "file_type.h"
#include "mode.h"
#include <functional>
#include <string>
#include <string_view>
#include <variant>

class Editor;

class ClangFormatter
{
public:
    explicit ClangFormatter(Editor& editor);

    bool operator()(Mode mode);

private:
    size_t byteOffsetForPosition(int y, int x) const;
    bool formatWithArgs(const std::string& extraArgs,
                        const std::string& successMessage);
    bool formatVisualSelection();
    bool formatVisualBlockSelection();

    Editor& editor;
};

class PythonFormatter
{
public:
    explicit PythonFormatter(Editor& editor);

    bool operator()(Mode mode);
    void lintBuffer();

private:
    Editor& editor;
};

class RobotFormatter
{
public:
    explicit RobotFormatter(Editor& editor);

    bool operator()(Mode mode);

private:
    Editor& editor;
};

class JsonFormatter
{
public:
    explicit JsonFormatter(Editor& editor);

    bool operator()(Mode mode);

private:
    Editor& editor;
};

class YamlFormatter
{
public:
    explicit YamlFormatter(Editor& editor);

    bool operator()(Mode mode);

private:
    Editor& editor;
};

class MlangFormatter
{
public:
    explicit MlangFormatter(Editor& editor);

    bool operator()(Mode mode);

private:
    Editor& editor;
};

class Formatter
{
public:
    explicit Formatter(Editor& editor);

    bool format(FileType type, Mode mode);
    void pythonLintBuffer();

private:
    using FormatterVariant =
        std::variant<std::reference_wrapper<ClangFormatter>,
                     std::reference_wrapper<PythonFormatter>,
                     std::reference_wrapper<RobotFormatter>,
                     std::reference_wrapper<JsonFormatter>,
                     std::reference_wrapper<YamlFormatter>,
                     std::reference_wrapper<MlangFormatter>>;

    FormatterVariant formatterFor(FileType type);

    Editor& editor;
    ClangFormatter clangFormatter;
    PythonFormatter pythonFormatter;
    RobotFormatter robotFormatter;
    JsonFormatter jsonFormatter;
    YamlFormatter yamlFormatter;
    MlangFormatter mlangFormatter;
};
