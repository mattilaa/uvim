#include "structural_navigation.h"
#include <gtest/gtest.h>

using structural_navigation::Position;
using structural_navigation::Target;
using structural_navigation::targets;

namespace
{
std::vector<std::string> cpp = {
    "// void fake() { wrong(); }",
    "int first(",
    "    int parameter)",
    "{",
    "    auto value = compute(parameter);",
    "    if (ready(value)) {",
    "        Widget local;",
    "        service.send(value);",
    "    }",
    "    const char* text = R\"tag(void fake() { nope(); })tag\";",
    "    /* int invented() { wrong(); } */",
    "    return value;",
    "}",
    "void prototype();",
    "void second() { finish(); }"};

Position at(const std::vector<std::string>& lines, int row,
            const std::string& word)
{
    return {row, static_cast<int>(lines[row].find(word))};
}
} // namespace

TEST(StructuralNavigation, CppDefinitionsIgnoreCallsControlFlowAndLiterals)
{
    EXPECT_EQ(
        targets(cpp, FileType::Cpp, Target::Function, {}),
        (std::vector<Position>{at(cpp, 1, "first"), at(cpp, 14, "second")}));
}

TEST(StructuralNavigation, LocalDeclarationsAndCallsIncludeNestedBlocks)
{
    EXPECT_EQ(targets(cpp, FileType::Cpp, Target::Local, {2, 0}),
              (std::vector<Position>{at(cpp, 4, "value"), at(cpp, 4, "compute"),
                                     at(cpp, 5, "ready"), at(cpp, 6, "local"),
                                     at(cpp, 7, "send"), at(cpp, 9, "text")}));
    EXPECT_TRUE(targets(cpp, FileType::Cpp, Target::Local, {13, 0}).empty());
    EXPECT_EQ(targets(cpp, FileType::Cpp, Target::Local, {14, 20}),
              (std::vector<Position>{at(cpp, 14, "finish")}));
}

TEST(StructuralNavigation, PythonMultilineAsyncMethodsAndNestedFunctionScopes)
{
    const std::vector<std::string> lines = {
        "class Service:",
        "    async def outer(",
        "        self, argument: str,",
        "    ) -> str:",
        "        text = '''def fake(): wrong()'''",
        "        if ready(argument):",
        "            value = fetch(argument)",
        "        def nested():",
        "            hidden = inner_call()",
        "        return finish(value)",
        "    def next_method(self):",
        "        other = work()"};
    EXPECT_EQ(
        targets(lines, FileType::Python, Target::Function, {}),
        (std::vector<Position>{at(lines, 1, "outer"), at(lines, 7, "nested"),
                               at(lines, 10, "next_method")}));
    EXPECT_EQ(
        targets(lines, FileType::Python, Target::Local, {1, 0}),
        (std::vector<Position>{at(lines, 4, "text"), at(lines, 5, "ready"),
                               at(lines, 6, "value"), at(lines, 6, "fetch"),
                               at(lines, 9, "finish")}));
    EXPECT_EQ(targets(lines, FileType::Python, Target::Local, {8, 15}),
              (std::vector<Position>{at(lines, 8, "hidden"),
                                     at(lines, 8, "inner_call")}));
}

TEST(StructuralNavigation, PythonContinuationDedentsAndInlineFunctions)
{
    const std::vector<std::string> lines = {
        "def first():",
        "    value = call(",
        "0,", // Dedented continuation is still inside the function.
        "    )",
        "    return finish(value)",
        "def inline(): return work()",
        "outside = global_call()"};
    EXPECT_EQ(
        targets(lines, FileType::Python, Target::Local, {2, 0}),
        (std::vector<Position>{at(lines, 1, "value"), at(lines, 1, "call"),
                               at(lines, 4, "finish")}));
    EXPECT_EQ(targets(lines, FileType::Python, Target::Local, {5, 0}),
              (std::vector<Position>{at(lines, 5, "work")}));
    EXPECT_TRUE(
        targets(lines, FileType::Python, Target::Local, {6, 5}).empty());
}

TEST(StructuralNavigation, JavaScriptMethodsArrowsAndRegexLiterals)
{
    const std::vector<std::string> lines = {
        "function outer(arg) {",
        "  const pattern = /fake() { nope(); }/;",
        "  const text = `fake() { ignored(); }`;",
        "  const nested = (name) => { return internal(name); };",
        "  let value = compute(arg);",
        "  return finish(value);",
        "}",
        "class Service { method() { invoke(); } }"};
    EXPECT_EQ(
        targets(lines, FileType::JavaScript, Target::Function, {}),
        (std::vector<Position>{at(lines, 0, "outer"), at(lines, 3, "nested"),
                               at(lines, 7, "method")}));
    EXPECT_EQ(
        targets(lines, FileType::JavaScript, Target::Local, {0, 0}),
        (std::vector<Position>{at(lines, 1, "pattern"), at(lines, 2, "text"),
                               at(lines, 4, "value"), at(lines, 4, "compute"),
                               at(lines, 5, "finish")}));
}

