#include "FileGrid.h"
#include "PhotoInfo.h"

namespace kp
{
static String timeText (double s)
{
    if (s < 10.0) return String (s, 2) + " s";
    const int t = (int) s;
    return String (t / 60) + ":" + String (t % 60).paddedLeft ('0', 2);
}

FileGrid::FileGrid (ThumbCache& c, AudioInfoCache& a) : thumbs (c), audio (a)
{
    setWantsKeyboardFocus (true);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    viewport.setWantsKeyboardFocus (false);
    addAndMakeVisible (viewport);
    addChildComponent (header);
}

void FileGrid::paint (Graphics& g) { g.fillAll (col::bg); }

void FileGrid::resized()
{
    auto r = getLocalBounds();
    header.setVisible (mode == Mode::List);
    if (mode == Mode::List) header.setBounds (r.removeFromTop (headerH));
    viewport.setBounds (r);
    updateSize();
}

void FileGrid::setMode (Mode m)
{
    if (m == mode) return;
    mode = m;
    resized();
    ensureVisible (selected);
    content.repaint();
    header.repaint();
    if (onModeChanged) onModeChanged();
}

// ---------------------------------------------------------------- rozloženie

int FileGrid::columns() const
{
    if (mode == Mode::List) return 1;
    const int w = viewport.getMaximumVisibleWidth() - gap;
    return jmax (1, w / (cellW + gap));
}

void FileGrid::updateSize()
{
    const int cols = columns();
    const int rows = ((int) entries.size() + cols - 1) / cols;
    const int h = mode == Mode::Grid ? rows * (cellH + gap) + gap : rows * rowH;
    content.setSize (viewport.getMaximumVisibleWidth(), jmax (viewport.getMaximumVisibleHeight(), h));
}

Rectangle<int> FileGrid::cellBounds (int index) const
{
    if (mode == Mode::List)
        return { 0, index * rowH, content.getWidth(), rowH };
    const int cols = columns();
    const int used = cols * (cellW + gap) - gap;
    const int x0 = jmax (gap, (content.getWidth() - used) / 2);
    return { x0 + (index % cols) * (cellW + gap), gap + (index / cols) * (cellH + gap), cellW, cellH };
}

int FileGrid::indexAt (Point<int> p) const
{
    if (entries.empty()) return -1;
    if (mode == Mode::List)
    {
        const int i = p.y / rowH;
        return isPositiveAndBelow (i, (int) entries.size()) ? i : -1;
    }
    const int cols = columns();
    const int row = (p.y - gap) / (cellH + gap);
    const int used = cols * (cellW + gap) - gap;
    const int x0 = jmax (gap, (content.getWidth() - used) / 2);
    const int c = (p.x - x0) / (cellW + gap);
    if (p.x < x0 || c >= cols || row < 0) return -1;
    const int i = row * cols + c;
    return isPositiveAndBelow (i, (int) entries.size()) && cellBounds (i).contains (p) ? i : -1;
}

FileGrid::Columns FileGrid::listColumns (Rectangle<int> row) const
{
    Columns c;
    auto r = row.reduced (8, 0);
    const bool roomy = r.getWidth() > 760, medium = r.getWidth() > 560;
    if (roomy) c.date = r.removeFromRight (124);
    c.size = r.removeFromRight (74);
    if (medium) c.format = r.removeFromRight (roomy ? 230 : 170);
    c.length = r.removeFromRight (62);
    c.wave = r.removeFromRight (jlimit (60, 160, r.getWidth() / 3));
    r.removeFromRight (8);
    c.name = r;
    return c;
}

// ---------------------------------------------------------------- obsah priečinka

void FileGrid::setFolder (const File& f, const File& toSelect)
{
    if (f != folder)
    {
        thumbs.cancelPending();
        audio.cancelPending();
    }
    const bool sameFolder = f == folder;
    folder = f;
    entries.clear();

    for (const auto& de : RangedDirectoryIterator (folder, false, "*", File::findFilesAndDirectories | File::ignoreHiddenFiles))
    {
        const auto& file = de.getFile();
        Entry e;
        e.file = file;
        e.kind = de.isDirectory() ? Kind::Folder : kindOf (file);
        e.name = file.getFileName();
        e.size = de.isDirectory() ? 0 : de.getFileSize();
        e.modified = de.getModificationTime();
        entries.push_back (std::move (e));
    }
    if (! sameFolder) filterText = {};
    allEntries = entries;
    entries.clear();
    for (auto& e : allEntries) if (passes (e)) entries.push_back (e);
    sortEntries();
    if (sortBy == SortBy::Taken) loadTakenDates();
    marked.assign (entries.size(), 0);
    anchor = -1;

    // režim podľa obsahu: zvuky = zoznam, obrázky a videá = mriežka
    if (! sameFolder)
    {
        int files = 0, sounds = 0, visuals = 0;
        for (auto& e : entries)
        {
            if (e.kind == Kind::Folder) continue;
            ++files;
            if (e.kind == Kind::Audio || e.kind == Kind::Midi) ++sounds;
            if (e.kind == Kind::Image || e.kind == Kind::Svg || e.kind == Kind::Video) ++visuals;
        }
        if (files > 0)
        {
            if (sounds * 2 > files) setMode (Mode::List);
            else if (visuals * 2 > files) setMode (Mode::Grid);
        }
    }

    selected = -1;
    updateSize();
    viewport.setViewPosition (0, 0);

    int idx = 0;
    if (toSelect != File())
        for (int i = 0; i < (int) entries.size(); ++i)
            if (entries[(size_t) i].file == toSelect) { idx = i; break; }

    if (! entries.empty()) selectIndex (idx);
    else if (onSelectionChanged) onSelectionChanged ({});
    content.repaint();
}

static String fold (const String& s)
{
    // bez diakritiky a veľkosti písmen
    static const char* from = "áäčďéěíĺľňóôöŕřšťúůüýžÁÄČĎÉĚÍĹĽŇÓÔÖŔŘŠŤÚŮÜÝŽ";
    static const char* to   = "aacdeeillnooorrstuuuyzaacdeeillnooorrstuuuyz";
    const auto src = String::fromUTF8 (from);
    String out;
    for (auto c : s.toLowerCase())
    {
        const int i = src.indexOfChar (c);
        out << (i >= 0 ? String::charToString ((juce_wchar) to[i]) : String::charToString (c));
    }
    return out;
}

bool FileGrid::passes (const Entry& e) const
{
    if (filterType != 0)
    {
        const auto k = e.kind;
        bool ok = false;
        switch (filterType)
        {
            case 1: ok = k == Kind::Image || k == Kind::Svg; break;
            case 2: ok = k == Kind::Audio || k == Kind::Midi; break;
            case 3: ok = k == Kind::Video; break;
            case 4: ok = k == Kind::Text || k == Kind::Table || k == Kind::Slides || k == Kind::Pdf; break;
            case 5: ok = k == Kind::Model || k == Kind::Cad; break;
            case 6: ok = k == Kind::Folder; break;
            default: ok = true;
        }
        if (! ok) return false;
    }
    if (filterText.isEmpty()) return true;
    const auto name = fold (e.name);
    for (auto& word : StringArray::fromTokens (fold (filterText), " ", ""))
        if (word.isNotEmpty() && ! name.contains (word)) return false;
    return true;
}

void FileGrid::applyFilter()
{
    const auto keep = getSelectedFile();
    entries.clear();
    for (auto& e : allEntries) if (passes (e)) entries.push_back (e);
    sortEntries();
    marked.assign (entries.size(), 0);
    anchor = -1;
    selected = -1;
    updateSize();
    viewport.setViewPosition (0, 0);
    int idx = 0;
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].file == keep) { idx = i; break; }
    if (! entries.empty()) selectIndex (idx);
    else if (onSelectionChanged) onSelectionChanged ({});
    content.repaint();
    marksChanged();
}

