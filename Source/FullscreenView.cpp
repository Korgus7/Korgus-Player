#include "FullscreenView.h"

namespace kp
{
FullscreenView::FullscreenView (ImageCache& ic, ThumbCache& tc) : images (ic), thumbs (tc)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
}

FullscreenView::~FullscreenView()
{
    *alive = false;
    fullLoader.removeAllJobs (true, 3000);
}

Point<float> FullscreenView::originalSize() const
{
    if (shown == nullptr || ! shown->display.isValid()) return { 1.0f, 1.0f };
    const bool svg = file.hasFileExtension ("svg");
    if (svg || shown->loaded.origW <= 0) return { (float) shown->display.getWidth(), (float) shown->display.getHeight() };
    return { (float) shown->loaded.origW, (float) shown->loaded.origH };
}

float FullscreenView::fitScale() const
{
    const auto o = originalSize();
    const float s = jmin ((float) getWidth() / o.x, (float) getHeight() / o.y);
    // malé obrázky v pôvodnej veľkosti, SVG vždy na celú obrazovku
    return file.hasFileExtension ("svg") ? s : jmin (1.0f, s);
}

void FullscreenView::clampCentre()
{
    const auto o = originalSize();
    const float sc = currentScale();
    const float hw = (float) getWidth() / (2.0f * sc), hh = (float) getHeight() / (2.0f * sc);
    centre.x = o.x <= 2 * hw ? o.x / 2 : jlimit (hw, o.x - hw, centre.x);
    centre.y = o.y <= 2 * hh ? o.y / 2 : jlimit (hh, o.y - hh, centre.y);
}

void FullscreenView::zoomAt (float newScale, Point<float> p)
{
    if (shown == nullptr || ! shown->display.isValid()) return;
    const float old = currentScale();
    const auto screenCentre = getLocalBounds().toFloat().getCentre();
    if (zoom <= 0.0f) centre = originalSize() / 2.0f;
    const auto imgPt = centre + (p - screenCentre) / old;
    newScale = jlimit (fitScale(), 32.0f, newScale);
    if (newScale <= fitScale() * 1.001f) { zoom = 0.0f; repaint(); return; }
    zoom = newScale;
    centre = imgPt - (p - screenCentre) / newScale;
    clampCentre();
    requestFull();
    repaint();
}

void FullscreenView::requestFull()
{
    if (shown == nullptr || file.hasFileExtension ("svg")) return;
    const auto path = file.getFullPathName();
    if (fullPath == path || fullLoading) return;
    // stačí, kým zmenšený obrázok nie je pri tomto priblížení rozmazaný
    if (currentScale() * originalSize().x <= (float) shown->display.getWidth() * 1.05f) return;
    fullLoading = true;
    auto aliveFlag = alive;
    const auto f = file;
    fullLoader.addJob ([this, f, path, aliveFlag]
    {
        auto loaded = img::loadFull (f, 30000);
        MessageManager::callAsync ([this, aliveFlag, path, loaded]
        {
            if (! *aliveFlag) return;
            fullLoading = false;
            if (file.getFullPathName() != path) return;
            full = loaded.image.isValid() ? NativeImageType().convert (loaded.image) : Image();
            fullPath = path;
            repaint();
        });
    });
}

void FullscreenView::setFile (const File& f, int pos, int tot)
{
    if (f != file) { zoom = 0.0f; full = {}; fullPath = {}; }
    file = f;
    position = pos;
    total = tot;
    images.setPinned (f);
    shown = images.get (f);
    repaint();
}

void FullscreenView::imageLoaded (const String& path)
{
    if (file.getFullPathName() == path)
    {
        shown = images.get (file);
        repaint();
    }
}

void FullscreenView::showMessage (const String& m, bool error)
{
    message = m;
    messageIsError = error;
    startTimer (2500);
    repaint();
}

