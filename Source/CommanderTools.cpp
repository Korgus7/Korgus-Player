// Korgus Commander – doplnky: rýchly náhľad, obľúbené, hromadné premenovanie, hľadanie,
// porovnanie podľa obsahu, synchronizácia priečinkov
#include "Commander.h"

namespace kp
{
namespace
{
    String items (int n)
    {
        return String (n) + (n == 1 ? tr (" položka") : (n >= 2 && n <= 4 ? tr (" položky") : tr (" položiek")));
    }

    // okno so zoznamom (výsledky hľadania, porovnanie, synchronizácia)
    class ListWindow : public Component, private ListBoxModel
    {
    public:
        ListWindow (const String& t, const StringArray& l, const String& action, bool mono) : title (t), lines (l), monospace (mono)
        {
            setWantsKeyboardFocus (true);
            list.setModel (this);
            list.setRowHeight (20);
            list.setColour (ListBox::backgroundColourId, col::docBg);
            list.setColour (ListBox::outlineColourId, col::line);
            list.setOutlineThickness (1);
            addAndMakeVisible (list);
            closeB.setButtonText (tr ("Zavrieť"));
            closeB.onClick = [this] { if (onClose) onClose(); };
            addAndMakeVisible (closeB);
            if (action.isNotEmpty())
            {
                actionB.setButtonText (action);
                actionB.onClick = [this] { if (onAction) onAction(); };
                addAndMakeVisible (actionB);
            }
        }
        std::function<void()> onAction, onClose;
        std::function<void (int)> onPick;
        ListBox list;

        void paint (Graphics& g) override
        {
            g.fillAll (col::panel);
            g.setColour (col::line);
            g.drawRect (getLocalBounds(), 1);
            g.setColour (col::text);
            g.setFont (FontOptions (15.0f, Font::bold));
            g.drawText (title, getLocalBounds().removeFromTop (38).reduced (14, 0), Justification::centredLeft, true);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (14).withTrimmedTop (28);
            auto b = r.removeFromBottom (36).withTrimmedTop (6);
            closeB.setBounds (b.removeFromRight (120));
            b.removeFromRight (8);
            if (actionB.isVisible()) actionB.setBounds (b.removeFromRight (160));
            list.setBounds (r);
        }
        bool keyPressed (const KeyPress& k) override
        {
            if (k.isKeyCode (KeyPress::escapeKey)) { if (onClose) onClose(); return true; }
            return false;
        }

    private:
        String title;
        StringArray lines;
        bool monospace;
        TextButton actionB, closeB;
        int getNumRows() override { return lines.size(); }
        void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
        {
            if (selected) { g.setColour (col::accent); g.fillRect (0, 0, w, h); }
            const String& s = lines[row];
            Colour c = col::text;
            if (s.startsWithChar ('+')) c = Colour (0xff7fbf7a);
            else if (s.startsWithChar ('-')) c = Colour (0xffe5605a);
            g.setColour (selected ? col::selText : c);
            g.setFont (monospace ? FontOptions (Font::getDefaultMonospacedFontName(), 13.0f, Font::plain) : FontOptions (13.5f));
            g.drawText (s, 8, 0, w - 12, h, Justification::centredLeft, true);
        }
        void listBoxItemDoubleClicked (int row, const MouseEvent&) override { if (onPick) onPick (row); }
        void returnKeyPressed (int row) override { if (onPick) onPick (row); }
    };

    String fill (String mask, const String& base, const String& ext, const String& counter)
    {
        return mask.replace ("[N]", base).replace ("[E]", ext).replace ("[C]", counter);
    }

    struct Info { bool dir = false; int64 size = 0; Time mod; };
    using Tree = std::map<String, Info>;

    void walkLocal (const File& dir, Tree& t, TaskJob& j)
    {
        for (const auto& e : RangedDirectoryIterator (dir, true, "*", File::findFilesAndDirectories | File::ignoreHiddenFiles))
        {
            if (j.cancelled) return;
            t[e.getFile().getRelativePathFrom (dir).replaceCharacter ('\\', '/')] = { e.isDirectory(), e.isDirectory() ? 0 : e.getFileSize(), e.getModificationTime() };
        }
    }

    // prehľad diskov: obsadenie každého disku
    class DriveWindow : public Component
    {
    public:
        DriveWindow()
        {
            setWantsKeyboardFocus (true);
            Array<File> roots;
            File::findFileSystemRoots (roots);
            for (auto& r : roots)
                if (r.isDirectory()) drives.push_back ({ r, r.getVolumeLabel(), r.getBytesFreeOnVolume(), r.getVolumeTotalSize() });
            closeB.setButtonText (tr ("Zavrieť"));
            closeB.onClick = [this] { if (onClose) onClose(); };
            addAndMakeVisible (closeB);
        }
        std::function<void()> onClose;
        std::function<void (const File&)> onPick;
        void paint (Graphics& g) override
        {
            g.fillAll (col::panel);
            g.setColour (col::line);
            g.drawRect (getLocalBounds(), 1);
            g.setColour (col::text);
            g.setFont (FontOptions (15.0f, Font::bold));
            g.drawText (tr ("Prehľad diskov"), getLocalBounds().removeFromTop (38).reduced (14, 0), Justification::centredLeft, true);
            for (size_t i = 0; i < drives.size(); ++i)
            {
                const auto& d = drives[i];
                auto r = rowRect ((int) i);
                if ((int) i == hover) { g.setColour (col::panelHi); g.fillRoundedRectangle (r.toFloat(), 3.0f); }
                auto inner = r.reduced (10, 6);
                g.setColour (col::text);
                g.setFont (FontOptions (14.0f, Font::bold));
                g.drawText (d.root.getFullPathName().trimCharactersAtEnd ("\\/") + "  " + d.label, inner.removeFromTop (18), Justification::centredLeft, true);
                const double used = d.total > 0 ? 1.0 - (double) d.free / (double) d.total : 0.0;
                auto bar = inner.removeFromTop (14).toFloat();
                g.setColour (col::docBg);
                g.fillRoundedRectangle (bar, 3.0f);
                g.setColour (used > 0.9 ? Colour (0xffc05050) : col::accentHi);
                g.fillRoundedRectangle (bar.withWidth ((float) (bar.getWidth() * used)), 3.0f);
                g.setColour (col::text);
                g.setFont (FontOptions (12.5f));
                g.drawText (sizeText (d.free) + tr (" voľných z ") + sizeText (d.total) + "  (" + String (roundToInt (used * 100)) + tr (" % obsadené)"),
                            inner, Justification::centredLeft, true);
            }
        }
        void resized() override { closeB.setBounds (getLocalBounds().reduced (14).removeFromBottom (30).removeFromRight (120)); }
        void mouseMove (const MouseEvent& e) override { int h = rowAt (e.getPosition()); if (h != hover) { hover = h; repaint(); } }
        void mouseExit (const MouseEvent&) override { hover = -1; repaint(); }
        void mouseUp (const MouseEvent& e) override { const int r = rowAt (e.getPosition()); if (r >= 0 && onPick) onPick (drives[(size_t) r].root); }
        bool keyPressed (const KeyPress& k) override { if (k.isKeyCode (KeyPress::escapeKey)) { if (onClose) onClose(); return true; } return false; }
        int preferredHeight() const { return 38 + (int) drives.size() * 58 + 60; }