void FileGrid::setFilter (const String& text, int type)
{
    if (text == filterText && type == filterType) return;
    filterText = text;
    filterType = type;
    applyFilter();
}

void FileGrid::sortEntries()
{
    const auto by = sortBy;
    const bool asc = ascending;
    auto& au = audio;
    std::stable_sort (entries.begin(), entries.end(), [by, asc, &au] (const Entry& a, const Entry& b)
    {
        const bool da = a.kind == Kind::Folder, db = b.kind == Kind::Folder;
        if (da != db) return da;  // priečinky vždy hore
        int c = 0;
        if (by == SortBy::Size) c = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
        else if (by == SortBy::Date) c = a.modified < b.modified ? -1 : (a.modified > b.modified ? 1 : 0);
        else if (by == SortBy::Taken)
        {
            const auto ta = a.takenKnown ? a.taken : a.modified, tb = b.takenKnown ? b.taken : b.modified;
            c = ta < tb ? -1 : (ta > tb ? 1 : 0);
        }
        else if (by == SortBy::Type)
        {
            c = a.file.getFileExtension().compareIgnoreCase (b.file.getFileExtension());
        }
        else if (by == SortBy::Length)
        {
            auto* ia = au.get (a.file);
            auto* ib = au.get (b.file);
            const double la = ia != nullptr ? ia->length : 1e12, lb = ib != nullptr ? ib->length : 1e12;
            c = la < lb ? -1 : (la > lb ? 1 : 0);
        }
        if (c == 0) c = a.name.compareNatural (b.name);
        return asc ? c < 0 : c > 0;
    });
}

