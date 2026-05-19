#include "editor_file_type_controller.h"
#include "constants.h"
#include "editor.h"
#include "text_utils.h"

#include <algorithm>

namespace
{
constexpr uint64_t file_type_bit(FileType type)
{
    return 1ull << static_cast<unsigned>(type);
}
} // namespace

EditorFileTypeController::EditorFileTypeController(Editor& editor)
    : editor(editor)
{
}

size_t EditorFileTypeController::fileTypeProbeHash() const
{
    if(!editor.lines)
        return 0;

    size_t h = 1469598103934665603ull;
    const int maxLines = std::min<int>(50, editor.lines->size());
    for(int i = 0; i < maxLines; ++i)
    {
        for(unsigned char c : (*editor.lines)[i])
        {
            h ^= c;
            h *= 1099511628211ull;
        }
        h ^= '\n';
        h *= 1099511628211ull;
    }
    return h;
}

bool EditorFileTypeController::isFileType(FileType type) const
{
    std::string_view pathSv;
    if(editor.filename && !editor.filename->empty())
        pathSv = *editor.filename;
    else if(editor.currentBuffer && !editor.currentBuffer->filename.empty())
        pathSv = editor.currentBuffer->filename;
    else
        return false;

    const size_t contentHash = fileTypeProbeHash();
    if(editor.currentBuffer && editor.currentBuffer->fileTypeCacheValid &&
       editor.currentBuffer->fileTypeCachePath == pathSv &&
       editor.currentBuffer->fileTypeCacheContentHash == contentHash)
    {
        return (editor.currentBuffer->fileTypeCacheMask &
                file_type_bit(type)) != 0;
    }

    const uint64_t mask = detectFileTypeMask(pathSv);
    if(editor.currentBuffer)
    {
        editor.currentBuffer->fileTypeCacheValid = true;
        editor.currentBuffer->fileTypeCachePath = std::string(pathSv);
        editor.currentBuffer->fileTypeCacheContentHash = contentHash;
        editor.currentBuffer->fileTypeCacheMask = mask;
    }

    return (mask & file_type_bit(type)) != 0;
}

