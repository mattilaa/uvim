#include "asm_documentation.h"

#include "json_utils.h"
#include "process_pipe.h"
#include "text_utils.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace asm_documentation
{
namespace
{
std::atomic_bool fetchOriginalDocsEnabled{false};

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

std::string embeddedDetails(std::string_view arch, std::string_view mnemonic)
{
    if(arch == "x86")
    {
        if(mnemonic == "mov")
            return "Copies the source operand into the destination operand. "
                   "The source is not modified. Common AT&T forms from clang "
                   "include `movq src, dst`, `movl src, dst`, and immediate "
                   "moves such as `movl $1, %eax`.";
        if(mnemonic == "lea")
            return "Computes an address expression and writes the resulting "
                   "integer address to the destination register. It does not "
                   "load memory; it is often used for pointer arithmetic and "
                   "some integer arithmetic.";
        if(mnemonic == "add")
            return "Adds the source operand to the destination operand and "
                   "stores the result in the destination. Arithmetic flags are "
                   "updated.";
        if(mnemonic == "sub")
            return "Subtracts the source operand from the destination operand "
                   "and stores the result in the destination. Arithmetic flags "
                   "are updated.";
        if(mnemonic == "cmp")
            return "Compares operands by subtracting the source from the "
                   "destination for flags only. The operands are not modified; "
                   "conditional jumps normally consume the resulting flags.";
        if(mnemonic == "test")
            return "Computes a bitwise AND for flags only. The operands are not "
                   "modified. Commonly used to test whether a register is zero.";
        if(mnemonic == "xor")
            return "Bitwise exclusive OR. `xorl %eax, %eax` is a common "
                   "zeroing idiom because writing a 32-bit register "
                   "zero-extends to the full 64-bit register.";
        if(mnemonic == "and")
            return "Bitwise AND of source and destination, storing the result "
                   "in the destination and updating flags.";
        if(mnemonic == "or")
            return "Bitwise OR of source and destination, storing the result in "
                   "the destination and updating flags.";
        if(mnemonic == "call")
            return "Pushes the return address and branches to the target "
                   "procedure. The target may be direct or indirect through a "
                   "register or memory operand.";
        if(mnemonic == "ret")
            return "Returns from a procedure by popping the return address from "
                   "the stack and branching to it.";
        if(mnemonic == "push")
            return "Decrements the stack pointer and stores the operand on the "
                   "stack.";
        if(mnemonic == "pop")
            return "Loads a value from the top of the stack into the operand "
                   "and increments the stack pointer.";
        if(mnemonic == "jcc")
            return "Conditional branch family such as `je`, `jne`, `jl`, and "
                   "`jg`. The branch decision is based on status flags set by "
                   "earlier instructions such as `cmp` or `test`.";
        if(mnemonic == "jmp")
            return "Unconditional branch to a direct label or an indirect "
                   "register/memory target.";
        if(mnemonic == "setcc")
            return "Writes 0 or 1 to an 8-bit destination depending on a "
                   "condition-code test of the current flags.";
        if(mnemonic == "nop")
            return "No operation. Commonly emitted for alignment or patchable "
                   "instruction space.";
    }
    if(arch == "aarch64")
    {
        if(mnemonic == "mov")
            return "Copies or materializes a value into a register. In AArch64 "
                   "assembly this is often an alias for instructions such as "
                   "`orr`, `movz`, `movn`, or `movk` depending on operands.";
        if(mnemonic == "movz")
            return "Moves a 16-bit immediate into a selected halfword of the "
                   "destination register and zeros the other bits.";
        if(mnemonic == "movk")
            return "Moves a 16-bit immediate into a selected halfword of the "
                   "destination register while keeping the other bits.";
        if(mnemonic == "ldr")
            return "Loads a register from memory. Common forms include base "
                   "plus immediate addressing such as `ldr x0, [sp, #8]` and "
                   "literal loads from PC-relative addresses.";
        if(mnemonic == "str")
            return "Stores a register to memory using forms such as base plus "
                   "immediate addressing.";
        if(mnemonic == "ldp")
            return "Loads a pair of registers from adjacent memory locations. "
                   "Often used in function epilogues to restore saved "
                   "registers.";
        if(mnemonic == "stp")
            return "Stores a pair of registers to adjacent memory locations. "
                   "Often used in function prologues to save registers.";
        if(mnemonic == "add")
            return "Adds operands and writes the result to the destination "
                   "register. Immediate and shifted-register forms are common.";
        if(mnemonic == "sub")
            return "Subtracts operands and writes the result to the destination "
                   "register. `subs` additionally updates condition flags.";
        if(mnemonic == "cmp")
            return "Comparison alias that subtracts for flags without keeping "
                   "the result. Conditional branches or selects consume the "
                   "flags.";
        if(mnemonic == "b")
            return "Unconditional or condition-suffixed branch. Forms such as "
                   "`b.eq` and `b.ne` branch based on condition flags.";
        if(mnemonic == "bl")
            return "Branch with link. Stores the return address in `lr` and "
                   "branches to the target function.";
        if(mnemonic == "br")
            return "Branches to the address held in a register.";
        if(mnemonic == "ret")
            return "Returns from a subroutine, normally by branching to the "
                   "address in `lr`.";
        if(mnemonic == "cbz")
            return "Compare a register with zero and branch if it is zero. It "
                   "does not update condition flags.";
        if(mnemonic == "cbnz")
            return "Compare a register with zero and branch if it is nonzero. "
                   "It does not update condition flags.";
        if(mnemonic == "csel")
            return "Conditional select. Chooses between two source registers "
                   "based on condition flags and writes the chosen value.";
        if(mnemonic == "adr")
            return "Computes a PC-relative address within a small range and "
                   "writes it to a register.";
        if(mnemonic == "adrp")
            return "Computes the page address of a PC-relative symbol. Usually "
                   "paired with an `add` or load/store instruction.";
    }
    return {};
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
        std::string details = embeddedDetails(
            title == "x86/x64" ? std::string_view{"x86"}
                               : std::string_view{"aarch64"},
            doc.mnemonic);
        if(!details.empty())
            out << "Documentation: " << details << "\n\n";
        out << "Reference: " << doc.reference << "\n\n";
        line += details.empty() ? 5 : 7;
    }
    return line;
}

fs::path docsPath(std::string_view arch)
{
    return cacheRoot() / (std::string(arch) + ".md");
}

fs::path fetchedDocsPath(std::string_view arch, std::string_view mnemonic)
{
    return cacheRoot() / "fetched" / "compiler-explorer" / std::string(arch) /
           (std::string(mnemonic) + ".md");
}

fs::path fallbackFetchedDocsPath(std::string_view arch,
                                 std::string_view mnemonic)
{
    return cacheRoot() / "fetched" / "source" / std::string(arch) /
           (std::string(mnemonic) + ".md");
}

std::string compilerExplorerArch(std::string_view arch)
{
    if(arch == "x86")
        return "amd64";
    return std::string(arch);
}

std::string compilerExplorerApiUrl(std::string_view arch,
                                   std::string_view mnemonic)
{
    return "https://godbolt.org/api/asm/" + compilerExplorerArch(arch) + "/" +
           std::string(mnemonic);
}

std::string decodeHtmlEntity(std::string_view entity)
{
    if(entity == "amp")
        return "&";
    if(entity == "lt")
        return "<";
    if(entity == "gt")
        return ">";
    if(entity == "quot")
        return "\"";
    if(entity == "apos" || entity == "#39")
        return "'";
    if(entity == "nbsp")
        return " ";
    return "&" + std::string(entity) + ";";
}

std::string htmlToText(std::string_view html)
{
    std::string text;
    text.reserve(html.size() / 2);
    bool inTag = false;
    bool inScript = false;
    bool inStyle = false;
    std::string tag;

    auto starts_case_insensitive = [](std::string_view value,
                                      std::string_view prefix)
    {
        if(value.size() < prefix.size())
            return false;
        for(size_t i = 0; i < prefix.size(); ++i)
        {
            if(text_utils::ascii_tolower(value[i]) != prefix[i])
                return false;
        }
        return true;
    };

    for(size_t i = 0; i < html.size(); ++i)
    {
        char ch = html[i];
        if(inTag)
        {
            if(ch == '>')
            {
                std::string lowerTag = lower(tag);
                if(starts_case_insensitive(lowerTag, "script"))
                    inScript = true;
                else if(starts_case_insensitive(lowerTag, "/script"))
                    inScript = false;
                else if(starts_case_insensitive(lowerTag, "style"))
                    inStyle = true;
                else if(starts_case_insensitive(lowerTag, "/style"))
                    inStyle = false;
                else if(!inScript && !inStyle &&
                        (starts_case_insensitive(lowerTag, "br") ||
                         starts_case_insensitive(lowerTag, "p") ||
                         starts_case_insensitive(lowerTag, "/p") ||
                         starts_case_insensitive(lowerTag, "div") ||
                         starts_case_insensitive(lowerTag, "/div") ||
                         starts_case_insensitive(lowerTag, "h") ||
                         starts_case_insensitive(lowerTag, "/h") ||
                         starts_case_insensitive(lowerTag, "li") ||
                         starts_case_insensitive(lowerTag, "/tr")))
                {
                    if(!text.empty() && text.back() != '\n')
                        text.push_back('\n');
                }
                tag.clear();
                inTag = false;
            }
            else
            {
                tag.push_back(ch);
            }
            continue;
        }

        if(ch == '<')
        {
            inTag = true;
            tag.clear();
            continue;
        }
        if(inScript || inStyle)
            continue;
        if(ch == '&')
        {
            size_t semi = html.find(';', i + 1);
            if(text_utils::is_found(semi) && semi - i <= 12)
            {
                text += decodeHtmlEntity(html.substr(i + 1, semi - i - 1));
                i = semi;
                continue;
            }
        }
        text.push_back(ch == '\r' ? '\n' : ch);
    }

    std::string compact;
    compact.reserve(text.size());
    bool blankLine = false;
    bool lineHasText = false;
    for(char ch : text)
    {
        if(ch == '\n')
        {
            if(!lineHasText)
            {
                if(!blankLine && !compact.empty())
                {
                    compact.push_back('\n');
                    blankLine = true;
                }
            }
            else
            {
                compact.push_back('\n');
                blankLine = false;
            }
            lineHasText = false;
            continue;
        }
        if(text_utils::is_space(ch))
        {
            if(!compact.empty() && compact.back() != ' ' &&
               compact.back() != '\n')
                compact.push_back(' ');
            continue;
        }
        compact.push_back(ch);
        lineHasText = true;
    }
    while(!compact.empty() &&
          (compact.back() == '\n' || compact.back() == ' '))
        compact.pop_back();
    return compact;
}

std::optional<std::string> fetchUrl(std::string_view url)
{
    ProcessPipe pipe({"curl", "-LfsS", "--max-time", "8", std::string(url)});
    if(!pipe)
        return std::nullopt;
    std::string output = pipe.readAll();
    const int status = pipe.close();
    if(status != 0 || output.empty())
        return std::nullopt;
    return output;
}

std::string wrapDocumentationLine(std::string_view line, int width)
{
    if(width <= 0 || text_utils::displayWidth(line) <= width)
        return std::string(line);

    std::string out;
    std::string current;
    size_t pos = 0;
    while(pos < line.size())
    {
        while(pos < line.size() && text_utils::is_space(line[pos]))
            ++pos;
        size_t start = pos;
        while(pos < line.size() && !text_utils::is_space(line[pos]))
            ++pos;
        if(start == pos)
            break;

        std::string word(line.substr(start, pos - start));
        int nextWidth = text_utils::displayWidth(current) +
                        (current.empty() ? 0 : 1) +
                        text_utils::displayWidth(word);
        if(nextWidth > width && !current.empty())
        {
            if(!out.empty())
                out.push_back('\n');
            out += current;
            current = "  " + word;
        }
        else
        {
            if(!current.empty())
                current.push_back(' ');
            current += word;
        }
    }

    if(!current.empty())
    {
        if(!out.empty())
            out.push_back('\n');
        out += current;
    }
    return out;
}

std::string wrapDocumentationText(std::string_view text, int width = 96)
{
    std::string out;
    size_t start = 0;
    while(start <= text.size())
    {
        size_t end = text.find('\n', start);
        std::string_view line =
            end == std::string_view::npos
                ? text.substr(start)
                : text.substr(start, end - start);
        if(!out.empty())
            out.push_back('\n');
        out += wrapDocumentationLine(line, width);
        if(end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return out;
}

std::optional<std::string> fetchJsonUrl(std::string_view url)
{
    ProcessPipe pipe({"curl", "-LfsS", "--max-time", "8", "-H",
                      "Accept: application/json", std::string(url)});
    if(!pipe)
        return std::nullopt;
    std::string output = pipe.readAll();
    const int status = pipe.close();
    if(status != 0 || output.empty())
        return std::nullopt;
    return output;
}

struct CompilerExplorerDoc
{
    std::string text;
    std::string sourceUrl;
};

std::optional<CompilerExplorerDoc>
parseCompilerExplorerDoc(std::string_view json)
{
    json_utils::Document doc;
    if(!json_utils::parse(doc, json) || !doc.IsObject())
        return std::nullopt;

    std::string html = json_utils::get_string(doc, "html");
    std::string tooltip = json_utils::get_string(doc, "tooltip");
    std::string sourceUrl = json_utils::get_string(doc, "url");

    std::string text = html.empty() ? tooltip : htmlToText(html);
    if(text.empty())
        text = htmlToText(tooltip);
    if(text.empty())
        return std::nullopt;

    return CompilerExplorerDoc{std::move(text), std::move(sourceUrl)};
}

std::optional<CompilerExplorerDoc>
fetchCompilerExplorerDoc(std::string_view arch, const DocEntry& doc)
{
    std::optional<std::string> response =
        fetchJsonUrl(compilerExplorerApiUrl(arch, doc.mnemonic));
    if(!response)
        return std::nullopt;
    return parseCompilerExplorerDoc(*response);
}

std::optional<fs::path>
ensureFetchedDoc(std::string_view arch, const DocEntry& doc)
{
    fs::path path = fetchedDocsPath(arch, doc.mnemonic);
    std::error_code ec;
    if(fs::exists(path, ec))
        return path;

    if(std::optional<CompilerExplorerDoc> ce =
           fetchCompilerExplorerDoc(arch, doc))
    {
        fs::create_directories(path.parent_path(), ec);
        std::ofstream out(path);
        if(out)
        {
            out << "## " << doc.mnemonic << "\n\n";
            out << doc.summary << "\n\n";
            out << "Fetched from Compiler Explorer asm docs API.\n\n";
            out << "Compiler Explorer documentation:\n\n";
            out << "```text\n";
            out << wrapDocumentationText(ce->text) << "\n";
            out << "```\n\n";
            if(!ce->sourceUrl.empty())
                out << "Source: " << ce->sourceUrl << "\n\n";
            out << "API: " << compilerExplorerApiUrl(arch, doc.mnemonic)
                << "\n";
            return path;
        }
    }

    path = fallbackFetchedDocsPath(arch, doc.mnemonic);
    if(fs::exists(path, ec))
        return path;
    std::optional<std::string> html = fetchUrl(doc.reference);
    if(!html)
        return std::nullopt;

    std::string original = htmlToText(*html);
    if(original.empty())
        return std::nullopt;

    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    if(!out)
        return std::nullopt;

    out << "## " << doc.mnemonic << "\n\n";
    out << doc.summary << "\n\n";
    out << "Source: " << doc.reference << "\n\n";
    out << "Source documentation fallback:\n\n";
    out << "```text\n";
    out << wrapDocumentationText(original) << "\n";
    out << "```\n";
    return path;
}

int ensureDocs(std::string_view arch, const auto& docs,
               std::string_view mnemonic)
{
    fs::path path = docsPath(arch);
    std::error_code ec;
    bool needsWrite = !fs::exists(path, ec);
    if(!needsWrite)
    {
        std::ifstream existing(path);
        std::string contents((std::istreambuf_iterator<char>(existing)),
                             std::istreambuf_iterator<char>());
        needsWrite = text_utils::is_not_found(contents.find("Documentation:"));
    }
    if(needsWrite)
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

void setFetchOriginalDocs(bool enabled)
{
    fetchOriginalDocsEnabled.store(enabled);
}

bool fetchOriginalDocs()
{
    if(fetchOriginalDocsEnabled.load())
        return true;
    if(const char* env = std::getenv("UVIM_ASM_DOCS_FETCH"))
        return std::string_view(env) == "1" || std::string_view(env) == "true" ||
               std::string_view(env) == "TRUE" || std::string_view(env) == "yes";
    return false;
}

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
    std::optional<DocEntry> doc =
        chooseArm ? lookup(aarch64Docs, mnemonic) : lookup(x86Docs, mnemonic);
    if(fetchOriginalDocs() && doc)
    {
        if(std::optional<fs::path> fetched = ensureFetchedDoc(arch, *doc))
        {
            return Location{fetched->string(), 0, mnemonic,
                            std::string(arch)};
        }
    }

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