void FileGrid::setSortExplicit (SortBy s, bool asc)
{
    sortBy = s;
    ascending = asc;
    const auto keep = getSelectedFile();
    sortEntries();
    marked.assign (entries.size(), 0);
    selected = -1;
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].file == keep) { selected = i; break; }
    ensureVisible (selected);
    content.repaint();
    header.repaint();
    if (s == SortBy::Taken) loadTakenDates();
}

void FileGrid::loadTakenDates()
{
    // dátumy fotenia z EXIF na pozadí, potom znova zoradiť
    std::vector<File> files;
    for (auto& e : allEntries) if (e.kind == Kind::Image && ! e.takenKnown) files.push_back (e.file);
    if (files.empty()) return;
    const int gen = ++takenGeneration;
    auto aliveFlag = alive;
    takenPool.removeAllJobs (false, 0);
    takenPool.addJob ([this, aliveFlag, gen, files]
    {
        auto results = std::make_shared<std::vector<std::pair<File, Time>>>();
        for (auto& f : files)
        {
            if (gen != takenGeneration) return;
            auto pi = readPhotoInfo (f);
            if (pi.hasTaken) results->push_back ({ f, pi.taken });
        }
        MessageManager::callAsync ([this, aliveFlag, gen, results]
        {
            if (! *aliveFlag || gen != takenGeneration) return;
            for (auto& [f, t] : *results)
            {
                for (auto& e : allEntries) if (e.file == f) { e.taken = t; e.takenKnown = true; }
                for (auto& e : entries) if (e.file == f) { e.taken = t; e.takenKnown = true; }
            }
            if (sortBy == SortBy::Taken) setSortExplicit (sortBy, ascending);
        });
    });
}

void FileGrid::setSort (SortBy s)
{
    if (sortBy == s) ascending = ! ascending;
    else { sortBy = s; ascending = s == SortBy::Name; }
    const auto keep = getSelectedFile();
    sortEntries();
    marked.assign (entries.size(), 0);
    marksChanged();
    selected = -1;
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].file == keep) { selected = i; break; }
    ensureVisible (selected);
    content.repaint();
    header.repaint();
    if (onSortChanged) onSortChanged();
}

