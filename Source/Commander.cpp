#include "Commander.h"

namespace kp
{
namespace
{
    String groupDigits (int64 v)
    {
        String s (v), out;
        for (int i = 0; i < s.length(); ++i)
        {
            if (i > 0 && (s.length() - i) % 3 == 0) out << " ";
            out << s[i];
        }
        return out;
    }

    bool sameFile (const File& a, const File& b) { return a.getFullPathName().equalsIgnoreCase (b.getFullPathName()); }

    // rozloží kopírovanie na položky; existujúci priečinok sa zlúči po súboroch,
    // aby sa pri chybe nikdy nemazal pôvodný obsah cieľa
    // policy: 0 = len zistiť konflikty, 1 = prepísať, 2 = preskočiť existujúce
    void expand (const File& from, const File& to, std::vector<Transfer::Item>& out, Array<File>& conflicts, int policy, Array<File>& mergedDirs)
    {
        if (from.isDirectory() && to.isDirectory())
        {
            mergedDirs.add (from);
            for (const auto& e : RangedDirectoryIterator (from, false, "*", File::findFilesAndDirectories | File::ignoreHiddenFiles))
                expand (e.getFile(), to.getChildFile (e.getFile().getFileName()), out, conflicts, policy, mergedDirs);
            return;
        }
        if (to.exists())
        {
            if (policy == 0) { conflicts.add (to); return; }
            if (policy == 2 || to.isDirectory() != from.isDirectory()) return;
        }
        out.push_back ({ from, to });
    }

    void drawDriveIcon (Graphics& g, Rectangle<float> r, Colour c)
    {
        auto b = r.withSizeKeepingCentre (r.getWidth(), r.getHeight() * 0.55f);
        g.setColour (c.withAlpha (0.25f));
        g.fillRoundedRectangle (b, 2.0f);
        g.setColour (c);
        g.drawRoundedRectangle (b, 2.0f, 1.2f);
        g.fillEllipse (Rectangle<float> (2.6f, 2.6f).withCentre ({ b.getRight() - 3.5f, b.getCentreY() }));
    }

    void drawFolderIcon (Graphics& g, Rectangle<float> r, Colour c)
    {
        auto b = r.withSizeKeepingCentre (14.0f, 10.0f);
        g.setColour (c);
        g.fillRoundedRectangle (b.withWidth (6.0f).withHeight (3.0f).translated (0, -1.5f), 1.0f);
        g.fillRoundedRectangle (b.withTrimmedTop (1.0f), 1.5f);
    }
}

// ================================================================ CmdPanel

CmdPanel::CmdPanel (Commander& o) : owner (o)
{
    setWantsKeyboardFocus (true);
    addAndMakeVisible (scroll);
    scroll.addListener (this);
    scroll.setAutoHide (true);
    scroll.setColour (ScrollBar::thumbColourId, col::line.brighter (0.35f));
    scroll.setColour (ScrollBar::trackColourId, col::bg);
    hold.fn = [this]
    {
        if (rightDown && rightLast == rightAnchor)
        {
            rightDown = false;
            menuShown = true;
            contextMenu();
        }
    };
    refreshDrives();
}

CmdPanel::~CmdPanel() { hold.stopTimer(); }

Rectangle<int> CmdPanel::listArea() const
{
    return getLocalBounds().withTrimmedTop (driveH + pathH + headH).withTrimmedBottom (footH).withTrimmedRight (12);
}

void CmdPanel::columns (Rectangle<int> row, Rectangle<int>& name, Rectangle<int>& ext, Rectangle<int>& size, Rectangle<int>& date) const
{
    row = row.reduced (4, 0);
    const bool narrow = row.getWidth() < 420;
    date = row.removeFromRight (narrow ? 0 : 118);
    size = row.removeFromRight (narrow ? 84 : 100);
    ext = row.removeFromRight (56);
    name = row;
}

std::vector<std::pair<Rectangle<int>, File>> CmdPanel::driveButtons() const
{
    std::vector<std::pair<Rectangle<int>, File>> v;
    auto r = driveArea().reduced (4, 3);
    for (auto& d : drives)
        v.push_back ({ r.removeFromLeft (38), d });
    v.push_back ({ r.removeFromLeft (28), File() });   // „\“ = koreň disku
    return v;
}

void CmdPanel::refreshDrives()
{
    Array<File> roots, now;
    File::findFileSystemRoots (roots);
    for (auto& r : roots)
        if (r.isDirectory()) now.add (r);
    if (now != drives) { drives = now; repaint(); }
}

int CmdPanel::rowAt (Point<int> p) const
{
    auto la = listArea();
    if (! la.withRight (getWidth()).contains (p)) return -1;
    const int i = top + (p.y - la.getY()) / rowH;
    return i < (int) entries.size() ? i : -1;
}

void CmdPanel::setFolder (const File& f, const File& select)
{
    if (! f.isDirectory()) return;
    renameEd.reset();
    if (f != folder) { marked.clear(); cursor = 0; top = 0; }
    folder = f;
    lastPerDrive[Transfer::rootOf (folder).getFullPathName().toLowerCase()] = folder;
    reload();
    if (select != File()) selectFile (select);
    else setCursor (cursor);
}

void CmdPanel::reload()
{
    const String curName = cursor >= 0 && cursor < (int) entries.size() ? entries[(size_t) cursor].name : String();
    entries.clear();
    if (folder.getParentDirectory() != folder)
    {
        Entry up;
        up.file = folder.getParentDirectory();
        up.name = "..";
        up.dir = up.up = true;
        entries.push_back (up);
    }
    for (const auto& e : RangedDirectoryIterator (folder, false, "*", File::findFilesAndDirectories | File::ignoreHiddenFiles))
    {
        Entry en;
        en.file = e.getFile();
        en.dir = e.isDirectory();
        en.name = en.file.getFileName();
        en.mod = e.getModificationTime();
        if (en.dir) en.colour = kindColour (Kind::Folder);
        else
        {
            en.ext = en.file.getFileExtension().fromFirstOccurrenceOf (".", false, false);
            en.size = e.getFileSize();
            en.colour = kindColour (kindOf (en.file));
        }
        entries.push_back (en);
    }
    folderTime = folder.getLastModificationTime();
    sortEntries();
    // označenie zmizne pri súboroch, ktoré už nie sú
    std::set<String> still;
    for (auto& e : entries) if (marked.count (e.name)) still.insert (e.name);
    marked.swap (still);
    cursor = 0;
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].name == curName) { cursor = i; break; }
    setCursor (cursor);
    repaint();
}

