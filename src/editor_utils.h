#pragma once

/// @file editor_utils.h
/// @brief Editor helper utilities for parsing, navigation, and path/LOC
/// helpers.

#include "file_entry.h"
#include "json_utils.h"
#include "token_type.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class GitIgnore;

namespace editor::helper
{

/// @brief Parses a token type name and returns the corresponding enum value.
/// @param value Token type name (case-insensitive).
/// @param fallback Value returned when parsing fails.
/// @return Parsed token type or @p fallback.
TokenType parse_token_type(std::string_view value, TokenType fallback);

/// @brief Computes a stable hash for a sequence of lines.
/// @param src Input lines.
/// @return Hash value for the full line sequence.
size_t hash_lines(const std::vector<std::string>& src);

/// @brief Returns the canonical lowercase name for a token type.
/// @param value Token type enum value.
/// @return Token type name.
std::string_view token_type_name(TokenType value);

/// @brief Returns a view trimmed of leading and trailing whitespace.
/// @param value Input text view.
/// @return Trimmed text view.
std::string_view trim_view(std::string_view value);

/// @brief Parses an integer from text.
/// @param value Input text.
/// @param out Parsed integer on success.
/// @return True when parsing succeeds.
bool parse_int(std::string_view value, int& out);

/// @brief Converts text to lowercase using ASCII rules.
/// @param value Input text.
/// @return Lowercased copy of @p value.
std::string ascii_lower(std::string_view value);

/// @brief Splits a comma-separated list and trims each item.
/// @param input CSV-like input text.
/// @return Non-empty, trimmed parts.
std::vector<std::string> split_csv(std::string_view input);

/// @brief Trims ASCII whitespace and returns an owning string.
/// @param s Input text.
/// @return Trimmed string.
std::string trim_ascii_ws(std::string_view s);

/// @brief Returns the first Robot Framework table cell from a line.
/// @param line Robot Framework line.
/// @return First cell view.
std::string_view robot_first_cell(std::string_view line);

/// @brief Checks whether a line is the Robot Framework keywords section header.
/// @param line Robot Framework line.
/// @return True if the line marks the keywords section.
bool robot_keyword_section(std::string_view line);

/// @brief Checks whether a line is any recognized Robot Framework section
/// header.
/// @param line Robot Framework line.
/// @return True if the line is a recognized section header.
bool robot_section_header(std::string_view line);

/// @brief Checks whether a line defines a Robot Framework keyword.
/// @param line Robot Framework line.
/// @return True if the line appears to define a keyword.
bool robot_is_keyword_def(std::string_view line);

/// @brief Checks whether a line defines a Python function with the given
/// symbol.
/// @param line Python source line.
/// @param symbol Function name to match.
/// @return True if the line defines @p symbol.
bool python_def_line(std::string_view line, std::string_view symbol);

/// @brief Finds a whole-word occurrence in a line.
/// @param line Source line.
/// @param word Word to search for.
/// @return Zero-based column, or -1 if not found.
int find_word_pos(std::string_view line, std::string_view word);

/// @brief Checks whether a JS/TS declaration line defines a symbol.
/// @param line Source line.
/// @param symbol Symbol name to match.
/// @param outX Zero-based match column on success.
/// @return True if a matching declaration is found.
bool js_ts_decl_line(std::string_view line, std::string_view symbol, int& outX);

/// @brief Finds a JS/TS symbol definition in a file.
/// @param path File path.
/// @param symbol Symbol name to find.
/// @param outY Zero-based line index on success.
/// @param outX Zero-based column on success.
/// @return True if the definition is found.
bool find_js_ts_def_in_file(const std::string& path, std::string_view symbol,
                            int& outY, int& outX);

/// @brief Loads and parses a JSON file.
/// @param path JSON file path.
/// @param doc Output parsed JSON document.
/// @return True if the file is loaded and parsed successfully.
bool load_json_file(const std::filesystem::path& path,
                    json_utils::Document& doc);

struct TsConfigPaths
{
    std::filesystem::path dir;
    std::filesystem::path baseUrl;
    std::vector<std::pair<std::string, std::vector<std::string>>> paths;
};

/// @brief Loads tsconfig/jsconfig path mapping information from parent dirs.
/// @param startDir Directory to start upward search from.
/// @param out Output configuration paths.
/// @return True if a config with usable path settings is found.
bool load_tsconfig_paths(const std::filesystem::path& startDir,
                         TsConfigPaths& out);

/// @brief Expands a module specifier using tsconfig path aliases.
/// @param cfg Loaded tsconfig/jsconfig mapping data.
/// @param module Module specifier to expand.
/// @return Candidate mapped module paths.
std::vector<std::string> expand_tsconfig_paths(const TsConfigPaths& cfg,
                                               std::string_view module);

/// @brief Extracts module specifier from an import/export line.
/// @param line JS/TS import or export line.
/// @param outSpec Extracted module specifier on success.
/// @return True if a module specifier is extracted.
bool extract_js_ts_module_specifier(std::string_view line,
                                    std::string_view& outSpec);

/// @brief Collects symbol-to-module mappings from JS/TS imports/exports.
/// @param lines Source lines.
/// @param symbolToModule Output map of imported symbol to module specifier.
void collect_js_ts_imports(
    const std::vector<std::string>& lines,
    std::unordered_map<std::string, std::string>& symbolToModule);

/// @brief Resolves a JS/TS module from a base directory as a relative module.
/// @param baseDir Directory used as resolution base.
/// @param module Module specifier.
/// @return Resolved file path, or empty string if unresolved.
std::string resolve_js_ts_from_dir(const std::filesystem::path& baseDir,
                                   std::string_view module);

/// @brief Resolves a package-style module through node_modules traversal.
/// @param fromFile Importing file path.
/// @param module Package/module specifier.
/// @return Resolved file path, or empty string if unresolved.
std::string resolve_node_module(const std::string& fromFile,
                                std::string_view module);

/// @brief Resolves any JS/TS module (relative, aliased, or node module).
/// @param fromFile Importing file path.
/// @param module Module specifier.
/// @return Resolved file path, or empty string if unresolved.
std::string resolve_js_ts_module(const std::string& fromFile,
                                 std::string_view module);

/// @brief Resolves only relative/absolute JS/TS module specifiers to a file.
/// @param fromFile Importing file path.
/// @param module Module specifier.
/// @return Resolved file path, or empty string if unresolved.
std::string resolve_js_ts_module_path(const std::string& fromFile,
                                      std::string_view module);

/// @brief Parses a TypeScript type annotation head into a type name.
/// @param text Type annotation text.
/// @return Parsed type name, or empty string when unavailable.
std::string parse_ts_type_name(std::string_view text);

/// @brief Infers TypeScript type name for an identifier by scanning upwards.
/// @param lines Source lines.
/// @param ident Identifier name.
/// @param startY Start line (inclusive), scanned backward.
/// @return Inferred type name, or empty string.
std::string find_ts_type_for_identifier(const std::vector<std::string>& lines,
                                        std::string_view ident, int startY);

/// @brief Infers callback parameter type from common array method calls.
/// @param line Current source line.
/// @param param Callback parameter name.
/// @param lines Full source lines.
/// @param lineNo Current line index used for backward inference.
/// @return Inferred type name, or empty string.
std::string infer_ts_type_from_array_method_line(
    std::string_view line, std::string_view param,
    const std::vector<std::string>& lines, int lineNo);

/// @brief Finds a TypeScript type/interface/class definition.
/// @param lines Source lines.
/// @param typeName Type name to find.
/// @param outY Zero-based line index on success.
/// @param outX Zero-based column on success.
/// @return True if the type definition is found.
bool find_ts_type_definition(const std::vector<std::string>& lines,
                             std::string_view typeName, int& outY, int& outX);

/// @brief Finds a member declaration inside a TypeScript type body.
/// @param lines Source lines.
/// @param typeStartY Start line for the enclosing type.
/// @param member Member name to find.
/// @param outY Zero-based line index on success.
/// @param outX Zero-based column on success.
/// @return True if a matching member declaration is found.
bool find_ts_member_in_type(const std::vector<std::string>& lines,
                            int typeStartY, std::string_view member, int& outY,
                            int& outX);

/// @brief Extracts HTML href/src path under the cursor position.
/// @param line HTML line.
/// @param cursorX Zero-based cursor column.
/// @param outPath Extracted path under cursor.
/// @return True if a valid path is found under cursor.
bool html_path_under_cursor(std::string_view line, int cursorX,
                            std::string_view& outPath);

/// @brief Extracts stylesheet href paths from HTML lines.
/// @param lines HTML source lines.
/// @return Stylesheet paths referenced by link tags.
std::vector<std::string>
extract_html_stylesheets(const std::vector<std::string>& lines);

/// @brief Finds a CSS selector occurrence in a file.
/// @param path CSS file path.
/// @param selector Selector text to find.
/// @param outY Zero-based line index on success.
/// @param outX Zero-based column on success.
/// @return True if the selector is found.
bool find_css_selector_in_file(const std::string& path,
                               std::string_view selector, int& outY, int& outX);

/// @brief Extracts CSS @import path under the cursor position.
/// @param line CSS line.
/// @param cursorX Zero-based cursor column.
/// @param outPath Extracted import path.
/// @return True if a valid import path is found under cursor.
bool css_import_path_under_cursor(std::string_view line, int cursorX,
                                  std::string_view& outPath);

/// @brief Finds a Robot Framework keyword definition in a file.
/// @param path File path.
/// @param keyword Keyword name to find.
/// @param outY Zero-based line index on success.
/// @param outX Zero-based column on success.
/// @return True if the keyword definition is found.
bool find_robot_keyword_in_file(const std::string& path,
                                std::string_view keyword, int& outY, int& outX);

/// @brief Finds a Python function definition in a file.
/// @param path File path.
/// @param symbol Function name to find.
/// @param outY Zero-based line index on success.
/// @param outX Zero-based column on success.
/// @return True if the function definition is found.
bool find_python_def_in_file(const std::string& path, std::string_view symbol,
                             int& outY, int& outX);

/// @brief Checks whether a directory name should be skipped during traversal.
/// @param path Directory path.
/// @return True when directory should be skipped.
bool is_skip_dir(const std::filesystem::path& path);

struct LocCommentRules
{
    std::string_view line;
    std::string_view blockStart;
    std::string_view blockEnd;
    bool hasLine = false;
    bool hasBlock = false;
};

/// @brief Heuristically checks whether a file is binary.
/// @param filepath File path.
/// @return True if the file appears to be binary.
bool locIsBinaryFile(const std::string& filepath);

/// @brief Checks whether a file is considered text for LOC counting.
/// @param filepath File path.
/// @return True if the file should be treated as text.
bool locIsTextFile(const std::string& filepath);

/// @brief Returns comment syntax rules used for LOC counting by path.
/// @param path File path or name.
/// @return Comment parsing rules for that file type.
LocCommentRules locCommentRulesForPath(std::string_view path);

/// @brief Counts non-empty, non-comment logical lines in a file.
/// @param filepath File path.
/// @param rules Comment syntax rules.
/// @return LOC count for the file, or 0 on read failure.
int locCountInFile(const std::string& filepath, const LocCommentRules& rules);

/// @brief Counts non-empty, non-comment logical lines in memory lines.
/// @param lines Source lines.
/// @param rules Comment syntax rules.
/// @return LOC count for the provided lines.
int locCountInLines(const std::vector<std::string>& lines,
                    const LocCommentRules& rules);

/// @brief Recursively collects files for LOC traversal.
/// @param dir Root directory.
/// @param depth Current recursion depth.
/// @param gitignore Loaded gitignore matcher.
/// @param out Output file paths.
void collectLocFiles(const std::string& dir, int depth,
                     const GitIgnore& gitignore, std::vector<std::string>& out);

/// @brief Recursively collects filesystem entries for fuzzy-style file lists.
/// @param dir Root directory.
/// @param depth Current recursion depth.
/// @param gitignore Loaded gitignore matcher.
/// @param out Output file entries.
void collectProjectFileEntries(const std::string& dir, int depth,
                               const GitIgnore& gitignore,
                               std::vector<FileEntry>& out);

/// @brief Expands a leading tilde to the current HOME directory.
/// @param path Path to expand.
/// @return Expanded path.
std::string expandTildePath(std::string path);

/// @brief Scores fuzzy match quality between text and pattern.
/// @param text Candidate text.
/// @param pattern Pattern to match.
/// @return Match score, or -1 if pattern does not match.
int fuzzyScore(const std::string& text, const std::string& pattern);

/// @brief Scores fuzzy match quality and records matched character positions.
/// @param needle Pattern to match.
/// @param haystack Candidate text.
/// @param matchPositions Matched character offsets.
/// @return Match score, or -1 if the pattern does not match.
int fuzzyScoreWithPositions(const std::string& needle,
                            const std::string& haystack,
                            std::vector<int>& matchPositions);

/// @brief Returns filesystem path completions for a partial path.
/// @param partial Partial path text.
/// @return Sorted completion candidates.
std::vector<std::string> getPathCompletions(std::string_view partial);

/// @brief Returns direct and recursive path completions for a partial path.
/// @param partial Partial path text.
/// @param respectGitignore Whether to filter using .gitignore rules.
/// @return Ranked completion candidates.
std::vector<std::string> getRecursivePathCompletions(std::string_view partial,
                                                     bool respectGitignore);

/// @brief Returns path completions used by LOC command input.
/// @param partial Partial path text.
/// @param respectGitignore Whether to filter using .gitignore rules.
/// @return Completion candidates.
std::vector<std::string> getLocPathCompletions(std::string_view partial,
                                               bool respectGitignore);

/// @brief Computes the longest common prefix among strings.
/// @param strings Input strings.
/// @return Longest common prefix, or empty string if none.
std::string longestCommonPrefix(const std::vector<std::string>& strings);

} // namespace editor::helper
