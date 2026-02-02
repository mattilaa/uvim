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

#include "text_utils.h"

using nlohmann::json;

#ifdef UVIM_DEBUG_LSP
static void logLspDebug(const std::string& tag, const json& payload)
{
    static std::mutex logMutex;
    std::lock_guard<std::mutex> lk(logMutex);
    std::ofstream out("/tmp/uvim_lsp_codeactions.log", std::ios::app);
    if(!out.is_open())
        return;
    out << "=== " << tag << " ===\n";
    out << payload.dump(2) << "\n";
}
#endif

static std::string readFileAll(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if(!f)
        return {};
    std::string s((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
    return s;
}

static std::vector<std::string> defaultSemanticTokenTypes()
{
    return {
        "namespace",   "type",       "class",      "enum",
        "interface",   "struct",     "typeParameter",
        "parameter",   "variable",   "property",   "enumMember",
        "event",       "function",   "method",     "macro",
        "keyword",     "modifier",   "comment",    "string",
        "number",      "regexp",     "operator",   "decorator"};
}

static std::vector<std::string> defaultSemanticTokenModifiers()
{
    return {"declaration", "definition", "readonly",  "static",
            "deprecated",  "abstract",   "async",     "modification",
            "documentation", "defaultLibrary"};
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
    mutable std::mutex diagMutex;
    std::unordered_map<std::string, std::vector<LspClient::Diagnostic>>
        diagnosticsByFile;
    std::unordered_map<std::string, size_t> diagnosticsRevision;
    mutable std::mutex semanticMutex;
    std::unordered_map<std::string, std::vector<LspClient::SemanticToken>>
        semanticTokensByFile;
    std::unordered_map<std::string, size_t> semanticTokensRevision;
    std::vector<std::string> semanticTokenTypes;
    std::vector<std::string> semanticTokenModifiers;
    std::mutex applyMutex;
    std::vector<json> pendingApplyEdits;

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
                else if(msg.contains("method") && msg["method"].is_string())
                {
                    std::string method = msg["method"].get<std::string>();
                    if(method == "textDocument/publishDiagnostics")
                    {
                        json params = msg.value("params", json::object());
                        std::string uri = params.value("uri", "");
                        if(!uri.empty())
                        {
                            std::string path = absPath(uriToPath(uri));
                            std::vector<LspClient::Diagnostic> diags;
                            if(params.contains("diagnostics") &&
                               params["diagnostics"].is_array())
                            {
                                const json& entries = params["diagnostics"];
                                diags.reserve(entries.size());
                                for(const auto& item : entries)
                                {
                                    if(!item.is_object())
                                        continue;
                                    json range =
                                        item.value("range", json::object());
                                    json start =
                                        range.value("start", json::object());
                                    json end =
                                        range.value("end", json::object());
                                    LspClient::Diagnostic diag;
                                    diag.line = start.value("line", 0);
                                    diag.character =
                                        start.value("character", 0);
                                    diag.endLine = end.value("line", diag.line);
                                    diag.endCharacter =
                                        end.value("character", diag.character);
                                    diag.severity = item.value("severity", 0);
                                    diag.message =
                                        item.value("message", std::string{});
                                    diag.source =
                                        item.value("source", std::string{});
                                    if(item.contains("code"))
                                    {
                                        try
                                        {
                                            diag.codeJson = item["code"].dump();
                                        }
                                        catch(...)
                                        {
                                        }
                                    }
                                    if(item.contains("data"))
                                    {
                                        try
                                        {
                                            diag.dataJson = item["data"].dump();
                                        }
                                        catch(...)
                                        {
                                        }
                                    }
                                    diags.push_back(std::move(diag));
                                }
                            }
                            {
                                std::lock_guard<std::mutex> lk(diagMutex);
                                diagnosticsByFile[path] = std::move(diags);
                                diagnosticsRevision[path]++;
                            }
                        }
                    }
                    else if(method == "workspace/applyEdit")
                    {
                        json params = msg.value("params", json::object());
                        if(params.contains("edit"))
                        {
                            std::lock_guard<std::mutex> lk(applyMutex);
                            pendingApplyEdits.push_back(
                                params.value("edit", json::object()));
                        }
                        if(msg.contains("id"))
                        {
                            json resp;
                            resp["jsonrpc"] = "2.0";
                            resp["id"] = msg["id"];
                            resp["result"] = {{"applied", true}};
                            sendRaw(resp.dump());
                        }
                    }
                }
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
        if(semanticTokenTypes.empty())
            semanticTokenTypes = defaultSemanticTokenTypes();
        if(semanticTokenModifiers.empty())
            semanticTokenModifiers = defaultSemanticTokenModifiers();
        json semCaps;
        semCaps["dynamicRegistration"] = false;
        semCaps["requests"] = {{"range", false}, {"full", true}};
        semCaps["tokenTypes"] = semanticTokenTypes;
        semCaps["tokenModifiers"] = semanticTokenModifiers;
        semCaps["formats"] = json::array({"relative"});

        params["capabilities"] = {
            {"textDocument", {{"semanticTokens", semCaps}}}};

        int id = sendRequest("initialize", params);
        auto resp = waitResponse(id, 5000);
        if(!resp)
            return false;
        if(resp->is_object())
        {
            json result = resp->value("result", json::object());
            json caps = result.value("capabilities", json::object());
            json sem = caps.value("semanticTokensProvider", json::object());
            json legend = sem.value("legend", json::object());
            if(legend.contains("tokenTypes") &&
               legend["tokenTypes"].is_array())
            {
                std::vector<std::string> types;
                for(const auto& item : legend["tokenTypes"])
                {
                    if(item.is_string())
                        types.push_back(item.get<std::string>());
                }
                if(!types.empty())
                    semanticTokenTypes = std::move(types);
            }
            if(legend.contains("tokenModifiers") &&
               legend["tokenModifiers"].is_array())
            {
                std::vector<std::string> mods;
                for(const auto& item : legend["tokenModifiers"])
                {
                    if(item.is_string())
                        mods.push_back(item.get<std::string>());
                }
                if(!mods.empty())
                    semanticTokenModifiers = std::move(mods);
            }
        }

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
                    utf16ch = text_utils::utf8ByteOffsetToUtf16(
                        ln, characterUtf8ByteOffset);
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
                      int characterUtf8ByteOffset, std::string_view lineText,
                      int triggerKind, char triggerCharacter)
{
    std::vector<CompletionItem> out;
    if(!running())
        return out;

    std::string abs = absPath(filePath);

    // Convert UTF-8 byte offset to UTF-16 code units for LSP.
    int utf16ch = characterUtf8ByteOffset;
    if(!lineText.empty())
    {
        utf16ch = text_utils::utf8ByteOffsetToUtf16(std::string(lineText),
                                                    characterUtf8ByteOffset);
    }
    else
    {
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
                        utf16ch = text_utils::utf8ByteOffsetToUtf16(
                            ln, characterUtf8ByteOffset);
                        break;
                    }
                    curLine++;
                    start = i + 1;
                }
            }
        }
    }

    json params;
    params["textDocument"] = {{"uri", pathToFileUri(abs)}};
    params["position"] = {{"line", line}, {"character", utf16ch}};
    // Minimal context; clangd is fine without it, but it helps some servers.
    params["context"] = {{"triggerKind", triggerKind}};
    if(triggerCharacter != '\0')
    {
        std::string tc(1, triggerCharacter);
        params["context"]["triggerCharacter"] = tc;
    }

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
        ci.kind = it.value("kind", 0);
        ci.detail = it.value("detail", std::string{});
        if(it.contains("labelDetails") && it["labelDetails"].is_object())
        {
            const json& ld = it["labelDetails"];
            if(ld.contains("detail") && ld["detail"].is_string())
                ci.labelDetail = ld["detail"].get<std::string>();
            if(ld.contains("description") && ld["description"].is_string())
                ci.labelDescription = ld["description"].get<std::string>();
        }
        if(it.contains("documentation"))
        {
            const json& doc = it["documentation"];
            if(doc.is_string())
            {
                ci.documentation = doc.get<std::string>();
            }
            else if(doc.is_object())
            {
                if(doc.contains("value") && doc["value"].is_string())
                    ci.documentation = doc["value"].get<std::string>();
            }
        }

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
                    utf16ch = text_utils::utf8ByteOffsetToUtf16(
                        ln, characterUtf8ByteOffset);
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