void CmdPanel::sortEntries()
{
    const int sc = sortCol;
    const bool desc = sortDesc;
    std::stable_sort (entries.begin(), entries.end(), [sc, desc] (const Entry& a, const Entry& b)
    {
        if (a.up != b.up) return a.up;
        if (a.dir != b.dir) return a.dir;
        int c = 0;
        if (a.dir)   // priečinky podľa názvu (pri dátume podľa dátumu)
            c = sc == 3 ? (a.mod < b.mod ? -1 : (b.mod < a.mod ? 1 : 0)) : a.name.compareNatural (b.name);
        else if (sc == 1) c = a.ext.compareIgnoreCase (b.ext);
        else if (sc == 2) c = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
        else if (sc == 3) c = a.mod < b.mod ? -1 : (b.mod < a.mod ? 1 : 0);
        if (c == 0) c = a.name.compareNatural (b.name);
        return desc ? c > 0 : c < 0;
    });
}

void CmdPanel::poll()
{
    refreshDrives();
    if (renameEd != nullptr) return;
    if (! folder.isDirectory())
    {
        File f = folder;
        while (! f.isDirectory() && f.getParentDirectory() != f) f = f.getParentDirectory();
        if (! f.isDirectory()) f = drives.isEmpty() ? File::getSpecialLocation (File::userHomeDirectory) : drives[0];
        setFolder (f);
        return;
    }
    if (folder.getLastModificationTime() != folderTime) reload();
}

Array<File> CmdPanel::selection() const
{
    Array<File> v;
    for (auto& e : entries)
        if (! e.up && marked.count (e.name)) v.add (e.file);
    if (v.isEmpty())
        if (auto f = currentFile(); f != File()) v.add (f);
    return v;
}

File CmdPanel::currentFile() const
{
    if (cursor < 0 || cursor >= (int) entries.size() || entries[(size_t) cursor].up) return {};
    return entries[(size_t) cursor].file;
}

void CmdPanel::selectFile (const File& f)
{
    for (int i = 0; i < (int) entries.size(); ++i)
        if (! entries[(size_t) i].up && sameFile (entries[(size_t) i].file, f)) { setCursor (i); return; }
    setCursor (cursor);
}

void CmdPanel::setCursor (int i)
{
    cursor = jlimit (0, jmax (0, (int) entries.size() - 1), i);
    ensureVisible();
    repaint();
}

void CmdPanel::ensureVisible()
{
    const int vis = visibleRows();
    if (cursor < top) top = cursor;
    if (cursor >= top + vis) top = cursor - vis + 1;
    top = jlimit (0, jmax (0, (int) entries.size() - vis), top);
    updateScroll();
}

void CmdPanel::updateScroll()
{
    scroll.setRangeLimits (0.0, (double) jmax (1, (int) entries.size()), dontSendNotification);
    scroll.setCurrentRange ((double) top, (double) visibleRows(), dontSendNotification);
}

void CmdPanel::scrollBarMoved (ScrollBar*, double newStart)
{
    top = jlimit (0, jmax (0, (int) entries.size() - visibleRows()), roundToInt (newStart));
    repaint();
}

void CmdPanel::toggleMark (int i)
{
    if (i < 0 || i >= (int) entries.size() || entries[(size_t) i].up) return;
    const auto& n = entries[(size_t) i].name;
    if (marked.count (n)) marked.erase (n); else marked.insert (n);
    repaint();
}

void CmdPanel::openCurrent()
{
    if (cursor < 0 || cursor >= (int) entries.size()) return;
    const auto e = entries[(size_t) cursor];
    if (e.up) goUp();
    else if (e.dir) setFolder (e.file);
    else owner.openFile (e.file);
}

void CmdPanel::goUp()
{
    if (folder.getParentDirectory() == folder) return;
    const File prev = folder;
    setFolder (folder.getParentDirectory(), prev);
}

void CmdPanel::markByMask (bool mark)
{
    const String def = "*.*";
    owner.dialog (mark ? tr ("Označiť") : tr ("Odznačiť"), tr ("Maska súborov, napríklad *.mp3;*.wav"), &def,
                  { tr ("OK"), tr ("Zrušiť") }, [sp = Component::SafePointer<CmdPanel> (this), mark] (int r, String text)
    {
        if (sp == nullptr || r != 1 || text.trim().isEmpty()) return;
        StringArray masks;
        masks.addTokens (text, ";, ", "\"");
        masks.removeEmptyStrings();
        for (auto& e : sp->entries)
        {
            if (e.up || e.dir) continue;
            for (auto& m : masks)
                if (e.name.matchesWildcard (m == "*.*" ? "*" : m, true))
                {
                    if (mark) sp->marked.insert (e.name); else sp->marked.erase (e.name);
                    break;
                }
        }
        sp->repaint();
    });
}

void CmdPanel::invertMarks()
{
    for (auto& e : entries)
    {
        if (e.up || e.dir) continue;
        if (marked.count (e.name)) marked.erase (e.name); else marked.insert (e.name);
    }
    repaint();
}

void CmdPanel::markAll()
{
    for (auto& e : entries) if (! e.up) marked.insert (e.name);
    repaint();
}

