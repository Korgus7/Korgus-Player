#include "DocViewers.h"
#include <cmath>
#include <map>

namespace kp
{
namespace
{
    // ---------------------------------------------------------------- DXF: skupiny (kód, hodnota)
    struct Group { int code; String value; };

    struct Ent
    {
        String type;
        std::vector<Group> g;
        std::vector<Ent> sub;            // VERTEX pri POLYLINE

        String str (int code, const String& def = {}) const
        {
            for (auto& x : g) if (x.code == code) return x.value;
            return def;
        }
        double num (int code, double def = 0.0) const
        {
            for (auto& x : g) if (x.code == code) return x.value.getDoubleValue();
            return def;
        }
        bool has (int code) const
        {
            for (auto& x : g) if (x.code == code) return true;
            return false;
        }
        std::vector<double> all (int code) const
        {
            std::vector<double> r;
            for (auto& x : g) if (x.code == code) r.push_back (x.value.getDoubleValue());
            return r;
        }
    };

    struct Block { double bx = 0, by = 0; std::vector<Ent> ents; };

    struct Drawing
    {
        std::map<uint32, Path> lines;     // farba → čiary
        std::map<uint32, Path> fills;     // farba → plochy (SOLID)
        struct Text { String s; AffineTransform t; float h; Colour c; };
        std::vector<Text> texts;
        Rectangle<float> bounds;
        int entities = 0, layers = 0;
        String error;
    };

    Colour aci (int i)
    {
        static const uint32 base[] = { 0xff000000, 0xffff0000, 0xffffff00, 0xff00ff00, 0xff00ffff, 0xff0000ff, 0xffff00ff,
                                       0xffffffff, 0xff808080, 0xffc0c0c0 };
        if (i >= 0 && i <= 9) return Colour (base[i]);
        if (i >= 250 && i <= 255) { const uint8 v = (uint8) (51 + (i - 250) * 40); return Colour (v, v, v); }
        if (i >= 10 && i <= 249)
        {
            const int idx = i - 10;
            const float hue = (float) (idx / 10) * 15.0f / 360.0f;
            const int var = idx % 10;
            static const float lev[] = { 1.0f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f, 0.5f, 0.5f, 0.3f, 0.3f };
            return Colour::fromHSV (hue, (var & 1) ? 0.5f : 1.0f, lev[var], 1.0f);
        }
        return Colours::white;
    }

    String decodeText (String s)
    {
        // \U+XXXX → znak
        int p;
        while ((p = s.indexOf ("\\U+")) >= 0 && p + 7 <= s.length())
        {
            const auto hex = s.substring (p + 3, p + 7);
            s = s.replaceSection (p, 7, String::charToString ((juce_wchar) hex.getHexValue32()));
        }
        return s.replace ("%%c", String::fromUTF8 ("Ø")).replace ("%%d", String::fromUTF8 ("°")).replace ("%%p", String::fromUTF8 ("±"))
                .replace ("%%C", String::fromUTF8 ("Ø")).replace ("%%D", String::fromUTF8 ("°")).replace ("%%P", String::fromUTF8 ("±"));
    }

    String stripMText (const String& in)
    {
        String out;
        auto t = in.getCharPointer();
        while (! t.isEmpty())
        {
            const auto c = t.getAndAdvance();
            if (c == '{' || c == '}') continue;
            if (c == '\\' && ! t.isEmpty())
            {
                const auto n = t.getAndAdvance();
                if (n == 'P' || n == 'n') { out << "\n"; continue; }
                if (n == '\\' || n == '{' || n == '}') { out << String::charToString (n); continue; }
                if (n == '~') { out << " "; continue; }
                if (n == 'U') { out << "\\U"; continue; }
                // formátovacie kódy \f...; \H...; \C...; atď. – preskočiť po bodkočiarku
                if (String ("ACFfHQTWpcSs").containsChar (n))
                {
                    while (! t.isEmpty() && *t != ';') ++t;
                    if (! t.isEmpty()) ++t;
                    continue;
                }
                continue;   // \L \l \O \o \K \k – podčiarknutie atď.
            }
            out << String::charToString (c);
        }
        return out;
    }

