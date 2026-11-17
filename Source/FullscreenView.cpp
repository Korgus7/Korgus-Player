#include "FullscreenView.h"

namespace kp
{
FullscreenView::FullscreenView (ImageCache& ic, ThumbCache& tc) : images (ic), thumbs (tc)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
}

void FullscreenView::setFile (const File& f, int pos, int tot)
{
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
    if (im.isValid())
    {
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        // veľké zmenšiť na obrazovku, malé nechať v pôvodnej veľkosti (náhľad sa zväčší)
        const auto placement = shown != nullptr ? RectanglePlacement::centred | RectanglePlacement::onlyReduceInSize
                                                : RectanglePlacement::centred;
        g.drawImageWithin (im, area.getX(), area.getY(), area.getWidth(), area.getHeight(), placement);
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
    g.drawText (info, bar.reduced (14, 0), Justification::centredLeft, true);
    g.setColour (col::textDim);
    g.drawText (String::fromUTF8 ("← → listovanie · 1–9 presun · Ctrl+Z späť · Esc koniec"),
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
        if (k == KeyPress::homeKey) { if (onStep) onStep (-100000); return true; }
        if (k == KeyPress::endKey)  { if (onStep) onStep (100000); return true; }
    }
    return onOtherKey != nullptr && onOtherKey (key);
}

void FullscreenView::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    // jedno cvaknutie kolieska = jeden obrázok (touchpad nesmie preletieť desiatky)
    const double now = Time::getMillisecondCounterHiRes();
    if (std::abs (w.deltaY) < 0.01f || now - lastWheel < 120.0) return;
    lastWheel = now;
    if (onStep) onStep (w.deltaY > 0 ? -1 : 1);
}
}