void CmdPanel::renameInPlace()
{
    if (cursor < 0 || cursor >= (int) entries.size() || entries[(size_t) cursor].up) return;
    ensureVisible();
    const auto e = entries[(size_t) cursor];
    auto row = listArea().removeFromTop (rowH).translated (0, (cursor - top) * rowH);
    Rectangle<int> n, x, s, d;
    columns (row, n, x, s, d);
    renameEd = std::make_unique<TextEditor>();
    renameEd->setFont (FontOptions (13.5f));
    renameEd->setText (e.name, false);
    renameEd->setBounds (n.withRight (x.getRight()).withTrimmedLeft (18).expanded (0, 1));
    addAndMakeVisible (*renameEd);
    renameEd->grabKeyboardFocus();
    const int dot = e.dir ? -1 : e.name.lastIndexOfChar ('.');
    renameEd->setHighlightedRegion ({ 0, dot > 0 ? dot : e.name.length() });

    Component::SafePointer<CmdPanel> sp (this);
    auto finish = [sp] (bool commit)
    {
        if (sp == nullptr || sp->renameEd == nullptr) return;
        const String newName = sp->renameEd->getText().trim();
        const File f = sp->currentFile();
        MessageManager::callAsync ([sp] { if (sp != nullptr) { sp->renameEd.reset(); sp->grabKeyboardFocus(); } });
        if (! commit || f == File() || newName.isEmpty() || newName == f.getFileName()) return;
        if (newName.containsAnyOf ("\\/:*?\"<>|"))
        {
            sp->owner.message (tr ("Názov nesmie obsahovať znaky \\ / : * ? \" < > |"), true);
            return;
        }
        const auto dest = f.getSiblingFile (newName);
        if (dest.exists() && ! sameFile (dest, f))
        {
            sp->owner.message (tr ("Súbor s týmto názvom už existuje."), true);
            return;
        }
        if (! f.moveFileTo (dest))
        {
            sp->owner.message (tr ("Nepodarilo sa premenovať (možno je otvorený v inom programe)."), true);
            return;
        }
        if (sp->marked.erase (f.getFileName())) sp->marked.insert (newName);
        sp->reload();
        sp->selectFile (dest);
    };
    renameEd->onReturnKey = [finish] { finish (true); };
    renameEd->onEscapeKey = [finish] { finish (false); };
    renameEd->onFocusLost = [finish] { finish (false); };
}

void CmdPanel::showDriveMenu()
{
    PopupMenu m;
    for (int i = 0; i < drives.size(); ++i)
    {
        const auto label = drives[i].getVolumeLabel();
        m.addItem (i + 1, drives[i].getFullPathName().upToFirstOccurrenceOf (":", false, false).toLowerCase()
                          + (label.isNotEmpty() ? "   " + label : String()),
                   true, sameFile (Transfer::rootOf (folder), drives[i]));
    }
    Component::SafePointer<CmdPanel> sp (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (driveArea())),
                     [sp] (int r)
    {
        if (sp == nullptr || r <= 0 || r > sp->drives.size()) return;
        const auto d = sp->drives[r - 1];
        const auto last = sp->lastPerDrive[d.getFullPathName().toLowerCase()];
        sp->setFolder (last.isDirectory() ? last : d);
        sp->grabKeyboardFocus();
    });
}

void CmdPanel::contextMenu()
{
    PopupMenu m;
    const bool any = ! selection().isEmpty();
    m.addItem (1, tr ("Otvoriť") + "\tEnter", cursor >= 0 && cursor < (int) entries.size());
    m.addItem (2, tr ("Zobraziť") + "\tF3", currentFile().existsAsFile());
    m.addItem (3, tr ("Upraviť") + "\tF4", currentFile().existsAsFile());
    m.addSeparator();
    m.addItem (4, tr ("Kopírovať") + "\tF5", any);
    m.addItem (5, tr ("Presunúť") + "\tF6", any);
    m.addItem (6, tr ("Premenovať") + "\tShift+F6", currentFile() != File());
    m.addItem (7, tr ("Odstrániť") + "\tF8", any);
    m.addSeparator();
    m.addItem (8, tr ("Nový priečinok") + "\tF7");
    m.addItem (9, tr ("Kopírovať cestu"), any);
    m.addItem (10, tr ("Ukázať v Prieskumníkovi"));
    Component::SafePointer<CmdPanel> sp (this);
    m.showMenuAsync (PopupMenu::Options().withMousePosition(), [sp] (int r)
    {
        if (sp == nullptr) return;
        auto& o = sp->owner;
        switch (r)
        {
            case 1: sp->openCurrent(); break;
            case 2: o.viewCurrent(); break;
            case 3: o.editCurrent(); break;
            case 4: o.copyOrMove (false); break;
            case 5: o.copyOrMove (true); break;
            case 6: sp->renameInPlace(); break;
            case 7: o.deleteSelection(); break;
            case 8: o.makeFolder(); break;
            case 9:
            {
                StringArray paths;
                for (auto& f : sp->selection()) paths.add (f.getFullPathName());
                SystemClipboard::copyTextToClipboard (paths.joinIntoString ("\r\n"));
                o.message (tr ("Cesta skopírovaná do schránky"));
                break;
            }
            case 10:
                if (auto f = sp->currentFile(); f != File()) f.revealToUser(); else sp->folder.revealToUser();
                break;
            default: break;
        }
    });
}

// ---------------------------------------------------------------- kreslenie