std::vector<LspClient::Diagnostic>
LspClient::diagnostics(const std::string& filePath) const
{
    if(!impl)
        return {};
    std::string abs = absPath(filePath);
    std::lock_guard<std::mutex> lk(impl->diagMutex);
    auto it = impl->diagnosticsByFile.find(abs);
    if(it == impl->diagnosticsByFile.end())
        return {};
    return it->second;
}

void LspClient::clearDiagnostics(const std::string& filePath)
{
    if(!impl)
        return;
    std::string abs = absPath(filePath);
    std::lock_guard<std::mutex> lk(impl->diagMutex);
    impl->diagnosticsByFile.erase(abs);
}

size_t LspClient::diagnosticsRevision(const std::string& filePath) const
{
    if(!impl)
        return 0;
    std::string abs = absPath(filePath);
    std::lock_guard<std::mutex> lk(impl->diagMutex);
    auto it = impl->diagnosticsRevision.find(abs);
    if(it == impl->diagnosticsRevision.end())
        return 0;
    return it->second;
}

static std::vector<LspClient::TextEdit>
parseTextEditsForUri(const json& edits, const std::string& targetPath)
{
    std::vector<LspClient::TextEdit> out;
    if(!edits.is_array())
        return out;

    for(const auto& edit : edits)
    {
        if(!edit.is_object())
            continue;
        json range = edit.value("range", json::object());
        json start = range.value("start", json::object());
        json end = range.value("end", json::object());
        LspClient::TextEdit te;
        te.startLine = start.value("line", 0);
        te.startCharacter = start.value("character", 0);
        te.endLine = end.value("line", te.startLine);
        te.endCharacter = end.value("character", te.startCharacter);
        te.newText = edit.value("newText", std::string{});
        out.push_back(std::move(te));
    }

    return out;
}

