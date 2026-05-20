#include "mlang_utilities.h"
#include "text_utils.h"

#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <unordered_set>

namespace
{
std::string ascii_lower(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for(char c : value)
        out.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

bool is_ident_char(char c)
{
    unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_';
}

bool find_builtin_marker(std::string_view symbol,
                         const std::filesystem::path& file,
                         std::string_view marker, std::string& path, int& line)
{
    std::string needle = std::string(symbol);
    std::string needleLower = ascii_lower(symbol);

    std::error_code ec;
    if(!std::filesystem::exists(file, ec))
        return false;

    std::ifstream in(file);
    if(!in)
        return false;

    std::string lineStr;
    int lineNo = 0;
    while(std::getline(in, lineStr))
    {
        ++lineNo;
        if(lineStr.rfind(marker, 0) != 0)
            continue;
        std::string name = lineStr.substr(marker.size());
        if(name.empty())
            continue;
        if(name == needle || ascii_lower(name) == needleLower)
        {
            path = file.string();
            line = lineNo - 1;
            return true;
        }
    }

    return false;
}
} // namespace

bool MlangUtilities::findBuiltinType(std::string_view symbol, std::string& path,
                                     int& line)
{
    for(const auto& root : stdlibRoots())
    {
        if(!root.empty() && find_builtin_marker(symbol, root / "types.mla",
                                                "// @builtin ", path, line))
            return true;
    }
    return false;
}

bool MlangUtilities::findBuiltinMacro(std::string_view symbol,
                                      std::string& path, int& line)
{
    for(const auto& root : stdlibRoots())
    {
        if(!root.empty() &&
           find_builtin_marker(symbol, root / "macros.mla",
                               "// @builtin_macro ", path, line))
            return true;
    }
    return false;
}

bool MlangUtilities::findBuiltinAttribute(std::string_view symbol,
                                          std::string& path, int& line)
{
    for(const auto& root : stdlibRoots())
    {
        if(!root.empty() &&
           find_builtin_marker(symbol, root / "attributes.mla",
                               "// @builtin_attribute ", path, line))
            return true;
    }
    return false;
}

bool MlangUtilities::findBuiltinFunction(std::string_view symbol,
                                         std::string& path, int& line,
                                         std::string_view contextFilePath)
{
    std::string needle = std::string(symbol);
    std::string needleLower = ascii_lower(symbol);

    for(const auto& root : stdlibRoots())
    {
        if(root.empty())
            continue;
        std::filesystem::path p = root / "test.mla";
        std::error_code ec;
        if(!std::filesystem::exists(p, ec))
            continue;
        std::ifstream in(p);
        if(!in)
            continue;
        std::string lineStr;
        int lineNo = 0;
        while(std::getline(in, lineStr))
        {
            ++lineNo;
            const std::string marker = "// @builtin_fn ";
            if(lineStr.rfind(marker, 0) != 0)
                continue;
            std::string name = lineStr.substr(marker.size());
            if(name.empty())
                continue;
            std::string base = name;
            auto sep = base.rfind("::");
            if(text_utils::is_found(sep) && sep + 2 < base.size())
                base = base.substr(sep + 2);
            if(name == needle || ascii_lower(name) == needleLower ||
               base == needle || ascii_lower(base) == needleLower)
            {
                path = p.string();
                line = lineNo - 1;
                return true;
            }
        }
    }

    auto match_stub_extern = [&](std::string_view lineView) -> bool
    {
        static constexpr std::string_view kPrefix = "extern fn ";
        size_t pos = lineView.find(kPrefix);
        if(text_utils::is_not_found(pos))
            return false;
        pos += kPrefix.size();
        while(pos < lineView.size() && text_utils::is_space(lineView[pos]))
            ++pos;
        size_t start = pos;
        while(pos < lineView.size() && is_ident_char(lineView[pos]))
            ++pos;
        if(pos <= start)
            return false;
        std::string fn(lineView.substr(start, pos - start));
        return fn == needle || ascii_lower(fn) == needleLower;
    };

    std::vector<std::filesystem::path> searchDirs;
    std::unordered_set<std::string> seenDirs;
    auto add_dir = [&](const std::filesystem::path& dir)
    {
        if(dir.empty())
            return;
        std::error_code ec;
        std::filesystem::path canon =
            std::filesystem::weakly_canonical(dir, ec);
        std::string key = (ec ? dir : canon).string();
        if(key.empty() || seenDirs.find(key) != seenDirs.end())
            return;
        seenDirs.insert(key);
        searchDirs.push_back(ec ? dir : canon);
    };

    if(!contextFilePath.empty())
    {
        std::filesystem::path dir =
            std::filesystem::path(std::string(contextFilePath)).parent_path();
        while(!dir.empty())
        {
            add_dir(dir);
            std::filesystem::path parent = dir.parent_path();
            if(parent == dir)
                break;
            dir = parent;
        }
    }

    {
        std::error_code ec;
        add_dir(std::filesystem::current_path(ec));
    }

    for(const auto& dir : searchDirs)
    {
        const std::array<std::filesystem::path, 1> candidates = {
            dir / "docs" / "runtime_builtins.mlastub",
        };

        for(const auto& p : candidates)
        {
            std::error_code ec;
            if(!std::filesystem::exists(p, ec))
                continue;
            std::ifstream in(p);
            if(!in)
                continue;
            std::string lineStr;
            int lineNo = 0;
            while(std::getline(in, lineStr))
            {
                ++lineNo;
                bool matched = match_stub_extern(lineStr);
                if(matched)
                {
                    path = p.string();
                    line = lineNo - 1;
                    return true;
                }
            }
        }
    }

    return false;
}

bool MlangUtilities::findTopLevelDefInLines(
    const std::vector<std::string>& lines, std::string_view symbol, int& outY,
    int& outX)
{
    auto parse_ident_after = [&](const std::string& line, size_t start,
                                 std::string_view kw) -> std::optional<int>
    {
        if(start + kw.size() > line.size())
            return std::nullopt;
        if(line.compare(start, kw.size(), kw) != 0)
            return std::nullopt;
        size_t i = start + kw.size();
        while(i < line.size() && text_utils::is_space(line[i]))
            ++i;
        if(i >= line.size() || !is_ident_char(line[i]))
            return std::nullopt;
        size_t nameStart = i;
        while(i < line.size() && is_ident_char(line[i]))
            ++i;
        std::string_view name(line.data() + nameStart, i - nameStart);
        if(name == symbol)
            return static_cast<int>(nameStart);
        return std::nullopt;
    };

    for(int y = 0; y < (int)lines.size(); ++y)
    {
        const std::string& line = lines[(size_t)y];
        size_t i = 0;
        while(i < line.size() && text_utils::is_space(line[i]))
            ++i;

        std::optional<int> x = parse_ident_after(line, i, "pub struct ");
        if(!x)
            x = parse_ident_after(line, i, "struct ");
        if(!x)
            x = parse_ident_after(line, i, "pub enum ");
        if(!x)
            x = parse_ident_after(line, i, "enum ");
        if(!x)
            x = parse_ident_after(line, i, "pub fn ");
        if(!x)
            x = parse_ident_after(line, i, "fn ");
        if(x)
        {
            outY = y;
            outX = *x;
            return true;
        }
    }

    return false;
}

std::string MlangUtilities::moduleRelPath(std::string_view modulePath)
{
    std::string rel;
    rel.reserve(modulePath.size());
    for(size_t i = 0; i < modulePath.size(); ++i)
    {
        char c = modulePath[i];
        if(c == ':' && i + 1 < modulePath.size() && modulePath[i + 1] == ':')
        {
            rel.push_back('/');
            ++i;
            continue;
        }
        rel.push_back(c);
    }
    return rel;
}

std::vector<std::filesystem::path> MlangUtilities::stdlibRoots()
{
    std::vector<std::filesystem::path> roots;
    if(const char* env = std::getenv("MLANG_STDLIB_PATH"))
        roots.emplace_back(env);
    if(const char* xdg = std::getenv("XDG_DATA_HOME"))
        roots.emplace_back(std::string(xdg) + "/mlang/stdlib");
    if(const char* home = std::getenv("HOME"))
        roots.emplace_back(std::string(home) + "/.local/share/mlang/stdlib");
    roots.emplace_back("/usr/local/share/mlang/stdlib");
    roots.emplace_back("/usr/share/mlang/stdlib");
    return roots;
}

bool MlangUtilities::resolveModuleFile(std::string_view modulePath,
                                       std::string_view contextFilePath,
                                       std::string& outPath)
{
    outPath.clear();
    if(modulePath.empty())
        return false;

    std::string rel = moduleRelPath(modulePath);
    auto try_candidate = [&](const std::filesystem::path& p) -> bool
    {
        std::error_code ec;
        if(!std::filesystem::exists(p, ec) || ec)
            return false;
        outPath = p.string();
        return true;
    };

    auto add_base = [](std::vector<std::filesystem::path>& bases,
                       std::unordered_set<std::string>& seen,
                       const std::filesystem::path& base)
    {
        if(base.empty())
            return;
        std::error_code ec;
        auto canon = std::filesystem::weakly_canonical(base, ec);
        std::string key = (ec ? base : canon).string();
        if(key.empty() || seen.find(key) != seen.end())
            return;
        seen.insert(key);
        bases.push_back(ec ? base : canon);
    };

    if(modulePath.rfind("std::", 0) == 0)
    {
        for(const auto& root : stdlibRoots())
        {
            if(root.empty())
                continue;
            if(try_candidate(root / (rel + ".mla")))
                return true;
            if(try_candidate(root / rel / "mod.mla"))
                return true;
        }
    }

    std::vector<std::filesystem::path> bases;
    std::unordered_set<std::string> seen;
    if(!contextFilePath.empty())
    {
        std::filesystem::path dir =
            std::filesystem::path(std::string(contextFilePath)).parent_path();
        while(!dir.empty())
        {
            add_base(bases, seen, dir);
            std::filesystem::path parent = dir.parent_path();
            if(parent == dir)
                break;
            dir = parent;
        }
    }
    {
        std::error_code ec;
        add_base(bases, seen, std::filesystem::current_path(ec));
    }

    for(const auto& base : bases)
    {
        if(try_candidate(base / (rel + ".mla")))
            return true;
        if(try_candidate(base / rel / "mod.mla"))
            return true;
    }
    return false;
}

bool MlangUtilities::moduleDeclUnderCursor(std::string_view line, int cursorX,
                                           std::string& modulePath)
{
    modulePath.clear();
    if(cursorX < 0 || line.empty())
        return false;

    size_t i = 0;
    while(i < line.size() && text_utils::is_space(line[i]))
        ++i;
    if(i + 3 > line.size() || line.compare(i, 3, "mod") != 0)
        return false;
    if(i + 3 < line.size() && !text_utils::is_space(line[i + 3]))
        return false;
    i += 3;
    while(i < line.size() && text_utils::is_space(line[i]))
        ++i;
    if(i >= line.size())
        return false;

    size_t pathStart = i;
    bool cursorInSegment = false;
    while(i < line.size())
    {
        if(!is_ident_char(line[i]))
            return false;
        size_t segStart = i;
        while(i < line.size() && is_ident_char(line[i]))
            ++i;
        size_t segEnd = i;
        if(cursorX >= (int)segStart && cursorX < (int)segEnd)
            cursorInSegment = true;

        if(i + 1 < line.size() && line[i] == ':' && line[i + 1] == ':')
        {
            i += 2;
            continue;
        }
        break;
    }

    size_t pathEnd = i;
    while(i < line.size() && text_utils::is_space(line[i]))
        ++i;
    if(i < line.size() && line[i] != ';')
        return false;

    if(!cursorInSegment || pathEnd <= pathStart)
        return false;
    modulePath.assign(line.substr(pathStart, pathEnd - pathStart));
    return true;
}
