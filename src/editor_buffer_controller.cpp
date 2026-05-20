#include "editor_buffer_controller.h"
#include "editor.h"
#include "editor_git_controller.h"
#include "text_utils.h"

EditorBufferController::EditorBufferController(Editor& editor) : editor(editor)
{
}

void EditorBufferController::createNewBuffer()
{
    editor.createNewBufferImpl();
}

void EditorBufferController::updateCurrentBufferPointers()
{
    editor.updateCurrentBufferPointersImpl();
}

void EditorBufferController::clearCurrentBufferPointers()
{
    editor.clearCurrentBufferPointersImpl();
}

bool EditorBufferController::hasBuffer() const
{
    return editor.hasBufferImpl();
}

void EditorBufferController::ensureBufferForMode(Mode mode)
{
    editor.ensureBufferForModeImpl(mode);
}

void EditorBufferController::switchToBuffer(int index)
{
    editor.switchToBufferImpl(index);
}

void EditorBufferController::nextBuffer()
{
    editor.nextBufferImpl();
}

void EditorBufferController::previousBuffer()
{
    editor.previousBufferImpl();
}

void EditorBufferController::moveBufferLeft()
{
    editor.moveBufferLeftImpl();
}

void EditorBufferController::moveBufferRight()
{
    editor.moveBufferRightImpl();
}

void EditorBufferController::closeCurrentBuffer()
{
    editor.closeCurrentBufferImpl();
}

void EditorBufferController::listBuffers()
{
    editor.listBuffersImpl();
}

int EditorBufferController::findBufferByFilename(const std::string& filename)
{
    return editor.findBufferByFilenameImpl(filename);
}

void EditorBufferController::saveBufferState()
{
    editor.saveBufferStateImpl();
}

void EditorBufferController::restoreBufferState()
{
    editor.restoreBufferStateImpl();
}

void Editor::createNewBufferImpl()
{
    auto buffer = std::make_unique<Buffer>();
    buffers.push_back(std::move(buffer));
    currentBufferIndex = buffers.size() - 1;
    updateCurrentBufferPointers();
    if(splitActive)
    {
        splitPanes[activePane].bufferIndex = currentBufferIndex;
        setPanePointers(activePane);
    }
    needsFullRedraw = true;
}

void Editor::updateCurrentBufferPointersImpl()
{
    if(currentBufferIndex >= 0 && currentBufferIndex < buffers.size())
    {
        currentBuffer = buffers[currentBufferIndex].get();
        lines = &currentBuffer->lines;
        filename = &currentBuffer->filename;
        dirty = &currentBuffer->dirty;
        if(splitActive)
        {
            setPanePointers(activePane);
        }
        else
        {
            cursorX = &currentBuffer->cursorX;
            cursorY = &currentBuffer->cursorY;
            wantedX = &currentBuffer->wantedX;
            offsetX = &currentBuffer->offsetX;
            offsetY = &currentBuffer->offsetY;
        }
    }
    else
    {
        currentBufferIndex = -1;
        clearCurrentBufferPointers();
    }
}

void Editor::clearCurrentBufferPointersImpl()
{
    currentBuffer = nullptr;
    lines = nullptr;
    filename = nullptr;
    dirty = nullptr;
    cursorX = nullptr;
    cursorY = nullptr;
    wantedX = nullptr;
    offsetX = nullptr;
    offsetY = nullptr;
}

bool Editor::hasBufferImpl() const
{
    return currentBuffer != nullptr;
}

void Editor::ensureBufferForModeImpl(Mode mode)
{
    switch(mode)
    {
    case WELCOME:
    case COMMAND:
    case FILE_BROWSER:
    case FUZZY_FIND:
    case BUFFER_BROWSER:
    case GREP_SEARCH:
    case REGEX_SEARCH:
    case REFERENCES:
    case LSP_INFO:
    case LOC_LIST:
    case GIT_STAGE:
    case GIT_COMMIT:
    case GIT_FIXUP:
    case GIT_PATCH:
    case COMMAND_OUTPUT:
        return;
    default:
        break;
    }

    if(!hasBuffer())
    {
        createNewBuffer();
        saveState();
        if(currentBuffer)
            currentBuffer->savedUndoIndex = 0;
    }
}

void Editor::switchToBufferImpl(int index)
{
    if(index >= 0 && index < buffers.size())
    {
        locMessage.clear();
        if(splitActive)
        {
            switchToBufferInActivePane(index);
        }
        else
        {
            saveBufferState();
            currentBufferIndex = index;
            updateCurrentBufferPointers();
            restoreBufferState();
            needsFullRedraw = true;
        }

        // Check if the file has been modified externally
        checkFileChanges();

        std::string msg = "Buffer " + std::to_string(currentBufferIndex + 1) +
                          "/" + std::to_string(buffers.size());
        if(!filename->empty())
        {
            msg += ": " + *filename;
        }
        else
        {
            msg += ": [No Name]";
        }
        if(*dirty)
        {
            msg += " [+]";
        }
        // setStatusMessage(msg);
    }
}

