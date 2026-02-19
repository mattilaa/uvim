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

#include "json_utils.h"
#include "text_utils.h"

namespace ju = json_utils;

#ifdef UVIM_DEBUG_LSP
static void logLspDebug(const std::string& tag, const ju::Value& payload)
{
    static std::mutex logMutex;
    std::lock_guard<std::mutex> lk(logMutex);
    std::ofstream out("/tmp/uvim_lsp_codeactions.log", std::ios::app);
    if(!out.is_open())
        return;
    out << "=== " << tag << " ===\n";
    out << ju::stringify_pretty(payload) << "\n";
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

static const ju::Value* member_ptr(const ju::Value* obj, const char* key)
{
    if(!obj || !obj->IsObject())
        return nullptr;
    return ju::find(*obj, key);
}

static int get_int_member(const ju::Value* obj, const char* key, int def = 0)
{
    if(!obj || !obj->IsObject())
        return def;
    return ju::get_int(*obj, key, def);
}

static std::string get_string_member(const ju::Value* obj, const char* key,
                                     std::string_view def = {})
{
    if(!obj || !obj->IsObject())
        return std::string(def);
    return ju::get_string(*obj, key, def);
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
    std::unordered_map<int, ju::Document> responses;

    std::string rootDir;
    std::string serverName;
    std::string serverVersion;
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
    std::vector<ju::Document> pendingApplyEdits;

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

    int sendRequest(const std::string& method, const ju::Document& params)
    {
        int id;
        {
            std::lock_guard<std::mutex> lk(m);
            id = nextId++;
        }

        ju::Document req(rapidjson::kObjectType);
        auto& alloc = req.GetAllocator();
        req.AddMember("jsonrpc", "2.0", alloc);
        req.AddMember("id", id, alloc);
        req.AddMember("method", ju::make_string(method, alloc), alloc);
        ju::Value paramsCopy;
        paramsCopy.CopyFrom(params, alloc);
        req.AddMember("params", paramsCopy, alloc);

        sendRaw(ju::stringify(req));
        return id;
    }

    void sendNotification(const std::string& method,
                          const ju::Document& params)
    {
        ju::Document n(rapidjson::kObjectType);
        auto& alloc = n.GetAllocator();
        n.AddMember("jsonrpc", "2.0", alloc);
        n.AddMember("method", ju::make_string(method, alloc), alloc);
        ju::Value paramsCopy;
        paramsCopy.CopyFrom(params, alloc);
        n.AddMember("params", paramsCopy, alloc);
        sendRaw(ju::stringify(n));
    }

    std::optional<ju::Document> waitResponse(int id, int timeoutMs = 3000)
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
        ju::Document resp = std::move(it->second);
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

                ju::Document msg;
                if(!ju::parse(msg, payload))
                    continue;

                if(const ju::Value* idVal = ju::find(msg, "id"))
                {
                    int id = -1;
                    if(idVal->IsInt())
                        id = idVal->GetInt();
                    if(id != -1)
                    {
                        std::lock_guard<std::mutex> lk(m);
                        responses[id] = std::move(msg);
                        cv.notify_all();
                    }
                }
                else if(const ju::Value* methodVal = ju::find(msg, "method");
                        methodVal && methodVal->IsString())
                {
                    std::string method(methodVal->GetString(),
                                       methodVal->GetStringLength());
                    if(method == "textDocument/publishDiagnostics")
                    {
                        const ju::Value* params = ju::find(msg, "params");
                        std::string uri = get_string_member(params, "uri");
                        if(!uri.empty())
                        {
                            std::string path = absPath(uriToPath(uri));
                            std::vector<LspClient::Diagnostic> diags;
                            const ju::Value* diagnostics =
                                member_ptr(params, "diagnostics");
                            if(diagnostics && diagnostics->IsArray())
                            {
                                diags.reserve(diagnostics->Size());
                                for(const auto& item :
                                    diagnostics->GetArray())
                                {
                                    if(!item.IsObject())
                                        continue;
                                    const ju::Value* range =
                                        ju::find(item, "range");
                                    const ju::Value* start =
                                        member_ptr(range, "start");
                                    const ju::Value* end =
                                        member_ptr(range, "end");
                                    LspClient::Diagnostic diag;
                                    diag.line =
                                        get_int_member(start, "line", 0);
                                    diag.character = get_int_member(
                                        start, "character", 0);
                                    diag.endLine = get_int_member(
                                        end, "line", diag.line);
                                    diag.endCharacter = get_int_member(
                                        end, "character", diag.character);
                                    diag.severity =
                                        get_int_member(&item, "severity", 0);
                                    diag.message = get_string_member(
                                        &item, "message");
                                    diag.source = get_string_member(
                                        &item, "source");
                                    if(const ju::Value* code =
                                           ju::find(item, "code"))
                                    {
                                        diag.codeJson = ju::stringify(*code);
                                    }
                                    if(const ju::Value* data =
                                           ju::find(item, "data"))
                                    {
                                        diag.dataJson = ju::stringify(*data);
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
                        const ju::Value* params = ju::find(msg, "params");
                        const ju::Value* edit = member_ptr(params, "edit");
                        if(edit)
                        {
                            std::lock_guard<std::mutex> lk(applyMutex);
                            ju::Document editDoc;
                            editDoc.CopyFrom(*edit, editDoc.GetAllocator());
                            pendingApplyEdits.push_back(std::move(editDoc));
                        }
                        if(const ju::Value* reqId = ju::find(msg, "id"))
                        {
                            ju::Document resp(rapidjson::kObjectType);
                            auto& alloc = resp.GetAllocator();
                            resp.AddMember("jsonrpc", "2.0", alloc);
                            ju::Value idCopy;
                            idCopy.CopyFrom(*reqId, alloc);
                            resp.AddMember("id", idCopy, alloc);
                            ju::Value result(rapidjson::kObjectType);
                            result.AddMember("applied", true, alloc);
                            resp.AddMember("result", result, alloc);
                            sendRaw(ju::stringify(resp));
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
        ju::Document params(rapidjson::kObjectType);
        auto& alloc = params.GetAllocator();
        params.AddMember("processId", (int)getpid(), alloc);
        params.AddMember("rootUri", ju::make_string(pathToFileUri(rootDir), alloc),
                         alloc);
        params.AddMember("rootPath", ju::make_string(absPath(rootDir), alloc),
                         alloc);
        ju::Value workspaceFolders(rapidjson::kArrayType);
        ju::Value workspaceFolder(rapidjson::kObjectType);
        workspaceFolder.AddMember("uri",
                                  ju::make_string(pathToFileUri(rootDir),
                                                  alloc),
                                  alloc);
        workspaceFolder.AddMember(
            "name",
            ju::make_string(
                std::filesystem::path(rootDir).filename().string(), alloc),
            alloc);
        workspaceFolders.PushBack(workspaceFolder, alloc);
        params.AddMember("workspaceFolders", workspaceFolders, alloc);
        if(semanticTokenTypes.empty())
            semanticTokenTypes = defaultSemanticTokenTypes();
        if(semanticTokenModifiers.empty())
            semanticTokenModifiers = defaultSemanticTokenModifiers();
        ju::Value semCaps(rapidjson::kObjectType);
        semCaps.AddMember("dynamicRegistration", false, alloc);
        ju::Value requests(rapidjson::kObjectType);
        requests.AddMember("range", false, alloc);
        requests.AddMember("full", true, alloc);
        semCaps.AddMember("requests", requests, alloc);
        ju::Value tokenTypes(rapidjson::kArrayType);
        for(const auto& t : semanticTokenTypes)
            tokenTypes.PushBack(ju::make_string(t, alloc), alloc);
        semCaps.AddMember("tokenTypes", tokenTypes, alloc);
        ju::Value tokenModifiers(rapidjson::kArrayType);
        for(const auto& m : semanticTokenModifiers)
            tokenModifiers.PushBack(ju::make_string(m, alloc), alloc);
        semCaps.AddMember("tokenModifiers", tokenModifiers, alloc);
        ju::Value formats(rapidjson::kArrayType);
        formats.PushBack("relative", alloc);
        semCaps.AddMember("formats", formats, alloc);

        ju::Value textDocument(rapidjson::kObjectType);
        textDocument.AddMember("semanticTokens", semCaps, alloc);
        ju::Value capabilities(rapidjson::kObjectType);
        capabilities.AddMember("textDocument", textDocument, alloc);
        params.AddMember("capabilities", capabilities, alloc);

        int id = sendRequest("initialize", params);
        auto resp = waitResponse(id, 5000);
        if(!resp)
            return false;
        if(resp->IsObject())
        {
            const ju::Value* result = ju::find(*resp, "result");
            const ju::Value* caps =
                result ? ju::find(*result, "capabilities") : nullptr;
            const ju::Value* serverInfo =
                result ? ju::find(*result, "serverInfo") : nullptr;
            const ju::Value* sem =
                caps ? ju::find(*caps, "semanticTokensProvider") : nullptr;
            const ju::Value* legend =
                sem ? ju::find(*sem, "legend") : nullptr;
            const ju::Value* tokenTypesVal =
                legend ? ju::find(*legend, "tokenTypes") : nullptr;
            if(tokenTypesVal && tokenTypesVal->IsArray())
            {
                std::vector<std::string> types;
                for(const auto& item : tokenTypesVal->GetArray())
                {
                    if(item.IsString())
                        types.emplace_back(item.GetString(),
                                           item.GetStringLength());
                }
                if(!types.empty())
                    semanticTokenTypes = std::move(types);
            }
            const ju::Value* tokenModsVal =
                legend ? ju::find(*legend, "tokenModifiers") : nullptr;
            if(tokenModsVal && tokenModsVal->IsArray())
            {
                std::vector<std::string> mods;
                for(const auto& item : tokenModsVal->GetArray())
                {
                    if(item.IsString())
                        mods.emplace_back(item.GetString(),
                                          item.GetStringLength());
                }
                if(!mods.empty())
                    semanticTokenModifiers = std::move(mods);
            }
            if(serverInfo && serverInfo->IsObject())
            {
                const ju::Value* name = ju::find(*serverInfo, "name");
                const ju::Value* version = ju::find(*serverInfo, "version");
                if(name && name->IsString())
                    serverName.assign(name->GetString(),
                                      name->GetStringLength());
                if(version && version->IsString())
                    serverVersion.assign(version->GetString(),
                                         version->GetStringLength());
            }
        }

        // Send initialized notification
        ju::Document initParams(rapidjson::kObjectType);
        sendNotification("initialized", initParams);
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
            ju::Document params(rapidjson::kObjectType);
            int id = impl->sendRequest("shutdown", params);
            (void)impl->waitResponse(id, 1000);
            ju::Document exitParams(rapidjson::kObjectType);
            impl->sendNotification("exit", exitParams);
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
    impl->serverName.clear();
    impl->serverVersion.clear();
}

bool LspClient::running() const
{
    return impl && impl->alive.load();
}

std::string LspClient::serverName() const
{
    if(!impl)
        return {};
    return impl->serverName;
}

std::string LspClient::serverVersion() const
{
    if(!impl)
        return {};
    return impl->serverVersion;
}

void LspClient::didOpen(const std::string& filePath,
                        const std::string& languageId, const std::string& text)
{
    if(!running())
        return;

    std::string abs = absPath(filePath);

    int ver = 1;
    impl->docVersion[abs] = ver;

    ju::Document params(rapidjson::kObjectType);
    auto& alloc = params.GetAllocator();
    ju::Value textDoc(rapidjson::kObjectType);
    textDoc.AddMember("uri", ju::make_string(pathToFileUri(abs), alloc), alloc);
    textDoc.AddMember("languageId", ju::make_string(languageId, alloc), alloc);
    textDoc.AddMember("version", ver, alloc);
    textDoc.AddMember("text", ju::make_string(text, alloc), alloc);
    params.AddMember("textDocument", textDoc, alloc);
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

    ju::Document params(rapidjson::kObjectType);
    auto& alloc = params.GetAllocator();
    ju::Value textDoc(rapidjson::kObjectType);
    textDoc.AddMember("uri", ju::make_string(pathToFileUri(abs), alloc), alloc);
    textDoc.AddMember("version", ver, alloc);
    params.AddMember("textDocument", textDoc, alloc);
    // Full-text sync
    ju::Value changes(rapidjson::kArrayType);
    ju::Value change(rapidjson::kObjectType);
    change.AddMember("text", ju::make_string(text, alloc), alloc);
    changes.PushBack(change, alloc);
    params.AddMember("contentChanges", changes, alloc);
    impl->sendNotification("textDocument/didChange", params);
}

void LspClient::didSave(const std::string& filePath)
{
    if(!running())
        return;

    std::string abs = absPath(filePath);
    ju::Document params(rapidjson::kObjectType);
    auto& alloc = params.GetAllocator();
    ju::Value textDoc(rapidjson::kObjectType);
    textDoc.AddMember("uri", ju::make_string(pathToFileUri(abs), alloc), alloc);
    params.AddMember("textDocument", textDoc, alloc);
    impl->sendNotification("textDocument/didSave", params);
}

static std::optional<LspClient::Location>
parseDefinitionResult(const ju::Value* result)
{
    // clangd may return Location | Location[] | LocationLink[]
    if(!result || result->IsNull())
        return std::nullopt;

    auto parseLocationObj =
        [](const ju::Value& loc) -> std::optional<LspClient::Location>
    {
        if(!loc.IsObject())
            return std::nullopt;

        std::string uri;
        const ju::Value* range = nullptr;

        if(const ju::Value* uriVal = ju::find(loc, "uri"))
        {
            if(uriVal->IsString())
                uri.assign(uriVal->GetString(), uriVal->GetStringLength());
            range = ju::find(loc, "range");
        }
        else if(const ju::Value* targetUriVal = ju::find(loc, "targetUri"))
        {
            // LocationLink
            if(targetUriVal->IsString())
                uri.assign(targetUriVal->GetString(),
                           targetUriVal->GetStringLength());
            range = ju::find(loc, "targetRange");
        }
        else
        {
            return std::nullopt;
        }

        if(uri.empty() || !range || !range->IsObject())
            return std::nullopt;

        const ju::Value* start = ju::find(*range, "start");
        int line = get_int_member(start, "line", 0);
        int ch = get_int_member(start, "character", 0);

        LspClient::Location out;
        out.path = uriToPath(uri);
        out.line = line;
        out.character = ch;
        return out;
    };

    if(result->IsArray() && result->Size() > 0)
        return parseLocationObj((*result)[0]);
    if(result->IsObject())
        return parseLocationObj(*result);
    return std::nullopt;
}

std::optional<LspClient::Location>
LspClient::definition(const std::string& filePath, int line,
                      int characterUtf8ByteOffset, std::string_view lineText)
{
    if(!running())
        return std::nullopt;

    std::string abs = absPath(filePath);

    // LSP wants UTF-16 character offset; we only have a byte offset.
    // The caller should pass the byte index within the line.
    int utf16ch = characterUtf8ByteOffset;
    if(!lineText.empty())
    {
        utf16ch = text_utils::utf8ByteOffsetToUtf16(std::string(lineText),
                                                    characterUtf8ByteOffset);
    }
    else
    {
        std::string text = readFileAll(abs);
        // If file isn't on disk (unsaved buffer), caller should pass lineText.
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

    ju::Document params(rapidjson::kObjectType);
    auto& alloc = params.GetAllocator();
    ju::Value textDoc(rapidjson::kObjectType);
    textDoc.AddMember("uri", ju::make_string(pathToFileUri(abs), alloc), alloc);
    params.AddMember("textDocument", textDoc, alloc);
    ju::Value pos(rapidjson::kObjectType);
    pos.AddMember("line", line, alloc);
    pos.AddMember("character", utf16ch, alloc);
    params.AddMember("position", pos, alloc);

    int id = impl->sendRequest("textDocument/definition", params);
    auto resp = impl->waitResponse(id, 5000);
    if(!resp || !resp->IsObject())
        return std::nullopt;

    if(ju::has(*resp, "error"))
        return std::nullopt;

    const ju::Value* result = ju::find(*resp, "result");
    return parseDefinitionResult(result);
}

std::optional<LspClient::Location>
LspClient::declaration(const std::string& filePath, int line,
                       int characterUtf8ByteOffset, std::string_view lineText)
{
    if(!running())
        return std::nullopt;

    std::string abs = absPath(filePath);

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

    ju::Document params(rapidjson::kObjectType);
    auto& alloc = params.GetAllocator();
    ju::Value textDoc(rapidjson::kObjectType);
    textDoc.AddMember("uri", ju::make_string(pathToFileUri(abs), alloc), alloc);
    params.AddMember("textDocument", textDoc, alloc);
    ju::Value pos(rapidjson::kObjectType);
    pos.AddMember("line", line, alloc);
    pos.AddMember("character", utf16ch, alloc);
    params.AddMember("position", pos, alloc);

    int id = impl->sendRequest("textDocument/declaration", params);
    auto resp = impl->waitResponse(id, 5000);
    if(!resp || !resp->IsObject())
        return std::nullopt;
    if(ju::has(*resp, "error"))
        return std::nullopt;

    const ju::Value* result = ju::find(*resp, "result");
    return parseDefinitionResult(result);
}

std::optional<LspClient::Location>
LspClient::typeDefinition(const std::string& filePath, int line,
                          int characterUtf8ByteOffset, std::string_view lineText)
{
    if(!running())
        return std::nullopt;

    std::string abs = absPath(filePath);

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

    ju::Document params(rapidjson::kObjectType);
    auto& alloc = params.GetAllocator();
    ju::Value textDoc(rapidjson::kObjectType);
    textDoc.AddMember("uri", ju::make_string(pathToFileUri(abs), alloc), alloc);
    params.AddMember("textDocument", textDoc, alloc);
    ju::Value pos(rapidjson::kObjectType);
    pos.AddMember("line", line, alloc);
    pos.AddMember("character", utf16ch, alloc);
    params.AddMember("position", pos, alloc);

    int id = impl->sendRequest("textDocument/typeDefinition", params);
    auto resp = impl->waitResponse(id, 5000);
    if(!resp || !resp->IsObject())
        return std::nullopt;
    if(ju::has(*resp, "error"))
        return std::nullopt;

    const ju::Value* result = ju::find(*resp, "result");
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

    ju::Document params(rapidjson::kObjectType);
    auto& alloc = params.GetAllocator();
    ju::Value textDoc(rapidjson::kObjectType);
    textDoc.AddMember("uri", ju::make_string(pathToFileUri(abs), alloc), alloc);
    params.AddMember("textDocument", textDoc, alloc);
    ju::Value pos(rapidjson::kObjectType);
    pos.AddMember("line", line, alloc);
    pos.AddMember("character", utf16ch, alloc);
    params.AddMember("position", pos, alloc);
    // Minimal context; clangd is fine without it, but it helps some servers.
    ju::Value ctx(rapidjson::kObjectType);
    ctx.AddMember("triggerKind", triggerKind, alloc);
    if(triggerCharacter != '\0')
    {
        std::string tc(1, triggerCharacter);
        ctx.AddMember("triggerCharacter", ju::make_string(tc, alloc), alloc);
    }
    params.AddMember("context", ctx, alloc);

    int id = impl->sendRequest("textDocument/completion", params);
    auto resp = impl->waitResponse(id, 5000);
    if(!resp || !resp->IsObject())
        return out;
    if(ju::has(*resp, "error"))
        return out;

    const ju::Value* result = ju::find(*resp, "result");
    const ju::Value* items = nullptr;
    if(result)
    {
        if(result->IsArray())
        {
            items = result;
        }
        else if(result->IsObject())
        {
            const ju::Value* itemsVal = ju::find(*result, "items");
            if(itemsVal)
                items = itemsVal;
            else
                items = ju::find(*result, "result");
        }
    }

    if(!items || !items->IsArray())
        return out;

    out.reserve(std::min<size_t>(items->Size(), 64));
    for(size_t i = 0; i < items->Size() && out.size() < 64; ++i)
    {
        const ju::Value& it = (*items)[i];
        if(!it.IsObject())
            continue;

        CompletionItem ci;
        ci.label = get_string_member(&it, "label");
        ci.insertText = get_string_member(&it, "insertText");
        int fmt = get_int_member(&it, "insertTextFormat", 1);
        ci.isSnippet = (fmt == 2);
        ci.kind = get_int_member(&it, "kind", 0);
        ci.detail = get_string_member(&it, "detail");
        const ju::Value* labelDetails = ju::find(it, "labelDetails");
        if(labelDetails && labelDetails->IsObject())
        {
            const ju::Value* detail = ju::find(*labelDetails, "detail");
            if(detail && detail->IsString())
                ci.labelDetail.assign(detail->GetString(),
                                      detail->GetStringLength());
            const ju::Value* desc = ju::find(*labelDetails, "description");
            if(desc && desc->IsString())
                ci.labelDescription.assign(desc->GetString(),
                                           desc->GetStringLength());
        }
        const ju::Value* docVal = ju::find(it, "documentation");
        if(docVal)
        {
            if(docVal->IsString())
            {
                ci.documentation.assign(docVal->GetString(),
                                        docVal->GetStringLength());
            }
            else if(docVal->IsObject())
            {
                const ju::Value* value = ju::find(*docVal, "value");
                if(value && value->IsString())
                    ci.documentation.assign(value->GetString(),
                                            value->GetStringLength());
            }
        }

        // Prefer textEdit.newText when present.
        const ju::Value* textEdit = ju::find(it, "textEdit");
        if(textEdit && textEdit->IsObject())
        {
            const ju::Value* newText = ju::find(*textEdit, "newText");
            if(newText && newText->IsString())
            {
                ci.insertText.assign(newText->GetString(),
                                     newText->GetStringLength());
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

    ju::Document params(rapidjson::kObjectType);
    auto& alloc = params.GetAllocator();
    ju::Value textDoc(rapidjson::kObjectType);
    textDoc.AddMember("uri", ju::make_string(pathToFileUri(abs), alloc), alloc);
    params.AddMember("textDocument", textDoc, alloc);
    ju::Value pos(rapidjson::kObjectType);
    pos.AddMember("line", line, alloc);
    pos.AddMember("character", utf16ch, alloc);
    params.AddMember("position", pos, alloc);
    ju::Value ctx(rapidjson::kObjectType);
    ctx.AddMember("includeDeclaration", includeDeclaration, alloc);
    params.AddMember("context", ctx, alloc);

    int id = impl->sendRequest("textDocument/references", params);
    auto resp = impl->waitResponse(id, 10000); // 10s timeout for references
    if(!resp || !resp->IsObject())
        return out;

    if(ju::has(*resp, "error"))
        return out;

    const ju::Value* result = ju::find(*resp, "result");
    if(!result || !result->IsArray())
        return out;

    out.reserve(result->Size());
    for(const auto& loc : result->GetArray())
    {
        if(!loc.IsObject())
            continue;

        std::string uri = get_string_member(&loc, "uri");
        if(uri.empty())
            continue;

        const ju::Value* range = ju::find(loc, "range");
        if(!range || !range->IsObject())
            continue;

        const ju::Value* startPos = ju::find(*range, "start");
        int locLine = get_int_member(startPos, "line", 0);
        int locChar = get_int_member(startPos, "character", 0);

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
parseTextEditsForUri(const ju::Value* edits, const std::string& targetPath)
{
    std::vector<LspClient::TextEdit> out;
    if(!edits || !edits->IsArray())
        return out;

    for(const auto& edit : edits->GetArray())
    {
        if(!edit.IsObject())
            continue;
        const ju::Value* range = ju::find(edit, "range");
        const ju::Value* start = member_ptr(range, "start");
        const ju::Value* end = member_ptr(range, "end");
        LspClient::TextEdit te;
        te.startLine = get_int_member(start, "line", 0);
        te.startCharacter = get_int_member(start, "character", 0);
        te.endLine = get_int_member(end, "line", te.startLine);
        te.endCharacter = get_int_member(end, "character", te.startCharacter);
        te.newText = get_string_member(&edit, "newText");
        out.push_back(std::move(te));
    }

    return out;
}

static void parseWorkspaceEditInto(const ju::Value* editObj,
                                   const std::string& filePath,
                                   std::vector<LspClient::TextEdit>& out)
{
    if(!editObj || !editObj->IsObject())
        return;
    const ju::Value* changes = ju::find(*editObj, "changes");
    if(changes && changes->IsObject())
    {
        for(auto it = changes->MemberBegin(); it != changes->MemberEnd();
            ++it)
        {
            std::string path(it->name.GetString(),
                             it->name.GetStringLength());
            path = uriToPath(path);
            if(absPath(path) != absPath(filePath))
                continue;
            std::vector<LspClient::TextEdit> edits =
                parseTextEditsForUri(&it->value, path);
            out.insert(out.end(), edits.begin(), edits.end());
        }
        return;
    }
    const ju::Value* documentChanges =
        ju::find(*editObj, "documentChanges");
    if(documentChanges && documentChanges->IsArray())
    {
        for(const auto& change : documentChanges->GetArray())
        {
            if(!change.IsObject())
                continue;
            const ju::Value* textDoc = ju::find(change, "textDocument");
            std::string uri = get_string_member(textDoc, "uri");
            if(uri.empty())
                continue;
            std::string path = uriToPath(uri);
            if(absPath(path) != absPath(filePath))
                continue;
            std::vector<LspClient::TextEdit> edits = parseTextEditsForUri(
                ju::find(change, "edits"), path);
            out.insert(out.end(), edits.begin(), edits.end());
        }
    }
}

static void fillCodeActionFromJson(const ju::Value& item,
                                   const std::string& filePath,
                                   LspClient::CodeAction& action)
{
    if(!item.IsObject())
        return;
    if(action.title.empty())
        action.title = get_string_member(&item, "title");

    if(const ju::Value* edit = ju::find(item, "edit"))
    {
        parseWorkspaceEditInto(edit, filePath, action.edits);
    }

    const ju::Value* command = ju::find(item, "command");
    if(command && command->IsString())
    {
        if(action.command.empty())
            action.command =
                std::string(command->GetString(), command->GetStringLength());
        const ju::Value* args = ju::find(item, "arguments");
        if(args && args->IsArray())
        {
            for(const auto& arg : args->GetArray())
            {
                action.commandArgsJson.push_back(ju::stringify(arg));
                if(!arg.IsObject())
                    continue;
                if(const ju::Value* wsEdit = ju::find(arg, "workspaceEdit"))
                {
                    parseWorkspaceEditInto(
                        wsEdit, filePath, action.edits);
                }
                else
                {
                    parseWorkspaceEditInto(&arg, filePath, action.edits);
                }
            }
        }
    }

    if(command && command->IsObject())
    {
        if(action.command.empty())
            action.command = get_string_member(command, "command");
        const ju::Value* args = ju::find(*command, "arguments");
        if(args && args->IsArray())
        {
            for(const auto& arg : args->GetArray())
            {
                action.commandArgsJson.push_back(ju::stringify(arg));
                if(!arg.IsObject())
                    continue;
                if(const ju::Value* wsEdit = ju::find(arg, "workspaceEdit"))
                {
                    parseWorkspaceEditInto(
                        wsEdit, filePath, action.edits);
                }
                else
                {
                    parseWorkspaceEditInto(&arg, filePath, action.edits);
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

    ju::Document params(rapidjson::kObjectType);
    auto& alloc = params.GetAllocator();
    ju::Value diagArray(rapidjson::kArrayType);
    for(const auto& d : diagnostics)
    {
        ju::Value range(rapidjson::kObjectType);
        ju::Value start(rapidjson::kObjectType);
        start.AddMember("line", d.line, alloc);
        start.AddMember("character", d.character, alloc);
        ju::Value end(rapidjson::kObjectType);
        end.AddMember("line", d.endLine, alloc);
        end.AddMember("character", d.endCharacter, alloc);
        range.AddMember("start", start, alloc);
        range.AddMember("end", end, alloc);
        ju::Value jd(rapidjson::kObjectType);
        jd.AddMember("range", range, alloc);
        if(d.severity > 0)
            jd.AddMember("severity", d.severity, alloc);
        if(!d.message.empty())
            jd.AddMember("message", ju::make_string(d.message, alloc), alloc);
        if(!d.source.empty())
            jd.AddMember("source", ju::make_string(d.source, alloc), alloc);
        if(!d.codeJson.empty())
        {
            ju::Document codeDoc;
            if(ju::parse(codeDoc, d.codeJson))
            {
                ju::Value codeValue;
                codeValue.CopyFrom(codeDoc, alloc);
                jd.AddMember("code", codeValue, alloc);
            }
        }
        if(!d.dataJson.empty())
        {
            ju::Document dataDoc;
            if(ju::parse(dataDoc, d.dataJson))
            {
                ju::Value dataValue;
                dataValue.CopyFrom(dataDoc, alloc);
                jd.AddMember("data", dataValue, alloc);
            }
        }
        diagArray.PushBack(jd, alloc);
    }

    ju::Value textDoc(rapidjson::kObjectType);
    textDoc.AddMember("uri", ju::make_string(pathToFileUri(abs), alloc), alloc);
    params.AddMember("textDocument", textDoc, alloc);
    ju::Value range(rapidjson::kObjectType);
    ju::Value rangeStart(rapidjson::kObjectType);
    rangeStart.AddMember("line", line, alloc);
    rangeStart.AddMember("character", 0, alloc);
    ju::Value rangeEnd(rapidjson::kObjectType);
    rangeEnd.AddMember("line", line, alloc);
    rangeEnd.AddMember("character", endCharUtf16, alloc);
    range.AddMember("start", rangeStart, alloc);
    range.AddMember("end", rangeEnd, alloc);
    params.AddMember("range", range, alloc);
    ju::Value ctx(rapidjson::kObjectType);
    ctx.AddMember("diagnostics", diagArray, alloc);
    params.AddMember("context", ctx, alloc);

    int id = impl->sendRequest("textDocument/codeAction", params);
    auto resp = impl->waitResponse(id, 5000);
#ifdef UVIM_DEBUG_LSP
    if(resp && resp->IsObject())
    {
        ju::Document logPayload(rapidjson::kObjectType);
        auto& logAlloc = logPayload.GetAllocator();
        ju::Value reqCopy;
        reqCopy.CopyFrom(params, logAlloc);
        logPayload.AddMember("request", reqCopy, logAlloc);
        ju::Value respCopy;
        respCopy.CopyFrom(*resp, logAlloc);
        logPayload.AddMember("response", respCopy, logAlloc);
        logLspDebug("codeAction", logPayload);
    }
#endif
    if(!resp || !resp->IsObject())
        return out;
    if(ju::has(*resp, "error"))
        return out;

    const ju::Value* result = ju::find(*resp, "result");
    if(!result || !result->IsArray())
        return out;

    for(const auto& item : result->GetArray())
    {
        if(!item.IsObject())
            continue;

        CodeAction action;
        fillCodeActionFromJson(item, filePath, action);

        if(action.edits.empty() && action.command.empty() &&
           ju::has(item, "data"))
        {
            ju::Document resolveParams;
            resolveParams.CopyFrom(item, resolveParams.GetAllocator());
            int rid = impl->sendRequest("codeAction/resolve", resolveParams);
            auto resolved = impl->waitResponse(rid, 5000);
#ifdef UVIM_DEBUG_LSP
            if(resolved && resolved->IsObject())
            {
                ju::Document logPayload(rapidjson::kObjectType);
                auto& logAlloc = logPayload.GetAllocator();
                ju::Value reqCopy;
                reqCopy.CopyFrom(item, logAlloc);
                logPayload.AddMember("request", reqCopy, logAlloc);
                ju::Value respCopy;
                respCopy.CopyFrom(*resolved, logAlloc);
                logPayload.AddMember("response", respCopy, logAlloc);
                logLspDebug("codeActionResolve", logPayload);
            }
#endif
            if(resolved && resolved->IsObject() &&
               !ju::has(*resolved, "error"))
            {
                const ju::Value* resolvedAction =
                    ju::find(*resolved, "result");
                if(resolvedAction && resolvedAction->IsObject())
                    fillCodeActionFromJson(*resolvedAction, filePath, action);
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
    ju::Document params(rapidjson::kObjectType);
    auto& alloc = params.GetAllocator();
    ju::Value textDoc(rapidjson::kObjectType);
    textDoc.AddMember("uri", ju::make_string(pathToFileUri(abs), alloc), alloc);
    params.AddMember("textDocument", textDoc, alloc);
    ju::Value options(rapidjson::kObjectType);
    options.AddMember("tabSize", tabSize, alloc);
    options.AddMember("insertSpaces", insertSpaces, alloc);
    params.AddMember("options", options, alloc);

    int id = impl->sendRequest("textDocument/formatting", params);
    auto resp = impl->waitResponse(id, 5000);
    if(!resp || !resp->IsObject())
        return out;
    if(ju::has(*resp, "error"))
        return out;

    const ju::Value* result = ju::find(*resp, "result");
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

    ju::Document params(rapidjson::kObjectType);
    auto& alloc = params.GetAllocator();
    params.AddMember("command", ju::make_string(command, alloc), alloc);
    ju::Value args(rapidjson::kArrayType);
    for(const auto& arg : argumentsJson)
    {
        ju::Document argDoc;
        if(ju::parse(argDoc, arg))
        {
            ju::Value argValue;
            argValue.CopyFrom(argDoc, alloc);
            args.PushBack(argValue, alloc);
        }
    }
    if(!args.Empty())
        params.AddMember("arguments", args, alloc);

    int id = impl->sendRequest("workspace/executeCommand", params);
    auto resp = impl->waitResponse(id, 5000);
    if(resp && resp->IsObject() && !ju::has(*resp, "error"))
    {
        const ju::Value* result = ju::find(*resp, "result");
        if(result && result->IsObject())
        {
            if(const ju::Value* edit = ju::find(*result, "edit"))
            {
                parseWorkspaceEditInto(edit, filePath, out);
            }
            else
            {
                parseWorkspaceEditInto(result, filePath, out);
            }
        }
    }

    std::vector<ju::Document> applyEdits;
    {
        std::lock_guard<std::mutex> lk(impl->applyMutex);
        applyEdits.swap(impl->pendingApplyEdits);
    }
    for(const auto& edit : applyEdits)
    {
        parseWorkspaceEditInto(&edit, filePath, out);
    }

    return out;
}

bool LspClient::requestSemanticTokens(const std::string& filePath)
{
    if(!running())
        return false;

    std::string abs = absPath(filePath);
    ju::Document params(rapidjson::kObjectType);
    auto& alloc = params.GetAllocator();
    ju::Value textDoc(rapidjson::kObjectType);
    textDoc.AddMember("uri", ju::make_string(pathToFileUri(abs), alloc), alloc);
    params.AddMember("textDocument", textDoc, alloc);

    int id = impl->sendRequest("textDocument/semanticTokens/full", params);
    auto resp = impl->waitResponse(id, 5000);
    if(!resp || !resp->IsObject() || ju::has(*resp, "error"))
    {
        clearSemanticTokens(abs);
        return false;
    }

    const ju::Value* result = ju::find(*resp, "result");
    if(!result || !result->IsObject())
    {
        clearSemanticTokens(abs);
        return false;
    }

    std::vector<LspClient::SemanticToken> tokens;
    const ju::Value* data = ju::find(*result, "data");
    if(data && data->IsArray())
    {
        int line = 0;
        int start = 0;
        auto get_int = [](const ju::Value& v) -> int
        {
            if(v.IsInt())
                return v.GetInt();
            if(v.IsUint())
                return static_cast<int>(v.GetUint());
            if(v.IsInt64())
                return static_cast<int>(v.GetInt64());
            if(v.IsUint64())
                return static_cast<int>(v.GetUint64());
            if(v.IsDouble())
                return static_cast<int>(v.GetDouble());
            return 0;
        };
        for(size_t i = 0; i + 4 < data->Size(); i += 5)
        {
            int deltaLine = get_int((*data)[i]);
            int deltaStart = get_int((*data)[i + 1]);
            int length = get_int((*data)[i + 2]);
            int tokenType = get_int((*data)[i + 3]);
            int tokenMods = get_int((*data)[i + 4]);

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
std::string LspClient::serverName() const
{
    return {};
}
std::string LspClient::serverVersion() const
{
    return {};
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
                                                         int, int, std::string_view)
{
    return std::nullopt;
}

std::optional<LspClient::Location> LspClient::declaration(const std::string&,
                                                          int, int, std::string_view)
{
    return std::nullopt;
}

std::optional<LspClient::Location>
LspClient::typeDefinition(const std::string&, int, int, std::string_view)
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
    (void)filePath;
    (void)tabSize;
    (void)insertSpaces;
    return {};
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