void CmdPanel::paint (Graphics& g)
{
    g.fillAll (col::bg);

    // lišta diskov
    auto da = driveArea();
    g.setColour (col::panel);
    g.fillRect (da);
    const auto root = Transfer::rootOf (folder);
    g.setFont (FontOptions (13.0f));
    for (auto& [r, d] : driveButtons())
    {
        const bool cur = d != File() && sameFile (d, root);
        if (cur)
        {
            g.setColour (col::accent);
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
        }
        g.setColour (col::text);
        if (d == File())
            g.drawText ("\\", r, Justification::centred, false);
        else
        {
            drawDriveIcon (g, r.toFloat().removeFromLeft (20).reduced (2, 3), col::text);
            g.drawText (d.getFullPathName().upToFirstOccurrenceOf (":", false, false).toLowerCase(),
                        r.withTrimmedLeft (22), Justification::centredLeft, false);
        }
    }
    {
        const int64 freeB = folder.getBytesFreeOnVolume(), totalB = folder.getVolumeTotalSize();
        if (totalB > 0)
        {
            auto info = da.reduced (8, 0);
            info.removeFromLeft (jmax (0, (int) driveButtons().size()) * 40);
            g.setColour (col::text);
            g.drawText (sizeText (freeB) + tr (" voľných z ") + sizeText (totalB), info, Justification::centredRight, true);
        }
    }

    // cesta (aktívny panel = farebná)
    auto pa = pathArea();
    g.setColour (active ? col::accent : col::panelHi);
    g.fillRect (pa);
    g.setColour (active ? col::selText : col::text);
    g.setFont (FontOptions (13.5f));
    g.drawText (folder.getFullPathName(), pa.reduced (6, 0), Justification::centredLeft, true);

    // hlavička stĺpcov
    auto ha = headArea();
    g.setColour (col::panel);
    g.fillRect (ha);
    g.setColour (col::line);
    g.fillRect (ha.removeFromBottom (1));
    {
        Rectangle<int> n, x, s, d;
        columns (headArea().withTrimmedRight (12), n, x, s, d);
        const Rectangle<int> rs[] = { n, x, s, d };
        const char* titles[] = { "Názov", "Prípona", "Veľkosť", "Dátum" };
        g.setFont (FontOptions (12.5f));
        for (int i = 0; i < 4; ++i)
        {
            if (rs[i].getWidth() <= 0) continue;
            String t = tr (titles[i]);
            if (i == sortCol) t << String::fromUTF8 (sortDesc ? " \xe2\x96\xbc" : " \xe2\x96\xb2");
            g.setColour (i == sortCol ? col::text.brighter (0.2f) : col::text);
            g.drawText (t, rs[i].withTrimmedLeft (i == 0 ? 18 : 0),
                        i >= 2 ? Justification::centredRight : Justification::centredLeft, true);
        }
    }

    // zoznam
    auto la = listArea();
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (la);
        g.setFont (FontOptions (13.5f));
        const int vis = visibleRows() + 1;
        for (int i = top; i < jmin ((int) entries.size(), top + vis); ++i)
        {
            const auto& e = entries[(size_t) i];
            auto row = la.withHeight (rowH).translated (0, (i - top) * rowH);
            if (i == cursor)
            {
                if (active && hasKeyboardFocus (true)) { g.setColour (col::accent); g.fillRect (row); }
                else { g.setColour (col::accentHi); g.drawRect (row, 1); }
            }
            const bool mk = isMarked (i);
            const Colour tc = mk ? col::gold : (i == cursor && active ? col::selText : col::text);
            Rectangle<int> n, x, s, d;
            columns (row, n, x, s, d);
            auto iconR = n.removeFromLeft (18).toFloat();
            if (e.up)
            {
                g.setColour (col::textDim);
                g.drawText (String::fromUTF8 ("\xe2\x86\x91"), iconR.toNearestInt(), Justification::centred, false);
            }
            else if (e.dir) drawFolderIcon (g, iconR, e.colour);
            else
            {
                g.setColour (e.colour);
                g.fillRoundedRectangle (iconR.withSizeKeepingCentre (8.0f, 10.0f), 1.5f);
            }
            g.setColour (tc);
            g.setFont (FontOptions (13.5f, mk ? Font::bold : Font::plain));
            const String shown = e.dir ? e.name : (e.ext.isEmpty() ? e.name : e.name.dropLastCharacters (e.ext.length() + 1));
            g.drawText (shown, n, Justification::centredLeft, true);
            g.drawText (e.ext, x, Justification::centredLeft, true);
            g.drawText (e.dir ? String ("<DIR>") : groupDigits (e.size), s, Justification::centredRight, true);
            if (d.getWidth() > 0 && ! e.up)
                g.drawText (e.mod.formatted ("%d.%m.%Y %H:%M"), d, Justification::centredRight, true);
        }
    }
    if (dropHover)
    {
        g.setColour (col::gold);
        g.drawRect (la, 2);
    }

    // súčet dole
    auto fa = footArea();
    g.setColour (col::panel);
    g.fillRect (fa);
    g.setColour (col::line);
    g.fillRect (fa.removeFromTop (1));
    int nFiles = 0, nDirs = 0, mFiles = 0, mDirs = 0;
    int64 sAll = 0, sMarked = 0;
    for (auto& e : entries)
    {
        if (e.up) continue;
        const bool mk = marked.count (e.name) > 0;
        if (e.dir) { ++nDirs; if (mk) ++mDirs; }
        else { ++nFiles; sAll += e.size; if (mk) { ++mFiles; sMarked += e.size; } }
    }
    g.setColour (col::text);
    g.setFont (FontOptions (12.5f));
    g.drawText (tr ("Označené: ") + String (mFiles) + " / " + String (nFiles) + tr (" súborov") + " (" + sizeText (sMarked) + " / " + sizeText (sAll) + ")"
                + ", " + String (mDirs) + " / " + String (nDirs) + tr (" priečinkov"),
                fa.reduced (6, 0), Justification::centredLeft, true);
}

void CmdPanel::resized()
{
    auto la = listArea();
    scroll.setBounds (la.getRight(), la.getY(), 12, la.getHeight());
    ensureVisible();
}

void CmdPanel::focusGained (FocusChangeType)
{
    owner.setActive (*this);
    repaint();
}

// ---------------------------------------------------------------- klávesnica a myš

