#pragma once
#include "Common.h"
#include "Transfer.h"
#include "Icons.h"
#include "Ftp.h"
#include "Archive.h"
#include <set>
#include <map>

namespace kp
{
// Tlačidlo KC / KP vľavo hore: prepína Player ↔ Korgus Commander
class ModeButton : public Button
{
public:
    ModeButton() : Button ("mode")
    {
        setMouseCursor (MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (false);
    }
    void setText (const String& t) { text = t; repaint(); }

    void paintButton (Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (down ? col::accentHi : (over ? col::accentHi.withAlpha (0.85f) : col::accent));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (col::line.brighter (0.35f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (col::selText);
        g.setFont (FontOptions (14.0f, Font::bold));
        g.drawText (text, r, Justification::centred, false);
    }

private:
    String text { "KC" };
};

class Commander;

// Jeden panel: lišta diskov, cesta, hlavička stĺpcov, zoznam, súčet dole
class CmdPanel : public Component, public DragAndDropTarget, private ScrollBar::Listener
{
public:
    struct Entry { File file; String name, ext; bool dir = false, up = false; int64 size = 0; Time mod; Colour colour; bool hidden = false; String rpath; };

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
    bool isVirtual() const { return ftp != nullptr || arc != File(); }

    // karty (Ctrl+T, Ctrl+W, Ctrl+Tab)
    void newTab();
    void closeTab();
    void nextTab (int dir);
    void selectTab (int i);
    StringArray getTabs() const { return tabs; }
    int getCurTab() const { return curTab; }
    void setTabs (const StringArray& t, int cur);

    bool active = false;

    void paint (Graphics& g) override;
    void resized() override;
    bool keyPressed (const KeyPress& key) override;
    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;
    void mouseUp (const MouseEvent& e) override;
    void mouseDoubleClick (const MouseEvent& e) override;
    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override;
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

    // pravé tlačidlo: označovanie, podržanie = ponuka
    bool rightDown = false, markMode = true, menuShown = false;
    int rightAnchor = -1, rightLast = -1;
    struct Hold : Timer { std::function<void()> fn; void timerCallback() override { stopTimer(); fn(); } } hold;
    bool dragStarted = false;

    static constexpr int driveH = 26, pathH = 22, headH = 20, footH = 20, rowH = 20;
    static constexpr int tabH = 22;
    int topOff() const { return tabs.size() > 1 ? tabH : 0; }
    Rectangle<int> tabArea() const { return getLocalBounds().removeFromTop (topOff()); }
    Rectangle<int> driveArea() const { return getLocalBounds().withTrimmedTop (topOff()).removeFromTop (driveH); }
    Rectangle<int> pathArea() const { return getLocalBounds().withTrimmedTop (topOff() + driveH).removeFromTop (pathH); }
    Rectangle<int> copyPathArea() const { return pathArea().removeFromLeft (pathH + 4); }
    Rectangle<int> headArea() const { return getLocalBounds().withTrimmedTop (topOff() + driveH + pathH).removeFromTop (headH); }
    std::vector<Rectangle<int>> tabRects() const;
    Rectangle<int> listArea() const;
    Rectangle<int> footArea() const { return getLocalBounds().removeFromBottom (footH); }
    void columns (Rectangle<int> row, Rectangle<int>& name, Rectangle<int>& ext, Rectangle<int>& size, Rectangle<int>& date) const;
    struct DriveBtn { Rectangle<int> r; File d; int kind; };   // 0 disk, 1 „\“, 2 FTP, 3 odpojiť
    std::vector<DriveBtn> driveButtons() const;
    int visibleRows() const { return jmax (1, listArea().getHeight() / rowH); }
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
    void dialogMulti (const String& title, const String& text, const StringArray& labels, const StringArray& values,
                      const StringArray& buttons, std::function<void (int, StringArray)> done);
    void showList (const String& title, const StringArray& lines, const String& action,
                   std::function<void()> onAction, std::function<void (int)> onPick);
    void showFtp();
    void fetchRemote (const CmdPanel::Entry& e, std::shared_ptr<FtpSession> s, bool view);

private:
    PropertiesFile& settings;
    CmdPanel left { *this }, right { *this };
    CmdPanel* act = &left;
    bool started = false;

    struct FnKey { String label; std::function<void()> fn; };
    std::vector<FnKey> fkeys;
    std::vector<Rectangle<int>> fkeyRects() const;
    static constexpr int msgH = 22, fbarH = 30;

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
    std::unique_ptr<Component> listWin;
    void runSync (int direction, const std::vector<std::pair<String, int>>& plan, const File& leftDir, const File& rightDir,
                  std::shared_ptr<FtpSession> ls, String lrd, std::shared_ptr<FtpSession> rs, String rrd);
    std::function<void (TaskJob&)> taskDone;
    std::unique_ptr<FtpManager> ftpMgr;
    std::unique_ptr<FtpJob> ftpJob;
    std::function<void (FtpJob&)> ftpDone;
    void startFtpJob (std::shared_ptr<FtpSession> s, FtpJob::Kind k, std::vector<FtpJob::Item> items, bool move, std::function<void (FtpJob&)> done);
};
}