static void parseWorkspaceEditInto(const json& editObj,
                                   const std::string& filePath,
                                   std::vector<LspClient::TextEdit>& out)
{
    if(!editObj.is_object())
        return;
    if(editObj.contains("changes") && editObj["changes"].is_object())
    {
        const json& changes = editObj["changes"];
        for(auto it = changes.begin(); it != changes.end(); ++it)
        {
            std::string path = uriToPath(it.key());
            if(absPath(path) != absPath(filePath))
                continue;
            std::vector<LspClient::TextEdit> edits =
                parseTextEditsForUri(it.value(), path);
            out.insert(out.end(), edits.begin(), edits.end());
        }
        return;
    }
    if(editObj.contains("documentChanges") &&
       editObj["documentChanges"].is_array())
    {
        for(const auto& change : editObj["documentChanges"])
        {
            if(!change.is_object())
                continue;
            json textDoc = change.value("textDocument", json::object());
            std::string uri = textDoc.value("uri", std::string{});
            if(uri.empty())
                continue;
            std::string path = uriToPath(uri);
            if(absPath(path) != absPath(filePath))
                continue;
            std::vector<LspClient::TextEdit> edits = parseTextEditsForUri(
                change.value("edits", json::array()), path);
            out.insert(out.end(), edits.begin(), edits.end());
        }
    }
}