    private:
        struct D { File root; String label; int64 free, total; };
        std::vector<D> drives;
        TextButton closeB;
        int hover = -1;
        Rectangle<int> rowRect (int i) const { return { 14, 40 + i * 58, getWidth() - 28, 54 }; }
        int rowAt (Point<int> p) const { for (int i = 0; i < (int) drives.size(); ++i) if (rowRect (i).contains (p)) return i; return -1; }
    };

    // čo zaberá miesto: mapa priečinka (obdĺžniky podľa veľkosti)
    class SpaceWindow : public Component
    {
    public:
        explicit SpaceWindow (const File& start)
        {
            setWantsKeyboardFocus (true);
            for (auto* b : { &upB, &openB, &closeB }) addAndMakeVisible (b);
            upB.setButtonText (tr ("O úroveň vyššie"));
            openB.setButtonText (tr ("Otvoriť v paneli"));
            closeB.setButtonText (tr ("Zavrieť"));
            upB.onClick = [this] { if (dir.getParentDirectory() != dir) load (dir.getParentDirectory()); };
            openB.onClick = [this] { if (onOpen) onOpen (dir); };
            closeB.onClick = [this] { if (onClose) onClose(); };
            load (start);
        }
        ~SpaceWindow() override { *alive = false; }
        std::function<void()> onClose;
        std::function<void (const File&)> onOpen;

        void load (const File& d)
        {
            dir = d;
            items.clear();
            rects.clear();
            loading = true;
            repaint();
            const int gen = ++generation;
            auto life = alive;
            Component::SafePointer<SpaceWindow> sp (this);
            Thread::launch ([d, gen, sp, life]
            {
                std::vector<Item> v;
                for (const auto& e : RangedDirectoryIterator (d, false, "*", File::findFilesAndDirectories))
                {
                    if (! *life) return;
                    Item it { e.getFile(), e.isDirectory(), 0 };
                    if (it.dir)
                        for (const auto& x : RangedDirectoryIterator (it.file, true, "*", File::findFiles))
                        {
                            if (! *life) return;
                            it.size += x.getFileSize();
                        }
                    else it.size = e.getFileSize();
                    v.push_back (it);
                }
                std::sort (v.begin(), v.end(), [] (const Item& a, const Item& b) { return a.size > b.size; });
                MessageManager::callAsync ([sp, v, gen]
                {
                    if (sp == nullptr || sp->generation != gen) return;
                    sp->items = v;
                    sp->loading = false;
                    sp->layout();
                    sp->repaint();
                });
            });
        }

        void paint (Graphics& g) override
        {
            g.fillAll (col::panel);
            g.setColour (col::line);
            g.drawRect (getLocalBounds(), 1);
            int64 total = 0;
            for (auto& i : items) total += i.size;
            g.setColour (col::text);
            g.setFont (FontOptions (15.0f, Font::bold));
            g.drawText (tr ("Čo zaberá miesto: ") + dir.getFullPathName() + (loading ? String() : "  (" + sizeText (total) + ")"),
                        getLocalBounds().removeFromTop (38).reduced (14, 0), Justification::centredLeft, true);
            auto area = mapArea();
            if (loading)
            {
                g.setFont (FontOptions (14.0f));
                g.drawText (tr ("Počítam veľkosti priečinkov…"), area, Justification::centred, true);
                return;
            }
            const Colour tones[] = { Colour (0xff2d4362), Colour (0xff3b5a7a), Colour (0xff4a6a5a), Colour (0xff5a4f6e), Colour (0xff6a5a3e), Colour (0xff3e6266) };
            for (size_t i = 0; i < rects.size(); ++i)
            {
                const auto& it = items[i];
                auto r = rects[i].reduced (1.0f);
                g.setColour ((int) i == hover ? tones[i % 6].brighter (0.35f) : (it.dir ? tones[i % 6] : col::panelHi));
                g.fillRoundedRectangle (r, 3.0f);
                if (r.getWidth() > 50 && r.getHeight() > 26)
                {
                    g.setColour (col::selText);
                    g.setFont (FontOptions (12.5f, it.dir ? Font::bold : Font::plain));
                    auto t = r.reduced (5.0f, 3.0f);
                    g.drawText (it.file.getFileName(), t.removeFromTop (16.0f).toNearestInt(), Justification::topLeft, true);
                    g.setFont (FontOptions (12.0f));
                    g.drawText (sizeText (it.size), t.removeFromTop (15.0f).toNearestInt(), Justification::topLeft, true);
                }
            }
        }
        void resized() override
        {
            auto b = getLocalBounds().reduced (14).removeFromBottom (30);
            closeB.setBounds (b.removeFromRight (120));
            b.removeFromRight (8);
            openB.setBounds (b.removeFromRight (150));
            b.removeFromRight (8);
            upB.setBounds (b.removeFromRight (150));
            layout();
        }
        void mouseMove (const MouseEvent& e) override { const int h = at (e.position); if (h != hover) { hover = h; repaint(); } }
        void mouseExit (const MouseEvent&) override { hover = -1; repaint(); }
        void mouseUp (const MouseEvent& e) override
        {
            const int i = at (e.position);
            if (i < 0) return;
            if (e.mods.isPopupMenu() || ! items[(size_t) i].dir) { if (onOpen) onOpen (items[(size_t) i].dir ? items[(size_t) i].file : dir); return; }
            load (items[(size_t) i].file);
        }
        bool keyPressed (const KeyPress& k) override
        {
            if (k.isKeyCode (KeyPress::escapeKey)) { if (onClose) onClose(); return true; }
            if (k.isKeyCode (KeyPress::backspaceKey)) { upB.triggerClick(); return true; }
            return false;
        }

    private:
        struct Item { File file; bool dir; int64 size; };
        File dir;
        std::vector<Item> items;
        std::vector<Rectangle<float>> rects;
        bool loading = false;
        int hover = -1, generation = 0;
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
        TextButton upB, openB, closeB;
        Rectangle<int> mapArea() const { return getLocalBounds().reduced (14).withTrimmedTop (28).withTrimmedBottom (40); }
        int at (Point<float> p) const { for (size_t i = 0; i < rects.size(); ++i) if (rects[i].contains (p)) return (int) i; return -1; }

