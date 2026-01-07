#include "lsp_client.h"

#ifdef UVIM_ENABLE_CLANGD_LSP

#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

using nlohmann::json;

static std::string readFileAll(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if(!f)
        return {};
    std::string s((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
    return s;
}

static std::string absPath(const std::string& p)
{
    char buf[PATH_MAX];
    if(realpath(p.c_str(), buf))
        return std::string(buf);
    // If it doesn't exist yet, fall back to cwd + p
    if(!p.empty() && p[0] == '/')
        return p;
    char cwd[PATH_MAX];
    if(getcwd(cwd, sizeof(cwd)))
        return std::string(cwd) + "/" + p;
    return p;
}

static std::string pathToFileUri(const std::string& path)
{
    // Minimal file:// URI escaping (spaces only).
    std::string p = absPath(path);
    std::string out = "file://";
    for(char c : p)
    {
        if(c == ' ')
            out += "%20";
        else
            out.push_back(c);
    }
    return out;
}

static std::string uriToPath(const std::string& uri)
{
    // Expect file://
    const std::string prefix = "file://";
    if(uri.rfind(prefix, 0) == 0)
    {
        std::string p = uri.substr(prefix.size());
        // Decode percent-encoded bytes (e.g. %20, %2B).
        std::string out;
        out.reserve(p.size());
        auto hex = [](char c) -> int
        {
            if(c >= '0' && c <= '9')
                return c - '0';
            if(c >= 'a' && c <= 'f')
                return 10 + (c - 'a');
            if(c >= 'A' && c <= 'F')
                return 10 + (c - 'A');
            return -1;
        };

        for(size_t i = 0; i < p.size(); ++i)
        {
            if(p[i] == '%' && i + 2 < p.size())
            {
                int hi = hex(p[i + 1]);
                int lo = hex(p[i + 2]);
                if(hi >= 0 && lo >= 0)
                {
                    out.push_back(static_cast<char>((hi << 4) | lo));
                    i += 2;
                    continue;
                }
            }
            out.push_back(p[i]);
        }
        return out;
    }
    return uri;
}

// Convert a UTF-8 byte offset in a line to UTF-16 code units (LSP uses UTF-16).
static int utf8ByteOffsetToUtf16(const std::string& line, int byteOffset)
{
    if(byteOffset <= 0)
        return 0;
    if(byteOffset > (int)line.size())
        byteOffset = (int)line.size();

    int u16 = 0;
    int i = 0;
    while(i < byteOffset)
    {
        unsigned char c = (unsigned char)line[i];
        int codepoint = 0;
        int len = 1;

        if(c < 0x80)
        {
            codepoint = c;
            len = 1;
        }
        else if((c & 0xE0) == 0xC0 && i + 1 < (int)line.size())
        {
            codepoint = ((c & 0x1F) << 6) | ((unsigned char)line[i + 1] & 0x3F);
            len = 2;
        }
        else if((c & 0xF0) == 0xE0 && i + 2 < (int)line.size())
        {
            codepoint = ((c & 0x0F) << 12) |
                        (((unsigned char)line[i + 1] & 0x3F) << 6) |
                        ((unsigned char)line[i + 2] & 0x3F);
            len = 3;
        }
        else if((c & 0xF8) == 0xF0 && i + 3 < (int)line.size())
        {
            codepoint = ((c & 0x07) << 18) |
                        (((unsigned char)line[i + 1] & 0x3F) << 12) |
                        (((unsigned char)line[i + 2] & 0x3F) << 6) |
                        ((unsigned char)line[i + 3] & 0x3F);
            len = 4;
        }
        else
        {
            // invalid; treat as single byte
            codepoint = c;
            len = 1;
        }

        if(i + len > byteOffset)
            break;

        // UTF-16 units
        if(codepoint <= 0xFFFF)
            u16 += 1;
        else
            u16 += 2;

        i += len;
    }
    return u16;
}

struct LspClient::Impl
{
    int inFd = -1;  // write to clangd stdin
    int outFd = -1; // read from clangd stdout
    pid_t pid = -1;

    std::thread reader;
    std::atomic<bool> alive{false};

    std::mutex m;
    std::condition_variable cv;

    int nextId = 1;
    std::unordered_map<int, json> responses;

    std::string rootDir;
    std::unordered_map<std::string, int> docVersion;

    bool sendRaw(const std::string& payload)
    {
        if(inFd < 0)
            return false;
        std::string hdr =
            "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n";
        std::string msg = hdr + payload;

        const char* p = msg.data();
        ssize_t left = (ssize_t)msg.size();
        while(left > 0)
        {
            ssize_t n = ::write(inFd, p, left);
            if(n < 0)
            {
                if(errno == EINTR)
                    continue;
                return false;
            }
            left -= n;
            p += n;
        }
        return true;
    }

    int sendRequest(const std::string& method, const json& params)
    {
        int id;
        {
            std::lock_guard<std::mutex> lk(m);
            id = nextId++;
        }

        json req;
        req["jsonrpc"] = "2.0";
        req["id"] = id;
        req["method"] = method;
        req["params"] = params;

        sendRaw(req.dump());
        return id;
    }

    void sendNotification(const std::string& method, const json& params)
    {
        json n;
        n["jsonrpc"] = "2.0";
        n["method"] = method;
        n["params"] = params;
        sendRaw(n.dump());
    }

    std::optional<json> waitResponse(int id, int timeoutMs = 3000)
    {
        std::unique_lock<std::mutex> lk(m);
        if(!cv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                        [&]
                        {
                            return responses.find(id) != responses.end() ||
                                   !alive.load();
                        }))
        {
            return std::nullopt;
        }
        auto it = responses.find(id);
        if(it == responses.end())
            return std::nullopt;
        json resp = it->second;
        responses.erase(it);
        return resp;
    }

    void readerLoop()
    {
        std::string buf;
        buf.reserve(64 * 1024);

        char tmp[4096];
        while(alive.load())
        {
            ssize_t n = ::read(outFd, tmp, sizeof(tmp));
            if(n <= 0)
            {
                break;
            }
            buf.append(tmp, tmp + n);

            while(true)
            {
                // parse header
                size_t hdrEnd = buf.find("\r\n\r\n");
                if(hdrEnd == std::string::npos)
                    break;

                std::string header = buf.substr(0, hdrEnd);
                size_t contentLen = 0;

                // find Content-Length
                {
                    const std::string key = "Content-Length:";
                    size_t p = header.find(key);
                    if(p != std::string::npos)
                    {
                        p += key.size();
                        while(p < header.size() && header[p] == ' ')
                            ++p;
                        size_t q = p;
                        while(q < header.size() &&
                              std::isdigit((unsigned char)header[q]))
                            ++q;
                        if(q > p)
                            contentLen =
                                (size_t)std::stoul(header.substr(p, q - p));
                    }
                }

                size_t payloadStart = hdrEnd + 4;
                if(buf.size() < payloadStart + contentLen)
                    break;

                std::string payload = buf.substr(payloadStart, contentLen);
                buf.erase(0, payloadStart + contentLen);

                json msg;
                try
                {
                    msg = json::parse(payload);
                }
                catch(...)
                {
                    continue;
                }

                if(msg.contains("id"))
                {
                    int id = -1;
                    try
                    {
                        id = msg["id"].get<int>();
                    }
                    catch(...)
                    {
                        // ignore
                    }
                    if(id != -1)
                    {
                        std::lock_guard<std::mutex> lk(m);
                        responses[id] = msg;
                        cv.notify_all();
                    }
                }
                // notifications ignored for now
            }
        }

        alive.store(false);
        cv.notify_all();
    }

    bool startClangd(const std::string& clangdPath,
                     const std::vector<std::string>& extraArgs)
    {
        int inPipe[2];
        int outPipe[2];
        if(pipe(inPipe) != 0)
            return false;
        if(pipe(outPipe) != 0)
            return false;

        pid = fork();
        if(pid == 0)
        {
            // child
            dup2(inPipe[0], STDIN_FILENO);
            dup2(outPipe[1], STDOUT_FILENO);
            dup2(outPipe[1], STDERR_FILENO); // send logs to stdout for now

            close(inPipe[0]);
            close(inPipe[1]);
            close(outPipe[0]);
            close(outPipe[1]);

            std::vector<char*> argv;
            argv.push_back(const_cast<char*>(clangdPath.c_str()));
            for(const auto& a : extraArgs)
                argv.push_back(const_cast<char*>(a.c_str()));
            argv.push_back(nullptr);

            execvp(argv[0], argv.data());
            _exit(127);
        }

        // parent
        close(inPipe[0]);
        close(outPipe[1]);
        inFd = inPipe[1];
        outFd = outPipe[0];

        // non-blocking read is fine but not required
        alive.store(true);
        reader = std::thread([this] { readerLoop(); });

        return true;
    }

    bool initialize(const std::string& rootDir)
    {
        json params;
        params["processId"] = (int)getpid();
        params["rootUri"] = pathToFileUri(rootDir);
        params["rootPath"] = absPath(rootDir);
        params["workspaceFolders"] = json::array({json{
            {"uri", pathToFileUri(rootDir)},
            {"name", std::filesystem::path(rootDir).filename().string()}}});
        params["capabilities"] = json::object(); // minimal

        int id = sendRequest("initialize", params);
        auto resp = waitResponse(id, 5000);
        if(!resp)
            return false;

        // Send initialized notification
        sendNotification("initialized", json::object());
        return true;
    }
};

LspClient::LspClient() : impl(new Impl) {}
LspClient::~LspClient()
{
    stop();
    delete impl;
}

bool LspClient::start(const std::string& clangdPath, const std::string& rootDir,
                      const std::string& compileCommandsDir,
                      const std::string& queryDriverAllowList)
{
    if(running())
        return true;

    impl->rootDir = absPath(rootDir);

    std::vector<std::string> args;
    // log can be tuned; keep quiet by default
    // args.push_back("--log=verbose");

    if(!compileCommandsDir.empty())
    {
        args.push_back("--compile-commands-dir=" + absPath(compileCommandsDir));
    }

    if(!queryDriverAllowList.empty())
    {
        args.push_back("--query-driver=" + queryDriverAllowList);
    }

    if(!impl->startClangd(clangdPath, args))
        return false;

    if(!impl->initialize(impl->rootDir))
    {
        stop();
        return false;
    }

    return true;
}

bool LspClient::startServer(const std::string& serverPath,
                            const std::string& rootDir,
                            const std::vector<std::string>& args)
{
    if(running())
        return true;

    impl->rootDir = absPath(rootDir);

    if(!impl->startClangd(serverPath, args))
        return false;

    if(!impl->initialize(impl->rootDir))
    {
        stop();
        return false;
    }

    return true;
}

void LspClient::stop()
{
    if(!impl)
        return;

    if(impl->alive.load())
    {
        // best-effort shutdown
        try
        {
            int id = impl->sendRequest("shutdown", json::object());
            (void)impl->waitResponse(id, 1000);
            impl->sendNotification("exit", json::object());
        }
        catch(...)
        {
        }
    }

    impl->alive.store(false);
    if(impl->inFd >= 0)
        close(impl->inFd);
    if(impl->outFd >= 0)
        close(impl->outFd);

    if(impl->reader.joinable())
        impl->reader.join();

    if(impl->pid > 0)
    {
        int status = 0;
        waitpid(impl->pid, &status, 0);
    }

    impl->inFd = impl->outFd = -1;
    impl->pid = -1;
    impl->responses.clear();
    impl->docVersion.clear();
}

bool LspClient::running() const
{
    return impl && impl->alive.load();
}

void LspClient::didOpen(const std::string& filePath,
                        const std::string& languageId, const std::string& text)
{
    if(!running())
        return;

    std::string abs = absPath(filePath);

    int ver = 1;
    impl->docVersion[abs] = ver;

    json params;
    params["textDocument"] = {
        {"uri", pathToFileUri(abs)},
        {"languageId", languageId},
        {"version", ver},
        {"text", text},
    };
    impl->sendNotification("textDocument/didOpen", params);
}

void LspClient::didChange(const std::string& filePath, const std::string& text)
{
    didChange(filePath, text, "cpp");
}

void LspClient::didChange(const std::string& filePath, const std::string& text,
                          const std::string& languageId)
{
    if(!running())
        return;

    std::string abs = absPath(filePath);

    // If the server hasn't seen this document yet, didOpen first.
    auto it0 = impl->docVersion.find(abs);
    if(it0 == impl->docVersion.end())
    {
        didOpen(abs, languageId, text);
        return;
    }

    int ver = ++it0->second;

    json params;
    params["textDocument"] = {{"uri", pathToFileUri(abs)}, {"version", ver}};
    // Full-text sync
    params["contentChanges"] = json::array({json{{"text", text}}});
    impl->sendNotification("textDocument/didChange", params);
}

void LspClient::didSave(const std::string& filePath)
{
    if(!running())
        return;

    std::string abs = absPath(filePath);
    json params;
    params["textDocument"] = {{"uri", pathToFileUri(abs)}};
    impl->sendNotification("textDocument/didSave", params);
}

static std::optional<LspClient::Location>
parseDefinitionResult(const json& result)
{
    // clangd may return Location | Location[] | LocationLink[]
    if(result.is_null())
        return std::nullopt;

    auto parseLocationObj =
        [](const json& loc) -> std::optional<LspClient::Location>
    {
        if(!loc.is_object())
            return std::nullopt;

        std::string uri;
        json range;

        if(loc.contains("uri"))
        {
            uri = loc.value("uri", "");
            range = loc.value("range", json::object());
        }
        else if(loc.contains("targetUri"))
        {
            // LocationLink
            uri = loc.value("targetUri", "");
            range = loc.value("targetRange", json::object());
        }
        else
        {
            return std::nullopt;
        }

        if(uri.empty() || !range.is_object())
            return std::nullopt;

        json start = range.value("start", json::object());
        int line = start.value("line", 0);
        int ch = start.value("character", 0);

        LspClient::Location out;
        out.path = uriToPath(uri);
        out.line = line;
        out.character = ch;
        return out;
    };

    if(result.is_array() && !result.empty())
        return parseLocationObj(result[0]);
    if(result.is_object())
        return parseLocationObj(result);
    return std::nullopt;
}

std::optional<LspClient::Location>
LspClient::definition(const std::string& filePath, int line,
                      int characterUtf8ByteOffset)
{
    if(!running())
        return std::nullopt;

    std::string abs = absPath(filePath);

    // LSP wants UTF-16 character offset; we only have a byte offset.
    // The caller should pass the byte index within the line.
    std::string text = readFileAll(abs);
    // If file isn't on disk (unsaved buffer), the caller should have sent
    // didChange with the buffer text. For safety, use the current on-disk line
    // to convert.
    int utf16ch = characterUtf8ByteOffset;
    if(!text.empty())
    {
        // get the relevant line
        int curLine = 0;
        size_t start = 0;
        for(size_t i = 0; i <= text.size(); ++i)
        {
            if(i == text.size() || text[i] == '\n')
            {
                if(curLine == line)
                {
                    std::string ln = text.substr(start, i - start);
                    utf16ch =
                        utf8ByteOffsetToUtf16(ln, characterUtf8ByteOffset);
                    break;
                }
                curLine++;
                start = i + 1;
            }
        }
    }

    json params;
    params["textDocument"] = {{"uri", pathToFileUri(abs)}};
    params["position"] = {{"line", line}, {"character", utf16ch}};

    int id = impl->sendRequest("textDocument/definition", params);
    auto resp = impl->waitResponse(id, 5000);
    if(!resp || !resp->is_object())
        return std::nullopt;

    if(resp->contains("error"))
        return std::nullopt;

    json result = resp->value("result", json());
    return parseDefinitionResult(result);
}

std::vector<LspClient::CompletionItem>
LspClient::completion(const std::string& filePath, int line,
                      int characterUtf8ByteOffset)
{
    std::vector<CompletionItem> out;
    if(!running())
        return out;

    std::string abs = absPath(filePath);

    // Convert UTF-8 byte offset to UTF-16 code units for LSP.
    int utf16ch = characterUtf8ByteOffset;
    std::string text = readFileAll(abs);
    if(!text.empty())
    {
        int curLine = 0;
        size_t start = 0;
        for(size_t i = 0; i <= text.size(); ++i)
        {
            if(i == text.size() || text[i] == '\n')
            {
                if(curLine == line)
                {
                    std::string ln = text.substr(start, i - start);
                    utf16ch =
                        utf8ByteOffsetToUtf16(ln, characterUtf8ByteOffset);
                    break;
                }
                curLine++;
                start = i + 1;
            }
        }
    }

    json params;
    params["textDocument"] = {{"uri", pathToFileUri(abs)}};
    params["position"] = {{"line", line}, {"character", utf16ch}};
    // Minimal context; clangd is fine without it, but it helps some servers.
    params["context"] = {{"triggerKind", 1}};

    int id = impl->sendRequest("textDocument/completion", params);
    auto resp = impl->waitResponse(id, 5000);
    if(!resp || !resp->is_object())
        return out;
    if(resp->contains("error"))
        return out;

    json result = resp->value("result", json());
    json items;

    if(result.is_array())
    {
        items = result;
    }
    else if(result.is_object())
    {
        if(result.contains("items"))
            items = result["items"];
        else if(result.contains("result"))
            items = result["result"];
    }

    if(!items.is_array())
        return out;

    out.reserve(std::min<size_t>(items.size(), 64));
    for(size_t i = 0; i < items.size() && out.size() < 64; ++i)
    {
        const json& it = items[i];
        if(!it.is_object())
            continue;

        CompletionItem ci;
        ci.label = it.value("label", std::string{});
        ci.insertText = it.value("insertText", std::string{});
        int fmt = it.value("insertTextFormat", 1);
        ci.isSnippet = (fmt == 2);

        // Prefer textEdit.newText when present.
        if(it.contains("textEdit") && it["textEdit"].is_object())
        {
            const json& te = it["textEdit"];
            if(te.contains("newText") && te["newText"].is_string())
            {
                ci.insertText = te["newText"].get<std::string>();
            }
        }

        if(ci.insertText.empty())
            ci.insertText = ci.label;

        if(!ci.label.empty())
            out.push_back(std::move(ci));
    }

    return out;
}

std::vector<LspClient::Location>
LspClient::references(const std::string& filePath, int line,
                      int characterUtf8ByteOffset, bool includeDeclaration)
{
    std::vector<Location> out;
    if(!running())
        return out;

    std::string abs = absPath(filePath);

    // Convert UTF-8 byte offset to UTF-16 code units for LSP
    int utf16ch = characterUtf8ByteOffset;
    std::string text = readFileAll(abs);
    if(!text.empty())
    {
        int curLine = 0;
        size_t start = 0;
        for(size_t i = 0; i <= text.size(); ++i)
        {
            if(i == text.size() || text[i] == '\n')
            {
                if(curLine == line)
                {
                    std::string ln = text.substr(start, i - start);
                    utf16ch =
                        utf8ByteOffsetToUtf16(ln, characterUtf8ByteOffset);
                    break;
                }
                curLine++;
                start = i + 1;
            }
        }
    }

    json params;
    params["textDocument"] = {{"uri", pathToFileUri(abs)}};
    params["position"] = {{"line", line}, {"character", utf16ch}};
    params["context"] = {{"includeDeclaration", includeDeclaration}};

    int id = impl->sendRequest("textDocument/references", params);
    auto resp = impl->waitResponse(id, 10000); // 10s timeout for references
    if(!resp || !resp->is_object())
        return out;

    if(resp->contains("error"))
        return out;

    json result = resp->value("result", json());
    if(!result.is_array())
        return out;

    out.reserve(result.size());
    for(const auto& loc : result)
    {
        if(!loc.is_object())
            continue;

        std::string uri = loc.value("uri", "");
        if(uri.empty())
            continue;

        json range = loc.value("range", json::object());
        if(!range.is_object())
            continue;

        json startPos = range.value("start", json::object());
        int locLine = startPos.value("line", 0);
        int locChar = startPos.value("character", 0);

        Location l;
        l.path = uriToPath(uri);
        l.line = locLine;
        l.character = locChar;
        out.push_back(std::move(l));
    }

    return out;
}

#else

// If UVIM_ENABLE_CLANGD_LSP is not set, compile a stub that always disables.

LspClient::LspClient() : impl(nullptr) {}
LspClient::~LspClient() {}

bool LspClient::start(const std::string&, const std::string&,
                      const std::string&, const std::string&)
{
    return false;
}
bool LspClient::startServer(const std::string&, const std::string&,
                            const std::vector<std::string>&)
{
    return false;
}
void LspClient::stop() {}
bool LspClient::running() const
{
    return false;
}
void LspClient::didOpen(const std::string&, const std::string&,
                        const std::string&)
{
}
void LspClient::didChange(const std::string&, const std::string&) {}
void LspClient::didChange(const std::string&, const std::string&,
                          const std::string&)
{
}
void LspClient::didSave(const std::string&) {}
std::optional<LspClient::Location> LspClient::definition(const std::string&,
                                                         int, int)
{
    return std::nullopt;
}

std::vector<LspClient::CompletionItem> LspClient::completion(const std::string&,
                                                             int, int)
{
    return {};
}

std::vector<LspClient::Location> LspClient::references(const std::string&, int,
                                                       int, bool)
{
    return {};
}

#endif
