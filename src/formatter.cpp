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
    case FileType::FormatterConfig:
        return std::ref(yamlFormatter);
    case FileType::Mla:
        return std::ref(mlangFormatter);
    case FileType::Toml:
    case FileType::Html:
    case FileType::Css:
    case FileType::JavaScript:
    case FileType::TypeScript:
    case FileType::Xml:
    case FileType::MarkupText:
    case FileType::Rdoc:
    case FileType::CMake:
    case FileType::Shell:
    case FileType::Asm:
    case FileType::Flex:
    case FileType::Bison:
        return std::ref(clangFormatter);
    }
    return std::ref(clangFormatter);
}

bool Formatter::format(FileType type, Mode mode)
{
#ifdef UVIM_ENABLE_FORMATTERS
    return std::visit([mode](auto formatter) { return formatter.get()(mode); },
                      formatterFor(type));
#else
    (void)type;
    (void)mode;
    return false;
#endif
}

void Formatter::pythonLintBuffer()
{
#ifdef UVIM_ENABLE_FORMATTERS
    pythonFormatter.lintBuffer();
#endif
}

#ifndef UVIM_ENABLE_FORMATTERS
bool ClangFormatter::operator()(Mode mode)
{
    (void)mode;
    return false;
}

bool PythonFormatter::operator()(Mode mode)
{
    (void)mode;
    return false;
}

void PythonFormatter::lintBuffer() {}

bool RobotFormatter::operator()(Mode mode)
{
    (void)mode;
    return false;
}

bool JsonFormatter::operator()(Mode mode)
{
    (void)mode;
    return false;
}

bool YamlFormatter::operator()(Mode mode)
{
    (void)mode;
    return false;
}

bool MlangFormatter::operator()(Mode mode)
{
    (void)mode;
    return false;
}
#endif
