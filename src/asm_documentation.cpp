#include "asm_documentation.h"

#include "text_utils.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace asm_documentation
{
namespace
{
struct DocEntry
{
    std::string_view mnemonic;
    std::string_view summary;
    std::string_view reference;
};

constexpr std::array x86Docs = {
    DocEntry{"adc", "Add with carry.", "https://www.felixcloutier.com/x86/adc"},
    DocEntry{"add", "Add source operand to destination operand.",
             "https://www.felixcloutier.com/x86/add"},
    DocEntry{"and", "Bitwise AND.",
             "https://www.felixcloutier.com/x86/and"},
    DocEntry{"call", "Call procedure.",
             "https://www.felixcloutier.com/x86/call"},
    DocEntry{"cmp", "Compare operands.",
             "https://www.felixcloutier.com/x86/cmp"},
    DocEntry{"dec", "Decrement by one.",
             "https://www.felixcloutier.com/x86/dec"},
    DocEntry{"div", "Unsigned divide.",
             "https://www.felixcloutier.com/x86/div"},
    DocEntry{"idiv", "Signed divide.",
             "https://www.felixcloutier.com/x86/idiv"},
    DocEntry{"imul", "Signed multiply.",
             "https://www.felixcloutier.com/x86/imul"},
    DocEntry{"inc", "Increment by one.",
             "https://www.felixcloutier.com/x86/inc"},
    DocEntry{"jcc", "Conditional jump.",
             "https://www.felixcloutier.com/x86/jcc"},
    DocEntry{"jmp", "Unconditional jump.",
             "https://www.felixcloutier.com/x86/jmp"},
    DocEntry{"lea", "Load effective address.",
             "https://www.felixcloutier.com/x86/lea"},
    DocEntry{"leave", "High-level procedure exit.",
             "https://www.felixcloutier.com/x86/leave"},
    DocEntry{"mov", "Move data.",
             "https://www.felixcloutier.com/x86/mov"},
    DocEntry{"movsx", "Move with sign-extension.",
             "https://www.felixcloutier.com/x86/movsx:movsxd"},
    DocEntry{"movsxd", "Move with sign-extension.",
             "https://www.felixcloutier.com/x86/movsx:movsxd"},
    DocEntry{"movzx", "Move with zero-extension.",
             "https://www.felixcloutier.com/x86/movzx"},
    DocEntry{"mul", "Unsigned multiply.",
             "https://www.felixcloutier.com/x86/mul"},
    DocEntry{"neg", "Two's complement negation.",
             "https://www.felixcloutier.com/x86/neg"},
    DocEntry{"nop", "No operation.",
             "https://www.felixcloutier.com/x86/nop"},
    DocEntry{"not", "One's complement negation.",
             "https://www.felixcloutier.com/x86/not"},
    DocEntry{"or", "Bitwise OR.", "https://www.felixcloutier.com/x86/or"},
    DocEntry{"pop", "Pop value from stack.",
             "https://www.felixcloutier.com/x86/pop"},
    DocEntry{"push", "Push value onto stack.",
             "https://www.felixcloutier.com/x86/push"},
    DocEntry{"ret", "Return from procedure.",
             "https://www.felixcloutier.com/x86/ret"},
    DocEntry{"sal", "Shift arithmetic left.",
             "https://www.felixcloutier.com/x86/sal:sar:shl:shr"},
    DocEntry{"sar", "Shift arithmetic right.",
             "https://www.felixcloutier.com/x86/sal:sar:shl:shr"},
    DocEntry{"setcc", "Set byte on condition.",
             "https://www.felixcloutier.com/x86/setcc"},
    DocEntry{"shl", "Shift logical left.",
             "https://www.felixcloutier.com/x86/sal:sar:shl:shr"},
    DocEntry{"shr", "Shift logical right.",
             "https://www.felixcloutier.com/x86/sal:sar:shl:shr"},
    DocEntry{"sub", "Subtract.",
             "https://www.felixcloutier.com/x86/sub"},
    DocEntry{"test", "Logical compare.",
             "https://www.felixcloutier.com/x86/test"},
    DocEntry{"xor", "Bitwise exclusive OR.",
             "https://www.felixcloutier.com/x86/xor"},
};

constexpr std::array aarch64Docs = {
    DocEntry{"adc", "Add with carry.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/adc.html"},
    DocEntry{"add", "Add.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/add_addsub_imm.html"},
    DocEntry{"adr", "Form PC-relative address.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/adr.html"},
    DocEntry{"adrp", "Form page-relative address.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/adrp.html"},
    DocEntry{"and", "Bitwise AND.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/and_log_imm.html"},
    DocEntry{"asr", "Arithmetic shift right.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/asr_asrv.html"},
    DocEntry{"b", "Branch.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/b_uncond.html"},
    DocEntry{"bl", "Branch with link.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/bl.html"},
    DocEntry{"br", "Branch to register.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/br.html"},
    DocEntry{"cbnz", "Compare and branch if nonzero.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/cbnz.html"},
    DocEntry{"cbz", "Compare and branch if zero.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/cbz.html"},
    DocEntry{"cmp", "Compare.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/cmp_subs_addsub_imm.html"},
    DocEntry{"csel", "Conditional select.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/csel.html"},
    DocEntry{"cset", "Conditional set.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/cset_csinc.html"},
    DocEntry{"eor", "Bitwise exclusive OR.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/eor_log_imm.html"},
    DocEntry{"fadd", "Floating-point add.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/fadd_float.html"},
    DocEntry{"fcmp", "Floating-point compare.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/fcmp_float.html"},
    DocEntry{"fdiv", "Floating-point divide.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/fdiv_float.html"},
    DocEntry{"fmov", "Floating-point move.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/fmov_float.html"},
    DocEntry{"fmul", "Floating-point multiply.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/fmul_float.html"},
    DocEntry{"fsub", "Floating-point subtract.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/fsub_float.html"},
    DocEntry{"ldr", "Load register.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/ldr_imm_gen.html"},
    DocEntry{"ldp", "Load pair of registers.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/ldp_gen.html"},
    DocEntry{"lsl", "Logical shift left.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/lsl_lslv.html"},
    DocEntry{"lsr", "Logical shift right.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/lsr_lsrv.html"},
    DocEntry{"madd", "Multiply-add.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/madd.html"},
    DocEntry{"mov", "Move.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/mov_orr_log_imm.html"},
    DocEntry{"movk", "Move wide with keep.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/movk.html"},
    DocEntry{"movn", "Move wide with NOT.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/movn.html"},
    DocEntry{"movz", "Move wide with zero.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/movz.html"},
    DocEntry{"mul", "Multiply.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/mul_madd.html"},
    DocEntry{"orr", "Bitwise OR.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/orr_log_imm.html"},
    DocEntry{"ret", "Return from subroutine.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/ret.html"},
    DocEntry{"sbc", "Subtract with carry.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/sbc.html"},
    DocEntry{"sdiv", "Signed divide.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/sdiv.html"},
    DocEntry{"stp", "Store pair of registers.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/stp_gen.html"},
    DocEntry{"str", "Store register.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/str_imm_gen.html"},
    DocEntry{"sub", "Subtract.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/sub_addsub_imm.html"},
    DocEntry{"svc", "Supervisor call.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/svc.html"},
    DocEntry{"tbnz", "Test bit and branch if nonzero.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/tbnz.html"},
    DocEntry{"tbz", "Test bit and branch if zero.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/tbz.html"},
    DocEntry{"udiv", "Unsigned divide.",
             "https://www.scs.stanford.edu/~zyedidia/arm64/udiv.html"},
};

std::string lower(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for(char ch : value)
        out.push_back(text_utils::ascii_tolower(ch));
    return out;
}

bool contains(const auto& docs, std::string_view mnemonic)
{
    return std::any_of(docs.begin(), docs.end(),
                       [&](const DocEntry& doc)
                       { return doc.mnemonic == mnemonic; });
}

std::optional<DocEntry> lookup(const auto& docs, std::string_view mnemonic)
{
    auto it = std::find_if(docs.begin(), docs.end(),
                           [&](const DocEntry& doc)
                           { return doc.mnemonic == mnemonic; });
    if(it == docs.end())
        return std::nullopt;
    return *it;
}

bool starts_numeric_reg(std::string_view word, char prefix, int max)
{
    if(word.size() < 2 || word[0] != prefix)
        return false;
    int value = 0;
    for(char ch : word.substr(1))
    {
        if(!text_utils::is_digit(ch))
            return false;
        value = value * 10 + (ch - '0');
    }
    return value >= 0 && value <= max;
}

bool lineLooksAarch64(std::string_view line)
{
    std::string lowered = lower(line);
    size_t start = 0;
    while(start < lowered.size())
    {
        while(start < lowered.size() &&
              !(text_utils::is_alpha(lowered[start]) || lowered[start] == '_'))
        {
            ++start;
        }
        size_t end = start;
        while(end < lowered.size() &&
              (text_utils::is_alpha(lowered[end]) ||
               text_utils::is_digit(lowered[end]) || lowered[end] == '_'))
        {
            ++end;
        }
        std::string_view word(lowered.data() + start, end - start);
        if(word == "sp" || word == "lr" || word == "fp" || word == "xzr" ||
           word == "wzr" || starts_numeric_reg(word, 'x', 30) ||
           starts_numeric_reg(word, 'w', 30) ||
           starts_numeric_reg(word, 'v', 31))
        {
            return true;
        }
        start = end + 1;
    }
    return false;
}

std::optional<std::string> instructionMnemonic(std::string_view line)
{
    size_t comment = line.find("//");
    size_t hash = line.find('#');
    size_t semi = line.find(';');
    size_t end = line.size();
    for(size_t pos : {comment, hash, semi})
        if(text_utils::is_found(pos))
            end = std::min(end, pos);
    line = line.substr(0, end);

    size_t i = 0;
    while(i < line.size() && text_utils::is_space(line[i]))
        ++i;
    if(i >= line.size() || line[i] == '.')
        return std::nullopt;

    auto read_word = [&](size_t& pos) -> std::string
    {
        while(pos < line.size() && text_utils::is_space(line[pos]))
            ++pos;
        size_t start = pos;
        while(pos < line.size() &&
              (text_utils::is_alpha(line[pos]) ||
               text_utils::is_digit(line[pos]) || line[pos] == '_' ||
               line[pos] == '.'))
        {
            ++pos;
        }
        return lower(line.substr(start, pos - start));
    };

    std::string first = read_word(i);
    if(first.empty())
        return std::nullopt;

    size_t afterFirst = i;
    while(afterFirst < line.size() && text_utils::is_space(line[afterFirst]))
        ++afterFirst;
    if(afterFirst < line.size() && line[afterFirst] == ':')
    {
        ++afterFirst;
        std::string second = read_word(afterFirst);
        if(second.empty() || (!second.empty() && second[0] == '.'))
            return std::nullopt;
        return second;
    }

    return first;
}

std::string normalizeX86(std::string mnemonic)
{
    if(mnemonic.starts_with("set") && mnemonic.size() > 3)
        return "setcc";
    if(mnemonic.size() == 2 && mnemonic[0] == 'j' && mnemonic != "jp" &&
       mnemonic != "js")
        return "jcc";
    if(mnemonic.starts_with("movsx"))
        return mnemonic == "movsxd" ? "movsxd" : "movsx";
    if(mnemonic.starts_with("movzx"))
        return "movzx";
    if(mnemonic.starts_with("mov"))
        return "mov";

    while(mnemonic.size() > 2)
    {
        char suffix = mnemonic.back();
        if(suffix != 'b' && suffix != 'w' && suffix != 'l' && suffix != 'q')
            break;
        std::string base = mnemonic.substr(0, mnemonic.size() - 1);
        if(contains(x86Docs, base))
            return base;
        mnemonic = std::move(base);
    }
    return mnemonic;
}

std::string normalizeAarch64(std::string mnemonic)
{
    if(mnemonic.starts_with("b."))
        return "b";
    if(mnemonic == "adds" || mnemonic == "adcs")
        return "add";
    if(mnemonic == "subs")
        return "sub";
    if(mnemonic == "ands")
        return "and";
    if(mnemonic == "ldrb" || mnemonic == "ldrh" || mnemonic == "ldrsb" ||
       mnemonic == "ldrsh" || mnemonic == "ldur")
        return "ldr";
    if(mnemonic == "strb" || mnemonic == "strh" || mnemonic == "stur")
        return "str";
    if(mnemonic == "msub")
        return "madd";
    return mnemonic;
}

fs::path cacheRoot()
{
    if(const char* env = std::getenv("UVIM_ASM_DOCS_CACHE_DIR"))
        return env;
    if(const char* xdg = std::getenv("XDG_CACHE_HOME"))
        return fs::path(xdg) / "uvim" / "asm-docs";
    if(const char* home = std::getenv("HOME"))
        return fs::path(home) / ".cache" / "uvim" / "asm-docs";
    return fs::temp_directory_path() / "uvim" / "asm-docs";
}

int writeDocs(const fs::path& path, std::string_view title, const auto& docs)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    if(!out)
        return -1;

    out << "# " << title << " Assembly Instructions\n\n";
    out << "Generated by uvim. These entries are a compact local jump index "
           "with reference links to architecture documentation.\n\n";

    int line = 4;
    for(const DocEntry& doc : docs)
    {
        out << "## " << doc.mnemonic << "\n\n";
        out << doc.summary << "\n\n";
        out << "Reference: " << doc.reference << "\n\n";
        line += 5;
    }
    return line;
}

fs::path docsPath(std::string_view arch)
{
    return cacheRoot() / (std::string(arch) + ".md");
}

int ensureDocs(std::string_view arch, const auto& docs,
               std::string_view mnemonic)
{
    fs::path path = docsPath(arch);
    std::error_code ec;
    if(!fs::exists(path, ec))
        writeDocs(path, arch == "x86" ? "x86/x64" : "AArch64", docs);

    std::ifstream in(path);
    if(!in)
        return -1;
    const std::string marker = "## " + std::string(mnemonic);
    std::string line;
    int lineNo = 0;
    while(std::getline(in, line))
    {
        if(line == marker)
            return lineNo;
        ++lineNo;
    }
    return -1;
}
} // namespace

std::optional<Location> find(std::string_view line, int)
{
#ifndef UVIM_ENABLE_ASM_DOCS
    (void)line;
    return std::nullopt;
#else
    std::optional<std::string> raw = instructionMnemonic(line);
    if(!raw)
        return std::nullopt;

    const std::string x86 = normalizeX86(*raw);
    const std::string arm = normalizeAarch64(*raw);
    const bool hasX86 = contains(x86Docs, x86);
    const bool hasArm = contains(aarch64Docs, arm);
    if(!hasX86 && !hasArm)
        return std::nullopt;

    const bool chooseArm = hasArm && (!hasX86 || lineLooksAarch64(line));
    const std::string_view arch = chooseArm ? "aarch64" : "x86";
    const std::string& mnemonic = chooseArm ? arm : x86;
    const int lineNo =
        chooseArm ? ensureDocs(arch, aarch64Docs, mnemonic)
                  : ensureDocs(arch, x86Docs, mnemonic);
    if(lineNo < 0)
        return std::nullopt;

    return Location{docsPath(arch).string(), lineNo, mnemonic,
                    std::string(arch)};
#endif
}
} // namespace asm_documentation
