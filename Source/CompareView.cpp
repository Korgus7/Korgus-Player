#include "CompareView.h"
#include "ImageLoader.h"

namespace kp
{
CompareView::CompareView (const File& l, const File& r, Array<File> imgs) : images (std::move (imgs))
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    load (a, l);
    load (b, r);
}

void CompareView::load (Side& s, const File& f)
{
    s.file = f;
    auto ld = img::loadFull (f, 3000);
    s.image = ld.image;
    s.w = ld.origW > 0 ? ld.origW : ld.image.getWidth();
    s.h = ld.origH > 0 ? ld.origH : ld.image.getHeight();
    repaint();
}

void CompareView::paintSide (Graphics& g, const Side& s, Rectangle<float> area)
{
    g.saveState();
    g.reduceClipRegion (area.toNearestInt());
    if (s.image.isValid())
    {
        auto fit = RectanglePlacement (RectanglePlacement::centred | RectanglePlacement::onlyReduceInSize)
                       .appliedTo (s.image.getBounds().toFloat(), area.reduced (6));
        // spoločné priblíženie okolo stredu každej polovice
        const auto c = area.getCentre();
        fit = fit.withSizeKeepingCentre (fit.getWidth() * zoom, fit.getHeight() * zoom).translated (pan.x * zoom, pan.y * zoom);
        ignoreUnused (c);
        g.setImageResamplingQuality (zoom > 2.5f ? Graphics::lowResamplingQuality : Graphics::highResamplingQuality);
        g.drawImage (s.image, fit);
    }
    else
    {
        g.setColour (Colours::grey);
        g.drawText (tr ("Obrázok sa nedá načítať"), area, Justification::centred);
    }
    g.restoreState();
    // popis
    auto label = area.removeFromBottom (28).reduced (8, 4);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (label.toFloat(), 4.0f);
    g.setColour (Colours::white);
    g.setFont (FontOptions (13.0f));
    g.drawText (s.file.getFileName() + "   " + String (s.w) + " × " + String (s.h) + "   " + File::descriptionOfSizeInBytes (s.file.getSize()),
                label.reduced (8, 0), Justification::centredLeft, true);
}

void CompareView::paint (Graphics& g)
{
    g.fillAll (Colour (0xff0b0d10));
    auto r = getLocalBounds().toFloat();
    auto top = r.removeFromTop (34);
    g.setColour (Colours::white.withAlpha (0.75f));
    g.setFont (FontOptions (13.0f));
    g.drawText (tr ("Porovnanie · koliesko priblíži obe · ťahanie posúva · ← → iná pravá fotka · 1 = nechať ľavú, 2 = nechať pravú (druhá sa označí) · Esc koniec"),
                top.reduced (12, 0), Justification::centred, true);
    auto left = r.removeFromLeft (r.getWidth() / 2);
    paintSide (g, a, left);
    paintSide (g, b, r);
    g.setColour (Colours::white.withAlpha (0.2f));
    g.fillRect (left.getRight() - 1.0f, left.getY(), 2.0f, left.getHeight());
}

bool CompareView::keyPressed (const KeyPress& k)
{
    const int c = k.getKeyCode();
    if (c == KeyPress::escapeKey || c == KeyPress::returnKey) { if (onClose) onClose(); return true; }
    if (c == KeyPress::rightKey || c == KeyPress::leftKey)
    {
        if (images.size() < 2) return true;
        int i = images.indexOf (b.file);
        for (int n = 0; n < images.size(); ++n)
        {
            i = (i + (c == KeyPress::rightKey ? 1 : -1) + images.size()) % images.size();
            if (images[i] != a.file) break;
        }
        load (b, images[i]);
        return true;
    }
    if (c == '1' || c == '2')
    {
        if (onPick) onPick (c == '1' ? a.file : b.file, c == '1' ? b.file : a.file);
        return true;
    }
    if (c == '0') { zoom = 1.0f; pan = {}; repaint(); return true; }
    return false;
}

void CompareView::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    zoom = jlimit (1.0f, 16.0f, zoom * (w.deltaY > 0 ? 1.2f : 1.0f / 1.2f));
    if (zoom <= 1.0f) pan = {};
    repaint();
}

void CompareView::mouseDown (const MouseEvent&) { dragPan = pan; }

void CompareView::mouseDrag (const MouseEvent& e)
{
    pan = dragPan + e.getOffsetFromDragStart().toFloat() / zoom;
    repaint();
}
}