bool CmdPanel::keyPressed (const KeyPress& key)
{
    if (renameEd != nullptr) return false;
    const int k = key.getKeyCode();
    const auto mods = key.getModifiers();
    const int vis = visibleRows();

    if (k == KeyPress::upKey && ! mods.isAltDown()) { if (mods.isShiftDown()) toggleMark (cursor); setCursor (cursor - 1); return true; }
    if (k == KeyPress::downKey && ! mods.isAltDown()) { if (mods.isShiftDown()) toggleMark (cursor); setCursor (cursor + 1); return true; }
    if (k == KeyPress::pageUpKey && mods.isCtrlDown()) { goUp(); return true; }
    if (k == KeyPress::pageUpKey) { setCursor (cursor - vis + 1); return true; }
    if (k == KeyPress::pageDownKey) { setCursor (cursor + vis - 1); return true; }
    if (k == KeyPress::homeKey) { setCursor (0); return true; }
    if (k == KeyPress::endKey) { setCursor ((int) entries.size() - 1); return true; }
    if (k == KeyPress::insertKey) { toggleMark (cursor); setCursor (cursor + 1); return true; }
    if (k == KeyPress::spaceKey && ! mods.isAnyModifierKeyDown()) { toggleMark (cursor); return true; }
    if (k == KeyPress::returnKey && ! mods.isAnyModifierKeyDown()) { openCurrent(); return true; }
    if (k == KeyPress::backspaceKey) { goUp(); return true; }
    if (mods.isCtrlDown() && k == '\\') { setFolder (Transfer::rootOf (folder)); return true; }
    if (k == KeyPress::numberPadAdd) { markByMask (true); return true; }
    if (k == KeyPress::numberPadSubtract) { markByMask (false); return true; }
    if (k == KeyPress::numberPadMultiply) { invertMarks(); return true; }
    if (mods.isCtrlDown() && (k == 'a' || k == 'A')) { markAll(); return true; }
    return owner.handleKey (key);
}

void CmdPanel::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    owner.setActive (*this);
    renameEd.reset();
    dragStarted = false;
    const auto p = e.getPosition();

    if (driveArea().contains (p))
    {
        for (auto& [r, d] : driveButtons())
            if (r.contains (p))
            {
                if (d == File()) { setFolder (Transfer::rootOf (folder)); return; }
                if (e.mods.isRightButtonDown()) { showDriveMenu(); return; }
                const auto last = lastPerDrive[d.getFullPathName().toLowerCase()];
                setFolder (last.isDirectory() ? last : d);
                return;
            }
        return;
    }
    if (headArea().contains (p))
    {
        Rectangle<int> n, x, s, d;
        columns (headArea().withTrimmedRight (12), n, x, s, d);
        int c = n.contains (p) ? 0 : x.contains (p) ? 1 : s.contains (p) ? 2 : d.contains (p) ? 3 : -1;
        if (c < 0) return;
        if (c == sortCol) sortDesc = ! sortDesc; else { sortCol = c; sortDesc = c >= 2; }
        const File cur = currentFile();
        sortEntries();
        if (cur != File()) selectFile (cur); else setCursor (cursor);
        return;
    }

    const int r = rowAt (p);
    if (e.mods.isRightButtonDown())
    {
        // ako v Total Commanderi: pravé tlačidlo označuje, podržanie otvorí ponuku
        rightDown = true;
        menuShown = false;
        if (r < 0) return;
        markMode = ! isMarked (r);
        if (! entries[(size_t) r].up) { if (markMode) marked.insert (entries[(size_t) r].name); else marked.erase (entries[(size_t) r].name); }
        rightAnchor = rightLast = r;
        setCursor (r);
        hold.startTimer (500);
        return;
    }
    if (r >= 0) setCursor (r);
}

void CmdPanel::mouseDrag (const MouseEvent& e)
{
    if (rightDown)
    {
        auto la = listArea();
        auto p = e.getPosition();
        if (p.y < la.getY() && top > 0) { --top; updateScroll(); }
        if (p.y > la.getBottom() && top + visibleRows() < (int) entries.size()) { ++top; updateScroll(); }
        const int y = jlimit (la.getY(), la.getBottom() - 1, p.y);
        const int r = jmin ((int) entries.size() - 1, top + (y - la.getY()) / rowH);
        if (r >= 0 && r != rightLast)
        {
            hold.stopTimer();
            for (int i = jmin (rightLast, r); i <= jmax (rightLast, r); ++i)
                if (! entries[(size_t) i].up) { if (markMode) marked.insert (entries[(size_t) i].name); else marked.erase (entries[(size_t) i].name); }
            rightLast = r;
            setCursor (r);
        }
        return;
    }
    if (! e.mods.isLeftButtonDown() || dragStarted || e.getDistanceFromDragStart() < 6) return;
    if (rowAt (e.getMouseDownPosition()) < 0) return;
    const auto files = selection();
    if (files.isEmpty()) return;
    dragStarted = true;
    Image img (Image::ARGB, 220, 24, true);
    {
        Graphics g (img);
        g.setColour (col::accent.withAlpha (0.9f));
        g.fillRoundedRectangle (img.getBounds().toFloat(), 3.0f);
        g.setColour (col::selText);
        g.setFont (FontOptions (13.0f));
        g.drawText (files.size() == 1 ? files[0].getFileName() : String (files.size()) + tr (" položiek"),
                    img.getBounds().reduced (8, 0), Justification::centredLeft, true);
    }
    if (auto* c = DragAndDropContainer::findParentDragContainerFor (this))
        c->startDragging ("kc", this, ScaledImage (img), true);
}

void CmdPanel::mouseUp (const MouseEvent&)
{
    rightDown = false;
    hold.stopTimer();
}

void CmdPanel::mouseDoubleClick (const MouseEvent& e)
{
    if (e.mods.isLeftButtonDown() && rowAt (e.getPosition()) >= 0) openCurrent();
}

void CmdPanel::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    const int step = w.deltaY > 0 ? -jmax (1, roundToInt (w.deltaY * 12)) : jmax (1, roundToInt (-w.deltaY * 12));
    top = jlimit (0, jmax (0, (int) entries.size() - visibleRows()), top + step);
    updateScroll();
    repaint();
}

bool CmdPanel::isInterestedInDragSource (const SourceDetails& d)
{
    auto* src = dynamic_cast<CmdPanel*> (d.sourceComponent.get());
    return d.description == var ("kc") && src != nullptr && src != this;
}

void CmdPanel::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    repaint();
    if (auto* src = dynamic_cast<CmdPanel*> (d.sourceComponent.get()))
        owner.dropped (*src, *this, ModifierKeys::currentModifiers.isShiftDown());
}