    class Builder
    {
    public:
        Drawing d;
        std::map<String, int> layerColour;
        std::map<String, Block> blocks;

        void emitAll (const std::vector<Ent>& ents, const AffineTransform& xf, int inheritedColour, int depth)
        {
            for (auto& e : ents) emit (e, xf, inheritedColour, depth);
        }

    private:
        static Point<float> pt (const Ent& e, int cx, int cy, const AffineTransform& xf)
        {
            float x = (float) e.num (cx), y = (float) e.num (cy);
            xf.transformPoint (x, y);
            return { x, y };
        }

        Colour colourFor (const Ent& e, int inherited, bool& visible)
        {
            visible = true;
            if (e.has (420))
            {
                const auto rgb = (uint32) e.str (420).getLargeIntValue();
                return Colour (0xff000000 | (rgb & 0xffffff));
            }
            int c = (int) e.num (62, 256);
            const auto layer = e.str (8, "0");
            int lc = 7;
            if (auto it = layerColour.find (layer); it != layerColour.end()) lc = it->second;
            if (lc < 0) { visible = false; return {}; }   // vypnutá vrstva
            if (c == 256) c = lc;
            if (c == 0) c = inherited > 0 ? inherited : 7;
            if (c < 0) { visible = false; return {}; }
            return aci (c);
        }

        Path& lines (Colour c) { return d.lines[c.getARGB()]; }

        static void addPoly (Path& p, const std::vector<Point<float>>& pts, bool closed)
        {
            if (pts.size() < 2) return;
            p.startNewSubPath (pts[0]);
            for (size_t i = 1; i < pts.size(); ++i) p.lineTo (pts[i]);
            if (closed) p.closeSubPath();
        }

        // body oblúka v pôvodných súradniciach (uhly v stupňoch, proti smeru hodín)
        static void arcPoints (std::vector<Point<double>>& out, double cx, double cy, double r, double a0, double a1)
        {
            while (a1 <= a0) a1 += 360.0;
            const double sweep = a1 - a0;
            const int steps = jmax (6, (int) (sweep / 360.0 * 96.0));
            for (int i = 0; i <= steps; ++i)
            {
                const double a = (a0 + sweep * i / steps) * MathConstants<double>::pi / 180.0;
                out.push_back ({ cx + r * std::cos (a), cy + r * std::sin (a) });
            }
        }

        static void bulgePoints (std::vector<Point<double>>& out, Point<double> p0, Point<double> p1, double bulge)
        {
            const double theta = 4.0 * std::atan (bulge);
            const double chord = p0.getDistanceFrom (p1);
            if (chord < 1e-12 || std::abs (bulge) < 1e-9) { out.push_back (p1); return; }
            const double r = chord / (2.0 * std::sin (theta / 2.0));
            const auto mid = (p0 + p1) * 0.5;
            const double h = r * std::cos (theta / 2.0);
            const auto dir = (p1 - p0) / chord;
            const Point<double> normal (-dir.y, dir.x);
            const auto c = mid + normal * h;
            const double a0 = std::atan2 (p0.y - c.y, p0.x - c.x);
            const int steps = jmax (4, (int) (std::abs (theta) / (2 * MathConstants<double>::pi) * 96.0));
            for (int i = 1; i <= steps; ++i)
            {
                const double a = a0 + theta * i / steps;
                out.push_back ({ c.x + std::abs (r) * std::cos (a), c.y + std::abs (r) * std::sin (a) });
            }
        }

        static std::vector<Point<float>> toScreen (const std::vector<Point<double>>& in, const AffineTransform& xf)
        {
            std::vector<Point<float>> out;
            out.reserve (in.size());
            for (auto& p : in)
            {
                float x = (float) p.x, y = (float) p.y;
                xf.transformPoint (x, y);
                out.push_back ({ x, y });
            }
            return out;
        }

