#include "mode_state_machine.h"
#include "editor.h"
#include "terminal.h"
#include "text_utils.h"

// ============================================================================
// ModeContext Implementation
// ============================================================================

void ModeContext::setStatusMessage(const std::string& msg)
{
    editor->setStatusMessage(msg);
}

int& ModeContext::cursorX()
{
    return *editor->cursorX;
}

int& ModeContext::cursorY()
{
    return *editor->cursorY;
}

int& ModeContext::offsetX()
{
    return *editor->offsetX;
}

int& ModeContext::offsetY()
{
    return *editor->offsetY;
}

int& ModeContext::wantedX()
{
    return *editor->wantedX;
}

std::vector<std::string>& ModeContext::lines()
{
    return *editor->lines;
}

const std::vector<std::string>& ModeContext::lines() const
{
    return *editor->lines;
}

bool& ModeContext::dirty()
{
    return *editor->dirty;
}

int ModeContext::screenRows() const
{
    return editor->screenRows;
}

int ModeContext::screenCols() const
{
    return editor->screenCols;
}

void ModeContext::requestFullRedraw()
{
    editor->needsFullRedraw = true;
}

void ModeContext::forceFullRedraw()
{
    editor->forceFullRedraw();
}

bool ModeContext::hasBuffer() const
{
    return editor->hasBuffer();
}

bool ModeContext::hasCurrentBuffer() const
{
    return editor->currentBuffer != nullptr;
}

bool ModeContext::hasFilename() const
{
    return editor->filename && !editor->filename->empty();
}

std::string_view ModeContext::currentFilename() const
{
    if(editor->filename)
        return *editor->filename;
    return std::string_view{};
}

Mode ModeContext::currentMode() const
{
    return editor->currentMode;
}

std::chrono::steady_clock::time_point& ModeContext::lastEscTime()
{
    return editor->lastEscTime;
}

bool ModeContext::hasSearchMatches() const
{
    return !editor->searchMatches.empty();
}

bool ModeContext::hasSearchQuery() const
{
    return !editor->searchQuery.empty();
}

bool ModeContext::completionActive() const
{
    return editor->completionActive;
}

bool ModeContext::completionFromLsp() const
{
    return editor->completionFromLsp;
}

bool ModeContext::autoCompletion() const
{
    return editor->autoCompletion;
}

bool ModeContext::autoBraces() const
{
    return editor->autoBraces;
}

int ModeContext::tabSpaces() const
{
    return editor->tabSpaces;
}

bool ModeContext::respectGitignore() const
{
    return editor->respectGitignore;
}

void ModeContext::setRespectGitignore(bool value)
{
    editor->respectGitignore = value;
}

void ModeContext::setFuzzyInitialized(bool value)
{
    editor->fuzzyInitialized = value;
}

void ModeContext::executeCommand(std::string_view cmd)
{
    editor->executeCommand(cmd);
}

bool ModeContext::takeCommandRequest(Mode& mode, std::string& path)
{
    if(!editor->commandRequestedModeSet)
        return false;
    mode = editor->commandRequestedMode;
    path = editor->commandRequestedPath;
    editor->commandRequestedModeSet = false;
    editor->commandRequestedPath.clear();
    return true;
}

std::optional<std::string> ModeContext::commandHistoryUp()
{
    return editor->commandHistoryUp();
}

std::optional<std::string> ModeContext::commandHistoryDown()
{
    return editor->commandHistoryDown();
}

std::vector<std::string> ModeContext::getSetCompletions(std::string_view prefix)
{
    return editor->getSetCompletions(prefix);
}

std::vector<std::string>
ModeContext::getHelpCompletions(std::string_view prefix)
{
    return editor->getHelpCompletions(prefix);
}

void ModeContext::startCommandPopup()
{
    editor->startCommandPopup();
}

void ModeContext::cancelCommandPopup()
{
    editor->cancelCommandPopup();
}

void ModeContext::updateCommandPopup(std::string_view query)
{
    editor->updateCommandPopup(query);
}

void ModeContext::moveCommandPopupCursor(int delta)
{
    editor->moveCommandPopupCursor(delta);
}

bool ModeContext::isCommandPopupActive() const
{
    return editor->isCommandPopupActive();
}

std::optional<std::string> ModeContext::commandPopupSelection() const
{
    return editor->commandPopupSelection();
}

void ModeContext::startCommandHistorySearch(std::string_view seed)
{
    editor->startCommandHistorySearch(seed);
}