TEST(StructuralNavigation, ConstructorInitializerListAndLambdaScopes)
{
    const std::vector<std::string> lines = {
        "Thing::Thing(int input) : first(input), second{input} {",
        "    auto callback = [input]() { hidden(input); };", "    run(input);",
        "}", "void next() { finish(); }"};
    EXPECT_EQ(targets(lines, FileType::Cpp, Target::Function, {}),
              (std::vector<Position>{Position{0, 7}, at(lines, 1, "["),
                                     at(lines, 4, "next")}));
    EXPECT_EQ(
        targets(lines, FileType::Cpp, Target::Local, {2, 0}),
        (std::vector<Position>{at(lines, 1, "callback"), at(lines, 2, "run")}));
    EXPECT_EQ(
        targets(lines, FileType::Cpp, Target::Local, at(lines, 1, "hidden")),
        (std::vector<Position>{at(lines, 1, "hidden")}));
}

TEST(StructuralNavigation, CSharpAndMlangDefinitions)
{
    const std::vector<std::string> csharp = {
        "public class Service {",
        "  public async Task<int> Run(int input)",
        "  {",
        "    var value = Fetch(input);",
        "    return value;",
        "  }",
        "}"};
    EXPECT_EQ(targets(csharp, FileType::CSharp, Target::Function, {}),
              (std::vector<Position>{at(csharp, 1, "Run")}));
    EXPECT_EQ(targets(csharp, FileType::CSharp, Target::Local, {3, 0}),
              (std::vector<Position>{at(csharp, 3, "value"),
                                     at(csharp, 3, "Fetch")}));
    const std::vector<std::string> mla = {"fn run(input: i32) -> i32 {",
                                          "  let value = compute(input);", "}"};
    EXPECT_EQ(targets(mla, FileType::Mla, Target::Function, {}),
              (std::vector<Position>{at(mla, 0, "run")}));
    EXPECT_EQ(
        targets(mla, FileType::Mla, Target::Local, {1, 0}),
        (std::vector<Position>{at(mla, 1, "value"), at(mla, 1, "compute")}));
}

TEST(StructuralNavigation, UnfinishedFunctionAndUnsupportedFiles)
{
    const std::vector<std::string> lines = {"void work() {",
                                            "  int value = call();"};
    EXPECT_EQ(
        targets(lines, FileType::Cpp, Target::Local, {1, 0}),
        (std::vector<Position>{at(lines, 1, "value"), at(lines, 1, "call")}));
    EXPECT_TRUE(
        targets(lines, FileType::MarkupText, Target::Function, {}).empty());
    EXPECT_TRUE(targets({}, FileType::Cpp, Target::Function, {}).empty());
}

TEST(StructuralNavigation, PythonAssignmentsLoopsAndKeywordArguments)
{
    const std::vector<std::string> lines = {
        "def work():",
        "    first, second = pair()",
        "    for item in values:",
        "        result = invoke(option=item)",
        "    with resource() as handle:",
        "        consume(handle)"};
    EXPECT_EQ(
        targets(lines, FileType::Python, Target::Local, {0, 0}),
        (std::vector<Position>{at(lines, 1, "first"), at(lines, 1, "second"),
                               at(lines, 1, "pair"), at(lines, 2, "item"),
                               at(lines, 3, "result"), at(lines, 3, "invoke"),
                               at(lines, 4, "resource"), at(lines, 4, "handle"),
                               at(lines, 5, "consume")}));
}

TEST(StructuralNavigation, GenericSignaturesAndCalls)
{
    const std::vector<std::string> lines = {
        "function identity<T>(value: T): T {",
        "    const result = convert<T>(value);", "    return result;", "}"};
    EXPECT_EQ(targets(lines, FileType::TypeScript, Target::Function, {}),
              (std::vector<Position>{at(lines, 0, "identity")}));
    EXPECT_EQ(targets(lines, FileType::TypeScript, Target::Local, {0, 0}),
              (std::vector<Position>{at(lines, 1, "result"),
                                     at(lines, 1, "convert")}));
}