void FileGrid::reload()
{
    const auto keep = getSelectedFile();
    const auto pos = viewport.getViewPosition();
    const int oldIndex = selected;
    setFolder (folder, keep);
    if (getSelectedFile() != keep && oldIndex >= 0 && ! entries.empty())
        selectIndex (jmin (oldIndex, (int) entries.size() - 1));
    viewport.setViewPosition (pos);
    ensureVisible (selected);
}

File FileGrid::getSelectedFile() const { return getFileAt (selected); }

File FileGrid::getFileAt (int index) const
{
    return isPositiveAndBelow (index, (int) entries.size()) ? entries[(size_t) index].file : File();
}

Kind FileGrid::getKindAt (int index) const
{
    return isPositiveAndBelow (index, (int) entries.size()) ? entries[(size_t) index].kind : Kind::Other;
}

void FileGrid::selectIndex (int index)
{
    if (entries.empty()) return;
    index = jlimit (0, (int) entries.size() - 1, index);
    if (index == selected) return;
    selected = index;
    ensureVisible (index);
    content.repaint();
    if (onSelectionChanged) onSelectionChanged (entries[(size_t) index].file);
}

void FileGrid::selectFile (const File& f)
{
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].file == f) { selectIndex (i); return; }
}

void FileGrid::removeFile (const File& f)
{
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].file == f)
        {
            entries.erase (entries.begin() + i);
            for (size_t k = 0; k < allEntries.size(); ++k)
                if (allEntries[k].file == f) { allEntries.erase (allEntries.begin() + (long) k); break; }
            if (i < (int) marked.size()) marked.erase (marked.begin() + i);
            if (anchor >= i) anchor = jmax (-1, anchor - 1);
            updateSize();
            const int old = selected;
            selected = -1;
            if (entries.empty())
            {
                content.repaint();
                if (onSelectionChanged) onSelectionChanged ({});
            }
            else
                selectIndex (jmin (old > i ? old - 1 : old, (int) entries.size() - 1));
            content.repaint();
            return;
        }
}

Array<File> FileGrid::getMarkedFiles() const
{
    Array<File> r;
    for (size_t i = 0; i < marked.size() && i < entries.size(); ++i)
        if (marked[i]) r.add (entries[i].file);
    if (r.isEmpty() && getSelectedFile() != File()) r.add (getSelectedFile());
    return r;
}

int FileGrid::getNumMarked() const
{
    int n = 0;
    for (auto m : marked) n += m ? 1 : 0;
    return n;
}

void FileGrid::marksChanged()
{
    content.repaint();
    if (onMarksChanged) onMarksChanged();
}

void FileGrid::clearMarks()
{
    if (getNumMarked() == 0) return;
    std::fill (marked.begin(), marked.end(), 0);
    marksChanged();
}

void FileGrid::markAll()
{
    std::fill (marked.begin(), marked.end(), 1);
    marksChanged();
}

void FileGrid::markRange (int a, int b)
{
    std::fill (marked.begin(), marked.end(), 0);
    if (a < 0) a = b;
    for (int i = jmin (a, b); i <= jmax (a, b); ++i)
        if (isPositiveAndBelow (i, (int) marked.size())) marked[(size_t) i] = 1;
    marksChanged();
}

void FileGrid::ensureVisible (int index)
{
    if (index < 0) return;
    const auto r = mode == Mode::Grid ? cellBounds (index).expanded (0, gap) : cellBounds (index);
    const auto view = viewport.getViewArea();
    if (r.getY() < view.getY()) viewport.setViewPosition (0, jmax (0, r.getY()));
    else if (r.getBottom() > view.getBottom()) viewport.setViewPosition (0, r.getBottom() - view.getHeight());
}

void FileGrid::activate (int index)
{
    const auto f = getFileAt (index);
    if (f == File()) return;
    if (f.isDirectory()) { if (onOpenFolder) onOpenFolder (f); }
    else if (onActivateFile) onActivateFile (f);
}

