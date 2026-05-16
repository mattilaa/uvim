#include "formatter.h"
#include "editor.h"
#include <algorithm>

ClangFormatter::ClangFormatter(Editor& editor) : editor(editor) {}

PythonFormatter::PythonFormatter(Editor& editor) : editor(editor) {}

RobotFormatter::RobotFormatter(Editor& editor) : editor(editor) {}

JsonFormatter::JsonFormatter(Editor& editor) : editor(editor) {}

YamlFormatter::YamlFormatter(Editor& editor) : editor(editor) {}

MlangFormatter::MlangFormatter(Editor& editor) : editor(editor) {}

Formatter::Formatter(Editor& editor)
    : editor(editor), clangFormatter(editor), pythonFormatter(editor),
      robotFormatter(editor), jsonFormatter(editor), yamlFormatter(editor),
      mlangFormatter(editor)
{
}

bool ClangFormatter::operator()()
{
    return formatWithArgs("", "clang-format: formatted file");
}

size_t ClangFormatter::byteOffsetForPosition(int y, int x) const
{
    if(!editor.lines || editor.lines->empty())
        return 0;

    y = std::clamp(y, 0, (int)editor.lines->size() - 1);
    const std::string& ln = (*editor.lines)[y];
    x = std::clamp(x, 0, (int)ln.size());

    size_t off = 0;
    for(int i = 0; i < y; ++i)
        off += (*editor.lines)[i].size() + 1; // + '\n'
    off += (size_t)x;
    return off;
}

Formatter::FormatterVariant Formatter::formatterFor(Kind kind)
{
    switch(kind)
    {
    case Kind::Clang:
        return std::ref(clangFormatter);
    case Kind::Python:
        return std::ref(pythonFormatter);
    case Kind::Robot:
        return std::ref(robotFormatter);
    case Kind::Json:
        return std::ref(jsonFormatter);
    case Kind::Yaml:
        return std::ref(yamlFormatter);
    case Kind::Mlang:
        return std::ref(mlangFormatter);
    }
    return std::ref(clangFormatter);
}

bool Formatter::format(Kind kind)
{
    return std::visit([](auto formatter) { return formatter.get()(); },
                      formatterFor(kind));
}

size_t Formatter::byteOffsetForPosition(int y, int x) const
{
    return clangFormatter.byteOffsetForPosition(y, x);
}

bool Formatter::clangFormatWithArgs(const std::string& extraArgs,
                                    const std::string& successMessage)
{
    return clangFormatter.formatWithArgs(extraArgs, successMessage);
}

bool Formatter::pythonFormatBuffer()
{
    return format(Kind::Python);
}

void Formatter::pythonLintBuffer()
{
    pythonFormatter.lintBuffer();
}

bool Formatter::robotFormatBuffer()
{
    return format(Kind::Robot);
}

bool Formatter::jsonFormatBuffer()
{
    return format(Kind::Json);
}

bool Formatter::yamlFormatBuffer()
{
    return format(Kind::Yaml);
}

bool Formatter::mlangFormatBuffer()
{
    return format(Kind::Mlang);
}

void Formatter::clangFormatVisualSelection()
{
    clangFormatter.formatVisualSelection();
}

void Formatter::clangFormatVisualBlockSelection()
{
    clangFormatter.formatVisualBlockSelection();
}
