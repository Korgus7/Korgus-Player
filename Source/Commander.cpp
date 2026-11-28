#include "Commander.h"

#if JUCE_WINDOWS
extern "C" __declspec(dllimport) unsigned long __stdcall GetFileAttributesW (const wchar_t*);
#endif

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

    // 1 položka, 2–4 položky, 5 a viac položiek
    String items (int n)
    {
        return String (n) + (n == 1 ? tr (" položka") : (n >= 2 && n <= 4 ? tr (" položky") : tr (" položiek")));
    }

    // 0 = obyčajný, 1 = skrytý, 2 = systémový
    int hiddenKind (const File& f)
    {
       #if JUCE_WINDOWS
        const unsigned long a = GetFileAttributesW (f.getFullPathName().toWideCharPointer());
        if (a == 0xffffffffUL) return 0;
        if (a & 0x4UL) return 2;
        return (a & 0x2UL) ? 1 : 0;
       #else
        return f.getFileName().startsWithChar ('.') ? 1 : 0;
       #endif
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
    return getLocalBounds().withTrimmedTop (topOff() + driveH + pathH + headH).withTrimmedBottom (footH).withTrimmedRight (12);
}

std::vector<Rectangle<int>> CmdPanel::tabRects() const
{
    std::vector<Rectangle<int>> v;
    auto r = tabArea().reduced (2, 2);
    const int n = tabs.size();
    if (n <= 1) return v;
    const int w = jmin (160, r.getWidth() / n);
    for (int i = 0; i < n; ++i) v.push_back (r.removeFromLeft (w).withTrimmedRight (2));
    return v;
}

void CmdPanel::setTabs (const StringArray& t, int cur)
{
    tabs.clear();
    for (auto& x : t) if (File::isAbsolutePath (x) && File (x).isDirectory()) tabs.add (x);
    curTab = jlimit (0, jmax (0, tabs.size() - 1), cur);
    if (tabs.size() > 0 && File (tabs[curTab]) != folder) setFolder (File (tabs[curTab]));
    resized();
    repaint();
}

void CmdPanel::newTab()
{
    if (tabs.isEmpty()) tabs.add (folder.getFullPathName());
    tabs.insert (curTab + 1, folder.getFullPathName());
    ++curTab;
    resized();
    repaint();
}

void CmdPanel::closeTab()
{
    if (tabs.size() <= 1) return;
    tabs.remove (curTab);
    curTab = jlimit (0, tabs.size() - 1, curTab);
    const File f (tabs[curTab]);
    setFolder (f.isDirectory() ? f : folder);
    resized();
    repaint();
}

void CmdPanel::nextTab (int dir)
{
    const int n = tabs.size();
    if (n > 1) selectTab ((curTab + dir + n) % n);
}

void CmdPanel::selectTab (int i)
{
    if (i < 0 || i >= tabs.size()) return;
    curTab = i;
    const File f (tabs[i]);
    setFolder (f.isDirectory() ? f : folder);
    repaint();
}

void CmdPanel::toggleFlat()
{
    if (isVirtual()) return;
    flat = ! flat;
    marked.clear();
    reload();
}

bool CmdPanel::quickSearch (const String& text, int from, int dir)
{
    const int n = (int) entries.size();
    for (int c = 0; c < n; ++c)
    {
        const int i = ((from + c * dir) % n + n) % n;
        const auto& nm = entries[(size_t) i].name;
        const String shown = flat ? nm.fromLastOccurrenceOf ("\\", false, false) : nm;
        if (! entries[(size_t) i].up && shown.startsWithIgnoreCase (text))
        {
            qsearch = text;
            setCursor (i);
            return true;
        }
    }
    return false;
}

void CmdPanel::calcDirSize (int i)
{
    if (i < 0 || i >= (int) entries.size() || ! entries[(size_t) i].dir || entries[(size_t) i].up || isVirtual()) return;
    const File d = entries[(size_t) i].file;
    const String name = entries[(size_t) i].name;
    Component::SafePointer<CmdPanel> sp (this);
    Thread::launch ([sp, d, name]
    {
        int64 sum = 0;
        for (const auto& e : RangedDirectoryIterator (d, true, "*", File::findFiles))
            sum += e.getFileSize();
        MessageManager::callAsync ([sp, d, name, sum]
        {
            if (sp == nullptr || sp->folder != d.getParentDirectory()) return;
            sp->dirSizes[name] = sum;
            sp->repaint();
        });
    });
}

void CmdPanel::columns (Rectangle<int> row, Rectangle<int>& name, Rectangle<int>& ext, Rectangle<int>& size, Rectangle<int>& date) const
{
    row = row.reduced (4, 0);
    const bool narrow = row.getWidth() < 420;
    const float k = owner.fontSize / 13.5f;
    date = row.removeFromRight (narrow || ! owner.showDateCol ? 0 : roundToInt (owner.colW[2] * k));
    size = row.removeFromRight (owner.showSizeCol ? roundToInt ((narrow ? owner.colW[1] * 0.84f : (float) owner.colW[1]) * k) : 0);
    ext = row.removeFromRight (owner.showExtCol ? roundToInt (owner.colW[0] * k) : 0);
    name = row;
}

std::vector<CmdPanel::DriveBtn> CmdPanel::driveButtons() const
{
    std::vector<DriveBtn> v;
    auto r = driveArea().reduced (4, 3);
    for (auto& d : drives)
        v.push_back ({ r.removeFromLeft (38), d, 0 });
    if (keep != nullptr)
    {
        const int w = 30 + jmin (120, GlyphArrangement::getStringWidthInt (FontOptions (13.0f), keep->info().name));
        v.push_back ({ r.removeFromLeft (w), File(), 2 });
        v.push_back ({ r.removeFromLeft (20), File(), 3 });
        r.removeFromLeft (4);
    }
    v.push_back ({ r.removeFromLeft (28), File(), 1 });   // „\“ = koreň disku
    return v;
}

void CmdPanel::disconnectFtp()
{
    if (keep == nullptr && ftp == nullptr) return;
    const bool wasRemote = ftp != nullptr;
    const String host = (keep != nullptr ? keep : ftp)->info().host;
    keep.reset();
    if (wasRemote) setFolder (folder);   // späť na disk (spojenie sa zavrie)
    else repaint();
    owner.message (tr ("FTP odpojené: ") + host);
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
    if (thumbs)
    {
        if (! la.contains (p)) return -1;
        const int c = (p.x - la.getX()) / (owner.thumbSide + 12);
        if (c >= gridCols()) return -1;
        const int idx = top + ((p.y - la.getY()) / (owner.thumbSide + 30)) * gridCols() + c;
        return idx < (int) entries.size() ? idx : -1;
    }
    const int i = top + (p.y - la.getY()) / rowH;
    return i < (int) entries.size() ? i : -1;
}

void CmdPanel::setFolder (const File& f, const File& select)
{
    if (! f.isDirectory()) return;
    renameEd.reset();
    const bool wasRemote = ftp != nullptr || arc != File() || net;
    net = false;
    netIp = {};
    if (ftp != nullptr) { keep = ftp; keepDir = rdir; }
    ftp.reset();
    rdir = {};
    arc = File();
    adir = {};
    arcItems.clear();
    loading = false;
    if (f != folder || wasRemote) { marked.clear(); cursor = 0; top = 0; }
    if (f != folder) { dirSizes.clear(); flat = false; }
    if (wasRemote) MessageManager::callAsync ([sp = Component::SafePointer<CmdPanel> (this)] { if (sp != nullptr) sp->owner.updateNetDetail(); });
    folder = f;
    lastPerDrive[Transfer::rootOf (folder).getFullPathName().toLowerCase()] = folder;
    if (tabs.isEmpty()) tabs.add (folder.getFullPathName());
    else tabs.set (curTab, folder.getFullPathName());
    reload();
    if (select != File()) selectFile (select);
    else setCursor (cursor);
    owner.saveState();   // priečinok sa pamätá hneď (aj keď sa program nezavrie normálne, napr. pri aktualizácii)
}

void CmdPanel::setRemote (std::shared_ptr<FtpSession> s, const String& dir, const String& selectName)
{
    renameEd.reset();
    const String d = dir.isEmpty() ? String ("/") : dir;
    if (s != ftp || d != rdir) { marked.clear(); cursor = 0; top = 0; }
    ftp = std::move (s);
    keep = ftp;
    rdir = d;
    pendingSelect = selectName;
    reload();
}

String CmdPanel::displayPath() const
{
    if (net)
    {
        if (netIp.isNotEmpty()) return "\\\\" + netIp;
        auto& e = owner.netEngine();
        String t = tr ("Sieť  ") + e.range();
        if (e.isScanning()) t << tr ("   prehľadávam ") << String (e.progressDone()) << " / " << String (e.progressTotal());
        else if (e.newCount() > 0) t << tr ("   nové zariadenia: ") << String (e.newCount());
        return t;
    }
    if (arc != File()) return arc.getFullPathName() + (adir.isEmpty() ? String() : "\\" + adir.replaceCharacter ('/', '\\'));
    if (flat && ftp == nullptr) return folder.getFullPathName() + tr ("   (všetky súbory z podpriečinkov – Ctrl+B)");
    return ftp != nullptr ? "ftp://" + ftp->info().host + rdir : folder.getFullPathName();
}

void CmdPanel::setArchive (const File& a, const String& dir, const String& selectName)
{
    renameEd.reset();
    if (a != arc || dir != adir) { marked.clear(); cursor = 0; top = 0; }
    const bool load = a != arc;
    if (ftp != nullptr) { keep = ftp; keepDir = rdir; ftp.reset(); }
    arc = a;
    adir = dir;
    pendingSelect = selectName;
    if (load)
    {
        String err;
        if (! archive::list (arc, arcItems, err))
        {
            owner.message (err, true);
            const File f = arc;
            arc = File();
            setFolder (f.getParentDirectory(), f);
            return;
        }
    }
    fillArchive();
}

void CmdPanel::fillArchive()
{
    const String curName = pendingSelect.isNotEmpty() ? pendingSelect
                         : (cursor >= 0 && cursor < (int) entries.size() ? entries[(size_t) cursor].name : String());
    pendingSelect = {};
    entries.clear();
    Entry up;
    up.name = "..";
    up.dir = up.up = true;
    entries.push_back (up);
    const auto tmp = File::getSpecialLocation (File::tempDirectory);
    for (auto& it : arcItems)
    {
        const String parent = it.path.contains ("/") ? it.path.upToLastOccurrenceOf ("/", false, false) : String();
        if (parent != adir) continue;
        Entry en;
        en.name = it.path.fromLastOccurrenceOf ("/", false, false);
        en.dir = it.dir;
        en.size = it.dir ? 0 : it.size;
        en.mod = it.mod;
        en.rpath = it.path;
        if (! it.dir) en.ext = en.name.contains (".") ? en.name.fromLastOccurrenceOf (".", false, false) : String();
        en.colour = it.dir ? kindColour (Kind::Folder) : kindColour (kindOf (tmp.getChildFile (en.name)));
        entries.push_back (en);
    }
    sortEntries();
    std::set<String> still;
    for (auto& e : entries) if (marked.count (e.name)) still.insert (e.name);
    marked.swap (still);
    cursor = 0;
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].name == curName) { cursor = i; break; }
    setCursor (cursor);
    repaint();
}

bool CmdPanel::hasName (const String& n) const
{
    for (auto& e : entries) if (! e.up && e.name == n) return true;
    return false;
}

const CmdPanel::Entry* CmdPanel::currentEntry() const
{
    if (cursor < 0 || cursor >= (int) entries.size() || entries[(size_t) cursor].up) return nullptr;
    return &entries[(size_t) cursor];
}

std::vector<CmdPanel::Entry> CmdPanel::selectedEntries() const
{
    std::vector<Entry> v;
    for (auto& e : entries) if (! e.up && marked.count (e.name)) v.push_back (e);
    if (v.empty()) if (auto* c = currentEntry()) v.push_back (*c);
    return v;
}

void CmdPanel::fillRemote (const std::vector<FtpEntry>& v)
{
    const String curName = pendingSelect.isNotEmpty() ? pendingSelect
                         : (cursor >= 0 && cursor < (int) entries.size() ? entries[(size_t) cursor].name : String());
    pendingSelect = {};
    entries.clear();
    if (rdir != "/")
    {
        Entry up;
        up.name = "..";
        up.dir = up.up = true;
        up.rpath = FtpSession::parent (rdir);
        entries.push_back (up);
    }
    const auto tmp = File::getSpecialLocation (File::tempDirectory);
    for (auto& f : v)
    {
        Entry en;
        en.name = f.name;
        en.dir = f.dir;
        en.size = f.dir ? 0 : f.size;
        en.mod = f.mod;
        en.rpath = FtpSession::join (rdir, f.name);
        if (! f.dir) en.ext = f.name.contains (".") ? f.name.fromLastOccurrenceOf (".", false, false) : String();
        en.colour = f.dir ? kindColour (Kind::Folder) : kindColour (kindOf (tmp.getChildFile (f.name)));
        entries.push_back (en);
    }
    sortEntries();
    std::set<String> still;
    for (auto& e : entries) if (marked.count (e.name)) still.insert (e.name);
    marked.swap (still);
    cursor = 0;
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].name == curName) { cursor = i; break; }
    setCursor (cursor);
    repaint();
}

