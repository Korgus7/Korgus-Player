#include "FileGrid.h"

namespace kp
{
FileGrid::FileGrid (ThumbCache& c) : thumbs (c)
{
    setWantsKeyboardFocus (true);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    viewport.setWantsKeyboardFocus (false);
    addAndMakeVisible (viewport);
}

void FileGrid::paint (Graphics& g) { g.fillAll (col::bg); }

void FileGrid::resized()
{
    viewport.setBounds (getLocalBounds());
    updateSize();
}

int FileGrid::columns() const
{
    const int w = viewport.getMaximumVisibleWidth() - gap;
    return jmax (1, w / (cellW + gap));
}

void FileGrid::updateSize()
{
    const int cols = columns();
    const int rows = ((int) entries.size() + cols - 1) / cols;
    content.setSize (viewport.getMaximumVisibleWidth(), jmax (viewport.getMaximumVisibleHeight(), rows * (cellH + gap) + gap));
}

Rectangle<int> FileGrid::cellBounds (int index) const
{
    const int cols = columns();
    const int used = cols * (cellW + gap) - gap;
    const int x0 = jmax (gap, (content.getWidth() - used) / 2);
    return { x0 + (index % cols) * (cellW + gap), gap + (index / cols) * (cellH + gap), cellW, cellH };
}

int FileGrid::indexAt (Point<int> p) const
{
    for (int i = 0; i < (int) entries.size(); ++i)
        if (cellBounds (i).contains (p)) return i;
    return -1;
}

void FileGrid::setFolder (const File& f, const File& toSelect)
{
    if (f != folder) thumbs.cancelPending();
    folder = f;
    entries.clear();

    auto files = folder.findChildFiles (File::findFilesAndDirectories | File::ignoreHiddenFiles, false);
    entries.reserve ((size_t) files.size());
    for (auto& file : files)
        entries.push_back ({ file, kindOf (file), file.getFileName() });

    std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b)
    {
        const bool da = a.kind == Kind::Folder, db = b.kind == Kind::Folder;
        if (da != db) return da;
        return a.name.compareNatural (b.name) < 0;
    });

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
    const auto r = cellBounds (index).expanded (0, gap);
    const auto view = viewport.getViewArea();
    if (r.getY() < view.getY()) viewport.setViewPosition (0, jmax (0, r.getY()));
    else if (r.getBottom() > view.getBottom()) viewport.setViewPosition (0, r.getBottom() - view.getHeight());
}

void FileGrid::activate (int index)
{
    const auto f = getFileAt (index);
    if (f == File()) return;
    if (f.isDirectory()) { if (onOpenFolder) onOpenFolder (f); }
    else if (onOpenExternally) onOpenExternally (f);
}

bool FileGrid::keyPressed (const KeyPress& key)
{
    if (entries.empty())
    {
        if (key == KeyPress::backspaceKey && onGoUp) { onGoUp(); return true; }
        return false;
    }

    const int cols = columns();
    const int perPage = jmax (1, viewport.getViewHeight() / (cellH + gap)) * cols;
    const int k = key.getKeyCode();
    const auto mods = key.getModifiers();
    if (mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown()) return false;

    int target = selected;
    if      (k == KeyPress::leftKey)     target = selected - 1;
    else if (k == KeyPress::rightKey)    target = selected + 1;
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

void FileGrid::paintCell (Graphics& g, int index, Rectangle<int> r)
{
    const auto& e = entries[(size_t) index];
    const bool sel = index == selected;
    auto rf = r.toFloat();

    if (sel)
    {
        g.setColour (hasKeyboardFocus (true) ? col::accent : col::accent.withAlpha (0.55f));
        g.fillRoundedRectangle (rf, radius);
    }
    else
    {
        g.setColour (col::panel);
        g.fillRoundedRectangle (rf, radius);
    }

    auto area = rf.reduced (6).withTrimmedBottom (26);
    auto thumb = thumbs.get (e.file);
    if (thumb.isValid())
    {
        g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
        g.drawImageWithin (thumb, (int) area.getX(), (int) area.getY(), (int) area.getWidth(), (int) area.getHeight(),
                           RectanglePlacement::centred | RectanglePlacement::onlyReduceInSize);
    }
    else
    {
        // štítok s príponou alebo symbol priečinka
        const auto c = kindColour (e.kind);
        if (e.kind == Kind::Folder)
        {
            auto f = area.withSizeKeepingCentre (84, 62);
            g.setColour (c);
            g.fillRoundedRectangle (f.withTrimmedTop (8), radius);
            g.fillRoundedRectangle (f.withWidth (34).withHeight (14), radius);
        }
        else
        {
            auto box = area.withSizeKeepingCentre (70, 86);
            g.setColour (col::panelHi);
            g.fillRoundedRectangle (box, radius);
            g.setColour (c);
            g.fillRoundedRectangle (box.removeFromBottom (26), radius);
            auto ext = e.file.getFileExtension().substring (1).toUpperCase();
            g.setColour (Colours::white);
            g.setFont (FontOptions (12.5f, Font::bold));
            g.drawText (ext.isEmpty() ? String ("?") : ext.substring (0, 5),
                        area.withSizeKeepingCentre (70, 86).removeFromBottom (26), Justification::centred);
            if (e.kind == Kind::Audio || e.kind == Kind::Video)
            {
                // malý symbol vlny / prehrávania
                auto top = area.withSizeKeepingCentre (70, 86).withTrimmedBottom (26).reduced (16, 14);
                g.setColour (c.withAlpha (0.9f));
                if (e.kind == Kind::Audio)
                {
                    const float bars[] = { 0.35f, 0.7f, 1.0f, 0.55f, 0.8f, 0.4f };
                    const float bw = top.getWidth() / 11.0f;
                    for (int i = 0; i < 6; ++i)
                    {
                        const float bh = top.getHeight() * bars[i];
                        g.fillRoundedRectangle (top.getX() + i * 2 * bw, top.getCentreY() - bh / 2, bw, bh, 1.0f);
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
    }

    g.setColour (sel ? Colours::white : col::text);
    g.setFont (FontOptions (12.5f));
    g.drawFittedText (e.name, r.reduced (6, 0).removeFromBottom (30).withTrimmedBottom (4), Justification::centred, 2, 1.0f);
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
