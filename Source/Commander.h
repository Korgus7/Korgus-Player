#pragma once
#include "Common.h"
#include "Transfer.h"
#include "Icons.h"
#include "Ftp.h"
#include "Archive.h"
#include "NetScanner.h"
#include <set>
#include <map>

namespace kp
{
// Prepínač vľavo hore: [ Player | Commander ] – zvýraznená časť = kde práve si, klik na druhú prepne
class ModeSwitch : public Component, public TooltipClient
{
public:
    ModeSwitch() { setMouseCursor (MouseCursor::PointingHandCursor); setWantsKeyboardFocus (false); }
    std::function<void (bool commander)> onChange;
    void setCommanderMode (bool c) { if (kc != c) { kc = c; repaint(); } }
    static int idealWidth() { return 170; }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (col::panelHi);
        g.fillRoundedRectangle (r, 3.0f);
        auto half = [&] (bool right) { return right ? r.withTrimmedLeft (r.getWidth() * splitAt()) : r.withWidth (r.getWidth() * splitAt()); };
        const auto on = half (kc);
        g.setColour (hover >= 0 && (hover == 1) != kc ? col::accentHi : col::accent);
        g.fillRoundedRectangle (on, 3.0f);
        if (hover >= 0 && (hover == 1) != kc)
        {
            g.setColour (col::panelHi.brighter (0.15f));
            g.fillRoundedRectangle (half (hover == 1), 3.0f);
        }
        g.setColour (col::line.brighter (0.35f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setFont (FontOptions (13.5f));
        g.setColour (kc ? col::text : col::selText);
        g.drawText ("Player", half (false), Justification::centred, false);
        g.setColour (kc ? col::selText : col::text);
        g.drawText ("Commander", half (true), Justification::centred, false);
    }
    void mouseMove (const MouseEvent& e) override { setHover (sideAt (e.x)); }
    void mouseExit (const MouseEvent&) override { setHover (-1); }
    void mouseDown (const MouseEvent& e) override
    {
        const bool c = sideAt (e.x) == 1;
        if (c != kc && onChange) onChange (c);
    }
    String getTooltip() override
    {
        const auto p = getMouseXYRelative();
        return sideAt (p.x) == 1 ? tr ("Korgus Commander – dva panely na prácu so súbormi")
                                 : tr ("Korgus Player – náhľady, fotky, hudba a video");
    }

private:
    bool kc = false;
    int hover = -1;
    float splitAt() const { return 0.42f; }
    int sideAt (int x) const { return x >= roundToInt (getWidth() * splitAt()) ? 1 : 0; }
    void setHover (int h) { if (hover != h) { hover = h; repaint(); } }
};

// Tlačidlo s textom (FTP v hornej lište Commandera)
class ModeButton : public Button
{
public:
    ModeButton() : Button ("mode")
    {
        setMouseCursor (MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (false);
    }
    void setText (const String& t) { text = t; repaint(); }
    void setSubtle (bool s) { subtle = s; repaint(); }     // tlmené tlačidlo v hornej lište
    void setLit (bool l) { if (lit != l) { lit = l; repaint(); } }
    void setFontSize (float f) { fontSize = f; repaint(); }

    void paintButton (Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool strong = ! subtle || lit;
        const Colour base = strong ? col::accent : col::panelHi;
        g.setColour (down ? col::accentHi : (over ? (strong ? col::accentHi.withAlpha (0.85f) : col::panelHi.brighter (0.15f)) : base));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (col::line.brighter (0.35f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (strong ? col::selText : col::text);
        g.setFont (FontOptions (fontSize, Font::bold));
        g.drawText (text, r, Justification::centred, false);
    }

private:
    String text { "KC" };
    bool subtle = false, lit = false;
    float fontSize = 14.0f;
};

class Commander;

// Jeden panel: lišta diskov, cesta, hlavička stĺpcov, zoznam, súčet dole
class CmdPanel : public Component, public DragAndDropTarget, private ScrollBar::Listener
{
public:
    struct Entry { File file; String name, ext; bool dir = false, up = false; int64 size = 0; Time mod; Colour colour; bool hidden = false; String rpath; int hostIdx = -1; };

    explicit CmdPanel (Commander& owner);
    ~CmdPanel() override;

    void setFolder (const File& folder, const File& select = {});
    const File& getFolder() const { return folder; }
    void reload();
    void poll();                          // zmena na disku / nové disky
    Array<File> selection() const;        // označené, inak súbor pod kurzorom
    File currentFile() const;
    void clearMarks() { marked.clear(); repaint(); }
    void selectFile (const File& f);
    void markByMask (bool mark);          // Num+ / Num−
    void invertMarks();                   // Num*
    void markAll();
    void renameInPlace();                 // Shift+F6
    void showDriveMenu();                 // Alt+F1 / Alt+F2

    // FTP: panel ukazuje priečinok na serveri
    bool isRemote() const { return ftp != nullptr; }
    void setRemote (std::shared_ptr<FtpSession> s, const String& dir, const String& selectName = {});
    std::shared_ptr<FtpSession> session() const { return ftp; }
    void disconnectFtp();                 // Ctrl+Shift+F alebo × pri FTP v lište diskov
    const String& remoteDir() const { return rdir; }
    std::vector<Entry> selectedEntries() const;
    const Entry* currentEntry() const;
    bool hasName (const String& n) const;
    String displayPath() const;

    // archív ako priečinok
    bool isArchive() const { return arc != File(); }
    const File& archiveFile() const { return arc; }
    const String& archiveDir() const { return adir; }
    void setArchive (const File& a, const String& dir, const String& selectName = {});
    bool isVirtual() const { return ftp != nullptr || arc != File() || net; }

    // Sieť: zariadenia v sieti a ich zdieľané priečinky
    bool isNet() const { return net; }
    bool isNetHostList() const { return net && netIp.isEmpty(); }
    void setNet (const String& hostIp = {});
    void fillNet();
    int netHostIndex() const;

    // karty (Ctrl+T, Ctrl+W, Ctrl+Tab)
    void newTab();
    void closeTab();
    void nextTab (int dir);
    void selectTab (int i);
    StringArray getTabs() const { return tabs; }
    int getCurTab() const { return curTab; }
    void setTabs (const StringArray& t, int cur);

    // náhľady obrázkov (Ctrl+Shift+F1), zoznam (Ctrl+F1 / Ctrl+F2)
    void setThumbMode (bool on);
    bool isThumbMode() const { return thumbs; }
    // Shift+F2: označiť súbory, ktoré sú iné ako v druhom paneli
    int markDiff (const CmdPanel& other);

    // Ctrl+B: všetky súbory z podpriečinkov v jednom zozname
    void toggleFlat();
    bool isFlat() const { return flat; }

    bool active = false;

    void paint (Graphics& g) override;
    void resized() override;
    bool keyPressed (const KeyPress& key) override;
    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;
    void mouseUp (const MouseEvent& e) override;
    void mouseDoubleClick (const MouseEvent& e) override;
    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override;
    void mouseMove (const MouseEvent& e) override;
    void focusGained (FocusChangeType) override;

    bool isInterestedInDragSource (const SourceDetails& d) override;
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails& d) override;

private:
    Commander& owner;
    File folder;
    std::vector<Entry> entries;
    std::set<String> marked;              // podľa názvu (prežije obnovenie)
    int cursor = 0, top = 0;
    int sortCol = 0; bool sortDesc = false;
    Time folderTime;
    std::map<String, File> lastPerDrive;  // posledný priečinok na každom disku
    Array<File> drives;
    ScrollBar scroll { true };
    std::unique_ptr<TextEditor> renameEd;
    bool dropHover = false;
    std::shared_ptr<FtpSession> ftp;
    std::shared_ptr<FtpSession> keep;     // spojenie ostane, aj keď sa panel prepne na disk
    String rdir, keepDir, pendingSelect;
    int listGen = 0;
    bool loading = false;
    void fillRemote (const std::vector<FtpEntry>& v);
    File arc;
    String adir;
    std::vector<ArcItem> arcItems;
    void fillArchive();
    std::map<String, int64> dirSizes;     // medzerník na priečinku
    void calcDirSize (int i);
    StringArray tabs;
    int curTab = 0;
    bool flat = false;
    bool thumbs = false;
    bool net = false;
    String netIp;
    void netColumns (Rectangle<int> row, Rectangle<int>& n, Rectangle<int>& ip, Rectangle<int>& ven, Rectangle<int>& svc) const;
    bool rightDragCandidate = false;
    String qsearch;                       // rýchle hľadanie písaním
    bool quickSearch (const String& text, int from = 0, int dir = 1);
    int separatorAt (Point<int> p) const;  // -1 alebo 0..2 (deliaca čiarka v hlavičke)
    int colDrag = -1, colDragStartX = 0, colDragStartW = 0;

    // pravé tlačidlo: označovanie, podržanie = ponuka
    bool rightDown = false, markMode = true, menuShown = false;
    int rightAnchor = -1, rightLast = -1;
    struct Hold : Timer { std::function<void()> fn; void timerCallback() override { stopTimer(); fn(); } } hold;
    bool dragStarted = false;

    static constexpr int driveH = 26, pathH = 22, headH = 20, footH = 20;
public:
    int rowH = 20;                        // podľa veľkosti písma (Vzhľad)
private:
    static constexpr int tabH = 22;
    int topOff() const { return tabs.size() > 1 ? tabH : 0; }
    Rectangle<int> tabArea() const { return getLocalBounds().removeFromTop (topOff()); }
    Rectangle<int> driveArea() const { return getLocalBounds().withTrimmedTop (topOff()).removeFromTop (driveH); }
    Rectangle<int> pathArea() const { return getLocalBounds().withTrimmedTop (topOff() + driveH).removeFromTop (pathH); }
    Rectangle<int> copyPathArea() const { return pathArea().removeFromLeft (pathH + 4); }
    Rectangle<int> netEditArea() const { return net ? pathArea().removeFromRight (pathH + 6) : Rectangle<int>(); }
    Rectangle<int> headArea() const { return getLocalBounds().withTrimmedTop (topOff() + driveH + pathH).removeFromTop (headH); }
    std::vector<Rectangle<int>> tabRects() const;
    Rectangle<int> listArea() const;
    Rectangle<int> footArea() const { return getLocalBounds().removeFromBottom (footH); }
    void columns (Rectangle<int> row, Rectangle<int>& name, Rectangle<int>& ext, Rectangle<int>& size, Rectangle<int>& date) const;
    struct DriveBtn { Rectangle<int> r; File d; int kind; };   // 0 disk, 1 „\“, 2 FTP, 3 odpojiť, 4 sieť
    std::vector<DriveBtn> driveButtons() const;
    int visibleRows() const;               // počet viditeľných položiek (aj v náhľadoch)
    int gridCols() const;
    Rectangle<int> itemRect (int i) const;
    int rowAt (Point<int> p) const;
    void setCursor (int i);
    void ensureVisible();
    void updateScroll();
    void scrollBarMoved (ScrollBar*, double newStart) override;
    void sortEntries();
    void openCurrent();
    void goUp();
    void toggleMark (int i);
    bool isMarked (int i) const { return i >= 0 && i < (int) entries.size() && ! entries[(size_t) i].up && marked.count (entries[(size_t) i].name) > 0; }
    void contextMenu();
    void refreshDrives();
};

// Korgus Commander: dva panely a lišta F3–F8 (ovládanie ako v Total Commanderi)
class Commander : public Component, public DragAndDropContainer, private Timer
{
public:
    explicit Commander (PropertiesFile& settings);
    ~Commander() override;

    void start();                         // pri zobrazení: priečinky z nastavení
    void saveState();
    void focusActive();
    std::function<void (const File&)> onView;   // F3 → zobraziť v Playeri
    void showViewMenu (Component* target);       // možnosti zobrazenia
    bool showHidden = false, showSystem = false;

    void paint (Graphics& g) override;
    void resized() override;
    void mouseDown (const MouseEvent& e) override;
    bool keyPressed (const KeyPress& key) override;
    void visibilityChanged() override;

    // pre panely
    bool handleKey (const KeyPress& key);
    void setActive (CmdPanel& p);
    CmdPanel& activePanel() { return *act; }
    CmdPanel& otherPanel() { return act == &left ? right : left; }
    void dropped (CmdPanel& from, CmdPanel& to, bool move);
    void message (const String& m, bool error = false);
    void dialog (const String& title, const String& text, const String* edit, const StringArray& buttons,
                 std::function<void (int, String)> done);
    void openFile (const File& f);
    void viewCurrent();
    void editCurrent();
    void copyOrMove (bool move);
    void makeFolder();
    void deleteSelection();
    bool busy() const { return listWin != nullptr || transfer != nullptr || ftpJob != nullptr || task != nullptr || dialogWin != nullptr || ftpMgr != nullptr; }
    void fetchArchive (const CmdPanel::Entry& e, const File& arcFile, bool view);
    void packZip();                       // Alt+F5
    void unpack();                        // Alt+F9
    void startTask (const String& label, std::function<void (TaskJob&)> work, std::function<void (TaskJob&)> done);

    // doplnky
    std::function<Component*()> quickPreview;          // náhľad z Playera (Ctrl+Q)
    std::function<void (const File&)> quickShow;
    std::function<void (Component*)> quickDone;
    void toggleQuick();
    bool isQuick() const { return quickComp != nullptr; }
    void cursorMoved (CmdPanel& p);
    void favMenu();                       // Ctrl+D
    void multiRename();                   // Ctrl+M
    void searchFiles();                   // Alt+F7
    void compareFiles();                  // porovnanie podľa obsahu
    void syncDirs();                      // synchronizácia priečinkov

    // vzhľad a správanie (menu pod ozubeným kolieskom)
    float fontSize = 13.5f;
    Colour markColour { 0xffe5605a };
    bool showExtCol = true, showSizeCol = true, showDateCol = true, quickLetters = false;
    int colW[3] = { 56, 100, 118 };       // šírky stĺpcov Prípona, Veľkosť, Dátum (ťahaním v hlavičke)
    void applyLook();
    int thumbSide = 110;
    std::function<Image (const File&)> getThumb;   // náhľad obrázka (dodá Player)
    // Ctrl+Z: vrátenie premenovania / presunu
    void pushUndo (std::vector<std::pair<File, File>> batch);
    void undo();
    // dvojice priečinkov a automatické nahrávanie na server
    void savePair();
    void openPair (int index);
    void toggleAutoUpload();
    bool autoUploadOn() const { return autoUp != nullptr; }
    void typeToCommandLine (const String& text);
    TextEditor cmdLine;                   // príkazový riadok pod panelmi
    void dialogMulti (const String& title, const String& text, const StringArray& labels, const StringArray& values,
                      const StringArray& buttons, std::function<void (int, StringArray)> done);
    void showList (const String& title, const StringArray& lines, const String& action,
                   std::function<void()> onAction, std::function<void (int)> onPick);
    void showFtp();
    void connectFtp (const FtpConn& c);   // pripojiť v aktívnom paneli
    // sieť v paneli (Sieť v lište diskov) a podrobnosti / kamera v druhom paneli
    void openNet();
    void showFile (const File& f);        // otvoriť priečinok súboru a označiť ho (napr. prijatý z mobilu)
    void toggleNet();                     // tlačidlo Sieť v hornej lište: zapnúť / späť do priečinka
    std::function<void()> onNetState;
    std::function<void (int)> onAppearance;   // 1 tmava, 2 svetla, 3 slovensky, 4 anglicky (ako v Playeri)     // zmena (pre zvýraznenie tlačidla Sieť)
    NetEngine& netEngine();
    void updateNetDetail();
    void netMenu();
    void netRangeDialog();
    void renameNetHost();
    std::function<Component*()> makeVideo;
    std::function<void (Component*, const String&)> playUrl;
    std::function<void (Component*)> stopVideo;
    bool videoAvailable = false;
    // ďalšie nástroje
    void driveOverview();
    void spaceMap();
    std::function<void (int, const Array<File>&, const File&)> onPlayerTool;   // 1 zmenšiť, 2 previesť, 3 podobné, 4 duplikáty
    std::function<void()> onWifi;         // prenos z mobilu
    void fetchRemote (const CmdPanel::Entry& e, std::shared_ptr<FtpSession> s, bool view);

private:
    PropertiesFile& settings;
    CmdPanel left { *this }, right { *this };
    CmdPanel* act = &left;
    bool started = false;

    struct FnKey { String label; std::function<void()> fn; };
    std::vector<FnKey> fkeys;
    std::vector<Rectangle<int>> fkeyRects() const;
    static constexpr int msgH = 22, fbarH = 30, cmdH = 26;
    void runCommand (const String& text);

    String msg; bool msgError = false;
    int tick = 0;
    std::unique_ptr<AlertWindow> dialogWin;
    int selButton = 0;
    void highlightButton (int index, bool focus);
    struct DialogKeys : KeyListener
    {
        explicit DialogKeys (Commander& c) : owner (c) {}
        bool keyPressed (const KeyPress& key, Component*) override;
        Commander& owner;
    } dialogKeys { *this };

    std::unique_ptr<Transfer> transfer;
    std::function<void (Transfer&)> transferDone;
    std::unique_ptr<Component> copyBox;   // okno priebehu kopirovania (ako v Total Commanderi)
    double transferStart = 0; bool copyInBackground = false;
    void updateCopyBox();
    void timerCallback() override;
    void startTransfer (std::vector<Transfer::Item> items, bool move, std::function<void (Transfer&)> done);
    void runCopyMove (Array<File> files, File src, File target, bool move, bool singleAsName);
    void finishMove (const Array<File>& dirsToRemove, CmdPanel* srcPanel);
    void remoteCopyMove (bool move);
    void archiveCopy (bool move);
    std::unique_ptr<TaskJob> task;
    Component* quickComp = nullptr;
    // F4 na serveri: upravovaný súbor sa sleduje a po uložení sa ponúkne nahratie späť
    struct EditWatch { File local; String remote; std::weak_ptr<FtpSession> s; Time mod; };
    std::vector<EditWatch> watches;
    void checkWatches();
    std::vector<std::vector<std::pair<File, File>>> undoStack;
    struct AutoUp { File local; String remote; std::weak_ptr<FtpSession> s; std::map<String, Time> known; std::set<String> dirs; };
    std::unique_ptr<AutoUp> autoUp;
    void autoUploadScan();
    bool pendingSync = false;
    void connectFtpTo (const FtpConn& c, CmdPanel* target, String dir);
    std::unique_ptr<Component> listWin;
    void closeOverlay();
    std::unique_ptr<NetEngine> netEng;
    std::unique_ptr<NetDetail> netDetail;
    void runSync (int direction, const std::vector<std::pair<String, int>>& plan, const File& leftDir, const File& rightDir,
                  std::shared_ptr<FtpSession> ls, String lrd, std::shared_ptr<FtpSession> rs, String rrd);
    std::function<void (TaskJob&)> taskDone;
    std::unique_ptr<FtpManager> ftpMgr;
    std::unique_ptr<FtpJob> ftpJob;
    std::function<void (FtpJob&)> ftpDone;
    void startFtpJob (std::shared_ptr<FtpSession> s, FtpJob::Kind k, std::vector<FtpJob::Item> items, bool move, std::function<void (FtpJob&)> done);
};
}
