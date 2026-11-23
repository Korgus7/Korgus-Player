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
            for (size_t i = 0; i < tmp.size(); ++i)
            {
                if (! tmp[i].second.exists() && tmp[i].first.moveFileTo (tmp[i].second)) ++ok;
                else
                {
                    ++skipped;   // cieľ už existuje → späť na pôvodný názov
                    tmp[i].first.moveFileTo (tmp[i].first.getSiblingFile (tmp[i].first.getFileName().upToLastOccurrenceOf (".kc-tmp", false, false)));
                }
            }
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
            { String::fromUTF8 ("\xe2\x86\x92 ") + tr ("Ľavý do pravého"), String::fromUTF8 ("\xe2\x86\x90 ") + tr ("Pravý do ľavého"),
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