        void emit (const Ent& e, const AffineTransform& xf, int inherited, int depth)
        {
            bool visible;
            const auto c = colourFor (e, inherited, visible);
            if (! visible) return;
            const auto& t = e.type;
            ++d.entities;

            if (t == "LINE")
            {
                addPoly (lines (c), { pt (e, 10, 20, xf), pt (e, 11, 21, xf) }, false);
            }
            else if (t == "CIRCLE" || t == "ARC")
            {
                std::vector<Point<double>> pts;
                const bool full = t == "CIRCLE";
                arcPoints (pts, e.num (10), e.num (20), e.num (40), full ? 0.0 : e.num (50), full ? 360.0 : e.num (51));
                addPoly (lines (c), toScreen (pts, xf), full);
            }
            else if (t == "LWPOLYLINE")
            {
                std::vector<Point<double>> verts;
                std::vector<double> bulges;
                for (size_t i = 0; i < e.g.size(); ++i)
                {
                    if (e.g[i].code == 10) { verts.push_back ({ e.g[i].value.getDoubleValue(), 0 }); bulges.push_back (0); }
                    else if (e.g[i].code == 20 && ! verts.empty()) verts.back().y = e.g[i].value.getDoubleValue();
                    else if (e.g[i].code == 42 && ! bulges.empty()) bulges.back() = e.g[i].value.getDoubleValue();
                }
                polyline (verts, bulges, ((int) e.num (70) & 1) != 0, xf, c);
            }
            else if (t == "POLYLINE")
            {
                std::vector<Point<double>> verts;
                std::vector<double> bulges;
                for (auto& v : e.sub)
                {
                    if (((int) v.num (70) & 128) != 0 && ! v.has (10)) continue;
                    verts.push_back ({ v.num (10), v.num (20) });
                    bulges.push_back (v.num (42));
                }
                polyline (verts, bulges, ((int) e.num (70) & 1) != 0, xf, c);
            }
            else if (t == "ELLIPSE")
            {
                const double cx = e.num (10), cy = e.num (20), mx = e.num (11), my = e.num (21), ratio = e.num (40, 1.0);
                double s = e.num (41, 0.0), en = e.num (42, MathConstants<double>::twoPi);
                while (en <= s) en += MathConstants<double>::twoPi;
                std::vector<Point<double>> pts;
                const int steps = jmax (8, (int) ((en - s) / MathConstants<double>::twoPi * 96.0));
                for (int i = 0; i <= steps; ++i)
                {
                    const double a = s + (en - s) * i / steps;
                    pts.push_back ({ cx + mx * std::cos (a) - ratio * my * std::sin (a), cy + my * std::cos (a) + ratio * mx * std::sin (a) });
                }
                addPoly (lines (c), toScreen (pts, xf), false);
            }
            else if (t == "SPLINE")
            {
                spline (e, xf, c);
            }
            else if (t == "SOLID" || t == "TRACE")
            {
                Path& p = d.fills[c.getARGB()];
                const auto a = pt (e, 10, 20, xf), b = pt (e, 11, 21, xf), cc = pt (e, 12, 22, xf);
                const auto dd = e.has (13) ? pt (e, 13, 23, xf) : cc;
                p.startNewSubPath (a); p.lineTo (b); p.lineTo (dd); p.lineTo (cc); p.closeSubPath();
            }
            else if (t == "3DFACE")
            {
                addPoly (lines (c), { pt (e, 10, 20, xf), pt (e, 11, 21, xf), pt (e, 12, 22, xf), pt (e, 13, 23, xf) }, true);
            }
            else if (t == "TEXT" || t == "ATTRIB" || t == "MTEXT")
            {
                text (e, xf, c);
            }
            else if ((t == "INSERT" || t == "DIMENSION") && depth < 10)
            {
                auto it = blocks.find (e.str (2));
                if (it == blocks.end()) return;
                const auto& b = it->second;
                const int blockColour = (int) e.num (62, 256) == 256 ? [&] { auto l = layerColour.find (e.str (8, "0")); return l != layerColour.end() ? l->second : 7; }()
                                                                    : (int) e.num (62);
                if (t == "DIMENSION")
                {
                    emitAll (b.ents, xf, blockColour, depth + 1);
                    return;
                }
                const double sx = e.num (41, 1.0), sy = e.num (42, 1.0), rot = e.num (50) * MathConstants<double>::pi / 180.0;
                const int cols = jmax (1, (int) e.num (70, 1)), rows = jmax (1, (int) e.num (71, 1));
                const double dx = e.num (44), dy = e.num (45);
                for (int r = 0; r < jmin (rows, 100); ++r)
                    for (int col = 0; col < jmin (cols, 100); ++col)
                    {
                        auto local = AffineTransform::translation ((float) -b.bx, (float) -b.by)
                                         .scaled ((float) sx, (float) sy)
                                         .translated ((float) (col * dx), (float) (r * dy))
                                         .rotated ((float) rot)
                                         .translated ((float) e.num (10), (float) e.num (20));
                        emitAll (b.ents, local.followedBy (xf), blockColour, depth + 1);
                    }
            }
        }

