#pragma once
#include "Common.h"

namespace kp
{
enum class Icon { Left, Right, Fit, Prev, Play, Pause, Stop, Next, Repeat, RepeatOne, Shuffle, RotateLeft, RotateRight, Resize, Slideshow, Volume };

// Ikony kreslené ako cesty (nezávislé od písma)
inline Path makeIcon (Icon icon, Rectangle<float> r)
{
    Path p;
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    auto tri = [&] (float x0, float y0, float x1, float y1, float x2, float y2) { p.addTriangle (x0, y0, x1, y1, x2, y2); };
    switch (icon)
    {
        case Icon::Left:  tri (x + w * 0.7f, y + h * 0.2f, x + w * 0.7f, y + h * 0.8f, x + w * 0.28f, y + h * 0.5f); break;
        case Icon::Right: tri (x + w * 0.3f, y + h * 0.2f, x + w * 0.3f, y + h * 0.8f, x + w * 0.72f, y + h * 0.5f); break;
        case Icon::Fit:
        {
            Path s;
            s.addRectangle (x + w * 0.1f, y + h * 0.2f, w * 0.8f, h * 0.6f);
            PathStrokeType (w * 0.08f).createStrokedPath (p, s);
            tri (x + w * 0.2f, y + h * 0.3f, x + w * 0.38f, y + h * 0.3f, x + w * 0.2f, y + h * 0.46f);
            tri (x + w * 0.8f, y + h * 0.7f, x + w * 0.62f, y + h * 0.7f, x + w * 0.8f, y + h * 0.54f);
            break;
        }
        case Icon::Play:  tri (x + w * 0.22f, y + h * 0.12f, x + w * 0.22f, y + h * 0.88f, x + w * 0.88f, y + h * 0.5f); break;
        case Icon::Pause: p.addRectangle (x + w * 0.2f, y + h * 0.15f, w * 0.2f, h * 0.7f); p.addRectangle (x + w * 0.6f, y + h * 0.15f, w * 0.2f, h * 0.7f); break;
        case Icon::Stop:  p.addRectangle (x + w * 0.2f, y + h * 0.2f, w * 0.6f, h * 0.6f); break;
        case Icon::Prev:  p.addRectangle (x + w * 0.12f, y + h * 0.18f, w * 0.12f, h * 0.64f); tri (x + w * 0.85f, y + h * 0.18f, x + w * 0.85f, y + h * 0.82f, x + w * 0.27f, y + h * 0.5f); break;
        case Icon::Next:  p.addRectangle (x + w * 0.76f, y + h * 0.18f, w * 0.12f, h * 0.64f); tri (x + w * 0.15f, y + h * 0.18f, x + w * 0.15f, y + h * 0.82f, x + w * 0.73f, y + h * 0.5f); break;
        case Icon::Repeat:
        case Icon::RepeatOne:
        {
            Path s;
            s.addRoundedRectangle (x + w * 0.12f, y + h * 0.25f, w * 0.76f, h * 0.5f, h * 0.18f);
            PathStrokeType (w * 0.1f).createStrokedPath (p, s);
            tri (x + w * 0.5f, y + h * 0.08f, x + w * 0.5f, y + h * 0.42f, x + w * 0.72f, y + h * 0.25f);
            if (icon == Icon::RepeatOne) p.addRectangle (x + w * 0.46f, y + h * 0.4f, w * 0.1f, h * 0.24f);
            break;
        }
        case Icon::Shuffle:
        {
            Path s;
            s.startNewSubPath (x + w * 0.1f, y + h * 0.3f);
            s.cubicTo (x + w * 0.45f, y + h * 0.3f, x + w * 0.45f, y + h * 0.7f, x + w * 0.78f, y + h * 0.7f);
            s.startNewSubPath (x + w * 0.1f, y + h * 0.7f);
            s.cubicTo (x + w * 0.45f, y + h * 0.7f, x + w * 0.45f, y + h * 0.3f, x + w * 0.78f, y + h * 0.3f);
            PathStrokeType (w * 0.09f).createStrokedPath (p, s);
            tri (x + w * 0.74f, y + h * 0.16f, x + w * 0.74f, y + h * 0.44f, x + w * 0.95f, y + h * 0.3f);
            tri (x + w * 0.74f, y + h * 0.56f, x + w * 0.74f, y + h * 0.84f, x + w * 0.95f, y + h * 0.7f);
            break;
        }
        case Icon::RotateLeft:
        case Icon::RotateRight:
        {
            const bool right = icon == Icon::RotateRight;
            Path s;
            const auto c = r.getCentre();
            const float rad = jmin (w, h) * 0.32f;
            // oblúk 270°, šípka na konci
            s.addCentredArc (c.x, c.y + h * 0.05f, rad, rad, 0.0f, right ? -MathConstants<float>::pi * 0.25f : MathConstants<float>::pi * 0.25f,
                             right ? MathConstants<float>::pi * 1.25f : -MathConstants<float>::pi * 1.25f, true);
            PathStrokeType (w * 0.1f).createStrokedPath (p, s);
            const float ax = right ? c.x + rad * 0.72f : c.x - rad * 0.72f, ay = c.y + h * 0.05f - rad * 0.72f;
            if (right) tri (ax - w * 0.2f, ay - h * 0.12f, ax + w * 0.08f, ay - h * 0.2f, ax + w * 0.02f, ay + h * 0.12f);
            else       tri (ax + w * 0.2f, ay - h * 0.12f, ax - w * 0.08f, ay - h * 0.2f, ax - w * 0.02f, ay + h * 0.12f);
            break;
        }
        case Icon::Resize:
        {
            Path s;
            s.addRectangle (x + w * 0.12f, y + h * 0.42f, w * 0.46f, h * 0.46f);
            PathStrokeType (w * 0.08f).createStrokedPath (p, s);
            Path a;
            a.startNewSubPath (x + w * 0.45f, y + h * 0.55f);
            a.lineTo (x + w * 0.84f, y + h * 0.16f);
            PathStrokeType (w * 0.08f).createStrokedPath (s, a);
            p.addPath (s);
            tri (x + w * 0.92f, y + h * 0.08f, x + w * 0.58f, y + h * 0.12f, x + w * 0.88f, y + h * 0.42f);
            break;
        }
        case Icon::Slideshow:
        {
            Path s;
            s.addRoundedRectangle (x + w * 0.08f, y + h * 0.18f, w * 0.84f, h * 0.64f, w * 0.06f);
            PathStrokeType (w * 0.08f).createStrokedPath (p, s);
            tri (x + w * 0.4f, y + h * 0.34f, x + w * 0.4f, y + h * 0.66f, x + w * 0.66f, y + h * 0.5f);
            break;
        }
        case Icon::Volume:
        {
            p.addRectangle (x + w * 0.1f, y + h * 0.38f, w * 0.18f, h * 0.24f);
            tri (x + w * 0.2f, y + h * 0.5f, x + w * 0.5f, y + h * 0.15f, x + w * 0.5f, y + h * 0.85f);
            Path arc, st;
            arc.addCentredArc (x + w * 0.5f, y + h * 0.5f, w * 0.3f, h * 0.3f, 0, MathConstants<float>::pi * 0.25f, MathConstants<float>::pi * 0.75f, true);
            PathStrokeType (w * 0.07f).createStrokedPath (st, arc);
            p.addPath (st);
            break;
        }
        default: break;
    }
    return p;
}

// Tlačidlo s ikonou (prípadne s textom vedľa)
class IconButton : public Button
{
public:
    IconButton (Icon i, const String& tip, const String& label = {}) : Button (tip), icon (i), text (label)
    {
        setTooltip (tip);
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    void setIcon (Icon i) { icon = i; repaint(); }
    void setActive (bool a) { active = a; repaint(); }
    void setText (const String& t) { text = t; repaint(); }

    void paintButton (Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        if (over || down || background)
        {
            g.setColour (down ? col::accent : (over ? col::accentHi.withAlpha (0.8f) : col::panelHi.withAlpha (0.85f)));
            g.fillRoundedRectangle (r, radius);
        }
        const float s = jmin (r.getHeight() - 10.0f, 18.0f);
        auto iconArea = text.isEmpty() ? r.withSizeKeepingCentre (s, s) : Rectangle<float> (r.getX() + 7, r.getCentreY() - s / 2, s, s);
        g.setColour (! isEnabled() ? col::textDim.withAlpha (0.4f) : (active ? col::gold : col::text));
        g.fillPath (makeIcon (icon, iconArea));
        if (text.isNotEmpty())
        {
            g.setFont (FontOptions (12.5f));
            g.drawText (text, r.withTrimmedLeft (s + 12), Justification::centredLeft, true);
        }
    }

    bool background = false;

private:
    Icon icon;
    String text;
    bool active = false;
};
}
