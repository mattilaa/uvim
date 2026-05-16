#pragma once

#include "file_type.h"
#include <functional>
#include <string>
#include <string_view>
#include <variant>

class Editor;

class ClangFormatter
{
public:
    explicit ClangFormatter(Editor& editor);

    bool operator()();
    size_t byteOffsetForPosition(int y, int x) const;
    bool formatWithArgs(const std::string& extraArgs,
                        const std::string& successMessage);
    void formatVisualSelection();
    void formatVisualBlockSelection();

private:
    Editor& editor;
};

class PythonFormatter
{
public:
    explicit PythonFormatter(Editor& editor);

    bool operator()();
    void lintBuffer();

private:
    Editor& editor;
};

class RobotFormatter
{
public:
    explicit RobotFormatter(Editor& editor);

    bool operator()();

private:
    Editor& editor;
};

class JsonFormatter
{
public:
    explicit JsonFormatter(Editor& editor);

    bool operator()();

private:
    Editor& editor;
};

class YamlFormatter
{
public:
    explicit YamlFormatter(Editor& editor);

    bool operator()();

private:
    Editor& editor;
};

class MlangFormatter
{
public:
    explicit MlangFormatter(Editor& editor);

    bool operator()();

private:
    Editor& editor;
};

class Formatter
{
public:
    enum class Kind
    {
        Clang,
        Python,
        Robot,
        Json,
        Yaml,
        Mlang
    };

    explicit Formatter(Editor& editor);

    size_t byteOffsetForPosition(int y, int x) const;
    bool format(Kind kind);
    bool format(FileType type);
    bool clangFormatWithArgs(const std::string& extraArgs,
                             const std::string& successMessage);
    void pythonLintBuffer();
    void clangFormatVisualSelection();
    void clangFormatVisualBlockSelection();

private:
    using FormatterVariant =
        std::variant<std::reference_wrapper<ClangFormatter>,
                     std::reference_wrapper<PythonFormatter>,
                     std::reference_wrapper<RobotFormatter>,
                     std::reference_wrapper<JsonFormatter>,
                     std::reference_wrapper<YamlFormatter>,
                     std::reference_wrapper<MlangFormatter>>;

    FormatterVariant formatterFor(Kind kind);
    FormatterVariant formatterFor(FileType type);

    Editor& editor;
    ClangFormatter clangFormatter;
    PythonFormatter pythonFormatter;
    RobotFormatter robotFormatter;
    JsonFormatter jsonFormatter;
    YamlFormatter yamlFormatter;
    MlangFormatter mlangFormatter;
};