        void polyline (const std::vector<Point<double>>& verts, const std::vector<double>& bulges, bool closed,
                       const AffineTransform& xf, Colour c)
        {
            if (verts.size() < 2) return;
            std::vector<Point<double>> pts { verts[0] };
            const size_t n = verts.size();
            for (size_t i = 0; i + 1 < n + (closed ? 1 : 0); ++i)
            {
                const auto& p0 = verts[i];
                const auto& p1 = verts[(i + 1) % n];
                if (std::abs (bulges[i]) > 1e-9) bulgePoints (pts, p0, p1, bulges[i]);
                else pts.push_back (p1);
            }
            addPoly (lines (c), toScreen (pts, xf), false);
        }

        void spline (const Ent& e, const AffineTransform& xf, Colour c)
        {
            const int deg = (int) e.num (71, 3);
            const auto knots = e.all (40);
            const auto cx = e.all (10), cy = e.all (20), w = e.all (41);
            std::vector<Point<double>> pts;
            const size_t n = jmin (cx.size(), cy.size());
            if (n >= (size_t) deg + 1 && knots.size() == n + (size_t) deg + 1 && deg >= 1)
            {
                const double t0 = knots[(size_t) deg], t1 = knots[n];
                const int steps = jmax (16, (int) n * 12);
                for (int s = 0; s <= steps; ++s)
                {
                    double t = t0 + (t1 - t0) * s / steps;
                    if (s == steps) t = t1 - 1e-9 * (t1 - t0);
                    size_t k = (size_t) deg;
                    while (k + 1 < n && knots[k + 1] <= t) ++k;
                    // de Boor (racionálny, ak sú váhy)
                    std::vector<double> X ((size_t) deg + 1), Y ((size_t) deg + 1), W ((size_t) deg + 1);
                    for (int j = 0; j <= deg; ++j)
                    {
                        const size_t i = k - (size_t) deg + (size_t) j;
                        const double wi = w.size() == n ? w[i] : 1.0;
                        X[(size_t) j] = cx[i] * wi; Y[(size_t) j] = cy[i] * wi; W[(size_t) j] = wi;
                    }
                    for (int r = 1; r <= deg; ++r)
                        for (int j = deg; j >= r; --j)
                        {
                            const size_t i = k - (size_t) deg + (size_t) j;
                            const double den = knots[i + (size_t) deg + 1 - (size_t) r] - knots[i];
                            const double a = den > 0 ? (t - knots[i]) / den : 0.0;
                            X[(size_t) j] = (1 - a) * X[(size_t) j - 1] + a * X[(size_t) j];
                            Y[(size_t) j] = (1 - a) * Y[(size_t) j - 1] + a * Y[(size_t) j];
                            W[(size_t) j] = (1 - a) * W[(size_t) j - 1] + a * W[(size_t) j];
                        }
                    const double ww = W[(size_t) deg] != 0 ? W[(size_t) deg] : 1.0;
                    pts.push_back ({ X[(size_t) deg] / ww, Y[(size_t) deg] / ww });
                }
            }
            else
            {
                const auto fx = e.all (11), fy = e.all (21);
                const auto& ux = fx.size() >= 2 ? fx : cx;
                const auto& uy = fx.size() >= 2 ? fy : cy;
                for (size_t i = 0; i < jmin (ux.size(), uy.size()); ++i) pts.push_back ({ ux[i], uy[i] });
            }
            addPoly (lines (c), toScreen (pts, xf), ((int) e.num (70) & 1) != 0);
        }