// ================================================================ Commander

Commander::Commander (PropertiesFile& s) : settings (s)
{
    addAndMakeVisible (left);
    addAndMakeVisible (right);
    fkeys = {
        { "F3 / " + tr ("Zobraziť"), [this] { viewCurrent(); } },
        { "F4 / " + tr ("Upraviť"), [this] { editCurrent(); } },
        { "F5 / " + tr ("Kopírovať"), [this] { copyOrMove (false); } },
        { "F6 / " + tr ("Pres./Prem."), [this] { copyOrMove (true); } },
        { "F7 / " + tr ("Nový priečinok"), [this] { makeFolder(); } },
        { "F8 / " + tr ("Odstrániť"), [this] { deleteSelection(); } },
        { "Alt+F4 / " + tr ("Skončiť"), [this] { if (auto* w = getTopLevelComponent()) if (auto* dw = dynamic_cast<DocumentWindow*> (w)) dw->closeButtonPressed(); } },
    };
}

Commander::~Commander()
{
    stopTimer();
    transfer.reset();
}

void Commander::start()
{
    if (! started)
    {
        started = true;
        const auto home = File::getSpecialLocation (File::userHomeDirectory);
        File l (settings.getValue ("kcLeft")), r (settings.getValue ("kcRight"));
        left.setFolder (l.isDirectory() ? l : home);
        right.setFolder (r.isDirectory() ? r : home);
        act = settings.getIntValue ("kcActive", 0) == 1 ? &right : &left;
        left.active = act == &left;
        right.active = act == &right;
    }
    else
    {
        left.poll();
        right.poll();
    }
    focusActive();
}

void Commander::saveState()
{
    if (! started) return;
    settings.setValue ("kcLeft", left.getFolder().getFullPathName());
    settings.setValue ("kcRight", right.getFolder().getFullPathName());
    settings.setValue ("kcActive", act == &right ? 1 : 0);
}

void Commander::visibilityChanged()
{
    if (isVisible()) startTimer (250);
    else if (transfer == nullptr) stopTimer();
}

void Commander::focusActive()
{
    Component::SafePointer<Commander> sp (this);
    MessageManager::callAsync ([sp] { if (sp != nullptr && sp->isShowing()) sp->act->grabKeyboardFocus(); });
}

void Commander::setActive (CmdPanel& p)
{
    if (act == &p) return;
    act->active = false;
    act->repaint();
    act = &p;
    p.active = true;
    p.repaint();
}

std::vector<Rectangle<int>> Commander::fkeyRects() const
{
    std::vector<Rectangle<int>> v;
    auto r = getLocalBounds().removeFromBottom (fbarH);
    const int n = (int) fkeys.size();
    for (int i = 0; i < n; ++i)
    {
        const int x0 = r.getX() + r.getWidth() * i / n, x1 = r.getX() + r.getWidth() * (i + 1) / n;
        v.push_back ({ x0, r.getY(), x1 - x0, r.getHeight() });
    }
    return v;
}

void Commander::paint (Graphics& g)
{
    g.fillAll (col::bg);
    auto r = getLocalBounds();
    auto fb = r.removeFromBottom (fbarH);
    auto ml = r.removeFromBottom (msgH);

    // riadok so správou / priebehom prenosu
    g.setColour (col::panel);
    g.fillRect (ml);
    if (transfer != nullptr)
    {
        g.setColour (col::accent);
        g.fillRect (ml.withWidth (roundToInt (ml.getWidth() * transfer->progress())));
    }
    g.setColour (msgError ? Colour (0xffe08a8a) : col::text);
    g.setFont (FontOptions (13.0f));
    g.drawText (msg, ml.reduced (8, 0), Justification::centredLeft, true);

    // lišta F3–F8
    g.setColour (col::panel);
    g.fillRect (fb);
    g.setColour (col::line);
    g.fillRect (fb.removeFromTop (1));
    const auto rects = fkeyRects();
    g.setFont (FontOptions (13.5f));
    for (size_t i = 0; i < rects.size(); ++i)
    {
        g.setColour (col::line);
        if (i > 0) g.fillRect (rects[i].getX(), rects[i].getY() + 6, 1, rects[i].getHeight() - 10);
        g.setColour (col::text);
        g.drawText (fkeys[i].label, rects[i], Justification::centred, true);
    }
}

void Commander::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (fbarH + msgH);
    left.setBounds (r.removeFromLeft (r.getWidth() / 2).withTrimmedRight (2));
    right.setBounds (r.withTrimmedLeft (2));
}

void Commander::mouseDown (const MouseEvent& e)
{
    const auto rects = fkeyRects();
    for (size_t i = 0; i < rects.size(); ++i)
        if (rects[i].contains (e.getPosition())) { fkeys[i].fn(); return; }
}

bool Commander::keyPressed (const KeyPress& key) { return handleKey (key); }

bool Commander::handleKey (const KeyPress& key)
{
    const int k = key.getKeyCode();
    const auto mods = key.getModifiers();
    if (k == KeyPress::escapeKey && transfer != nullptr) { transfer->cancel(); return true; }
    if (k == KeyPress::tabKey && ! mods.isCtrlDown()) { auto& o = otherPanel(); setActive (o); o.grabKeyboardFocus(); return true; }
    if (mods.isAltDown() && k == KeyPress::F1Key) { left.showDriveMenu(); return true; }
    if (mods.isAltDown() && k == KeyPress::F2Key) { right.showDriveMenu(); return true; }
    if (mods.isCtrlDown() && (k == 'u' || k == 'U'))
    {
        const File a = left.getFolder(), b = right.getFolder();
        left.setFolder (b);
        right.setFolder (a);
        return true;
    }
    if (mods.isCtrlDown() && (k == 'r' || k == 'R')) { act->reload(); return true; }
    if (mods.isAltDown()) return false;
    if (k == KeyPress::F3Key) { viewCurrent(); return true; }
    if (k == KeyPress::F4Key) { editCurrent(); return true; }
    if (k == KeyPress::F5Key) { copyOrMove (false); return true; }
    if (k == KeyPress::F6Key && mods.isShiftDown()) { act->renameInPlace(); return true; }
    if (k == KeyPress::F6Key) { copyOrMove (true); return true; }
    if (k == KeyPress::F7Key) { makeFolder(); return true; }
    if (k == KeyPress::F8Key || (k == KeyPress::deleteKey && ! mods.isCtrlDown())) { deleteSelection(); return true; }
    return false;
}

