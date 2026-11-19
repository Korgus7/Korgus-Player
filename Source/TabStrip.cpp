#include "TabStrip.h"

namespace kp
{
float TabStrip::tabWidth() const
{
    const float avail = (float) getWidth() - 40.0f;
    return jlimit (90.0f, 200.0f, avail / (float) jmax (1, tabs.size()));
}

Rectangle<float> TabStrip::tabBounds (int i) const
{
    return { 6.0f + (float) i * tabWidth(), 4.0f, tabWidth() - 4.0f, (float) getHeight() - 4.0f };
}

Rectangle<float> TabStrip::addBounds() const
{
    const float x = 6.0f + (float) tabs.size() * tabWidth();
    return { x, 6.0f, 26.0f, (float) getHeight() - 10.0f };
}

int TabStrip::tabAt (Point<float> p) const
{
    for (int i = 0; i < tabs.size(); ++i)
        if (tabBounds (i).contains (p)) return i;
    return -1;
}

void TabStrip::paint (Graphics& g)
{
    g.fillAll (col::bg);
    const auto mouse = getMouseXYRelative().toFloat();
    const bool over = isMouseOver();
    for (int i = 0; i < tabs.size(); ++i)
    {
        auto r = tabBounds (i);
        const bool cur = i == current;
        Path p;
        p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight() + 4.0f, 5.0f, 5.0f, true, true, false, false);
        g.setColour (cur ? col::panel : (over && r.contains (mouse) ? col::panelHi.withAlpha (0.7f) : col::panel.withAlpha (0.45f)));
        g.fillPath (p);
        if (cur) { g.setColour (col::gold); g.fillRect (r.getX() + 6.0f, r.getY(), r.getWidth() - 12.0f, 2.0f); }
        auto name = tabs[i].getFileName();
        if (name.isEmpty()) name = tabs[i].getFullPathName();
        g.setColour (cur ? col::text : col::textDim);
        g.setFont (FontOptions (13.0f));
        auto tr2 = r.reduced (10, 0);
        if (tabs.size() > 1) tr2.removeFromRight (16);
        g.drawText (name, tr2, Justification::centredLeft, true);
        if (tabs.size() > 1 && (cur || (over && r.contains (mouse))))
        {
            auto c = closeBounds (i);
            const bool hot = over && c.contains (mouse);
            if (hot) { g.setColour (col::panelHi); g.fillEllipse (c.expanded (3)); }
            g.setColour (hot ? col::text : col::textDim);
            g.drawLine (c.getX() + 3, c.getY() + 3, c.getRight() - 3, c.getBottom() - 3, 1.4f);
            g.drawLine (c.getRight() - 3, c.getY() + 3, c.getX() + 3, c.getBottom() - 3, 1.4f);
        }
    }
    auto a = addBounds();
    g.setColour (over && a.contains (mouse) ? col::text : col::textDim);
    g.drawLine (a.getCentreX() - 5, a.getCentreY(), a.getCentreX() + 5, a.getCentreY(), 1.6f);
    g.drawLine (a.getCentreX(), a.getCentreY() - 5, a.getCentreX(), a.getCentreY() + 5, 1.6f);
}

void TabStrip::mouseMove (const MouseEvent& e)
{
    const int i = tabAt (e.position);
    setTooltip (i >= 0 ? tabs[i].getFullPathName() + tr ("\\nCtrl+T nová karta · Ctrl+W zavrieť · Ctrl+Tab ďalšia")
                       : (addBounds().contains (e.position) ? tr ("Nová karta (Ctrl+T)") : String()));
    repaint();
}

void TabStrip::mouseDown (const MouseEvent& e)
{
    if (addBounds().contains (e.position)) { if (onAdd) onAdd(); return; }
    const int i = tabAt (e.position);
    if (i < 0) return;
    if (tabs.size() > 1 && (e.mods.isMiddleButtonDown() || closeBounds (i).contains (e.position))) { if (onClose) onClose (i); return; }
    if (onSelect) onSelect (i);
}
}