        // squarified treemap
        void layout()
        {
            rects.assign (items.size(), {});
            int64 total = 0;
            size_t n = 0;
            for (auto& i : items) { if (i.size <= 0) break; total += i.size; ++n; }
            if (n == 0 || total <= 0) return;
            auto area = mapArea().toFloat();
            const double scale = (double) area.getWidth() * area.getHeight() / (double) total;
            size_t i = 0;
            while (i < n && area.getWidth() > 1 && area.getHeight() > 1)
            {
                const bool horiz = area.getWidth() >= area.getHeight();
                const double side = horiz ? area.getHeight() : area.getWidth();
                size_t j = i;
                double sum = 0, best = 1e30;
                while (j < n)
                {
                    const double s2 = sum + items[j].size * scale;
                    const double len = s2 / side;
                    double worst = 0;
                    for (size_t k = i; k <= j; ++k)
                    {
                        const double other = items[k].size * scale / len;
                        worst = jmax (worst, jmax (len / other, other / len));
                    }
                    if (worst > best) break;
                    best = worst;
                    sum = s2;
                    ++j;
                }
                const float len = (float) (sum / side);
                float pos = horiz ? area.getY() : area.getX();
                for (size_t k = i; k < j; ++k)
                {
                    const float other = (float) (items[k].size * scale / len);
                    rects[k] = horiz ? Rectangle<float> (area.getX(), pos, len, other) : Rectangle<float> (pos, area.getY(), other, len);
                    pos += other;
                }
                if (horiz) area.removeFromLeft (len); else area.removeFromTop (len);
                i = j;
            }
        }
    };

    void walkRemote (FtpSession& s, const String& root, const String& rel, Tree& t, TaskJob& j)
    {
        std::vector<FtpEntry> v;
        String err;
        if (! s.list (rel.isEmpty() ? root : FtpSession::join (root, rel), v, err)) { if (j.error.isEmpty()) j.error = err; return; }
        for (auto& e : v)
        {
            if (j.cancelled) return;
            const String r = rel.isEmpty() ? e.name : rel + "/" + e.name;
            t[r] = { e.dir, e.size, e.mod };
            if (e.dir) walkRemote (s, root, r, t, j);
        }
    }
}

// ---------------------------------------------------------------- spoločné okná

void Commander::dialogMulti (const String& title, const String& text, const StringArray& labels, const StringArray& values,
                             const StringArray& buttons, std::function<void (int, StringArray)> done)
{
    if (dialogWin != nullptr) return;
    dialogWin = std::make_unique<AlertWindow> (title, text, MessageBoxIconType::NoIcon, this);
    for (int i = 0; i < labels.size(); ++i) dialogWin->addTextEditor ("e" + String (i), values[i], labels[i]);
    for (int i = 0; i < buttons.size(); ++i)
    {
        const bool last = i == buttons.size() - 1;
        dialogWin->addButton (buttons[i], last ? 0 : i + 1,
                              i == 0 ? KeyPress (KeyPress::returnKey) : (last ? KeyPress (KeyPress::escapeKey) : KeyPress()));
        if (auto* b = dialogWin->getButton (i)) { b->setWantsKeyboardFocus (true); b->addKeyListener (&dialogKeys); }
    }
    addAndMakeVisible (*dialogWin);
    dialogWin->setSize (jmin (560, getWidth() - 40), dialogWin->getHeight());
    for (int i = 0; i < labels.size(); ++i)
        if (auto* ed = dialogWin->getTextEditor ("e" + String (i))) ed->setSize (dialogWin->getWidth() - 50, ed->getHeight());
    dialogWin->setCentrePosition (getLocalBounds().getCentre());
    const int n = labels.size();
    Component::SafePointer<Commander> sp (this);
    dialogWin->enterModalState (true, ModalCallbackFunction::create ([sp, done, n] (int result)
    {
        if (sp == nullptr || sp->dialogWin == nullptr) return;
        StringArray v;
        for (int i = 0; i < n; ++i) v.add (sp->dialogWin->getTextEditorContents ("e" + String (i)));
        auto* w = sp->dialogWin.release();
        w->setVisible (false);
        MessageManager::callAsync ([w] { delete w; });
        sp->act->grabKeyboardFocus();
        if (done) done (result, v);
    }));
    highlightButton (0, false);
    if (auto* ed = dialogWin->getTextEditor ("e0")) { ed->grabKeyboardFocus(); ed->selectAll(); }
}

void Commander::showList (const String& title, const StringArray& lines, const String& action,
                          std::function<void()> onAction, std::function<void (int)> onPick)
{
    if (listWin != nullptr) return;
    const bool mono = title.startsWith (tr ("Porovnanie"));
    auto w = std::make_unique<ListWindow> (title, lines, action, mono);
    Component::SafePointer<Commander> sp (this);
    auto close = [sp]
    {
        if (sp == nullptr || sp->listWin == nullptr) return;
        sp->listWin->exitModalState (0);
        auto* c = sp->listWin.release();
        c->setVisible (false);
        MessageManager::callAsync ([c] { delete c; });
        sp->focusActive();
    };
    w->onClose = close;
    if (onAction) w->onAction = [close, onAction] { close(); onAction(); };
    if (onPick) w->onPick = [close, onPick] (int i) { close(); onPick (i); };
    listWin = std::move (w);
    addAndMakeVisible (*listWin);
    resized();
    listWin->enterModalState (true);
    static_cast<ListWindow*> (listWin.get())->list.selectRow (0);
    static_cast<ListWindow*> (listWin.get())->list.grabKeyboardFocus();
}

// ---------------------------------------------------------------- rýchly náhľad (Ctrl+Q)

void Commander::toggleQuick()
{
    if (quickComp != nullptr)
    {
        auto* c = quickComp;
        quickComp = nullptr;
        removeChildComponent (c);
        otherPanel().setVisible (true);
        if (quickDone) quickDone (c);
        focusActive();
        return;
    }
    if (! quickPreview) return;
    quickComp = quickPreview();
    if (quickComp == nullptr) return;
    addAndMakeVisible (quickComp);
    otherPanel().setVisible (false);
    resized();
    cursorMoved (*act);
    focusActive();
}

void Commander::cursorMoved (CmdPanel& p)
{
    if (&p == act && p.isNet() && netDetail != nullptr && netDetail->isVisible()) netDetail->setHost (p.netHostIndex());
    if (&p == act && getWidth() > 0)
    {
        const int want = jmin ((getWidth() - 4) / 2, GlyphArrangement::getStringWidthInt (FontOptions (13.0f), act->displayPath() + ">") + 14) + 2;
        if (cmdLine.getX() != want) resized();
        repaint (0, getHeight() - fbarH - msgH - cmdH, getWidth(), cmdH);
    }
    if (quickComp != nullptr && &p == act && quickShow) quickShow (p.currentFile());
}

// ---------------------------------------------------------------- obľúbené (Ctrl+D)

void Commander::favMenu()
{
    StringArray fav = StringArray::fromLines (settings.getValue ("kcFav"));
    fav.removeEmptyStrings();
    PopupMenu m, rem;
    for (int i = 0; i < fav.size(); ++i) { m.addItem (i + 1, fav[i]); rem.addItem (2000 + i, fav[i]); }
    if (! fav.isEmpty()) m.addSeparator();
    m.addItem (1000, tr ("Pridať aktuálny priečinok"), ! act->isVirtual() && ! fav.contains (act->getFolder().getFullPathName()));
    m.addSubMenu (tr ("Odstrániť"), rem, ! fav.isEmpty());
    Component::SafePointer<Commander> sp (this);
    CmdPanel* p = act;
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (p), [sp, fav, p] (int r) mutable
    {
        if (sp == nullptr || r == 0) return;
        if (r == 1000) fav.add (p->getFolder().getFullPathName());
        else if (r >= 2000) fav.remove (r - 2000);
        else { const File f (fav[r - 1]); if (f.isDirectory()) p->setFolder (f); else sp->message (tr ("Priečinok neexistuje: ") + fav[r - 1], true); }
        sp->settings.setValue ("kcFav", fav.joinIntoString ("\n"));
        sp->focusActive();
    });
}

