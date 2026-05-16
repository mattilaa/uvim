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

Formatter::FormatterVariant Formatter::formatterFor(FileType type)
{
    switch(type)
    {
    case FileType::Cpp:
        return std::ref(clangFormatter);
    case FileType::Python:
        return std::ref(pythonFormatter);
    case FileType::Robot:
        return std::ref(robotFormatter);
    case FileType::Json:
        return std::ref(jsonFormatter);
    case FileType::Yaml:
        return std::ref(yamlFormatter);
    case FileType::Mla:
        return std::ref(mlangFormatter);
    }
    return std::ref(clangFormatter);
}

bool Formatter::format(FileType type, Mode mode)
{
    return std::visit([mode](auto formatter) { return formatter.get()(mode); },
                      formatterFor(type));
}

void Formatter::pythonLintBuffer()
{
    pythonFormatter.lintBuffer();
}
