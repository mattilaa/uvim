// Portable compiler stand-in for argument, cancellation and error-path tests.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;
    std::vector<std::string> args(argv + 1, argv + argc);
    fs::path output, root;
    bool error = false;
    for(size_t i = 0; i < args.size(); ++i)
    {
        if(args[i] == "-o" && i + 1 < args.size())
            output = args[++i];
        else if(args[i] == "--uvim-test-dir" && i + 1 < args.size())
            root = args[++i];
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
        }
        std::ofstream(root / "started", std::ios::binary) << "started";
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while(fs::exists(root / "hold") && !fs::exists(root / "release") &&
              std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if(error)
    {
        std::cerr << "example.cpp:3: compiler error: invalid source\n";
        return 3;
    }
    std::ofstream assembly(output, std::ios::binary);
    assembly << ".text\nfake_function:\n\tmovq %rdi, %rax\n\taddq $1, "
                "%rax\n\tretq\n";
    return assembly ? 0 : 2;
}