bool FileGrid::keyPressed (const KeyPress& key)
{
    if (entries.empty())
    {
        if (key == KeyPress::backspaceKey && onGoUp) { onGoUp(); return true; }
        return false;
    }

    const int cols = columns();
    const int perPage = jmax (1, viewport.getViewHeight() / itemHeight()) * cols;
    const int k = key.getKeyCode();
    const auto mods = key.getModifiers();
    if (mods.isCtrlDown() && (k == 'a' || k == 'A')) { markAll(); return true; }
    if (k == KeyPress::escapeKey && getNumMarked() > 0) { clearMarks(); return true; }
    if (mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown()) return false;
    const bool extend = mods.isShiftDown();

    int target = selected;
    if      (k == KeyPress::leftKey)     target = mode == Mode::Grid ? selected - 1 : selected;
    else if (k == KeyPress::rightKey)    target = mode == Mode::Grid ? selected + 1 : selected;
    else if (k == KeyPress::upKey)       target = selected - cols;
    else if (k == KeyPress::downKey)     target = selected + cols;
    else if (k == KeyPress::pageUpKey)   target = selected - perPage;
    else if (k == KeyPress::pageDownKey) target = selected + perPage;
    else if (k == KeyPress::homeKey)     target = 0;
    else if (k == KeyPress::endKey)      target = (int) entries.size() - 1;
    else if (k == KeyPress::returnKey)   { activate (selected); return true; }
    else if (k == KeyPress::backspaceKey){ if (onGoUp) onGoUp(); return true; }
    else return false;

    if (k == KeyPress::upKey && selected - cols < 0) target = selected;      // hore na prvom riadku ostaň
    if (k == KeyPress::downKey && target >= (int) entries.size())
        target = (selected / cols + 1) * cols < (int) entries.size() ? (int) entries.size() - 1 : selected;
    target = jlimit (0, (int) entries.size() - 1, target);
    if (extend)
    {
        if (anchor < 0) anchor = selected;
        markRange (anchor, target);
    }
    else
    {
        anchor = target;
        clearMarks();
    }
    selectIndex (target);
    return true;
}

// ---------------------------------------------------------------- kreslenie

void FileGrid::Content::paint (Graphics& g)
{
    g.fillAll (col::bg);
    auto& o = owner;
    if (o.entries.empty())
    {
        g.setColour (col::textDim);
        g.setFont (FontOptions (15.0f));
        g.drawText (String::fromUTF8 (o.folder == File() ? "Vyber priečinok vľavo" : (o.isFiltered() ? "Nič nevyhovuje hľadaniu" : "Priečinok je prázdny")),
                    getLocalBounds(), Justification::centred);
        return;
    }
    const auto clip = g.getClipBounds();
    if (o.mode == Mode::List)
    {
        const int first = jmax (0, clip.getY() / rowH);
        const int last = jmin ((int) o.entries.size() - 1, clip.getBottom() / rowH);
        for (int i = first; i <= last; ++i)
            o.paintRow (g, i, o.cellBounds (i));
        return;
    }
    const int cols = o.columns();
    const int firstRow = jmax (0, (clip.getY() - gap) / (cellH + gap));
    const int lastRow = (clip.getBottom() - gap) / (cellH + gap);
    for (int row = firstRow; row <= lastRow; ++row)
        for (int c = 0; c < cols; ++c)
        {
            const int i = row * cols + c;
            if (i >= (int) o.entries.size()) return;
            o.paintCell (g, i, o.cellBounds (i));
        }
}