        void text (const Ent& e, const AffineTransform& xf, Colour c)
        {
            String s = e.type == "MTEXT" ? [&] { String r; for (auto& g : e.g) if (g.code == 3) r << g.value; return r + e.str (1); }()
                                         : e.str (1);
            s = decodeText (e.type == "MTEXT" ? stripMText (s) : s);
            if (s.trim().isEmpty()) return;
            const float h = (float) e.num (40, 1.0);
            double x = e.num (10), y = e.num (20);
            if (e.type != "MTEXT" && ((int) e.num (72) != 0 || (int) e.num (73) != 0) && e.has (11))
            {
                x = e.num (11); y = e.num (21);
            }
            double rot = e.num (50) * MathConstants<double>::pi / 180.0;
            if (e.type == "MTEXT" && e.has (11)) rot = std::atan2 (e.num (21), e.num (11));
            // text v miestnych súradniciach: základná čiara na y=0, os y nadol (JUCE), preto zrkadlenie
            auto t = AffineTransform::scale (1.0f, -1.0f).rotated ((float) rot).translated ((float) x, (float) y).followedBy (xf);
            d.texts.push_back ({ s, t, h, c });
        }
    };

    std::shared_ptr<Drawing> loadDxf (const File& f)
    {
        auto dr = std::make_shared<Drawing>();
        MemoryBlock mb;
        if (! f.loadFileAsData (mb) || mb.getSize() < 20)
        {
            dr->error = String::fromUTF8 ("Výkres sa nedá načítať");
            return dr;
        }
        if (std::memcmp (mb.getData(), "AutoCAD Binary DXF", 18) == 0)
        {
            dr->error = String::fromUTF8 ("Binárny DXF zatiaľ nepodporujem");
            return dr;
        }
        auto* bytes = static_cast<const char*> (mb.getData());
        String text = CharPointer_UTF8::isValidString (bytes, (int) mb.getSize())
                        ? String (CharPointer_UTF8 (bytes), mb.getSize())
                        : String::createStringFromData (bytes, (int) mb.getSize());

        // páry riadkov: kód, hodnota
        std::vector<Group> groups;
        {
            auto lines = StringArray::fromLines (text);
            groups.reserve ((size_t) lines.size() / 2);
            for (int i = 0; i + 1 < lines.size(); i += 2)
                groups.push_back ({ lines[i].trim().getIntValue(), lines[i + 1].trimEnd() });
        }

        Builder b;
        std::vector<Ent> entities;
        size_t i = 0;
        auto readEntity = [&] (Ent& e)   // groups[i] je (0, TYP)
        {
            e.type = groups[i].value.trim();
            ++i;
            while (i < groups.size() && groups[i].code != 0) e.g.push_back (groups[i++]);
        };
        auto readEntities = [&] (std::vector<Ent>& out, const String& endTag)
        {
            while (i < groups.size())
            {
                if (groups[i].code != 0) { ++i; continue; }
                const auto tag = groups[i].value.trim();
                if (tag == endTag || tag == "ENDSEC" || tag == "EOF") return;
                Ent e;
                readEntity (e);
                if (e.type == "POLYLINE")
                {
                    while (i < groups.size() && groups[i].code == 0 && groups[i].value.trim() == "VERTEX")
                    {
                        Ent v;
                        readEntity (v);
                        e.sub.push_back (std::move (v));
                    }
                    if (i < groups.size() && groups[i].code == 0 && groups[i].value.trim() == "SEQEND")
                    {
                        Ent s;
                        readEntity (s);
                    }
                }
                else if (e.type == "INSERT" && (int) e.num (66) == 1)
                {
                    // atribúty bloku za INSERT
                    while (i < groups.size() && groups[i].code == 0 && groups[i].value.trim() == "ATTRIB")
                    {
                        Ent a;
                        readEntity (a);
                        out.push_back (std::move (a));
                    }
                    if (i < groups.size() && groups[i].code == 0 && groups[i].value.trim() == "SEQEND") { Ent s; readEntity (s); }
                }
                out.push_back (std::move (e));
            }
        };

        while (i < groups.size())
        {
            if (! (groups[i].code == 0 && groups[i].value.trim() == "SECTION")) { ++i; continue; }
            ++i;
            if (i >= groups.size()) break;
            const auto name = groups[i].value.trim();
            ++i;
            if (name == "ENTITIES")
                readEntities (entities, "ENDSEC");
            else if (name == "TABLES")
            {
                while (i < groups.size() && ! (groups[i].code == 0 && groups[i].value.trim() == "ENDSEC"))
                {
                    if (groups[i].code == 0 && groups[i].value.trim() == "LAYER")
                    {
                        Ent l;
                        readEntity (l);
                        if (l.has (2))
                        {
                            int c = (int) l.num (62, 7);
                            if (((int) l.num (70) & 1) != 0) c = -std::abs (c);   // zmrazená vrstva
                            b.layerColour[l.str (2)] = c;
                        }
                    }
                    else ++i;
                }
            }
            else if (name == "BLOCKS")
            {
                while (i < groups.size() && ! (groups[i].code == 0 && groups[i].value.trim() == "ENDSEC"))
                {
                    if (groups[i].code == 0 && groups[i].value.trim() == "BLOCK")
                    {
                        Ent head;
                        readEntity (head);
                        Block blk;
                        blk.bx = head.num (10);
                        blk.by = head.num (20);
                        readEntities (blk.ents, "ENDBLK");
                        if (i < groups.size() && groups[i].value.trim() == "ENDBLK") { Ent end; readEntity (end); }
                        b.blocks[head.str (2)] = std::move (blk);
                    }
                    else ++i;
                }
            }
        }

        b.emitAll (entities, AffineTransform(), 7, 0);
        auto& d = b.d;
        d.layers = (int) b.layerColour.size();

        Rectangle<float> bounds;
        bool first = true;
        auto grow = [&] (Rectangle<float> r)
        {
            if (r.isEmpty() && r.getWidth() == 0 && r.getHeight() == 0 && r.getPosition().isOrigin()) return;
            bounds = first ? r : bounds.getUnion (r);
            first = false;
        };
        for (auto& [k, p] : d.lines) grow (p.getBounds());
        for (auto& [k, p] : d.fills) grow (p.getBounds());
        for (auto& t : d.texts) grow (Rectangle<float> (0, -t.h, t.h * 0.6f * (float) t.s.length(), t.h).transformedBy (t.t));
        d.bounds = bounds;
        if (first) d.error = String::fromUTF8 ("Výkres je prázdny alebo nepodporovaný");
        return std::make_shared<Drawing> (std::move (d));
    }

