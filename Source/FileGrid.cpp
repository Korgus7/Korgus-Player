#include "FileGrid.h"

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
    if (medium) c.format = r.removeFromRight (140);
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
    sortEntries();

    // režim podľa obsahu: zvuky = zoznam, obrázky a videá = mriežka
    if (! sameFolder)
    {
        int files = 0, sounds = 0, visuals = 0;
        for (auto& e : entries)
        {
            if (e.kind == Kind::Folder) continue;
            ++files;
            if (e.kind == Kind::Audio) ++sounds;
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

void FileGrid::setSort (SortBy s)
{
    if (sortBy == s) ascending = ! ascending;
    else { sortBy = s; ascending = s == SortBy::Name; }
    const auto keep = getSelectedFile();
    sortEntries();
    selected = -1;
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].file == keep) { selected = i; break; }
    ensureVisible (selected);
    content.repaint();
    header.repaint();
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
    if (mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown()) return false;

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
        g.drawText (String::fromUTF8 (o.folder == File() ? "Vyber priečinok vľavo" : "Priečinok je prázdny"),
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

    if (e.kind == Kind::Audio || e.kind == Kind::Video)
    {
        auto top = box.reduced (16, 14);
        g.setColour (c.withAlpha (0.9f));
        if (e.kind == Kind::Audio)
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

    g.setColour (sel ? (hasKeyboardFocus (true) ? col::accent : col::accent.withAlpha (0.55f)) : col::panel);
    g.fillRoundedRectangle (rf, radius);

    auto area = rf.reduced (6).withTrimmedBottom (26);
    auto thumb = thumbs.get (e.file);
    if (thumb.isValid())
    {
        g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
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

    if (sel)
    {
        g.setColour (hasKeyboardFocus (true) ? col::accent : col::accent.withAlpha (0.55f));
        g.fillRoundedRectangle (r.toFloat().reduced (3, 1), radius);
    }
    else if (index % 2 == 1)
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

    if (e.kind == Kind::Audio)
    {
        if (auto* info = audio.get (e.file))
        {
            if (info->ok)
            {
                g.drawText (timeText (info->length), c.length, Justification::centredRight, false);
                if (! c.format.isEmpty())
                {
                    const auto dot = String::fromUTF8 (" · ");
                    String f = String (info->sampleRate / 1000.0, 1) + " kHz";
                    if (info->bits > 0) f << dot << info->bits << " bit";
                    f << dot << (info->channels == 1 ? String ("mono") : info->channels == 2 ? String ("stereo") : String (info->channels) + " k");
                    g.drawText (f, c.format.withTrimmedLeft (10), Justification::centredLeft, true);
                }
                // malá vlna
                auto w = c.wave.reduced (0, 4).toFloat();
                const auto& pk = info->peaks;
                if (! pk.empty())
                {
                    g.setColour (sel ? Colours::white.withAlpha (0.85f) : col::gold.withAlpha (0.8f));
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
    const int i = owner.indexAt (e.getPosition());
    if (i >= 0) owner.selectIndex (i);
}

void FileGrid::Content::mouseDoubleClick (const MouseEvent& e)
{
    const int i = owner.indexAt (e.getPosition());
    if (i >= 0) owner.activate (i);
}
}
