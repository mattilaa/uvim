// Deterministic stdio server for responsiveness tests. Gate files let tests
// stall initialization or stop reading stdin without depending on clangd.
#include "json_utils.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace fs = std::filesystem;
namespace ju = json_utils;

void gate(const fs::path& root, const char* hold, const char* release)
{
    if(!fs::exists(root / hold))
        return;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while(!fs::exists(root / release) &&
          std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

void respond(int id, const std::string& result)
{
    const std::string body =
        "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
        ",\"result\":" + result + "}";
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n"
              << body << std::flush;
}

int main(int argc, char** argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if(argc > 1 && std::string(argv[1]) == "--drain")
    {
        std::string line;
        while(std::getline(std::cin, line))
        {
        }
        return 0;
    }
    fs::path root;
    std::string header;
    while(std::getline(std::cin, header))
    {
        size_t length = 0;
        do
        {
            if(header.compare(0, 15, "Content-Length:") == 0)
                length = std::stoull(header.substr(15));
        } while(header != "\r" && !header.empty() &&
                std::getline(std::cin, header));
        std::string body(length, '\0');
        if(!std::cin.read(body.data(), static_cast<std::streamsize>(length)))
            break;
        ju::Document message;
        if(!ju::parse(message, body))
            return 1;
        const std::string method = ju::get_string(message, "method");
        const auto* params = ju::find(message, "params");
        if(method == "initialize")
        {
            root = ju::get_string(*params, "rootPath");
            std::ofstream(root / "initialize.received") << "received";
            gate(root, "hold-initialize", "initialize.release");
            respond(
                ju::get_int(message, "id"),
                "{\"capabilities\":{},\"serverInfo\":{\"name\":\"test-lsp\"}}");
        }
        else if(method == "initialized")
        {
            std::ofstream(root / "initialized.received") << "received";
            gate(root, "hold-input", "input.release");
        }
        else if(method == "exit")
            return 0;
        else if(method == "textDocument/didOpen" ||
                method == "textDocument/didChange")
        {
            std::ofstream(root / "messages", std::ios::app) << method << '\n';
            if(method == "textDocument/didChange")
            {
                const auto* changes = ju::find(*params, "contentChanges");
                if(changes && changes->IsArray() && changes->Size() > 0)
                    std::ofstream(root / "latest-text")
                        << ju::get_string((*changes)[0], "text");
            }
        }
        else if(const auto* id = ju::find(message, "id"); id && id->IsInt())
            respond(id->GetInt(), "null");
    }
}