// ---------------------------------------------------------------- hromadné premenovanie (Ctrl+M)

void Commander::multiRename()
{
    if (busy()) return;
    if (act->isVirtual()) { message (tr ("Hromadne premenovať sa dajú súbory na disku."), true); return; }
    const auto files = act->selection();
    if (files.isEmpty()) return;
    CmdPanel* p = act;
    Component::SafePointer<Commander> sp (this);
    dialogMulti (tr ("Hromadné premenovanie"), tr ("[N] = pôvodný názov, [C] = počítadlo, [E] = pôvodná prípona"),
                 { tr ("Maska názvu"), tr ("Prípona"), tr ("Hľadať"), tr ("Nahradiť za"), tr ("Počítadlo od"), tr ("Počet číslic") },
                 { "[N]", "[E]", "", "", "1", "1" }, { tr ("Ďalej"), tr ("Zrušiť") }, [sp, files, p] (int r, StringArray v)
    {
        if (sp == nullptr || r != 1) return;
        std::vector<std::pair<File, File>> plan;
        StringArray lines;
        const int start = v[4].getIntValue(), digits = jlimit (1, 9, v[5].getIntValue());
        for (int i = 0; i < files.size(); ++i)
        {
            const auto& f = files.getReference (i);
            const String base = f.isDirectory() ? f.getFileName() : f.getFileNameWithoutExtension();
            const String ext = f.isDirectory() ? String() : f.getFileExtension().fromFirstOccurrenceOf (".", false, false);
            const String c = String (start + i).paddedLeft ('0', digits);
            String n = fill (v[0], base, ext, c);
            const String e = f.isDirectory() ? String() : fill (v[1], base, ext, c);
            if (v[2].isNotEmpty()) n = n.replace (v[2], v[3]);
            const String nn = (n + (e.isNotEmpty() ? "." + e : String())).trim();
            if (nn.isEmpty() || nn.containsAnyOf ("\\/:*?\"<>|") || nn == f.getFileName()) continue;
            plan.push_back ({ f, f.getSiblingFile (nn) });
            lines.add (f.getFileName() + String::fromUTF8 ("  \xe2\x86\x92  ") + nn);
        }
        if (plan.empty()) { sp->message (tr ("Názvy by sa nezmenili.")); return; }
        sp->showList (tr ("Hromadné premenovanie – náhľad (") + items ((int) plan.size()) + ")", lines, tr ("Premenovať"), [sp, plan, p]
        {
            if (sp == nullptr) return;
            // najprv dočasné názvy (aby sa dali aj vymeniť), potom konečné
            std::vector<std::pair<File, File>> tmp;
            for (size_t i = 0; i < plan.size(); ++i)
            {
                const auto t = plan[i].first.getSiblingFile (plan[i].first.getFileName() + ".kc-tmp" + String ((int) i));
                if (plan[i].first.moveFileTo (t)) tmp.push_back ({ t, plan[i].second });
            }
            int ok = 0, skipped = 0;
            std::vector<std::pair<File, File>> done;
            for (size_t i = 0; i < tmp.size(); ++i)
            {
                const File orig = tmp[i].first.getSiblingFile (tmp[i].first.getFileName().upToLastOccurrenceOf (".kc-tmp", false, false));
                if (! tmp[i].second.exists() && tmp[i].first.moveFileTo (tmp[i].second)) { ++ok; done.push_back ({ orig, tmp[i].second }); }
                else
                {
                    ++skipped;   // cieľ už existuje → späť na pôvodný názov
                    tmp[i].first.moveFileTo (tmp[i].first.getSiblingFile (tmp[i].first.getFileName().upToLastOccurrenceOf (".kc-tmp", false, false)));
                }
            }
            sp->pushUndo (done);
            p->clearMarks();
            p->reload();
            sp->message (tr ("Premenované: ") + String (ok) + (skipped > 0 ? tr (" · preskočené (názov už existuje): ") + String (skipped) : String()), skipped > 0);
        }, nullptr);
    });
}

// ---------------------------------------------------------------- hľadanie (Alt+F7)

void Commander::searchFiles()
{
    if (busy()) return;
    if (act->isVirtual()) { message (tr ("Hľadať sa dá na disku."), true); return; }
    const File dir = act->getFolder();
    CmdPanel* p = act;
    Component::SafePointer<Commander> sp (this);
    dialogMulti (tr ("Hľadať súbory"), tr ("Hľadá v ") + dir.getFullPathName() + tr (" a v podpriečinkoch"),
                 { tr ("Názov (napr. *.mp3;*.wav)"), tr ("Obsahuje text (nepovinné)") }, { "*", "" },
                 { tr ("Hľadať"), tr ("Zrušiť") }, [sp, dir, p] (int r, StringArray v)
    {
        if (sp == nullptr || r != 1) return;
        StringArray masks;
        masks.addTokens (v[0].trim().isEmpty() ? String ("*") : v[0], ";,", "\"");
        masks.trim();
        masks.removeEmptyStrings();
        const String text = v[1];
        sp->startTask (tr ("Hľadám"), [dir, masks, text] (TaskJob& j)
        {
            Array<File> tops;
            for (const auto& e : RangedDirectoryIterator (dir, false, "*", File::findFilesAndDirectories)) tops.add (e.getFile());
            auto check = [&] (const File& f)
            {
                const auto n = f.getFileName();
                bool match = false;
                for (auto& m : masks) if (n.matchesWildcard (m, true)) { match = true; break; }
                if (! match) return;
                if (text.isNotEmpty())
                {
                    if (f.isDirectory() || f.getSize() > 50 * 1024 * 1024) return;
                    if (! f.loadFileAsString().containsIgnoreCase (text)) return;
                }
                j.lines.push_back (f.getFullPathName());
            };
            for (int i = 0; i < tops.size() && ! j.cancelled && j.lines.size() < 5000; ++i)
            {
                check (tops[i]);
                if (tops[i].isDirectory())
                    for (const auto& e : RangedDirectoryIterator (tops[i], true, "*", File::findFilesAndDirectories))
                    {
                        if (j.cancelled || j.lines.size() >= 5000) break;
                        check (e.getFile());
                    }
                j.progress = (double) (i + 1) / (double) jmax (1, tops.size());
            }
        }, [sp, dir, p] (TaskJob& j)
        {
            if (sp == nullptr) return;
            if (j.lines.empty()) { sp->message (tr ("Nič sa nenašlo.")); return; }
            StringArray shown;
            for (auto& l : j.lines) shown.add (File (l).getRelativePathFrom (dir));
            auto full = j.lines;
            sp->message ({});
            sp->showList (tr ("Nájdené: ") + String ((int) full.size()) + (full.size() >= 5000 ? tr (" (prvých 5000)") : String()), shown, {}, nullptr,
                          [sp, full, p] (int i)
            {
                if (sp == nullptr || i < 0 || i >= (int) full.size()) return;
                const File f (full[(size_t) i]);
                sp->setActive (*p);
                p->setFolder (f.getParentDirectory(), f);
            });
        });
    });
}

