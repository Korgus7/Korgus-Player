#pragma once
#include "Common.h"
#include "ThumbCache.h"
#include "FolderTree.h"
#include "FileGrid.h"
#include "PreviewPane.h"
#include "StatusBar.h"
#include "ImageCache.h"
#include "AudioInfoCache.h"
#include "FullscreenView.h"
#include "FullscreenDoc.h"

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
    void deleteMarked();
    void renameSelected();
    void rotateMarked (bool clockwise);
    void resizeDialog();
    void showContextMenu (Point<int> screenPos);
    void fileChanged (const File& f);
    ThreadPool editPool { ThreadPoolOptions{}.withThreadName ("Upravy").withNumberOfThreads (1) };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    std::unique_ptr<AlertWindow> resizeWindow;
    void setTarget (int slot, const File& dir);
    void chooseTarget (int slot);
    void saveTargets();
    void updateFolderInfo();
    void message (const String& m, bool error = false);
    bool handleSortKey (const KeyPress& key);   // 1–9, Ctrl+1–9, Ctrl+Z
    int nextImageIndex (int from, int dir) const;
    void openFullscreen();
    void closeFullscreen();
    void stepFullscreen (int dir);
    void updateViewButton();

    ApplicationProperties props;
    ThumbCache thumbs { 200 };
    ImageCache imageCache;
    AudioInfoCache audioInfo;
    FolderTree tree;
    FileGrid grid { thumbs, audioInfo };
    PreviewPane preview { thumbs, imageCache };
    StatusBar status;
    TextButton upButton, viewButton;
    std::unique_ptr<FullscreenView> fullscreen;
    std::unique_ptr<FullscreenDoc> docFullscreen;
    void openDocFullscreen();
    int nextOfKind (int from, int dir, Kind k) const;

    StretchableLayoutManager layout;
    StretchableLayoutResizerBar bar1 { &layout, 1, true }, bar2 { &layout, 3, true };
    TooltipWindow tooltips { this, 600 };

    Array<File> targets;
    struct Move { File from, to; };
    using Batch = std::vector<Move>;
    std::vector<Batch> undoStack;
    std::unique_ptr<AlertWindow> renameWindow;
    std::unique_ptr<FileChooser> chooser;
};
}
