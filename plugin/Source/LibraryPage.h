#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "kitforge/Librarian.h"

namespace kitforge
{

class KitForgeProcessor;

/** Searchable library of kits, patterns, songs and global settings (SQLite). */
class LibraryPage : public juce::Component, private juce::TableListBoxModel, private juce::ChangeListener
{
public:
    explicit LibraryPage(KitForgeProcessor& p);
    ~LibraryPage() override;
    void resized() override;
    void refreshList();
    void addCurrentKit();

private:
    enum Column { ColFav = 1, ColName, ColKind, ColTags, ColSource, ColAdded };

    // TableListBoxModel
    int getNumRows() override { return (int) items_.size(); }
    void paintRowBackground(juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell(juce::Graphics&, int row, int col, int w, int h, bool selected) override;
    void cellClicked(int row, int col, const juce::MouseEvent&) override;
    void cellDoubleClicked(int row, int col, const juce::MouseEvent&) override;
    void selectedRowsChanged(int lastRow) override;
    void deleteKeyPressed(int) override { removeSelected(); }
    void returnKeyPressed(int) override { loadSelected(); }

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    std::vector<int64_t> selectedIds() const;
    void refreshTags();
    void showDetails();
    void importFiles();
    void importFolder();
    void loadSelected();
    void diffSelected();
    void exportSelected();
    void renameSelected();
    void editTags();
    void toggleFavorite();
    void removeSelected();
    void sendSelected();
    void showMessage(const juce::String& title, const juce::String& text);

    KitForgeProcessor& proc_;
    std::vector<LibraryItem> items_;
    juce::TextEditor search_;
    juce::ComboBox kind_, tag_;
    juce::ToggleButton favs_ { "Favorites only" };
    juce::TableListBox table_;
    juce::TextEditor details_;
    juce::TextButton importFiles_ { "Import files..." }, importFolder_ { "Import SD folder..." }, addKit_ { "Add current kit" },
        load_ { "Load" }, diff_ { "Diff" }, export_ { "Export..." }, rename_ { "Rename..." }, tags_ { "Tags..." },
        fav_ { "Favorite" }, delete_ { "Delete" }, send_ { "Send to device..." };
    juce::Label count_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<juce::AlertWindow> dialog_;
};

} // namespace kitforge