std::string ModeContext::cancelCommandHistorySearch()
{
    return editor->cancelCommandHistorySearch();
}

std::string ModeContext::acceptCommandHistorySearch()
{
    return editor->acceptCommandHistorySearch();
}

void ModeContext::updateCommandHistorySearchQuery(std::string_view query)
{
    editor->updateCommandHistorySearchQuery(query);
}

void ModeContext::moveCommandHistorySearchCursor(int delta)
{
    editor->moveCommandHistorySearchCursor(delta);
}

bool ModeContext::isCommandHistorySearchActive() const
{
    return editor->isCommandHistorySearchActive();
}

std::string_view ModeContext::commandHistorySearchQuery() const
{
    return editor->commandHistorySearchQuery();
}

std::vector<std::string>
ModeContext::getCommandCompletions(std::string_view prefix)
{
    return editor->getCommandCompletions(prefix);
}

std::vector<std::string> ModeContext::getPathCompletions(std::string_view path)
{
    return editor->getPathCompletions(path);
}

std::vector<std::string>
ModeContext::getPathCompletionsRecursive(std::string_view path)
{
    return editor->getPathCompletionsRecursive(path);
}

std::vector<std::string>
ModeContext::getLocPathCompletions(std::string_view path)
{
    return editor->getLocPathCompletions(path);
}

void ModeContext::openFile(std::string_view path)
{
    editor->openFile(path);
}

void ModeContext::openFileBrowser(std::string_view path)
{
    editor->openFileBrowser(path);
}

void ModeContext::switchToBuffer(int index)
{
    editor->switchToBuffer(index);
}

void ModeContext::closeCurrentBuffer()
{
    editor->closeCurrentBuffer();
}

void ModeContext::saveFile()
{
    editor->saveFile();
}

void ModeContext::deleteFilePrompt()
{
    editor->deleteFilePrompt();
}

void ModeContext::renameFilePrompt()
{
    editor->renameFilePrompt();
}

void ModeContext::createNewFilePrompt()
{
    editor->createNewFilePrompt();
}

void ModeContext::createNewDirectoryPrompt()
{
    editor->createNewDirectoryPrompt();
}

bool ModeContext::pythonFormatBuffer()
{
    return editor->pythonFormatBuffer();
}

void ModeContext::pythonLintBuffer()
{
    editor->pythonLintBuffer();
}

bool ModeContext::robotFormatBuffer()
{
    return editor->robotFormatBuffer();
}

bool ModeContext::jsonFormatBuffer()
{
    return editor->jsonFormatBuffer();
}

bool ModeContext::yamlFormatBuffer()
{
    return editor->yamlFormatBuffer();
}

bool ModeContext::clangFormatWithArgs(const std::string& extraArgs,
                                      const std::string& successMessage)
{
    return editor->clangFormatWithArgs(extraArgs, successMessage);
}

void ModeContext::clangFormatVisualSelection()
{
    editor->clangFormatVisualSelection();
}

void ModeContext::clangFormatVisualBlockSelection()
{
    editor->clangFormatVisualBlockSelection();
}

void ModeContext::performSearch()
{
    editor->performSearch();
}

void ModeContext::performIncrementalSearch(const std::string& query,
                                           bool forward)
{
    editor->performIncrementalSearch(query, forward);
}

void ModeContext::addSearchToHistory(const std::string& query)
{
    editor->addSearchToHistory(query);
}

std::string ModeContext::getPreviousSearch()
{
    return editor->getPreviousSearch();
}

std::string ModeContext::getNextSearch()
{
    return editor->getNextSearch();
}

void ModeContext::findAllMatches()
{
    editor->findAllMatches();
}

void ModeContext::jumpToMatch(int index)
{
    editor->jumpToMatch(index);
}

void ModeContext::searchNext()
{
    editor->searchNext();
}

void ModeContext::searchPrevious()
{
    editor->searchPrevious();
}

void ModeContext::searchWordUnderCursor(bool forward)
{
    editor->searchWordUnderCursor(forward);
}

void ModeContext::clearSearch()
{
    editor->clearSearch();
}

void ModeContext::nextCompletion()
{
    editor->nextCompletion();
}

void ModeContext::previousCompletion()
{
    editor->previousCompletion();
}

void ModeContext::acceptCompletion()
{
    editor->acceptCompletion();
}

void ModeContext::cancelCompletion()
{
    editor->cancelCompletion();
}