// ---------------------------------------------------------------- porovnanie podľa obsahu

void Commander::compareFiles()
{
    if (busy()) return;
    File a, b;
    const auto sel = act->selection();
    if (sel.size() >= 2 && sel[0].existsAsFile() && sel[1].existsAsFile()) { a = sel[0]; b = sel[1]; }
    else { a = act->currentFile(); b = otherPanel().currentFile(); if (&otherPanel() == &left) std::swap (a, b); }
    if (! a.existsAsFile() || ! b.existsAsFile())
    {
        message (tr ("Daj kurzor na súbor v oboch paneloch, alebo označ dva súbory."), true);
        return;
    }
    Component::SafePointer<Commander> sp (this);
    startTask (tr ("Porovnávam"), [a, b] (TaskJob& j)
    {
        FileInputStream ia (a), ib (b);
        if (! ia.openedOk() || ! ib.openedOk()) { j.error = tr ("Súbor sa nedá otvoriť."); return; }
        // rovnaké bajty?
        bool same = a.getSize() == b.getSize();
        int64 firstDiff = -1, pos = 0;
        bool textLike = a.getSize() < 5 * 1024 * 1024 && b.getSize() < 5 * 1024 * 1024;
        HeapBlock<char> ba (65536), bb (65536);
        for (;;)
        {
            if (j.cancelled) return;
            const int na = ia.read (ba, 65536), nb = ib.read (bb, 65536);
            if (na <= 0 && nb <= 0) break;
            const int n = jmin (na, nb);
            if (pos < 8192)
                for (int i = 0; i < jmax (na, nb) && i < 8192; ++i)
                    if ((i < na && ba[i] == 0) || (i < nb && bb[i] == 0)) textLike = false;
            if (firstDiff < 0)
                for (int i = 0; i < n; ++i) if (ba[i] != bb[i]) { firstDiff = pos + i; break; }
            if (firstDiff < 0 && na != nb) firstDiff = pos + n;
            pos += jmax (na, nb);
            j.progress = (double) pos / (double) jmax ((int64) 1, jmax (a.getSize(), b.getSize()));
            if (firstDiff >= 0 && ! textLike) break;
        }
        same = same && firstDiff < 0;
        if (same) { j.result = "same"; return; }
        if (! textLike) { j.result = "bin:" + String (firstDiff); return; }

        StringArray la = StringArray::fromLines (a.loadFileAsString()), lb = StringArray::fromLines (b.loadFileAsString());
        const int n = la.size(), m = lb.size();
        std::vector<String> out;
        if ((int64) n * m <= 4000000)
        {
            std::vector<int> dp ((size_t) (n + 1) * (size_t) (m + 1), 0);
            auto at = [&] (int i, int k) -> int& { return dp[(size_t) i * (size_t) (m + 1) + (size_t) k]; };
            for (int i = n - 1; i >= 0; --i)
                for (int k = m - 1; k >= 0; --k)
                    at (i, k) = la[i] == lb[k] ? at (i + 1, k + 1) + 1 : jmax (at (i + 1, k), at (i, k + 1));
            int i = 0, k = 0;
            while (i < n || k < m)
            {
                if (i < n && k < m && la[i] == lb[k]) { out.push_back ("  " + la[i]); ++i; ++k; }
                else if (i < n && (k == m || at (i + 1, k) >= at (i, k + 1))) { out.push_back ("- " + la[i]); ++i; }
                else { out.push_back ("+ " + lb[k]); ++k; }
            }
        }
        else
            for (int i = 0; i < jmax (n, m); ++i)
            {
                if (i < n && i < m && la[i] == lb[i]) out.push_back ("  " + la[i]);
                else { if (i < n) out.push_back ("- " + la[i]); if (i < m) out.push_back ("+ " + lb[i]); }
            }
        // dlhé rovnaké úseky skrátiť
        std::vector<String> shortOut;
        for (size_t i = 0; i < out.size();)
        {
            size_t e = i;
            while (e < out.size() && out[e].startsWith ("  ")) ++e;
            if (e - i > 6)
            {
                shortOut.push_back (out[i]); shortOut.push_back (out[i + 1]);
                shortOut.push_back (String::fromUTF8 ("  \xe2\x80\xa6"));
                shortOut.push_back (out[e - 2]); shortOut.push_back (out[e - 1]);
                i = e;
            }
            else if (e > i) { for (size_t q = i; q < e; ++q) shortOut.push_back (out[q]); i = e; }
            else shortOut.push_back (out[i++]);
        }
        j.lines = shortOut;
        j.result = "text";
    }, [sp, a, b] (TaskJob& j)
    {
        if (sp == nullptr) return;
        if (j.error.isNotEmpty()) { sp->message (j.error, true); return; }
        const String r = j.result.toString();
        if (r == "same") { sp->message (tr ("Súbory sú rovnaké: ") + a.getFileName() + " = " + b.getFileName()); return; }
        if (r.startsWith ("bin:")) { sp->message (tr ("Súbory sa líšia (prvý rozdiel na bajte ") + r.substring (4) + ")."); return; }
        StringArray lines;
        for (auto& l : j.lines) lines.add (l);
        sp->message ({});
        sp->showList (tr ("Porovnanie: ") + a.getFileName() + String::fromUTF8 ("  \xe2\x86\x94  ") + b.getFileName()
                      + tr ("  (− len vľavo, + len vpravo)"), lines, {}, nullptr, nullptr);
    });
}

// ---------------------------------------------------------------- synchronizácia priečinkov

