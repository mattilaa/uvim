#pragma once

#include "token_type.h"
#include "json_utils.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class GitIgnore;

namespace editor::helper
{
TokenType parse_token_type(std::string_view value, TokenType fallback);
size_t hash_lines(const std::vector<std::string>& src);
std::string_view token_type_name(TokenType value);
std::unordered_map<std::string, std::string>
parseYamlMap(const std::string& input);
std::string_view trim_view(std::string_view value);
bool parse_int(std::string_view value, int& out);
std::string ascii_lower(std::string_view value);
std::vector<std::string> split_csv(std::string_view input);
std::string trim_ascii_ws(std::string_view s);

std::string_view robot_first_cell(std::string_view line);
bool robot_keyword_section(std::string_view line);
bool robot_section_header(std::string_view line);
bool robot_is_keyword_def(std::string_view line);
bool python_def_line(std::string_view line, std::string_view symbol);
int find_word_pos(std::string_view line, std::string_view word);
bool js_ts_decl_line(std::string_view line, std::string_view symbol, int& outX);
bool find_js_ts_def_in_file(const std::string& path, std::string_view symbol,
                            int& outY, int& outX);
bool load_json_file(const std::filesystem::path& path, json_utils::Document& doc);

struct TsConfigPaths
{
    std::filesystem::path dir;
    std::filesystem::path baseUrl;
    std::vector<std::pair<std::string, std::vector<std::string>>> paths;
};

bool load_tsconfig_paths(const std::filesystem::path& startDir,
                         TsConfigPaths& out);
std::vector<std::string> expand_tsconfig_paths(const TsConfigPaths& cfg,
                                               std::string_view module);
bool extract_js_ts_module_specifier(std::string_view line,
                                    std::string_view& outSpec);
void collect_js_ts_imports(
    const std::vector<std::string>& lines,
    std::unordered_map<std::string, std::string>& symbolToModule);
std::string resolve_js_ts_from_dir(const std::filesystem::path& baseDir,
                                   std::string_view module);
std::string resolve_node_module(const std::string& fromFile,
                                std::string_view module);
std::string resolve_js_ts_module(const std::string& fromFile,
                                 std::string_view module);
std::string resolve_js_ts_module_path(const std::string& fromFile,
                                      std::string_view module);
std::string parse_ts_type_name(std::string_view text);
std::string find_ts_type_for_identifier(
    const std::vector<std::string>& lines, std::string_view ident, int startY);
std::string infer_ts_type_from_array_method_line(
    std::string_view line, std::string_view param,
    const std::vector<std::string>& lines, int lineNo);
bool find_ts_type_definition(const std::vector<std::string>& lines,
                             std::string_view typeName, int& outY, int& outX);
bool find_ts_member_in_type(const std::vector<std::string>& lines, int typeStartY,
                            std::string_view member, int& outY, int& outX);
bool html_path_under_cursor(std::string_view line, int cursorX,
                            std::string_view& outPath);
std::vector<std::string> extract_html_stylesheets(
    const std::vector<std::string>& lines);
bool find_css_selector_in_file(const std::string& path,
                               std::string_view selector, int& outY, int& outX);
bool css_import_path_under_cursor(std::string_view line, int cursorX,
                                  std::string_view& outPath);
bool find_robot_keyword_in_file(const std::string& path,
                                std::string_view keyword, int& outY, int& outX);
bool find_python_def_in_file(const std::string& path,
                             std::string_view symbol, int& outY, int& outX);
bool is_skip_dir(const std::filesystem::path& path);

struct LocCommentRules
{
    std::string_view line;
    std::string_view blockStart;
    std::string_view blockEnd;
    bool hasLine = false;
    bool hasBlock = false;
};

bool locIsBinaryFile(const std::string& filepath);
bool locIsTextFile(const std::string& filepath);
LocCommentRules locCommentRulesForPath(std::string_view path);
int locCountInFile(const std::string& filepath, const LocCommentRules& rules);
int locCountInLines(const std::vector<std::string>& lines,
                    const LocCommentRules& rules);
void collectLocFiles(const std::string& dir, int depth,
                     const GitIgnore& gitignore,
                     std::vector<std::string>& out);
std::string expandTildePath(std::string path);

int fuzzyScore(const std::string& text, const std::string& pattern);
std::vector<std::string> getPathCompletions(std::string_view partial);
std::vector<std::string> getRecursivePathCompletions(std::string_view partial,
                                                     bool respectGitignore);
std::vector<std::string> getLocPathCompletions(std::string_view partial,
                                               bool respectGitignore);
std::string longestCommonPrefix(const std::vector<std::string>& strings);
} // namespace editor::helper