void ModeContext::rebuildCompletionFilter()
{
    editor->rebuildCompletionFilter();
}

void ModeContext::triggerCompletion()
{
    editor->triggerCompletion();
}

void ModeContext::requestCompletion()
{
    editor->requestCompletion();
}

bool ModeContext::shouldTriggerCompletion() const
{
    return editor->shouldTriggerCompletion();
}

void ModeContext::beginChangeRecording(int count)
{
    editor->beginChangeRecording(count);
}

void ModeContext::recordChangeKey(int key)
{
    editor->recordChangeKey(key);
}

void ModeContext::deferChangeRecordingCommit()
{
    editor->deferChangeRecordingCommit();
}

void ModeContext::finishChangeRecordingIfDeferred()
{
    editor->finishChangeRecordingIfDeferred();
}

bool ModeContext::isRecordingChange() const
{
    return editor->isRecordingChange();
}

bool ModeContext::isReplayingChange() const
{
    return editor->isReplayingChange();
}

void ModeContext::cancelChangeRecording()
{
    editor->cancelChangeRecording();
}

void ModeContext::commitChangeRecording()
{
    editor->commitChangeRecording();
}

int ModeContext::readKeyRecorded()
{
    return editor->readKeyRecorded();
}

void ModeContext::repeatLastChange(int times)
{
    editor->repeatLastChange(times);
}

void ModeContext::moveLeft()
{
    editor->moveLeft();
}

void ModeContext::moveRight()
{
    editor->moveRight();
}

void ModeContext::moveUp(int count)
{
    editor->moveUp(count);
}

void ModeContext::moveDown(int count)
{
    editor->moveDown(count);
}

void ModeContext::moveWordForward()
{
    editor->moveWordForward();
}

void ModeContext::moveWordBackward()
{
    editor->moveWordBackward();
}

void ModeContext::moveWordForwardBig()
{
    editor->moveWordForwardBig();
}

void ModeContext::moveWordBackwardBig()
{
    editor->moveWordBackwardBig();
}

void ModeContext::moveToEndOfWord()
{
    editor->moveToEndOfWord();
}

void ModeContext::moveToEndOfWordBig()
{
    editor->moveToEndOfWordBig();
}

void ModeContext::moveToLineStart()
{
    editor->moveToLineStart();
}

void ModeContext::moveToLineEnd()
{
    editor->moveToLineEnd();
}

void ModeContext::moveToFirstLine()
{
    editor->moveToFirstLine();
}

void ModeContext::moveToLastLine()
{
    editor->moveToLastLine();
}

void ModeContext::moveToLine(int line)
{
    editor->moveToLine(line);
}

void ModeContext::moveToFirstNonBlank()
{
    editor->moveToFirstNonBlank();
}

void ModeContext::moveToMatchingBracket()
{
    editor->moveToMatchingBracket();
}

void ModeContext::moveParagraphForward()
{
    editor->moveParagraphForward();
}

void ModeContext::moveParagraphBackward()
{
    editor->moveParagraphBackward();
}

void ModeContext::moveToScreenTop()
{
    editor->moveToScreenTop();
}

void ModeContext::moveToScreenMiddle()
{
    editor->moveToScreenMiddle();
}

void ModeContext::moveToScreenBottom()
{
    editor->moveToScreenBottom();
}

void ModeContext::scrollPageUp()
{
    editor->scrollPageUp();
}

void ModeContext::scrollPageDown()
{
    editor->scrollPageDown();
}

void ModeContext::scrollHalfPageUp()
{
    editor->scrollHalfPageUp();
}

void ModeContext::scrollHalfPageDown()
{
    editor->scrollHalfPageDown();
}

void ModeContext::scrollToTop()
{
    editor->scrollToTop();
}

void ModeContext::scrollToBottom()
{
    editor->scrollToBottom();
}

void ModeContext::centerScreen()
{
    editor->centerScreen();
}

void ModeContext::insertLineAbove()
{
    editor->insertLineAbove();
}

void ModeContext::insertLineBelow()
{
    editor->insertLineBelow();
}

void ModeContext::insertNewline()
{
    editor->insertNewline();
}

void ModeContext::insertTab()
{
    editor->insertTab();
}

void ModeContext::insertChar(char c)
{
    editor->insertChar(c);
}

void ModeContext::insertUtf8Char(int c)
{
    editor->insertUtf8Char(c);
}

void ModeContext::replaceCharAtCursor(char c)
{
    editor->replaceCharAtCursor(c);
}

