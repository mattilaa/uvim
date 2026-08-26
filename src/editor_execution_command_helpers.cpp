#include "editor_execution_command_helpers.h"
#include "editor.h"
#include "editor_path_utilities.h"
#include "terminal.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace command::execution::detail
{
void requestMode(Editor& editor, Mode mode, std::string path)
{
    editor.commandRequestedModeSet = true;
    editor.commandRequestedMode = mode;
    editor.commandRequestedPath = std::move(path);
}

bool hasDirtyBuffers(const Editor& editor)
{
    for(const auto& buffer : editor.buffers)
    {
        if(buffer && buffer->dirty)
            return true;
    }
    return false;
}

void clearAndExit()
{
    Terminal::clearScreen();
    std::exit(0);
}

bool saveAllBuffers(Editor& editor, bool forceExit)
{
    int savedCount = 0;
    int skippedNoName = 0;
    int currentBuf = editor.currentBufferIndex;

    for(size_t i = 0; i < editor.buffers.size(); i++)
    {
        if(!editor.buffers[i] || !editor.buffers[i]->dirty)
            continue;

        if(editor.buffers[i]->filename.empty())
        {
            skippedNoName++;
            continue;
        }
        editor.switchToBuffer((int)i);
        editor.saveFile();
        savedCount++;
    }

    editor.switchToBuffer(currentBuf);

    if(!forceExit)
    {
        if(skippedNoName > 0)
            editor.setStatusMessage("Saved " + std::to_string(savedCount) +
                                    " buffer(s), " +
                                    std::to_string(skippedNoName) + " unnamed");
        else
            editor.setStatusMessage("Saved " + std::to_string(savedCount) +
                                    " buffer(s)");
    }

    return skippedNoName == 0;
}

bool openEditTarget(Editor& editor, std::string path)
{
    if(path == ".")
    {
        requestMode(editor, FILE_BROWSER, ".");
        return true;
    }

    std::error_code ec;
    if(std::filesystem::is_directory(path, ec) && !ec)
    {
        requestMode(editor, FILE_BROWSER, path);
        return true;
    }

    editor.openFile(path);
    editor.setMode(NORMAL);
    return true;
}

bool openTabTarget(Editor& editor, std::string_view cmd)
{
    std::string fname;
    if(cmd.rfind("tabe ", 0) == 0 && cmd.length() > 5)
        fname = std::string(cmd.substr(5));
    else if(cmd.rfind("tabnew ", 0) == 0 && cmd.length() > 7)
        fname = std::string(cmd.substr(7));

    if(!fname.empty())
        editor.openFile(fname);
    else
    {
        editor.createNewBuffer();
        editor.setStatusMessage("New buffer created");
    }
    return true;
}

bool setCwd(Editor& editor, std::string path)
{
    if(path.empty())
        path = EditorPathUtilities::homeDirectory().string();

    std::string displayPath;
    std::string errorMessage;
    if(EditorPathUtilities::setWorkingDirectory(path, displayPath,
                                                errorMessage))
    {
        editor.followWorkingDirectoryInFileBrowser();
        editor.setStatusMessage(displayPath);
    }
    else
        editor.setStatusMessage("Cannot change to: " + path + " (" +
                                errorMessage + ")");
    return true;
}
} // namespace command::execution::detail