void Commander::syncDirs()
{
    if (busy()) return;
    if (left.isArchive() || right.isArchive()) { message (tr ("Archív sa synchronizovať nedá."), true); return; }
    if (left.isRemote() && right.isRemote()) { message (tr ("Synchronizovať sa dá disk s diskom alebo disk so serverom."), true); return; }
    const File ld = left.getFolder(), rd = right.getFolder();
    auto ls = left.session(), rs = right.session();
    const String lrd = left.remoteDir(), rrd = right.remoteDir();
    if (ls == nullptr && rs == nullptr && ld == rd) { message (tr ("Oba panely ukazujú ten istý priečinok."), true); return; }
    Component::SafePointer<Commander> sp (this);
    dialog (tr ("Synchronizovať priečinky"),
            tr ("Ľavý: ") + left.displayPath() + "\n" + tr ("Pravý: ") + right.displayPath() + "\n\n"
            + tr ("Porovná aj podpriečinky podľa veľkosti a dátumu. Nič sa nemaže."), nullptr,
            { String::fromUTF8 ("\xe2\x86\x92 ") + tr ("Doprava"), String::fromUTF8 ("\xe2\x86\x90 ") + tr ("Doľava"),
              tr ("Oba smery"), tr ("Zrušiť") },
            [sp, ld, rd, ls, rs, lrd, rrd] (int dirn, String)
    {
        if (sp == nullptr || dirn < 1 || dirn > 3) return;
        sp->startTask (tr ("Porovnávam"), [ld, rd, ls, rs, lrd, rrd, dirn] (TaskJob& j)
        {
            Tree L, R;
            if (ls != nullptr) walkRemote (*ls, lrd, {}, L, j); else walkLocal (ld, L, j);
            j.progress = 0.5;
            if (rs != nullptr) walkRemote (*rs, rrd, {}, R, j); else walkLocal (rd, R, j);
            if (j.cancelled) return;
            // 1 = zľava doprava, 2 = sprava doľava; chýbajúci priečinok sa kopíruje celý
            std::vector<std::pair<String, int>> plan;
            StringArray skipUnder;
            auto skipped = [&] (const String& rel) { for (auto& s : skipUnder) if (rel.startsWith (s + "/")) return true; return false; };
            auto consider = [&] (const Tree& A, const Tree& B, int code)
            {
                for (auto& [rel, a] : A)
                {
                    if (skipped (rel)) continue;
                    auto it = B.find (rel);
                    if (it == B.end()) { plan.push_back ({ rel, code }); if (a.dir) skipUnder.add (rel); continue; }
                    const auto& b = it->second;
                    if (a.dir || b.dir) continue;
                    const bool newer = a.mod.toMilliseconds() > b.mod.toMilliseconds() + 120000;
                    const bool older = b.mod.toMilliseconds() > a.mod.toMilliseconds() + 120000;
                    if (newer || (a.size != b.size && ! older)) plan.push_back ({ rel, code });
                }
            };
            if (dirn == 1 || dirn == 3) consider (L, R, 1);
            if (dirn == 2 || dirn == 3) consider (R, L, 2);
            for (auto& [rel, code] : plan) j.lines.push_back (String (code) + "|" + rel);
            j.progress = 1.0;
        }, [sp, ld, rd, ls, rs, lrd, rrd, dirn] (TaskJob& j)
        {
            if (sp == nullptr) return;
            if (j.error.isNotEmpty()) sp->message (j.error, true);
            if (j.lines.empty()) { if (j.error.isEmpty()) sp->message (tr ("Priečinky sú rovnaké.")); return; }
            std::vector<std::pair<String, int>> plan;
            StringArray lines;
            for (auto& l : j.lines)
            {
                const int code = l.upToFirstOccurrenceOf ("|", false, false).getIntValue();
                const String rel = l.fromFirstOccurrenceOf ("|", false, false);
                plan.push_back ({ rel, code });
                lines.add (String::fromUTF8 (code == 1 ? "\xe2\x86\x92  " : "\xe2\x86\x90  ") + rel);
            }
            sp->message ({});
            sp->showList (tr ("Synchronizácia – ") + items ((int) plan.size()), lines, tr ("Synchronizovať"),
                          [sp, dirn, plan, ld, rd, ls, lrd, rs, rrd] { if (sp != nullptr) sp->runSync (dirn, plan, ld, rd, ls, lrd, rs, rrd); }, nullptr);
        });
    });
}

void Commander::runSync (int, const std::vector<std::pair<String, int>>& plan, const File& ld, const File& rd,
                         std::shared_ptr<FtpSession> ls, String lrd, std::shared_ptr<FtpSession> rs, String rrd)
{
    Component::SafePointer<Commander> sp (this);
    auto finish = [sp] (const String& m, bool err)
    {
        if (sp == nullptr) return;
        sp->left.reload();
        sp->right.reload();
        sp->message (m, err);
    };
    if (ls == nullptr && rs == nullptr)
    {
        std::vector<Transfer::Item> its;
        for (auto& [rel, code] : plan)
        {
            const File from = (code == 1 ? ld : rd).getChildFile (rel), to = (code == 1 ? rd : ld).getChildFile (rel);
            to.getParentDirectory().createDirectory();
            if (from.exists()) its.push_back ({ from, to });
        }
        startTransfer (std::move (its), false, [finish] (Transfer& t)
        {
            finish (tr ("Synchronizované: ") + String ((int) t.completed.size()) + (t.error.isNotEmpty() ? " · " + t.error : String()), t.error.isNotEmpty());
        });
        return;
    }
    // disk ↔ server: nahrať aj stiahnuť (dve úlohy za sebou)
    const bool leftRemote = ls != nullptr;
    auto s = leftRemote ? ls : rs;
    const File local = leftRemote ? rd : ld;
    const String remote = leftRemote ? lrd : rrd;
    std::vector<FtpJob::Item> up, down;
    for (auto& [rel, code] : plan)
    {
        const bool toRemote = (code == 1) != leftRemote;   // smer k serveru
        const File lf = local.getChildFile (rel);
        const String rf = FtpSession::join (remote, rel);
        if (toRemote) { if (lf.exists()) up.push_back ({ lf, rf, lf.isDirectory(), lf.getSize() }); }
        else down.push_back ({ lf, rf, false, 0 });
    }
    auto runDown = [sp, s, down, finish] (String upMsg)
    {
        if (sp == nullptr) return;
        if (down.empty()) { finish (upMsg, false); return; }
        // pri sťahovaní treba vedieť, čo je priečinok – zistí sa zo zoznamu nadradeného priečinka
        std::vector<FtpJob::Item> its = down;
        for (auto& it : its)
        {
            std::vector<FtpEntry> v;
            String err;
            if (s->list (FtpSession::parent (it.remote), v, err))
                for (auto& e : v)
                    if (e.name == it.remote.fromLastOccurrenceOf ("/", false, false)) { it.dir = e.dir; it.size = e.size; }
            it.local.getParentDirectory().createDirectory();
        }
        sp->startFtpJob (s, FtpJob::Kind::Download, std::move (its), false, [finish, upMsg] (FtpJob& j)
        {
            finish (upMsg + tr ("Stiahnuté: ") + String (j.completedCount()) + (j.error.isNotEmpty() ? " · " + j.error : String()), j.error.isNotEmpty());
        });
    };
    if (up.empty()) { runDown ({}); return; }
    startFtpJob (s, FtpJob::Kind::Upload, std::move (up), false, [sp, runDown] (FtpJob& j)
    {
        const String m = tr ("Nahrané: ") + String (j.completedCount()) + (j.error.isNotEmpty() ? " · " + j.error : String()) + " · ";
        MessageManager::callAsync ([sp, runDown, m] { if (sp != nullptr) runDown (m); });
    });
}
}

