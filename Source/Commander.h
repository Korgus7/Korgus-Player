#pragma once
#include "Common.h"
#include "Transfer.h"
#include "Icons.h"
#include "Ftp.h"
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
    const String& remoteDir() const { return rdir; }
    std::vector<Entry> selectedEntries() const;
    const Entry* currentEntry() const;
    bool hasName (const String& n) const;
    String displayPath() const;

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
    String rdir, pendingSelect;
    int listGen = 0;
    bool loading = false;
    void fillRemote (const std::vector<FtpEntry>& v);

    // pravé tlačidlo: označovanie, podržanie = ponuka
    bool rightDown = false, markMode = true, menuShown = false;
    int rightAnchor = -1, rightLast = -1;
    struct Hold : Timer { std::function<void()> fn; void timerCallback() override { stopTimer(); fn(); } } hold;
    bool dragStarted = false;

    static constexpr int driveH = 26, pathH = 22, headH = 20, footH = 20, rowH = 20;
    Rectangle<int> driveArea() const { return getLocalBounds().removeFromTop (driveH); }
    Rectangle<int> pathArea() const { return getLocalBounds().withTrimmedTop (driveH).removeFromTop (pathH); }
    Rectangle<int> copyPathArea() const { return pathArea().removeFromLeft (pathH + 4); }
    Rectangle<int> headArea() const { return getLocalBounds().withTrimmedTop (driveH + pathH).removeFromTop (headH); }
    Rectangle<int> listArea() const;
    Rectangle<int> footArea() const { return getLocalBounds().removeFromBottom (footH); }
    void columns (Rectangle<int> row, Rectangle<int>& name, Rectangle<int>& ext, Rectangle<int>& size, Rectangle<int>& date) const;
    std::vector<std::pair<Rectangle<int>, File>> driveButtons() const;
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
    bool busy() const { return transfer != nullptr || ftpJob != nullptr || dialogWin != nullptr || ftpMgr != nullptr; }
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
    std::unique_ptr<FtpManager> ftpMgr;
    std::unique_ptr<FtpJob> ftpJob;
    std::function<void (FtpJob&)> ftpDone;
    void startFtpJob (std::shared_ptr<FtpSession> s, FtpJob::Kind k, std::vector<FtpJob::Item> items, bool move, std::function<void (FtpJob&)> done);
};
}