void ModeContext::deleteCharAtCursor()
{
    editor->deleteCharAtCursor();
}

void ModeContext::deleteCharBeforeCursor()
{
    editor->deleteCharBeforeCursor();
}

void ModeContext::deleteCurrentLine()
{
    editor->deleteCurrentLine();
}

void ModeContext::deleteToEndOfLine()
{
    editor->deleteToEndOfLine();
}

void ModeContext::deleteWordBackward()
{
    editor->deleteWordBackward();
}

void ModeContext::deleteToLineStart()
{
    editor->deleteToLineStart();
}

void ModeContext::joinLines()
{
    editor->joinLines();
}

void ModeContext::toggleCase()
{
    editor->toggleCase();
}

void ModeContext::pasteAfter()
{
    editor->pasteAfter();
}

void ModeContext::pasteBefore()
{
    editor->pasteBefore();
}

void ModeContext::pasteFromSystemClipboard()
{
    editor->pasteFromSystemClipboard();
}

void ModeContext::yankLine()
{
    editor->yankLine();
}

void ModeContext::yankToSystemClipboard()
{
    editor->yankToSystemClipboard();
}

void ModeContext::saveState()
{
    editor->saveState();
}

void ModeContext::deleteSelection()
{
    editor->deleteSelection();
}

void ModeContext::yankSelection()
{
    editor->yankSelection();
}

void ModeContext::deleteVisualBlock()
{
    editor->deleteVisualBlock();
}

void ModeContext::yankVisualBlock()
{
    editor->yankVisualBlock();
}

void ModeContext::indentSelection()
{
    editor->indentSelection();
}

void ModeContext::dedentSelection()
{
    editor->dedentSelection();
}

void ModeContext::autoIndentSelection()
{
    editor->autoIndentSelection();
}

void ModeContext::indentLineSelection()
{
    editor->indentLineSelection();
}

void ModeContext::dedentLineSelection()
{
    editor->dedentLineSelection();
}

void ModeContext::autoIndentLineSelection()
{
    editor->autoIndentLineSelection();
}

void ModeContext::lowercaseSelection()
{
    editor->lowercaseSelection();
}

void ModeContext::uppercaseSelection()
{
    editor->uppercaseSelection();
}

void ModeContext::toggleCaseSelection()
{
    editor->toggleCaseSelection();
}

void ModeContext::yankLineSelection()
{
    editor->yankLineSelection();
}

void ModeContext::deleteLineSelection()
{
    editor->deleteLineSelection();
}

void ModeContext::prepareBlockInsert(bool atEnd)
{
    editor->prepareBlockInsert(atEnd);
}

void ModeContext::swapVisualBlockCorner()
{
    editor->swapVisualBlockCorner();
}

void ModeContext::changeVisualBlock()
{
    editor->changeVisualBlock();
}

bool ModeContext::getTextObjectRange(char objChar, bool around, int& outStartY,
                                     int& outStartX, int& outEndY, int& outEndX)
{
    return editor->getTextObjectRange(objChar, around, outStartY, outStartX,
                                      outEndY, outEndX);
}

void ModeContext::applyOperatorToRange(char op, int startY, int startX,
                                       int endY, int endX)
{
    editor->applyOperatorToRange(op, startY, startX, endY, endX);
}

void ModeContext::handleLinewiseOperator(char op, int count)
{
    editor->handleLinewiseOperator(op, count);
}

void ModeContext::jumpBack()
{
    editor->jumpBack();
}

void ModeContext::jumpForward()
{
    editor->jumpForward();
}

void ModeContext::jumpToAlternateFile()
{
    editor->jumpToAlternateFile();
}

void ModeContext::switchToAlternateFile()
{
    editor->switchToAlternateFile();
}

void ModeContext::setMark(char mark)
{
    editor->setMark(mark);
}

void ModeContext::jumpToMark(char mark)
{
    editor->jumpToMark(mark);
}

void ModeContext::goToDefinition()
{
    editor->goToDefinition();
}

void ModeContext::findReferences()
{
    editor->findReferences();
}

bool ModeContext::hasReferences() const
{
    return editor->hasReferences();
}

void ModeContext::clearReferences()
{
    editor->clearReferences();
}

void ModeContext::referencesUp()
{
    editor->referencesUp();
}

void ModeContext::referencesDown()
{
    editor->referencesDown();
}

void ModeContext::referencesFirst()
{
    editor->referencesFirst();
}

