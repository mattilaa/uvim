#pragma once

#include <string>

class Editor;

class EditorReferencesController
{
public:
    explicit EditorReferencesController(Editor& editor);

    void findReferences();
    void clearReferences();
    bool selectReference();
    void openReferencePreview();
    void referencesUp();
    void referencesDown();
    void referencesHalfPageUp();
    void referencesHalfPageDown();
    void referencesFirst();
    void referencesLast();
    void toggleReferencesPreview();
    void drawReferences();
    bool hasReferences() const;

private:
    std::string readLineFromFile(const std::string& path, int lineNum);

    Editor& editor;
};
