#include "editor.h"
#include "editor_path_utilities.h"
#include "formatter.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

#ifdef UVIM_ENABLE_CLANGD_LSP
static std::string resolve_executable_path(const std::string& exe)
{
    if(exe.empty())
        return {};
    fs::path exePath(exe);
    if(exePath.has_parent_path())
    {
        std::error_code ec;
        if(fs::exists(exePath, ec) && fs::is_regular_file(exePath, ec))
            return exePath.string();
        return {};
    }

    const char* path = std::getenv("PATH");
    if(!path || !*path)
        return {};

    std::string_view pathView{path};
    size_t start = 0;
    while(start < pathView.size())
    {
#ifdef _WIN32
        size_t end = pathView.find(';', start);
#else
        size_t end = pathView.find(':', start);
#endif
        if(end == std::string_view::npos)
            end = pathView.size();
        if(end > start)
        {
            fs::path candidate =
                fs::path(std::string(pathView.substr(start, end - start))) /
                exe;
            std::error_code ec;
            if(fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
                return candidate.string();
        }
        start = end + 1;
    }
    return {};
}

static fs::path make_temp_file_path(const std::string& stem,
                                    const std::string& extension)
{
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec);
    if(ec || dir.empty())
        dir = fs::current_path(ec);

    const auto now =
        std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream name;
    name << stem << '_' << now << '_' << reinterpret_cast<std::uintptr_t>(&dir)
         << extension;
    return dir / name.str();
}

static int utf16ToUtf8ByteOffset(const std::string& line, int utf16Offset)
{
    if(utf16Offset <= 0)
        return 0;

    int u16 = 0;
    int i = 0;
    while(i < (int)line.size() && u16 < utf16Offset)
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

        int u16len = (codepoint <= 0xFFFF) ? 1 : 2;
        if(u16 + u16len > utf16Offset)
            break;

        u16 += u16len;
        i += len;
    }
    return i;
}