    // vykreslenie výkresu; view mapuje súradnice výkresu na obrazovku (os Y sa otáča)
    AffineTransform fitTransform (const Drawing& d, Rectangle<float> area)
    {
        if (d.bounds.isEmpty() && d.bounds.getWidth() <= 0 && d.bounds.getHeight() <= 0) return {};
        const float bw = jmax (1e-6f, d.bounds.getWidth()), bh = jmax (1e-6f, d.bounds.getHeight());
        const float s = jmin (area.getWidth() / bw, area.getHeight() / bh) * 0.94f;
        return AffineTransform::translation (-d.bounds.getCentreX(), -d.bounds.getCentreY())
                   .scaled (s, -s)
                   .translated (area.getCentreX(), area.getCentreY());
    }

    void renderDrawing (Graphics& g, const Drawing& d, const AffineTransform& view)
    {
        const float scale = std::sqrt (std::abs (view.getDeterminant()));
        for (auto& [argb, path] : d.fills)
        {
            Path p (path);
            p.applyTransform (view);
            g.setColour (Colour (argb));
            g.fillPath (p);
        }
        for (auto& [argb, path] : d.lines)
        {
            Path p (path);
            p.applyTransform (view);
            g.setColour (Colour (argb));
            g.strokePath (p, PathStrokeType (1.0f));
        }
        for (auto& t : d.texts)
        {
            if (t.h * scale < 3.0f) continue;   // príliš malé
            Graphics::ScopedSaveState ss (g);
            g.addTransform (t.t.followedBy (view));
            g.setColour (t.c);
            g.setFont (FontOptions (jmax (0.01f, t.h * 1.35f)));
            auto lines = StringArray::fromLines (t.s);
            for (int li = 0; li < lines.size(); ++li)
                g.drawSingleLineText (lines[li], 0, (int) std::round (t.h * 1.6f * (float) li));
        }
    }