static void fillCodeActionFromJson(const json& item,
                                   const std::string& filePath,
                                   LspClient::CodeAction& action)
{
    if(!item.is_object())
        return;
    if(action.title.empty())
        action.title = item.value("title", std::string{});

    if(item.contains("edit"))
    {
        parseWorkspaceEditInto(item.value("edit", json::object()), filePath,
                               action.edits);
    }

    if(item.contains("command") && item["command"].is_string())
    {
        if(action.command.empty())
            action.command = item.value("command", std::string{});
        json args = item.value("arguments", json::array());
        if(args.is_array())
        {
            for(const auto& arg : args)
            {
                try
                {
                    action.commandArgsJson.push_back(arg.dump());
                }
                catch(...)
                {
                }
                if(!arg.is_object())
                    continue;
                if(arg.contains("workspaceEdit"))
                {
                    parseWorkspaceEditInto(
                        arg.value("workspaceEdit", json::object()), filePath,
                        action.edits);
                }
                else
                {
                    parseWorkspaceEditInto(arg, filePath, action.edits);
                }
            }
        }
    }

    if(item.contains("command") && item["command"].is_object())
    {
        json cmd = item["command"];
        if(action.command.empty())
            action.command = cmd.value("command", std::string{});
        json args = cmd.value("arguments", json::array());
        if(args.is_array())
        {
            for(const auto& arg : args)
            {
                try
                {
                    action.commandArgsJson.push_back(arg.dump());
                }
                catch(...)
                {
                }
                if(!arg.is_object())
                    continue;
                if(arg.contains("workspaceEdit"))
                {
                    parseWorkspaceEditInto(
                        arg.value("workspaceEdit", json::object()), filePath,
                        action.edits);
                }
                else
                {
                    parseWorkspaceEditInto(arg, filePath, action.edits);
                }
            }
        }
    }
}

std::vector<LspClient::CodeAction>
LspClient::codeActions(const std::string& filePath, int line,
                       std::string_view lineText,
                       const std::vector<Diagnostic>& diagnostics)
{
    std::vector<CodeAction> out;
    if(!running())
        return out;

    std::string abs = absPath(filePath);

    int endCharUtf16 = text_utils::utf8ByteOffsetToUtf16(std::string(lineText),
                                                         (int)lineText.size());

    json diagArray = json::array();
    for(const auto& d : diagnostics)
    {
        json range;
        range["start"] = {{"line", d.line}, {"character", d.character}};
        range["end"] = {{"line", d.endLine}, {"character", d.endCharacter}};
        json jd;
        jd["range"] = range;
        if(d.severity > 0)
            jd["severity"] = d.severity;
        if(!d.message.empty())
            jd["message"] = d.message;
        if(!d.source.empty())
            jd["source"] = d.source;
        if(!d.codeJson.empty())
        {
            try
            {
                jd["code"] = json::parse(d.codeJson);
            }
            catch(...)
            {
            }
        }
        if(!d.dataJson.empty())
        {
            try
            {
                jd["data"] = json::parse(d.dataJson);
            }
            catch(...)
            {
            }
        }
        diagArray.push_back(jd);
    }

    json params;
    params["textDocument"] = {{"uri", pathToFileUri(abs)}};
    params["range"] = {{"start", {{"line", line}, {"character", 0}}},
                       {"end", {{"line", line}, {"character", endCharUtf16}}}};
    params["context"] = {{"diagnostics", diagArray}};

    int id = impl->sendRequest("textDocument/codeAction", params);
    auto resp = impl->waitResponse(id, 5000);
#ifdef UVIM_DEBUG_LSP
    if(resp && resp->is_object())
    {
        json logPayload = json::object();
        logPayload["request"] = params;
        logPayload["response"] = *resp;
        logLspDebug("codeAction", logPayload);
    }
#endif
    if(!resp || !resp->is_object())
        return out;
    if(resp->contains("error"))
        return out;

    json result = resp->value("result", json());
    if(!result.is_array())
        return out;

    for(const auto& item : result)
    {
        if(!item.is_object())
            continue;

        CodeAction action;
        fillCodeActionFromJson(item, filePath, action);

        if(action.edits.empty() && action.command.empty() &&
           item.contains("data"))
        {
            int rid = impl->sendRequest("codeAction/resolve", item);
            auto resolved = impl->waitResponse(rid, 5000);
#ifdef UVIM_DEBUG_LSP
            if(resolved && resolved->is_object())
            {
                json logPayload = json::object();
                logPayload["request"] = item;
                logPayload["response"] = *resolved;
                logLspDebug("codeActionResolve", logPayload);
            }
#endif
            if(resolved && resolved->is_object() &&
               !resolved->contains("error"))
            {
                json resolvedAction = resolved->value("result", json::object());
                fillCodeActionFromJson(resolvedAction, filePath, action);
            }
        }

        if(action.edits.empty() && action.command.empty())
            continue;
        if(action.title.empty())
            action.title = "Fix";
        out.push_back(std::move(action));
    }

    return out;
}

