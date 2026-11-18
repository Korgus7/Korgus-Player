#pragma once
#include "Common.h"
#include "ThumbCache.h"
#include "FolderTree.h"
#include "FileGrid.h"
#include "PreviewPane.h"
#include "StatusBar.h"
#include "ImageCache.h"
#include "AudioPlayer.h"
#include "PlayerBar.h"
#include "MetaStore.h"
#include "ImageEditor.h"
#include "Transfer.h"
#include "CompareView.h"
#include "VideoView.h"
#include "AudioInfoCache.h"
#include "FullscreenView.h"
#include "FullscreenDoc.h"

namespace kp
{
class MainComponent : public Component, private FocusChangeListener
{
public:
    explicit MainComponent (const String& commandLine);
    ~MainComponent() override;

    PropertiesFile& settings() { return *props.getUserSettings(); }
    void focusGrid() { grid.grabKeyboardFocus(); }

    void paint (Graphics& g) override;
    void resized() override;
    void paintOverChildren (Graphics& g) override;
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
    AudioPlayer player;
    MetaStore meta;
    FolderTree tree;
    FileGrid grid { thumbs, audioInfo };
    PreviewPane preview { thumbs, imageCache, player };
    PlayerBar playerBar { player };
    StatusBar status;
    TextButton upButton, viewButton, settingsButton, dualButton, up2;
    // dva panely (ako Total Commander): druhý panel nahradí náhľad
    FileGrid grid2 { thumbs, audioInfo };
    bool dual = false;
    Component dualHolder;   // len miesto v rozložení
    FileGrid* lastFocused = &grid;
    void setDual (bool on);
    FileGrid& activeGrid() { return dual && lastFocused == &grid2 ? grid2 : grid; }
    FileGrid& otherGrid() { return &activeGrid() == &grid ? grid2 : grid; }
    void openFolderIn (FileGrid& g, const File& folder, const File& toSelect = {});
    void transferToOther (bool move);
    bool moveFilesTo (const Array<File>& files, const File& dir, FileGrid& source);
    void focusChangeNotify();
    void globalFocusChanged (Component*) override;
    void showSettingsMenu();
    void checkForUpdates (bool manual);
    void downloadVideoPlayer();
    bool updateBusy = false;
    TextEditor searchBox;
    ComboBox typeBox, sortBox;
    void applySortBox();
    void setRatingMarked (int stars);
    void batchRenameDialog();
    void findDuplicates();
    void openEditor();
    void printMarked();
    void openCompare();
    void exportList();
    void convertDialog();
    // hľadanie v podpriečinkoch, podobné fotky / zvuky
    void searchDeep (const String& text);
    void findSimilarPhotos();
    void findSimilarSounds (const File& reference);
    std::shared_ptr<std::atomic<bool>> searchCancel;
    std::unique_ptr<AlertWindow> convertWindow;
    std::unique_ptr<CompareView> compare;
    // prenos na pozadí (kopírovanie, presun medzi diskami)
    std::unique_ptr<Transfer> transfer;
    std::function<void (Transfer&)> transferDone;
    struct Poll : Timer { std::function<void()> fn; void timerCallback() override { fn(); } } transferPoll;
    bool startTransfer (std::vector<Transfer::Item> items, bool move, std::function<void (Transfer&)> done);
    void afterMoved (const File& from, const File& to, FileGrid& source);
    void openVideoFullscreen();
    std::unique_ptr<VideoFullscreen> videoFull;
    std::unique_ptr<ImageEditor> editor;
    void pickSlideshowMusic();
    std::unique_ptr<AlertWindow> batchWindow;
    std::unique_ptr<Label> batchPreview;
    void setLabelMarked (int label);
    IconButton slideshowButton { Icon::Slideshow, tr ("Prezentácia obrázkov na celú obrazovku (F5 na celej obrazovke)"), tr ("Prezentácia") };
    void startSlideshow();
    Array<File> audioPlaylist() const;
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