void FileGrid::paintIcon (Graphics& g, const Entry& e, Rectangle<float> area, bool small)
{
    const auto c = kindColour (e.kind);
    if (e.kind == Kind::Folder)
    {
        auto f = small ? area.withSizeKeepingCentre (15, 12) : area.withSizeKeepingCentre (84, 62);
        const float tab = small ? 3.0f : 8.0f;
        g.setColour (c);
        g.fillRoundedRectangle (f.withTrimmedTop (tab), small ? 2.0f : radius);
        g.fillRoundedRectangle (f.withWidth (f.getWidth() * 0.42f).withHeight (tab + (small ? 2.0f : 6.0f)), small ? 1.5f : radius);
        return;
    }
    if (small)
    {
        g.setColour (c);
        g.fillRoundedRectangle (area.withSizeKeepingCentre (11, 14), 2.0f);
        return;
    }

    auto box = area.withSizeKeepingCentre (70, 86);
    g.setColour (col::panelHi);
    g.fillRoundedRectangle (box, radius);
    auto label = box.removeFromBottom (26);
    g.setColour (c);
    g.fillRoundedRectangle (label, radius);
    const auto ext = e.file.getFileExtension().substring (1).toUpperCase();
    g.setColour (Colours::white);
    g.setFont (FontOptions (12.5f, Font::bold));
    g.drawText (ext.isEmpty() ? String ("?") : ext.substring (0, 5), label, Justification::centred);

    if (e.kind == Kind::Audio || e.kind == Kind::Midi || e.kind == Kind::Video)
    {
        auto top = box.reduced (16, 14);
        g.setColour (c.withAlpha (0.9f));
        if (e.kind != Kind::Video)
        {
            const float bars[] = { 0.35f, 0.7f, 1.0f, 0.55f, 0.8f, 0.4f };
            const float bw = top.getWidth() / 11.0f;
            for (int i = 0; i < 6; ++i)
            {
                const float bh = top.getHeight() * bars[i];
                g.fillRoundedRectangle (top.getX() + (float) i * 2 * bw, top.getCentreY() - bh / 2, bw, bh, 1.0f);
            }
        }
        else
        {
            Path p;
            auto t = top.withSizeKeepingCentre (22, 24);
            p.addTriangle (t.getX(), t.getY(), t.getX(), t.getBottom(), t.getRight(), t.getCentreY());
            g.fillPath (p);
        }
    }
}

void FileGrid::paintCell (Graphics& g, int index, Rectangle<int> r)
{
    const auto& e = entries[(size_t) index];
    const bool sel = index == selected;
    auto rf = r.toFloat();

    const bool mk = isPositiveAndBelow (index, (int) marked.size()) && marked[(size_t) index];
    g.setColour (sel ? (hasKeyboardFocus (true) ? col::accent : col::accent.withAlpha (0.55f))
                     : (mk ? col::accent.withAlpha (0.6f) : col::panel));
    g.fillRoundedRectangle (rf, radius);
    if (mk)
    {
        g.setColour (col::gold);
        g.drawRoundedRectangle (rf.reduced (1.0f), radius, 1.5f);
    }

    auto area = rf.reduced (6).withTrimmedBottom (26);
    auto thumb = thumbs.get (e.file);
    if (thumb.isValid())
    {
        g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
        if (thumb.hasAlphaChannel())
            drawChecker (g, RectanglePlacement (RectanglePlacement::centred | RectanglePlacement::onlyReduceInSize)
                                .appliedTo (thumb.getBounds().toFloat(), area), 6.0f);
        g.drawImageWithin (thumb, (int) area.getX(), (int) area.getY(), (int) area.getWidth(), (int) area.getHeight(),
                           RectanglePlacement::centred | RectanglePlacement::onlyReduceInSize);
    }
    else
        paintIcon (g, e, area, false);

    g.setColour (sel ? Colours::white : col::text);
    g.setFont (FontOptions (12.5f));
    g.drawFittedText (e.name, r.reduced (6, 0).removeFromBottom (30).withTrimmedBottom (4), Justification::centred, 2, 1.0f);
}