void Commander::message (const String& m, bool error)
{
    msg = m;
    msgError = error;
    repaint (0, getHeight() - fbarH - msgH, getWidth(), msgH);
}

void Commander::dialog (const String& title, const String& text, const String* edit, const StringArray& buttons,
                        std::function<void (int, String)> done)
{
    if (dialogWin != nullptr) return;
    dialogWin = std::make_unique<AlertWindow> (title, text, MessageBoxIconType::NoIcon, this);
    if (edit != nullptr) dialogWin->addTextEditor ("t", *edit, {});
    for (int i = 0; i < buttons.size(); ++i)
    {
        const bool last = i == buttons.size() - 1;
        dialogWin->addButton (buttons[i], last ? 0 : i + 1,
                              i == 0 ? KeyPress (KeyPress::returnKey) : (last ? KeyPress (KeyPress::escapeKey) : KeyPress()));
    }
    addAndMakeVisible (*dialogWin);
    dialogWin->setSize (jmin (560, getWidth() - 40), dialogWin->getHeight());
    if (auto* ed = dialogWin->getTextEditor ("t"))
    {
        ed->setSize (dialogWin->getWidth() - 50, ed->getHeight());
        ed->selectAll();
    }
    dialogWin->setCentrePosition (getLocalBounds().getCentre());

    Component::SafePointer<Commander> sp (this);
    dialogWin->enterModalState (true, ModalCallbackFunction::create ([sp, done] (int result)
    {
        if (sp == nullptr || sp->dialogWin == nullptr) return;
        String t;
        if (auto* ed = sp->dialogWin->getTextEditor ("t")) t = ed->getText();
        auto* w = sp->dialogWin.release();
        w->setVisible (false);
        MessageManager::callAsync ([w] { delete w; });
        sp->act->grabKeyboardFocus();
        if (done) done (result, t);
    }));
}

void Commander::openFile (const File& f)
{
    if (! f.startAsProcess()) message (tr ("Súbor sa nedá otvoriť: ") + f.getFileName(), true);
}

void Commander::viewCurrent()
{
    const auto f = act->currentFile();
    if (f.existsAsFile() && onView) onView (f);
}

void Commander::editCurrent()
{
    const auto f = act->currentFile();
    if (! f.existsAsFile()) return;
   #if JUCE_WINDOWS
    const auto notepad = File::getSpecialLocation (File::windowsSystemDirectory).getChildFile ("notepad.exe");
    if (notepad.existsAsFile() && notepad.startAsProcess (f.getFullPathName().quoted())) return;
   #endif
    openFile (f);
}

void Commander::copyOrMove (bool move)
{
    if (busy()) return;
    const auto files = act->selection();
    if (files.isEmpty()) return;
    const File srcDir = act->getFolder(), dstDir = otherPanel().getFolder();
    const String what = files.size() == 1 ? files[0].getFileName().quoted() : String (files.size()) + tr (" položiek");
    const String def = dstDir.getFullPathName();
    Component::SafePointer<Commander> sp (this);
    dialog (move ? tr ("Presunúť / premenovať") : tr ("Kopírovať"),
            (move ? tr ("Presunúť ") : tr ("Kopírovať ")) + what + tr (" do:"), &def,
            { move ? tr ("Presunúť") : tr ("Kopírovať"), tr ("Zrušiť") },
            [sp, files, srcDir, move] (int r, String t)
    {
        if (sp == nullptr || r != 1) return;
        t = t.trim().unquoted().trim();
        if (t.isEmpty()) return;
        const File target = File::isAbsolutePath (t) ? File (t) : srcDir.getChildFile (t);
        bool asName = false;
        if (! target.isDirectory())
        {
            if (files.size() == 1 && target.getParentDirectory().isDirectory()) asName = true;
            else { sp->message (tr ("Cieľový priečinok neexistuje: ") + t, true); return; }
        }
        // výber dialógov sa zatvára asynchrónne – ďalší dialóg až potom
        MessageManager::callAsync ([sp, files, srcDir, target, move, asName]
        {
            if (sp != nullptr) sp->runCopyMove (files, srcDir, target, move, asName);
        });
    });
}