namespace kp
{
// ---------------------------------------------------------------- Ctrl+Z

void Commander::pushUndo (std::vector<std::pair<File, File>> batch)
{
    if (batch.empty()) return;
    undoStack.push_back (std::move (batch));
    if (undoStack.size() > 30) undoStack.erase (undoStack.begin());
}

void Commander::undo()
{
    if (busy()) return;
    if (undoStack.empty()) { message (tr ("Nie je čo vrátiť.")); return; }
    auto batch = std::move (undoStack.back());
    undoStack.pop_back();
    int ok = 0;
    for (auto it = batch.rbegin(); it != batch.rend(); ++it)
        if (it->second.exists() && ! it->first.exists() && it->second.moveFileTo (it->first)) ++ok;
    left.reload();
    right.reload();
    message (tr ("Vrátené: ") + items (ok) + (ok < (int) batch.size() ? tr (" (niektoré sa nedali vrátiť)") : String()), ok < (int) batch.size());
}

// ---------------------------------------------------------------- dvojice priečinkov

void Commander::savePair()
{
    auto side = [] (CmdPanel& p, XmlElement& e, const String& pre)
    {
        if (p.isRemote() && p.session() != nullptr)
        {
            e.setAttribute (pre + "ftp", p.session()->info().name);
            e.setAttribute (pre + "dir", p.remoteDir());
        }
        else e.setAttribute (pre + "path", p.getFolder().getFullPathName());
    };
    if (left.isArchive() || right.isArchive()) { message (tr ("Archív sa do dvojice uložiť nedá."), true); return; }
    const String def = left.getFolder().getFileName() + String::fromUTF8 (" \xe2\x86\x94 ")
                     + (right.isRemote() && right.session() != nullptr ? right.session()->info().name : right.getFolder().getFileName());
    Component::SafePointer<Commander> sp (this);
    dialog (tr ("Uložiť dvojicu"), tr ("Názov dvojice:"), &def, { tr ("Uložiť"), tr ("Zrušiť") }, [sp, side] (int r, String name)
    {
        if (sp == nullptr || r != 1 || name.trim().isEmpty()) return;
        auto x = sp->settings.getXmlValue ("kcPairs");
        if (x == nullptr) x = std::make_unique<XmlElement> ("pairs");
        auto* e = x->createNewChildElement ("pair");
        e->setAttribute ("name", name.trim());
        side (sp->left, *e, "l");
        side (sp->right, *e, "r");
        sp->settings.setValue ("kcPairs", x.get());
        sp->message (tr ("Dvojica uložená: ") + name.trim());
    });
}

void Commander::openPair (int index)
{
    auto x = settings.getXmlValue ("kcPairs");
    if (x == nullptr) return;
    auto* e = x->getChildElement (index);
    if (e == nullptr) return;
    const auto conns = ftpstore::load (settings);
    bool waitFtp = false;
    for (auto [p, pre] : { std::pair<CmdPanel*, String> { &left, "l" }, std::pair<CmdPanel*, String> { &right, "r" } })
    {
        const String ftpName = e->getStringAttribute (pre + "ftp");
        if (ftpName.isNotEmpty())
        {
            auto it = std::find_if (conns.begin(), conns.end(), [&] (const FtpConn& c) { return c.name == ftpName; });
            if (it == conns.end()) { message (tr ("FTP pripojenie neexistuje: ") + ftpName, true); return; }
            connectFtpTo (*it, p, e->getStringAttribute (pre + "dir"));
            waitFtp = true;
        }
        else
        {
            const File f (e->getStringAttribute (pre + "path"));
            if (! f.isDirectory()) { message (tr ("Priečinok neexistuje: ") + f.getFullPathName(), true); return; }
            p->setFolder (f);
        }
    }
    if (waitFtp) pendingSync = true;   // porovnanie sa spustí po pripojení
    else syncDirs();
}

// ---------------------------------------------------------------- automatické nahrávanie

void Commander::toggleAutoUpload()
{
    if (autoUp != nullptr)
    {
        autoUp.reset();
        message (tr ("Automatické nahrávanie je vypnuté."));
        repaint();
        return;
    }
    CmdPanel* r = left.isRemote() ? &left : (right.isRemote() ? &right : nullptr);
    CmdPanel* l = r == &left ? &right : &left;
    if (r == nullptr || l->isVirtual()) { message (tr ("V jednom paneli otvor priečinok na disku a v druhom server."), true); return; }
    auto a = std::make_unique<AutoUp>();
    a->local = l->getFolder();
    a->remote = r->remoteDir();
    a->s = r->session();
    for (const auto& en : RangedDirectoryIterator (a->local, true, "*", File::findFilesAndDirectories | File::ignoreHiddenFiles))
    {
        const String rel = en.getFile().getRelativePathFrom (a->local).replaceCharacter ('\\', '/');
        if (en.isDirectory()) a->dirs.insert (rel); else a->known[rel] = en.getModificationTime();
    }
    autoUp = std::move (a);
    message (tr ("Automatické nahrávanie: každý uložený súbor z ") + autoUp->local.getFullPathName() + tr (" sa nahrá na server."));
    repaint();
}

void Commander::autoUploadScan()
{
    auto s = autoUp->s.lock();
    if (s == nullptr || ! autoUp->local.isDirectory()) { autoUp.reset(); message (tr ("Automatické nahrávanie skončilo (spojenie je zatvorené).")); repaint(); return; }
    std::vector<FtpJob::Item> its;
    std::vector<String> changed;
    StringArray newDirs;
    for (const auto& en : RangedDirectoryIterator (autoUp->local, true, "*", File::findFilesAndDirectories | File::ignoreHiddenFiles))
    {
        const String rel = en.getFile().getRelativePathFrom (autoUp->local).replaceCharacter ('\\', '/');
        bool underNew = false;
        for (auto& d : newDirs) if (rel.startsWith (d + "/")) { underNew = true; break; }
        if (en.isDirectory())
        {
            if (autoUp->dirs.count (rel) || underNew) { if (underNew) autoUp->dirs.insert (rel); continue; }
            autoUp->dirs.insert (rel);
            newDirs.add (rel);
            its.push_back ({ en.getFile(), FtpSession::join (autoUp->remote, rel), true, 0 });   // nový priečinok celý
            continue;
        }
        const auto t = en.getModificationTime();
        auto it = autoUp->known.find (rel);
        if (it != autoUp->known.end() && it->second == t) continue;
        autoUp->known[rel] = t;
        if (underNew) continue;
        // súbor sa ešte zapisuje? (zmena pred menej ako sekundou) – nahrá sa pri ďalšom kole
        if (Time::getCurrentTime() - t < RelativeTime::seconds (1.0)) { autoUp->known.erase (rel); continue; }
        its.push_back ({ en.getFile(), FtpSession::join (autoUp->remote, rel), false, en.getFileSize() });
        changed.push_back (rel);
    }
    if (its.empty()) return;
    const int n = (int) its.size();
    Component::SafePointer<Commander> sp (this);
    startFtpJob (s, FtpJob::Kind::Upload, std::move (its), false, [sp, n, changed] (FtpJob& j)
    {
        if (sp == nullptr) return;
        if (j.error.isNotEmpty() && sp->autoUp != nullptr)
            for (auto& c : changed) sp->autoUp->known.erase (c);   // skúsi sa znova
        sp->left.reload();
        sp->right.reload();
        sp->message (j.error.isNotEmpty() ? tr ("Automatické nahrávanie: ") + j.error
                                          : tr ("Automaticky nahrané: ") + items (n) + " (" + Time::getCurrentTime().toString (false, true, false, true) + ")",
                     j.error.isNotEmpty());
    });
}
}