uint64_t
EditorFileTypeController::detectFileTypeMask(std::string_view pathSv) const
{
    uint64_t mask = 0;

    auto set_if = [&](FileType type, bool matches)
    {
        if(matches)
            mask |= file_type_bit(type);
    };

    set_if(FileType::Cpp,
           constants::is_filetype<constants::no_pattern,
                                  constants::cpp_suffixes,
                                  constants::cpp_stdlib_patterns>(pathSv));
    set_if(FileType::Mla,
           constants::is_filetype<constants::no_pattern,
                                  constants::mla_suffixes>(pathSv));
    set_if(FileType::Robot,
           constants::is_filetype<constants::no_pattern,
                                  constants::robot_suffixes>(pathSv));
    set_if(FileType::Python,
           constants::is_filetype<constants::no_pattern,
                                  constants::python_suffixes>(pathSv));
    set_if(FileType::Json,
           constants::is_filetype<constants::no_pattern,
                                  constants::json_suffixes>(pathSv));
    set_if(FileType::Yaml,
           constants::is_filetype<constants::no_pattern,
                                  constants::yaml_suffixes>(pathSv));
    set_if(FileType::Toml,
           constants::is_filetype<constants::no_pattern,
                                  constants::toml_suffixes>(pathSv));
    set_if(FileType::Html,
           constants::is_filetype<constants::no_pattern,
                                  constants::html_suffixes>(pathSv));
    set_if(FileType::Css,
           constants::is_filetype<constants::no_pattern,
                                  constants::css_suffixes>(pathSv));
    set_if(FileType::JavaScript,
           constants::is_filetype<constants::no_pattern,
                                  constants::javascript_suffixes>(pathSv));
    set_if(FileType::TypeScript,
           constants::is_filetype<constants::no_pattern,
                                  constants::typescript_suffixes>(pathSv));
    set_if(FileType::Xml,
           constants::is_filetype<constants::no_pattern,
                                  constants::xml_suffixes>(pathSv));

    size_t slashPos = pathSv.find_last_of("/\\");
    std::string_view base =
        (slashPos == std::string_view::npos) ? pathSv
                                             : pathSv.substr(slashPos + 1);

    bool isReadmeMarkup = std::any_of(
        constants::markup_readme_basenames.begin(),
        constants::markup_readme_basenames.end(), [&](std::string_view name)
        { return text_utils::iequals_ascii(base, name); });
    auto dot = base.find_last_of('.');
    std::string_view ext =
        dot == std::string_view::npos ? std::string_view{} : base.substr(dot);

    bool isMarkup = isReadmeMarkup;
    if(!isMarkup && !ext.empty())
    {
        bool knownMarkupSuffix =
            constants::matches_file_patterns(ext,
                                             constants::markup_text_suffixes);
        if(knownMarkupSuffix)
        {
            if(text_utils::iequals_ascii(ext, ".rd") ||
               text_utils::iequals_ascii(ext, ".rdoc") ||
               text_utils::iequals_ascii(ext, ".md") ||
               text_utils::iequals_ascii(ext, ".markdown"))
            {
                isMarkup = true;
            }
            else if(editor.lines && !editor.lines->empty())
            {
                const int maxLines = std::min<int>(50, editor.lines->size());
                for(int i = 0; i < maxLines && !isMarkup; ++i)
                {
                    std::string_view ln{(*editor.lines)[i]};
                    if(ln.empty())
                        continue;
                    if(ln.starts_with("#") || ln.starts_with("##") ||
                       ln.starts_with("```") || ln.starts_with("~~~") ||
                       ln.starts_with("* ") || ln.starts_with("- ") ||
                       ln.starts_with("+ "))
                    {
                        isMarkup = true;
                    }
                    else if(ln.size() > 2 && text_utils::is_digit(ln[0]) &&
                            ln[1] == '.' && ln[2] == ' ')
                    {
                        isMarkup = true;
                    }
                    else if(ln.find("**") != std::string_view::npos ||
                            ln.find("`") != std::string_view::npos ||
                            (ln.find("[") != std::string_view::npos &&
                             ln.find("]") != std::string_view::npos &&
                             ln.find("(") != std::string_view::npos &&
                             ln.find(")") != std::string_view::npos))
                    {
                        isMarkup = true;
                    }
                }
            }
        }
    }
    set_if(FileType::MarkupText, isMarkup);

    bool isRdoc = isReadmeMarkup;
    if(!isRdoc && !ext.empty())
    {
        isRdoc = text_utils::iequals_ascii(ext, ".rd") ||
                 text_utils::iequals_ascii(ext, ".rdoc");
    }
    set_if(FileType::Rdoc, isRdoc);

    set_if(FileType::CMake,
           constants::is_filetype<constants::cmake_prefixes,
                                  constants::cmake_suffixes>(base));

    bool isShell = constants::is_filetype<constants::no_pattern,
                                          constants::shell_suffixes>(base);
    if(!isShell)
    {
        isShell = std::any_of(
            constants::shell_basenames.begin(),
            constants::shell_basenames.end(), [&](std::string_view name)
            { return text_utils::iequals_ascii(base, name); });
    }
    if(!isShell && editor.lines && !editor.lines->empty())
    {
        std::string_view first{(*editor.lines)[0]};
        if(first.starts_with("#!"))
        {
            isShell =
                std::any_of(constants::shell_shebang_hints.begin(),
                            constants::shell_shebang_hints.end(),
                            [&](std::string_view hint)
                            { return text_utils::contains(first, hint); });
            if(!isShell)
            {
                isShell = first.find("/sh") != std::string_view::npos ||
                          first.find(" sh") != std::string_view::npos;
            }
        }
    }
    set_if(FileType::Shell, isShell);

    return mask;
}
