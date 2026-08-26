#include "editor_utils.h"

#include "constants.h"
#include "gitignore.h"
#include "json_utils.h"
#include "text_utils.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits.h>
#include <sstream>
#include <system_error>
#include <unordered_set>

namespace editor::helper
{

TokenType parse_token_type(std::string_view value, TokenType fallback)
{
    std::string v = text_utils::ascii_lower(value);
    if(v == "normal")
        return TOKEN_NORMAL;
    if(v == "keyword")
        return TOKEN_KEYWORD;
    if(v == "type")
        return TOKEN_TYPE;
    if(v == "string")
        return TOKEN_STRING;
    if(v == "char")
        return TOKEN_CHAR;
    if(v == "comment")
        return TOKEN_COMMENT;
    if(v == "preprocessor")
        return TOKEN_PREPROCESSOR;
    if(v == "number")
        return TOKEN_NUMBER;
    if(v == "operator")
        return TOKEN_OPERATOR;
    if(v == "function")
        return TOKEN_FUNCTION;
    if(v == "member")
        return TOKEN_MEMBER;
    if(v == "namespace1" || v == "namespace_1")
        return TOKEN_NAMESPACE_1;
    if(v == "namespace2" || v == "namespace_2")
        return TOKEN_NAMESPACE_2;
    if(v == "namespace3" || v == "namespace_3")
        return TOKEN_NAMESPACE_3;
    if(v == "namespace4" || v == "namespace_4" || v == "namespace")
        return TOKEN_NAMESPACE_4;
    return fallback;
}

size_t hash_lines(const std::vector<std::string>& src)
{
    size_t h = 1469598103934665603ull;
    for(const auto& line : src)
    {
        for(unsigned char c : line)
        {
            h ^= c;
            h *= 1099511628211ull;
        }
        h ^= '\n';
        h *= 1099511628211ull;
    }
    return h;
}

std::string_view token_type_name(TokenType value)
{
    switch(value)
    {
    case TOKEN_NORMAL:
        return "normal";
    case TOKEN_KEYWORD:
        return "keyword";
    case TOKEN_TYPE:
        return "type";
    case TOKEN_STRING:
        return "string";
    case TOKEN_CHAR:
        return "char";
    case TOKEN_COMMENT:
        return "comment";
    case TOKEN_PREPROCESSOR:
        return "preprocessor";
    case TOKEN_NUMBER:
        return "number";
    case TOKEN_OPERATOR:
        return "operator";
    case TOKEN_FUNCTION:
        return "function";
    case TOKEN_MEMBER:
        return "member";
    case TOKEN_NAMESPACE_1:
        return "namespace1";
    case TOKEN_NAMESPACE_2:
        return "namespace2";
    case TOKEN_NAMESPACE_3:
        return "namespace3";
    case TOKEN_NAMESPACE_4:
        return "namespace4";
    }
    return "normal";
}

std::string_view trim_view(std::string_view value)
{
    while(!value.empty() && text_utils::is_space(value.front()))
        value.remove_prefix(1);
    while(!value.empty() && text_utils::is_space(value.back()))
        value.remove_suffix(1);
    return value;
}

bool parse_int(std::string_view value, int& out)
{
    value = trim_view(value);
    if(value.empty())
        return false;
    int result = 0;
    auto* begin = value.data();
    auto* end = value.data() + value.size();
    auto [ptr, ec] = std::from_chars(begin, end, result);
    if(ec != std::errc() || ptr != end)
        return false;
    out = result;
    return true;
}

std::string ascii_lower(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for(char c : value)
        out.push_back(text_utils::ascii_tolower(c));
    return out;
}

std::vector<std::string> split_csv(std::string_view input)
{
    std::vector<std::string> out;
    size_t start = 0;
    while(start <= input.size())
    {
        size_t comma = input.find(',', start);
        size_t end = (text_utils::is_not_found(comma)) ? input.size() : comma;
        std::string_view part = trim_view(input.substr(start, end - start));
        if(!part.empty())
            out.emplace_back(part);
        if(text_utils::is_not_found(comma))
            break;
        start = comma + 1;
    }
    return out;
}

std::string trim_ascii_ws(std::string_view s)
{
    while(!s.empty() && std::isspace((unsigned char)s.front()))
        s.remove_prefix(1);
    while(!s.empty() && std::isspace((unsigned char)s.back()))
        s.remove_suffix(1);
    return std::string(s);
}

std::string_view robot_first_cell(std::string_view line)
{
    line = trim_view(line);
    size_t space = line.find_first_of(" \t");
    if(text_utils::is_not_found(space))
        return line;
    return line.substr(0, space);
}

bool robot_keyword_section(std::string_view line)
{
    line = trim_view(line);
    if(line.empty())
        return false;
    if(!line.starts_with("***"))
        return false;
    std::string_view cell = robot_first_cell(line);
    if(cell == "***")
    {
        std::string_view rest = trim_view(line.substr(3));
        cell = robot_first_cell(rest);
    }
    return text_utils::iequals_ascii(cell, "keywords");
}

bool robot_section_header(std::string_view line)
{
    line = trim_view(line);
    if(line.empty())
        return false;
    if(!line.starts_with("***"))
        return false;
    std::string_view cell = robot_first_cell(line);
    if(cell == "***")
    {
        std::string_view rest = trim_view(line.substr(3));
        cell = robot_first_cell(rest);
    }
    return text_utils::iequals_ascii(cell, "keywords") ||
           text_utils::iequals_ascii(cell, "settings") ||
           text_utils::iequals_ascii(cell, "variables") ||
           text_utils::iequals_ascii(cell, "test") ||
           text_utils::iequals_ascii(cell, "tasks");
}

bool robot_is_keyword_def(std::string_view line)
{
    line = trim_view(line);
    if(line.empty())
        return false;
    if(line.starts_with("***"))
        return false;
    if(line.starts_with("#"))
        return false;
    if(line.starts_with("|"))
    {
        line = trim_view(line.substr(1));
        size_t bar = line.find('|');
        if(text_utils::is_found(bar))
            line = trim_view(line.substr(0, bar));
    }
    if(line.empty())
        return false;
    return !std::isspace((unsigned char)line.front());
}

bool python_def_line(std::string_view line, std::string_view symbol)
{
    std::string_view trimmed = trim_view(line);
    if(!trimmed.starts_with("def "))
        return false;
    trimmed = trim_view(trimmed.substr(4));
    if(!trimmed.starts_with(symbol))
        return false;
    if(trimmed.size() == symbol.size())
        return true;
    char next = trimmed[symbol.size()];
    return next == '(' || std::isspace((unsigned char)next);
}

int find_word_pos(std::string_view line, std::string_view word)
{
    if(word.empty())
        return -1;
    size_t pos = line.find(word);
    while(text_utils::is_found(pos))
    {
        bool leftOk = (pos == 0 || !std::isalnum((unsigned char)line[pos - 1]));
        bool rightOk = (pos + word.size() >= line.size() ||
                        !std::isalnum((unsigned char)line[pos + word.size()]));
        if(leftOk && rightOk)
            return (int)pos;
        pos = line.find(word, pos + word.size());
    }
    return -1;
}

bool js_ts_decl_line(std::string_view line, std::string_view symbol, int& outX)
{
    std::string_view trimmed = trim_view(line);
    if(trimmed.empty())
        return false;
    if(trimmed.starts_with("//"))
        return false;

    if(trimmed.starts_with("export "))
        trimmed = trim_view(trimmed.substr(7));
    if(trimmed.starts_with("default "))
        trimmed = trim_view(trimmed.substr(8));
    if(trimmed.starts_with("async "))
        trimmed = trim_view(trimmed.substr(6));

    static constexpr std::string_view kDecls[] = {
        "function", "class", "interface", "type", "enum", "const", "let", "var",
    };
    for(auto kw : kDecls)
    {
        if(trimmed.starts_with(kw))
        {
            size_t end = kw.size();
            if(end < trimmed.size())
            {
                if(kw == "function" && trimmed[end] == '*')
                {
                    end++;
                }
                else if(!text_utils::is_space(trimmed[end]))
                {
                    continue;
                }
            }
            trimmed = trim_view(trimmed.substr(end));
            if(trimmed.empty())
                return false;
            size_t i = 0;
            while(
                i < trimmed.size() &&
                (std::isalnum((unsigned char)trimmed[i]) || trimmed[i] == '_'))
                ++i;
            if(i == 0)
                return false;
            std::string_view name = trimmed.substr(0, i);
            if(text_utils::iequals_ascii(name, symbol))
            {
                int pos = find_word_pos(line, name);
                if(pos >= 0)
                    outX = pos;
                else
                    outX = 0;
                return true;
            }
            return false;
        }
    }

    return false;
}

bool find_js_ts_def_in_file(const std::string& path, std::string_view symbol,
                            int& outY, int& outX)
{
    std::ifstream file(path);
    if(!file.is_open())
        return false;

    std::string line;
    int lineNo = 0;
    while(std::getline(file, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();
        if(js_ts_decl_line(line, symbol, outX))
        {
            outY = lineNo;
            return true;
        }
        lineNo++;
    }
    return false;
}

bool load_json_file(const std::filesystem::path& path,
                    json_utils::Document& doc)
{
    std::ifstream input(path);
    if(!input.is_open())
        return false;
    return json_utils::parse(doc, input);
}

bool load_tsconfig_paths(const std::filesystem::path& startDir,
                         TsConfigPaths& out)
{
    std::error_code ec;
    std::filesystem::path dir = startDir;
    std::filesystem::path root = dir.root_path();
    while(true)
    {
        std::filesystem::path tsconfig = dir / "tsconfig.json";
        if(std::filesystem::exists(tsconfig, ec) && !ec)
        {
            json_utils::Document doc;
            if(load_json_file(tsconfig, doc) && doc.IsObject())
            {
                out.dir = dir;
                out.baseUrl.clear();
                out.paths.clear();
                const auto* compilerOptions =
                    json_utils::find(doc, "compilerOptions");
                if(compilerOptions && compilerOptions->IsObject())
                {
                    std::string baseUrlStr =
                        json_utils::get_string(*compilerOptions, "baseUrl", "");
                    if(!baseUrlStr.empty())
                        out.baseUrl = baseUrlStr;
                    const auto* paths =
                        json_utils::find(*compilerOptions, "paths");
                    if(paths && paths->IsObject())
                    {
                        for(auto it = paths->MemberBegin();
                            it != paths->MemberEnd(); ++it)
                        {
                            if(!it->name.IsString() || !it->value.IsArray())
                                continue;
                            std::string key(it->name.GetString(),
                                            it->name.GetStringLength());
                            std::vector<std::string> vals;
                            for(const auto& v : it->value.GetArray())
                            {
                                if(v.IsString())
                                {
                                    vals.emplace_back(v.GetString(),
                                                      v.GetStringLength());
                                }
                            }
                            if(!key.empty() && !vals.empty())
                                out.paths.emplace_back(std::move(key),
                                                       std::move(vals));
                        }
                    }
                }
                return !out.baseUrl.empty() || !out.paths.empty();
            }
        }

        std::filesystem::path jsconfig = dir / "jsconfig.json";
        if(std::filesystem::exists(jsconfig, ec) && !ec)
        {
            json_utils::Document doc;
            if(load_json_file(jsconfig, doc) && doc.IsObject())
            {
                out.dir = dir;
                out.baseUrl.clear();
                out.paths.clear();
                const auto* compilerOptions =
                    json_utils::find(doc, "compilerOptions");
                if(compilerOptions && compilerOptions->IsObject())
                {
                    std::string baseUrlStr =
                        json_utils::get_string(*compilerOptions, "baseUrl", "");
                    if(!baseUrlStr.empty())
                        out.baseUrl = baseUrlStr;
                    const auto* paths =
                        json_utils::find(*compilerOptions, "paths");
                    if(paths && paths->IsObject())
                    {
                        for(auto it = paths->MemberBegin();
                            it != paths->MemberEnd(); ++it)
                        {
                            if(!it->name.IsString() || !it->value.IsArray())
                                continue;
                            std::string key(it->name.GetString(),
                                            it->name.GetStringLength());
                            std::vector<std::string> vals;
                            for(const auto& v : it->value.GetArray())
                            {
                                if(v.IsString())
                                {
                                    vals.emplace_back(v.GetString(),
                                                      v.GetStringLength());
                                }
                            }
                            if(!key.empty() && !vals.empty())
                                out.paths.emplace_back(std::move(key),
                                                       std::move(vals));
                        }
                    }
                }
                return !out.baseUrl.empty() || !out.paths.empty();
            }
        }

        if(dir == root || !dir.has_parent_path())
            break;
        dir = dir.parent_path();
    }
    return false;
}

std::vector<std::string> expand_tsconfig_paths(const TsConfigPaths& cfg,
                                               std::string_view module)
{
    std::vector<std::string> out;
    for(const auto& entry : cfg.paths)
    {
        const std::string& pattern = entry.first;
        size_t star = pattern.find('*');
        if(text_utils::is_not_found(star))
        {
            if(pattern == module)
            {
                for(const auto& target : entry.second)
                    out.push_back(target);
            }
            continue;
        }
        std::string_view prefix(pattern.data(), star);
        std::string_view suffix(pattern.data() + star + 1,
                                pattern.size() - star - 1);
        if(module.size() < prefix.size() + suffix.size())
            continue;
        if(!module.starts_with(prefix) || !module.ends_with(suffix))
            continue;
        std::string_view captured = module.substr(
            prefix.size(), module.size() - prefix.size() - suffix.size());
        for(const auto& target : entry.second)
        {
            size_t tgtStar = target.find('*');
            if(text_utils::is_not_found(tgtStar))
            {
                out.push_back(target);
            }
            else
            {
                std::string resolved = target;
                resolved.replace(tgtStar, 1, captured);
                out.push_back(std::move(resolved));
            }
        }
    }
    return out;
}

bool extract_js_ts_module_specifier(std::string_view line,
                                    std::string_view& outSpec)
{
    std::string_view trimmed = trim_view(line);
    if(!(trimmed.starts_with("import ") || trimmed.starts_with("export ")))
        return false;

    size_t fromPos = trimmed.find(" from ");
    size_t start = text_utils::npos();
    size_t end = text_utils::npos();
    char quote = 0;

    if(text_utils::is_found(fromPos))
    {
        size_t q = trimmed.find_first_of("\"'", fromPos);
        if(text_utils::is_not_found(q))
            return false;
        quote = trimmed[q];
        start = q + 1;
        end = trimmed.find(quote, start);
    }
    else if(trimmed.starts_with("import "))
    {
        size_t q = trimmed.find_first_of("\"'", 6);
        if(text_utils::is_not_found(q))
            return false;
        quote = trimmed[q];
        start = q + 1;
        end = trimmed.find(quote, start);
    }

    if(text_utils::is_not_found(start) || text_utils::is_not_found(end) ||
       end <= start)
    {
        return false;
    }

    outSpec = trimmed.substr(start, end - start);
    return true;
}

static void
parse_js_ts_named_imports(std::string_view list, const std::string& module,
                          std::unordered_map<std::string, std::string>& out)
{
    list = trim_view(list);
    if(list.starts_with("type "))
        list = trim_view(list.substr(5));
    size_t start = 0;
    while(start < list.size())
    {
        size_t comma = list.find(',', start);
        if(text_utils::is_not_found(comma))
            comma = list.size();
        std::string_view item = trim_view(list.substr(start, comma - start));
        if(!item.empty())
        {
            if(item.starts_with("type "))
                item = trim_view(item.substr(5));
            size_t asPos = item.find(" as ");
            std::string_view name = (text_utils::is_not_found(asPos))
                                        ? item
                                        : trim_view(item.substr(asPos + 4));
            if(!name.empty())
                out.emplace(std::string(name), module);
        }
        start = comma + 1;
    }
}

void collect_js_ts_imports(
    const std::vector<std::string>& lines,
    std::unordered_map<std::string, std::string>& symbolToModule)
{
    for(const auto& line : lines)
    {
        std::string_view trimmed = trim_view(line);
        if(!(trimmed.starts_with("import ") || trimmed.starts_with("export ")))
            continue;

        std::string_view module;
        if(!extract_js_ts_module_specifier(trimmed, module))
            continue;

        std::string moduleStr(module);
        if(trimmed.starts_with("import "))
        {
            size_t fromPos = trimmed.find(" from ");
            std::string_view head =
                (text_utils::is_not_found(fromPos))
                    ? trim_view(trimmed.substr(6))
                    : trim_view(trimmed.substr(6, fromPos - 6));
            if(head.starts_with("type "))
                head = trim_view(head.substr(5));

            if(head.starts_with("{"))
            {
                size_t end = head.find('}');
                if(text_utils::is_found(end))
                {
                    parse_js_ts_named_imports(head.substr(1, end - 1),
                                              moduleStr, symbolToModule);
                }
            }
            else if(head.starts_with("*"))
            {
                size_t asPos = head.find(" as ");
                if(text_utils::is_found(asPos))
                {
                    std::string_view name = trim_view(head.substr(asPos + 4));
                    if(!name.empty())
                        symbolToModule.emplace(std::string(name), moduleStr);
                }
            }
            else if(!head.empty())
            {
                size_t comma = head.find(',');
                std::string_view defaultName = trim_view(head.substr(0, comma));
                if(!defaultName.empty())
                    symbolToModule.emplace(std::string(defaultName), moduleStr);
                if(text_utils::is_found(comma))
                {
                    std::string_view rest = trim_view(head.substr(comma + 1));
                    if(rest.starts_with("{"))
                    {
                        size_t end = rest.find('}');
                        if(text_utils::is_found(end))
                        {
                            parse_js_ts_named_imports(rest.substr(1, end - 1),
                                                      moduleStr,
                                                      symbolToModule);
                        }
                    }
                    else if(rest.starts_with("*"))
                    {
                        size_t asPos = rest.find(" as ");
                        if(text_utils::is_found(asPos))
                        {
                            std::string_view name =
                                trim_view(rest.substr(asPos + 4));
                            if(!name.empty())
                                symbolToModule.emplace(std::string(name),
                                                       moduleStr);
                        }
                    }
                }
            }
        }
        else if(trimmed.starts_with("export "))
        {
            size_t fromPos = trimmed.find(" from ");
            if(text_utils::is_not_found(fromPos))
                continue;
            std::string_view head = trim_view(trimmed.substr(6, fromPos - 6));
            if(head.starts_with("type "))
                head = trim_view(head.substr(5));
            if(head.starts_with("{"))
            {
                size_t end = head.find('}');
                if(text_utils::is_found(end))
                {
                    parse_js_ts_named_imports(head.substr(1, end - 1),
                                              moduleStr, symbolToModule);
                }
            }
        }
    }
}

std::string resolve_js_ts_from_dir(const std::filesystem::path& baseDir,
                                   std::string_view module)
{
    std::string spec(module);
    if(!spec.empty() && spec.front() != '.' && spec.front() != '/')
        spec = "./" + spec;
    std::filesystem::path dummy = baseDir / "__uvim__";
    return resolve_js_ts_module_path(dummy.string(), spec);
}

std::string resolve_node_module(const std::string& fromFile,
                                std::string_view module)
{
    std::string moduleStr(module);
    if(moduleStr.empty())
        return {};
    if(moduleStr.front() == '.' || moduleStr.front() == '/')
        return {};

    std::string pkg;
    std::string subpath;
    if(moduleStr.starts_with("@"))
    {
        size_t first = moduleStr.find('/');
        if(text_utils::is_not_found(first))
            return {};
        size_t second = moduleStr.find('/', first + 1);
        if(text_utils::is_not_found(second))
        {
            pkg = moduleStr;
        }
        else
        {
            pkg = moduleStr.substr(0, second);
            subpath = moduleStr.substr(second + 1);
        }
    }
    else
    {
        size_t slash = moduleStr.find('/');
        if(text_utils::is_not_found(slash))
        {
            pkg = moduleStr;
        }
        else
        {
            pkg = moduleStr.substr(0, slash);
            subpath = moduleStr.substr(slash + 1);
        }
    }

    std::filesystem::path dir = std::filesystem::path(fromFile).parent_path();
    std::filesystem::path root = dir.root_path();
    std::error_code ec;
    while(true)
    {
        std::filesystem::path base = dir / "node_modules" / pkg;
        std::filesystem::path candidate = base;
        if(!subpath.empty())
            candidate /= subpath;

        if(std::filesystem::exists(candidate, ec) && !ec)
        {
            if(std::filesystem::is_regular_file(candidate, ec))
                return candidate.string();
            if(std::filesystem::is_directory(candidate, ec))
            {
                std::string attempt =
                    resolve_js_ts_from_dir(candidate, "./index");
                if(!attempt.empty())
                    return attempt;
            }
        }
        if(!subpath.empty())
        {
            std::string attempt = resolve_js_ts_from_dir(base, "./" + subpath);
            if(!attempt.empty())
                return attempt;
        }

        if(std::filesystem::exists(base, ec) && !ec &&
           std::filesystem::is_directory(base, ec) && subpath.empty())
        {
            std::filesystem::path pkgJson = base / "package.json";
            if(std::filesystem::exists(pkgJson, ec) && !ec)
            {
                json_utils::Document doc;
                if(load_json_file(pkgJson, doc) && doc.IsObject())
                {
                    std::string entry =
                        json_utils::get_string(doc, "types", "");
                    if(entry.empty())
                        entry = json_utils::get_string(doc, "typings", "");
                    if(entry.empty())
                        entry = json_utils::get_string(doc, "module", "");
                    if(entry.empty())
                        entry = json_utils::get_string(doc, "main", "");
                    if(!entry.empty())
                    {
                        std::string resolved =
                            resolve_js_ts_from_dir(base, entry);
                        if(!resolved.empty())
                            return resolved;
                    }
                }
            }
            std::string fallback = resolve_js_ts_from_dir(base, "./index");
            if(!fallback.empty())
                return fallback;
        }

        if(dir == root || !dir.has_parent_path())
            break;
        dir = dir.parent_path();
    }

    return {};
}

std::string resolve_js_ts_module(const std::string& fromFile,
                                 std::string_view module)
{
    if(module.empty())
        return {};
    if(module.front() == '.' || module.front() == '/')
        return resolve_js_ts_module_path(fromFile, module);

    TsConfigPaths cfg;
    std::filesystem::path startDir =
        std::filesystem::path(fromFile).parent_path();
    if(load_tsconfig_paths(startDir, cfg))
    {
        auto candidates = expand_tsconfig_paths(cfg, module);
        if(!candidates.empty())
        {
            std::filesystem::path baseDir = cfg.dir;
            if(!cfg.baseUrl.empty())
                baseDir /= cfg.baseUrl;
            for(const auto& candidate : candidates)
            {
                std::string resolved =
                    resolve_js_ts_from_dir(baseDir, candidate);
                if(!resolved.empty())
                    return resolved;
            }
        }
        if(!cfg.baseUrl.empty())
        {
            std::filesystem::path baseDir = cfg.dir / cfg.baseUrl;
            std::string resolved = resolve_js_ts_from_dir(baseDir, module);
            if(!resolved.empty())
                return resolved;
        }
    }

    return resolve_node_module(fromFile, module);
}

std::string resolve_js_ts_module_path(const std::string& fromFile,
                                      std::string_view module)
{
    if(module.empty())
        return {};
    if(module.front() != '.' && module.front() != '/')
        return {};

    std::filesystem::path base = std::filesystem::path(fromFile).parent_path();
    std::filesystem::path candidate =
        (module.front() == '/') ? std::filesystem::path(module) : base / module;

    std::error_code ec;
    auto try_file = [&](const std::filesystem::path& path) -> std::string
    {
        if(std::filesystem::exists(path, ec) && !ec &&
           std::filesystem::is_regular_file(path, ec))
            return path.string();
        return {};
    };

    std::string found = try_file(candidate);
    if(!found.empty())
        return found;

    static constexpr std::string_view kExts[] = {
        ".ts", ".tsx", ".js", ".jsx", ".mjs", ".cjs", ".d.ts", ".json",
    };

    if(!candidate.has_extension())
    {
        for(auto ext : kExts)
        {
            std::string attempt =
                try_file(candidate.string() + std::string(ext));
            if(!attempt.empty())
                return attempt;
        }
    }
    else
    {
        std::filesystem::path stemPath = candidate;
        stemPath.replace_extension();
        std::string stem = stemPath.string();
        for(auto ext : kExts)
        {
            std::string attempt = try_file(stem + std::string(ext));
            if(!attempt.empty())
                return attempt;
        }
    }

    if(std::filesystem::exists(candidate, ec) && !ec &&
       std::filesystem::is_directory(candidate, ec))
    {
        for(auto ext : kExts)
        {
            std::string attempt =
                try_file(candidate / ("index" + std::string(ext)));
            if(!attempt.empty())
                return attempt;
        }
    }

    return {};
}

static bool isIdent(char c)
{
    return std::isalnum((unsigned char)c) || c == '_';
}

std::string parse_ts_type_name(std::string_view text)
{
    text = trim_view(text);
    if(text.empty())
        return {};
    if(text.front() == '{' || text.front() == '(')
        return {};

    size_t i = 0;
    while(i < text.size() && (isIdent(text[i]) || text[i] == '.'))
        ++i;
    if(i == 0)
        return {};

    std::string_view head = text.substr(0, i);
    if(head == "Array" || head == "ReadonlyArray")
    {
        size_t lt = text.find('<', i);
        if(text_utils::is_found(lt))
        {
            std::string inner = parse_ts_type_name(text.substr(lt + 1));
            if(!inner.empty())
                return inner;
        }
    }
    return std::string(head);
}

std::string find_ts_type_for_identifier(const std::vector<std::string>& lines,
                                        std::string_view ident, int startY)
{
    for(int y = startY; y >= 0; --y)
    {
        const std::string& line = lines[y];
        auto matches = text_utils::find_cursor(line, ident);
        size_t pos = 0;
        while(matches.next(pos))
        {
            bool leftOk = (pos == 0) || !isIdent(line[pos - 1]);
            size_t end = pos + ident.size();
            bool rightOk = (end >= line.size()) || !isIdent(line[end]);
            if(!leftOk || !rightOk)
            {
                continue;
            }

            size_t after = end;
            if(after < line.size() && line[after] == '?')
                ++after;
            while(after < line.size() && text_utils::is_space(line[after]))
                ++after;
            if(after < line.size() && line[after] == ':')
            {
                ++after;
                while(after < line.size() && text_utils::is_space(line[after]))
                    ++after;
                std::string type =
                    parse_ts_type_name(std::string_view(line).substr(after));
                if(!type.empty())
                    return type;
            }
        }
    }
    return {};
}

std::string infer_ts_type_from_array_method_line(
    std::string_view line, std::string_view param,
    const std::vector<std::string>& lines, int lineNo)
{
    static constexpr std::string_view kMethods[] = {
        "find", "map", "filter", "some", "every", "reduce",
    };

    for(auto method : kMethods)
    {
        std::string needle = "." + std::string(method) + "(";
        auto matches = text_utils::find_cursor(line, needle);
        size_t pos = 0;
        while(matches.next(pos))
        {
            int dotPos = (int)pos;
            int nameEnd = dotPos - 1;
            while(nameEnd >= 0 && text_utils::is_space(line[nameEnd]))
                --nameEnd;
            int nameStart = nameEnd;
            while(nameStart >= 0 && isIdent(line[nameStart]))
                --nameStart;
            ++nameStart;
            if(nameStart > nameEnd)
            {
                continue;
            }

            std::string_view arrayName =
                line.substr(nameStart, nameEnd - nameStart + 1);
            size_t argsStart = pos + needle.size();
            while(argsStart < line.size() &&
                  text_utils::is_space(line[argsStart]))
                ++argsStart;

            std::string_view paramName;
            if(argsStart < line.size() && line[argsStart] == '(')
            {
                ++argsStart;
                while(argsStart < line.size() &&
                      text_utils::is_space(line[argsStart]))
                    ++argsStart;
                size_t i = argsStart;
                while(i < line.size() && isIdent(line[i]))
                    ++i;
                if(i > argsStart)
                    paramName = line.substr(argsStart, i - argsStart);
            }
            else
            {
                size_t i = argsStart;
                while(i < line.size() && isIdent(line[i]))
                    ++i;
                if(i > argsStart)
                    paramName = line.substr(argsStart, i - argsStart);
            }

            if(!paramName.empty() && paramName == param)
            {
                return find_ts_type_for_identifier(lines, arrayName, lineNo);
            }

            pos += needle.size();
        }
    }

    return {};
}

bool find_ts_type_definition(const std::vector<std::string>& lines,
                             std::string_view typeName, int& outY, int& outX)
{
    static constexpr std::string_view kDecls[] = {"type", "interface", "class"};
    for(size_t y = 0; y < lines.size(); ++y)
    {
        std::string_view line = lines[y];
        for(auto kw : kDecls)
        {
            size_t pos = line.find(kw);
            while(text_utils::is_found(pos))
            {
                bool leftOk = (pos == 0) || !isIdent(line[pos - 1]);
                size_t end = pos + kw.size();
                bool rightOk = (end >= line.size()) || !isIdent(line[end]);
                if(leftOk && rightOk)
                {
                    size_t i = end;
                    while(i < line.size() && text_utils::is_space(line[i]))
                        ++i;
                    size_t nameStart = i;
                    while(i < line.size() &&
                          (isIdent(line[i]) || line[i] == '.'))
                        ++i;
                    if(i > nameStart)
                    {
                        std::string_view name =
                            line.substr(nameStart, i - nameStart);
                        if(name == typeName)
                        {
                            outY = (int)y;
                            outX = (int)nameStart;
                            return true;
                        }
                    }
                }
                pos = line.find(kw, pos + 1);
            }
        }
    }
    return false;
}

bool find_ts_member_in_type(const std::vector<std::string>& lines,
                            int typeStartY, std::string_view member, int& outY,
                            int& outX)
{
    int depth = 0;
    bool sawOpen = false;
    for(size_t y = typeStartY; y < lines.size(); ++y)
    {
        const std::string& line = lines[y];
        for(char c : line)
        {
            if(c == '{')
            {
                depth++;
                sawOpen = true;
            }
            else if(c == '}' && depth > 0)
            {
                depth--;
                if(sawOpen && depth == 0)
                    return false;
            }
        }

        if(!sawOpen)
            continue;

        auto matches = text_utils::find_cursor(line, member);
        size_t pos = 0;
        while(matches.next(pos))
        {
            bool leftOk = (pos == 0) || !isIdent(line[pos - 1]);
            size_t end = pos + member.size();
            bool rightOk = (end >= line.size()) || !isIdent(line[end]);
            if(!leftOk || !rightOk)
            {
                continue;
            }

            size_t after = end;
            if(after < line.size() && line[after] == '?')
                ++after;
            while(after < line.size() && text_utils::is_space(line[after]))
                ++after;
            if(after < line.size() &&
               (line[after] == ':' || line[after] == '('))
            {
                outY = (int)y;
                outX = (int)pos;
                return true;
            }
        }
    }
    return false;
}

bool html_path_under_cursor(std::string_view line, int cursorX,
                            std::string_view& outPath)
{
    std::string_view trimmed = trim_view(line);
    if(trimmed.empty() || trimmed[0] != '<')
        return false;

    auto check_attr = [&](std::string_view attr) -> bool
    {
        size_t pos = trimmed.find(attr);
        if(text_utils::is_not_found(pos))
            return false;
        size_t eq = trimmed.find('=', pos + attr.size());
        if(text_utils::is_not_found(eq))
            return false;
        size_t q = trimmed.find_first_of("\"'", eq + 1);
        if(text_utils::is_not_found(q))
            return false;
        char quote = trimmed[q];
        size_t end = trimmed.find(quote, q + 1);
        if(text_utils::is_not_found(end) || end <= q + 1)
            return false;
        int startX = static_cast<int>(q + 1 + (trimmed.data() - line.data()));
        int endX = static_cast<int>(end + (trimmed.data() - line.data()));
        if(cursorX >= startX && cursorX <= endX)
        {
            outPath = trimmed.substr(q + 1, end - q - 1);
            return true;
        }
        return false;
    };

    if(check_attr("href"))
        return true;
    if(check_attr("src"))
        return true;
    return false;
}

std::vector<std::string>
extract_html_stylesheets(const std::vector<std::string>& lines)
{
    std::vector<std::string> out;
    for(const auto& line : lines)
    {
        std::string lower = ascii_lower(line);
        if(!text_utils::contains(lower, "<link"))
            continue;
        if(!text_utils::contains(lower, "stylesheet"))
            continue;
        size_t hrefPos = lower.find("href");
        if(text_utils::is_not_found(hrefPos))
            continue;
        size_t eq = line.find('=', hrefPos);
        if(text_utils::is_not_found(eq))
            continue;
        size_t q = line.find_first_of("\"'", eq + 1);
        if(text_utils::is_not_found(q))
            continue;
        char quote = line[q];
        size_t end = line.find(quote, q + 1);
        if(text_utils::is_not_found(end) || end <= q + 1)
            continue;
        std::string path = line.substr(q + 1, end - q - 1);
        if(!path.empty())
            out.push_back(path);
    }
    return out;
}

bool find_css_selector_in_file(const std::string& path,
                               std::string_view selector, int& outY, int& outX)
{
    std::ifstream file(path);
    if(!file.is_open())
        return false;

    std::string line;
    int lineNo = 0;
    while(std::getline(file, line))
    {
        int pos = find_word_pos(line, selector);
        if(pos >= 0)
        {
            outY = lineNo;
            outX = pos;
            return true;
        }
        lineNo++;
    }
    return false;
}

bool css_import_path_under_cursor(std::string_view line, int cursorX,
                                  std::string_view& outPath)
{
    std::string_view trimmed = trim_view(line);
    if(!trimmed.starts_with("@import"))
        return false;
    size_t q = trimmed.find_first_of("\"'");
    if(text_utils::is_not_found(q))
        return false;
    char quote = trimmed[q];
    size_t end = trimmed.find(quote, q + 1);
    if(text_utils::is_not_found(end) || end <= q + 1)
        return false;
    int startX = static_cast<int>(q + 1 + (trimmed.data() - line.data()));
    int endX = static_cast<int>(end + (trimmed.data() - line.data()));
    if(cursorX >= startX && cursorX <= endX)
    {
        outPath = trimmed.substr(q + 1, end - q - 1);
        return true;
    }
    return false;
}

bool find_robot_keyword_in_file(const std::string& path,
                                std::string_view keyword, int& outY, int& outX)
{
    std::ifstream file(path);
    if(!file.is_open())
        return false;

    std::string line;
    bool inKeywords = false;
    int lineNo = 0;
    while(std::getline(file, line))
    {
        std::string_view view{line};
        if(robot_section_header(view))
        {
            inKeywords = robot_keyword_section(view);
            lineNo++;
            continue;
        }
        if(inKeywords && robot_is_keyword_def(view))
        {
            std::string_view name = robot_first_cell(view);
            if(!name.empty() && text_utils::iequals_ascii(name, keyword))
            {
                outY = lineNo;
                outX = 0;
                return true;
            }
        }
        lineNo++;
    }
    return false;
}

bool find_python_def_in_file(const std::string& path, std::string_view symbol,
                             int& outY, int& outX)
{
    std::ifstream file(path);
    if(!file.is_open())
        return false;

    std::string line;
    int lineNo = 0;
    while(std::getline(file, line))
    {
        std::string_view view{line};
        if(python_def_line(view, symbol))
        {
            outY = lineNo;
            outX = 0;
            return true;
        }
        lineNo++;
    }
    return false;
}

bool is_skip_dir(const std::filesystem::path& path)
{
    std::string name = path.filename().string();
    return name == ".git" || name == ".venv" || name == "build" ||
           name == "node_modules" || name == "dist" || name == "out";
}

bool locIsBinaryFile(const std::string& filepath)
{
    std::ifstream file(filepath, std::ios::binary);
    if(!file)
        return true;

    char buffer[512];
    file.read(buffer, sizeof(buffer));
    std::streamsize bytesRead = file.gcount();

    int nullCount = 0;
    int nonPrintable = 0;

    for(std::streamsize i = 0; i < bytesRead; i++)
    {
        unsigned char c = static_cast<unsigned char>(buffer[i]);

        if(c == 0)
        {
            nullCount++;
            if(nullCount > 1)
                return true;
        }

        if(c < 7 || (c > 14 && c < 32))
        {
            nonPrintable++;
            if(nonPrintable > bytesRead / 10)
                return true;
        }
    }

    return false;
}

bool locIsTextFile(const std::string& filepath)
{
    std::string ext;
    size_t dotPos = filepath.find_last_of('.');
    if(text_utils::is_found(dotPos))
    {
        ext = filepath.substr(dotPos);
        bool isPythonExt =
            constants::is_filetype<constants::no_pattern,
                                   constants::python_suffixes>(filepath);
        bool isMlaExt =
            constants::is_filetype<constants::no_pattern,
                                   constants::mla_suffixes>(filepath);

        if(ext == ".txt" || ext == ".cpp" || ext == ".c" || ext == ".h" ||
           ext == ".hpp" || isPythonExt || ext == ".js" || ext == ".ts" ||
           ext == ".jsx" || ext == ".tsx" || ext == ".java" || ext == ".rs" ||
           ext == ".go" || ext == ".rb" || ext == ".php" || ext == ".sh" ||
           ext == ".bash" || ext == ".zsh" || ext == ".vim" || ext == ".lua" ||
           ext == ".md" || ext == ".markdown" || ext == ".rst" ||
           ext == ".tex" || ext == ".css" || ext == ".scss" || ext == ".html" ||
           ext == ".xml" || ext == ".json" || ext == ".yaml" || ext == ".yml" ||
           ext == ".toml" || ext == ".ini" || ext == ".conf" ||
           ext == ".config" || ext == ".log" || ext == ".cmake" ||
           ext == ".make" || ext == ".mk" || ext == ".am" || isMlaExt)
        {
            return true;
        }

        if(ext == ".exe" || ext == ".o" || ext == ".so" || ext == ".a" ||
           ext == ".dll" || ext == ".dylib" || ext == ".bin" || ext == ".dat" ||
           ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" ||
           ext == ".bmp" || ext == ".ico" || ext == ".pdf" || ext == ".doc" ||
           ext == ".docx" || ext == ".xls" || ext == ".xlsx" || ext == ".ppt" ||
           ext == ".pptx" || ext == ".zip" || ext == ".tar" || ext == ".gz" ||
           ext == ".bz2" || ext == ".7z" || ext == ".rar" || ext == ".mp3" ||
           ext == ".mp4" || ext == ".avi" || ext == ".mov" || ext == ".wav" ||
           ext == ".flac" || ext == ".ogg" || ext == ".ttf" || ext == ".otf" ||
           ext == ".woff" || ext == ".woff2" || ext == ".eot")
        {
            return false;
        }
    }

    return !locIsBinaryFile(filepath);
}

LocCommentRules locCommentRulesForPath(std::string_view path)
{
    LocCommentRules rules;
    auto lower_ascii = [](std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        for(char c : text)
            out.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return out;
    };
    std::string baseLower = lower_ascii(text_utils::basename(path));
    std::string extLower;
    size_t dotPos = baseLower.find_last_of('.');
    if(text_utils::is_found(dotPos))
        extLower = baseLower.substr(dotPos);

    if(baseLower.rfind("cmakelists", 0) == 0 ||
       baseLower.rfind("cmakecache", 0) == 0 ||
       baseLower.rfind("cmakefiles", 0) == 0 || (extLower == ".cmake"))
    {
        rules.line = "#";
        rules.hasLine = true;
        return rules;
    }

    bool isCpp =
        constants::is_filetype<constants::no_pattern, constants::cpp_suffixes>(
            path);
    bool isMla =
        constants::is_filetype<constants::no_pattern, constants::mla_suffixes>(
            path);
    bool isRust =
        constants::is_filetype<constants::no_pattern, constants::rust_suffixes>(
            path);
    bool isGo =
        constants::is_filetype<constants::no_pattern, constants::go_suffixes>(
            path);
    bool isJs = constants::is_filetype<constants::no_pattern,
                                       constants::javascript_suffixes>(path);
    bool isTs = constants::is_filetype<constants::no_pattern,
                                       constants::typescript_suffixes>(path);
    bool isCss =
        constants::is_filetype<constants::no_pattern, constants::css_suffixes>(
            path);
    bool isHtml =
        constants::is_filetype<constants::no_pattern, constants::html_suffixes>(
            path);
    bool isXml =
        constants::is_filetype<constants::no_pattern, constants::xml_suffixes>(
            path);
    bool isPython = constants::is_filetype<constants::no_pattern,
                                           constants::python_suffixes>(path);
    bool isRobot = constants::is_filetype<constants::no_pattern,
                                          constants::robot_suffixes>(path);
    bool isYaml =
        constants::is_filetype<constants::no_pattern, constants::yaml_suffixes>(
            path);
    bool isToml =
        constants::is_filetype<constants::no_pattern, constants::toml_suffixes>(
            path);
    bool isCMake = constants::is_filetype<constants::cmake_prefixes,
                                          constants::cmake_suffixes>(path);
    bool isShell = constants::is_filetype<constants::no_pattern,
                                          constants::shell_suffixes>(path);
    bool isMarkup =
        constants::is_filetype<constants::no_pattern,
                               constants::markup_text_suffixes>(path);
    bool isIni = (extLower == ".ini" || extLower == ".conf" ||
                  extLower == ".config" || extLower == ".cfg");

    if(isCpp || isMla || isRust || isGo || isJs || isTs)
    {
        rules.line = "//";
        rules.blockStart = "/*";
        rules.blockEnd = "*/";
        rules.hasLine = true;
        rules.hasBlock = true;
        return rules;
    }

    if(isCss)
    {
        rules.blockStart = "/*";
        rules.blockEnd = "*/";
        rules.hasBlock = true;
        return rules;
    }

    if(isHtml || isXml || isMarkup)
    {
        rules.blockStart = "<!--";
        rules.blockEnd = "-->";
        rules.hasBlock = true;
        return rules;
    }

    if(isPython || isRobot || isYaml || isToml || isCMake || isShell || isIni)
    {
        rules.line = "#";
        rules.hasLine = true;
        return rules;
    }

    return rules;
}

int locCountInFile(const std::string& filepath, const LocCommentRules& rules)
{
    std::ifstream file(filepath);
    if(!file)
        return 0;

    std::string line;
    int count = 0;
    bool inBlock = false;

    while(std::getline(file, line))
    {
        if(!line.empty() && line.back() == '\r')
            line.pop_back();

        std::string_view view = line;
        size_t pos = 0;
        bool counted = false;

        while(true)
        {
            while(pos < view.size() &&
                  std::isspace(static_cast<unsigned char>(view[pos])))
            {
                ++pos;
            }

            if(pos >= view.size())
                break;

            if(inBlock)
            {
                if(!rules.hasBlock)
                    break;
                size_t end = view.find(rules.blockEnd, pos);
                if(text_utils::is_not_found(end))
                {
                    pos = view.size();
                    break;
                }
                pos = end + rules.blockEnd.size();
                inBlock = false;
                continue;
            }

            if(rules.hasLine &&
               view.compare(pos, rules.line.size(), rules.line) == 0)
            {
                pos = view.size();
                break;
            }

            if(rules.hasBlock && view.compare(pos, rules.blockStart.size(),
                                              rules.blockStart) == 0)
            {
                size_t end =
                    view.find(rules.blockEnd, pos + rules.blockStart.size());
                if(text_utils::is_not_found(end))
                {
                    inBlock = true;
                    pos = view.size();
                    break;
                }
                pos = end + rules.blockEnd.size();
                continue;
            }

            counted = true;
            break;
        }

        if(counted)
            count++;
    }

    return count;
}

int locCountInLines(const std::vector<std::string>& lines,
                    const LocCommentRules& rules)
{
    int count = 0;
    bool inBlock = false;

    for(const auto& lineRef : lines)
    {
        std::string_view view = lineRef;
        size_t pos = 0;
        bool counted = false;

        while(true)
        {
            while(pos < view.size() &&
                  std::isspace(static_cast<unsigned char>(view[pos])))
            {
                ++pos;
            }

            if(pos >= view.size())
                break;

            if(inBlock)
            {
                if(!rules.hasBlock)
                    break;
                size_t end = view.find(rules.blockEnd, pos);
                if(text_utils::is_not_found(end))
                {
                    pos = view.size();
                    break;
                }
                pos = end + rules.blockEnd.size();
                inBlock = false;
                continue;
            }

            if(rules.hasLine &&
               view.compare(pos, rules.line.size(), rules.line) == 0)
            {
                pos = view.size();
                break;
            }

            if(rules.hasBlock && view.compare(pos, rules.blockStart.size(),
                                              rules.blockStart) == 0)
            {
                size_t end =
                    view.find(rules.blockEnd, pos + rules.blockStart.size());
                if(text_utils::is_not_found(end))
                {
                    inBlock = true;
                    pos = view.size();
                    break;
                }
                pos = end + rules.blockEnd.size();
                continue;
            }

            counted = true;
            break;
        }

        if(counted)
            count++;
    }

    return count;
}

void collectLocFiles(const std::string& dir, int depth,
                     const GitIgnore& gitignore, std::vector<std::string>& out)
{
    if(depth > 10)
        return;

    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if(ec)
        return;
    for(; it != std::filesystem::directory_iterator(); it.increment(ec))
    {
        if(ec)
            break;
        std::string name = it->path().filename().string();
        if(name.empty() || name[0] == '.')
            continue;

        std::string fullPath = dir + "/" + name;

        std::error_code statEc;
        bool isDir = it->is_directory(statEc);
        if(statEc)
            continue;
        if(gitignore.isIgnored(fullPath, isDir))
            continue;

        if(isDir)
            collectLocFiles(fullPath, depth + 1, gitignore, out);
        else
            out.push_back(fullPath);
    }
}

void collectProjectFileEntries(const std::string& dir, int depth,
                               const GitIgnore& gitignore,
                               std::vector<FileEntry>& out)
{
    if(depth > 10)
        return;

    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if(ec)
        return;

    for(; it != std::filesystem::directory_iterator(); it.increment(ec))
    {
        if(ec)
            break;

        std::string name = it->path().filename().string();
        if(name.empty())
            continue;

        std::string fullPath = dir + "/" + name;

        std::error_code statEc;
        bool isDir = it->is_directory(statEc);
        if(statEc)
            continue;

        if(gitignore.isIgnored(fullPath, isDir))
            continue;

        if(name[0] == '.')
            continue;

        FileEntry fileEntry;
        fileEntry.name = name;
        fileEntry.path = fullPath;
        fileEntry.isDirectory = isDir;

        std::error_code sizeEc;
        fileEntry.size =
            isDir ? 0 : (uintmax_t)std::filesystem::file_size(fullPath, sizeEc);
        if(sizeEc)
            fileEntry.size = 0;

        std::error_code mtEc;
        auto ftime = std::filesystem::last_write_time(fullPath, mtEc);
        if(!mtEc)
        {
            using namespace std::chrono;
            auto sctp = time_point_cast<system_clock::duration>(
                ftime - decltype(ftime)::clock::now() + system_clock::now());
            fileEntry.modTime = system_clock::to_time_t(sctp);
        }
        fileEntry.metadataLoaded = true;

        out.push_back(fileEntry);

        if(isDir)
            collectProjectFileEntries(fullPath, depth + 1, gitignore, out);
    }
}

std::string expandTildePath(std::string path)
{
    if(!path.empty() && path[0] == '~')
    {
        const char* home = getenv("HOME");
        if(home)
            path = std::string(home) + path.substr(1);
    }
    return path;
}

int fuzzyScore(const std::string& text, const std::string& pattern)
{
    if(pattern.empty())
        return 0;

    auto lower = [](unsigned char ch) -> unsigned char
    {
        if(ch >= 'A' && ch <= 'Z')
            return (unsigned char)(ch - 'A' + 'a');
        return ch;
    };

    int score = 0;
    int ti = 0;
    int consecutive = 0;

    for(int pi = 0; pi < (int)pattern.size(); ++pi)
    {
        unsigned char pc = lower((unsigned char)pattern[pi]);
        bool found = false;

        while(ti < (int)text.size())
        {
            unsigned char tc = (unsigned char)text[ti];
            unsigned char ltc = lower(tc);
            if(ltc == pc)
            {
                score += 10;
                score += consecutive * 5;

                if(ti == 0)
                    score += 8;
                else
                {
                    char prev = text[ti - 1];
                    if(prev == '_' || prev == ':' || prev == ' ' ||
                       prev == '\t' || prev == '-')
                        score += 8;
                }

                ++consecutive;
                ++ti;
                found = true;
                break;
            }
            else
            {
                consecutive = 0;
                ++ti;
            }
        }
        if(!found)
            return -1;
    }

    return score;
}

int fuzzyScoreWithPositions(const std::string& needle,
                            const std::string& haystack,
                            std::vector<int>& matchPositions)
{
    matchPositions.clear();

    if(needle.empty())
        return 0;
    if(needle.length() > haystack.length())
        return -1;

    int score = 0;
    int consecutiveBonus = 10;
    int separatorBonus = 30;
    int camelBonus = 30;
    int firstLetterBonus = 15;

    size_t needleIdx = 0;
    int prevMatchIdx = -1;

    for(size_t i = 0; i < haystack.length() && needleIdx < needle.length(); i++)
    {
        char needleChar = std::tolower(needle[needleIdx]);
        char haystackChar = std::tolower(haystack[i]);

        if(needleChar == haystackChar)
        {
            matchPositions.push_back(i);

            score += 100;

            if(prevMatchIdx >= 0 && i == (size_t)prevMatchIdx + 1)
                score += consecutiveBonus;

            if(i > 0)
            {
                char prevChar = haystack[i - 1];
                if(prevChar == '/' || prevChar == '-' || prevChar == '_' ||
                   prevChar == '.')
                {
                    score += separatorBonus;
                }
            }

            if(i > 0 && std::islower(haystack[i - 1]) &&
               std::isupper(haystack[i]))
            {
                score += camelBonus;
            }

            if(i == 0)
                score += firstLetterBonus;

            if(needle[needleIdx] == haystack[i])
                score += 5;

            prevMatchIdx = static_cast<int>(i);
            needleIdx++;
        }
        else if(prevMatchIdx >= 0)
        {
            score -= (int)(i - static_cast<size_t>(prevMatchIdx));
        }
    }

    if(needleIdx != needle.length())
        return -1;

    score -= static_cast<int>(haystack.length());
    return score;
}

std::vector<std::string> getPathCompletions(std::string_view partial)
{
    std::vector<std::string> completions;

    std::string dirPath;
    std::string prefix;

    std::string expandedPartial(partial);
    if(!expandedPartial.empty() && expandedPartial[0] == '~')
    {
        const char* home = getenv("HOME");
        if(home)
            expandedPartial = std::string(home) + expandedPartial.substr(1);
    }

    size_t lastSlash = expandedPartial.find_last_of('/');
    if(text_utils::is_found(lastSlash))
    {
        dirPath = expandedPartial.substr(0, lastSlash);
        if(dirPath.empty())
            dirPath = "/";
        prefix = expandedPartial.substr(lastSlash + 1);
    }
    else
    {
        dirPath = ".";
        prefix = expandedPartial;
    }

    std::error_code ec;
    std::filesystem::directory_iterator it(dirPath, ec);
    if(ec)
        return completions;
    for(; it != std::filesystem::directory_iterator(); it.increment(ec))
    {
        if(ec)
            break;
        std::string name = it->path().filename().string();
        if(name.empty())
            continue;

        if(name[0] == '.' && (prefix.empty() || prefix[0] != '.'))
            continue;

        if(prefix.empty() || name.substr(0, prefix.length()) == prefix)
        {
            std::string fullPath;
            if(text_utils::is_found(lastSlash))
            {
                if(!partial.empty() && partial[0] == '~')
                {
                    size_t origSlash = partial.find_last_of('/');
                    fullPath =
                        std::string(partial.substr(0, origSlash + 1)) + name;
                }
                else
                {
                    fullPath = dirPath + "/" + name;
                }
            }
            else
            {
                fullPath = name;
            }

            std::error_code statEc;
            if(it->is_directory(statEc) && !statEc)
                fullPath += "/";

            completions.push_back(fullPath);
        }
    }

    std::sort(completions.begin(), completions.end());

    return completions;
}

static bool completionSubsequenceMatch(std::string_view text,
                                       std::string_view pattern)
{
    if(pattern.empty())
        return true;
    size_t i = 0;
    for(char ch : text)
    {
        if(ch == pattern[i])
        {
            ++i;
            if(i >= pattern.size())
                return true;
        }
    }
    return false;
}

static void collectRecursiveCompletion(const std::string& dir,
                                       const std::string& relBase,
                                       std::string_view prefix,
                                       std::vector<std::string>& relMatches,
                                       int depth, int& budget,
                                       const GitIgnore* gitignore)
{
    if(depth > 8 || budget <= 0)
        return;

    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if(ec)
        return;
    for(; it != std::filesystem::directory_iterator(); it.increment(ec))
    {
        if(ec)
            break;
        std::string name = it->path().filename().string();
        if(name.empty() || name[0] == '.')
            continue;

        std::string fullPath = dir + "/" + name;
        std::error_code statEc;
        bool isDir = it->is_directory(statEc);
        if(statEc)
            continue;
        if(gitignore && gitignore->isIgnored(fullPath, isDir))
            continue;
        std::string rel = relBase.empty() ? name : (relBase + "/" + name);
        bool match = false;
        if(prefix.empty())
        {
            match = true;
        }
        else if(text_utils::contains(prefix, '/'))
        {
            match = text_utils::contains(rel, prefix) ||
                    completionSubsequenceMatch(rel, prefix);
        }
        else
        {
            match = text_utils::contains(name, prefix) ||
                    completionSubsequenceMatch(name, prefix);
        }

        if(match)
        {
            if(isDir)
                rel += "/";
            relMatches.push_back(rel);
            if(--budget <= 0)
                break;
        }

        if(isDir)
        {
            collectRecursiveCompletion(
                fullPath, relBase.empty() ? name : relBase + "/" + name, prefix,
                relMatches, depth + 1, budget, gitignore);
            if(budget <= 0)
                break;
        }
    }
}

std::vector<std::string> getRecursivePathCompletions(std::string_view partial,
                                                     bool respectGitignore)
{
    if(partial.empty())
        return getPathCompletions(partial);

    std::vector<std::string> completions = getPathCompletions(partial);

    std::string expandedPartial(partial);
    if(!expandedPartial.empty() && expandedPartial[0] == '~')
    {
        const char* home = getenv("HOME");
        if(home)
            expandedPartial = std::string(home) + expandedPartial.substr(1);
    }

    std::string dirPath;
    std::string prefix;
    size_t lastSlash = expandedPartial.find_last_of('/');
    if(text_utils::is_found(lastSlash))
    {
        dirPath = expandedPartial.substr(0, lastSlash);
        if(dirPath.empty())
            dirPath = "/";
        prefix = expandedPartial.substr(lastSlash + 1);
    }
    else
    {
        dirPath = ".";
        prefix = expandedPartial;
    }

    GitIgnore gitignore;
    if(respectGitignore)
        gitignore.loadRecursive(dirPath);

    if(prefix.empty())
    {
        if(respectGitignore)
        {
            std::vector<std::string> filtered;
            filtered.reserve(completions.size());
            for(const auto& item : completions)
            {
                std::string path = item;
                if(!path.empty() && path.back() == '/')
                    path.pop_back();
                if(!path.empty() && path[0] == '~')
                {
                    const char* home = getenv("HOME");
                    if(home)
                        path = std::string(home) + path.substr(1);
                }
                std::string fullPath = (text_utils::is_found(lastSlash))
                                           ? path
                                           : (dirPath + "/" + path);
                std::error_code statEc;
                bool isDir = std::filesystem::is_directory(fullPath, statEc);
                if(statEc)
                    isDir = false;
                if(!gitignore.isIgnored(fullPath, isDir))
                    filtered.push_back(item);
            }
            completions.swap(filtered);
        }
        std::sort(completions.begin(), completions.end());
        completions.erase(std::unique(completions.begin(), completions.end()),
                          completions.end());
        return completions;
    }

    std::vector<std::string> relMatches;
    int budget = 1000;
    collectRecursiveCompletion(dirPath, "", prefix, relMatches, 0, budget,
                               respectGitignore ? &gitignore : nullptr);

    if(!relMatches.empty())
    {
        std::string basePrefix;
        if(text_utils::is_found(lastSlash))
        {
            if(!partial.empty() && partial[0] == '~')
            {
                size_t origSlash = partial.find_last_of('/');
                basePrefix = std::string(partial.substr(0, origSlash + 1));
            }
            else
            {
                if(dirPath == ".")
                    basePrefix.clear();
                else
                    basePrefix = dirPath + "/";
            }
        }

        for(const auto& rel : relMatches)
        {
            std::string fullPath = basePrefix.empty() ? rel : basePrefix + rel;
            completions.push_back(fullPath);
        }
    }

    auto scorePath = [&](std::string_view path) -> int
    {
        if(prefix.empty())
            return 0;

        std::string_view candidate = path;
        if(!candidate.empty() && candidate.back() == '/')
            candidate = candidate.substr(0, candidate.size() - 1);

        std::string_view name = candidate;
        size_t slashPos = candidate.find_last_of('/');
        if(text_utils::is_found(slashPos))
            name = candidate.substr(slashPos + 1);

        bool hasSlash = text_utils::contains(prefix, '/');
        std::string_view hay = hasSlash ? candidate : name;

        if(hay.rfind(prefix, 0) == 0)
            return 1000 - (int)candidate.size();
        if(text_utils::contains(hay, prefix))
            return 600 - (int)candidate.size();
        if(completionSubsequenceMatch(hay, prefix))
            return 300 - (int)candidate.size();
        return 0;
    };

    std::sort(completions.begin(), completions.end(),
              [&](const std::string& a, const std::string& b)
              {
                  int sa = scorePath(a);
                  int sb = scorePath(b);
                  if(sa != sb)
                      return sa > sb;
                  if(a.size() != b.size())
                      return a.size() < b.size();
                  return a < b;
              });
    completions.erase(std::unique(completions.begin(), completions.end()),
                      completions.end());
    return completions;
}

std::vector<std::string> getLocPathCompletions(std::string_view partial,
                                               bool respectGitignore)
{
    return getRecursivePathCompletions(partial, respectGitignore);
}

std::string longestCommonPrefix(const std::vector<std::string>& strings)
{
    if(strings.empty())
        return "";
    if(strings.size() == 1)
        return strings[0];

    std::string prefix = strings[0];
    for(size_t i = 1; i < strings.size(); ++i)
    {
        size_t j = 0;
        while(j < prefix.length() && j < strings[i].length() &&
              prefix[j] == strings[i][j])
        {
            ++j;
        }
        prefix = prefix.substr(0, j);
        if(prefix.empty())
            break;
    }
    return prefix;
}

} // namespace editor::helper