namespace kp
{
// ---------------------------------------------------------------- spoločné prekrytia

void Commander::closeOverlay()
{
    if (listWin == nullptr) return;
    listWin->exitModalState (0);
    auto* c = listWin.release();
    c->setVisible (false);
    MessageManager::callAsync ([c] { delete c; });
    focusActive();
}

void Commander::driveOverview()
{
    if (listWin != nullptr) return;
    auto w = std::make_unique<DriveWindow>();
    Component::SafePointer<Commander> sp (this);
    w->onClose = [sp] { if (sp != nullptr) sp->closeOverlay(); };
    w->onPick = [sp] (const File& d) { if (sp == nullptr) return; sp->closeOverlay(); sp->act->setFolder (d); };
    w->getProperties().set ("kcW", 620);
    w->getProperties().set ("kcH", w->preferredHeight());
    listWin = std::move (w);
    addAndMakeVisible (*listWin);
    resized();
    listWin->enterModalState (true);
    listWin->grabKeyboardFocus();
}

void Commander::spaceMap()
{
    if (listWin != nullptr || act->isVirtual()) return;
    auto w = std::make_unique<SpaceWindow> (act->getFolder());
    Component::SafePointer<Commander> sp (this);
    w->onClose = [sp] { if (sp != nullptr) sp->closeOverlay(); };
    w->onOpen = [sp] (const File& d) { if (sp == nullptr) return; sp->closeOverlay(); sp->act->setFolder (d); };
    listWin = std::move (w);
    addAndMakeVisible (*listWin);
    resized();
    listWin->enterModalState (true);
    listWin->grabKeyboardFocus();
}

// ---------------------------------------------------------------- sieť

NetEngine& Commander::netEngine()
{
    if (netEng == nullptr)
    {
        netEng = std::make_unique<NetEngine> (settings);
        Component::SafePointer<Commander> sp (this);
        netEng->onChange = [sp]
        {
            if (sp == nullptr) return;
            for (auto* p : { &sp->left, &sp->right })
                if (p->isNet()) { p->fillNet(); p->repaint(); }
            if (sp->netDetail != nullptr) { sp->netDetail->setHost (sp->act->netHostIndex()); sp->netDetail->repaint(); }
            sp->repaint (0, sp->getHeight() - fbarH - msgH - cmdH, sp->getWidth(), cmdH);
        };
    }
    return *netEng;
}

void Commander::openNet()
{
    act->setNet();
    act->grabKeyboardFocus();
}

void Commander::updateNetDetail()
{
    const bool show = act->isNet() && ! isQuick();
    if (show && netDetail == nullptr)
    {
        netDetail = std::make_unique<NetDetail> (netEngine());
        netDetail->makeVideo = makeVideo;
        netDetail->playUrl = playUrl;
        netDetail->stopVideo = stopVideo;
        netDetail->videoAvailable = videoAvailable;
        Component::SafePointer<Commander> sp (this);
        netDetail->openFolder = [sp] (const File& f) { if (sp != nullptr) sp->act->setFolder (f); };
        netDetail->openFtp = [sp] (const String& ip)
        {
            if (sp == nullptr) return;
            FtpConn c;
            c.name = c.host = ip;
            c.user = "anonymous";
            sp->connectFtpTo (c, &sp->otherPanel(), {});
        };
        addChildComponent (*netDetail);
    }
    if (netDetail == nullptr) return;
    if (show)
    {
        otherPanel().setVisible (false);
        netDetail->setVisible (true);
        netDetail->setBounds (otherPanel().getBounds());
        netDetail->setHost (act->netHostIndex());
    }
    else if (netDetail->isVisible())
    {
        netDetail->stopCamera();
        netDetail->setVisible (false);
        if (! isQuick()) { left.setVisible (true); right.setVisible (true); }
    }
}

void Commander::renameNetHost()
{
    updateNetDetail();
    if (netDetail != nullptr && netDetail->getHost() >= 0) netDetail->renameHost();
}

void Commander::netRangeDialog()
{
    const String def = netEngine().range();
    Component::SafePointer<Commander> sp (this);
    dialog (tr ("Rozsah adries"), tr ("Napríklad 192.168.0.1-254 alebo viac rozsahov oddelených čiarkou"), &def,
            { tr ("Prehľadať"), tr ("Zrušiť") }, [sp] (int r, String t)
    {
        if (sp == nullptr || r != 1 || t.trim().isEmpty()) return;
        auto& e = sp->netEngine();
        e.stop();
        e.setRange (t);
        MessageManager::callAsync ([sp, t] { if (sp != nullptr) sp->netEngine().start (t); });
    });
}

void Commander::netMenu()
{
    updateNetDetail();
    const int hi = act->netHostIndex();
    auto& e = netEngine();
    PopupMenu m;
    if (hi >= 0 && hi < (int) e.hosts.size())
    {
        const auto& h = e.hosts[(size_t) hi];
        m.addSectionHeader (e.displayName (h).isNotEmpty() ? e.displayName (h) + "  (" + h.ipText + ")" : h.ipText);
        if (h.ports.contains (80)) m.addItem (1, tr ("Otvoriť web"));
        if (h.ports.contains (443)) m.addItem (2, tr ("Otvoriť web (HTTPS)"));
        if (h.ports.contains (8080)) m.addItem (3, tr ("Otvoriť web na porte 8080"));
        if (h.ports.contains (3389)) m.addItem (4, tr ("Vzdialená plocha"));
        if (h.ports.contains (21)) m.addItem (5, tr ("FTP v druhom paneli"));
        if (h.ports.contains (554)) m.addItem (6, tr ("Kopírovať adresu kamery (rtsp://…)"));
        if (h.ports.contains (445)) m.addItem (7, tr ("Otvoriť v Prieskumníkovi"));
        m.addItem (8, tr ("Kopírovať IP"));
        m.addItem (9, tr ("Kopírovať MAC"), h.mac.isNotEmpty());
        m.addItem (10, tr ("Pomenovať…") + "\tShift+F6");
        m.addSeparator();
    }
    m.addItem (20, tr ("Prehľadať znova") + "\tCtrl+R", ! e.isScanning());
    m.addItem (21, tr ("Zastaviť prehľadávanie"), e.isScanning());
    m.addItem (22, tr ("Zmeniť rozsah adries…"));
    Component::SafePointer<Commander> sp (this);
    m.showMenuAsync (PopupMenu::Options().withMousePosition(), [sp] (int r)
    {
        if (sp == nullptr || r == 0) return;
        if (r == 10) { sp->renameNetHost(); return; }
        if (r == 20) { sp->netEngine().start (sp->netEngine().range()); return; }
        if (r == 21) { sp->netEngine().stop(); return; }
        if (r == 22) { sp->netRangeDialog(); return; }
        if (sp->netDetail != nullptr) sp->netDetail->runAction (r);
    });
}
}
