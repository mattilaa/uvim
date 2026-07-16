#include "constants.h"
#include "cpp_constants.h"
#include "editor.h"
#include "json_utils.h"
#include "syntax_highlighter.h"
#include "syntax_state.h"
#include "text_utils.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

template <class Arr>
inline bool contains_sorted(const Arr& arr, std::string_view s)
{
    return std::ranges::binary_search(arr, s);
}

inline bool is_two_char_op(char a, char b) noexcept
{
    switch(a)
    {
    case '+':
        return (b == '+' || b == '=');
    case '-':
        return (b == '-' || b == '=' || b == '>');
    case '&':
        return (b == '&');
    case '|':
        return (b == '|');
    case '=':
        return (b == '=');
    case '!':
        return (b == '=');
    case '<':
        return (b == '<' || b == '=');
    case '>':
        return (b == '>' || b == '=');
    case ':':
        return (b == ':');
    case '.':
        // treats ".." as two-char; "..." will become ".." + "."
        return (b == '.');
    case '*':
        return (b == '=');
    case '/':
        return (b == '=');
    case '%':
        return (b == '=');
    default:
        return false;
    }
}

namespace
{
std::string ascii_lower(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for(char c : value)
        out.push_back(text_utils::ascii_tolower(c));
    return out;
}

bool is_asm_keyword(std::string_view word)
{
    static constexpr std::array keywords = {
        "adc",     "adcs",   "add",    "addb",  "addl",   "addq",   "adds",
        "addw",    "adr",    "adrp",   "and",   "andb",   "andl",   "andq",
        "ands",    "andw",   "asr",    "b",     "b.eq",   "b.ge",   "b.gt",
        "b.hi",    "b.hs",   "b.le",   "b.lo",  "b.ls",   "b.lt",   "b.mi",
        "b.ne",    "b.pl",   "b.vc",   "b.vs",  "beq",    "bge",    "bgt",
        "bhi",     "bhs",    "bic",    "bl",    "ble",    "blo",    "bls",
        "blt",     "bmi",    "bne",    "bpl",   "br",     "break",  "bvc",
        "bvs",     "call",   "callq",  "cbnz",  "cbz",    "cinc",   "cisel",
        "cmp",     "cmpb",   "cmpl",   "cmpq",  "cmpw",   "cmpeq",  "csel",
        "cset",    "dec",    "decl",   "decq",  "div",    "eor",    "fadd",
        "fcmp",    "fcsel",  "fcvt",   "fdiv",  "fmadd",  "fmov",   "fmsub",
        "fmul",    "fneg",   "fsqrt",  "fsub",  "idiv",   "imul",   "inc",
        "incl",    "incq",   "ja",     "jae",   "jb",     "jbe",    "je",
        "jg",      "jge",    "jl",     "jle",   "jmp",    "jne",    "jno",
        "jnp",     "jns",    "jo",     "jp",    "js",     "ldr",    "ldp",
        "ldrb",    "ldrh",   "ldrsb",  "ldrsh", "ldur",   "lea",    "leal",
        "leaq",    "leave",  "leaveq", "lsl",   "lsr",    "madd",   "mov",
        "movabsq", "movb",   "movk",   "movl",  "movn",   "movq",   "movsbl",
        "movslq",  "movswl", "movw",   "movz",  "movzbl", "movzwl", "mrs",
        "msr",     "msub",   "mul",    "neg",   "negl",   "negq",   "nop",
        "not",     "orr",    "or",     "orb",   "orl",    "orq",    "orw",
        "pop",     "popq",   "prfm",   "push",  "pushq",  "ret",    "retq",
        "ror",     "sal",    "sall",   "salq",  "sar",    "sarl",   "sarq",
        "sbc",     "sbcs",   "sdiv",   "sete",  "setg",   "setge",  "setl",
        "setle",   "setne",  "shl",    "shll",  "shlq",   "shr",    "shrl",
        "shrq",    "stp",    "str",    "strb",  "strh",   "stur",   "sub",
        "subb",    "subl",   "subq",   "subs",  "subw",   "svc",    "tbnz",
        "tbz",     "test",   "testb",  "testl", "testq",  "testw",  "udiv",
        "umaddl",  "umsubl", "xor",    "xorb",  "xorl",   "xorq"};

    static constexpr std::array directives = {"align",
                                              "ascii",
                                              "asciz",
                                              "balign",
                                              "byte",
                                              "cfi_def_cfa",
                                              "cfi_endproc",
                                              "cfi_offset",
                                              "cfi_startproc",
                                              "comm",
                                              "data",
                                              "file",
                                              "globl",
                                              "hidden",
                                              "ident",
                                              "long",
                                              "p2align",
                                              "quad",
                                              "section",
                                              "set",
                                              "short",
                                              "size",
                                              "text",
                                              "type",
                                              "value",
                                              "weak",
                                              "word",
                                              "zerofill",
                                              "loc",
                                              "loc_mark_labels",
                                              "subsections_via_symbols",
                                              "private_extern",
                                              "build_version"};

    const std::string lower = ascii_lower(word);
    return std::find(keywords.begin(), keywords.end(),
                     std::string_view(lower)) != keywords.end() ||
           std::find(directives.begin(), directives.end(),
                     std::string_view(lower)) != directives.end();
}

bool is_asm_register(std::string_view word)
{
    static constexpr std::array registers = {
        "ah",   "al",   "ax",   "bh",   "bl",   "bp",   "bpl",  "bx",   "ch",
        "cl",   "cx",   "dh",   "di",   "dil",  "dl",   "dx",   "eax",  "ebp",
        "ebx",  "ecx",  "edi",  "edx",  "eip",  "esi",  "esp",  "ip",   "r10",
        "r10b", "r10d", "r10w", "r11",  "r11b", "r11d", "r11w", "r12",  "r12b",
        "r12d", "r12w", "r13",  "r13b", "r13d", "r13w", "r14",  "r14b", "r14d",
        "r14w", "r15",  "r15b", "r15d", "r15w", "r8"};

    static constexpr std::array moreRegisters = {
        "fp",  "lr",   "nzcv", "r8b",  "r8d", "r8w", "r9",  "r9b",
        "r9d", "r9w",  "rax",  "rbp",  "rbx", "rcx", "rdi", "rdx",
        "rip", "rsi",  "rsp",  "si",   "sil", "sp",  "spl", "wzr",
        "xzr", "xmm0", "xmm1", "xmm2", "xmm3"};

    const std::string lower = ascii_lower(word);
    const std::string_view view(lower);
    auto numeric_register = [&](std::string_view prefix, int max) -> bool
    {
        if(!view.starts_with(prefix) || view.size() == prefix.size())
            return false;
        int value = 0;
        for(char ch : view.substr(prefix.size()))
        {
            if(!text_utils::is_digit(ch))
                return false;
            value = value * 10 + (ch - '0');
        }
        return value >= 0 && value <= max;
    };

    return std::find(registers.begin(), registers.end(), view) !=
               registers.end() ||
           std::find(moreRegisters.begin(), moreRegisters.end(), view) !=
               moreRegisters.end() ||
           numeric_register("r", 15) || numeric_register("x", 30) ||
           numeric_register("w", 30) || numeric_register("b", 31) ||
           numeric_register("h", 31) || numeric_register("s", 31) ||
           numeric_register("d", 31) || numeric_register("q", 31) ||
           numeric_register("v", 31) || numeric_register("mm", 31) ||
           numeric_register("st", 7) || numeric_register("xmm", 31) ||
           numeric_register("ymm", 31) || numeric_register("zmm", 31);
}

std::vector<std::filesystem::path> default_mlang_stdlib_paths()
{
    std::vector<std::filesystem::path> paths;
    if(const char* env = std::getenv("MLANG_STDLIB_PATH"))
        paths.emplace_back(env);
    if(const char* xdg = std::getenv("XDG_DATA_HOME"))
        paths.emplace_back(std::string(xdg) + "/mlang/stdlib");
    if(const char* home = std::getenv("HOME"))
        paths.emplace_back(std::string(home) + "/.local/share/mlang/stdlib");
    paths.emplace_back("/usr/local/share/mlang/stdlib");
    paths.emplace_back("/usr/share/mlang/stdlib");
    return paths;
}

void load_builtin_types_from_file(MlangTokenCache& cache,
                                  const std::filesystem::path& path)
{
    std::ifstream in(path);
    if(!in)
        return;
    std::string line;
    int lineNo = 0;
    while(std::getline(in, line))
    {
        ++lineNo;
        const std::string marker = "// @builtin ";
        if(line.rfind(marker, 0) != 0)
            continue;
        std::string name = line.substr(marker.size());
        if(name.empty())
            continue;
        if(cache.caseInsensitive)
            name = ascii_lower(name);
        MlangTokenCache::BuiltinTypeDef def;
        def.path = path.string();
        def.line = lineNo - 1;
        cache.builtinTypes.emplace(std::move(name), std::move(def));
    }
}

void load_builtin_macros_from_file(MlangTokenCache& cache,
                                   const std::filesystem::path& path)
{
    std::ifstream in(path);
    if(!in)
        return;
    std::string line;
    int lineNo = 0;
    while(std::getline(in, line))
    {
        ++lineNo;
        const std::string marker = "// @builtin_macro ";
        if(line.rfind(marker, 0) != 0)
            continue;
        std::string name = line.substr(marker.size());
        if(name.empty())
            continue;
        if(cache.caseInsensitive)
            name = ascii_lower(name);
        MlangTokenCache::BuiltinTypeDef def;
        def.path = path.string();
        def.line = lineNo - 1;
        cache.builtinMacros.emplace(std::move(name), std::move(def));
    }
}

void load_builtin_attributes_from_file(MlangTokenCache& cache,
                                       const std::filesystem::path& path)
{
    std::ifstream in(path);
    if(!in)
        return;
    std::string line;
    int lineNo = 0;
    while(std::getline(in, line))
    {
        ++lineNo;
        const std::string marker = "// @builtin_attribute ";
        if(line.rfind(marker, 0) != 0)
            continue;
        std::string name = line.substr(marker.size());
        if(name.empty())
            continue;
        if(cache.caseInsensitive)
            name = ascii_lower(name);
        MlangTokenCache::BuiltinTypeDef def;
        def.path = path.string();
        def.line = lineNo - 1;
        cache.builtinAttributes.emplace(std::move(name), std::move(def));
    }
}

void load_builtin_functions_from_file(MlangTokenCache& cache,
                                      const std::filesystem::path& path)
{
    std::ifstream in(path);
    if(!in)
        return;
    std::string line;
    int lineNo = 0;
    while(std::getline(in, line))
    {
        ++lineNo;
        const std::string marker = "// @builtin_fn ";
        if(line.rfind(marker, 0) != 0)
            continue;
        std::string name = line.substr(marker.size());
        if(name.empty())
            continue;
        if(cache.caseInsensitive)
            name = ascii_lower(name);
        MlangTokenCache::BuiltinTypeDef def;
        def.path = path.string();
        def.line = lineNo - 1;
        cache.builtinFunctions.emplace(name, def);
        auto sep = name.rfind("::");
        if(text_utils::is_found(sep) && sep + 2 < name.size())
        {
            std::string base = name.substr(sep + 2);
            cache.builtinFunctions.emplace(std::move(base), def);
        }
    }
}

void ensure_builtin_types_loaded(MlangTokenCache& cache)
{
    if(cache.builtinTypesLoaded)
        return;
    for(const auto& root : default_mlang_stdlib_paths())
    {
        if(root.empty())
            continue;
        std::filesystem::path p = root / "types.mla";
        std::error_code ec;
        if(!std::filesystem::exists(p, ec))
            continue;
        load_builtin_types_from_file(cache, p);
        if(!cache.builtinTypes.empty())
        {
            cache.builtinTypesLoaded = true;
            return;
        }
    }
}

void ensure_builtin_macros_loaded(MlangTokenCache& cache)
{
    if(cache.builtinMacrosLoaded)
        return;
    for(const auto& root : default_mlang_stdlib_paths())
    {
        if(root.empty())
            continue;
        std::filesystem::path p = root / "macros.mla";
        std::error_code ec;
        if(!std::filesystem::exists(p, ec))
            continue;
        load_builtin_macros_from_file(cache, p);
        if(!cache.builtinMacros.empty())
        {
            cache.builtinMacrosLoaded = true;
            return;
        }
    }
}

void ensure_builtin_attributes_loaded(MlangTokenCache& cache)
{
    if(cache.builtinAttributesLoaded)
        return;
    for(const auto& root : default_mlang_stdlib_paths())
    {
        if(root.empty())
            continue;
        std::filesystem::path p = root / "attributes.mla";
        std::error_code ec;
        if(!std::filesystem::exists(p, ec))
            continue;
        load_builtin_attributes_from_file(cache, p);
        if(!cache.builtinAttributes.empty())
        {
            cache.builtinAttributesLoaded = true;
            return;
        }
    }
}

void ensure_builtin_functions_loaded(MlangTokenCache& cache)
{
    if(cache.builtinFunctionsLoaded)
        return;
    for(const auto& root : default_mlang_stdlib_paths())
    {
        if(root.empty())
            continue;
        std::filesystem::path p = root / "test.mla";
        std::error_code ec;
        if(!std::filesystem::exists(p, ec))
            continue;
        load_builtin_functions_from_file(cache, p);
        if(!cache.builtinFunctions.empty())
        {
            cache.builtinFunctionsLoaded = true;
            return;
        }
    }
}

std::optional<TokenType> parse_token_type(std::string_view value)
{
    const std::string type = ascii_lower(value);
    if(type == "normal")
        return TOKEN_NORMAL;
    if(type == "keyword")
        return TOKEN_KEYWORD;
    if(type == "type")
        return TOKEN_TYPE;
    if(type == "string")
        return TOKEN_STRING;
    if(type == "char")
        return TOKEN_CHAR;
    if(type == "comment")
        return TOKEN_COMMENT;
    if(type == "preprocessor")
        return TOKEN_PREPROCESSOR;
    if(type == "number")
        return TOKEN_NUMBER;
    if(type == "operator")
        return TOKEN_OPERATOR;
    if(type == "function" || type == "builtin")
        return TOKEN_FUNCTION;
    if(type == "constant" || type == "literal" || type == "bool")
        return TOKEN_KEYWORD;
    return std::nullopt;
}

bool is_mlang_keyword(std::string_view word)
{
    static constexpr std::array<std::string_view, 24> kKeywords = {
        "alias",  "break", "continue", "else",      "enum", "extern",
        "fn",     "for",   "if",       "impl",      "in",   "let",
        "match",  "mod",   "mut",      "namespace", "pub",  "return",
        "struct", "trait", "type",     "use",       "var",  "while"};
    return std::ranges::any_of(kKeywords,
                               [&](std::string_view kw) { return kw == word; });
}

bool is_mlang_type(std::string_view word)
{
    static constexpr std::array<std::string_view, 22> kTypes = {
        "bool", "f32", "f64", "i16",   "i32",    "i64",    "i8",    "int",
        "list", "map", "ptr", "str16", "str8",   "string", "tuple", "u16",
        "u32",  "u64", "u8",  "void",  "Result", "Option"};
    return std::ranges::any_of(kTypes,
                               [&](std::string_view ty) { return ty == word; });
}

bool is_mlang_builtin(std::string_view word)
{
    static constexpr std::array<std::string_view, 8> kBuiltins = {
        "assert_eq", "debug", "eprint",  "eprintln",
        "format",    "print", "println", "vec"};
    return std::ranges::any_of(kBuiltins,
                               [&](std::string_view fn) { return fn == word; });
}

bool is_mlang_platform_keyword(std::string_view word)
{
    static constexpr std::array<std::string_view, 6> kPlatform = {
        "aarch64", "linux", "macos", "posix", "windows", "x64"};
    return std::ranges::any_of(kPlatform,
                               [&](std::string_view kw) { return kw == word; });
}

std::vector<std::string> split_command_line(std::string_view command)
{
    std::vector<std::string> args;
    std::string current;
    bool inQuote = false;
    char quoteChar = 0;

    auto flush = [&]()
    {
        if(!current.empty())
        {
            args.push_back(current);
            current.clear();
        }
    };

    for(size_t i = 0; i < command.size(); ++i)
    {
        char c = command[i];
        if(inQuote)
        {
            if(c == '\\' && i + 1 < command.size())
            {
                current.push_back(command[i + 1]);
                ++i;
                continue;
            }
            if(c == quoteChar)
            {
                inQuote = false;
                continue;
            }
            current.push_back(c);
            continue;
        }

        if(text_utils::is_space(c))
        {
            flush();
            continue;
        }
        if(c == '"' || c == '\'')
        {
            inQuote = true;
            quoteChar = c;
            continue;
        }
        if(c == '\\' && i + 1 < command.size())
        {
            current.push_back(command[i + 1]);
            ++i;
            continue;
        }
        current.push_back(c);
    }

    flush();
    return args;
}

void scan_line_for_cpp_method_context(
    const std::string& line, const std::unordered_set<std::string>& classNames,
    CppMethodScanState& state, bool* lineHasMethodStart)
{
    size_t i = 0;
    const size_t len = line.size();
    while(i < len)
    {
        if(state.inBlockComment)
        {
            size_t closePos = line.find("*/", i);
            if(text_utils::is_not_found(closePos))
                return;
            state.inBlockComment = false;
            i = closePos + 2;
            continue;
        }

        char c = line[i];
        if(c == '/' && i + 1 < len && line[i + 1] == '/')
            return;
        if(c == '/' && i + 1 < len && line[i + 1] == '*')
        {
            state.inBlockComment = true;
            i += 2;
            continue;
        }
        if(c == '"' || c == '\'')
        {
            char quote = c;
            ++i;
            while(i < len)
            {
                if(line[i] == '\\' && i + 1 < len)
                {
                    i += 2;
                    continue;
                }
                if(line[i] == quote)
                {
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }
        if(c == '{')
        {
            ++state.braceDepth;
            if(state.pendingMethod)
            {
                state.inMethod = true;
                state.methodBraceDepth = state.braceDepth;
                state.pendingMethod = false;
                if(lineHasMethodStart)
                    *lineHasMethodStart = true;
            }
            ++i;
            continue;
        }
        if(c == '}')
        {
            if(state.inMethod && state.braceDepth == state.methodBraceDepth)
                state.inMethod = false;
            if(state.braceDepth > 0)
                --state.braceDepth;
            ++i;
            continue;
        }
        if(c == ';')
        {
            if(state.pendingMethod)
                state.pendingMethod = false;
            ++i;
            continue;
        }
        if(text_utils::is_alpha(c) || c == '_')
        {
            size_t start = i;
            ++i;
            while(i < len && (text_utils::is_alpha(line[i]) ||
                              text_utils::is_digit(line[i]) || line[i] == '_'))
            {
                ++i;
            }
            std::string_view ident(line.data() + start, i - start);
            if(!classNames.empty() &&
               classNames.find(std::string(ident)) != classNames.end())
            {
                size_t j = i;
                while(j < len && text_utils::is_space(line[j]))
                    ++j;
                if(j + 1 < len && line[j] == ':' && line[j + 1] == ':')
                {
                    j += 2;
                    while(j < len && text_utils::is_space(line[j]))
                        ++j;
                    if(j < len && (text_utils::is_alpha(line[j]) ||
                                   line[j] == '_' || line[j] == '~'))
                    {
                        ++j;
                        while(j < len &&
                              (text_utils::is_alpha(line[j]) ||
                               text_utils::is_digit(line[j]) || line[j] == '_'))
                        {
                            ++j;
                        }
                        while(j < len && text_utils::is_space(line[j]))
                            ++j;
                        if(j < len && line[j] == '(')
                            state.pendingMethod = true;
                    }
                }
            }
            continue;
        }
        ++i;
    }
}

void scan_line_for_cpp_param_list_context(const std::string& line,
                                          CppParamListScanState& state,
                                          bool* lineHasParamListStart)
{
    size_t i = 0;
    const size_t len = line.size();
    bool inString = false;
    char quote = 0;

    while(i < len)
    {
        if(state.inBlockComment)
        {
            size_t closePos = line.find("*/", i);
            if(text_utils::is_not_found(closePos))
                return;
            state.inBlockComment = false;
            i = closePos + 2;
            continue;
        }

        char c = line[i];
        if(c == '/' && i + 1 < len && line[i + 1] == '/')
            return;
        if(c == '/' && i + 1 < len && line[i + 1] == '*')
        {
            state.inBlockComment = true;
            i += 2;
            continue;
        }
        if(inString)
        {
            if(c == '\\' && i + 1 < len)
            {
                i += 2;
                continue;
            }
            if(c == quote)
            {
                inString = false;
                ++i;
                continue;
            }
            ++i;
            continue;
        }
        if(c == '"' || c == '\'')
        {
            inString = true;
            quote = c;
            ++i;
            continue;
        }
        if(c == '(')
        {
            if(!state.inParamList && state.parenDepth == 0)
            {
                int back = (int)i - 1;
                while(back >= 0 && text_utils::is_space(line[back]))
                    --back;
                if(back >= 0 && line[back] == ']')
                {
                    state.inParamList = true;
                    state.parenDepth = 1;
                    if(lineHasParamListStart)
                        *lineHasParamListStart = true;
                    ++i;
                    continue;
                }
                int end = back;
                while(end >= 0 &&
                      (text_utils::is_alpha(line[end]) ||
                       text_utils::is_digit(line[end]) || line[end] == '_'))
                    --end;
                int start = end + 1;
                if(start <= back)
                {
                    std::string_view name =
                        std::string_view(line).substr(start, back - start + 1);
                    if(!name.empty() && !cpp_constants::is_keyword(name))
                    {
                        state.inParamList = true;
                        state.parenDepth = 1;
                        if(lineHasParamListStart)
                            *lineHasParamListStart = true;
                        ++i;
                        continue;
                    }
                }
            }
            if(state.inParamList)
                ++state.parenDepth;
            ++i;
            continue;
        }
        if(c == ')' && state.inParamList)
        {
            if(state.parenDepth > 0)
                --state.parenDepth;
            if(state.parenDepth == 0)
                state.inParamList = false;
            ++i;
            continue;
        }
        ++i;
    }
}

void scan_line_for_cpp_function_context(const std::string& line,
                                        CppFunctionScanState& state,
                                        bool* lineHasFunctionStart)
{
    size_t i = 0;
    const size_t len = line.size();
    while(i < len)
    {
        if(state.inBlockComment)
        {
            size_t closePos = line.find("*/", i);
            if(text_utils::is_not_found(closePos))
                return;
            state.inBlockComment = false;
            i = closePos + 2;
            continue;
        }

        char c = line[i];
        if(c == '/' && i + 1 < len && line[i + 1] == '/')
            return;
        if(c == '/' && i + 1 < len && line[i + 1] == '*')
        {
            state.inBlockComment = true;
            i += 2;
            continue;
        }
        if(c == '"' || c == '\'')
        {
            char quote = c;
            ++i;
            while(i < len)
            {
                if(line[i] == '\\' && i + 1 < len)
                {
                    i += 2;
                    continue;
                }
                if(line[i] == quote)
                {
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }
        if(c == '{')
        {
            ++state.braceDepth;
            if(state.pendingFunction)
            {
                state.inFunction = true;
                state.functionBraceDepth = state.braceDepth;
                state.pendingFunction = false;
                if(lineHasFunctionStart)
                    *lineHasFunctionStart = true;
            }
            ++i;
            continue;
        }
        if(c == '}')
        {
            if(state.inFunction && state.braceDepth == state.functionBraceDepth)
                state.inFunction = false;
            if(state.braceDepth > 0)
                --state.braceDepth;
            ++i;
            continue;
        }
        if(c == ';')
        {
            if(state.pendingFunction)
                state.pendingFunction = false;
            ++i;
            continue;
        }
        if(text_utils::is_alpha(c) || c == '_' || c == '~')
        {
            size_t start = i;
            ++i;
            while(i < len && (text_utils::is_alpha(line[i]) ||
                              text_utils::is_digit(line[i]) || line[i] == '_'))
            {
                ++i;
            }
            std::string_view ident(line.data() + start, i - start);
            if(ident == "if" || ident == "for" || ident == "while" ||
               ident == "switch" || ident == "catch" || ident == "return" ||
               ident == "sizeof" || ident == "static_cast" ||
               ident == "reinterpret_cast" || ident == "dynamic_cast" ||
               ident == "const_cast")
            {
                continue;
            }
            if(cpp_constants::is_keyword(ident))
                continue;
            size_t j = i;
            while(j < len && text_utils::is_space(line[j]))
                ++j;
            if(j < len && line[j] == '(')
                state.pendingFunction = true;
            continue;
        }
        ++i;
    }
}

std::filesystem::path find_mlang_root(const std::filesystem::path& start)
{
    std::error_code ec;
    std::filesystem::path dir =
        start.empty() ? std::filesystem::current_path(ec) : start;
    if(ec)
        return {};

    for(;;)
    {
        if(std::filesystem::exists(dir / ".mlangd", ec) ||
           std::filesystem::exists(dir / "mlang_commands.json", ec))
            return dir;
        if(dir.has_parent_path())
        {
            auto parent = dir.parent_path();
            if(parent == dir)
                break;
            dir = parent;
        }
        else
        {
            break;
        }
    }

    return std::filesystem::current_path(ec);
}

struct MlangConfig
{
    std::string commandsJson;
    std::string buildDir;
    bool caseInsensitive = false;
    bool caseInsensitiveSet = false;
};

MlangConfig parse_mlangd(const std::filesystem::path& path)
{
    MlangConfig cfg;
    std::ifstream in(path);
    if(!in)
        return cfg;

    std::string contents((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    if(contents.empty())
        return cfg;

    auto trim = [](std::string& s)
    {
        size_t start = 0;
        while(start < s.size() && text_utils::is_space(s[start]))
            ++start;
        size_t end = s.size();
        while(end > start && text_utils::is_space(s[end - 1]))
            --end;
        s = s.substr(start, end - start);
    };

    // JSON format
    if(!contents.empty() && text_utils::contains(contents, '{'))
    {
        json_utils::Document root;
        if(json_utils::parse(root, contents) && root.IsObject())
        {
            cfg.commandsJson = json_utils::get_string(root, "commands_json");
            cfg.buildDir = json_utils::get_string(root, "build_dir");
            if(json_utils::has(root, "case_insensitive"))
            {
                cfg.caseInsensitive =
                    json_utils::get_bool(root, "case_insensitive", false);
                cfg.caseInsensitiveSet = true;
            }
        }
        return cfg;
    }

    // key=value format
    std::istringstream iss(contents);
    std::string line;
    while(std::getline(iss, line))
    {
        trim(line);
        if(line.empty())
            continue;
        if(line.starts_with("#") || line.starts_with("//"))
            continue;
        auto pos = line.find('=');
        if(text_utils::is_not_found(pos))
            continue;
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        trim(key);
        trim(value);
        if(key == "commands_json")
            cfg.commandsJson = value;
        else if(key == "build_dir")
            cfg.buildDir = value;
        else if(key == "case_insensitive")
        {
            cfg.caseInsensitive =
                (value == "1" || value == "true" || value == "yes");
            cfg.caseInsensitiveSet = true;
        }
    }

    return cfg;
}
} // namespace

SyntaxHighlighter::SyntaxHighlighter(Editor* editor) : editor(editor) {}

void SyntaxHighlighter::ensureCppMemberIndex() const
{
    if(cppMemberIndexLoaded)
        return;
    cppMemberIndexLoaded = true;
    cppMemberNames.clear();
    cppClassNames.clear();

    if(!editor)
        return;

    std::filesystem::path root =
        editor->projectRoot.empty()
            ? std::filesystem::current_path()
            : std::filesystem::path(editor->projectRoot);
    std::error_code ec;
    if(root.empty() || !std::filesystem::exists(root, ec))
        return;

    auto is_header = [](const std::filesystem::path& path) -> bool
    {
        std::string ext = path.extension().string();
        for(char& c : ext)
            c = (char)std::tolower((unsigned char)c);
        return ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".hh" ||
               ext == ".inl" || ext == ".tpp";
    };

    auto is_skip_dir = [](const std::filesystem::path& path) -> bool
    {
        std::string name = path.filename().string();
        return name == ".git" || name == ".venv" || name == "build" ||
               name == "node_modules" || name == "dist" || name == "out";
    };

    bool inBlockComment = false;
    for(std::filesystem::recursive_directory_iterator
            it(root, std::filesystem::directory_options::skip_permission_denied,
               ec),
        end;
        it != end; ++it)
    {
        if(it->is_directory(ec) && is_skip_dir(it->path()))
        {
            it.disable_recursion_pending();
            continue;
        }
        if(!it->is_regular_file(ec))
            continue;
        if(!is_header(it->path()))
            continue;

        std::ifstream in(it->path());
        if(!in)
            continue;

        bool inClass = false;
        bool pendingClass = false;
        int braceDepth = 0;
        std::string line;
        while(std::getline(in, line))
        {
            if(!line.empty() && line.back() == '\r')
                line.pop_back();

            // strip comments
            std::string cleaned;
            cleaned.reserve(line.size());
            for(size_t i = 0; i < line.size(); ++i)
            {
                if(inBlockComment)
                {
                    if(i + 1 < line.size() && line[i] == '*' &&
                       line[i + 1] == '/')
                    {
                        inBlockComment = false;
                        ++i;
                    }
                    continue;
                }
                if(i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*')
                {
                    inBlockComment = true;
                    ++i;
                    continue;
                }
                if(i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/')
                {
                    break;
                }
                cleaned.push_back(line[i]);
            }

            if(cleaned.empty())
                continue;

            auto has_keyword = [&](std::string_view kw) -> bool
            { return text_utils::contains(cleaned, kw); };

            auto parse_class_name = [&]()
            {
                size_t pos = cleaned.find("class ");
                size_t skip = 6;
                if(text_utils::is_not_found(pos))
                {
                    pos = cleaned.find("struct ");
                    skip = 7;
                }
                if(text_utils::is_not_found(pos))
                    return;
                size_t i = pos + skip;
                while(i < cleaned.size() && text_utils::is_space(cleaned[i]))
                    ++i;
                size_t start = i;
                while(i < cleaned.size() &&
                      (text_utils::is_alpha(cleaned[i]) ||
                       text_utils::is_digit(cleaned[i]) || cleaned[i] == '_'))
                {
                    ++i;
                }
                if(i > start)
                    cppClassNames.insert(
                        std::string(cleaned.substr(start, i - start)));
            };

            if(!inClass && !pendingClass &&
               (has_keyword("class ") || has_keyword("struct ")))
            {
                parse_class_name();
                if(text_utils::contains(cleaned, '{'))
                {
                    inClass = true;
                    braceDepth = 0;
                }
                else
                {
                    pendingClass = true;
                }
            }

            if(pendingClass && text_utils::contains(cleaned, '{'))
            {
                pendingClass = false;
                inClass = true;
                braceDepth = 0;
            }
            if(pendingClass && text_utils::contains(cleaned, ';') &&
               !text_utils::contains(cleaned, '{'))
            {
                pendingClass = false;
            }

            bool parseMembers = inClass;
            for(char c : cleaned)
            {
                if(c == '{')
                    ++braceDepth;
                else if(c == '}')
                    --braceDepth;
            }
            bool classEnded = inClass && braceDepth <= 0;

            auto finalize_line = [&]()
            {
                if(classEnded)
                    inClass = false;
            };

            if(!parseMembers)
            {
                finalize_line();
                continue;
            }

            std::string_view memberLine(cleaned);
            size_t bracePos = memberLine.rfind('{');
            if(text_utils::is_found(bracePos))
                memberLine = memberLine.substr(bracePos + 1);
            size_t closePos = memberLine.find('}');
            if(text_utils::is_found(closePos))
                memberLine = memberLine.substr(0, closePos);

            if(has_keyword("typedef") || has_keyword("using ") ||
               has_keyword("enum ") || has_keyword("template"))
            {
                finalize_line();
                continue;
            }

            size_t segStart = 0;
            bool foundSemi = false;
            while(segStart < memberLine.size())
            {
                size_t semi = memberLine.find(';', segStart);
                if(text_utils::is_not_found(semi))
                    break;
                foundSemi = true;
                std::string_view stmt =
                    memberLine.substr(segStart, semi - segStart);
                if(text_utils::contains(stmt, '('))
                {
                    segStart = semi + 1;
                    continue;
                }

                auto eq = stmt.find('=');
                if(text_utils::is_found(eq))
                    stmt = stmt.substr(0, eq);

                // split by commas to catch "int a, b"
                size_t start = 0;
                while(start < stmt.size())
                {
                    size_t comma = stmt.find(',', start);
                    std::string_view part =
                        stmt.substr(start, text_utils::is_not_found(comma)
                                               ? text_utils::npos()
                                               : comma - start);
                    // find last identifier
                    int i = (int)part.size() - 1;
                    while(i >= 0 && text_utils::is_space(part[i]))
                        --i;
                    int end = i;
                    while(i >= 0 &&
                          (text_utils::is_alpha(part[i]) ||
                           text_utils::is_digit(part[i]) || part[i] == '_'))
                    {
                        --i;
                    }
                    int begin = i + 1;
                    if(begin <= end)
                    {
                        std::string name(part.substr(begin, end - begin + 1));
                        if(!name.empty())
                            cppMemberNames.insert(name);
                    }
                    if(text_utils::is_not_found(comma))
                        break;
                    start = comma + 1;
                }
                segStart = semi + 1;
            }

            if(!foundSemi)
            {
                finalize_line();
                continue;
            }

            finalize_line();
        }
    }
}

void SyntaxHighlighter::ensureSystemIncludeDirsLoaded() const
{
    if(systemIncludeDirsLoaded)
        return;
    systemIncludeDirsLoaded = true;
    systemIncludeDirs.clear();

    if(!editor)
        return;

    std::filesystem::path baseDir;
    if(!editor->clangdLspCompileCommandsDir.empty())
        baseDir = editor->clangdLspCompileCommandsDir;
    else if(!editor->projectRoot.empty())
        baseDir = editor->projectRoot;
    else
        baseDir = std::filesystem::current_path();

    std::filesystem::path ccPath = baseDir / "compile_commands.json";
    if(!std::filesystem::exists(ccPath))
    {
        std::filesystem::path alt = baseDir / "build" / "compile_commands.json";
        if(std::filesystem::exists(alt))
        {
            ccPath = alt;
            baseDir = alt.parent_path();
        }
        else
        {
            return;
        }
    }

    std::ifstream in(ccPath);
    if(!in)
        return;

    json_utils::Document root;
    if(!json_utils::parse(root, in) || !root.IsArray())
        return;

    std::unordered_set<std::string> seen;

    for(const auto& item : root.GetArray())
    {
        if(!item.IsObject())
            continue;
        std::string directory =
            json_utils::get_string(item, "directory", baseDir.string());
        std::vector<std::string> args;
        const json_utils::Value* arguments =
            json_utils::find(item, "arguments");
        if(arguments && arguments->IsArray())
        {
            for(const auto& arg : arguments->GetArray())
            {
                if(arg.IsString())
                    args.emplace_back(arg.GetString(), arg.GetStringLength());
            }
        }
        else
        {
            const json_utils::Value* command =
                json_utils::find(item, "command");
            if(command && command->IsString())
            {
                args = split_command_line(std::string(
                    command->GetString(), command->GetStringLength()));
            }
        }
        if(args.empty())
            continue;

        auto add_path = [&](const std::string& raw)
        {
            if(raw.empty())
                return;
            std::filesystem::path path(raw);
            if(path.is_relative())
                path = std::filesystem::path(directory) / path;
            path = path.lexically_normal();
            std::error_code ec;
            if(!std::filesystem::exists(path, ec) ||
               !std::filesystem::is_directory(path, ec))
                return;
            std::string key = path.string();
            if(seen.insert(key).second)
                systemIncludeDirs.push_back(path);
        };

        for(size_t i = 0; i < args.size(); ++i)
        {
            const std::string& arg = args[i];
            auto consume = [&](const std::string& prefix) -> bool
            {
                if(arg == prefix)
                {
                    if(i + 1 < args.size())
                    {
                        add_path(args[i + 1]);
                        ++i;
                    }
                    return true;
                }
                if(arg.rfind(prefix, 0) == 0 && arg.size() > prefix.size())
                {
                    add_path(arg.substr(prefix.size()));
                    return true;
                }
                return false;
            };

            if(consume("-I"))
                continue;
            if(consume("-isystem"))
                continue;
            if(consume("-iquote"))
                continue;
        }
    }
}

bool SyntaxHighlighter::isSystemInclude(std::string_view header) const
{
    ensureSystemIncludeDirsLoaded();
    if(systemIncludeDirs.empty())
        return false;

    for(const auto& dir : systemIncludeDirs)
    {
        std::filesystem::path path = dir / std::string(header);
        std::error_code ec;
        if(std::filesystem::exists(path, ec))
            return true;
    }
    return false;
}

bool SyntaxHighlighter::isFileType(FileType type) const
{
    return editor && editor->isFileType(type);
}

void SyntaxHighlighter::ensureMlangTokensLoaded() const
{
    if(!editor || !editor->mlangTokenCache)
        return;

    auto& cache = *editor->mlangTokenCache;
    std::filesystem::path start;
    if(editor->currentBuffer && !editor->currentBuffer->filename.empty())
    {
        start = std::filesystem::path(editor->currentBuffer->filename)
                    .parent_path();
    }
    else if(editor->filename && !editor->filename->empty())
    {
        start = std::filesystem::path(*editor->filename).parent_path();
    }
    else if(!editor->projectRoot.empty())
    {
        start = std::filesystem::path(editor->projectRoot);
    }
    std::filesystem::path root = find_mlang_root(start);
    std::string rootStr = root.empty() ? std::string{} : root.string();
    const bool hasExplicitLspPath = !editor->mlangLspPath.empty() &&
                                    editor->mlangLspPath != "mlangd" &&
                                    editor->mlangLspPath != "mlangd-mla";
    const std::string effectiveLspPath =
        hasExplicitLspPath ? editor->mlangLspPath : std::string{};

    if(cache.loaded && cache.root == rootStr &&
       cache.lspPath == effectiveLspPath && cache.builtinTypesLoaded)
    {
        return;
    }

    cache.loaded = true;
    cache.available = false;
    cache.caseInsensitive = false;
    cache.tokenTypes.clear();
    cache.builtinTypes.clear();
    cache.builtinTypesLoaded = false;
    cache.builtinMacros.clear();
    cache.builtinMacrosLoaded = false;
    cache.builtinAttributes.clear();
    cache.builtinAttributesLoaded = false;
    cache.builtinFunctions.clear();
    cache.builtinFunctionsLoaded = false;
    cache.root = rootStr;
    cache.configPath.clear();
    cache.lspPath = effectiveLspPath;

    std::filesystem::path configPath;
    MlangConfig cfg;
    if(!rootStr.empty())
    {
        configPath = root / ".mlangd";
        cfg = parse_mlangd(configPath);
        if(!cfg.commandsJson.empty() || !cfg.buildDir.empty() ||
           cfg.caseInsensitiveSet)
        {
            cache.configPath = configPath.string();
            if(cfg.caseInsensitiveSet)
                cache.caseInsensitive = cfg.caseInsensitive;
        }
    }

    auto resolve_path = [&](const std::string& value) -> std::filesystem::path
    {
        std::filesystem::path p(value);
        if(p.empty())
            return {};
        if(p.is_relative() && !rootStr.empty())
            p = root / p;
        return p;
    };

    auto load_from_path = [&](const std::filesystem::path& path,
                              bool allowJsonCase) -> bool
    {
        std::error_code ec;
        if(!std::filesystem::exists(path, ec))
            return false;

        std::ifstream in(path);
        if(!in)
            return false;

        json_utils::Document root;
        if(!json_utils::parse(root, in) || !root.IsObject())
            return false;

        if(allowJsonCase)
        {
            cache.caseInsensitive = json_utils::get_bool(
                root, "case_insensitive", cache.caseInsensitive);
        }

        auto add_tokens =
            [&](std::string_view typeName, const json_utils::Value& items)
        {
            auto tokenType = parse_token_type(typeName);
            if(!tokenType || !items.IsArray())
                return;

            for(const auto& item : items.GetArray())
            {
                if(!item.IsString())
                    continue;
                std::string key(item.GetString(), item.GetStringLength());
                if(cache.caseInsensitive)
                    key = ascii_lower(key);
                cache.tokenTypes[key] = *tokenType;
            }
        };

        if(json_utils::has(root, "tokens"))
        {
            const auto* tokens = json_utils::find(root, "tokens");
            if(tokens && tokens->IsArray())
            {
                for(const auto& entry : tokens->GetArray())
                {
                    if(!entry.IsObject())
                        continue;
                    std::string type = json_utils::get_string(entry, "type");
                    if(const auto* items = json_utils::find(entry, "items"))
                        add_tokens(type, *items);
                }
            }
            else if(tokens && tokens->IsObject())
            {
                for(auto it = tokens->MemberBegin(); it != tokens->MemberEnd();
                    ++it)
                {
                    std::string_view typeName(it->name.GetString(),
                                              it->name.GetStringLength());
                    add_tokens(typeName, it->value);
                }
            }
        }

        if(json_utils::has(root, "builtin_types"))
        {
            const auto* types = json_utils::find(root, "builtin_types");
            if(types && types->IsArray())
            {
                for(const auto& entry : types->GetArray())
                {
                    if(!entry.IsObject())
                        continue;
                    std::string name = json_utils::get_string(entry, "name");
                    std::string path = json_utils::get_string(entry, "path");
                    int line = json_utils::get_int(entry, "line", 1);
                    if(name.empty() || path.empty())
                        continue;
                    if(cache.caseInsensitive)
                        name = ascii_lower(name);
                    MlangTokenCache::BuiltinTypeDef def;
                    def.path = path;
                    def.line = line > 0 ? line - 1 : 0;
                    cache.builtinTypes.emplace(std::move(name), std::move(def));
                }
            }
        }

        if(json_utils::has(root, "builtin_macros"))
        {
            const auto* macros = json_utils::find(root, "builtin_macros");
            if(macros && macros->IsArray())
            {
                for(const auto& entry : macros->GetArray())
                {
                    if(!entry.IsObject())
                        continue;
                    std::string name = json_utils::get_string(entry, "name");
                    std::string path = json_utils::get_string(entry, "path");
                    int line = json_utils::get_int(entry, "line", 1);
                    if(name.empty() || path.empty())
                        continue;
                    if(cache.caseInsensitive)
                        name = ascii_lower(name);
                    MlangTokenCache::BuiltinTypeDef def;
                    def.path = path;
                    def.line = line > 0 ? line - 1 : 0;
                    cache.builtinMacros.emplace(std::move(name),
                                                std::move(def));
                }
            }
        }

        if(json_utils::has(root, "builtin_attributes"))
        {
            const auto* attrs = json_utils::find(root, "builtin_attributes");
            if(attrs && attrs->IsArray())
            {
                for(const auto& entry : attrs->GetArray())
                {
                    if(!entry.IsObject())
                        continue;
                    std::string name = json_utils::get_string(entry, "name");
                    std::string path = json_utils::get_string(entry, "path");
                    int line = json_utils::get_int(entry, "line", 1);
                    if(name.empty() || path.empty())
                        continue;
                    if(cache.caseInsensitive)
                        name = ascii_lower(name);
                    MlangTokenCache::BuiltinTypeDef def;
                    def.path = path;
                    def.line = line > 0 ? line - 1 : 0;
                    cache.builtinAttributes.emplace(std::move(name),
                                                    std::move(def));
                }
            }
        }

        if(json_utils::has(root, "builtin_functions"))
        {
            const auto* fns = json_utils::find(root, "builtin_functions");
            if(fns && fns->IsArray())
            {
                for(const auto& entry : fns->GetArray())
                {
                    if(!entry.IsObject())
                        continue;
                    std::string name = json_utils::get_string(entry, "name");
                    std::string path = json_utils::get_string(entry, "path");
                    int line = json_utils::get_int(entry, "line", 1);
                    if(name.empty() || path.empty())
                        continue;
                    if(cache.caseInsensitive)
                        name = ascii_lower(name);
                    MlangTokenCache::BuiltinTypeDef def;
                    def.path = path;
                    def.line = line > 0 ? line - 1 : 0;
                    cache.builtinFunctions.emplace(name, def);
                    auto sep = name.rfind("::");
                    if(text_utils::is_found(sep) && sep + 2 < name.size())
                    {
                        std::string base = name.substr(sep + 2);
                        cache.builtinFunctions.emplace(std::move(base), def);
                    }
                }
            }
        }

        if(!cache.builtinTypes.empty())
        {
            for(const auto& kv : cache.builtinTypes)
            {
                cache.tokenTypes.emplace(kv.first, TOKEN_TYPE);
            }
        }

        cache.available = !cache.tokenTypes.empty();
        cache.builtinTypesLoaded = !cache.builtinTypes.empty();
        if(!cache.builtinTypesLoaded)
            ensure_builtin_types_loaded(cache);
        if(!cache.builtinMacrosLoaded)
            ensure_builtin_macros_loaded(cache);
        if(!cache.builtinAttributesLoaded)
            ensure_builtin_attributes_loaded(cache);
        if(!cache.builtinFunctionsLoaded)
            ensure_builtin_functions_loaded(cache);
        if(cache.builtinTypesLoaded)
        {
            for(const auto& kv : cache.builtinTypes)
                cache.tokenTypes.emplace(kv.first, TOKEN_TYPE);
            cache.available = true;
        }
        if(cache.builtinMacrosLoaded)
        {
            for(const auto& kv : cache.builtinMacros)
                cache.tokenTypes.emplace(kv.first, TOKEN_FUNCTION);
            cache.available = true;
        }
        if(cache.builtinAttributesLoaded)
        {
            for(const auto& kv : cache.builtinAttributes)
                cache.tokenTypes.emplace(kv.first, TOKEN_KEYWORD);
            cache.available = true;
        }
        if(cache.builtinFunctionsLoaded)
        {
            for(const auto& kv : cache.builtinFunctions)
                cache.tokenTypes.emplace(kv.first, TOKEN_FUNCTION);
            cache.available = true;
        }
        return cache.available;
    };

    std::vector<std::filesystem::path> candidates;
    if(!cfg.commandsJson.empty())
        candidates.push_back(resolve_path(cfg.commandsJson));
    else
    {
        std::string buildDir = cfg.buildDir.empty() ? "build" : cfg.buildDir;
        if(!rootStr.empty())
            candidates.push_back(root / buildDir / "mlang_commands.json");
    }
    if(hasExplicitLspPath)
    {
        std::filesystem::path lspPath = editor->mlangLspPath;
        if(lspPath.is_relative())
            lspPath = std::filesystem::absolute(lspPath);
        std::filesystem::path dir = lspPath.parent_path();
        if(!dir.empty())
            candidates.push_back(dir / "mlang_commands.json");
    }

    candidates.push_back(std::filesystem::current_path() /
                         "mlang_commands.json");

    for(const auto& candidate : candidates)
    {
        if(load_from_path(candidate, !cfg.caseInsensitiveSet))
            return;
    }

    // Fallback: load builtin types from installed stdlib even if there is no
    // mlang_commands.json in the project.
    ensure_builtin_types_loaded(cache);
    if(cache.builtinTypesLoaded)
    {
        for(const auto& kv : cache.builtinTypes)
            cache.tokenTypes.emplace(kv.first, TOKEN_TYPE);
        cache.available = true;
    }
    ensure_builtin_macros_loaded(cache);
    if(cache.builtinMacrosLoaded)
    {
        for(const auto& kv : cache.builtinMacros)
            cache.tokenTypes.emplace(kv.first, TOKEN_FUNCTION);
        cache.available = true;
    }
    ensure_builtin_attributes_loaded(cache);
    if(cache.builtinAttributesLoaded)
    {
        for(const auto& kv : cache.builtinAttributes)
            cache.tokenTypes.emplace(kv.first, TOKEN_KEYWORD);
        cache.available = true;
    }
    ensure_builtin_functions_loaded(cache);
    if(cache.builtinFunctionsLoaded)
    {
        for(const auto& kv : cache.builtinFunctions)
            cache.tokenTypes.emplace(kv.first, TOKEN_FUNCTION);
        cache.available = true;
    }
}

std::optional<TokenType>
SyntaxHighlighter::lookupMlangTokenType(std::string_view word) const
{
    ensureMlangTokensLoaded();
    if(!editor || !editor->mlangTokenCache ||
       !editor->mlangTokenCache->available)
        return std::nullopt;

    std::string key = editor->mlangTokenCache->caseInsensitive
                          ? ascii_lower(word)
                          : std::string(word);
    auto it = editor->mlangTokenCache->tokenTypes.find(key);
    if(it == editor->mlangTokenCache->tokenTypes.end())
        return std::nullopt;
    return it->second;
}

std::string SyntaxHighlighter::getColorCode(TokenType type) const
{
    if(!editor)
        return {};
    return editor->theme.syntax(type);
}

std::vector<Token> SyntaxHighlighter::tokenizeLine(
    const std::string& line, bool& inBlockComment, bool& inTomlMultiline,
    char& tomlQuote, bool& inMarkupFence, char& markupFenceChar,
    bool inCppMethodContext, bool inCppFunctionContext,
    bool inCppParamListContext) const
{
    if(!editor)
        return {};

    std::vector<Token> extraTypeTokens;
    std::unordered_set<std::string> localDeclNames;

    auto isRobotKeyword = [&](std::string_view word)
    { return editor->isRobotKeyword(word); };
    auto isRobotSetting = [&](std::string_view cell)
    { return editor->isRobotSetting(cell); };
    auto isRobotCustomKeyword = [&](std::string_view word)
    { return editor->isRobotCustomKeyword(word); };
    const bool syntaxRobotHighlightTitles = editor->syntaxRobotHighlightTitles;
    const bool syntaxRobotHighlightCalls = editor->syntaxRobotHighlightCalls;
    const bool syntaxJson = editor->syntaxJson;
    const bool syntaxYaml = editor->syntaxYaml;
    const bool syntaxRobotKeywords = editor->syntaxRobotKeywords;
    const bool syntaxCppHighlightMembers = editor->syntaxCppHighlightMembers;
    const bool syntaxCppHighlightTypeNames =
        editor->syntaxCppHighlightTypeNames;
    const bool syntaxCppHighlightImplicitMembers =
        editor->syntaxCppHighlightImplicitMembers;
    const bool syntaxMlangHighlightTypes = editor->syntaxMlangHighlightTypes;
    const bool syntaxMlangHighlightBuiltinDocs =
        editor->syntaxMlangHighlightBuiltinDocs;
    const bool syntaxCppHighlightParamTypes =
        editor->syntaxCppHighlightParamTypes;
    const bool syntaxCppHighlightSystemIncludes =
        editor->syntaxCppHighlightSystemIncludes;
    const bool isJs = isFileType<FileType::JavaScript>() ||
                      isFileType<FileType::TypeScript>();
    const bool isCss = isFileType<FileType::Css>();

    if(isFileType<FileType::Cpp>() && syntaxCppHighlightTypeNames)
        ensureCppMemberIndex();

    int paramListStart = -1;
    int paramListEnd = -1;
    bool paramListOpen = false;
    if(isFileType<FileType::Cpp>() &&
       (syntaxCppHighlightParamTypes || syntaxCppHighlightTypeNames))
    {
        bool inString = false;
        char quote = 0;
        bool inLineComment = false;
        bool inBlock = inBlockComment;
        int parenDepth = 0;
        for(int idx = 0; idx < (int)line.size(); ++idx)
        {
            char ch = line[idx];
            if(inLineComment)
                break;
            if(inBlock)
            {
                if(ch == '*' && idx + 1 < (int)line.size() &&
                   line[idx + 1] == '/')
                {
                    inBlock = false;
                    ++idx;
                }
                continue;
            }
            if(inString)
            {
                if(ch == '\\' && idx + 1 < (int)line.size())
                {
                    ++idx;
                    continue;
                }
                if(ch == quote)
                {
                    inString = false;
                }
                continue;
            }
            if(ch == '"' || ch == '\'')
            {
                inString = true;
                quote = ch;
                continue;
            }
            if(ch == '/' && idx + 1 < (int)line.size() && line[idx + 1] == '/')
            {
                inLineComment = true;
                continue;
            }
            if(ch == '/' && idx + 1 < (int)line.size() && line[idx + 1] == '*')
            {
                inBlock = true;
                ++idx;
                continue;
            }
            if(ch == '(')
            {
                if(parenDepth == 0 && paramListStart < 0)
                {
                    int back = idx - 1;
                    while(back >= 0 && text_utils::is_space(line[back]))
                        --back;
                    if(back >= 0 && line[back] == ']')
                    {
                        parenDepth = 1;
                        paramListStart = idx;
                        continue;
                    }
                    int end = back;
                    while(end >= 0 &&
                          (text_utils::is_alpha(line[end]) ||
                           text_utils::is_digit(line[end]) || line[end] == '_'))
                        --end;
                    int start = end + 1;
                    if(start <= back)
                    {
                        std::string_view name = std::string_view(line).substr(
                            start, back - start + 1);
                        if(!name.empty() && !cpp_constants::is_keyword(name))
                        {
                            parenDepth = 1;
                            paramListStart = idx;
                            continue;
                        }
                    }
                }
                ++parenDepth;
                continue;
            }
            if(ch == ')' && parenDepth > 0)
            {
                --parenDepth;
                if(parenDepth == 0 && paramListStart >= 0)
                {
                    size_t after = idx + 1;
                    while(after < line.size() &&
                          text_utils::is_space(line[after]))
                        ++after;
                    if(after < line.size())
                    {
                        std::string_view tail =
                            std::string_view(line).substr(after);
                        if(tail.starts_with("{") || tail.starts_with(";") ||
                           tail.starts_with(":") || tail.starts_with("const") ||
                           tail.starts_with("noexcept") ||
                           tail.starts_with("->") ||
                           tail.starts_with("override") ||
                           tail.starts_with("final"))
                        {
                            paramListEnd = idx;
                        }
                    }
                }
                continue;
            }
        }
        if(paramListStart >= 0 && parenDepth > 0 && paramListEnd < 0)
            paramListOpen = true;
    }

    if(isFileType<FileType::Cpp>())
    {
        size_t first = 0;
        while(first < line.size() && text_utils::is_space(line[first]))
            ++first;
        if(first < line.size() && line[first] == '#')
        {
            std::vector<Token> preprocessorTokens;
            preprocessorTokens.push_back(
                {TOKEN_PREPROCESSOR, (int)first, (int)(line.size() - first)});
            if(syntaxCppHighlightSystemIncludes)
            {
                size_t pos = first + 1;
                while(pos < line.size() && text_utils::is_space(line[pos]))
                    ++pos;
                constexpr std::string_view includeKw = "include";
                constexpr std::string_view includeNextKw = "include_next";
                auto starts_with_kw = [&](std::string_view kw) -> bool
                {
                    if(pos + kw.size() > line.size())
                        return false;
                    if(line.compare(pos, kw.size(), kw) != 0)
                        return false;
                    size_t end = pos + kw.size();
                    if(end < line.size() &&
                       (text_utils::is_alpha(line[end]) ||
                        text_utils::is_digit(line[end]) || line[end] == '_'))
                        return false;
                    return true;
                };
                if(starts_with_kw(includeKw) || starts_with_kw(includeNextKw))
                {
                    pos += starts_with_kw(includeKw) ? includeKw.size()
                                                     : includeNextKw.size();
                    while(pos < line.size() && text_utils::is_space(line[pos]))
                        ++pos;
                    if(pos < line.size() &&
                       (line[pos] == '<' || line[pos] == '"'))
                    {
                        char open = line[pos];
                        char close = (open == '<') ? '>' : '"';
                        size_t start = pos;
                        ++pos;
                        size_t end = line.find(close, pos);
                        if(text_utils::is_found(end) && end > pos)
                        {
                            std::string_view header =
                                std::string_view(line).substr(pos, end - pos);
                            if(isSystemInclude(header))
                            {
                                preprocessorTokens.push_back(
                                    {TOKEN_STRING, (int)start,
                                     (int)(end - start + 1)});
                            }
                        }
                    }
                }
            }
            return preprocessorTokens;
        }
    }

    if(isFileType<FileType::Robot>())
    {
        std::vector<Token> tokens;
        std::string_view sv{line};
        const int len = static_cast<int>(sv.size());
        int i = 0;

        int first = 0;
        while(first < len && text_utils::is_space(sv[first]))
            ++first;

        if(first >= len)
            return tokens;

        std::string_view trimmed = sv.substr(first);
        if(trimmed.starts_with("#"))
        {
            tokens.push_back({TOKEN_COMMENT, first, len - first});
            return tokens;
        }

        if(trimmed.starts_with("***") && trimmed.ends_with("***"))
        {
            tokens.push_back({TOKEN_KEYWORD, first, len - first});
            return tokens;
        }

        bool isContinuation = false;
        if(trimmed.starts_with("..."))
        {
            tokens.push_back({TOKEN_OPERATOR, first, 3});
            i = first + 3;
            isContinuation = true;
        }

        if(sv[first] == '[')
        {
            size_t close = sv.find(']', (size_t)first + 1);
            if(text_utils::is_found(close))
            {
                tokens.push_back(
                    {TOKEN_KEYWORD, first, (int)(close - first + 1)});
            }
        }

        auto is_keyword = [&](std::string_view word) -> bool
        { return isRobotKeyword(word); };

        auto parse_first_cell = [&](int start) -> std::pair<int, int>
        {
            int idx = start;
            int spaceRun = 0;
            for(; idx < len; ++idx)
            {
                if(sv[idx] == '\t')
                    break;
                if(sv[idx] == ' ')
                {
                    ++spaceRun;
                    if(spaceRun >= 2)
                        break;
                }
                else
                {
                    spaceRun = 0;
                }
            }
            int end = idx;
            if(end > start && sv[end - 1] == ' ')
                --end;
            if(end < start)
                end = start;
            return {start, end};
        };

        if(!isContinuation)
        {
            struct CellRange
            {
                int start = 0;
                int end = 0;
            };

            auto parse_cells = [&](int start) -> std::vector<CellRange>
            {
                std::vector<CellRange> cells;
                int idx = start;
                while(idx < len)
                {
                    while(idx < len && text_utils::is_space(sv[idx]))
                        ++idx;
                    if(idx >= len)
                        break;
                    int cellStart = idx;
                    int spaceRun = 0;
                    for(; idx < len; ++idx)
                    {
                        if(sv[idx] == '\t')
                            break;
                        if(sv[idx] == ' ')
                        {
                            ++spaceRun;
                            if(spaceRun >= 2)
                                break;
                        }
                        else
                        {
                            spaceRun = 0;
                        }
                    }
                    int cellEnd = idx;
                    if(cellEnd > cellStart && sv[cellEnd - 1] == ' ')
                        --cellEnd;
                    if(cellEnd < cellStart)
                        cellEnd = cellStart;
                    cells.push_back({cellStart, cellEnd});
                    while(idx < len && text_utils::is_space(sv[idx]))
                        ++idx;
                }
                return cells;
            };

            auto trim_cell = [&](std::string_view value) -> std::string_view
            {
                while(!value.empty() && text_utils::is_space(value.front()))
                    value.remove_prefix(1);
                while(!value.empty() && text_utils::is_space(value.back()))
                    value.remove_suffix(1);
                return value;
            };

            auto is_assignment_cell = [&](std::string_view cell) -> bool
            {
                cell = trim_cell(cell);
                if(cell.empty())
                    return false;
                if(cell == "=")
                    return true;
                return cell.back() == '=';
            };

            auto cells = parse_cells(first);
            int keywordCellIndex = 0;
            for(size_t c = 0; c < cells.size(); ++c)
            {
                std::string_view cell = sv.substr(
                    cells[c].start,
                    static_cast<size_t>(cells[c].end - cells[c].start));
                if(is_assignment_cell(cell))
                {
                    keywordCellIndex = (int)c + 1;
                    break;
                }
            }

            auto [cellStart, cellEnd] = parse_first_cell(first);
            if(cellEnd > cellStart)
            {
                std::string_view cell = sv.substr(
                    cellStart, static_cast<size_t>(cellEnd - cellStart));
                bool isSettingCell = cell.starts_with('[');
                bool highlightedCell = false;
                if(isRobotSetting(cell))
                {
                    tokens.push_back(
                        {TOKEN_KEYWORD, cellStart, cellEnd - cellStart});
                    highlightedCell = true;
                }
                if(isRobotCustomKeyword(cell))
                {
                    tokens.push_back(
                        {TOKEN_FUNCTION, cellStart, cellEnd - cellStart});
                    highlightedCell = true;
                }
                if(!highlightedCell && syntaxRobotHighlightTitles &&
                   first == 0 && !isSettingCell)
                {
                    if(keywordCellIndex == 0)
                    {
                        tokens.push_back(
                            {TOKEN_FUNCTION, cellStart, cellEnd - cellStart});
                        highlightedCell = true;
                    }
                }
                if(!highlightedCell && syntaxRobotHighlightCalls &&
                   !isSettingCell)
                {
                    if(keywordCellIndex == 0)
                    {
                        tokens.push_back(
                            {TOKEN_FUNCTION, cellStart, cellEnd - cellStart});
                    }
                }
            }

            if(keywordCellIndex > 0 && keywordCellIndex < (int)cells.size())
            {
                const auto& range = cells[keywordCellIndex];
                if(range.end > range.start)
                {
                    std::string_view cell =
                        sv.substr(range.start,
                                  static_cast<size_t>(range.end - range.start));
                    bool isSettingCell = cell.starts_with('[');
                    if(!isSettingCell)
                    {
                        bool shouldHighlight = false;
                        if(isRobotCustomKeyword(cell) || isRobotKeyword(cell))
                            shouldHighlight = true;
                        else if(syntaxRobotHighlightCalls)
                            shouldHighlight = true;
                        if(shouldHighlight)
                        {
                            tokens.push_back({TOKEN_FUNCTION, range.start,
                                              range.end - range.start});
                        }
                    }
                }
            }
        }

        while(i < len)
        {
            if(text_utils::is_space(sv[i]))
            {
                ++i;
                continue;
            }

            if(sv[i] == '#' && (i == first || text_utils::is_space(sv[i - 1])))
            {
                tokens.push_back({TOKEN_COMMENT, i, len - i});
                break;
            }

            if((sv[i] == '$' || sv[i] == '@' || sv[i] == '&') && i + 1 < len &&
               sv[i + 1] == '{')
            {
                int start = i;
                i += 2;
                while(i < len && sv[i] != '}')
                    ++i;
                if(i < len)
                    ++i;
                tokens.push_back({TOKEN_TYPE, start, i - start});
                continue;
            }

            if(sv[i] == '"' || sv[i] == '\'')
            {
                char quote = sv[i];
                int start = i++;
                while(i < len && sv[i] != quote)
                {
                    if(sv[i] == '\\' && i + 1 < len)
                        i += 2;
                    else
                        ++i;
                }
                if(i < len)
                    ++i;
                tokens.push_back({TOKEN_STRING, start, i - start});
                continue;
            }

            int start = i;
            while(i < len && !text_utils::is_space(sv[i]) && sv[i] != '#')
                ++i;
            if(i > start)
            {
                std::string_view word = sv.substr(start, i - start);
                if(syntaxRobotKeywords && is_keyword(word))
                {
                    tokens.push_back({TOKEN_KEYWORD, start, i - start});
                }
            }
        }

        return tokens;
    }

    if(isFileType<FileType::Html>() || isFileType<FileType::Xml>())
    {
        std::vector<Token> tokens;
        std::string_view sv{line};
        const int len = static_cast<int>(sv.size());
        int i = 0;

        auto is_name_char = [](char c) -> bool
        {
            return text_utils::is_alpha(c) || text_utils::is_digit(c) ||
                   c == '-' || c == '_' || c == ':';
        };

        while(i < len)
        {
            if(i + 3 < len && sv.compare((size_t)i, 4, "<!--") == 0)
            {
                int start = i;
                int endPos = -1;
                for(int j = i + 4; j + 2 < len; ++j)
                {
                    if(sv[j] == '-' && sv[j + 1] == '-' && sv[j + 2] == '>')
                    {
                        endPos = j + 3;
                        break;
                    }
                }
                if(endPos < 0)
                {
                    tokens.push_back({TOKEN_COMMENT, start, len - start});
                    break;
                }
                tokens.push_back({TOKEN_COMMENT, start, endPos - start});
                i = endPos;
                continue;
            }

            if(sv[i] == '<')
            {
                int tagStart = i;
                ++i;
                if(i < len && (sv[i] == '/' || sv[i] == '!' || sv[i] == '?'))
                    ++i;

                int nameStart = i;
                while(i < len && is_name_char(sv[i]))
                    ++i;
                if(i > nameStart)
                {
                    tokens.push_back({TOKEN_KEYWORD, nameStart, i - nameStart});
                }

                while(i < len && sv[i] != '>')
                {
                    if(text_utils::is_space(sv[i]) || sv[i] == '/' ||
                       sv[i] == '=')
                    {
                        ++i;
                        continue;
                    }
                    if(sv[i] == '"' || sv[i] == '\'')
                    {
                        char quote = sv[i];
                        int start = i++;
                        while(i < len && sv[i] != quote)
                        {
                            if(sv[i] == '\\' && i + 1 < len)
                                i += 2;
                            else
                                ++i;
                        }
                        if(i < len)
                            ++i;
                        tokens.push_back({TOKEN_STRING, start, i - start});
                        continue;
                    }
                    int attrStart = i;
                    while(i < len && is_name_char(sv[i]))
                        ++i;
                    if(i > attrStart)
                    {
                        tokens.push_back(
                            {TOKEN_TYPE, attrStart, i - attrStart});
                        continue;
                    }
                    ++i;
                }

                if(i < len && sv[i] == '>')
                {
                    tokens.push_back({TOKEN_OPERATOR, tagStart, 1});
                    tokens.push_back({TOKEN_OPERATOR, i, 1});
                    ++i;
                }
                continue;
            }

            ++i;
        }

        return tokens;
    }

    if(isCss)
    {
        std::vector<Token> tokens;
        std::string_view sv{line};
        const int len = static_cast<int>(sv.size());
        int i = 0;

        auto is_ident_char = [](char c) -> bool
        {
            return text_utils::is_alpha(c) || text_utils::is_digit(c) ||
                   c == '-' || c == '_';
        };

        while(i < len)
        {
            if(inBlockComment)
            {
                size_t end = sv.find("*/", (size_t)i);
                if(text_utils::is_not_found(end))
                {
                    tokens.push_back({TOKEN_COMMENT, i, len - i});
                    return tokens;
                }
                int start = i;
                i = (int)end + 2;
                tokens.push_back({TOKEN_COMMENT, start, i - start});
                inBlockComment = false;
                continue;
            }

            if(i + 1 < len && sv[i] == '/' && sv[i + 1] == '*')
            {
                int start = i;
                size_t end = sv.find("*/", (size_t)i + 2);
                if(text_utils::is_not_found(end))
                {
                    tokens.push_back({TOKEN_COMMENT, start, len - start});
                    inBlockComment = true;
                    return tokens;
                }
                i = (int)end + 2;
                tokens.push_back({TOKEN_COMMENT, start, i - start});
                continue;
            }

            char c = sv[i];
            if(text_utils::is_space(c))
            {
                ++i;
                continue;
            }
            if(c == '"' || c == '\'')
            {
                char quote = c;
                int start = i++;
                while(i < len && sv[i] != quote)
                {
                    if(sv[i] == '\\' && i + 1 < len)
                        i += 2;
                    else
                        ++i;
                }
                if(i < len)
                    ++i;
                tokens.push_back({TOKEN_STRING, start, i - start});
                continue;
            }
            if(c == '@')
            {
                int start = i++;
                while(i < len && is_ident_char(sv[i]))
                    ++i;
                tokens.push_back({TOKEN_KEYWORD, start, i - start});
                continue;
            }
            if((c >= '0' && c <= '9'))
            {
                int start = i++;
                while(i < len && (text_utils::is_digit(sv[i]) || sv[i] == '.' ||
                                  sv[i] == '%'))
                {
                    ++i;
                }
                tokens.push_back({TOKEN_NUMBER, start, i - start});
                continue;
            }
            if(is_ident_char(c))
            {
                int start = i++;
                while(i < len && is_ident_char(sv[i]))
                    ++i;
                int look = i;
                while(look < len && text_utils::is_space(sv[look]))
                    ++look;
                if(look < len && sv[look] == ':')
                    tokens.push_back({TOKEN_TYPE, start, i - start});
                continue;
            }

            if(std::ispunct(static_cast<unsigned char>(c)))
            {
                tokens.push_back({TOKEN_OPERATOR, i, 1});
                ++i;
                continue;
            }

            ++i;
        }
        return tokens;
    }

    if(isFileType<FileType::Json>())
    {
        if(!syntaxJson)
            return {};
        std::vector<Token> tokens;
        std::string_view sv{line};
        const int len = static_cast<int>(sv.size());
        int i = 0;
        while(i < len)
        {
            char c = sv[i];

            if(text_utils::is_space(c))
            {
                ++i;
                continue;
            }

            if(c == '/' && i + 1 < len && sv[i + 1] == '/')
            {
                tokens.push_back({TOKEN_COMMENT, i, len - i});
                break;
            }

            if(c == '"')
            {
                int start = i++;
                while(i < len && sv[i] != '"')
                {
                    if(sv[i] == '\\' && i + 1 < len)
                        i += 2;
                    else
                        ++i;
                }
                if(i < len)
                    ++i;
                tokens.push_back({TOKEN_STRING, start, i - start});
                continue;
            }

            if((c >= '0' && c <= '9') || c == '-')
            {
                int start = i++;
                while(i < len && (text_utils::is_digit(sv[i]) || sv[i] == '.' ||
                                  sv[i] == 'e' || sv[i] == 'E' ||
                                  sv[i] == '+' || sv[i] == '-'))
                {
                    ++i;
                }
                tokens.push_back({TOKEN_NUMBER, start, i - start});
                continue;
            }

            if(std::isalpha(static_cast<unsigned char>(c)))
            {
                int start = i++;
                while(i < len &&
                      std::isalpha(static_cast<unsigned char>(sv[i])))
                {
                    ++i;
                }
                std::string_view word = sv.substr(start, i - start);
                if(word == "true" || word == "false" || word == "null")
                {
                    tokens.push_back({TOKEN_KEYWORD, start, i - start});
                }
                continue;
            }

            if(text_utils::contains(std::string_view("{}[]:,."), c))
            {
                tokens.push_back({TOKEN_OPERATOR, i, 1});
                ++i;
                continue;
            }

            ++i;
        }
        return tokens;
    }

    if(isFileType<FileType::Python>())
    {
        std::vector<Token> tokens;
        static constexpr std::string_view kPythonKeywords[] = {
            "and",    "as",       "assert",   "async", "await",  "break",
            "class",  "continue", "def",      "del",   "elif",   "else",
            "except", "False",    "finally",  "for",   "from",   "global",
            "if",     "import",   "in",       "is",    "lambda", "match",
            "case",   "None",     "nonlocal", "not",   "or",     "pass",
            "raise",  "return",   "True",     "try",   "while",  "with",
            "yield",
        };
        static constexpr std::string_view kPythonTypes[] = {
            "int",  "bool", "str", "float", "bytes", "complex",
            "list", "dict", "set", "tuple", "None",
        };
        auto is_keyword = [&](std::string_view word) -> bool
        {
            for(auto kw : kPythonKeywords)
            {
                if(kw == word)
                    return true;
            }
            return false;
        };
        auto is_type = [&](std::string_view word) -> bool
        {
            for(auto ty : kPythonTypes)
            {
                if(ty == word)
                    return true;
            }
            return false;
        };

        std::string_view sv{line};
        const int len = static_cast<int>(sv.size());
        int i = 0;
        while(i < len)
        {
            char c = sv[i];
            if(text_utils::is_space(c))
            {
                ++i;
                continue;
            }
            if(c == '#')
            {
                tokens.push_back({TOKEN_COMMENT, i, len - i});
                break;
            }
            if(c == '"' || c == '\'')
            {
                char quote = c;
                int start = i++;
                if(i + 1 < len && sv[i] == quote && sv[i + 1] == quote)
                {
                    i += 2;
                    size_t end = sv.find(std::string(3, quote), (size_t)i);
                    if(text_utils::is_not_found(end))
                    {
                        tokens.push_back({TOKEN_STRING, start, len - start});
                        break;
                    }
                    i = (int)end + 3;
                    tokens.push_back({TOKEN_STRING, start, i - start});
                    continue;
                }
                while(i < len)
                {
                    if(sv[i] == '\\' && i + 1 < len)
                    {
                        i += 2;
                        continue;
                    }
                    if(sv[i] == quote)
                    {
                        ++i;
                        break;
                    }
                    ++i;
                }
                tokens.push_back({TOKEN_STRING, start, i - start});
                continue;
            }
            if(text_utils::is_digit(c))
            {
                int start = i++;
                while(i < len && (text_utils::is_digit(sv[i]) || sv[i] == '.' ||
                                  sv[i] == '_'))
                    ++i;
                tokens.push_back({TOKEN_NUMBER, start, i - start});
                continue;
            }
            if(text_utils::is_alpha(c) || c == '_')
            {
                int start = i++;
                while(i < len && (text_utils::is_alpha(sv[i]) ||
                                  text_utils::is_digit(sv[i]) || sv[i] == '_'))
                {
                    ++i;
                }
                std::string_view word = sv.substr(start, i - start);
                if(is_type(word))
                {
                    tokens.push_back({TOKEN_TYPE, start, i - start});
                    continue;
                }
                if(is_keyword(word))
                {
                    tokens.push_back({TOKEN_KEYWORD, start, i - start});
                    if(word == "class")
                    {
                        int j = i;
                        while(j < len && text_utils::is_space(sv[j]))
                            ++j;
                        if(j < len &&
                           (text_utils::is_alpha(sv[j]) || sv[j] == '_'))
                        {
                            int typeStart = j++;
                            while(j < len &&
                                  (text_utils::is_alpha(sv[j]) ||
                                   text_utils::is_digit(sv[j]) || sv[j] == '_'))
                            {
                                ++j;
                            }
                            tokens.push_back(
                                {TOKEN_TYPE, typeStart, j - typeStart});
                        }
                    }
                    continue;
                }
                int prev = start - 1;
                while(prev >= 0 && text_utils::is_space(sv[prev]))
                    --prev;
                if(prev >= 0 && sv[prev] == '.')
                {
                    int objEnd = prev - 1;
                    while(objEnd >= 0 && text_utils::is_space(sv[objEnd]))
                        --objEnd;
                    int objStart = objEnd;
                    while(objStart >= 0 &&
                          (text_utils::is_alpha(sv[objStart]) ||
                           text_utils::is_digit(sv[objStart]) ||
                           sv[objStart] == '_'))
                    {
                        --objStart;
                    }
                    ++objStart;
                    bool isAllCaps = true;
                    for(char ch : word)
                    {
                        if(!(std::isupper((unsigned char)ch) ||
                             std::isdigit((unsigned char)ch) || ch == '_'))
                        {
                            isAllCaps = false;
                            break;
                        }
                    }

                    if(isAllCaps)
                    {
                        tokens.push_back({TOKEN_MEMBER, start, i - start});
                        continue;
                    }

                    if(objStart <= objEnd)
                    {
                        std::string_view obj =
                            sv.substr(objStart, objEnd - objStart + 1);
                        if(obj == "self" || obj == "cls")
                        {
                            tokens.push_back({TOKEN_MEMBER, start, i - start});
                            continue;
                        }
                        if(!obj.empty() && std::isupper((unsigned char)obj[0]))
                        {
                            tokens.push_back({TOKEN_MEMBER, start, i - start});
                            continue;
                        }
                    }
                }
                int lookahead = i;
                while(lookahead < len && text_utils::is_space(sv[lookahead]))
                    ++lookahead;
                if(lookahead < len && sv[lookahead] == '(')
                {
                    tokens.push_back({TOKEN_FUNCTION, start, i - start});
                    continue;
                }
            }
            ++i;
        }
        return tokens;
    }

    if(isFileType<FileType::Yaml>())
    {
        if(!syntaxYaml)
            return {};
        std::vector<Token> tokens;
        std::string_view sv{line};
        const int len = static_cast<int>(sv.size());
        int i = 0;
        while(i < len)
        {
            if(text_utils::is_space(sv[i]))
            {
                ++i;
                continue;
            }

            if(sv[i] == '#')
            {
                tokens.push_back({TOKEN_COMMENT, i, len - i});
                break;
            }

            if(sv[i] == '\'' || sv[i] == '"')
            {
                char quote = sv[i];
                int start = i++;
                while(i < len && sv[i] != quote)
                {
                    if(sv[i] == '\\' && i + 1 < len)
                        i += 2;
                    else
                        ++i;
                }
                if(i < len)
                    ++i;
                tokens.push_back({TOKEN_STRING, start, i - start});
                continue;
            }

            int start = i;
            while(i < len && !text_utils::is_space(sv[i]) && sv[i] != ':')
                ++i;
            if(i < len && sv[i] == ':')
            {
                tokens.push_back({TOKEN_KEYWORD, start, i - start});
                tokens.push_back({TOKEN_OPERATOR, i, 1});
                ++i;
                continue;
            }

            if(i > start)
            {
                std::string_view word = sv.substr(start, i - start);
                if(word == "true" || word == "false" || word == "null")
                {
                    tokens.push_back({TOKEN_KEYWORD, start, i - start});
                }
            }
        }
        return tokens;
    }

    if(isFileType<FileType::Toml>())
    {
        std::vector<Token> tokens;
        std::string_view sv{line};
        const int len = static_cast<int>(sv.size());
        int i = 0;

        while(i < len)
        {
            if(text_utils::is_space(sv[i]))
            {
                ++i;
                continue;
            }

            if(sv[i] == '#')
            {
                tokens.push_back({TOKEN_COMMENT, i, len - i});
                break;
            }

            if(sv[i] == '[')
            {
                int start = i++;
                if(i < len && sv[i] == '[')
                    ++i;
                while(i < len && sv[i] != ']')
                    ++i;
                if(i < len)
                {
                    ++i;
                    if(i < len && sv[i] == ']')
                        ++i;
                }
                tokens.push_back({TOKEN_KEYWORD, start, i - start});
                continue;
            }

            if(sv[i] == '\'' || sv[i] == '"')
            {
                char quote = sv[i];
                int start = i++;
                if(i + 1 < len && sv[i] == quote && sv[i + 1] == quote)
                {
                    i += 2;
                    inTomlMultiline = true;
                    tomlQuote = quote;
                }
                while(i < len)
                {
                    if(sv[i] == '\\' && i + 1 < len)
                    {
                        i += 2;
                        continue;
                    }
                    if(sv[i] == quote)
                    {
                        if(inTomlMultiline)
                        {
                            if(i + 2 < len && sv[i + 1] == quote &&
                               sv[i + 2] == quote)
                            {
                                i += 3;
                                inTomlMultiline = false;
                                break;
                            }
                            ++i;
                            continue;
                        }
                        ++i;
                        break;
                    }
                    ++i;
                }
                tokens.push_back({TOKEN_STRING, start, i - start});
                continue;
            }

            if(text_utils::is_digit(sv[i]) || sv[i] == '-' || sv[i] == '+')
            {
                int start = i++;
                while(i < len && (text_utils::is_digit(sv[i]) || sv[i] == '.' ||
                                  sv[i] == 'e' || sv[i] == 'E' ||
                                  sv[i] == '+' || sv[i] == '-'))
                {
                    ++i;
                }
                tokens.push_back({TOKEN_NUMBER, start, i - start});
                continue;
            }

            if(text_utils::contains(std::string_view("=.,{}[]"), sv[i]))
            {
                tokens.push_back({TOKEN_OPERATOR, i, 1});
                ++i;
                continue;
            }

            int start = i;
            while(i < len && !text_utils::is_space(sv[i]) && sv[i] != '=')
                ++i;
            if(i < len && sv[i] == '=')
            {
                tokens.push_back({TOKEN_KEYWORD, start, i - start});
                continue;
            }
        }

        return tokens;
    }

    std::vector<Token> tokens;
    std::string_view sv{line};
    const int len = static_cast<int>(sv.size());
    int i = 0;

    auto push_token = [&](TokenType type, int start, int length)
    { tokens.push_back({type, start, length}); };

    if(inBlockComment)
    {
        size_t end = sv.find("*/");
        if(text_utils::is_found(end))
        {
            int len = (int)end + 2;
            push_token(TOKEN_COMMENT, 0, len);
            inBlockComment = false;
            i = len;
        }
        else
        {
            push_token(TOKEN_COMMENT, 0, len);
            return tokens;
        }
    }

    if(inMarkupFence)
    {
        size_t fenceStart = line.find(markupFenceChar);
        if(text_utils::is_found(fenceStart))
        {
            bool isFence = fenceStart + 2 < line.size() &&
                           line[fenceStart + 1] == markupFenceChar &&
                           line[fenceStart + 2] == markupFenceChar;
            if(isFence)
            {
                push_token(TOKEN_KEYWORD, (int)fenceStart,
                           (int)(line.size() - fenceStart));
                inMarkupFence = false;
                return tokens;
            }
        }
        push_token(TOKEN_STRING, 0, len);
        return tokens;
    }

    if(isFileType<FileType::Mla>() && syntaxMlangHighlightBuiltinDocs)
    {
        auto match_builtin = [&](std::string_view marker, TokenType nameType)
        {
            if(!sv.starts_with(marker))
                return false;
            push_token(TOKEN_COMMENT, 0, len);
            size_t atPos = sv.find('@');
            if(text_utils::is_found(atPos))
            {
                size_t spacePos = sv.find(' ', atPos);
                size_t keywordLen = text_utils::is_not_found(spacePos)
                                        ? marker.size() - atPos
                                        : spacePos - atPos;
                if(keywordLen > 0)
                    push_token(TOKEN_KEYWORD, (int)atPos, (int)keywordLen);
            }

            size_t nameStart = marker.size();
            while(nameStart < sv.size() && text_utils::is_space(sv[nameStart]))
                ++nameStart;
            size_t nameEnd = nameStart;
            while(nameEnd < sv.size() &&
                  (text_utils::is_alpha(sv[nameEnd]) ||
                   text_utils::is_digit(sv[nameEnd]) || sv[nameEnd] == '_' ||
                   sv[nameEnd] == ':' || sv[nameEnd] == '<' ||
                   sv[nameEnd] == '>'))
            {
                ++nameEnd;
            }
            if(nameEnd > nameStart)
                push_token(nameType, (int)nameStart,
                           (int)(nameEnd - nameStart));
            return true;
        };

        if(match_builtin("// @builtin ", TOKEN_TYPE) ||
           match_builtin("// @builtin_macro ", TOKEN_FUNCTION) ||
           match_builtin("// @builtin_attribute ", TOKEN_KEYWORD) ||
           match_builtin("// @builtin_fn ", TOKEN_FUNCTION))
        {
            return tokens;
        }
    }

    while(i < len)
    {
        char c = sv[i];

        if(c == '/' && i + 1 < len)
        {
            if(sv[i + 1] == '/')
            {
                push_token(TOKEN_COMMENT, i, len - i);
                break;
            }
            if(sv[i + 1] == '*')
            {
                int start = i;
                i += 2;
                size_t end = sv.find("*/", (size_t)i);
                if(text_utils::is_found(end))
                {
                    int tokenLen = (int)end + 2 - start;
                    push_token(TOKEN_COMMENT, start, tokenLen);
                    i = start + tokenLen;
                }
                else
                {
                    push_token(TOKEN_COMMENT, start, len - start);
                    inBlockComment = true;
                    break;
                }
                continue;
            }
        }

        if(c == '"' || c == '\'')
        {
            char quote = c;
            int start = i++;
            bool isChar = (quote == '\'');
            while(i < len)
            {
                if(sv[i] == '\\' && i + 1 < len)
                {
                    i += 2;
                    continue;
                }
                if(sv[i] == quote)
                {
                    ++i;
                    break;
                }
                ++i;
            }
            push_token(isChar ? TOKEN_CHAR : TOKEN_STRING, start, i - start);
            continue;
        }

        if(c == '#' && isFileType<FileType::MarkupText>())
        {
            if(i + 1 < len && sv[i + 1] == '#')
            {
                push_token(TOKEN_KEYWORD, i, len - i);
                break;
            }
            push_token(TOKEN_KEYWORD, i, len - i);
            break;
        }

        if(text_utils::is_space(c))
        {
            ++i;
            continue;
        }

        if(text_utils::is_digit(c))
        {
            int start = i++;
            while(i < len && (text_utils::is_digit(sv[i]) || sv[i] == '.' ||
                              sv[i] == 'x' || sv[i] == 'X' || sv[i] == 'b' ||
                              sv[i] == 'B' || sv[i] == '_'))
                ++i;
            push_token(TOKEN_NUMBER, start, i - start);
            continue;
        }

        if(text_utils::is_alpha(c) || c == '_' ||
           (c == '#' && i + 1 < len && text_utils::is_alpha(sv[i + 1])))
        {
            int start = i++;
            while(i < len &&
                  (text_utils::is_alpha(sv[i]) || text_utils::is_digit(sv[i]) ||
                   sv[i] == '_' || sv[i] == '#' || sv[i] == '@'))
            {
                ++i;
            }

            std::string_view word = sv.substr(start, i - start);
            bool inParamList = inCppParamListContext;
            if(!inParamList)
            {
                if(paramListStart >= 0 &&
                   (paramListEnd > paramListStart || paramListOpen) &&
                   start > paramListStart &&
                   (paramListEnd > paramListStart ? start < paramListEnd
                                                  : true))
                {
                    inParamList = true;
                }
            }
            if(isJs)
            {
                static constexpr std::string_view kJsKeywords[] = {
                    "break",    "case",     "catch",   "class",   "const",
                    "continue", "debugger", "default", "delete",  "do",
                    "else",     "export",   "extends", "finally", "for",
                    "function", "if",       "import",  "in",      "instanceof",
                    "let",      "new",      "return",  "super",   "switch",
                    "this",     "throw",    "try",     "typeof",  "var",
                    "void",     "while",    "with",    "yield",   "await",
                    "async",    "static",   "get",     "set",     "of",
                };
                static constexpr std::string_view kJsTypes[] = {
                    "string",    "number", "boolean", "any",
                    "unknown",   "never",  "void",    "null",
                    "undefined", "bigint", "symbol",  "object",
                };
                auto is_js_keyword = [&](std::string_view w) -> bool
                {
                    for(auto kw : kJsKeywords)
                    {
                        if(kw == w)
                            return true;
                    }
                    return false;
                };
                auto is_js_type = [&](std::string_view w) -> bool
                {
                    for(auto ty : kJsTypes)
                    {
                        if(ty == w)
                            return true;
                    }
                    return false;
                };

                if(is_js_keyword(word))
                {
                    push_token(TOKEN_KEYWORD, start, i - start);
                    continue;
                }
                if(is_js_type(word))
                {
                    push_token(TOKEN_TYPE, start, i - start);
                    continue;
                }
            }
            if(isFileType<FileType::Asm>())
            {
                if(is_asm_keyword(word))
                {
                    push_token(TOKEN_KEYWORD, start, i - start);
                    continue;
                }
                if(is_asm_register(word))
                {
                    push_token(TOKEN_TYPE, start, i - start);
                    continue;
                }
            }
            if(isFileType<FileType::Cpp>())
            {
                if(cpp_constants::is_keyword(word))
                {
                    push_token(TOKEN_KEYWORD, start, i - start);
                    if(word == "template" && syntaxCppHighlightTypeNames)
                    {
                        size_t lt = sv.find('<', i);
                        if(text_utils::is_found(lt))
                        {
                            int depth = 0;
                            for(size_t j = lt; j < sv.size(); ++j)
                            {
                                char ch = sv[j];
                                if(ch == '<')
                                {
                                    ++depth;
                                    continue;
                                }
                                if(ch == '>')
                                {
                                    --depth;
                                    if(depth <= 0)
                                        break;
                                    continue;
                                }
                                if(ch == '"' || ch == '\'')
                                {
                                    char quote = ch;
                                    ++j;
                                    while(j < sv.size())
                                    {
                                        if(sv[j] == '\\' && j + 1 < sv.size())
                                        {
                                            j += 2;
                                            continue;
                                        }
                                        if(sv[j] == quote)
                                            break;
                                        ++j;
                                    }
                                    continue;
                                }
                                if(text_utils::is_alpha(ch) || ch == '_')
                                {
                                    size_t nameStart = j;
                                    ++j;
                                    while(j < sv.size() &&
                                          (text_utils::is_alpha(sv[j]) ||
                                           text_utils::is_digit(sv[j]) ||
                                           sv[j] == '_'))
                                    {
                                        ++j;
                                    }
                                    std::string_view ident =
                                        sv.substr(nameStart, j - nameStart);
                                    if(ident != "typename" &&
                                       ident != "class" && ident != "struct" &&
                                       ident != "template")
                                    {
                                        extraTypeTokens.push_back(
                                            {TOKEN_TYPE, (int)nameStart,
                                             (int)(j - nameStart)});
                                    }
                                    --j;
                                }
                            }
                        }
                    }
                    if(syntaxCppHighlightTypeNames &&
                       (word == "class" || word == "struct" || word == "enum" ||
                        word == "union" || word == "typedef" ||
                        word == "using"))
                    {
                        int j = i;
                        while(j < len && text_utils::is_space(sv[j]))
                            ++j;
                        if(j < len &&
                           (text_utils::is_alpha(sv[j]) || sv[j] == '_'))
                        {
                            int typeStart = j++;
                            while(j < len &&
                                  (text_utils::is_alpha(sv[j]) ||
                                   text_utils::is_digit(sv[j]) || sv[j] == '_'))
                            {
                                ++j;
                            }
                            push_token(TOKEN_TYPE, typeStart, j - typeStart);
                            i = j;
                        }
                    }
                    continue;
                }
                if(cpp_constants::is_type(word))
                {
                    push_token(TOKEN_TYPE, start, i - start);
                    if(syntaxCppHighlightTypeNames)
                    {
                        int j = i;
                        while(j < len && text_utils::is_space(sv[j]))
                            ++j;
                        if(j < len && sv[j] == '<')
                        {
                            int depth = 0;
                            for(int k = j; k < len; ++k)
                            {
                                char ch = sv[k];
                                if(ch == '<')
                                {
                                    ++depth;
                                    continue;
                                }
                                if(ch == '>')
                                {
                                    --depth;
                                    if(depth <= 0)
                                        break;
                                    continue;
                                }
                                if(ch == '"' || ch == '\'')
                                {
                                    char quote = ch;
                                    ++k;
                                    while(k < len)
                                    {
                                        if(sv[k] == '\\' && k + 1 < len)
                                        {
                                            k += 2;
                                            continue;
                                        }
                                        if(sv[k] == quote)
                                            break;
                                        ++k;
                                    }
                                    continue;
                                }
                                if(text_utils::is_alpha(ch) || ch == '_')
                                {
                                    int nameStart = k++;
                                    while(k < len &&
                                          (text_utils::is_alpha(sv[k]) ||
                                           text_utils::is_digit(sv[k]) ||
                                           sv[k] == '_'))
                                    {
                                        ++k;
                                    }
                                    extraTypeTokens.push_back(
                                        {TOKEN_TYPE, nameStart, k - nameStart});
                                    --k;
                                }
                            }
                        }
                    }
                    if(syntaxCppHighlightMembers)
                    {
                        int j = i;
                        while(j < len && text_utils::is_space(sv[j]))
                            ++j;
                        if(j < len &&
                           (text_utils::is_alpha(sv[j]) || sv[j] == '_'))
                        {
                            int nameStart = j++;
                            while(j < len &&
                                  (text_utils::is_alpha(sv[j]) ||
                                   text_utils::is_digit(sv[j]) || sv[j] == '_'))
                            {
                                ++j;
                            }
                            if(inParamList || inCppFunctionContext ||
                               inCppMethodContext)
                            {
                                localDeclNames.insert(std::string(
                                    sv.substr(nameStart, j - nameStart)));
                                continue;
                            }
                            int k = j;
                            while(k < len && text_utils::is_space(sv[k]))
                                ++k;
                            bool hasParen = false;
                            for(int t = k; t < len; ++t)
                            {
                                if(sv[t] == '(')
                                {
                                    hasParen = true;
                                    break;
                                }
                                if(sv[t] == ';' || sv[t] == '=' || sv[t] == ',')
                                    break;
                            }
                            if(!hasParen)
                            {
                                push_token(TOKEN_MEMBER, nameStart,
                                           j - nameStart);
                                i = j;
                            }
                        }
                    }
                    continue;
                }
                auto is_user_type = [&](std::string_view name) -> bool
                {
                    if(cppClassNames.empty())
                        return false;
                    return cppClassNames.find(std::string(name)) !=
                           cppClassNames.end();
                };
                if(syntaxCppHighlightParamTypes && inParamList &&
                   is_user_type(word))
                {
                    push_token(TOKEN_TYPE, start, i - start);
                    continue;
                }
                if(inParamList && !syntaxCppHighlightParamTypes &&
                   is_user_type(word))
                {
                    int j = i;
                    while(j < len && text_utils::is_space(sv[j]))
                        ++j;
                    while(j < len && (sv[j] == '*' || sv[j] == '&'))
                        ++j;
                    while(j < len && text_utils::is_space(sv[j]))
                        ++j;
                    if(j < len && (text_utils::is_alpha(sv[j]) || sv[j] == '_'))
                    {
                        int nameStart = j++;
                        while(j < len &&
                              (text_utils::is_alpha(sv[j]) ||
                               text_utils::is_digit(sv[j]) || sv[j] == '_'))
                        {
                            ++j;
                        }
                        localDeclNames.insert(
                            std::string(sv.substr(nameStart, j - nameStart)));
                    }
                    continue;
                }
                if(syntaxCppHighlightTypeNames && !inParamList &&
                   is_user_type(word))
                {
                    int j = i;
                    while(j < len && text_utils::is_space(sv[j]))
                        ++j;
                    if(j < len && sv[j] == '<')
                    {
                        int depth = 0;
                        for(int k = j; k < len; ++k)
                        {
                            char ch = sv[k];
                            if(ch == '<')
                            {
                                ++depth;
                                continue;
                            }
                            if(ch == '>')
                            {
                                --depth;
                                if(depth <= 0)
                                    break;
                                continue;
                            }
                            if(ch == '"' || ch == '\'')
                            {
                                char quote = ch;
                                ++k;
                                while(k < len)
                                {
                                    if(sv[k] == '\\' && k + 1 < len)
                                    {
                                        k += 2;
                                        continue;
                                    }
                                    if(sv[k] == quote)
                                        break;
                                    ++k;
                                }
                                continue;
                            }
                            if(text_utils::is_alpha(ch) || ch == '_')
                            {
                                int nameStart = k++;
                                while(k < len && (text_utils::is_alpha(sv[k]) ||
                                                  text_utils::is_digit(sv[k]) ||
                                                  sv[k] == '_'))
                                {
                                    ++k;
                                }
                                extraTypeTokens.push_back(
                                    {TOKEN_TYPE, nameStart, k - nameStart});
                                --k;
                            }
                        }
                    }
                    while(j < len && (sv[j] == '*' || sv[j] == '&'))
                        ++j;
                    while(j < len && text_utils::is_space(sv[j]))
                        ++j;
                    if(j < len && (text_utils::is_alpha(sv[j]) || sv[j] == '_'))
                    {
                        int nameStart = j++;
                        while(j < len &&
                              (text_utils::is_alpha(sv[j]) ||
                               text_utils::is_digit(sv[j]) || sv[j] == '_'))
                        {
                            ++j;
                        }
                        if(inCppFunctionContext || inCppMethodContext)
                        {
                            localDeclNames.insert(std::string(
                                sv.substr(nameStart, j - nameStart)));
                        }
                        push_token(TOKEN_TYPE, start, i - start);
                        continue;
                    }
                }
                if(syntaxCppHighlightMembers)
                {
                    int p = start - 1;
                    while(p >= 0 && text_utils::is_space(sv[p]))
                        --p;
                    if(syntaxCppHighlightTypeNames && p >= 1 && sv[p] == ':' &&
                       sv[p - 1] == ':')
                    {
                        push_token(TOKEN_TYPE, start, i - start);
                        continue;
                    }
                    if(p >= 0 && sv[p] == '.')
                    {
                        push_token(TOKEN_MEMBER, start, i - start);
                        continue;
                    }
                    if(p >= 0 && sv[p] == '>' && p - 1 >= 0 && sv[p - 1] == '-')
                    {
                        push_token(TOKEN_MEMBER, start, i - start);
                        continue;
                    }
                }
                if(syntaxCppHighlightImplicitMembers && inCppMethodContext &&
                   !inParamList &&
                   localDeclNames.find(std::string(word)) ==
                       localDeclNames.end() &&
                   cppMemberNames.find(std::string(word)) !=
                       cppMemberNames.end())
                {
                    push_token(TOKEN_MEMBER, start, i - start);
                    continue;
                }
            }
            else if(isFileType<FileType::Mla>())
            {
                if(is_mlang_platform_keyword(word))
                {
                    push_token(TOKEN_KEYWORD, start, i - start);
                    continue;
                }
                if(syntaxMlangHighlightTypes)
                {
                    if(auto mapped = lookupMlangTokenType(word))
                    {
                        push_token(*mapped, start, i - start);
                        if(*mapped == TOKEN_TYPE)
                        {
                            int j = i;
                            while(j < len && text_utils::is_space(sv[j]))
                                ++j;
                            if(j < len && sv[j] == '<')
                            {
                                int depth = 0;
                                for(int k = j; k < len; ++k)
                                {
                                    char ch = sv[k];
                                    if(ch == '<')
                                    {
                                        ++depth;
                                        continue;
                                    }
                                    if(ch == '>')
                                    {
                                        --depth;
                                        if(depth <= 0)
                                            break;
                                        continue;
                                    }
                                    if(ch == '"' || ch == '\'')
                                    {
                                        char quote = ch;
                                        ++k;
                                        while(k < len)
                                        {
                                            if(sv[k] == '\\' && k + 1 < len)
                                            {
                                                k += 2;
                                                continue;
                                            }
                                            if(sv[k] == quote)
                                                break;
                                            ++k;
                                        }
                                        continue;
                                    }
                                    if(text_utils::is_alpha(ch) || ch == '_')
                                    {
                                        int nameStart = k++;
                                        while(k < len &&
                                              (text_utils::is_alpha(sv[k]) ||
                                               text_utils::is_digit(sv[k]) ||
                                               sv[k] == '_'))
                                        {
                                            ++k;
                                        }
                                        extraTypeTokens.push_back(
                                            {TOKEN_TYPE, nameStart,
                                             k - nameStart});
                                        --k;
                                    }
                                }
                            }
                        }
                        continue;
                    }
                }
                if(syntaxMlangHighlightTypes && !word.empty() &&
                   std::isupper(static_cast<unsigned char>(word[0])) != 0)
                {
                    bool looksLikeType = false;

                    int p = start - 1;
                    while(p >= 0 && text_utils::is_space(sv[p]))
                        --p;
                    if(p >= 0 && sv[p] == ':')
                        looksLikeType = true;

                    if(!looksLikeType)
                    {
                        int q = i;
                        while(q < len && text_utils::is_space(sv[q]))
                            ++q;
                        if(q < len && sv[q] == '{')
                            looksLikeType = true;
                    }

                    if(looksLikeType)
                    {
                        push_token(TOKEN_TYPE, start, i - start);
                        continue;
                    }
                }
                if(is_mlang_keyword(word))
                {
                    push_token(TOKEN_KEYWORD, start, i - start);
                    bool colorsNextTypeName = word == "struct" ||
                                              word == "trait" ||
                                              word == "impl" || word == "alias";
                    if(!colorsNextTypeName && word == "type")
                    {
                        int p = start - 1;
                        while(p >= 0 && text_utils::is_space(sv[p]))
                            --p;
                        int prevEnd = p + 1;
                        while(p >= 0 &&
                              (text_utils::is_alpha(sv[p]) ||
                               text_utils::is_digit(sv[p]) || sv[p] == '_'))
                        {
                            --p;
                        }
                        std::string_view prev =
                            sv.substr(p + 1, prevEnd - (p + 1));
                        colorsNextTypeName = prev == "use";
                    }
                    if(syntaxMlangHighlightTypes && colorsNextTypeName)
                    {
                        int j = i;
                        while(j < len && text_utils::is_space(sv[j]))
                            ++j;
                        if(j < len &&
                           (text_utils::is_alpha(sv[j]) || sv[j] == '_'))
                        {
                            int typeStart = j++;
                            while(j < len &&
                                  (text_utils::is_alpha(sv[j]) ||
                                   text_utils::is_digit(sv[j]) || sv[j] == '_'))
                            {
                                ++j;
                            }
                            push_token(TOKEN_TYPE, typeStart, j - typeStart);
                            i = j;
                        }
                    }
                    continue;
                }
                if(syntaxMlangHighlightTypes && is_mlang_type(word))
                {
                    push_token(TOKEN_TYPE, start, i - start);
                    int j = i;
                    while(j < len && text_utils::is_space(sv[j]))
                        ++j;
                    if(j < len && sv[j] == '<')
                    {
                        int depth = 0;
                        for(int k = j; k < len; ++k)
                        {
                            char ch = sv[k];
                            if(ch == '<')
                            {
                                ++depth;
                                continue;
                            }
                            if(ch == '>')
                            {
                                --depth;
                                if(depth <= 0)
                                    break;
                                continue;
                            }
                            if(ch == '"' || ch == '\'')
                            {
                                char quote = ch;
                                ++k;
                                while(k < len)
                                {
                                    if(sv[k] == '\\' && k + 1 < len)
                                    {
                                        k += 2;
                                        continue;
                                    }
                                    if(sv[k] == quote)
                                        break;
                                    ++k;
                                }
                                continue;
                            }
                            if(text_utils::is_alpha(ch) || ch == '_')
                            {
                                int nameStart = k++;
                                while(k < len && (text_utils::is_alpha(sv[k]) ||
                                                  text_utils::is_digit(sv[k]) ||
                                                  sv[k] == '_'))
                                {
                                    ++k;
                                }
                                extraTypeTokens.push_back(
                                    {TOKEN_TYPE, nameStart, k - nameStart});
                                --k;
                            }
                        }
                    }
                    continue;
                }
                if(is_mlang_builtin(word))
                {
                    int j = i;
                    while(j < len && text_utils::is_space(sv[j]))
                        ++j;
                    if(j < len && sv[j] == '!')
                    {
                        push_token(TOKEN_FUNCTION, start, i - start);
                        continue;
                    }
                }
                {
                    int p = start - 1;
                    while(p >= 0 && text_utils::is_space(sv[p]))
                        --p;
                    if(p >= 0 && sv[p] == '.')
                    {
                        push_token(TOKEN_MEMBER, start, i - start);
                        continue;
                    }
                }
            }
            else if(isFileType<FileType::MarkupText>())
            {
                if(word == "---" || word == "===")
                {
                    push_token(TOKEN_KEYWORD, start, i - start);
                    continue;
                }
            }

            if(word == "true" || word == "false" || word == "null")
            {
                push_token(TOKEN_KEYWORD, start, i - start);
                continue;
            }

            if(word == "TODO" || word == "FIXME" || word == "NOTE")
            {
                push_token(TOKEN_KEYWORD, start, i - start);
                continue;
            }

            if(word == "BEGIN" || word == "END")
            {
                push_token(TOKEN_KEYWORD, start, i - start);
                continue;
            }

            if(word == "WARNING" || word == "ERROR")
            {
                push_token(TOKEN_KEYWORD, start, i - start);
                continue;
            }

            int lookahead = i;
            while(lookahead < len && text_utils::is_space(sv[lookahead]))
                ++lookahead;
            if(isFileType<FileType::Cpp>() && syntaxCppHighlightTypeNames &&
               lookahead + 1 < len && sv[lookahead] == ':' &&
               sv[lookahead + 1] == ':')
            {
                push_token(TOKEN_TYPE, start, i - start);
                continue;
            }
            if(lookahead < len && sv[lookahead] == '(')
            {
                push_token(TOKEN_FUNCTION, start, i - start);
                continue;
            }
        }

        if(std::ispunct(static_cast<unsigned char>(c)))
        {
            if(isFileType<FileType::Cpp>() && syntaxCppHighlightMembers)
            {
                if(c == '.')
                {
                    push_token(TOKEN_OPERATOR, i, 1);
                    int j = i + 1;
                    while(j < len && text_utils::is_space(sv[j]))
                        ++j;
                    if(j < len && (text_utils::is_alpha(sv[j]) || sv[j] == '_'))
                    {
                        int memberStart = j++;
                        while(j < len &&
                              (text_utils::is_alpha(sv[j]) ||
                               text_utils::is_digit(sv[j]) || sv[j] == '_'))
                        {
                            ++j;
                        }
                        push_token(TOKEN_MEMBER, memberStart, j - memberStart);
                        i = j;
                        continue;
                    }
                }
                if(c == '-' && i + 1 < len && sv[i + 1] == '>')
                {
                    push_token(TOKEN_OPERATOR, i, 2);
                    int j = i + 2;
                    while(j < len && text_utils::is_space(sv[j]))
                        ++j;
                    if(j < len && (text_utils::is_alpha(sv[j]) || sv[j] == '_'))
                    {
                        int memberStart = j++;
                        while(j < len &&
                              (text_utils::is_alpha(sv[j]) ||
                               text_utils::is_digit(sv[j]) || sv[j] == '_'))
                        {
                            ++j;
                        }
                        push_token(TOKEN_MEMBER, memberStart, j - memberStart);
                        i = j;
                        continue;
                    }
                }
            }
            int start = i++;
            if(i < len)
            {
                char a = sv[start];
                char b = sv[i];
                if(is_two_char_op(a, b))
                {
                    ++i;
                    push_token(TOKEN_OPERATOR, start, 2);
                    continue;
                }
            }
            push_token(TOKEN_OPERATOR, start, 1);
            continue;
        }

        ++i;
    }

    if(!extraTypeTokens.empty())
    {
        tokens.insert(tokens.end(), extraTypeTokens.begin(),
                      extraTypeTokens.end());
    }

    return tokens;
}

void SyntaxHighlighter::renderLineWithSyntax(std::string& output,
                                             const std::string& line, int start,
                                             int len, int fileRow) const
{
    if(!editor || !editor->lines || !editor->cursorX || !editor->cursorY)
        return;

    auto* lines = editor->lines;
    auto* buffer = editor->currentBuffer;
    auto* cursorX = editor->cursorX;
    auto* cursorY = editor->cursorY;
    const auto& theme = editor->theme;
    const auto currentMode = editor->currentMode;
    auto isInSelection = [&](int row, int col)
    { return editor->isInSelection(row, col); };
    auto isInVisualBlock = [&](int row, int col)
    { return editor->isInVisualBlock(row, col); };
    auto isInSearchMatch = [&](int row, int col)
    { return editor->isInSearchMatch(row, col); };

    // fileRow is already the absolute line index in the buffer
    int absoluteLineNum = fileRow;

    // Helper function to scan a line and update block comment state
    // This needs to be careful about string literals and character literals
    auto scanLineForBlockComments =
        [](const std::string& scanLine, bool& inComment)
    {
        size_t pos = 0;
        size_t len = scanLine.length();

        while(pos < len)
        {
            if(inComment)
            {
                // Looking for closing */
                size_t closePos = scanLine.find("*/", pos);
                if(text_utils::is_found(closePos))
                {
                    inComment = false;
                    pos = closePos + 2;
                }
                else
                {
                    break; // Still in block comment at end of line
                }
            }
            else
            {
                char c = scanLine[pos];

                // Skip string literals
                if(c == '"')
                {
                    pos++;
                    while(pos < len)
                    {
                        if(scanLine[pos] == '\\' && pos + 1 < len)
                        {
                            pos += 2; // Skip escaped character
                        }
                        else if(scanLine[pos] == '"')
                        {
                            pos++;
                            break;
                        }
                        else
                        {
                            pos++;
                        }
                    }
                    continue;
                }

                // Skip character literals
                if(c == '\'')
                {
                    pos++;
                    while(pos < len)
                    {
                        if(scanLine[pos] == '\\' && pos + 1 < len)
                        {
                            pos += 2; // Skip escaped character
                        }
                        else if(scanLine[pos] == '\'')
                        {
                            pos++;
                            break;
                        }
                        else
                        {
                            pos++;
                        }
                    }
                    continue;
                }

                // Check for line comment
                if(pos + 1 < len && scanLine[pos] == '/' &&
                   scanLine[pos + 1] == '/')
                {
                    // Rest of line is a line comment
                    break;
                }

                // Check for block comment start
                if(pos + 1 < len && scanLine[pos] == '/' &&
                   scanLine[pos + 1] == '*')
                {
                    inComment = true;
                    pos += 2;
                    continue;
                }

                pos++;
            }
        }
    };

    // Determine block comment / fence / context state for this line
    bool blockCommentState = false;
    bool tomlMultilineState = false;
    char tomlQuote = 0;
    bool markupFenceState = false;
    char markupFenceChar = 0;
    bool inCppMethodContext = false;
    bool inCppFunctionContext = false;
    bool inCppParamListContext = false;

    auto scanLineForTomlMultiline =
        [](const std::string& scanLine, bool& inMultiline, char& quoteChar)
    {
        for(size_t i = 0; i + 2 < scanLine.size(); ++i)
        {
            char c = scanLine[i];
            if(c != '"' && c != '\'')
                continue;
            if(scanLine[i + 1] != c || scanLine[i + 2] != c)
                continue;

            if(c == '"')
            {
                size_t j = i;
                int backslashes = 0;
                while(j > 0 && scanLine[j - 1] == '\\')
                {
                    ++backslashes;
                    --j;
                }
                if((backslashes % 2) == 1)
                {
                    i += 2;
                    continue;
                }
            }

            if(!inMultiline)
            {
                inMultiline = true;
                quoteChar = c;
            }
            else if(quoteChar == c)
            {
                inMultiline = false;
            }
            if(c == '"' && i + 3 < scanLine.size() && scanLine[i + 3] == '"')
                i += 3;
            else
                i += 2;
        }
    };

    auto scanLineForMarkupFence =
        [](const std::string& scanLine, bool& inFence, char& fenceChar)
    {
        size_t i = 0;
        while(i < scanLine.size() && text_utils::is_space(scanLine[i]))
            ++i;
        if(i + 2 >= scanLine.size())
            return;
        char c = scanLine[i];
        if((c == '`' || c == '~') && scanLine[i + 1] == c &&
           scanLine[i + 2] == c)
        {
            if(!inFence)
            {
                inFence = true;
                fenceChar = c;
            }
            else if(fenceChar == c)
            {
                inFence = false;
            }
        }
    };

    if(buffer && !buffer->dirty)
    {
        if((int)buffer->syntaxCache.size() != (int)lines->size())
        {
            buffer->syntaxCache.assign(lines->size(), {});
            buffer->syntaxCacheComputedUpTo = -1;
        }

        if(isFileType<FileType::Cpp>())
            ensureCppMemberIndex();

        auto compute_to = [&](int target)
        {
            int startLine = buffer->syntaxCacheComputedUpTo + 1;
            if(startLine < 0)
                startLine = 0;

            for(int i = startLine; i <= target && i < (int)lines->size(); ++i)
            {
                Buffer::SyntaxCacheLine lineState;
                if(i == 0)
                {
                    lineState.methodState = CppMethodScanState{};
                    lineState.functionState = CppFunctionScanState{};
                    lineState.paramState = CppParamListScanState{};
                }
                else
                {
                    const auto& prev = buffer->syntaxCache[i - 1];
                    lineState.inBlockComment = prev.inBlockComment;
                    lineState.inTomlMultiline = prev.inTomlMultiline;
                    lineState.tomlQuote = prev.tomlQuote;
                    lineState.inMarkupFence = prev.inMarkupFence;
                    lineState.markupFenceChar = prev.markupFenceChar;
                    lineState.methodState = prev.methodState;
                    lineState.functionState = prev.functionState;
                    lineState.paramState = prev.paramState;
                }

                const std::string& curLine = (*lines)[i];
                bool methodStart = false;
                bool functionStart = false;
                bool paramStart = false;
                bool methodBefore = lineState.methodState.inMethod;
                bool functionBefore = lineState.functionState.inFunction;
                bool paramBefore = lineState.paramState.inParamList;

                if(isFileType<FileType::Cpp>())
                {
                    scan_line_for_cpp_method_context(curLine, cppClassNames,
                                                     lineState.methodState,
                                                     &methodStart);
                    scan_line_for_cpp_function_context(
                        curLine, lineState.functionState, &functionStart);
                    scan_line_for_cpp_param_list_context(
                        curLine, lineState.paramState, &paramStart);
                }

                lineState.inCppMethodContext = methodBefore || methodStart;
                lineState.inCppFunctionContext =
                    functionBefore || functionStart;
                lineState.inCppParamListContext = paramBefore || paramStart;

                if(isFileType<FileType::Cpp>() || isFileType<FileType::Mla>())
                    scanLineForBlockComments(curLine, lineState.inBlockComment);

                if(isFileType<FileType::Toml>())
                    scanLineForTomlMultiline(curLine, lineState.inTomlMultiline,
                                             lineState.tomlQuote);

                if(isFileType<FileType::MarkupText>())
                    scanLineForMarkupFence(curLine, lineState.inMarkupFence,
                                           lineState.markupFenceChar);

                lineState.valid = true;
                buffer->syntaxCache[i] = lineState;
                buffer->syntaxCacheComputedUpTo = i;
            }
        };

        compute_to(absoluteLineNum);
        if(absoluteLineNum > 0 &&
           absoluteLineNum - 1 < (int)buffer->syntaxCache.size())
        {
            const auto& prev = buffer->syntaxCache[absoluteLineNum - 1];
            blockCommentState = prev.inBlockComment;
            tomlMultilineState = prev.inTomlMultiline;
            tomlQuote = prev.tomlQuote;
            markupFenceState = prev.inMarkupFence;
            markupFenceChar = prev.markupFenceChar;
        }
        if(absoluteLineNum < (int)buffer->syntaxCache.size())
        {
            const auto& cur = buffer->syntaxCache[absoluteLineNum];
            inCppMethodContext = cur.inCppMethodContext;
            inCppFunctionContext = cur.inCppFunctionContext;
            inCppParamListContext = cur.inCppParamListContext;
        }
    }
    else
    {
        if(isFileType<FileType::Cpp>() || isFileType<FileType::Mla>())
        {
            for(int i = 0; i < absoluteLineNum && i < (int)lines->size(); i++)
            {
                scanLineForBlockComments((*lines)[i], blockCommentState);
            }
        }
        if(isFileType<FileType::Cpp>())
        {
            CppFunctionScanState functionState;
            CppParamListScanState paramState;
            for(int i = 0; i < absoluteLineNum && i < (int)lines->size(); i++)
            {
                scan_line_for_cpp_function_context((*lines)[i], functionState,
                                                   nullptr);
                scan_line_for_cpp_param_list_context((*lines)[i], paramState,
                                                     nullptr);
            }
            bool inParamListAtLineStart = paramState.inParamList;
            bool lineHasFunctionStart = false;
            bool lineHasParamListStart = false;
            if(absoluteLineNum < (int)lines->size())
            {
                scan_line_for_cpp_function_context((*lines)[absoluteLineNum],
                                                   functionState,
                                                   &lineHasFunctionStart);
                scan_line_for_cpp_param_list_context((*lines)[absoluteLineNum],
                                                     paramState,
                                                     &lineHasParamListStart);
            }
            inCppFunctionContext =
                functionState.inFunction || lineHasFunctionStart;
            inCppParamListContext =
                inParamListAtLineStart || lineHasParamListStart;
        }
        if(isFileType<FileType::Cpp>() &&
           editor->syntaxCppHighlightImplicitMembers)
        {
            ensureCppMemberIndex();
            CppMethodScanState methodState;
            for(int i = 0; i < absoluteLineNum && i < (int)lines->size(); i++)
            {
                scan_line_for_cpp_method_context((*lines)[i], cppClassNames,
                                                 methodState, nullptr);
            }
            bool lineHasMethodStart = false;
            if(absoluteLineNum < (int)lines->size())
            {
                scan_line_for_cpp_method_context((*lines)[absoluteLineNum],
                                                 cppClassNames, methodState,
                                                 &lineHasMethodStart);
            }
            inCppMethodContext = methodState.inMethod || lineHasMethodStart;
        }
        if(isFileType<FileType::Toml>())
        {
            for(int i = 0; i < absoluteLineNum && i < (int)lines->size(); i++)
            {
                scanLineForTomlMultiline((*lines)[i], tomlMultilineState,
                                         tomlQuote);
            }
        }
        if(isFileType<FileType::MarkupText>())
        {
            for(int i = 0; i < absoluteLineNum && i < (int)lines->size(); i++)
            {
                scanLineForMarkupFence((*lines)[i], markupFenceState,
                                       markupFenceChar);
            }
        }
    }

    // Now tokenize the current line
    std::vector<Token> tokens =
        tokenizeLine(line, blockCommentState, tomlMultilineState, tomlQuote,
                     markupFenceState, markupFenceChar, inCppMethodContext,
                     inCppFunctionContext, inCppParamListContext);

    std::vector<TokenType> charColors(len, TOKEN_NORMAL);

    for(const auto& token : tokens)
    {
        TokenType effectiveType = token.type;
        if(isFileType<FileType::Cpp>() && editor)
        {
            if(token.type == TOKEN_MEMBER)
                effectiveType = editor->syntaxCppMemberToken;
        }
        int tokenEnd = token.start + token.length;
        for(int pos = token.start; pos < tokenEnd; pos++)
        {
            int visiblePos = pos - start;
            if(visiblePos >= 0 && visiblePos < len)
            {
                charColors[visiblePos] = effectiveType;
            }
        }
    }

    if(editor &&
       ((isFileType<FileType::Cpp>() && editor->syntaxCppSemanticTokens) ||
        (isFileType<FileType::Mla>() && editor->isMlangLspEnabled() &&
         editor->syntaxMlangSemanticTokens)))
    {
        Buffer* buffer = editor->currentBuffer;
        if(buffer && buffer->lspSemanticTokensValid && fileRow >= 0 &&
           fileRow < (int)buffer->lspSemanticTokens.size())
        {
            const auto& semTokens = buffer->lspSemanticTokens[fileRow];
            const bool isCppSemantics = isFileType<FileType::Cpp>();
            bool inFunctionContext = false;
            if(isCppSemantics && fileRow >= 0 &&
               fileRow < (int)buffer->syntaxCache.size())
            {
                const auto& state = buffer->syntaxCache[fileRow];
                if(state.valid)
                {
                    inFunctionContext =
                        state.inCppFunctionContext || state.inCppMethodContext;
                }
            }
            for(const auto& token : semTokens)
            {
                std::optional<TokenType> mapped;
                std::string_view type = token.tokenType;
                if(type == "type" || type == "class" || type == "struct" ||
                   type == "enum" || type == "interface" ||
                   type == "typeParameter")
                {
                    mapped = TOKEN_TYPE;
                }
                else if(type == "keyword")
                {
                    mapped = TOKEN_KEYWORD;
                }
                else if(type == "function" || type == "method")
                {
                    mapped = TOKEN_FUNCTION;
                }
                else if(type == "macro")
                {
                    mapped = TOKEN_FUNCTION;
                }
                else if(type == "parameter")
                {
                    mapped = isCppSemantics ? editor->syntaxCppLocalToken
                                            : TOKEN_NORMAL;
                }
                else if(type == "variable")
                {
                    if(!isCppSemantics)
                    {
                        mapped = TOKEN_NORMAL;
                    }
                    else
                    {
                        bool isObjectReference = false;
                        const std::string& lineRef = line;
                        int tokenStart = token.start;
                        int tokenEnd = token.start + token.length;
                        if(tokenEnd > tokenStart)
                        {
                            int p = tokenStart - 1;
                            while(p >= 0 && text_utils::is_space(lineRef[p]))
                                --p;
                            if(p >= 0 && lineRef[p] == '.')
                                isObjectReference = true;
                            if(p >= 1 && lineRef[p] == '>' &&
                               lineRef[p - 1] == '-')
                                isObjectReference = true;
                            if(!isObjectReference)
                            {
                                int q = tokenEnd;
                                while(q < (int)lineRef.size() &&
                                      text_utils::is_space(lineRef[q]))
                                    ++q;
                                if(q < (int)lineRef.size() &&
                                   (lineRef[q] == '.' ||
                                    (lineRef[q] == '-' &&
                                     q + 1 < (int)lineRef.size() &&
                                     lineRef[q + 1] == '>')))
                                {
                                    isObjectReference = true;
                                }
                            }
                        }
                        if(!inFunctionContext &&
                           (token.isDeclaration || token.isDefinition))
                        {
                            mapped = editor->syntaxCppMemberToken;
                        }
                        else
                        {
                            mapped = isObjectReference
                                         ? editor->syntaxCppMemberToken
                                         : editor->syntaxCppLocalToken;
                        }
                    }
                }
                else if(type == "property" || type == "enumMember" ||
                        type == "member" || type == "field")
                {
                    mapped = isCppSemantics ? editor->syntaxCppMemberToken
                                            : TOKEN_MEMBER;
                }
                if(!mapped)
                    continue;
                TokenType effectiveType = *mapped;
                int tokenStart = token.start;
                int tokenEnd = token.start + token.length;
                for(int pos = tokenStart; pos < tokenEnd; pos++)
                {
                    int visiblePos = pos - start;
                    if(visiblePos >= 0 && visiblePos < len)
                    {
                        if(!isCppSemantics && effectiveType == TOKEN_NORMAL &&
                           charColors[visiblePos] == TOKEN_TYPE)
                        {
                            continue;
                        }
                        charColors[visiblePos] = effectiveType;
                    }
                }
            }
        }
    }

    TokenType currentColor = TOKEN_NORMAL;
    for(int x = 0; x < len;)
    {
        int col = x + start;
        int nextCol = text_utils::nextUtf8CharStart(line, col);
        nextCol = std::clamp(nextCol, col + 1, start + len);
        int charLen = nextCol - col;

        bool highlighted = false;
        if(isInSelection(fileRow, col) || isInVisualBlock(fileRow, col))
        {
            output += theme.selection();
            highlighted = true;
        }
        else if(isInSearchMatch(fileRow, col))
        {
            output += theme.searchMatch();
            highlighted = true;
        }

        if(!highlighted && charColors[x] != currentColor)
        {
            currentColor = charColors[x];
            output += getColorCode(currentColor);
        }

        output.append(line, col, charLen);

        if(highlighted)
        {
            output += theme.reset();
            currentColor = TOKEN_NORMAL;
        }
        x += charLen;
    }

    if(currentColor != TOKEN_NORMAL)
    {
        output += theme.baseFg();
    }
}