void ModeContext::referencesLast()
{
    editor->referencesLast();
}

void ModeContext::referencesHalfPageUp()
{
    editor->referencesHalfPageUp();
}

void ModeContext::referencesHalfPageDown()
{
    editor->referencesHalfPageDown();
}

void ModeContext::selectReference()
{
    editor->selectReference();
}

void ModeContext::toggleReferencesPreview()
{
    editor->toggleReferencesPreview();
}

void ModeContext::openReferencePreview()
{
    editor->openReferencePreview();
}

void ModeContext::showFileInfo()
{
    editor->showFileInfo();
}

void ModeContext::goToFile()
{
    editor->goToFile();
}

void ModeContext::showLspInfo()
{
    editor->showLspInfo();
}

void ModeContext::clearLspInfo()
{
    editor->clearLspInfo();
}

void ModeContext::forceQuit()
{
    editor->forceQuit();
}

bool ModeContext::isClangdLspEnabled() const
{
    return editor->isClangdLspEnabled();
}

bool ModeContext::isPythonLspEnabled() const
{
    return editor->isPythonLspEnabled();
}

bool ModeContext::isRobotLspEnabled() const
{
    return editor->isRobotLspEnabled();
}

ParsedCommand parseCommandLine(std::string_view commandLine)
{
    while(!commandLine.empty() && text_utils::is_space(commandLine.front()))
    {
        commandLine.remove_prefix(1);
    }

    if(commandLine.empty())
    {
        return {};
    }

    ParsedCommand result;
    size_t spacePos = commandLine.find(' ');
    if(spacePos != std::string_view::npos)
    {
        result.cmd = std::string(commandLine.substr(0, spacePos));
        std::string_view argsView = commandLine.substr(spacePos + 1);
        while(!argsView.empty() && text_utils::is_space(argsView.front()))
        {
            argsView.remove_prefix(1);
        }
        result.args = std::string(argsView);
    }
    else
    {
        result.cmd = std::string(commandLine);
    }

    return result;
}

std::optional<ModeState>
dispatchCommandLine(ModeContext& ctx, std::string_view commandLine,
                    const ModeCommandCallback& modeHandler,
                    const CommandFallback& fallbackHandler)
{
    ParsedCommand command = parseCommandLine(commandLine);
    if(command.cmd.empty())
    {
        return std::nullopt;
    }

    std::optional<ModeState> nextState;
    if(modeHandler && modeHandler(ctx, command, nextState))
    {
        return nextState;
    }

    if(fallbackHandler)
    {
        return fallbackHandler(ctx, commandLine);
    }

    return std::nullopt;
}

std::optional<ModeState> dispatchEditorCommand(ModeContext& ctx,
                                               std::string_view commandLine,
                                               std::string_view previousFile,
                                               bool returnToNormalIfBuffer)
{
    ctx.executeCommand(commandLine);

    if(ctx.currentMode() == LSP_INFO)
    {
        return LspInfoMode{};
    }
    if(ctx.editor && ctx.editor->getModeStateMachine())
    {
        const ModeState& state =
            ctx.editor->getModeStateMachine()->state();
        if(std::holds_alternative<GitLogMode>(state))
            return std::get<GitLogMode>(state);
        if(std::holds_alternative<GitShowCommitMode>(state))
            return std::get<GitShowCommitMode>(state);
        if(std::holds_alternative<GitStageMode>(state))
            return std::get<GitStageMode>(state);
        if(std::holds_alternative<GitCommitMode>(state))
            return std::get<GitCommitMode>(state);
        if(std::holds_alternative<GitFixupMode>(state))
            return std::get<GitFixupMode>(state);
        if(std::holds_alternative<GitPatchMode>(state))
            return std::get<GitPatchMode>(state);
    }

    Mode mode = NORMAL;
    std::string path;
    if(ctx.takeCommandRequest(mode, path))
    {
        if(mode == FILE_BROWSER)
        {
            return FileBrowserMode{path, std::string(previousFile)};
        }
        if(mode == LSP_INFO)
        {
            return LspInfoMode{};
        }
        if(mode == LOC_LIST)
        {
            return LocListMode{};
        }
        if(mode == HELP)
        {
            return HelpMode{path, std::string(previousFile)};
        }
        if(mode == GIT_STAGE)
        {
            GitStageMode stage;
            stage.returnMode = ctx.editor->commandRequestedReturnMode;
            if(stage.returnMode.has_value() &&
               stage.returnMode.value() == FILE_BROWSER)
            {
                stage.returnBrowseCursor =
                    ctx.editor->commandRequestedBrowseCursor;
                stage.returnBrowseOffset =
                    ctx.editor->commandRequestedBrowseOffset;
                stage.returnBrowseDirectory =
                    ctx.editor->commandRequestedBrowseDirectory;
            }
            ctx.editor->commandRequestedReturnMode.reset();
            ctx.editor->commandRequestedBrowseCursor = 0;
            ctx.editor->commandRequestedBrowseOffset = 0;
            ctx.editor->commandRequestedBrowseDirectory.clear();
            return stage;
        }
        if(mode == GIT_COMMIT)
        {
            return GitCommitMode{};
        }
    }

    if(returnToNormalIfBuffer && ctx.hasBuffer())
    {
        return NormalMode{};
    }

    return std::nullopt;
}

