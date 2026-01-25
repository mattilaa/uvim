#include <array>
#include <string_view>

namespace constants
{

static constexpr std::array<std::string_view, 9> CPP_FILE_EXTENSIONS = {
    ".cpp", ".cc", ".cxx", ".h",  ".hpp",
    ".hxx", ".c",  ".C",   ".ixx"
    // add/remove as you like (e.g. ".tpp", ".inl", ".hh", ".cu")
};

static constexpr std::array<std::string_view, 1> MLA_FILE_EXTENSIONS = {".mla"};

static constexpr std::array<std::string_view, 3> PYTHON_FILE_EXTENSIONS = {
    ".py",
    ".pyi",
    ".pyw",
};

static constexpr std::array<std::string_view, 6> SHELL_FILE_EXTENSIONS = {
    ".sh",
    ".bash",
    ".zsh",
    ".ksh",
    ".dash",
    ".profile",
};

static constexpr std::array<std::string_view, 8> SHELL_FILE_BASENAMES = {
    ".bashrc",
    ".bash_profile",
    ".bash_logout",
    ".zshrc",
    ".zprofile",
    ".zlogin",
    ".zlogout",
    ".kshrc",
};

static constexpr std::array<std::string_view, 4> SHELL_SHEBANG_HINTS = {
    "bash",
    "zsh",
    "ksh",
    "dash",
};

static constexpr std::array<std::string_view, 3> ROBOT_FILE_EXTENSIONS = {
    ".robot",
    ".resource",
    ".robotframework",
};

static constexpr std::array<std::string_view, 2> JSON_FILE_EXTENSIONS = {
    ".json",
    ".jsonc",
};

static constexpr std::array<std::string_view, 2> YAML_FILE_EXTENSIONS = {
    ".yaml",
    ".yml",
};

static constexpr std::array<std::string_view, 1> TOML_FILE_EXTENSIONS = {
    ".toml",
};

static constexpr std::array<std::string_view, 3> CMAKE_FILE_BASENAMES = {
    "CMakeLists.txt",
    "CMakeFiles.txt",
    "CMakeCache.txt",
};

static constexpr std::array<std::string_view, 2> CMAKE_FILE_SUFFIXES = {
    ".cmake",
    ".cmake.in",
};

} // namespace constants
