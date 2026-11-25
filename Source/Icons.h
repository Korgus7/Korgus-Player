#pragma once
#include "Common.h"

namespace kp
{
enum class Icon { Left, Right, Fit, Crop, Camera, Close, Prev, Play, Pause, Stop, Next, Repeat, RepeatOne, Shuffle, RotateLeft, RotateRight, Resize, Slideshow, Volume,
                  Up, Filter, Sort, Grid, List, Dual, Menu, Copy, Gear, Net, Phone, Disk, Pencil };

// Ikony kreslené ako cesty (nezávislé od písma)
inline Path makeIcon (Icon icon, Rectangle<float> r)
{
    Path p, a;
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    auto P = [&] (float fx, float fy) { return Point<float> (x + w * fx, y + h * fy); };
    auto M = [&] (float fx, float fy) { a.startNewSubPath (P (fx, fy)); };
    auto L = [&] (float fx, float fy) { a.lineTo (P (fx, fy)); };
    auto RR = [&] (float fx, float fy, float fw, float fh, float cr) { a.addRoundedRectangle (x + w * fx, y + h * fy, w * fw, h * fh, w * cr); };
    auto tri = [&] (float x0, float y0, float x1, float y1, float x2, float y2) { p.addTriangle (x0, y0, x1, y1, x2, y2); };
    switch (icon)
    {
        case Icon::Left:  tri (x + w * 0.7f, y + h * 0.2f, x + w * 0.7f, y + h * 0.8f, x + w * 0.28f, y + h * 0.5f); break;
        case Icon::Right: tri (x + w * 0.3f, y + h * 0.2f, x + w * 0.3f, y + h * 0.8f, x + w * 0.72f, y + h * 0.5f); break;
        case Icon::Close:
        {
            Path a;
            a.startNewSubPath (x + w * 0.2f, y + h * 0.2f); a.lineTo (x + w * 0.8f, y + h * 0.8f);
            a.startNewSubPath (x + w * 0.8f, y + h * 0.2f); a.lineTo (x + w * 0.2f, y + h * 0.8f);
            PathStrokeType (w * 0.1f, PathStrokeType::curved, PathStrokeType::rounded).createStrokedPath (p, a);
            break;
        }
        case Icon::Camera:
        {
            Path a;
            a.addRoundedRectangle (x + w * 0.08f, y + h * 0.3f, w * 0.84f, h * 0.56f, w * 0.08f);
            PathStrokeType (w * 0.08f).createStrokedPath (p, a);
            p.addEllipse (x + w * 0.36f, y + h * 0.44f, w * 0.28f, h * 0.28f);
            p.addRectangle (x + w * 0.32f, y + h * 0.18f, w * 0.3f, h * 0.12f);
            break;
        }
        case Icon::Crop:
        {
            Path a;
            a.startNewSubPath (x + w * 0.28f, y + h * 0.08f);
            a.lineTo (x + w * 0.28f, y + h * 0.72f);
            a.lineTo (x + w * 0.92f, y + h * 0.72f);
            a.startNewSubPath (x + w * 0.08f, y + h * 0.28f);
            a.lineTo (x + w * 0.72f, y + h * 0.28f);
            a.lineTo (x + w * 0.72f, y + h * 0.92f);
            PathStrokeType (w * 0.1f).createStrokedPath (p, a);
            break;
        }
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
            // otvorený kruh, tenká čiara, zreteľná šípka hore v smere otáčania
            const bool right = icon == Icon::RotateRight;
            const float sgn = right ? 1.0f : -1.0f;
            const auto c = r.getCentre().translated (0.0f, h * 0.05f);
            const float rad = jmin (w, h) * 0.32f;
            const float pi = MathConstants<float>::pi;
            Path s;
            s.addCentredArc (c.x, c.y, rad, rad, 0.0f, sgn * pi * 0.55f, sgn * pi * 1.92f, true);
            PathStrokeType (w * 0.07f, PathStrokeType::curved, PathStrokeType::rounded).createStrokedPath (p, s);
            const float tx = c.x, ty = c.y - rad;
            tri (tx - sgn * w * 0.03f, ty - h * 0.15f,
                 tx - sgn * w * 0.03f, ty + h * 0.15f,
                 tx + sgn * w * 0.2f, ty);
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
        case Icon::Menu:
            for (int i = 0; i < 3; ++i)
                p.addRoundedRectangle (x + w * 0.14f, y + h * (0.22f + 0.25f * (float) i), w * 0.72f, h * 0.09f, h * 0.04f);
            break;
        case Icon::Copy:   RR (0.34f, 0.30f, 0.50f, 0.58f, 0.09f); M (0.20f, 0.70f); L (0.20f, 0.24f);
                           a.quadraticTo (P (0.20f, 0.12f), P (0.32f, 0.12f)); L (0.66f, 0.12f); break;
        case Icon::Filter: M (0.12f, 0.28f); L (0.88f, 0.28f); M (0.26f, 0.50f); L (0.74f, 0.50f); M (0.40f, 0.72f); L (0.60f, 0.72f); break;
        case Icon::Grid:   for (float yy : { 0.14f, 0.56f }) for (float xx : { 0.14f, 0.56f }) RR (xx, yy, 0.30f, 0.30f, 0.07f); break;
        case Icon::List:   for (int i = 0; i < 3; ++i) { const float yy = 0.26f + 0.24f * (float) i; M (0.34f, yy); L (0.88f, yy);
                               a.addEllipse (x + w * 0.12f, y + h * yy - w * 0.045f, w * 0.09f, w * 0.09f); } break;
        case Icon::Slideshow: RR (0.08f, 0.18f, 0.84f, 0.64f, 0.1f); M (0.42f, 0.36f); L (0.42f, 0.64f); L (0.64f, 0.50f); a.closeSubPath(); break;
        case Icon::Dual:   RR (0.08f, 0.18f, 0.84f, 0.64f, 0.1f); M (0.50f, 0.18f); L (0.50f, 0.82f); break;
        case Icon::Gear:
        {
            const auto c = r.getCentre();
            const float ro = w * 0.42f, ri = w * 0.32f;
            for (int i = 0; i < 6; ++i)
            {
                const float an = (float) i * MathConstants<float>::twoPi / 6.0f;
                auto pt = [&] (float rad, float ang) { return Point<float> (c.x + rad * std::sin (ang), c.y - rad * std::cos (ang)); };
                if (i == 0) a.startNewSubPath (pt (ri, an - 0.42f)); else a.lineTo (pt (ri, an - 0.42f));
                a.lineTo (pt (ro, an - 0.22f)); a.lineTo (pt (ro, an + 0.22f)); a.lineTo (pt (ri, an + 0.42f));
            }
            a.closeSubPath();
            a.addEllipse (Rectangle<float> (w * 0.26f, w * 0.26f).withCentre (c));
            break;
        }
        case Icon::Net:    RR (0.35f, 0.10f, 0.30f, 0.22f, 0.04f); M (0.50f, 0.32f); L (0.50f, 0.50f);
                           M (0.22f, 0.66f); L (0.22f, 0.50f); L (0.78f, 0.50f); L (0.78f, 0.66f);
                           RR (0.07f, 0.66f, 0.30f, 0.22f, 0.04f); RR (0.63f, 0.66f, 0.30f, 0.22f, 0.04f); break;
        case Icon::Phone:  RR (0.29f, 0.08f, 0.42f, 0.84f, 0.08f); M (0.44f, 0.79f); L (0.56f, 0.79f); break;
        case Icon::Disk:   RR (0.08f, 0.30f, 0.84f, 0.40f, 0.07f); M (0.20f, 0.50f); L (0.50f, 0.50f);
                           a.addEllipse (x + w * 0.70f, y + h * 0.50f - w * 0.05f, w * 0.10f, w * 0.10f); break;
        case Icon::Pencil: M (0.18f, 0.82f); L (0.24f, 0.60f); L (0.64f, 0.20f); L (0.80f, 0.36f); L (0.40f, 0.76f); a.closeSubPath();
                           M (0.56f, 0.28f); L (0.72f, 0.44f); break;
        case Icon::Up:
        {
            Path a;
            a.startNewSubPath (x + w * 0.5f, y + h * 0.84f); a.lineTo (x + w * 0.5f, y + h * 0.18f);
            a.startNewSubPath (x + w * 0.26f, y + h * 0.42f); a.lineTo (x + w * 0.5f, y + h * 0.18f); a.lineTo (x + w * 0.74f, y + h * 0.42f);
            PathStrokeType (w * 0.07f, PathStrokeType::curved, PathStrokeType::rounded).createStrokedPath (p, a);
            break;
        }
        case Icon::Sort:
        {
            Path a;
            a.startNewSubPath (x + w * 0.1f, y + h * 0.26f); a.lineTo (x + w * 0.56f, y + h * 0.26f);
            a.startNewSubPath (x + w * 0.1f, y + h * 0.5f);  a.lineTo (x + w * 0.44f, y + h * 0.5f);
            a.startNewSubPath (x + w * 0.1f, y + h * 0.74f); a.lineTo (x + w * 0.32f, y + h * 0.74f);
            a.startNewSubPath (x + w * 0.76f, y + h * 0.18f); a.lineTo (x + w * 0.76f, y + h * 0.82f);
            a.startNewSubPath (x + w * 0.62f, y + h * 0.66f); a.lineTo (x + w * 0.76f, y + h * 0.82f); a.lineTo (x + w * 0.9f, y + h * 0.66f);
            PathStrokeType (w * 0.07f, PathStrokeType::curved, PathStrokeType::rounded).createStrokedPath (p, a);
            break;
        }
        default: break;
    }
    if (! a.isEmpty())
        PathStrokeType (w * 0.068f, PathStrokeType::curved, PathStrokeType::rounded).createStrokedPath (p, a);
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
    void setMaxIcon (float m) { maxIcon = m; repaint(); }
    int idealWidth (int h) const { const float s = jmin ((float) h - 10.0f, maxIcon); return text.isEmpty() ? h + 8 : roundToInt (s) + 20 + GlyphArrangement::getStringWidthInt (FontOptions (13.5f), text); }
    const String& getText() const { return text; }

    void paintButton (Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat();
        if (over || down || background)
        {
            g.setColour (down ? col::accent : (over ? col::accentHi.withAlpha (0.8f) : col::panelHi.withAlpha (0.85f)));
            g.fillRoundedRectangle (r, radius);
        }
        const float s = jmin (r.getHeight() - 10.0f, maxIcon);
        auto iconArea = text.isEmpty() ? r.withSizeKeepingCentre (s, s) : Rectangle<float> (r.getX() + 7, r.getCentreY() - s / 2, s, s);
        g.setColour (! isEnabled() ? col::textDim.withAlpha (0.4f) : (active ? col::gold : col::text));
        g.fillPath (makeIcon (icon, iconArea));
        if (text.isNotEmpty())
        {
            g.setFont (FontOptions (maxIcon > 18.0f ? 13.5f : 12.5f));
            g.drawText (text, r.withTrimmedLeft (s + 12), Justification::centredLeft, true);
        }
    }

    bool background = false;

private:
    float maxIcon = 18.0f;
    Icon icon;
    String text;
    bool active = false;
};
}