    class CadView : public DocViewer
    {
    public:
        ~CadView() override
        {
            *alive = false;
            loader.removeAllJobs (true, 5000);
        }

        void load (const File& f) override
        {
            file = f;
            drawing.reset();
            userView = {};
            const int gen = ++generation;
            auto aliveFlag = alive;
            loader.removeAllJobs (false, 0);
            loader.addJob ([this, aliveFlag, gen, f]
            {
                auto d = loadDxf (f);
                MessageManager::callAsync ([this, aliveFlag, gen, d]
                {
                    if (! *aliveFlag || gen != generation) return;
                    drawing = d;
                    repaint();
                    infoChanged();
                });
            });
            repaint();
            infoChanged();
        }

        String infoText() const override
        {
            if (drawing == nullptr) return String::fromUTF8 ("Načítavam výkres… · ") + sizeText (file.getSize());
            if (drawing->error.isNotEmpty()) return drawing->error + String::fromUTF8 (" · ") + sizeText (file.getSize());
            auto f = [] (float v) { return String (v, v < 100 ? 2 : 0); };
            return String::fromUTF8 ("DXF · ") + f (drawing->bounds.getWidth()) + String::fromUTF8 (" × ") + f (drawing->bounds.getHeight())
                 + String::fromUTF8 (" · ") + String (drawing->entities) + String::fromUTF8 (" objektov · ") + String (drawing->layers)
                 + String::fromUTF8 (" vrstiev · ") + sizeText (file.getSize());
        }

        String hintText() const override { return String::fromUTF8 ("koliesko priblíženie · ťahanie posun · dvojklik celý výkres · Esc koniec"); }

        void paint (Graphics& g) override
        {
            g.fillAll (Colour (0xff12161c));
            if (drawing == nullptr) return;
            renderDrawing (g, *drawing, fitTransform (*drawing, getLocalBounds().toFloat()).followedBy (userView));
        }

        void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
        {
            const float f = std::pow (1.25f, jlimit (-3.0f, 3.0f, w.deltaY * 4.0f));
            userView = userView.followedBy (AffineTransform::scale (f, f, e.position.x, e.position.y));
            repaint();
        }

        void mouseDown (const MouseEvent& e) override { last = e.position; }

        void mouseDrag (const MouseEvent& e) override
        {
            const auto d = e.position - last;
            last = e.position;
            userView = userView.followedBy (AffineTransform::translation (d.x, d.y));
            repaint();
        }

        void mouseDoubleClick (const MouseEvent&) override { userView = {}; repaint(); }

        bool handleKey (const KeyPress& key) override
        {
            if (key.getKeyCode() == '0') { userView = {}; repaint(); return true; }
            return false;
        }

    private:
        File file;
        std::shared_ptr<Drawing> drawing;
        AffineTransform userView;
        Point<float> last;
        int generation = 0;
        ThreadPool loader { ThreadPoolOptions{}.withThreadName ("DXF").withNumberOfThreads (1) };
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };
}

std::unique_ptr<DocViewer> createCadViewer() { return std::make_unique<CadView>(); }

Image cadThumb (const File& f, int side)
{
    auto d = loadDxf (f);
    if (d->error.isNotEmpty()) return {};
    Image im (Image::RGB, side, side * 3 / 4, true, SoftwareImageType());
    Graphics g (im);
    g.fillAll (Colour (0xff12161c));
    renderDrawing (g, *d, fitTransform (*d, im.getBounds().toFloat()));
    return im;
}
}