std::vector<LspClient::TextEdit>
LspClient::formatting(const std::string& filePath, int tabSize,
                      bool insertSpaces)
{
    std::vector<TextEdit> out;
    if(!running())
        return out;

    std::string abs = absPath(filePath);
    json params;
    params["textDocument"] = {{"uri", pathToFileUri(abs)}};
    params["options"] = {{"tabSize", tabSize}, {"insertSpaces", insertSpaces}};

    int id = impl->sendRequest("textDocument/formatting", params);
    auto resp = impl->waitResponse(id, 5000);
    if(!resp || !resp->is_object())
        return out;
    if(resp->contains("error"))
        return out;

    json result = resp->value("result", json());
    return parseTextEditsForUri(result, abs);
}

std::vector<LspClient::TextEdit>
LspClient::executeCommand(const std::string& command,
                          const std::vector<std::string>& argumentsJson,
                          const std::string& filePath)
{
    std::vector<TextEdit> out;
    if(!running())
        return out;

    json args = json::array();
    for(const auto& arg : argumentsJson)
    {
        try
        {
            args.push_back(json::parse(arg));
        }
        catch(...)
        {
        }
    }

    json params;
    params["command"] = command;
    if(!args.empty())
        params["arguments"] = args;

    int id = impl->sendRequest("workspace/executeCommand", params);
    auto resp = impl->waitResponse(id, 5000);
    if(resp && resp->is_object() && !resp->contains("error"))
    {
        json result = resp->value("result", json());
        if(result.is_object())
        {
            if(result.contains("edit"))
            {
                parseWorkspaceEditInto(result.value("edit", json::object()),
                                       filePath, out);
            }
            else
            {
                parseWorkspaceEditInto(result, filePath, out);
            }
        }
    }

    std::vector<json> applyEdits;
    {
        std::lock_guard<std::mutex> lk(impl->applyMutex);
        applyEdits.swap(impl->pendingApplyEdits);
    }
    for(const auto& edit : applyEdits)
    {
        parseWorkspaceEditInto(edit, filePath, out);
    }

    return out;
}

bool LspClient::requestSemanticTokens(const std::string& filePath)
{
    if(!running())
        return false;

    std::string abs = absPath(filePath);
    json params;
    params["textDocument"] = {{"uri", pathToFileUri(abs)}};

    int id = impl->sendRequest("textDocument/semanticTokens/full", params);
    auto resp = impl->waitResponse(id, 5000);
    if(!resp || !resp->is_object() || resp->contains("error"))
    {
        clearSemanticTokens(abs);
        return false;
    }

    json result = resp->value("result", json::object());
    if(!result.is_object())
    {
        clearSemanticTokens(abs);
        return false;
    }

    std::vector<LspClient::SemanticToken> tokens;
    if(result.contains("data") && result["data"].is_array())
    {
        const json& data = result["data"];
        int line = 0;
        int start = 0;
        for(size_t i = 0; i + 4 < data.size(); i += 5)
        {
            int deltaLine = data[i].get<int>();
            int deltaStart = data[i + 1].get<int>();
            int length = data[i + 2].get<int>();
            int tokenType = data[i + 3].get<int>();
            int tokenMods = data[i + 4].get<int>();

            line += deltaLine;
            if(deltaLine == 0)
                start += deltaStart;
            else
                start = deltaStart;

            if(tokenType < 0 ||
               tokenType >= (int)impl->semanticTokenTypes.size())
                continue;

            LspClient::SemanticToken token;
            token.line = line;
            token.character = start;
            token.length = length;
            token.tokenType = impl->semanticTokenTypes[tokenType];
            token.modifiers = tokenMods;
            tokens.push_back(std::move(token));
        }
    }

    {
        std::lock_guard<std::mutex> lk(impl->semanticMutex);
        impl->semanticTokensByFile[abs] = std::move(tokens);
        impl->semanticTokensRevision[abs]++;
    }
    return true;
}

