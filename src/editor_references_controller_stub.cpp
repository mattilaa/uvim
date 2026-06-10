#include "editor_references_controller.h"

EditorReferencesController::EditorReferencesController(Editor& editor)
    : editor(editor)
{
}

void EditorReferencesController::findReferences() {}

void EditorReferencesController::clearReferences() {}

bool EditorReferencesController::selectReference()
{
    return false;
}

void EditorReferencesController::openReferencePreview() {}

void EditorReferencesController::referencesUp() {}

void EditorReferencesController::referencesDown() {}

void EditorReferencesController::referencesHalfPageUp() {}

void EditorReferencesController::referencesHalfPageDown() {}

void EditorReferencesController::referencesFirst() {}

void EditorReferencesController::referencesLast() {}

void EditorReferencesController::toggleReferencesPreview() {}

void EditorReferencesController::drawReferences() {}

bool EditorReferencesController::hasReferences() const
{
    return false;
}
