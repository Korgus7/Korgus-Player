#pragma once
#include "Common.h"
#include "ThumbCache.h"
#include "FolderTree.h"
#include "FileGrid.h"
#include "PreviewPane.h"
#include "StatusBar.h"

namespace kp
{
class MainComponent : public Component
{
public:
    explicit MainComponent (const String& commandLine);
    ~MainComponent() override;

    PropertiesFile& settings() { return *props.getUserSettings(); }
    void focusGrid() { grid.grabKeyboardFocus(); }

    void paint (Graphics& g) override;
    void resized() override;
    bool keyPressed (const KeyPress& key) override;

private:
    void openFolder (const File& folder, const File& toSelect = {});
    void goUp();
    void selectionChanged (const File& f);
    void moveSelectedTo (int slot);
    void undo();
    void setTarget (int slot, const File& dir);
    void chooseTarget (int slot);
    void saveTargets();
    void updateFolderInfo();

    ApplicationProperties props;
    ThumbCache thumbs { 200 };
    FolderTree tree;
    FileGrid grid { thumbs };
    PreviewPane preview { thumbs };
    StatusBar status;
    TextButton upButton;

    StretchableLayoutManager layout;
    StretchableLayoutResizerBar bar1 { &layout, 1, true }, bar2 { &layout, 3, true };
    TooltipWindow tooltips { this, 600 };

    Array<File> targets;
    struct Move { File from, to; };
    std::vector<Move> undoStack;
    std::unique_ptr<FileChooser> chooser;
};
}