void CmdPanel::setNet (const String& hostIp)
{
    renameEd.reset();
    if (ftp != nullptr) { keep = ftp; keepDir = rdir; ftp.reset(); rdir = {}; }
    arc = File();
    adir = {};
    flat = false;
    const bool change = ! net || hostIp != netIp;
    const String prevIp = netIp;
    net = true;
    netIp = hostIp;
    if (change) { marked.clear(); cursor = 0; top = 0; }
    pendingSelect = hostIp.isEmpty() ? prevIp : String();
    auto& e = owner.netEngine();
    if (e.hosts.empty() && ! e.isScanning()) e.start (e.range());
    fillNet();
    owner.updateNetDetail();
}

int CmdPanel::netHostIndex() const
{
    if (! net || cursor < 0 || cursor >= (int) entries.size()) return -1;
    if (netIp.isNotEmpty())
    {
        auto& h = owner.netEngine().hosts;
        for (int i = 0; i < (int) h.size(); ++i) if (h[(size_t) i].ipText == netIp) return i;
        return -1;
    }
    return entries[(size_t) cursor].hostIdx;
}

void CmdPanel::fillNet()
{
    const String curKey = pendingSelect.isNotEmpty() ? pendingSelect
                        : (cursor >= 0 && cursor < (int) entries.size() ? entries[(size_t) cursor].rpath : String());
    pendingSelect = {};
    entries.clear();
    Entry up;
    up.name = "..";
    up.dir = up.up = true;
    entries.push_back (up);
    auto& eng = owner.netEngine();
    if (netIp.isEmpty())
    {
        std::vector<int> order;
        for (int i = 0; i < (int) eng.hosts.size(); ++i) order.push_back (i);
        std::sort (order.begin(), order.end(), [&] (int a, int b) { return eng.hosts[(size_t) a].ip < eng.hosts[(size_t) b].ip; });
        for (int i : order)
        {
            const auto& h = eng.hosts[(size_t) i];
            Entry en;
            const String dn = eng.displayName (h);
            en.name = dn.isNotEmpty() ? dn : h.ipText;
            en.rpath = h.ipText;
            en.hostIdx = i;
            en.dir = ! h.shares.isEmpty();
            en.colour = h.isNew ? Colour (0xffe0a040) : Colour (0xff7fbf7a);
            entries.push_back (en);
        }
    }
    else
    {
        for (auto& h : eng.hosts)
            if (h.ipText == netIp)
                for (auto& sh : h.shares)
                {
                    Entry en;
                    const bool printer = sh.startsWithChar ('*');
                    en.name = sh.trimCharactersAtStart ("*");
                    en.dir = ! printer;
                    en.file = File ("\\\\" + netIp + "\\" + en.name);
                    en.rpath = en.name;
                    en.colour = printer ? col::textDim : kindColour (Kind::Folder);
                    entries.push_back (en);
                }
    }
    cursor = 0;
    for (int i = 0; i < (int) entries.size(); ++i)
        if (! entries[(size_t) i].up && entries[(size_t) i].rpath == curKey) { cursor = i; break; }
    setCursor (cursor);
    repaint();
}

void CmdPanel::netColumns (Rectangle<int> row, Rectangle<int>& n, Rectangle<int>& ip, Rectangle<int>& ven, Rectangle<int>& svc) const
{
    row = row.reduced (4, 0);
    const int w = row.getWidth();
    n = row.removeFromLeft (w * 32 / 100);
    ip = row.removeFromLeft (110);
    ven = row.removeFromLeft (row.getWidth() * 45 / 100);
    svc = row;
}