bool MlangFormatter::operator()()
{
    if(!editor.currentBuffer || !editor.lines)
        return false;
    if(!editor.isFileType<FileType::Mla>())
        return false;

    const int savedY = editor.cursorY ? *editor.cursorY : 0;
    const int savedX = editor.cursorX ? *editor.cursorX : 0;
    bool externalFormatterFailed = false;
    std::string externalFormatterError;

    {
        fs::path tempPath = make_temp_file_path("uvim_mlang_fmt", ".mla");
        std::ofstream tempFile(tempPath);
        if(tempFile.is_open())
        {
            for(size_t i = 0; i < editor.lines->size(); ++i)
                tempFile << (*editor.lines)[i] << '\n';
            tempFile.close();

            std::string absFilename = editor.currentBuffer->filename;
            if(!absFilename.empty())
                absFilename =
                    EditorPathUtilities::resolveEditorPath(absFilename)
                        .string();

            fs::path errPath =
                make_temp_file_path("uvim_mlang_fmt_err", ".log");
            auto runFmt = [&](const std::string& exe) -> int
            {
                std::string cmd = "\"" + exe +
                                  "\" -i --style file --assume-filename=\"" +
                                  absFilename + "\" \"" + tempPath.string() +
                                  "\" 2>\"" + errPath.string() + "\"";
                return std::system(cmd.c_str());
            };

            std::string fmtExe = resolve_executable_path("mlang-format");
            if(fmtExe.empty())
            {
                std::error_code ec;
                const std::string hb = "/opt/homebrew/bin/mlang-format";
                if(fs::exists(hb, ec) && fs::is_regular_file(hb, ec))
                    fmtExe = hb;
            }

            int fmtStatus = -1;
            bool formattedOk = false;
            if(!fmtExe.empty())
            {
                fmtStatus = runFmt(fmtExe);
                formattedOk = (fmtStatus == 0);
            }

            if(!formattedOk)
            {
                externalFormatterFailed = true;
                std::ifstream errFile(errPath);
                if(errFile.is_open())
                {
                    std::getline(errFile, externalFormatterError);
                    errFile.close();
                }
                if(externalFormatterError.empty())
                {
                    if(fmtExe.empty())
                        externalFormatterError = "mlang-format not found";
                    else
                        externalFormatterError =
                            "exit status " + std::to_string(fmtStatus);
                }
            }

            if(formattedOk)
            {
                std::ifstream in(tempPath);
                std::vector<std::string> newLines;
                std::string line;
                while(std::getline(in, line))
                {
                    if(!line.empty() && line.back() == '\r')
                        line.pop_back();
                    newLines.push_back(line);
                }
                {
                    std::error_code removeEc;
                    fs::remove(tempPath, removeEc);
                    fs::remove(errPath, removeEc);
                }

                if(!newLines.empty() && newLines.back().empty())
                    newLines.pop_back();
                if(newLines.empty())
                    newLines.push_back("");

                std::vector<std::string>& activeLines =
                    editor.currentBuffer->lines;
                const std::string fmtLabel =
                    fmtExe.empty() ? "mlang-format"
                                   : ("mlang-format (" + fmtExe + ")");
                if(newLines == activeLines)
                {
                    editor.setStatusMessage(fmtLabel + ": no changes");
                    return true;
                }

                editor.saveState();
                activeLines = std::move(newLines);
                editor.lines = &editor.currentBuffer->lines;
                if(editor.dirty)
                    *editor.dirty = true;
                editor.currentBuffer->lspSyncNeeded = true;
                if(editor.cursorY && editor.cursorX && editor.lines &&
                   !editor.lines->empty())
                {
                    *editor.cursorY =
                        std::clamp(savedY, 0, (int)editor.lines->size() - 1);
                    *editor.cursorX = std::clamp(
                        savedX, 0,
                        (int)(*editor.lines)[*editor.cursorY].size());
                }
                editor.adjustViewport();
                editor.needsFullRedraw = true;
                editor.setStatusMessage(fmtLabel + ": formatted buffer");
                return true;
            }
            {
                std::error_code removeEc;
                fs::remove(tempPath, removeEc);
                fs::remove(errPath, removeEc);
            }
        }
    }

    if(!editor.isMlangLspEnabled() || !editor.mlangLspClient)
    {
        editor.setStatusMessage("mlang-format: not found (and mlang LSP OFF)");
        return false;
    }

    std::string text;
    text.reserve(editor.lines->size() * 80);
    for(size_t i = 0; i < editor.lines->size(); ++i)
    {
        text += (*editor.lines)[i];
        if(i + 1 < editor.lines->size())
            text.push_back('\n');
    }
    editor.mlangLspClient->didChange(editor.currentBuffer->filename, text,
                                     "mlang");
    editor.mlangLspClient->didChange(editor.currentBuffer->filename, text,
                                     "mlang");

    std::vector<LspClient::TextEdit> edits = editor.mlangLspClient->formatting(
        editor.currentBuffer->filename, 4, true);
    if(edits.empty())
    {
        if(externalFormatterFailed)
        {
            editor.setStatusMessage("mlang-format failed (" +
                                    externalFormatterError.substr(0, 80) +
                                    "); mlang LSP: no changes");
        }
        else
            editor.setStatusMessage("mlang LSP: no changes");
        return true;
    }

    std::sort(edits.begin(), edits.end(),
              [](const LspClient::TextEdit& a, const LspClient::TextEdit& b)
              {
                  if(a.startLine != b.startLine)
                      return a.startLine > b.startLine;
                  return a.startCharacter > b.startCharacter;
              });

    for(const auto& edit : edits)
    {
        if(edit.startLine < 0 || edit.startLine >= (int)editor.lines->size())
            continue;
        if(edit.endLine < 0 || edit.endLine >= (int)editor.lines->size())
            continue;

        std::string& startLine = (*editor.lines)[edit.startLine];
        std::string& endLine = (*editor.lines)[edit.endLine];
        int startByte = utf16ToUtf8ByteOffset(startLine, edit.startCharacter);
        int endByte = utf16ToUtf8ByteOffset(endLine, edit.endCharacter);

        if(edit.startLine == edit.endLine)
        {
            startLine = startLine.substr(0, startByte) + edit.newText +
                        endLine.substr(endByte);
            continue;
        }

        std::string prefix = startLine.substr(0, startByte);
        std::string suffix = endLine.substr(endByte);
        std::string combined = prefix + edit.newText + suffix;

        std::vector<std::string> newLines;
        size_t pos = 0;
        while(pos <= combined.size())
        {
            size_t next = combined.find('\n', pos);
            if(next == std::string::npos)
            {
                newLines.push_back(combined.substr(pos));
                break;
            }
            newLines.push_back(combined.substr(pos, next - pos));
            pos = next + 1;
        }

        editor.lines->erase(editor.lines->begin() + edit.startLine,
                            editor.lines->begin() + edit.endLine + 1);
        editor.lines->insert(editor.lines->begin() + edit.startLine,
                             newLines.begin(), newLines.end());
    }

    *editor.dirty = true;
    editor.saveState();
    editor.currentBuffer->lspSyncNeeded = true;
    editor.adjustViewport();
    editor.needsFullRedraw = true;
    if(externalFormatterFailed)
    {
        editor.setStatusMessage("mlang-format failed (" +
                                externalFormatterError.substr(0, 80) +
                                "); mlang LSP: formatted buffer");
    }
    else
        editor.setStatusMessage("mlang LSP: formatted buffer");
    return true;
}
#else
bool MlangFormatter::operator()()
{
    editor.setStatusMessage("mlang LSP: not compiled");
    return false;
}
#endif