void FileGrid::paintRow (Graphics& g, int index, Rectangle<int> r)
{
    const auto& e = entries[(size_t) index];
    const bool sel = index == selected;

    const bool mk = isPositiveAndBelow (index, (int) marked.size()) && marked[(size_t) index];
    if (sel)
    {
        g.setColour (hasKeyboardFocus (true) ? col::accent : col::accent.withAlpha (0.55f));
        g.fillRoundedRectangle (r.toFloat().reduced (3, 1), radius);
    }
    else if (mk)
    {
        g.setColour (col::accent.withAlpha (0.6f));
        g.fillRoundedRectangle (r.toFloat().reduced (3, 1), radius);
    }
    if (mk)
    {
        g.setColour (col::gold);
        g.fillRect (r.getX() + 3, r.getY() + 3, 3, r.getHeight() - 6);
    }
    if (! sel && ! mk && index % 2 == 1)
    {
        g.setColour (col::panel.withAlpha (0.55f));
        g.fillRect (r);
    }

    const auto c = listColumns (r);
    const auto textCol = sel ? Colours::white : col::text;
    const auto dimCol = sel ? Colours::white.withAlpha (0.8f) : col::textDim;

    auto nameArea = c.name;
    paintIcon (g, e, nameArea.removeFromLeft (20).toFloat(), true);
    nameArea.removeFromLeft (6);
    g.setColour (textCol);
    g.setFont (FontOptions (13.0f));
    g.drawText (e.name, nameArea, Justification::centredLeft, true);

    g.setFont (FontOptions (12.0f));
    g.setColour (dimCol);
    if (e.kind != Kind::Folder)
    {
        g.drawText (sizeText (e.size), c.size, Justification::centredRight, false);
    }
    if (! c.date.isEmpty())
        g.drawText (e.modified.formatted ("%d.%m.%Y %H:%M"), c.date, Justification::centredRight, false);

    if (e.kind == Kind::Audio || e.kind == Kind::Midi)
    {
        if (auto* info = audio.get (e.file))
        {
            if (info->ok)
            {
                g.drawText (timeText (info->length), c.length, Justification::centredRight, false);
                if (! c.format.isEmpty())
                    g.drawText (info->format, c.format.withTrimmedLeft (10), Justification::centredLeft, true);
                // malá vlna (pri MIDI hustota nôt)
                auto w = c.wave.reduced (0, 4).toFloat();
                const auto& pk = info->peaks;
                if (! pk.empty())
                {
                    const auto base = e.kind == Kind::Midi ? kindColour (Kind::Midi) : col::gold;
                    g.setColour (sel ? Colours::white.withAlpha (0.85f) : base.withAlpha (0.8f));
                    const float bw = w.getWidth() / (float) pk.size();
                    for (size_t i = 0; i < pk.size(); ++i)
                    {
                        const float h = jmax (1.0f, pk[i] * w.getHeight());
                        g.fillRect (w.getX() + (float) i * bw, w.getCentreY() - h / 2, jmax (1.0f, bw - 0.4f), h);
                    }
                }
            }
            else
                g.drawText (String::fromUTF8 ("—"), c.length, Justification::centredRight, false);
        }
    }
    else if (e.kind != Kind::Folder && ! c.format.isEmpty())
    {
        g.drawText (e.file.getFileExtension().substring (1).toUpperCase(), c.format.withTrimmedLeft (10), Justification::centredLeft, true);
    }
}

void FileGrid::Header::paint (Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::line);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
    const auto c = owner.listColumns (getLocalBounds().withWidth (owner.viewport.getMaximumVisibleWidth()));

    auto title = [&] (Rectangle<int> r, const char* text, SortBy s, Justification j)
    {
        if (r.isEmpty()) return;
        String t = String::fromUTF8 (text);
        if (owner.sortBy == s) t = j == Justification::centredRight ? String::fromUTF8 (owner.ascending ? "▲ " : "▼ ") + t
                                                                        : t + String::fromUTF8 (owner.ascending ? " ▲" : " ▼");
        g.setColour (owner.sortBy == s ? col::text : col::textDim);
        g.drawText (t, r, j, false);
    };
    g.setFont (FontOptions (12.0f, Font::bold));
    title (c.name.withTrimmedLeft (26), "Názov", SortBy::Name, Justification::centredLeft);
    g.setColour (col::textDim);
    g.drawText (String::fromUTF8 ("Vlna"), c.wave, Justification::centredLeft, false);
    title (c.length, "Dĺžka", SortBy::Length, Justification::centredRight);
    if (! c.format.isEmpty())
    {
        g.setColour (col::textDim);
        g.drawText (String::fromUTF8 ("Formát"), c.format.withTrimmedLeft (10), Justification::centredLeft, false);
    }
    title (c.size, "Veľkosť", SortBy::Size, Justification::centredRight);
    title (c.date, "Zmenené", SortBy::Date, Justification::centredRight);
}