bool CommandPrompt::isActive() const
{
    return active;
}

const std::string& CommandPrompt::getInput() const
{
    return input;
}

void CommandPrompt::setInput(std::string value)
{
    input = std::move(value);
    completions.clear();
    completionIndex = -1;
    originalInput.clear();
}

bool CommandPrompt::handle(
    ModeContext& ctx, int key,
    const std::function<std::optional<ModeState>(std::string_view)>& execute,
    std::optional<ModeState>& nextState)
{
    Editor* ed = ctx.editor;
    auto clearCompletions = [&]()
    {
        completions.clear();
        completionIndex = -1;
        originalInput.clear();
    };

    auto handleTabCompletion = [&](bool reverse)
    {
        std::string inputText = input;
        bool wholeCompletion = false;
        bool helpCompletion = false;
        bool locCompletion = false;
        std::string locCommand;

        if(completions.empty())
        {
            originalInput = inputText;
            locCompletion = false;
            locCommand.clear();

            if(inputText.rfind("set", 0) == 0)
            {
                completions = ctx.getSetCompletions(inputText);
                wholeCompletion = true;
            }
            else
            {
                size_t spacePos = inputText.find(' ');
                if(spacePos != std::string::npos)
                {
                    std::string cmd = inputText.substr(0, spacePos);
                    std::string_view pathPart =
                        std::string_view(inputText).substr(spacePos + 1);
                    while(!pathPart.empty() &&
                          (pathPart.front() == ' ' ||
                           pathPart.front() == '\t'))
                    {
                        pathPart.remove_prefix(1);
                    }

                    if(cmd == "help" || cmd == "h")
                    {
                        completions = ctx.getHelpCompletions(pathPart);
                        helpCompletion = true;
                    }
                    else if(cmd == "e" || cmd == "edit" || cmd == "tabe" ||
                            cmd == "tabnew" || cmd == "w" || cmd == "cd")
                    {
                        completions = ctx.getPathCompletionsRecursive(pathPart);
                    }
                    else if(cmd == "loc" || cmd == "loc!" || cmd == "loctotal")
                    {
                        completions = ctx.getLocPathCompletions(pathPart);
                        if(completions.empty())
                            completions =
                                ctx.getPathCompletionsRecursive(pathPart);
                        locCompletion = true;
                        locCommand = cmd;
                    }
                    else if(cmd == "git")
                    {
                        if(pathPart.empty() ||
                           std::string_view("stage").rfind(pathPart, 0) == 0)
                            completions.push_back("stage");
                        if(pathPart.empty() ||
                           std::string_view("log").rfind(pathPart, 0) == 0)
                            completions.push_back("log");
                        if(pathPart.empty() ||
                           std::string_view("stash").rfind(pathPart, 0) == 0)
                            completions.push_back("stash");
                        if(pathPart.empty() ||
                           std::string_view("stash pop").rfind(pathPart, 0) == 0)
                            completions.push_back("stash pop");
                    }
                }
                else
                {
                    if(inputText == "help" || inputText == "h")
                    {
                        completions = ctx.getHelpCompletions("");
                        helpCompletion = true;
                    }
                    else if(inputText == "loc" || inputText == "loc!" ||
                            inputText == "loctotal")
                    {
                        completions = ctx.getLocPathCompletions("");
                        if(completions.empty())
                            completions =
                                ctx.getPathCompletionsRecursive("");
                        locCompletion = true;
                        locCommand = inputText;
                    }
                    else if(inputText == "e" || inputText == "edit" ||
                            inputText == "tabe" || inputText == "tabnew" ||
                            inputText == "w" || inputText == "cd")
                    {
                        completions = ctx.getPathCompletionsRecursive("");
                    }
                    else
                    {
                        completions = ctx.getCommandCompletions(inputText);
                    }
                }
            }

        if(completions.empty())
            return;

        }

        if(reverse)
        {
            completionIndex--;
            if(completionIndex < 0)
                completionIndex = (int)completions.size() - 1;
        }
        else
        {
            completionIndex++;
            if(completionIndex >= (int)completions.size())
                completionIndex = 0;
        }

        if(wholeCompletion || originalInput.rfind("set", 0) == 0)
        {
            input = completions[completionIndex];
            return;
        }

        if(helpCompletion || originalInput.rfind("help", 0) == 0 ||
           originalInput.rfind("h", 0) == 0)
        {
            std::string cmd = (originalInput.rfind("h", 0) == 0 &&
                               originalInput.rfind("help", 0) != 0)
                                  ? "h"
                                  : "help";
            input = cmd + " " + completions[completionIndex];
            return;
        }

        size_t spacePos = originalInput.find(' ');
        if(locCompletion)
        {
            input = locCommand + " " + completions[completionIndex];
            return;
        }
        if(spacePos != std::string::npos)
        {
            std::string cmd = originalInput.substr(0, spacePos);
            input = cmd + " " + completions[completionIndex];
            return;
        }

        input = completions[completionIndex];
    };

    auto updatePopup = [&]()
    {
        if(input.empty())
        {
            if(!ctx.isCommandPopupActive())
                ctx.startCommandPopup();
            ctx.updateCommandPopup("");
            return;
        }

        bool isSetQuery = input.rfind("set", 0) == 0;
        bool isHelpQuery = input == "help" || input == "h" ||
                           input.rfind("help ", 0) == 0 ||
                           input.rfind("h ", 0) == 0;
        bool isGitQuery = input == "git" || input.rfind("git ", 0) == 0;
        if(input.find(' ') != std::string::npos && !isSetQuery &&
           !isHelpQuery && !isGitQuery)
        {
            ctx.cancelCommandPopup();
            return;
        }

        if(!ctx.isCommandPopupActive())
            ctx.startCommandPopup();
        ctx.updateCommandPopup(input);
    };

    if(!active)
    {
        if(key == ':')
        {
            active = true;
            input.clear();
            clearCompletions();
            updatePopup();
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }
        return false;
    }

    if(ctx.isCommandPopupActive())
    {
        if(key == Terminal::CTRL_K)
        {
            ctx.moveCommandPopupCursor(-1);
            if(auto selection = ctx.commandPopupSelection())
                input = *selection;
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }
        if(key == Terminal::CTRL_J)
        {
            ctx.moveCommandPopupCursor(1);
            if(auto selection = ctx.commandPopupSelection())
                input = *selection;
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }
    }

    if(ctx.isCommandHistorySearchActive())
    {
        if(key == Terminal::ESC)
        {
            input = ctx.cancelCommandHistorySearch();
            clearCompletions();
            updatePopup();
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }

        if(key == Terminal::ENTER)
        {
            input = ctx.acceptCommandHistorySearch();
            clearCompletions();
            updatePopup();
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }

        if(key == Terminal::CTRL_J || key == Terminal::ARROW_DOWN)
        {
            ctx.moveCommandHistorySearchCursor(1);
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }

        if(key == Terminal::CTRL_K || key == Terminal::ARROW_UP)
        {
            ctx.moveCommandHistorySearchCursor(-1);
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }

        if(key == Terminal::BACKSPACE || key == Terminal::DEL ||
           key == Terminal::CTRL_H)
        {
            std::string query(ctx.commandHistorySearchQuery());
            if(!query.empty())
            {
                query.pop_back();
                ctx.updateCommandHistorySearchQuery(query);
                input = query;
                clearCompletions();
                updatePopup();
                ed->needsFullRedraw = true;
            }
            nextState.reset();
            return true;
        }

        if(key == Terminal::CTRL_U)
        {
            ctx.updateCommandHistorySearchQuery("");
            input.clear();
            clearCompletions();
            updatePopup();
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }

        if(key >= 32 && key < 127)
        {
            std::string query(ctx.commandHistorySearchQuery());
            query += static_cast<char>(key);
            ctx.updateCommandHistorySearchQuery(query);
            input = query;
            clearCompletions();
            updatePopup();
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }

        nextState.reset();
        return true;
    }

    if(key == Terminal::ESC)
    {
        active = false;
        input.clear();
        clearCompletions();
        ctx.cancelCommandPopup();
        ed->needsFullRedraw = true;
        nextState.reset();
        return true;
    }

    if(key == Terminal::ENTER)
    {
        if(ctx.isCommandPopupActive())
        {
            if(auto selection = ctx.commandPopupSelection())
            {
                if(input.find(' ') == std::string::npos ||
                   selection->find(' ') != std::string::npos)
                {
                    input = *selection;
                }
            }
        }
        nextState = execute(input);
        active = false;
        input.clear();
        clearCompletions();
        ctx.cancelCommandPopup();
        ed->needsFullRedraw = true;
        return true;
    }

    if(key == Terminal::BACKSPACE || key == Terminal::DEL)
    {
        if(!input.empty())
        {
            input.pop_back();
            clearCompletions();
            updatePopup();
            ed->needsFullRedraw = true;
        }
        nextState.reset();
        return true;
    }

        if(key == Terminal::TAB)
        {
            handleTabCompletion(false);
            updatePopup();
            ed->needsFullRedraw = true;
            nextState.reset();
            return true;
        }

    if(key == Terminal::SHIFT_TAB)
    {
        handleTabCompletion(true);
        updatePopup();
        ed->needsFullRedraw = true;
        nextState.reset();
        return true;
    }

    if(key == Terminal::CTRL_F)
    {
        ctx.startCommandHistorySearch(input);
        clearCompletions();
        ctx.cancelCommandPopup();
        ed->needsFullRedraw = true;
        nextState.reset();
        return true;
    }

    if(key == Terminal::CTRL_K || key == Terminal::ARROW_UP)
    {
        if(auto cmd = ctx.commandHistoryUp())
        {
            input = *cmd;
            clearCompletions();
            ctx.cancelCommandPopup();
            ed->needsFullRedraw = true;
        }
        nextState.reset();
        return true;
    }

    if(key == Terminal::CTRL_J || key == Terminal::ARROW_DOWN)
    {
        if(auto cmd = ctx.commandHistoryDown())
        {
            input = *cmd;
            clearCompletions();
            ctx.cancelCommandPopup();
            ed->needsFullRedraw = true;
        }
        nextState.reset();
        return true;
    }

    if(key >= 32 && key < 127)
    {
        if(key == '/')
        {
            auto isPathCmd = [&]() -> bool
            {
                return input.rfind("e", 0) == 0 ||
                       input.rfind("edit", 0) == 0 ||
                       input.rfind("tabe", 0) == 0 ||
                       input.rfind("tabnew", 0) == 0 ||
                       input.rfind("w", 0) == 0 ||
                       input.rfind("cd", 0) == 0 ||
                       input.rfind("loc", 0) == 0 ||
                       input.rfind("loctotal", 0) == 0;
            };
            if(isPathCmd() && !input.empty() && input.back() == '/')
            {
                nextState.reset();
                return true;
            }
        }
        input += static_cast<char>(key);
        clearCompletions();
        updatePopup();
        ed->needsFullRedraw = true;
        nextState.reset();
        return true;
    }

    nextState.reset();
    return true;
}

// ============================================================================
// ModeStateMachine Implementation
// ============================================================================
//
// Note: ModeStateMachine now inherits from StateMachine<ModeState, ModeContext,
// KeyEvent> so all the dispatch/transition logic is provided by the base class
// template. Only the createModeContext helper needs implementation here.
//
// The constructors are defined inline in the header using the base class.
//

// ============================================================================
// Helper: Create context from Editor
// ============================================================================

ModeContext createModeContext(Editor* editor)
{
    return ModeContext{
        .editor = editor,
        .commandBuffer = editor->commandBuffer,
        .repeatCount = editor->repeatCount,
        .pendingOperator = editor->pendingOperator,
        .pendingAwaitingObject = editor->pendingAwaitingObject,
        .pendingObjectType = editor->pendingObjectType,
        .pendingCount = editor->pendingCount,
        .searchQuery = editor->searchQuery,
        .searchForward = editor->searchForward,
        .savedCursorX = editor->savedCursorX,
        .savedCursorY = editor->savedCursorY,
    };
}

ModeState defaultExitMode(const Editor* editor)
{
    if(!editor || !editor->hasBuffer())
        return WelcomeMode{};
    return NormalMode{};
}
