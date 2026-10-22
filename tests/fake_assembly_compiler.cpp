// Portable compiler stand-in for argument, cancellation and error-path tests.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;
    std::vector<std::string> args(argv + 1, argv + argc);
    if(std::find(args.begin(), args.end(), "-dumpmachine") != args.end())
    {
        std::string target = "x86_64-test-platform";
        for(size_t i = 0; i < args.size(); ++i)
        {
            if((args[i] == "--target" || args[i] == "-target" ||
                args[i] == "-arch") &&
               i + 1 < args.size())
                target = args[++i];
            else if(args[i].starts_with("--target=") ||
                    args[i].starts_with("-target="))
                target = args[i].substr(args[i].find('=') + 1);
        }
        std::cout << target << '\n';
        return 0;
    }
    fs::path output, root;
    bool error = false;
    bool origins = false;
    for(size_t i = 0; i < args.size(); ++i)
    {
        if(args[i] == "-o" && i + 1 < args.size())
            output = args[++i];
        else if(args[i] == "--uvim-test-dir" && i + 1 < args.size())
            root = args[++i];
        else if(args[i] == "--uvim-test-origins")
            origins = true;
        else if(args[i] == "--uvim-test-error")
            error = true;
    }
    if(!root.empty())
    {
        {
            std::ofstream record(root / "arguments", std::ios::binary);
            for(const auto& arg : args)
                record << arg << '\n';
            std::ifstream source(args.back(), std::ios::binary);
            std::ofstream snapshot(root / "source", std::ios::binary);
            snapshot << source.rdbuf();
            std::ofstream directory(root / "directory", std::ios::binary);
            directory << fs::current_path().string();
        }
        std::ofstream(root / "started", std::ios::binary) << "started";
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while(fs::exists(root / "hold") && !fs::exists(root / "release") &&
              std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(!args.empty() && fs::path(args.back()).extension() == ".mla")
        std::ofstream("mlang_commands.json", std::ios::binary)
            << "temporary compiler index";
    if(error)
    {
        std::cerr << "example.cpp:3: compiler error: invalid source\n";
        return 3;
    }
    std::ofstream assembly(output, std::ios::binary);
    if(origins)
    {
        assembly << ".file 1 " << std::quoted(args.back()) << '\n'
                 << ".file 2 \"helper-library.mla\"\n"
                 << ".text\nlocal_function:\n.loc 1 1 0\n\tmovq %rdi, "
                    "%rax\n\tretq\n.cfi_endproc\n"
                 << "library_helper:\n.loc 2 12 0\n\taddq $1, "
                    "%rax\n\tretq\n.cfi_endproc\n"
                 << "unknown_helper:\n\tnop\n\tretq\n.cfi_endproc\n";
        return assembly ? 0 : 2;
    }
    assembly << ".file 1 " << std::quoted(args.back()) << '\n'
             << ".file 2 \"unrelated-header.h\"\n"
             << ".text\nfake_function:\n.loc 1 1 0\n\tmovq %rdi, %rax\n"
             << ".loc 1 2 0\n\taddq $1, %rax\n"
             << ".loc 1 1 0\n\tnop\n"
             << ".loc 2 1 0\n\tnop\n"
             << ".loc 1 3 0\n\tretq\n";
    return assembly ? 0 : 2;
}