void FileGrid::Header::mouseDown (const MouseEvent& e)
{
    const auto c = owner.listColumns (getLocalBounds().withWidth (owner.viewport.getMaximumVisibleWidth()));
    const auto p = e.getPosition();
    if (c.name.contains (p)) owner.setSort (SortBy::Name);
    else if (c.length.contains (p)) owner.setSort (SortBy::Length);
    else if (c.size.contains (p)) owner.setSort (SortBy::Size);
    else if (c.date.contains (p)) owner.setSort (SortBy::Date);
    owner.grabKeyboardFocus();
}

void FileGrid::Content::mouseDown (const MouseEvent& e)
{
    owner.grabKeyboardFocus();
    dragging = false;
    clearOnUp = false;
    const int i = owner.indexAt (e.getPosition());
    if (i < 0) { owner.clearMarks(); return; }

    if (e.mods.isPopupMenu())
    {
        // pravé tlačidlo: na označenom ostane označenie, inak vybrať tento súbor
        if (! owner.marked[(size_t) i]) { owner.clearMarks(); owner.anchor = i; }
        owner.selectIndex (i);
        if (owner.onContextMenu) owner.onContextMenu (e.getScreenPosition());
        return;
    }

    if (e.mods.isCtrlDown())
    {
        // Ctrl+klik: pridať / odobrať (prvé Ctrl+klik pridá aj doterajší súbor)
        if (owner.getNumMarked() == 0 && isPositiveAndBelow (owner.selected, (int) owner.marked.size()))
            owner.marked[(size_t) owner.selected] = 1;
        owner.marked[(size_t) i] = owner.marked[(size_t) i] ? 0 : 1;
        owner.anchor = i;
        owner.marksChanged();
        owner.selectIndex (i);
        return;
    }
    if (e.mods.isShiftDown())
    {
        if (owner.anchor < 0) owner.anchor = owner.selected;
        owner.markRange (owner.anchor, i);
        owner.selectIndex (i);
        return;
    }
    // obyčajný klik na označený súbor: označenie ostane kvôli ťahaniu, zruší sa až pri pustení
    if (owner.marked[(size_t) i] && owner.getNumMarked() > 1) clearOnUp = true;
    else owner.clearMarks();
    owner.anchor = i;
    owner.selectIndex (i);
}

void FileGrid::Content::mouseDrag (const MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 8 || e.mods.isPopupMenu()) return;
    if (owner.indexAt (e.getMouseDownPosition()) < 0) return;
    dragging = true;
    clearOnUp = false;

    // ťahanie súborov do iných programov (DAW, Prieskumník) – kopíruje sa
    StringArray paths;
    for (auto& f : owner.getMarkedFiles()) paths.add (f.getFullPathName());
    if (! paths.isEmpty())
        DragAndDropContainer::performExternalDragDropOfFiles (paths, false, this);
}

void FileGrid::Content::mouseUp (const MouseEvent&)
{
    if (clearOnUp && ! dragging) owner.clearMarks();
    clearOnUp = false;
    dragging = false;
}

void FileGrid::Content::mouseDoubleClick (const MouseEvent& e)
{
    const int i = owner.indexAt (e.getPosition());
    if (i >= 0) owner.activate (i);
}
}