void CmdPanel::reload()
{
    if (net) { fillNet(); return; }
    if (arc != File())
    {
        String err;
        if (! arc.existsAsFile() || ! archive::list (arc, arcItems, err)) { const File f = arc; setFolder (f.getParentDirectory()); return; }
        fillArchive();
        return;
    }
    if (ftp != nullptr)
    {
        const int gen = ++listGen;
        loading = true;
        repaint();
        auto s = ftp;
        const String dir = rdir;
        Component::SafePointer<CmdPanel> sp (this);
        Thread::launch ([s, dir, gen, sp]
        {
            std::vector<FtpEntry> v;
            String err;
            const bool ok = s->list (dir, v, err);
            MessageManager::callAsync ([sp, v, err, ok, gen]
            {
                if (sp == nullptr || sp->listGen != gen || sp->ftp == nullptr) return;
                sp->loading = false;
                if (! ok) sp->owner.message (tr ("Zoznam zo servera sa nedá načítať: ") + err, true);
                sp->fillRemote (v);
            });
        });
        return;
    }
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
    for (const auto& e : RangedDirectoryIterator (folder, flat, "*", flat ? File::findFiles : File::findFilesAndDirectories))
    {
        if (flat && entries.size() > 50000) break;
        Entry en;
        en.file = e.getFile();
        if (e.isHidden())
        {
            const int hk = hiddenKind (en.file);
            if (hk == 2 ? ! owner.showSystem : (hk == 1 && ! owner.showHidden)) continue;
            en.hidden = hk != 0;
        }
        en.dir = e.isDirectory();
        en.name = flat ? en.file.getRelativePathFrom (folder) : en.file.getFileName();
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
    if (renameEd != nullptr || ftp != nullptr || arc != File() || net) return;
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
    if (ftp != nullptr || arc != File() || net) return v;
    for (auto& e : entries)
        if (! e.up && marked.count (e.name)) v.add (e.file);
    if (v.isEmpty())
        if (auto f = currentFile(); f != File()) v.add (f);
    return v;
}

File CmdPanel::currentFile() const
{
    if (ftp != nullptr || arc != File() || net) return {};
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
    owner.cursorMoved (*this);
}

int CmdPanel::gridCols() const
{
    return thumbs ? jmax (1, listArea().getWidth() / (owner.thumbSide + 12)) : 1;
}

int CmdPanel::visibleRows() const
{
    if (thumbs) return jmax (1, listArea().getHeight() / (owner.thumbSide + 30)) * gridCols();
    return jmax (1, listArea().getHeight() / rowH);
}

Rectangle<int> CmdPanel::itemRect (int i) const
{
    const auto la = listArea();
    if (! thumbs) return la.withHeight (rowH).translated (0, (i - top) * rowH);
    const int cols = gridCols(), rel = i - top, cw = owner.thumbSide + 12, ch = owner.thumbSide + 30;
    return { la.getX() + (rel % cols) * cw, la.getY() + (rel / cols) * ch, cw, ch };
}

void CmdPanel::setThumbMode (bool on)
{
    thumbs = on;
    top = 0;
    ensureVisible();
    repaint();
}

int CmdPanel::markDiff (const CmdPanel& o)
{
    std::map<String, const Entry*> other;
    for (auto& e : o.entries) if (! e.up && ! e.dir) other[e.name.toLowerCase()] = &e;
    marked.clear();
    for (auto& e : entries)
    {
        if (e.up || e.dir) continue;
        auto it = other.find (e.name.toLowerCase());
        if (it == other.end() || it->second->size != e.size || e.mod.toMilliseconds() > it->second->mod.toMilliseconds() + 2000)
            marked.insert (e.name);
    }
    repaint();
    return (int) marked.size();
}

void CmdPanel::ensureVisible()
{
    if (thumbs)
    {
        const int cols = gridCols(), vis = visibleRows();
        if (cursor < top) top = (cursor / cols) * cols;
        if (cursor >= top + vis) top = (cursor / cols - vis / cols + 1) * cols;
        top = jlimit (0, jmax (0, ((int) entries.size() - 1) / cols * cols), (top / cols) * cols);
        updateScroll();
        return;
    }
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
    if (thumbs) top = (top / gridCols()) * gridCols();
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
    if (net)
    {
        if (e.up) goUp();
        else if (netIp.isEmpty()) { if (e.dir) setNet (e.rpath); }
        else if (e.dir) setFolder (e.file);
        return;
    }
    if (e.up) goUp();
    else if (ftp != nullptr) { if (e.dir) setRemote (ftp, e.rpath); else owner.fetchRemote (e, ftp, false); }
    else if (arc != File()) { if (e.dir) setArchive (arc, e.rpath); else owner.fetchArchive (e, arc, false); }
    else if (e.dir) setFolder (e.file);
    else if (archive::isArchive (e.file)) setArchive (e.file, {});
    else owner.openFile (e.file);
}

void CmdPanel::goUp()
{
    if (net)
    {
        if (netIp.isNotEmpty()) setNet ({});
        else { setFolder (folder); owner.updateNetDetail(); }
        return;
    }
    if (arc != File())
    {
        if (adir.isEmpty()) { const File a = arc; setFolder (a.getParentDirectory(), a); }
        else setArchive (arc, adir.contains ("/") ? adir.upToLastOccurrenceOf ("/", false, false) : String(), adir.fromLastOccurrenceOf ("/", false, false));
        return;
    }
    if (ftp != nullptr)
    {
        if (rdir == "/") return;
        setRemote (ftp, FtpSession::parent (rdir), rdir.trimCharactersAtEnd ("/").fromLastOccurrenceOf ("/", false, false));
        return;
    }
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
        if (e.up) continue;   // * prehodi oznacenie suborov aj priecinkov
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
    if (net) { owner.renameNetHost(); return; }
    if (arc != File()) { owner.message (tr ("V archíve sa premenovať nedá.")); return; }
    if (cursor < 0 || cursor >= (int) entries.size() || entries[(size_t) cursor].up) return;
    ensureVisible();
    const auto e = entries[(size_t) cursor];
    auto row = itemRect (cursor);
    Rectangle<int> n, x, s, d;
    columns (row, n, x, s, d);
    if (thumbs) { n = row.removeFromBottom (24).withTrimmedLeft (-14); x = n.withLeft (n.getRight()); }
    renameEd = std::make_unique<TextEditor>();
    renameEd->setFont (FontOptions (owner.fontSize));
    renameEd->setText (flat ? e.file.getFileName() : e.name, false);
    renameEd->setBounds (n.withRight (x.getRight()).withTrimmedLeft (18).expanded (0, 1));
    addAndMakeVisible (*renameEd);
    renameEd->grabKeyboardFocus();
    const String rn = flat ? e.file.getFileName() : e.name;
    const int dot = e.dir ? -1 : rn.lastIndexOfChar ('.');
    renameEd->setHighlightedRegion ({ 0, dot > 0 ? dot : rn.length() });

    Component::SafePointer<CmdPanel> sp (this);
    auto finish = [sp] (bool commit)
    {
        if (sp == nullptr || sp->renameEd == nullptr) return;
        const String newName = sp->renameEd->getText().trim();
        const File f = sp->currentFile();
        MessageManager::callAsync ([sp] { if (sp != nullptr) { sp->renameEd.reset(); sp->grabKeyboardFocus(); } });
        if (sp->ftp != nullptr)
        {
            const auto* ce = sp->currentEntry();
            if (! commit || ce == nullptr || newName.isEmpty() || newName == ce->name) return;
            if (newName.containsAnyOf ("/\\")) { sp->owner.message (tr ("Názov nesmie obsahovať / ani \\"), true); return; }
            if (! sp->ftp->rename (ce->rpath, FtpSession::join (sp->rdir, newName)))
            {
                sp->owner.message (tr ("Na serveri sa nepodarilo premenovať."), true);
                return;
            }
            sp->setRemote (sp->ftp, sp->rdir, newName);
            return;
        }
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
        sp->owner.pushUndo ({ { f, dest } });
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
    if (net) { owner.netMenu(); return; }
    PopupMenu m;
    const bool any = ! selectedEntries().empty();
    const auto* ce = currentEntry();
    const bool isFile = ce != nullptr && ! ce->dir;
    m.addItem (1, tr ("Otvoriť") + "\tEnter", cursor >= 0 && cursor < (int) entries.size());
    m.addItem (2, tr ("Zobraziť") + "\tF3", isFile);
    m.addItem (3, tr ("Upraviť") + "\tF4", isFile && arc == File());
    m.addSeparator();
    m.addItem (4, tr ("Kopírovať") + "\tF5", any);
    m.addItem (5, tr ("Presunúť") + "\tF6", any);
    m.addItem (6, tr ("Premenovať") + "\tShift+F6", ce != nullptr);
    m.addItem (7, tr ("Odstrániť") + "\tF8", any);
    m.addSeparator();
    m.addItem (8, tr ("Nový priečinok") + "\tF7");
    m.addItem (9, tr ("Kopírovať cestu"), any);
    m.addItem (10, tr ("Ukázať v Prieskumníkovi"), ftp == nullptr && arc == File());
    m.addSeparator();
    m.addItem (11, tr ("Zbaliť do ZIP") + "\tAlt+F5", any && ! isVirtual());
    m.addItem (12, tr ("Rozbaliť") + "\tAlt+F9", ! isVirtual() && ce != nullptr && archive::isArchive (ce->file));
    m.addItem (13, tr ("Hromadné premenovanie") + "\tCtrl+M", any && ! isVirtual());
    m.addItem (14, tr ("Porovnať podľa obsahu"), ! isVirtual());
    {
        bool img = false;
        for (auto& f : selection()) img = img || kindOf (f) == Kind::Image;
        m.addSeparator();
        m.addItem (15, tr ("Zmeniť veľkosť obrázkov…"), img && owner.onPlayerTool != nullptr);
        m.addItem (16, tr ("Previesť do iného formátu…"), any && ! isVirtual() && owner.onPlayerTool != nullptr);
        m.addItem (17, tr ("Nájsť podobné fotky v priečinku"), ! isVirtual() && owner.onPlayerTool != nullptr);
        m.addItem (18, tr ("Nájsť duplikáty v priečinku"), ! isVirtual() && owner.onPlayerTool != nullptr);
        m.addItem (19, tr ("Čo zaberá miesto"), ! isVirtual());
    }
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
                for (auto& e : sp->selectedEntries())
                    paths.add (sp->ftp != nullptr ? "ftp://" + sp->ftp->info().host + e.rpath
                               : sp->arc != File() ? sp->arc.getFullPathName() + "\\" + e.rpath.replaceCharacter ('/', '\\')
                               : e.file.getFullPathName());
                SystemClipboard::copyTextToClipboard (paths.joinIntoString ("\r\n"));
                o.message (tr ("Cesta skopírovaná do schránky"));
                break;
            }
            case 11: o.packZip(); break;
            case 12: o.unpack(); break;
            case 13: o.multiRename(); break;
            case 14: o.compareFiles(); break;
            case 15: case 16: case 17: case 18:
                if (o.onPlayerTool) o.onPlayerTool (r - 14, sp->selection(), sp->getFolder());
                break;
            case 19: o.spaceMap(); break;
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

    // karty
    if (topOff() > 0)
    {
        g.setColour (col::bg);
        g.fillRect (tabArea());
        const auto tr_ = tabRects();
        g.setFont (FontOptions (12.5f));
        for (int i = 0; i < (int) tr_.size(); ++i)
        {
            g.setColour (i == curTab ? col::accent : col::panelHi);
            g.fillRoundedRectangle (tr_[(size_t) i].toFloat(), 3.0f);
            g.setColour (i == curTab ? col::selText : col::text);
            const File tf (tabs[i]);
            g.drawText (tf.getFileName().isNotEmpty() ? tf.getFileName() : tf.getFullPathName(), tr_[(size_t) i].reduced (6, 0), Justification::centredLeft, true);
        }
    }

    // lišta diskov
    auto da = driveArea();
    g.setColour (col::panel);
    g.fillRect (da);
    const auto root = Transfer::rootOf (folder);
    g.setFont (FontOptions (13.0f));
    for (auto& [r, d, kind] : driveButtons())
    {
        const bool cur = (kind == 0 && ! isVirtual() && sameFile (d, root)) || (kind == 2 && ftp != nullptr) || (kind == 4 && net);
        if (cur)
        {
            g.setColour (col::accent);
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
        }
        g.setColour (col::text);
        if (kind == 1)
            g.drawText ("\\", r, Justification::centred, false);
        else if (kind == 4)
        {
            // dve prepojené obrazovky = sieť
            auto ic = r.toFloat().removeFromLeft (22).reduced (3.0f, 4.0f);
            const float w = ic.getWidth() * 0.5f, h = ic.getHeight() * 0.45f;
            g.drawRect (Rectangle<float> (ic.getX(), ic.getY(), w, h), 1.1f);
            g.drawRect (Rectangle<float> (ic.getRight() - w, ic.getBottom() - h, w, h), 1.1f);
            g.drawLine (ic.getX() + w * 0.5f, ic.getY() + h, ic.getX() + w * 0.5f, ic.getBottom() - h * 0.5f, 1.0f);
            g.drawLine (ic.getX() + w * 0.5f, ic.getBottom() - h * 0.5f, ic.getRight() - w, ic.getBottom() - h * 0.5f, 1.0f);
            g.drawText (tr ("Sieť"), r.withTrimmedLeft (22), Justification::centredLeft, true);
        }
        else if (kind == 2)
        {
            // zemeguľa = FTP spojenie
            auto gl = r.toFloat().removeFromLeft (22).reduced (4.0f, 3.0f);
            gl = gl.withSizeKeepingCentre (jmin (gl.getWidth(), gl.getHeight()), jmin (gl.getWidth(), gl.getHeight()));
            g.drawEllipse (gl, 1.2f);
            g.drawEllipse (gl.reduced (gl.getWidth() * 0.28f, 0.0f), 1.0f);
            g.drawLine (gl.getX(), gl.getCentreY(), gl.getRight(), gl.getCentreY(), 1.0f);
            g.drawText (keep->info().name, r.withTrimmedLeft (24), Justification::centredLeft, true);
        }
        else if (kind == 3)
        {
            auto x = r.toFloat().withSizeKeepingCentre (8.0f, 8.0f);
            g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.4f);
            g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.4f);
        }
        else
        {
            drawDriveIcon (g, r.toFloat().removeFromLeft (20).reduced (2, 3), col::text);
            g.drawText (d.getFullPathName().upToFirstOccurrenceOf (":", false, false).toLowerCase(),
                        r.withTrimmedLeft (22), Justification::centredLeft, false);
        }
    }
    {
        const int64 freeB = folder.getBytesFreeOnVolume(), totalB = folder.getVolumeTotalSize();
        if (totalB > 0 && ! isVirtual())
        {
            auto info = da.reduced (8, 0);
            info.setLeft (driveButtons().back().r.getRight() + 8);
            g.setColour (col::text);
            g.drawText (sizeText (freeB) + tr (" voľných z ") + sizeText (totalB), info, Justification::centredRight, true);
        }
    }

    // cesta (aktívny panel = farebná)
    auto pa = pathArea();
    g.setColour (active ? col::accent : col::panelHi);
    g.fillRect (pa);
    g.setColour (active ? col::selText : col::text);
    g.fillPath (makeIcon (Icon::Copy, copyPathArea().toFloat().reduced (5.0f, 3.0f)));
    if (net) g.fillPath (makeIcon (Icon::Pencil, netEditArea().toFloat().reduced (6.0f, 3.0f)));   // zmeniť rozsah siete
    g.setFont (FontOptions (13.5f));
    g.drawText (displayPath() + (loading ? String::fromUTF8 ("   \xe2\x80\xa6") : String()), pa.withTrimmedLeft (copyPathArea().getWidth()).withTrimmedRight (netEditArea().getWidth()).reduced (4, 0), Justification::centredLeft, true);

    // hlavička stĺpcov
    auto ha = headArea();
    g.setColour (col::panel);
    g.fillRect (ha);
    g.setColour (col::line);
    g.fillRect (ha.removeFromBottom (1));
    {
        Rectangle<int> n, x, s, d;
        if (isNetHostList()) netColumns (headArea().withTrimmedRight (12), n, x, s, d);
        else columns (headArea().withTrimmedRight (12), n, x, s, d);
        const Rectangle<int> rs[] = { n, x, s, d };
        const char* titles[] = { "Názov", "Prípona", "Veľkosť", "Dátum" };
        const char* netTitles[] = { "Zariadenie", "IP", "Výrobca", "Služby" };
        g.setColour (col::line.brighter (0.3f));
        for (int i = 1; i < 4; ++i)
            if (rs[i].getWidth() > 0) g.fillRect (rs[i].getX() - 2, headArea().getY() + 3, 1, headArea().getHeight() - 6);
        g.setFont (FontOptions (12.5f));
        for (int i = 0; i < 4; ++i)
        {
            if (rs[i].getWidth() <= 0) continue;
            String t = tr (isNetHostList() ? netTitles[i] : titles[i]);
            if (i == sortCol && ! net) t << String::fromUTF8 (sortDesc ? " \xe2\x96\xbc" : " \xe2\x96\xb2");
            g.setColour (i == sortCol ? col::text.brighter (0.2f) : col::text);
            g.drawText (t, rs[i].withTrimmedLeft (i == 0 ? 18 : 4).withTrimmedRight (i >= 2 ? 2 : 0),
                        i >= 2 && ! isNetHostList() ? Justification::centredRight : Justification::centredLeft, true);
        }
    }

    // zoznam
    auto la = listArea();
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (la);
        g.setFont (FontOptions (13.5f));
        const int vis = visibleRows() + 1;
        for (int i = top; i < jmin ((int) entries.size(), top + vis + (thumbs ? gridCols() : 0)); ++i)
        {
            const auto& e = entries[(size_t) i];
            auto row = itemRect (i);
            if (isNetHostList() && ! e.up && ! thumbs)
            {
                if (i == cursor)
                {
                    if (active && hasKeyboardFocus (true)) { g.setColour (col::accent); g.fillRect (row); }
                    else { g.setColour (col::accentHi); g.drawRect (row, 1); }
                }
                Rectangle<int> n, ipR, venR, svcR;
                netColumns (row, n, ipR, venR, svcR);
                g.setColour (e.colour);
                g.fillEllipse (n.removeFromLeft (18).toFloat().withSizeKeepingCentre (9.0f, 9.0f));
                const auto& eng = owner.netEngine();
                const NetHost* h = e.hostIdx >= 0 && e.hostIdx < (int) eng.hosts.size() ? &eng.hosts[(size_t) e.hostIdx] : nullptr;
                g.setColour (i == cursor && active ? col::selText : col::text);
                g.setFont (FontOptions (owner.fontSize));
                g.drawText (e.name + (h != nullptr && h->isNew ? tr ("  (nové)") : String()), n, Justification::centredLeft, true);
                if (h != nullptr)
                {
                    g.drawText (h->ipText, ipR.withTrimmedLeft (4), Justification::centredLeft, true);
                    g.drawText (h->vendor, venR.withTrimmedLeft (4), Justification::centredLeft, true);
                    g.drawText (eng.servicesText (*h), svcR.withTrimmedLeft (4), Justification::centredLeft, true);
                }
                continue;
            }
            if (thumbs)
            {
                auto cell = row.reduced (3);
                if (i == cursor)
                {
                    if (active && hasKeyboardFocus (true)) { g.setColour (col::accent); g.fillRoundedRectangle (cell.toFloat(), 3.0f); }
                    else { g.setColour (col::accentHi); g.drawRoundedRectangle (cell.toFloat(), 3.0f, 1.0f); }
                }
                const bool mk = isMarked (i);
                auto pic = cell.reduced (4).withTrimmedBottom (20).toFloat();
                Image im;
                if (! e.dir && ! isVirtual() && owner.getThumb) im = owner.getThumb (e.file);
                if (im.isValid())
                {
                    const auto fit = RectanglePlacement (RectanglePlacement::centred | RectanglePlacement::onlyReduceInSize)
                                         .appliedTo (im.getBounds().toFloat(), pic);
                    g.drawImage (im, fit);
                }
                else if (e.up)
                {
                    g.setColour (col::textDim);
                    g.setFont (FontOptions (28.0f));
                    g.drawText (String::fromUTF8 ("\xe2\x86\x91"), pic.toNearestInt(), Justification::centred, false);
                }
                else if (e.dir)
                {
                    auto b = pic.withSizeKeepingCentre (pic.getHeight() * 0.7f, pic.getHeight() * 0.5f);
                    g.setColour (e.colour);
                    g.fillRoundedRectangle (b.withWidth (b.getWidth() * 0.42f).withHeight (b.getHeight() * 0.25f).translated (0, -b.getHeight() * 0.12f), 2.0f);
                    g.fillRoundedRectangle (b.withTrimmedTop (b.getHeight() * 0.06f), 3.0f);
                }
                else
                {
                    g.setColour (e.colour);
                    g.fillRoundedRectangle (pic.withSizeKeepingCentre (pic.getHeight() * 0.4f, pic.getHeight() * 0.5f), 3.0f);
                }
                if (mk) { g.setColour (owner.markColour); g.drawRoundedRectangle (cell.toFloat().reduced (1.0f), 3.0f, 2.0f); }
                g.setColour (mk ? owner.markColour : (i == cursor && active ? col::selText : col::text));
                g.setFont (FontOptions (12.5f, mk ? Font::bold : Font::plain));
                g.drawText (e.name, cell.removeFromBottom (20).reduced (2, 0), Justification::centred, true);
                continue;
            }
            if (i == cursor)
            {
                if (active && hasKeyboardFocus (true)) { g.setColour (col::accent); g.fillRect (row); }
                else { g.setColour (col::accentHi); g.drawRect (row, 1); }
            }
            const bool mk = isMarked (i);
            Colour tc = mk ? owner.markColour : (i == cursor && active ? col::selText : col::text);
            if (e.hidden && ! mk) tc = tc.withAlpha (0.55f);
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
            g.setFont (FontOptions (owner.fontSize, mk ? Font::bold : Font::plain));
            const String shown = e.dir || ! owner.showExtCol ? e.name : (e.ext.isEmpty() ? e.name : e.name.dropLastCharacters (e.ext.length() + 1));
            g.drawText (shown, n, Justification::centredLeft, true);
            g.drawText (e.ext, x.withTrimmedLeft (4), Justification::centredLeft, true);
            if (net) continue;   // sieť: len názvy
            const auto ds = dirSizes.find (e.name);
            g.drawText (e.dir ? (ds != dirSizes.end() && ! e.up ? groupDigits (ds->second) : String ("<DIR>")) : groupDigits (e.size), s, Justification::centredRight, true);
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
    if (qsearch.isNotEmpty())
    {
        g.setColour (col::accent);
        g.fillRect (fa);
        g.setColour (col::selText);
        g.setFont (FontOptions (12.5f));
        g.drawText (tr ("Hľadať: ") + qsearch + "_", fa.reduced (6, 0), Justification::centredLeft, true);
        return;
    }
    if (net)
    {
        g.setColour (col::text);
        g.setFont (FontOptions (12.5f));
        g.drawText (netIp.isEmpty() ? tr ("Zariadenia: ") + String ((int) entries.size() - 1) + tr ("   Enter = zdieľané priečinky, Shift+F6 = pomenovať, Ctrl+R = prehľadať znova")
                                    : tr ("Zdieľané priečinky: ") + String ((int) entries.size() - 1),
                    fa.reduced (6, 0), Justification::centredLeft, true);
        return;
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

    // rýchle hľadanie (Ctrl+Alt+písmená, alebo len písmená podľa nastavenia)
    juce_wchar ch = key.getTextCharacter();
    const bool ctrlAlt = mods.isCtrlDown() && mods.isAltDown();
    if (ch < 32 && ctrlAlt && ((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9'))) ch = (juce_wchar) (k >= 'A' && k <= 'Z' ? k + 32 : k);
    if (qsearch.isNotEmpty())
    {
        if (k == KeyPress::escapeKey) { qsearch = {}; repaint(); return true; }
        if (k == KeyPress::backspaceKey) { qsearch = qsearch.dropLastCharacters (1); if (qsearch.isNotEmpty()) quickSearch (qsearch); repaint(); return true; }
        if (k == KeyPress::downKey) { quickSearch (qsearch, cursor + 1, 1); return true; }
        if (k == KeyPress::upKey) { quickSearch (qsearch, cursor - 1, -1); return true; }
        if (ch >= 32 && (! mods.isCtrlDown() || ctrlAlt)) { quickSearch (qsearch + String::charToString (ch), cursor); repaint(); return true; }
        qsearch = {};
        repaint();
    }
    // * + - (numericka klavesnica): oznacit / odznacit vsetko, oznacit / odznacit podla masky; Windows ich posiela ako znak
    if ((ch == '*' || ch == '+' || ch == '-') && ! mods.isCtrlDown() && ! mods.isAltDown() && owner.cmdLine.isEmpty())
    {
        if (ch == '*') invertMarks(); else markByMask (ch == '+');
        return true;
    }
    const bool numpad = k == KeyPress::numberPadAdd || k == KeyPress::numberPadSubtract || k == KeyPress::numberPadMultiply;
    if (ch > 32 && ch != 127 && ! numpad && (ctrlAlt || (! mods.isCtrlDown() && ! mods.isAltDown())))
    {
        if (ctrlAlt || owner.quickLetters) { quickSearch (String::charToString (ch), cursor); repaint(); }
        else owner.typeToCommandLine (String::charToString (ch));
        return true;
    }

    const int step = thumbs ? gridCols() : 1;
    if (k == KeyPress::upKey && ! mods.isAltDown()) { if (mods.isShiftDown()) toggleMark (cursor); setCursor (cursor - step); return true; }
    if (k == KeyPress::downKey && ! mods.isAltDown()) { if (mods.isShiftDown()) toggleMark (cursor); setCursor (cursor + step); return true; }
    if (thumbs && ! mods.isCtrlDown() && ! mods.isAltDown() && k == KeyPress::leftKey) { setCursor (cursor - 1); return true; }
    if (thumbs && ! mods.isCtrlDown() && ! mods.isAltDown() && k == KeyPress::rightKey) { setCursor (cursor + 1); return true; }
    if (mods.isCtrlDown() && mods.isShiftDown() && k == KeyPress::F1Key) { setThumbMode (true); return true; }
    if (mods.isCtrlDown() && ! mods.isShiftDown() && (k == KeyPress::F1Key || k == KeyPress::F2Key)) { setThumbMode (false); return true; }
    if (k == KeyPress::pageUpKey && mods.isCtrlDown()) { goUp(); return true; }
    if (k == KeyPress::pageUpKey) { setCursor (cursor - vis + 1); return true; }
    if (k == KeyPress::pageDownKey) { setCursor (cursor + vis - 1); return true; }
    if (k == KeyPress::homeKey) { setCursor (0); return true; }
    if (k == KeyPress::endKey) { setCursor ((int) entries.size() - 1); return true; }
    if (k == KeyPress::insertKey) { toggleMark (cursor); setCursor (cursor + 1); return true; }
    if (k == KeyPress::spaceKey && ! mods.isAnyModifierKeyDown()) { toggleMark (cursor); calcDirSize (cursor); return true; }
    if (k == KeyPress::returnKey && ! mods.isAnyModifierKeyDown()) { openCurrent(); return true; }
    if (k == KeyPress::returnKey && mods.isAltDown())
    {
        if (auto f = currentFile(); f != File()) winshell::properties (f); else if (! isVirtual()) winshell::properties (folder);
        return true;
    }
    if (k == KeyPress::returnKey && mods.isCtrlDown())
    {
        if (auto* e = currentEntry())
            owner.typeToCommandLine ((e->name.containsChar (' ') ? e->name.quoted() : e->name) + " ");
        return true;
    }
    if (mods.isCtrlDown() && (k == 'b' || k == 'B')) { toggleFlat(); return true; }
    if (k == KeyPress::backspaceKey) { goUp(); return true; }
    if (mods.isCtrlDown() && k == '\\') { if (ftp != nullptr) setRemote (ftp, "/"); else setFolder (Transfer::rootOf (folder)); return true; }
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
    if (qsearch.isNotEmpty()) { qsearch = {}; repaint(); }
    const auto p = e.getPosition();

    if (tabArea().contains (p))
    {
        const auto tr_ = tabRects();
        for (int i = 0; i < (int) tr_.size(); ++i)
            if (tr_[(size_t) i].contains (p)) { if (e.getNumberOfClicks() > 1) closeTab(); else selectTab (i); return; }
        return;
    }
    if (driveArea().contains (p))
    {
        for (auto& [r, d, kind] : driveButtons())
            if (r.contains (p))
            {
                if (kind == 3) { disconnectFtp(); return; }
                if (kind == 4) { owner.setActive (*this); setNet(); return; }
                if (kind == 2) { if (ftp == nullptr && keep != nullptr) setRemote (keep, keepDir); return; }
                if (kind == 1) { if (ftp != nullptr) setRemote (ftp, "/"); else setFolder (Transfer::rootOf (folder)); return; }
                if (e.mods.isRightButtonDown()) { showDriveMenu(); return; }
                const auto last = lastPerDrive[d.getFullPathName().toLowerCase()];
                setFolder (last.isDirectory() ? last : d);
                return;
            }
        owner.driveOverview();
        return;
    }
    if (net && netEditArea().contains (p))
    {
        owner.setActive (*this);
        owner.netRangeDialog();
        return;
    }
    if (copyPathArea().contains (p))
    {
        SystemClipboard::copyTextToClipboard (displayPath());
        owner.message (tr ("Cesta skopírovaná do schránky"));
        return;
    }
    if (headArea().contains (p) && separatorAt (p) >= 0)
    {
        colDrag = separatorAt (p);
        colDragStartX = p.x;
        colDragStartW = owner.colW[colDrag];
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
        rightDragCandidate = false;
        if (r < 0) return;
        if (isMarked (r) && ! isVirtual())
        {
            // ťahanie označených pravým tlačidlom → po pustení ponuka Kopírovať / Presunúť
            rightDragCandidate = true;
            rightAnchor = rightLast = r;
            setCursor (r);
            hold.startTimer (500);
            return;
        }
        markMode = ! isMarked (r);
        if (! entries[(size_t) r].up) { if (markMode) marked.insert (entries[(size_t) r].name); else marked.erase (entries[(size_t) r].name); }
        rightAnchor = rightLast = r;
        setCursor (r);
        hold.startTimer (500);
        return;
    }
    if (r >= 0) setCursor (r);
}

int CmdPanel::separatorAt (Point<int> p) const
{
    if (! headArea().contains (p)) return -1;
    Rectangle<int> n, x, s, d;
    columns (headArea().withTrimmedRight (12), n, x, s, d);
    const Rectangle<int> rs[] = { x, s, d };
    for (int i = 0; i < 3; ++i)
        if (rs[i].getWidth() > 0 && std::abs (p.x - (rs[i].getX() - 2)) <= 4) return i;
    return -1;
}

void CmdPanel::mouseMove (const MouseEvent& e)
{
    setMouseCursor (separatorAt (e.getPosition()) >= 0 ? MouseCursor::LeftRightResizeCursor
                    : ((net && netEditArea().contains (e.getPosition())) || copyPathArea().contains (e.getPosition())) ? MouseCursor::PointingHandCursor
                    : MouseCursor::NormalCursor);
}

void CmdPanel::mouseDrag (const MouseEvent& e)
{
    if (colDrag >= 0)
    {
        // stĺpce sú ukotvené vpravo: ťahanie čiarky doľava stĺpec napravo od nej rozšíri
        const float k = owner.fontSize / 13.5f;
        owner.colW[colDrag] = jlimit (30, 400, colDragStartW + roundToInt ((colDragStartX - e.getPosition().x) / k));
        owner.applyLook();
        return;
    }
    if (rightDown && rightDragCandidate)
    {
        if (e.getDistanceFromDragStart() < 8 || dragStarted) return;
        hold.stopTimer();
        dragStarted = true;
        const auto sel = selectedEntries();
        Image img (Image::ARGB, 220, 24, true);
        {
            Graphics g (img);
            g.setColour (col::accent.withAlpha (0.9f));
            g.fillRoundedRectangle (img.getBounds().toFloat(), 3.0f);
            g.setColour (col::selText);
            g.setFont (FontOptions (13.0f));
            g.drawText (sel.size() == 1 ? sel[0].name : items ((int) sel.size()), img.getBounds().reduced (8, 0), Justification::centredLeft, true);
        }
        if (auto* c = DragAndDropContainer::findParentDragContainerFor (this))
            c->startDragging ("kc-right", this, ScaledImage (img), true);
        return;
    }
    if (rightDown)
    {
        auto la = listArea();
        auto p = e.getPosition();
        if (p.y < la.getY() && top > 0) { --top; updateScroll(); }
        if (p.y > la.getBottom() && top + visibleRows() < (int) entries.size()) { ++top; updateScroll(); }
        const int y = jlimit (la.getY(), la.getBottom() - 1, p.y);
        const int r = thumbs ? rowAt ({ jlimit (la.getX(), la.getRight() - 1, p.x), y }) : jmin ((int) entries.size() - 1, top + (y - la.getY()) / rowH);
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
    const auto sel = selectedEntries();
    if (sel.empty()) return;
    dragStarted = true;
    Image img (Image::ARGB, 220, 24, true);
    {
        Graphics g (img);
        g.setColour (col::accent.withAlpha (0.9f));
        g.fillRoundedRectangle (img.getBounds().toFloat(), 3.0f);
        g.setColour (col::selText);
        g.setFont (FontOptions (13.0f));
        g.drawText (sel.size() == 1 ? sel[0].name : items ((int) sel.size()),
                    img.getBounds().reduced (8, 0), Justification::centredLeft, true);
    }
    if (auto* c = DragAndDropContainer::findParentDragContainerFor (this))
        c->startDragging ("kc", this, ScaledImage (img), true);
}

void CmdPanel::mouseUp (const MouseEvent&)
{
    if (colDrag >= 0)
    {
        colDrag = -1;
        owner.saveState();
    }
    if (rightDown && rightDragCandidate && ! dragStarted && ! menuShown && rightAnchor >= 0 && rightAnchor < (int) entries.size())
    {
        marked.erase (entries[(size_t) rightAnchor].name);   // obyčajný klik = odznačiť
        repaint();
    }
    rightDragCandidate = false;
    rightDown = false;
    hold.stopTimer();
}

void CmdPanel::mouseDoubleClick (const MouseEvent& e)
{
    if (e.mods.isLeftButtonDown() && rowAt (e.getPosition()) >= 0) openCurrent();
}

void CmdPanel::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    int step = w.deltaY > 0 ? -jmax (1, roundToInt (w.deltaY * 12)) : jmax (1, roundToInt (-w.deltaY * 12));
    if (thumbs) step = (step > 0 ? 1 : -1) * gridCols();
    top = jlimit (0, jmax (0, (int) entries.size() - visibleRows()), top + step);
    if (thumbs) top = (top / gridCols()) * gridCols();
    updateScroll();
    repaint();
}

bool CmdPanel::isInterestedInDragSource (const SourceDetails& d)
{
    auto* src = dynamic_cast<CmdPanel*> (d.sourceComponent.get());
    return (d.description == var ("kc") || d.description == var ("kc-right")) && src != nullptr && src != this && ! net;
}

void CmdPanel::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    repaint();
    auto* src = dynamic_cast<CmdPanel*> (d.sourceComponent.get());
    if (src == nullptr) return;
    if (d.description == var ("kc-right"))
    {
        PopupMenu m;
        m.addItem (1, tr ("Kopírovať sem"));
        m.addItem (2, tr ("Presunúť sem"));
        m.addSeparator();
        m.addItem (3, tr ("Zrušiť"));
        Component::SafePointer<CmdPanel> sp (this), ss (src);
        m.showMenuAsync (PopupMenu::Options().withMousePosition(), [sp, ss] (int r)
        {
            if (sp != nullptr && ss != nullptr && (r == 1 || r == 2)) sp->owner.dropped (*ss, *sp, r == 2);
        });
        return;
    }
    owner.dropped (*src, *this, ModifierKeys::currentModifiers.isShiftDown());
}

// ================================================================ Commander

Commander::Commander (PropertiesFile& s) : settings (s)
{
    fontSize = (float) settings.getDoubleValue ("kcFont", 13.5);
    markColour = Colour::fromString (settings.getValue ("kcMarkCol", "ffe5605a"));
    showExtCol = settings.getBoolValue ("kcColExt", true);
    showSizeCol = settings.getBoolValue ("kcColSize", true);
    showDateCol = settings.getBoolValue ("kcColDate", true);
    quickLetters = settings.getBoolValue ("kcQuickLetters", false);
    {
        StringArray w = StringArray::fromTokens (settings.getValue ("kcColW", "56,100,118"), ",", {});
        for (int i = 0; i < 3 && i < w.size(); ++i) colW[i] = jlimit (30, 400, w[i].getIntValue());
    }
    cmdLine.setFont (FontOptions (13.5f));
    cmdLine.setColour (TextEditor::backgroundColourId, col::docBg);
    cmdLine.setColour (TextEditor::outlineColourId, col::line);
    cmdLine.onReturnKey = [this] { runCommand (cmdLine.getText()); };
    cmdLine.onEscapeKey = [this] { cmdLine.clear(); act->grabKeyboardFocus(); };
    addAndMakeVisible (cmdLine);
    showHidden = settings.getBoolValue ("kcHidden", false);
    showSystem = settings.getBoolValue ("kcSystem", false);
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
    ftpJob.reset();
    task.reset();
}

void Commander::start()
{
    if (! started)
    {
        const auto home = File::getSpecialLocation (File::userHomeDirectory);
        File l (settings.getValue ("kcLeft")), r (settings.getValue ("kcRight"));
        left.setFolder (l.isDirectory() ? l : home);
        right.setFolder (r.isDirectory() ? r : home);
        left.setTabs (StringArray::fromLines (settings.getValue ("kcTabsL")), settings.getIntValue ("kcTabL", 0));
        right.setTabs (StringArray::fromLines (settings.getValue ("kcTabsR")), settings.getIntValue ("kcTabR", 0));
        act = settings.getIntValue ("kcActive", 0) == 1 ? &right : &left;
        left.active = act == &left;
        right.active = act == &right;
        applyLook();
        started = true;   // až teraz – saveState() počas obnovy by prepísal uložené karty
    }
    else
    {
        left.poll();
        right.poll();
    }
    focusActive();
}

void Commander::showViewMenu (Component* target)
{
    PopupMenu m;
    m.addSectionHeader (tr ("Zobrazenie súborov"));
    m.addItem (1, tr ("Zobrazovať skryté položky"), true, showHidden);
    m.addItem (2, tr ("Zobrazovať systémové súbory"), true, showSystem);
    PopupMenu look, fonts, colours, cols;
    const float sizes[] = { 12.0f, 13.5f, 15.0f, 17.0f };
    const char* sizeNames[] = { "Malé", "Normálne", "Veľké", "Veľmi veľké" };
    for (int i = 0; i < 4; ++i) fonts.addItem (30 + i, tr (sizeNames[i]), true, std::abs (fontSize - sizes[i]) < 0.1f);
    const uint32 cl[] = { 0xffe5605a, 0xffc9a45c, 0xff7fbf7a, 0xffb48fd8 };
    const char* clNames[] = { "Červená", "Zlatá", "Zelená", "Fialová" };
    for (int i = 0; i < 4; ++i) colours.addItem (40 + i, tr (clNames[i]), true, markColour.getARGB() == cl[i]);
    cols.addItem (50, tr ("Prípona"), true, showExtCol);
    cols.addItem (51, tr ("Veľkosť"), true, showSizeCol);
    cols.addItem (52, tr ("Dátum"), true, showDateCol);
    look.addSubMenu (tr ("Písmo"), fonts);
    look.addSubMenu (tr ("Farba označených súborov"), colours);
    look.addSubMenu (tr ("Stĺpce"), cols);
    look.addItem (53, tr ("Rýchle hľadanie písaním (bez Ctrl+Alt)"), true, quickLetters);
    // farba a jazyk – spolocne s Playerom
    look.addSectionHeader (tr ("Vzhľad"));
    look.addItem (60, tr ("Tmavá téma"), true, ! col::light);
    look.addItem (61, tr ("Svetlá téma"), true, col::light);
    look.addSectionHeader (tr ("Jazyk / Language"));
    look.addItem (62, "Slovensky", true, ! english);
    look.addItem (63, "English", true, english);
    m.addSubMenu (tr ("Vzhľad a správanie"), look);
    m.addSectionHeader (tr ("Nástroje"));
    m.addItem (10, tr ("Hľadať súbory") + "\tAlt+F7");
    m.addItem (11, tr ("Synchronizovať priečinky"));
    m.addItem (12, tr ("Porovnať podľa obsahu"));
    m.addItem (13, tr ("Hromadné premenovanie") + "\tCtrl+M");
    m.addItem (14, tr ("Obľúbené priečinky") + "\tCtrl+D");
    m.addItem (15, tr ("Rýchly náhľad") + "\tCtrl+Q", true, isQuick());
    m.addItem (16, tr ("Nová karta") + "\tCtrl+T");
    m.addItem (23, tr ("Čo zaberá miesto"), ! act->isVirtual());
    m.addItem (18, tr ("Porovnať priečinky") + "\tShift+F2");
    m.addItem (19, tr ("Vrátiť premenovanie / presun") + "\tCtrl+Z", ! undoStack.empty());
    m.addItem (20, tr ("Náhľady obrázkov") + "\tCtrl+Shift+F1", true, act->isThumbMode());
    {
        const bool canAuto = (left.isRemote() && ! right.isVirtual()) || (right.isRemote() && ! left.isVirtual());
        m.addItem (21, tr ("Automaticky nahrávať zmeny na server"), canAuto || autoUp != nullptr, autoUp != nullptr);
    }
    PopupMenu pairs, delPairs;
    if (auto x = settings.getXmlValue ("kcPairs"))
    {
        int i = 0;
        for (auto* e : x->getChildWithTagNameIterator ("pair"))
        {
            pairs.addItem (200 + i, e->getStringAttribute ("name"));
            delPairs.addItem (300 + i, e->getStringAttribute ("name"));
            ++i;
        }
        if (i > 0) pairs.addSeparator();
    }
    pairs.addItem (199, tr ("Uložiť aktuálne panely ako dvojicu…"));
    pairs.addSubMenu (tr ("Odstrániť"), delPairs, delPairs.getNumItems() > 0);
    m.addSubMenu (tr ("Dvojice priečinkov (synchronizácia)"), pairs);
    Component::SafePointer<Commander> sp (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (target), [sp] (int r)
    {
        if (sp == nullptr || r == 0) return;
        if (r == 199) { sp->savePair(); return; }
        if (r >= 200 && r < 300) { sp->openPair (r - 200); return; }
        if (r >= 300 && r < 400)
        {
            if (auto x = sp->settings.getXmlValue ("kcPairs"))
                if (auto* e = x->getChildElement (r - 300)) { x->removeChildElement (e, true); sp->settings.setValue ("kcPairs", x.get()); }
            return;
        }
        if (r >= 30)
        {
            const float sizes[] = { 12.0f, 13.5f, 15.0f, 17.0f };
            const uint32 cl[] = { 0xffe5605a, 0xffc9a45c, 0xff7fbf7a, 0xffb48fd8 };
            if (r >= 30 && r < 34) sp->fontSize = sizes[r - 30];
            if (r >= 40 && r < 44) sp->markColour = Colour (cl[r - 40]);
            if (r == 50) sp->showExtCol = ! sp->showExtCol;
            if (r == 51) sp->showSizeCol = ! sp->showSizeCol;
            if (r == 52) sp->showDateCol = ! sp->showDateCol;
            if (r == 53) sp->quickLetters = ! sp->quickLetters;
            if (r >= 60 && r <= 63 && sp->onAppearance) { sp->onAppearance (r - 59); return; }
            sp->settings.setValue ("kcFont", (double) sp->fontSize);
            sp->settings.setValue ("kcMarkCol", sp->markColour.toString());
            sp->settings.setValue ("kcColExt", sp->showExtCol);
            sp->settings.setValue ("kcColSize", sp->showSizeCol);
            sp->settings.setValue ("kcColDate", sp->showDateCol);
            sp->settings.setValue ("kcQuickLetters", sp->quickLetters);
            sp->applyLook();
            sp->focusActive();
            return;
        }
        if (r >= 10)
        {
            switch (r)
            {
                case 10: sp->searchFiles(); break;
                case 11: sp->syncDirs(); break;
                case 12: sp->compareFiles(); break;
                case 13: sp->multiRename(); break;
                case 14: sp->favMenu(); break;
                case 15: sp->toggleQuick(); break;
                case 16: sp->act->newTab(); break;
                case 23: sp->spaceMap(); break;
                case 18: { KeyPress kp (KeyPress::F2Key, ModifierKeys::shiftModifier, 0); sp->handleKey (kp); break; }
                case 19: sp->undo(); break;
                case 20: sp->act->setThumbMode (! sp->act->isThumbMode()); break;
                case 21: sp->toggleAutoUpload(); break;
                default: break;
            }
            return;
        }
        if (r == 1) sp->showHidden = ! sp->showHidden;
        if (r == 2) sp->showSystem = ! sp->showSystem;
        sp->settings.setValue ("kcHidden", sp->showHidden);
        sp->settings.setValue ("kcSystem", sp->showSystem);
        sp->left.reload();
        sp->right.reload();
        sp->focusActive();
    });
}

void Commander::saveState()
{
    if (! started) return;
    settings.setValue ("kcLeft", left.getFolder().getFullPathName());
    settings.setValue ("kcRight", right.getFolder().getFullPathName());
    settings.setValue ("kcActive", act == &right ? 1 : 0);
    settings.setValue ("kcColW", String (colW[0]) + "," + String (colW[1]) + "," + String (colW[2]));
    settings.setValue ("kcTabsL", left.getTabs().joinIntoString ("\n"));
    settings.setValue ("kcTabL", left.getCurTab());
    settings.setValue ("kcTabsR", right.getTabs().joinIntoString ("\n"));
    settings.setValue ("kcTabR", right.getCurTab());
}

void Commander::visibilityChanged()
{
    if (isVisible()) startTimer (250);
    else if (transfer == nullptr && ftpJob == nullptr && task == nullptr) stopTimer();
}

void Commander::focusActive()
{
    Component::SafePointer<Commander> sp (this);
    Timer::callAfterDelay (60, [sp] { if (sp != nullptr && sp->isShowing() && sp->dialogWin == nullptr) sp->act->grabKeyboardFocus(); });
}

void Commander::setActive (CmdPanel& p)
{
    if (act == &p) return;
    act->active = false;
    act->repaint();
    act = &p;
    p.active = true;
    p.repaint();
    resized();
    repaint (0, getHeight() - fbarH - msgH - cmdH, getWidth(), cmdH);
    updateNetDetail();
}

void Commander::applyLook()
{
    for (auto* p : { &left, &right })
    {
        p->rowH = roundToInt (fontSize + 7.0f);
        p->resized();
        p->repaint();
    }
}

void Commander::typeToCommandLine (const String& text)
{
    cmdLine.grabKeyboardFocus();
    cmdLine.moveCaretToEnd();
    cmdLine.insertTextAtCaret (text);
}

void Commander::runCommand (const String& text)
{
    const String t = text.trim();
    cmdLine.clear();
    act->grabKeyboardFocus();
    if (t.isEmpty()) return;
    if (act->isVirtual()) { message (tr ("Príkazy fungujú v priečinku na disku."), true); return; }
    const File dir = act->getFolder();
    // cd, cd.., cd\ ako v Total Commanderi
    if (t.equalsIgnoreCase ("cd") || t.startsWithIgnoreCase ("cd ") || t.startsWithIgnoreCase ("cd..") || t.startsWithIgnoreCase ("cd\\"))
    {
        String a = t.substring (2).trim().unquoted();
        File d = a.isEmpty() ? File::getSpecialLocation (File::userHomeDirectory)
               : a == "\\" ? Transfer::rootOf (dir)
               : File::isAbsolutePath (a) ? File (a) : dir.getChildFile (a);
        if (d.isDirectory()) act->setFolder (d); else message (tr ("Priečinok neexistuje: ") + a, true);
        return;
    }
    winshell::run (t, dir);
    message (tr ("Spustené: ") + t);
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
    {
        // príkazový riadok: cesta aktívneho panela >
        auto c = r.removeFromBottom (cmdH);
        g.setColour (col::panel);
        g.fillRect (c);
        g.setColour (col::text);
        g.setFont (FontOptions (13.0f));
        g.drawText (act->displayPath() + ">", c.withRight (cmdLine.getX() - 4).withTrimmedLeft (6), Justification::centredRight, true);
    }

    // riadok so správou / priebehom prenosu
    g.setColour (col::panel);
    g.fillRect (ml);
    if (transfer != nullptr || ftpJob != nullptr || task != nullptr)
    {
        const double pr = transfer != nullptr ? transfer->progress() : ftpJob != nullptr ? ftpJob->progress() : task->progress.load();
        g.setColour (col::accent);
        g.fillRect (ml.withWidth (roundToInt (ml.getWidth() * jlimit (0.0, 1.0, pr))));
    }
    g.setFont (FontOptions (13.0f));
    if (autoUp != nullptr)
    {
        const String a = String::fromUTF8 ("\xe2\x86\x91 ") + tr ("Automatické nahrávanie: ") + autoUp->local.getFileName();
        const int w = GlyphArrangement::getStringWidthInt (FontOptions (13.0f), a) + 16;
        g.setColour (col::gold);
        g.drawText (a, ml.removeFromRight (w).reduced (8, 0), Justification::centredRight, true);
    }
    g.setColour (msgError ? Colour (0xffe08a8a) : col::text);
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
    {
        auto c = r.removeFromBottom (cmdH).reduced (2, 2);
        const int prompt = jmin (c.getWidth() / 2, GlyphArrangement::getStringWidthInt (FontOptions (13.0f), act->displayPath() + ">") + 14);
        cmdLine.setBounds (c.withTrimmedLeft (prompt));
    }
    left.setBounds (r.removeFromLeft (r.getWidth() / 2).withTrimmedRight (2));
    right.setBounds (r.withTrimmedLeft (2));
    if (quickComp != nullptr) quickComp->setBounds (otherPanel().getBounds());
    if (netDetail != nullptr && netDetail->isVisible()) netDetail->setBounds (otherPanel().getBounds());
    if (listWin != nullptr)
    {
        const int fw = listWin->getProperties().getWithDefault ("kcW", 900), fh = listWin->getProperties().getWithDefault ("kcH", 560);
        listWin->setBounds (Rectangle<int> (jmin (fw, getWidth() - 40), jmin (fh, getHeight() - 40)).withCentre (getLocalBounds().getCentre()));
    }
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
    if (k == KeyPress::escapeKey && ftpJob != nullptr) { ftpJob->cancel(); return true; }
    if (k == KeyPress::escapeKey && task != nullptr) { task->cancel(); return true; }
    if (mods.isAltDown() && k == KeyPress::F5Key) { packZip(); return true; }
    if (mods.isAltDown() && k == KeyPress::F9Key) { unpack(); return true; }
    if (mods.isCtrlDown() && (k == 'f' || k == 'F'))
    {
        if (mods.isShiftDown()) act->disconnectFtp();
        else showFtp();
        return true;
    }
    if (k == KeyPress::tabKey && mods.isCtrlDown()) { act->nextTab (mods.isShiftDown() ? -1 : 1); return true; }
    if (k == KeyPress::tabKey) { if (isQuick() || (netDetail != nullptr && netDetail->isVisible())) return true; auto& o = otherPanel(); setActive (o); o.grabKeyboardFocus(); return true; }
    if (mods.isCtrlDown() && ! mods.isShiftDown() && (k == 't' || k == 'T')) { act->newTab(); return true; }
    if (mods.isCtrlDown() && (k == 'w' || k == 'W')) { act->closeTab(); return true; }
    if (mods.isCtrlDown() && (k == 'q' || k == 'Q')) { toggleQuick(); return true; }
    if (mods.isCtrlDown() && (k == 'z' || k == 'Z')) { undo(); return true; }
    if (mods.isShiftDown() && ! mods.isCtrlDown() && k == KeyPress::F2Key)
    {
        const int a = left.markDiff (right), b = right.markDiff (left);
        message (tr ("Rozdiely: vľavo ") + String (a) + tr (", vpravo ") + String (b) + (a + b == 0 ? tr (" – priečinky sú rovnaké") : String()));
        return true;
    }
    if (mods.isCtrlDown() && ! mods.isAltDown() && (k == KeyPress::leftKey || k == KeyPress::rightKey))
    {
        // Ctrl+←/→: priečinok pod kurzorom (alebo aktuálny) otvoriť v ľavom / pravom paneli
        CmdPanel& target = k == KeyPress::leftKey ? left : right;
        const auto* e = act->currentEntry();
        if (act->isRemote()) target.setRemote (act->session(), e != nullptr && e->dir ? e->rpath : act->remoteDir());
        else if (! act->isVirtual()) target.setFolder (e != nullptr && e->dir ? e->file : act->getFolder());
        return true;
    }
    if (mods.isCtrlDown() && (k == 'd' || k == 'D')) { favMenu(); return true; }
    if (mods.isCtrlDown() && (k == 'm' || k == 'M')) { multiRename(); return true; }
    if (mods.isAltDown() && k == KeyPress::F7Key) { searchFiles(); return true; }
    if (mods.isAltDown() && k == KeyPress::F1Key) { left.showDriveMenu(); return true; }
    if (mods.isAltDown() && k == KeyPress::F2Key) { right.showDriveMenu(); return true; }
    if (mods.isCtrlDown() && (k == 'u' || k == 'U'))
    {
        const File a = left.getFolder(), b = right.getFolder();
        left.setFolder (b);
        right.setFolder (a);
        return true;
    }
    if (mods.isCtrlDown() && (k == 'r' || k == 'R'))
    {
        if (act->isNet()) { auto& e = netEngine(); if (! e.isScanning()) e.start (e.range()); act->fillNet(); }
        else act->reload();
        return true;
    }
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
    // zvýraznené tlačidlo = Enter; šípky ←/→ a Tab prepínajú
    for (int i = 0; i < dialogWin->getNumButtons(); ++i)
        if (auto* b = dialogWin->getButton (i))
        {
            b->setWantsKeyboardFocus (true);
            b->addKeyListener (&dialogKeys);
        }
    addAndMakeVisible (*dialogWin);
    dialogWin->setSize (jmin (jmax (560, dialogWin->getWidth()), getWidth() - 40), dialogWin->getHeight());
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
    highlightButton (0, edit == nullptr);
}

void Commander::highlightButton (int index, bool focus)
{
    if (dialogWin == nullptr) return;
    const int n = dialogWin->getNumButtons();
    if (n == 0) return;
    selButton = (index % n + n) % n;
    for (int i = 0; i < n; ++i)
        if (auto* b = dialogWin->getButton (i))
        {
            const bool sel = i == selButton;
            b->setColour (TextButton::buttonColourId, sel ? col::accentHi : col::panelHi);
            b->setColour (TextButton::textColourOffId, sel ? col::selText : col::text);
            b->repaint();
        }
    if (focus)
        if (auto* b = dialogWin->getButton (selButton)) b->grabKeyboardFocus();
}

bool Commander::DialogKeys::keyPressed (const KeyPress& key, Component*)
{
    const int k = key.getKeyCode();
    if (k == KeyPress::rightKey || (k == KeyPress::tabKey && ! key.getModifiers().isShiftDown())) { owner.highlightButton (owner.selButton + 1, true); return true; }
    if (k == KeyPress::leftKey || (k == KeyPress::tabKey && key.getModifiers().isShiftDown())) { owner.highlightButton (owner.selButton - 1, true); return true; }
    return false;
}

void Commander::openFile (const File& f)
{
    if (! f.startAsProcess()) message (tr ("Súbor sa nedá otvoriť: ") + f.getFileName(), true);
}

void Commander::viewCurrent()
{
    if (act->isRemote())
    {
        if (auto* e = act->currentEntry()) if (! e->dir) fetchRemote (*e, act->session(), true);
        return;
    }
    if (act->isArchive())
    {
        if (auto* e = act->currentEntry()) if (! e->dir) fetchArchive (*e, act->archiveFile(), true);
        return;
    }
    const auto f = act->currentFile();
    if (f.existsAsFile() && onView) onView (f);
}

void Commander::editCurrent()
{
    if (act->isArchive()) { message (tr ("Súbor z archívu najprv skopíruj k sebe (F5), potom ho uprav.")); return; }
    if (act->isRemote())
    {
        auto* e = act->currentEntry();
        if (e == nullptr || e->dir || busy()) return;
        const auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("Korgus FTP").getChildFile ("upravy").getNonexistentChildFile ("s", {}, false);
        dir.createDirectory();
        const auto local = dir.getChildFile (e->name);
        const String remote = e->rpath;
        auto s = act->session();
        Component::SafePointer<Commander> sp (this);
        startFtpJob (s, FtpJob::Kind::Download, { { local, remote, false, e->size } }, false, [sp, local, remote, s] (FtpJob& j)
        {
            if (sp == nullptr) return;
            if (j.error.isNotEmpty() || ! local.existsAsFile()) { sp->message (j.error.isNotEmpty() ? j.error : tr ("Zrušené"), true); return; }
            sp->watches.push_back ({ local, remote, s, local.getLastModificationTime() });
           #if JUCE_WINDOWS
            const auto notepad = File::getSpecialLocation (File::windowsSystemDirectory).getChildFile ("notepad.exe");
            if (! (notepad.existsAsFile() && notepad.startAsProcess (local.getFullPathName().quoted())))
           #endif
                local.startAsProcess();
            sp->message (tr ("Upravuješ súbor zo servera – po uložení sa ponúkne nahratie späť."));
        });
        return;
    }
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
    if (act->isArchive() || otherPanel().isArchive()) { archiveCopy (move); return; }
    if (act->isRemote() || otherPanel().isRemote()) { remoteCopyMove (move); return; }
    const auto files = act->selection();
    if (files.isEmpty()) return;
    const File srcDir = act->getFolder(), dstDir = otherPanel().getFolder();
    const String what = files.size() == 1 ? files[0].getFileName().quoted() : items (files.size());
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
            std::vector<std::pair<File, File>> moved;
            for (auto& it : items)
                if (it.from.moveFileTo (it.to)) { ++ok; moved.push_back ({ it.from, it.to }); } else failed = it.from.getFileName();
            sp->pushUndo (moved);
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
            (conflicts.size() == 1 ? tr ("V cieli už existuje ") + conflicts[0].getFileName()
                                    : tr ("V cieli už existuje ") + items (conflicts.size()) + tr (", napríklad ") + conflicts[0].getFileName()),
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

// ---------------------------------------------------------------- okno priebehu kopirovania (ako v TC)
namespace
{
    struct CopyBox : Component
    {
        Transfer& t; std::function<void()> onBackground;
        TextButton bgB { tr ("Na pozadí") }, pauseB { tr ("Pauza") }, cancelB { tr ("Zrušiť") };
        String from, to, info; double totalP = 0, fileP = 0;
        CopyBox (Transfer& tr_) : t (tr_)
        {
            for (auto* b : { &bgB, &pauseB, &cancelB }) { b->setWantsKeyboardFocus (false); addAndMakeVisible (*b); }
            bgB.onClick = [this] { if (onBackground) onBackground(); };
            pauseB.onClick = [this] { t.setPaused (! t.isPaused()); pauseB.setButtonText (t.isPaused() ? tr ("Pokračovať") : tr ("Pauza")); };
            cancelB.onClick = [this] { t.setPaused (false); t.cancel(); };
        }
        void paint (Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (col::panel); g.fillRoundedRectangle (r, 4.0f);
            g.setColour (col::line); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
            auto a = getLocalBounds().reduced (16, 12);
            g.setColour (col::text); g.setFont (FontOptions (14.0f));
            g.drawText (t.isMove() ? tr ("Presúvanie") : tr ("Kopírovanie"), a.removeFromTop (22), Justification::centredLeft);
            g.setFont (FontOptions (12.5f)); g.setColour (col::textDim);
            g.drawText (tr ("Z: ") + from, a.removeFromTop (18), Justification::centredLeft, true);
            g.drawText (tr ("Do: ") + to, a.removeFromTop (18), Justification::centredLeft, true);
            a.removeFromTop (6);
            auto bar = [&g] (Rectangle<int> b, double p)
            {
                g.setColour (col::bg); g.fillRoundedRectangle (b.toFloat(), 3.0f);
                g.setColour (col::accentHi); g.fillRoundedRectangle (b.withWidth (roundToInt (b.getWidth() * jlimit (0.0, 1.0, p))).toFloat(), 3.0f);
                g.setColour (col::text); g.setFont (FontOptions (12.0f));
                g.drawText (String (roundToInt (p * 100.0)) + " %", b, Justification::centred);
            };
            g.setColour (col::textDim); g.setFont (FontOptions (12.0f));
            g.drawText (tr ("Súbor"), a.removeFromTop (16), Justification::centredLeft);
            bar (a.removeFromTop (16), fileP); a.removeFromTop (8);
            g.setColour (col::textDim); g.setFont (FontOptions (12.0f));
            g.drawText (tr ("Celkom"), a.removeFromTop (16), Justification::centredLeft);
            bar (a.removeFromTop (16), totalP); a.removeFromTop (6);
            g.setColour (col::textDim); g.setFont (FontOptions (12.5f));
            g.drawText (info, a.removeFromTop (18), Justification::centredLeft, true);
        }
        void resized() override
        {
            auto b = getLocalBounds().reduced (16, 12).removeFromBottom (28);
            cancelB.setBounds (b.removeFromRight (96)); b.removeFromRight (8);
            pauseB.setBounds (b.removeFromRight (96)); b.removeFromRight (8);
            bgB.setBounds (b.removeFromRight (96));
        }
    };

    String copySizeText (int64 b) { return b >= (1 << 30) ? String ((double) b / (1 << 30), 2) + " GB" : String ((double) b / (1 << 20), 1) + " MB"; }
}

void Commander::updateCopyBox()
{
    if (transfer == nullptr || copyInBackground) { copyBox = nullptr; return; }
    const double el = Time::getMillisecondCounterHiRes() * 0.001 - transferStart;
    if (copyBox == nullptr)
    {
        if (el < 0.4) return;   // kratke kopirovanie bez okna
        auto* cb = new CopyBox (*transfer);
        Component::SafePointer<Commander> sp (this);
        cb->onBackground = [sp] { if (sp == nullptr) return; sp->copyInBackground = true; MessageManager::callAsync ([sp] { if (sp != nullptr) sp->copyBox = nullptr; }); };
        copyBox.reset (cb);
        addAndMakeVisible (*cb);
        cb->setBounds (getLocalBounds().withSizeKeepingCentre (jmin (560, getWidth() - 40), 236));
    }
    auto* cb = static_cast<CopyBox*> (copyBox.get());
    String f, t; transfer->currentNames (f, t);
    cb->from = f; cb->to = t; cb->fileP = transfer->fileProgress(); cb->totalP = transfer->progress();
    const double speed = el > 0.5 ? (double) transfer->bytesDone() / el : 0.0;
    String info = copySizeText (transfer->bytesDone()) + " / " + copySizeText (transfer->bytesTotal()) + "   ·   " + String (transfer->currentIndex() + 1) + " / " + String (transfer->count());
    if (speed > 0) info << "   ·   " << String (speed / (1 << 20), 1) << " MB/s";
    if (speed > 0 && ! transfer->isPaused())
    {
        const int left = roundToInt ((double) (transfer->bytesTotal() - transfer->bytesDone()) / speed);
        info << tr ("   ·   zostáva ") << (left >= 60 ? String (left / 60) + " min " : String()) << String (left % 60) << " s";
    }
    if (transfer->isPaused()) info << tr ("   ·   pozastavené");
    cb->info = info;
    cb->repaint();
}

void Commander::startTransfer (std::vector<Transfer::Item> items, bool move, std::function<void (Transfer&)> done)
{
    if (transfer != nullptr) { message (tr ("Počkaj, kým sa dokončí prebiehajúci prenos (Esc ho zruší)."), true); return; }
    transfer = std::make_unique<Transfer> (std::move (items), move);
    transferDone = std::move (done);
    transferStart = Time::getMillisecondCounterHiRes() * 0.001; copyInBackground = false;
    transfer->start();
    startTimer (200);
}

void Commander::timerCallback()
{
    if (transfer != nullptr)
    {
        if (transfer->isDone())
        {
            copyBox = nullptr;
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
            updateCopyBox();
        }
    }
    if (task != nullptr)
    {
        if (task->isDone())
        {
            auto j = std::move (task);
            auto d = std::move (taskDone);
            taskDone = nullptr;
            if (d) d (*j);
            if (! isVisible()) stopTimer();
        }
        else message (task->label() + " " + String (roundToInt (task->progress.load() * 100.0)) + " %" + tr (" · Esc = zrušiť"));
    }
    if (ftpJob != nullptr)
    {
        if (ftpJob->isDone())
        {
            auto j = std::move (ftpJob);
            auto d = std::move (ftpDone);
            ftpDone = nullptr;
            if (d) d (*j);
            if (! isVisible()) stopTimer();
        }
        else
        {
            const auto k = ftpJob->kind();
            message ((k == FtpJob::Kind::Upload ? tr ("Nahrávam na server ") : k == FtpJob::Kind::Download ? tr ("Sťahujem ") : tr ("Mažem na serveri "))
                     + String (ftpJob->currentIndex() + 1) + " / " + String (jmax (1, ftpJob->count()))
                     + " · " + String (roundToInt (ftpJob->progress() * 100.0)) + " %" + tr (" · Esc = zrušiť"));
        }
    }
    if (tick % 8 == 4 && ! busy()) checkWatches();
    if (tick % 12 == 6 && autoUp != nullptr && ! busy()) autoUploadScan();
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
        if (p->isArchive()) { sp->message (tr ("V archíve sa priečinok vytvoriť nedá.")); return; }
        if (p->isRemote())
        {
            if (name.containsAnyOf ("\\")) { sp->message (tr ("Názov nesmie obsahovať \\"), true); return; }
            if (! p->session()->makeDir (FtpSession::join (p->remoteDir(), name))) { sp->message (tr ("Na serveri sa priečinok nepodarilo vytvoriť."), true); return; }
            p->setRemote (p->session(), p->remoteDir(), name.upToFirstOccurrenceOf ("/", false, false));
            return;
        }
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
    if (act->isArchive())
    {
        const auto es = act->selectedEntries();
        if (es.empty()) return;
        CmdPanel* ap = act;
        const File zf = act->archiveFile();
        if (! archive::isZip (zf)) { message (tr ("Z tohto archívu sa mazať nedá (len zo ZIP)."), true); return; }
        Component::SafePointer<Commander> sp (this);
        dialog (tr ("Odstrániť z archívu"), tr ("Odstrániť z archívu ") + (es.size() == 1 ? es[0].name.quoted() : items ((int) es.size())) + "?",
                nullptr, { tr ("Odstrániť"), tr ("Zrušiť") }, [sp, es, ap, zf] (int r, String)
        {
            if (sp == nullptr || r != 1) return;
            StringArray paths;
            for (auto& e : es) paths.add (e.rpath);
            String err;
            if (! archive::deleteFromZip (zf, paths, err)) sp->message (err, true);
            ap->clearMarks();
            ap->reload();
        });
        return;
    }
    if (act->isRemote())
    {
        const auto es = act->selectedEntries();
        if (es.empty()) return;
        CmdPanel* rp = act;
        String t = tr ("Natrvalo zmazať zo servera ") + (es.size() == 1 ? es[0].name.quoted() : items ((int) es.size())) + "?";
        Component::SafePointer<Commander> sp (this);
        dialog (tr ("Odstrániť zo servera"), t, nullptr, { tr ("Odstrániť"), tr ("Zrušiť") }, [sp, es, rp] (int r, String)
        {
            if (sp == nullptr || r != 1) return;
            std::vector<FtpJob::Item> its;
            for (auto& e : es) its.push_back ({ File(), e.rpath, e.dir, 0 });
            sp->startFtpJob (rp->session(), FtpJob::Kind::Delete, std::move (its), false, [sp, rp] (FtpJob& j)
            {
                if (sp == nullptr) return;
                rp->clearMarks();
                rp->reload();
                if (j.error.isNotEmpty()) sp->message (j.error, true);
                else sp->message (tr ("Zmazané zo servera: ") + String (j.completedCount()));
            });
        });
        return;
    }
    const auto files = act->selection();
    if (files.isEmpty()) return;
    CmdPanel* p = act;
    String text = files.size() == 1 ? tr ("Presunúť do Koša ") + files[0].getFileName().quoted() + "?"
                                    : tr ("Presunúť do Koša ") + items (files.size()) + "?";
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
        else sp->message ((ok == 1 ? files[0].getFileName() : items (ok)) + tr (" → Kôš"));
    });
}

void Commander::startFtpJob (std::shared_ptr<FtpSession> s, FtpJob::Kind k, std::vector<FtpJob::Item> its, bool move, std::function<void (FtpJob&)> done)
{
    if (transfer != nullptr || ftpJob != nullptr) { message (tr ("Počkaj, kým sa dokončí prebiehajúci prenos (Esc ho zruší)."), true); return; }
    if (s == nullptr || its.empty()) return;
    ftpJob = std::make_unique<FtpJob> (std::move (s), k, std::move (its), move);
    ftpDone = std::move (done);
    ftpJob->start();
    startTimer (250);
}

void Commander::fetchRemote (const CmdPanel::Entry& e, std::shared_ptr<FtpSession> s, bool view)
{
    if (busy()) return;
    const auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("Korgus FTP");
    dir.createDirectory();
    const auto local = dir.getChildFile (e.name);
    Component::SafePointer<Commander> sp (this);
    startFtpJob (std::move (s), FtpJob::Kind::Download, { { local, e.rpath, false, e.size } }, false, [sp, local, view] (FtpJob& j)
    {
        if (sp == nullptr) return;
        if (j.error.isNotEmpty() || j.wasCancelled() || ! local.existsAsFile()) { sp->message (j.error.isNotEmpty() ? j.error : tr ("Zrušené"), j.error.isNotEmpty()); return; }
        sp->message ({});
        if (view && sp->onView) sp->onView (local);
        else sp->openFile (local);
    });
}

void Commander::remoteCopyMove (bool move)
{
    CmdPanel& src = *act;
    CmdPanel& dst = otherPanel();
    if (src.isRemote() && dst.isRemote()) { message (tr ("Medzi dvoma servermi sa kopírovať nedá – najprv k sebe."), true); return; }
    Component::SafePointer<Commander> sp (this);
    CmdPanel* srcP = &src;
    CmdPanel* dstP = &dst;

    auto finished = [sp, srcP, dstP, move] (FtpJob& j)
    {
        if (sp == nullptr) return;
        srcP->clearMarks();
        srcP->reload();
        dstP->reload();
        String m = j.wasCancelled() ? tr ("Zrušené · hotové: ")
                 : (j.kind() == FtpJob::Kind::Upload ? (move ? tr ("Presunuté na server: ") : tr ("Nahrané na server: "))
                                                     : (move ? tr ("Presunuté zo servera: ") : tr ("Stiahnuté: ")));
        m << String (j.completedCount());
        if (j.error.isNotEmpty()) m << String::fromUTF8 (" \xc2\xb7 ") << j.error;
        sp->message (m, j.error.isNotEmpty());
    };

    if (dst.isRemote())
    {
        const auto files = src.selection();
        if (files.isEmpty()) return;
        const auto s = dst.session();
        const String rdir = dst.remoteDir();
        const String what = files.size() == 1 ? files[0].getFileName().quoted() : items (files.size());
        dialog (move ? tr ("Presunúť na server") : tr ("Nahrať na server"),
                (move ? tr ("Presunúť ") : tr ("Kopírovať ")) + what + tr (" do priečinka na serveri:"), &rdir,
                { move ? tr ("Presunúť") : tr ("Kopírovať"), tr ("Zrušiť") },
                [sp, files, s, rdir, dstP, move, finished] (int r, String t)
        {
            if (sp == nullptr || r != 1) return;
            t = t.trim();
            const String target = t.startsWith ("/") ? t : rdir;
            StringArray conflicts;
            if (target == rdir) for (auto& f : files) if (dstP->hasName (f.getFileName())) conflicts.add (f.getFileName());
            auto go = [sp, files, s, target, move, finished, conflicts] (bool overwrite)
            {
                if (sp == nullptr) return;
                std::vector<FtpJob::Item> its;
                for (auto& f : files)
                    if (overwrite || ! conflicts.contains (f.getFileName()))
                        its.push_back ({ f, FtpSession::join (target, f.getFileName()), f.isDirectory(), f.getSize() });
                if (its.empty()) { sp->message (tr ("Nič sa nezmenilo – všetko už v cieli je.")); return; }
                sp->startFtpJob (s, FtpJob::Kind::Upload, std::move (its), move, finished);
            };
            if (conflicts.isEmpty()) { go (true); return; }
            MessageManager::callAsync ([sp, go, conflicts]
            {
                if (sp == nullptr) return;
                sp->dialog (tr ("Súbory už existujú"), tr ("Na serveri už existuje ") + (conflicts.size() == 1 ? conflicts[0] : items (conflicts.size())),
                            nullptr, { tr ("Prepísať všetky"), tr ("Preskočiť existujúce"), tr ("Zrušiť") },
                            [go] (int r2, String) { if (r2 == 1) go (true); else if (r2 == 2) go (false); });
            });
        });
        return;
    }

    // zo servera k sebe
    const auto es = src.selectedEntries();
    if (es.empty()) return;
    const auto s = src.session();
    const String def = dst.getFolder().getFullPathName();
    const String what = es.size() == 1 ? es[0].name.quoted() : items ((int) es.size());
    dialog (move ? tr ("Presunúť zo servera") : tr ("Stiahnuť zo servera"),
            (move ? tr ("Presunúť ") : tr ("Kopírovať ")) + what + tr (" do:"), &def,
            { move ? tr ("Presunúť") : tr ("Kopírovať"), tr ("Zrušiť") },
            [sp, es, s, move, finished] (int r, String t)
    {
        if (sp == nullptr || r != 1) return;
        const File target (t.trim().unquoted().trim());
        if (! File::isAbsolutePath (t.trim().unquoted().trim()) || ! target.isDirectory()) { sp->message (tr ("Cieľový priečinok neexistuje: ") + t, true); return; }
        StringArray conflicts;
        for (auto& e : es) if (target.getChildFile (e.name).exists()) conflicts.add (e.name);
        auto go = [sp, es, s, target, move, finished, conflicts] (bool overwrite)
        {
            if (sp == nullptr) return;
            std::vector<FtpJob::Item> its;
            for (auto& e : es)
                if (overwrite || ! conflicts.contains (e.name))
                    its.push_back ({ target.getChildFile (e.name), e.rpath, e.dir, e.size });
            if (its.empty()) { sp->message (tr ("Nič sa nezmenilo – všetko už v cieli je.")); return; }
            sp->startFtpJob (s, FtpJob::Kind::Download, std::move (its), move, finished);
        };
        if (conflicts.isEmpty()) { go (true); return; }
        MessageManager::callAsync ([sp, go, conflicts]
        {
            if (sp == nullptr) return;
            sp->dialog (tr ("Súbory už existujú"), tr ("V cieli už existuje ") + (conflicts.size() == 1 ? conflicts[0] : items (conflicts.size())),
                        nullptr, { tr ("Prepísať všetky"), tr ("Preskočiť existujúce"), tr ("Zrušiť") },
                        [go] (int r2, String) { if (r2 == 1) go (true); else if (r2 == 2) go (false); });
        });
    });
}

void Commander::showFtp()
{
    if (busy()) return;
    ftpMgr = std::make_unique<FtpManager> (settings);
    Component::SafePointer<Commander> sp (this);
    auto close = [sp]
    {
        if (sp == nullptr || sp->ftpMgr == nullptr) return;
        sp->ftpMgr->exitModalState (0);
        auto* m = sp->ftpMgr.release();
        m->setVisible (false);
        MessageManager::callAsync ([m] { delete m; });
        sp->focusActive();
    };
    ftpMgr->onClose = close;
    ftpMgr->onConnect = [sp, close] (const FtpConn& c)
    {
        if (sp == nullptr) return;
        sp->message (tr ("Pripájam sa k ") + c.host + String::fromUTF8 ("\xe2\x80\xa6"));
        close();
        sp->connectFtp (c);
    };
    addAndMakeVisible (*ftpMgr);
    ftpMgr->setBounds (Rectangle<int> (jmin (640, getWidth() - 40), jmin (400, getHeight() - 40)).withCentre (getLocalBounds().getCentre()));
    ftpMgr->enterModalState (true);
}

void Commander::connectFtp (const FtpConn& c) { connectFtpTo (c, act, c.dir); }

void Commander::connectFtpTo (const FtpConn& c, CmdPanel* p, String dir)
{
    Component::SafePointer<Commander> sp (this);
    {
        sp->message (tr ("Pripájam sa k ") + c.host + String::fromUTF8 ("\xe2\x80\xa6"));
        auto s = std::make_shared<FtpSession>();
        Thread::launch ([s, c, sp, p, dir]
        {
            String err;
            const bool ok = s->connect (c, err);
            MessageManager::callAsync ([s, c, sp, p, ok, err, dir]
            {
                if (sp == nullptr) return;
                if (! ok)
                {
                    sp->message (tr ("Pripojenie zlyhalo: ") + err, true);
                    AlertWindow::showMessageBoxAsync (MessageBoxIconType::NoIcon, tr ("FTP – pripojenie zlyhalo"),
                                                      c.host + "\n\n" + err, {}, sp.getComponent());
                    return;
                }
                sp->message (tr ("Pripojené: ") + c.host);
                sp->setActive (*p);
                p->setRemote (s, dir.isNotEmpty() ? dir : s->startDir());
                p->grabKeyboardFocus();
                if (sp->pendingSync && ! sp->left.isArchive() && ! sp->right.isArchive()
                    && (! sp->left.isRemote() || ! sp->right.isRemote()))
                {
                    const bool bothReady = (sp->left.isRemote() || ! sp->left.isVirtual()) && (sp->right.isRemote() || ! sp->right.isVirtual());
                    if (bothReady) { sp->pendingSync = false; sp->syncDirs(); }
                }
            });
        });
    }
}

void Commander::checkWatches()
{
    for (size_t i = 0; i < watches.size(); ++i)
    {
        auto& w = watches[i];
        auto s = w.s.lock();
        if (s == nullptr || ! w.local.existsAsFile()) { watches.erase (watches.begin() + (long) i); return; }
        const auto t = w.local.getLastModificationTime();
        if (t == w.mod) continue;
        w.mod = t;
        const File local = w.local;
        const String remote = w.remote;
        Component::SafePointer<Commander> sp (this);
        dialog (tr ("Súbor sa zmenil"), local.getFileName().quoted() + tr (" je uložený. Nahrať ho späť na server?"), nullptr,
                { tr ("Nahrať"), tr ("Nie") }, [sp, local, remote, s] (int r, String)
        {
            if (sp == nullptr || r != 1) return;
            sp->startFtpJob (s, FtpJob::Kind::Upload, { { local, remote, false, local.getSize() } }, false, [sp] (FtpJob& j)
            {
                if (sp == nullptr) return;
                sp->left.reload();
                sp->right.reload();
                sp->message (j.error.isNotEmpty() ? j.error : tr ("Nahrané späť na server."), j.error.isNotEmpty());
            });
        });
        return;
    }
}

void Commander::startTask (const String& label, std::function<void (TaskJob&)> work, std::function<void (TaskJob&)> done)
{
    if (transfer != nullptr || ftpJob != nullptr || task != nullptr) { message (tr ("Počkaj, kým sa dokončí prebiehajúca úloha (Esc ju zruší)."), true); return; }
    task = std::make_unique<TaskJob> (label, std::move (work));
    taskDone = std::move (done);
    task->start();
    startTimer (250);
}

void Commander::fetchArchive (const CmdPanel::Entry& e, const File& arcFile, bool view)
{
    if (busy()) return;
    const auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("Korgus archiv");
    dir.createDirectory();
    const String path = e.rpath, base = path.contains ("/") ? path.upToLastOccurrenceOf ("/", false, false) : String();
    const auto local = dir.getChildFile (e.name);
    Component::SafePointer<Commander> sp (this);
    startTask (tr ("Rozbaľujem"), [arcFile, path, base, dir] (TaskJob& j)
    {
        archive::extract (arcFile, { path }, base, dir, &j, j.error);
    }, [sp, local, view] (TaskJob& j)
    {
        if (sp == nullptr) return;
        if (j.error.isNotEmpty() || ! local.existsAsFile()) { sp->message (j.error.isNotEmpty() ? j.error : tr ("Zrušené"), true); return; }
        sp->message ({});
        if (view && sp->onView) sp->onView (local); else sp->openFile (local);
    });
}

void Commander::archiveCopy (bool move)
{
    CmdPanel& src = *act;
    CmdPanel& dst = otherPanel();
    CmdPanel* srcP = &src;
    CmdPanel* dstP = &dst;
    Component::SafePointer<Commander> sp (this);
    if (src.isArchive() && dst.isArchive()) { message (tr ("Medzi dvoma archívmi sa kopírovať nedá."), true); return; }
    if (src.isArchive())
    {
        if (dst.isRemote()) { message (tr ("Z archívu najprv rozbaľ k sebe, potom nahraj na server."), true); return; }
        const auto es = src.selectedEntries();
        if (es.empty()) return;
        const File arcFile = src.archiveFile();
        const String base = src.archiveDir();
        if (move && ! archive::isZip (arcFile)) move = false;
        const String def = dst.getFolder().getFullPathName();
        dialog (tr ("Rozbaliť"), tr ("Rozbaliť ") + (es.size() == 1 ? es[0].name.quoted() : items ((int) es.size())) + tr (" do:"), &def,
                { tr ("Rozbaliť"), tr ("Zrušiť") }, [sp, es, arcFile, base, move, srcP, dstP] (int r, String t)
        {
            if (sp == nullptr || r != 1) return;
            const File target (t.trim().unquoted().trim());
            if (! File::isAbsolutePath (t.trim().unquoted().trim()) || ! target.isDirectory()) { sp->message (tr ("Cieľový priečinok neexistuje: ") + t, true); return; }
            StringArray paths;
            for (auto& e : es) paths.add (e.rpath);
            sp->startTask (tr ("Rozbaľujem"), [arcFile, paths, base, target, move] (TaskJob& j)
            {
                if (archive::extract (arcFile, paths, base, target, &j, j.error) && move && ! j.cancelled)
                    archive::deleteFromZip (arcFile, paths, j.error);
            }, [sp, srcP, dstP, paths] (TaskJob& j)
            {
                if (sp == nullptr) return;
                srcP->clearMarks();
                srcP->reload();
                dstP->reload();
                sp->message (j.error.isNotEmpty() ? j.error : tr ("Rozbalené: ") + items (paths.size()), j.error.isNotEmpty());
            });
        });
        return;
    }
    // do archívu (len ZIP)
    const File zf = dst.archiveFile();
    if (! archive::isZip (zf)) { message (tr ("Do tohto archívu sa zapisovať nedá (len do ZIP)."), true); return; }
    if (src.isRemote()) { message (tr ("Zo servera najprv skopíruj k sebe, potom pridaj do archívu."), true); return; }
    const auto files = src.selection();
    if (files.isEmpty()) return;
    const String adirTarget = dst.archiveDir();
    dialog (tr ("Pridať do archívu"), tr ("Pridať ") + (files.size() == 1 ? files[0].getFileName().quoted() : items (files.size())) + tr (" do ") + zf.getFileName() + "?",
            nullptr, { tr ("Pridať"), tr ("Zrušiť") }, [sp, files, zf, adirTarget, move, srcP, dstP] (int r, String)
    {
        if (sp == nullptr || r != 1) return;
        sp->startTask (tr ("Balím"), [files, zf, adirTarget, move] (TaskJob& j)
        {
            if (archive::addToZip (zf, adirTarget, files, &j, j.error) && move)
                for (auto& f : files) f.moveToTrash();
        }, [sp, srcP, dstP, files] (TaskJob& j)
        {
            if (sp == nullptr) return;
            srcP->clearMarks();
            srcP->reload();
            dstP->reload();
            sp->message (j.error.isNotEmpty() ? j.error : tr ("Pridané do archívu: ") + items (files.size()), j.error.isNotEmpty());
        });
    });
}

void Commander::packZip()
{
    if (busy()) return;
    if (act->isVirtual()) { message (tr ("Zbaliť sa dajú súbory z disku."), true); return; }
    const auto files = act->selection();
    if (files.isEmpty()) return;
    const File srcDir = act->getFolder();
    const File dstDir = otherPanel().isVirtual() ? srcDir : otherPanel().getFolder();
    const String base = files.size() == 1 ? files[0].getFileNameWithoutExtension() : (srcDir.getFileName().isNotEmpty() ? srcDir.getFileName() : String ("archiv"));
    const String def = dstDir.getChildFile (base + ".zip").getFullPathName();
    CmdPanel* p = act;
    Component::SafePointer<Commander> sp (this);
    dialog (tr ("Zbaliť do ZIP"), tr ("Zbaliť ") + (files.size() == 1 ? files[0].getFileName().quoted() : items (files.size())) + tr (" do:"), &def,
            { tr ("Zbaliť"), tr ("Zrušiť") }, [sp, files, srcDir, p] (int r, String t)
    {
        if (sp == nullptr || r != 1) return;
        t = t.trim().unquoted().trim();
        if (! File::isAbsolutePath (t)) t = srcDir.getChildFile (t).getFullPathName();
        File zip (t);
        if (! zip.hasFileExtension ("zip")) zip = zip.withFileExtension ("zip");
        if (! zip.getParentDirectory().isDirectory()) { sp->message (tr ("Cieľový priečinok neexistuje."), true); return; }
        sp->startTask (tr ("Balím"), [files, srcDir, zip] (TaskJob& j) { archive::createZip (files, srcDir, zip, &j, j.error); },
                       [sp, zip, p] (TaskJob& j)
        {
            if (sp == nullptr) return;
            p->clearMarks();
            sp->left.reload();
            sp->right.reload();
            sp->message (j.error.isNotEmpty() ? j.error : tr ("Zbalené: ") + zip.getFileName(), j.error.isNotEmpty());
        });
    });
}

void Commander::unpack()
{
    if (busy()) return;
    if (act->isVirtual()) { message (tr ("Označ archívy na disku (ZIP, RAR, 7z)."), true); return; }
    Array<File> arcs;
    for (auto& f : act->selection()) if (archive::isArchive (f)) arcs.add (f);
    if (arcs.isEmpty()) { message (tr ("Označ archívy na rozbalenie (ZIP, RAR, 7z)."), true); return; }
    const String def = (otherPanel().isVirtual() ? act->getFolder() : otherPanel().getFolder()).getFullPathName();
    Component::SafePointer<Commander> sp (this);
    dialog (tr ("Rozbaliť"), tr ("Rozbaliť ") + (arcs.size() == 1 ? arcs[0].getFileName().quoted() : items (arcs.size())) + tr (" do:"), &def,
            { tr ("Rozbaliť"), tr ("Zrušiť") }, [sp, arcs] (int r, String t)
    {
        if (sp == nullptr || r != 1) return;
        const File target (t.trim().unquoted().trim());
        if (! File::isAbsolutePath (t.trim().unquoted().trim())) { sp->message (tr ("Zadaj celú cestu."), true); return; }
        target.createDirectory();
        sp->startTask (tr ("Rozbaľujem"), [arcs, target] (TaskJob& j)
        {
            for (int i = 0; i < arcs.size() && ! j.cancelled; ++i)
            {
                // viac archívov: každý do vlastného priečinka
                const File d = arcs.size() == 1 ? target : target.getChildFile (arcs[i].getFileNameWithoutExtension());
                d.createDirectory();
                if (! archive::extractAll (arcs[i], d, nullptr, j.error)) break;
                j.progress = (double) (i + 1) / (double) arcs.size();
            }
        }, [sp, arcs] (TaskJob& j)
        {
            if (sp == nullptr) return;
            sp->left.reload();
            sp->right.reload();
            sp->message (j.error.isNotEmpty() ? j.error : tr ("Rozbalené: ") + items (arcs.size()), j.error.isNotEmpty());
        });
    });
}

void Commander::dropped (CmdPanel& from, CmdPanel& to, bool move)
{
    if (&from == &to) return;
    setActive (from);
    copyOrMove (move);
}
}