void Commander::runCopyMove (Array<File> files, File srcDir, File target, bool move, bool asName)
{
    std::vector<Transfer::Item> pairs;
    for (auto& f : files)
    {
        const File to = asName ? target : target.getChildFile (f.getFileName());
        if (sameFile (to, f)) { message (tr ("Súbor sa nedá kopírovať sám na seba."), true); return; }
        if (f.isDirectory() && to.isAChildOf (f)) { message (tr ("Priečinok sa nedá kopírovať sám do seba."), true); return; }
        pairs.push_back ({ f, to });
    }
    CmdPanel* srcPanel = sameFile (left.getFolder(), srcDir) ? &left : &right;
    const bool sameDrive = sameFile (Transfer::rootOf (srcDir), Transfer::rootOf (asName ? target.getParentDirectory() : target));

    Component::SafePointer<Commander> sp (this);
    auto go = [sp, pairs, move, sameDrive, srcPanel, target] (int policy)
    {
        if (sp == nullptr) return;
        std::vector<Transfer::Item> items;
        Array<File> none, merged;
        for (auto& p : pairs) expand (p.from, p.to, items, none, policy, merged);
        if (items.empty()) { sp->message (tr ("Nič sa nezmenilo – všetko už v cieli je.")); return; }
        if (move && sameDrive)
        {
            int ok = 0;
            String failed;
            for (auto& it : items)
                if (it.from.moveFileTo (it.to)) ++ok; else failed = it.from.getFileName();
            sp->finishMove (merged, srcPanel);
            if (failed.isNotEmpty()) sp->message (tr ("Nepodarilo sa presunúť: ") + failed + tr (" (možno je otvorený v inom programe)"), true);
            else sp->message (tr ("Presunuté: ") + String (ok) + tr (" → ") + target.getFullPathName());
            return;
        }
        sp->startTransfer (std::move (items), move, [sp, merged, srcPanel, move, target] (Transfer& t)
        {
            if (sp == nullptr) return;
            if (move) sp->finishMove (merged, srcPanel);
            else { sp->left.reload(); sp->right.reload(); srcPanel->clearMarks(); }
            String m = t.wasCancelled() ? tr ("Zrušené · hotové: ") : (move ? tr ("Presunuté: ") : tr ("Skopírované: "));
            m << String ((int) t.completed.size()) << tr (" → ") << target.getFullPathName();
            if (t.error.isNotEmpty()) m << String::fromUTF8 (" \xc2\xb7 ") << t.error;
            sp->message (m, t.error.isNotEmpty());
        });
    };

    Array<File> conflicts, merged;
    std::vector<Transfer::Item> tmp;
    for (auto& p : pairs) expand (p.from, p.to, tmp, conflicts, 0, merged);
    if (conflicts.isEmpty()) { go (1); return; }
    dialog (tr ("Súbory už existujú"),
            String (conflicts.size()) + tr (" položiek už v cieli existuje, napríklad ") + conflicts[0].getFileName(),
            nullptr, { tr ("Prepísať všetky"), tr ("Preskočiť existujúce"), tr ("Zrušiť") },
            [go] (int r, String) { if (r == 1) go (1); else if (r == 2) go (2); });
}

void Commander::finishMove (const Array<File>& dirsToRemove, CmdPanel* srcPanel)
{
    // zlúčené priečinky zostali prázdne → preč (zmaže sa len prázdny priečinok)
    for (int i = dirsToRemove.size(); --i >= 0;)
        dirsToRemove[i].deleteFile();
    if (srcPanel != nullptr) srcPanel->clearMarks();
    left.reload();
    right.reload();
}

void Commander::startTransfer (std::vector<Transfer::Item> items, bool move, std::function<void (Transfer&)> done)
{
    if (transfer != nullptr) { message (tr ("Počkaj, kým sa dokončí prebiehajúci prenos (Esc ho zruší)."), true); return; }
    transfer = std::make_unique<Transfer> (std::move (items), move);
    transferDone = std::move (done);
    transfer->start();
    startTimer (250);
}

void Commander::timerCallback()
{
    if (transfer != nullptr)
    {
        if (transfer->isDone())
        {
            auto t = std::move (transfer);
            auto d = std::move (transferDone);
            transferDone = nullptr;
            if (d) d (*t);
            if (! isVisible()) stopTimer();
        }
        else
        {
            message ((transfer->isMove() ? tr ("Presúvam ") : tr ("Kopírujem ")) + String (transfer->currentIndex() + 1) + " / " + String (transfer->count())
                     + " · " + String (roundToInt (transfer->progress() * 100.0)) + " %" + tr (" · Esc = zrušiť"));
        }
    }
    if (++tick % 8 == 0 && dialogWin == nullptr && isVisible())
    {
        left.poll();
        right.poll();
    }
}

void Commander::makeFolder()
{
    if (busy()) return;
    CmdPanel* p = act;
    const String empty;
    Component::SafePointer<Commander> sp (this);
    dialog (tr ("Nový priečinok"), tr ("Názov priečinka:"), &empty, { tr ("Vytvoriť"), tr ("Zrušiť") }, [sp, p] (int r, String name)
    {
        if (sp == nullptr || r != 1) return;
        name = name.trim();
        if (name.isEmpty()) return;
        if (name.containsAnyOf ("/:*?\"<>|")) { sp->message (tr ("Názov nesmie obsahovať znaky / : * ? \" < > |"), true); return; }
        const auto d = p->getFolder().getChildFile (name);
        if (d.exists()) { sp->message (tr ("Priečinok s týmto názvom už existuje."), true); return; }
        if (! d.createDirectory()) { sp->message (tr ("Priečinok sa nepodarilo vytvoriť."), true); return; }
        File first = d;
        while (first.getParentDirectory() != p->getFolder() && first.getParentDirectory() != first) first = first.getParentDirectory();
        p->reload();
        p->selectFile (first);
    });
}

void Commander::deleteSelection()
{
    if (busy()) return;
    const auto files = act->selection();
    if (files.isEmpty()) return;
    CmdPanel* p = act;
    String text = files.size() == 1 ? tr ("Presunúť do Koša ") + files[0].getFileName().quoted() + "?"
                                    : tr ("Presunúť do Koša ") + String (files.size()) + tr (" položiek?");
    if (files.size() > 1)
    {
        for (int i = 0; i < jmin (6, files.size()); ++i) text << "\n" << files[i].getFileName();
        if (files.size() > 6) text << String::fromUTF8 ("\n\xe2\x80\xa6");
    }
    Component::SafePointer<Commander> sp (this);
    dialog (tr ("Odstrániť"), text, nullptr, { tr ("Odstrániť"), tr ("Zrušiť") }, [sp, files, p] (int r, String)
    {
        if (sp == nullptr || r != 1) return;
        int ok = 0;
        String failed;
        for (auto& f : files)
            if (f.moveToTrash()) ++ok; else failed = f.getFileName();
        p->clearMarks();
        sp->left.reload();
        sp->right.reload();
        if (failed.isNotEmpty()) sp->message (tr ("Nepodarilo sa presunúť do Koša: ") + failed, true);
        else sp->message ((ok == 1 ? files[0].getFileName() : String (ok) + tr (" položiek")) + tr (" → Kôš"));
    });
}

void Commander::dropped (CmdPanel& from, CmdPanel& to, bool move)
{
    if (&from == &to) return;
    setActive (from);
    copyOrMove (move);
}
}
