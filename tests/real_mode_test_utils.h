#pragma once

#include "asm_documentation.h"
#include "color_constant.h"
#include "editor.h"
#include "mode_state_machine.h"
#include "terminal.h"
#include "text_utils.h"
#include "widgets/status_bar.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <memory>
#include <utility>

using namespace editor::statemachine;

namespace uvim_test
{
template <typename InitialState>
ModeStateMachine makeMachine(Editor& editor, InitialState&& initial)
{
    return ModeStateMachine(createModeContext(&editor),
                            std::forward<InitialState>(initial));
}

inline std::filesystem::path make_temp_dir(const std::string& prefix)
{
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path base =
        std::filesystem::temp_directory_path() / (prefix + std::to_string(now));
    std::filesystem::create_directories(base);
    return base;
}

inline void write_file(const std::filesystem::path& path,
                       std::string_view content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

inline std::string read_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

inline void dispatch_command(ModeStateMachine& sm, std::string_view cmd)
{
    sm.dispatch(':');
    for(char c : cmd)
        sm.dispatch(c);
    sm.dispatch(keyCode(control::ControlKey::ENTER));
}

inline void set_buffer_filename(Editor& editor, std::string filename)
{
    editor.currentBuffer->filename = std::move(filename);
    if(editor.filename)
        *editor.filename = editor.currentBuffer->filename;
}

inline void set_env_var(const std::string& name, const std::string& value)
{
#ifdef _WIN32
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

inline void unset_env_var(const std::string& name)
{
#ifdef _WIN32
    _putenv_s(name.c_str(), "");
#else
    unsetenv(name.c_str());
#endif
}

class ScopedCurrentPath
{
public:
    explicit ScopedCurrentPath(const std::filesystem::path& next)
    {
        std::error_code ec;
        previous = std::filesystem::current_path(ec);
        if(!ec)
            std::filesystem::current_path(next, ec);
    }

    ~ScopedCurrentPath()
    {
        std::error_code ec;
        if(!previous.empty())
            std::filesystem::current_path(previous, ec);
    }

private:
    std::filesystem::path previous;
};

class ScopedEnv
{
public:
    ScopedEnv(const char* name, std::string value) : name(name)
    {
        const char* current = std::getenv(name);
        if(current)
        {
            hadValue = true;
            previous = current;
        }
        set_env_var(name, value);
    }

    ~ScopedEnv()
    {
        if(hadValue)
            set_env_var(name, previous);
        else
            unset_env_var(name);
    }

private:
    std::string name;
    bool hadValue = false;
    std::string previous;
};
} // namespace uvim_test