void FullscreenView::paint (Graphics& g)
{
    g.fillAll (Colours::black);
    auto area = getLocalBounds();

    Image im = shown != nullptr ? shown->display : thumbs.get (file);
    if (shown != nullptr && fullPath == file.getFullPathName() && full.isValid()) im = full;
    if (im.isValid() && shown != nullptr)
    {
        const auto o = originalSize();
        const float sc = currentScale();
        const auto c = zoom > 0.0f ? centre : o / 2.0f;
        const auto mid = area.toFloat().getCentre();
        const auto t = AffineTransform::scale (o.x / (float) im.getWidth(), o.y / (float) im.getHeight())
                           .translated (-c.x, -c.y).scaled (sc).translated (mid.x, mid.y);
        g.setImageResamplingQuality (sc * o.x / (float) im.getWidth() > 2.0f ? Graphics::lowResamplingQuality
                                                                                : Graphics::highResamplingQuality);
        g.drawImageTransformed (im, t, false);
    }
    else if (im.isValid())
    {
        // kým sa načíta, ukáž náhľad
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImageWithin (im, area.getX(), area.getY(), area.getWidth(), area.getHeight(), RectanglePlacement::centred);
    }
    else if (shown != nullptr)
    {
        g.setColour (col::textDim);
        g.setFont (FontOptions (16.0f));
        g.drawText (String::fromUTF8 ("Obrázok sa nedá načítať"), area, Justification::centred);
    }

    // spodný riadok: názov a poradie
    auto bar = area.removeFromBottom (30);
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillRect (bar);
    g.setColour (col::text);
    g.setFont (FontOptions (13.0f));
    String info = file.getFileName() + String::fromUTF8 ("   ·   ") + String (position) + " / " + String (total);
    if (shown != nullptr && shown->loaded.origW > 0)
        info << String::fromUTF8 ("   ·   ") << shown->loaded.origW << String::fromUTF8 (" × ") << shown->loaded.origH;
    info << String::fromUTF8 ("   ·   ") << roundToInt (currentScale() * 100.0f) << " %";
    g.drawText (info, bar.reduced (14, 0), Justification::centredLeft, true);
    g.setColour (col::textDim);
    g.drawText (String::fromUTF8 ("← → listovanie · koliesko priblíženie · 1–9 presun · Ctrl+Z späť · Esc koniec"),
                bar.reduced (14, 0), Justification::centredRight, true);

    if (message.isNotEmpty())
    {
        auto m = getLocalBounds().withTrimmedBottom (50).removeFromBottom (40).withSizeKeepingCentre (jmin (getWidth() - 40, 900), 36);
        g.setColour (Colours::black.withAlpha (0.75f));
        g.fillRoundedRectangle (m.toFloat(), radius);
        g.setColour (messageIsError ? Colour (0xffd7807a) : col::gold);
        g.setFont (FontOptions (15.0f));
        g.drawText (message, m.reduced (12, 0), Justification::centred, true);
    }
}

bool FullscreenView::keyPressed (const KeyPress& key)
{
    const int k = key.getKeyCode();
    const auto mods = key.getModifiers();
    if (! mods.isCtrlDown() && ! mods.isAltDown())
    {
        if (k == KeyPress::escapeKey || k == KeyPress::returnKey || k == KeyPress::F11Key)
        {
            if (onClose) onClose();
            return true;
        }
        if (k == KeyPress::leftKey || k == KeyPress::upKey || k == KeyPress::pageUpKey || k == KeyPress::backspaceKey)
        {
            if (onStep) onStep (-1);
            return true;
        }
        if (k == KeyPress::rightKey || k == KeyPress::downKey || k == KeyPress::pageDownKey || k == KeyPress::spaceKey)
        {
            if (onStep) onStep (1);
            return true;
        }
        if (k == '+' || k == '=' || k == KeyPress::numberPadAdd)
        {
            zoomAt (currentScale() * 1.25f, getLocalBounds().toFloat().getCentre());
            return true;
        }
        if (k == '-' || k == KeyPress::numberPadSubtract)
        {
            zoomAt (currentScale() / 1.25f, getLocalBounds().toFloat().getCentre());
            return true;
        }
        if (k == '0' || k == KeyPress::numberPad0) { zoom = 0.0f; repaint(); return true; }
        if (k == KeyPress::homeKey) { if (onStep) onStep (-100000); return true; }
        if (k == KeyPress::endKey)  { if (onStep) onStep (100000); return true; }
    }
    return onOtherKey != nullptr && onOtherKey (key);
}

void FullscreenView::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (std::abs (w.deltaY) < 0.001f) return;
    const float factor = std::pow (1.25f, jlimit (-3.0f, 3.0f, w.deltaY * 4.0f));
    zoomAt (currentScale() * factor, e.position);
}

void FullscreenView::mouseDoubleClick (const MouseEvent& e)
{
    // prepínanie: celý obrázok ↔ 100 %
    if (zoom > 0.0f) { zoom = 0.0f; repaint(); }
    else zoomAt (jmax (1.0f, fitScale() * 2.0f), e.position);
}

void FullscreenView::mouseDown (const MouseEvent&)
{
    dragStartCentre = centre;
}

void FullscreenView::mouseDrag (const MouseEvent& e)
{
    if (zoom <= 0.0f) return;
    centre = dragStartCentre - e.getOffsetFromDragStart().toFloat() / currentScale();
    clampCentre();
    repaint();
}
}
