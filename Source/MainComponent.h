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
#include "TabStrip.h"
#include "VideoExport.h"
#include "VideoView.h"
#include "AudioInfoCache.h"
#include "FullscreenView.h"
#include "FullscreenDoc.h"
#include "Commander.h"
#include "WifiTransfer.h"

namespace kp
{
// posuvník veľkosti náhľadov dole vpravo (malý štvorček – čiara – veľký štvorček)
class SizeSlider : public Component, public SettableTooltipClient
{
public:
    SizeSlider() { setMouseCursor (MouseCursor::PointingHandCursor); setWantsKeyboardFocus (false); }
    std::function<void()> onChange, onEnd;
    double getValue() const { return value; }
    void setValue (double v) { v = jlimit (0.0, 1.0, v); if (v != value) { value = v; repaint(); } }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float cy = r.getCentreY();
        g.setColour (col::textDim);
        g.drawRect (Rectangle<float> (7, 7).withCentre ({ r.getX() + 5, cy }), 1.2f);
        g.drawRect (Rectangle<float> (12, 12).withCentre ({ r.getRight() - 7, cy }), 1.4f);
        const auto t = track();
        g.setColour (col::line.brighter (0.25f));
        g.fillRoundedRectangle (t.withSizeKeepingCentre (t.getWidth(), 4), 2);
        const float x = t.getX() + (float) value * t.getWidth();
        g.setColour (col::accentHi);
        g.fillRoundedRectangle (t.withRight (x).withSizeKeepingCentre (x - t.getX(), 4), 2);
        g.setColour (col::text);
        g.fillEllipse (Rectangle<float> (12, 12).withCentre ({ x, cy }));
    }
    void mouseDown (const MouseEvent& e) override { setFromX (e.position.x); }
    void mouseDrag (const MouseEvent& e) override { setFromX (e.position.x); }
    void mouseUp (const MouseEvent&) override { if (onEnd) onEnd(); }

private:
    double value = 0.5;
    Rectangle<float> track() const { return getLocalBounds().toFloat().reduced (20, 0); }
    void setFromX (float x)
    {
        const auto t = track();
        const double v = jlimit (0.0, 1.0, (double) ((x - t.getX()) / jmax (1.0f, t.getWidth())));
        if (v == value) return;
        value = v;
        repaint();
        if (onChange) onChange();
    }
};

class MainComponent : public Component, private FocusChangeListener
{
public:
    explicit MainComponent (const String& commandLine);
    void openPath (const File& f) { if (f.existsAsFile()) openFolder (f.getParentDirectory(), f); else if (f.isDirectory()) openFolder (f); }   // z druheho spustenia
    ~MainComponent() override;

    PropertiesFile& settings() { return *props.getUserSettings(); }
    void focusGrid() { if (kcMode && commander != nullptr) commander->focusActive(); else grid.grabKeyboardFocus(); }
    void prepareToQuit();   // pri zatváraní: hneď stíšiť prehrávanie

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
    void newFolder();   // novy priecinok v aktualnom priecinku
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
    // horná lišta: ikony, klik otvorí menu
    IconButton upButton { Icon::Up, tr ("O priečinok vyššie (Backspace)") },
               filterButton { Icon::Filter, tr ("Filter typu súborov") },
               sortButton { Icon::Sort, tr ("Triedenie") },
               viewButton { Icon::Grid, tr ("Mriežka / zoznam (V)") },
               dualButton { Icon::Dual, tr ("Dva panely (F9)") },
               settingsButton { Icon::Gear, tr ("Nastavenia") },
               up2 { Icon::Up, tr ("O priečinok vyššie (Backspace)") },
               copyPathButton { Icon::Copy, tr ("Kopírovať cestu (Ctrl+Shift+C)") };
    // Korgus Commander (KC): tlačidlo vľavo hore prepína celé okno
    ModeButton modeButton;
    ModeButton kcFtpButton;
    IconButton kcNetButton  { Icon::Net,   tr ("Sieť – zariadenia a kamery (druhý klik vráti priečinok)") };
    IconButton kcWifiButton { Icon::Phone, tr ("Prenos z mobilu cez Wi-Fi") };
    IconButton kcDiskButton { Icon::Disk,  tr ("Prehľad diskov") };
    IconButton kcViewButton { Icon::Gear, tr ("Možnosti zobrazenia") };
    std::unique_ptr<Commander> commander;
    ThumbCache kcThumbs { 120 };          // náhľady v Korgus Commanderi
    bool kcMode = false;
    void setCommander (bool on);
    std::unique_ptr<DocumentWindow> netWindow;   // prenos z mobilu
    void openNetScanner();
    void openWifi();
    SizeSlider sizeSlider;   // dole: veľkosť náhľadov (v zozname riadkov)
    void updateSizeSlider();
    void showFilterMenu();
    void showSortMenu();
    void showPathMenu (const File& folder);
    Rectangle<int> pathArea() const;
    void copyMarkedPaths();
    void saveViewSizes();
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
    ComboBox typeBox, sortBox;   // neviditeľné: držia výber filtra a triedenia, ovládajú sa z menu
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
    TabStrip tabs;
    void selectTab (int i);
    void addTab();
    void closeTab (int i);
    void saveTabs();
    void openSequencer();
    void audioSettingsDialog();
    // import fotiek z karty / fotoaparátu do priečinkov podľa dátumu
    void importDialog();
    void runImport (const File& source, const File& target, int structure, const String& suffix);
    std::unique_ptr<AlertWindow> importWindow;
    struct DrivePoll : Timer { std::function<void()> fn; void timerCallback() override { fn(); } } drivePoll;
    StringArray knownCards;
    void videoDialog();
    void startVideo (Array<File> imgs, File music, double secs);
    std::unique_ptr<VideoExport> videoExport;
    struct VideoPoll : Timer { std::function<void()> fn; void timerCallback() override { fn(); } } videoPoll;
    std::unique_ptr<AlertWindow> videoWindow;
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
    IconButton slideshowButton { Icon::Slideshow, tr ("Prezentácia") };
    void startSlideshow();
    Array<File> audioPlaylist() const;
    Array<File> audioPlaylistOf (const FileGrid& g) const;
    std::unique_ptr<FullscreenView> fullscreen;
    std::unique_ptr<FullscreenDoc> docFullscreen;
    void openDocFullscreen();
    int nextOfKind (int from, int dir, Kind k) const;

    StretchableLayoutManager layout;
    StretchableLayoutResizerBar bar1 { &layout, 1, true }, bar2 { &layout, 3, true };
    TooltipWindow tooltips { nullptr, 600 };   // vlastné okno ako vo Windows (s tieňom)

    Array<File> targets;
    struct Move { File from, to; };
    using Batch = std::vector<Move>;
    std::vector<Batch> undoStack;
    std::unique_ptr<AlertWindow> renameWindow;
    std::unique_ptr<FileChooser> chooser;
};
}