void Editor::nextBufferImpl()
{
    if(buffers.size() > 1)
    {
        int nextIndex = (currentBufferIndex + 1) % buffers.size();
        switchToBuffer(nextIndex);
    }
    else
    {
        setStatusMessage("No other buffers");
    }
}

void Editor::previousBufferImpl()
{
    if(buffers.size() > 1)
    {
        int prevIndex = currentBufferIndex - 1;
        if(prevIndex < 0)
            prevIndex = buffers.size() - 1;
        switchToBuffer(prevIndex);
    }
    else
    {
        setStatusMessage("No other buffers");
    }
}

void Editor::moveBufferLeftImpl()
{
    if(buffers.size() < 2 || currentBufferIndex <= 0)
        return;
    int a = currentBufferIndex - 1;
    int b = currentBufferIndex;
    std::swap(buffers[a], buffers[b]);
    currentBufferIndex = a;
    updateCurrentBufferPointers();
    if(splitActive)
    {
        for(int i = 0; i < 2; i++)
        {
            int& p = splitPanes[i].bufferIndex;
            if(p == a)
                p = b;
            else if(p == b)
                p = a;
        }
    }
    needsFullRedraw = true;
}

void Editor::moveBufferRightImpl()
{
    if(buffers.size() < 2 || currentBufferIndex >= (int)buffers.size() - 1)
        return;
    int a = currentBufferIndex;
    int b = currentBufferIndex + 1;
    std::swap(buffers[a], buffers[b]);
    currentBufferIndex = b;
    updateCurrentBufferPointers();
    if(splitActive)
    {
        for(int i = 0; i < 2; i++)
        {
            int& p = splitPanes[i].bufferIndex;
            if(p == a)
                p = b;
            else if(p == b)
                p = a;
        }
    }
    needsFullRedraw = true;
}

void Editor::closeCurrentBufferImpl()
{
    if(*dirty)
    {
        setStatusMessage("No write since last change (add ! to override)");
        return;
    }

    if(buffers.size() == 1)
    {
        buffers.erase(buffers.begin());
        currentBufferIndex = -1;
        clearCurrentBufferPointers();
        splitActive = false;
        setMode(WELCOME);
    }
    else
    {
        int removedIndex = currentBufferIndex;
        buffers.erase(buffers.begin() + currentBufferIndex);
        if(currentBufferIndex >= buffers.size())
        {
            currentBufferIndex = buffers.size() - 1;
        }
        updateCurrentBufferPointers();
        if(splitActive)
        {
            for(int i = 0; i < 2; i++)
            {
                int& paneIndex = splitPanes[i].bufferIndex;
                if(paneIndex == removedIndex)
                {
                    paneIndex = currentBufferIndex;
                }
                else if(paneIndex > removedIndex)
                {
                    paneIndex -= 1;
                }
            }
            currentBufferIndex = splitPanes[activePane].bufferIndex;
            updateCurrentBufferPointers();
        }
        restoreBufferState();
    }

    needsFullRedraw = true;
}

void Editor::listBuffersImpl()
{
    std::stringstream ss;
    ss << "Buffers: ";

    for(size_t i = 0; i < buffers.size(); i++)
    {
        if(i == currentBufferIndex)
            ss << "[";

        ss << (i + 1) << ":";

        if(!buffers[i]->filename.empty())
        {
            ss << text_utils::basename(buffers[i]->filename);
        }
        else
        {
            ss << "[No Name]";
        }

        if(buffers[i]->dirty)
            ss << "+";

        if(i == currentBufferIndex)
            ss << "]";

        if(i < buffers.size() - 1)
            ss << " ";
    }

    std::string status = ss.str();
    if(gitController)
    {
        if(auto changes = gitController->currentBufferHasChanges();
           changes.has_value())
        {
            status += " | git add ";
            if(*changes)
                status += "current buffer";
            else
                status += "(nothing to add)";
        }
    }
    setStatusMessage(status);
}

int Editor::findBufferByFilenameImpl(const std::string& fname)
{
    for(int i = 0; i < buffers.size(); i++)
    {
        if(buffers[i]->filename == fname)
            return i;
    }
    return -1;
}

void Editor::saveBufferStateImpl()
{
    // State is automatically saved in buffer structure
}

void Editor::restoreBufferStateImpl()
{
    if(currentMode == VISUAL || currentMode == VISUAL_LINE)
    {
        setMode(NORMAL);
    }
}