std::vector<LspClient::SemanticToken>
LspClient::semanticTokens(const std::string& filePath) const
{
    if(!running())
        return {};
    std::string abs = absPath(filePath);
    std::lock_guard<std::mutex> lk(impl->semanticMutex);
    auto it = impl->semanticTokensByFile.find(abs);
    if(it == impl->semanticTokensByFile.end())
        return {};
    return it->second;
}

size_t LspClient::semanticTokensRevision(const std::string& filePath) const
{
    if(!running())
        return 0;
    std::string abs = absPath(filePath);
    std::lock_guard<std::mutex> lk(impl->semanticMutex);
    auto it = impl->semanticTokensRevision.find(abs);
    if(it == impl->semanticTokensRevision.end())
        return 0;
    return it->second;
}

bool LspClient::semanticTokenHasModifier(int modifiers,
                                         std::string_view name) const
{
    if(!impl)
        return false;
    for(size_t i = 0; i < impl->semanticTokenModifiers.size(); ++i)
    {
        if(impl->semanticTokenModifiers[i] == name)
            return (modifiers & (1 << i)) != 0;
    }
    return false;
}

void LspClient::clearSemanticTokens(const std::string& filePath)
{
    std::string abs = absPath(filePath);
    std::lock_guard<std::mutex> lk(impl->semanticMutex);
    impl->semanticTokensByFile.erase(abs);
    impl->semanticTokensRevision.erase(abs);
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

std::vector<LspClient::CompletionItem>
LspClient::completion(const std::string&, int, int, std::string_view, int, char)
{
    return {};
}

std::vector<LspClient::Location> LspClient::references(const std::string&, int,
                                                       int, bool)
{
    return {};
}

std::vector<LspClient::Diagnostic>
LspClient::diagnostics(const std::string&) const
{
    return {};
}

void LspClient::clearDiagnostics(const std::string&) {}

size_t LspClient::diagnosticsRevision(const std::string&) const
{
    return 0;
}

std::vector<LspClient::CodeAction>
LspClient::codeActions(const std::string&, int, std::string_view,
                       const std::vector<Diagnostic>&)
{
    return {};
}

std::vector<LspClient::TextEdit>
LspClient::formatting(const std::string& filePath, int tabSize,
                      bool insertSpaces)
{
    std::vector<TextEdit> out;
    if(!running())
        return out;

    std::string abs = absPath(filePath);
    json params;
    params["textDocument"] = {{"uri", pathToFileUri(abs)}};
    params["options"] = {{"tabSize", tabSize}, {"insertSpaces", insertSpaces}};

    int id = impl->sendRequest("textDocument/formatting", params);
    auto resp = impl->waitResponse(id, 5000);
    if(!resp || !resp->is_object())
        return out;
    if(resp->contains("error"))
        return out;

    json result = resp->value("result", json());
    return parseTextEditsForUri(result, abs);
}

std::vector<LspClient::TextEdit>
LspClient::executeCommand(const std::string&, const std::vector<std::string>&,
                          const std::string&)
{
    return {};
}
bool LspClient::requestSemanticTokens(const std::string&)
{
    return false;
}
std::vector<LspClient::SemanticToken>
LspClient::semanticTokens(const std::string&) const
{
    return {};
}
size_t LspClient::semanticTokensRevision(const std::string&) const
{
    return 0;
}
bool LspClient::semanticTokenHasModifier(int, std::string_view) const
{
    return false;
}
void LspClient::clearSemanticTokens(const std::string&) {}

#endif
