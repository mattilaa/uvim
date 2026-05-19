#include "editor.h"
#include "editor_drawing_controller.h"
#include "terminal.h"
#include "text_utils.h"
#ifdef UVIM_ENABLE_CLANGD_LSP
#include "lsp_client.h"
#endif

void Editor::drawBufferView()
{
    drawingController->drawBufferView();
}
