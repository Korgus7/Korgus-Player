#include "DocViewers.h"
#include <cmath>

namespace kp
{
namespace
{
    struct V3 { float x, y, z; };
    inline V3 operator- (V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline V3 cross (V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

    struct Mesh
    {
        std::vector<V3> v;               // 3 vrcholy na trojuholník
        V3 lo { 0, 0, 0 }, hi { 0, 0, 0 };
        String error;
        size_t triangles() const { return v.size() / 3; }

        void finish()
        {
            if (v.empty()) return;
            lo = hi = v[0];
            for (auto& p : v)
            {
                lo = { jmin (lo.x, p.x), jmin (lo.y, p.y), jmin (lo.z, p.z) };
                hi = { jmax (hi.x, p.x), jmax (hi.y, p.y), jmax (hi.z, p.z) };
            }
        }
    };

    // ---------------------------------------------------------------- načítanie
    bool loadStl (const MemoryBlock& mb, Mesh& m)
    {
        auto* d = static_cast<const uint8*> (mb.getData());
        const size_t n = mb.getSize();
        const bool looksText = n >= 5 && std::memcmp (d, "solid", 5) == 0;

        auto readBinary = [&] (uint32 count) -> bool
        {
            if (count == 0 || (size_t) count * 50 + 84 > n) return false;
            m.v.resize ((size_t) count * 3);
            for (uint32 i = 0; i < count; ++i)
                std::memcpy (&m.v[(size_t) i * 3], d + 84 + (size_t) i * 50 + 12, 36);
            return true;
        };

        uint32 count = 0;
        if (n >= 84) std::memcpy (&count, d + 80, 4);

        // binárne STL: počet sedí presne, alebo súbor má pár bajtov navyše / nezačína „solid“
        if (n >= 84 && (size_t) count * 50 + 84 == n && readBinary (count)) return true;
        if (n >= 84 && ! looksText && readBinary (count)) return true;

        // textové STL: riadky "vertex x y z"
        const String text (CharPointer_UTF8 (static_cast<const char*> (mb.getData())), mb.getSize());
        for (auto line : StringArray::fromLines (text))
        {
            line = line.trim();
            if (! line.startsWithIgnoreCase ("vertex")) continue;
            auto t = StringArray::fromTokens (line.substring (6), " \t", "");
            t.removeEmptyStrings();
            if (t.size() >= 3) m.v.push_back ({ t[0].getFloatValue(), t[1].getFloatValue(), t[2].getFloatValue() });
        }
        m.v.resize (m.v.size() / 3 * 3);
        if (! m.v.empty()) return true;

        // „solid“ na začiatku, ale v skutočnosti binárne (časté pri niektorých exportoch)
        if (n >= 84 && readBinary (count)) return true;
        // počet v hlavičke chybný: odvodiť z veľkosti
        if (n > 84) return readBinary ((uint32) ((n - 84) / 50));
        return false;
    }

    bool loadObj (const MemoryBlock& mb, Mesh& m)
    {
        std::vector<V3> pts;
        const String text (CharPointer_UTF8 (static_cast<const char*> (mb.getData())), mb.getSize());
        for (auto line : StringArray::fromLines (text))
        {
            line = line.trimStart();
            if (line.startsWith ("v "))
            {
                auto t = StringArray::fromTokens (line.substring (2), " \t", "");
                t.removeEmptyStrings();
                if (t.size() >= 3) pts.push_back ({ t[0].getFloatValue(), t[1].getFloatValue(), t[2].getFloatValue() });
            }
            else if (line.startsWith ("f "))
            {
                auto t = StringArray::fromTokens (line.substring (2), " \t", "");
                t.removeEmptyStrings();
                std::vector<int> idx;
                for (auto& s : t)
                {
                    int i = s.upToFirstOccurrenceOf ("/", false, false).getIntValue();
                    if (i < 0) i = (int) pts.size() + i + 1;     // záporné indexy
                    if (i >= 1 && i <= (int) pts.size()) idx.push_back (i - 1);
                }
                for (size_t k = 1; k + 1 < idx.size(); ++k)       // mnohouholník na trojuholníky
                {
                    m.v.push_back (pts[(size_t) idx[0]]);
                    m.v.push_back (pts[(size_t) idx[k]]);
                    m.v.push_back (pts[(size_t) idx[k + 1]]);
                }
            }
        }
        return ! m.v.empty();
    }

    bool load3mf (const File& f, Mesh& m)
    {
        ZipFile zip (f);
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            auto* e = zip.getEntry (i);
            if (! e->filename.endsWithIgnoreCase (".model")) continue;
            std::unique_ptr<InputStream> in (zip.createStreamForEntry (i));
            if (in == nullptr) continue;
            auto xml = parseXML (in->readEntireStreamAsString());
            if (xml == nullptr) continue;

            // všetky siete vo všetkých objektoch
            std::function<void (XmlElement&)> walk = [&] (XmlElement& el)
            {
                if (el.getTagNameWithoutNamespace() == "mesh")
                {
                    std::vector<V3> pts;
                    auto child = [] (XmlElement& parent, const char* name) -> XmlElement*
                    {
                        for (auto* c : parent.getChildIterator())
                            if (c->getTagNameWithoutNamespace() == name) return c;
                        return nullptr;
                    };
                    if (auto* verts = child (el, "vertices"))
                        for (auto* vx : verts->getChildIterator())
                            pts.push_back ({ (float) vx->getDoubleAttribute ("x"), (float) vx->getDoubleAttribute ("y"), (float) vx->getDoubleAttribute ("z") });
                    if (auto* tris = child (el, "triangles"))
                        for (auto* t : tris->getChildIterator())
                        {
                            const int a = t->getIntAttribute ("v1", -1), b = t->getIntAttribute ("v2", -1), c = t->getIntAttribute ("v3", -1);
                            const int n = (int) pts.size();
                            if (a >= 0 && b >= 0 && c >= 0 && a < n && b < n && c < n)
                            {
                                m.v.push_back (pts[(size_t) a]);
                                m.v.push_back (pts[(size_t) b]);
                                m.v.push_back (pts[(size_t) c]);
                            }
                        }
                    return;
                }
                for (auto* c : el.getChildIterator()) walk (*c);
            };
            walk (*xml);
        }
        return ! m.v.empty();
    }

    std::shared_ptr<Mesh> loadMesh (const File& f)
    {
        auto m = std::make_shared<Mesh>();
        const auto e = f.getFileExtension().toLowerCase();
        bool ok = false;
        if (e == ".3mf") ok = load3mf (f, *m);
        else
        {
            MemoryBlock mb;
            if (f.loadFileAsData (mb))
                ok = e == ".obj" ? loadObj (mb, *m) : loadStl (mb, *m);
        }
        if (! ok) m->error = String::fromUTF8 ("Model sa nedá načítať");
        m->finish();
        return m;
    }

    // ---------------------------------------------------------------- vykreslenie (softvérovo, so z-bufferom)
    struct ViewParams { float yaw = -0.6f, pitch = 0.55f, zoom = 1.0f, panX = 0, panY = 0; };

    Image renderMesh (const Mesh& m, int w, int h, const ViewParams& vp, bool light = false)
    {
        Image im (Image::RGB, jmax (1, w), jmax (1, h), false, SoftwareImageType());
        {
            Graphics g (im);
            g.setGradientFill (ColourGradient (light ? Colour (0xfff2f4f7) : Colour (0xff222b37), 0, 0,
                                               light ? Colour (0xffd5dbe3) : Colour (0xff12161c), 0, (float) h, false));
            g.fillAll();
        }
        if (m.v.empty()) return im;

        const V3 c { (m.lo.x + m.hi.x) / 2, (m.lo.y + m.hi.y) / 2, (m.lo.z + m.hi.z) / 2 };
        const V3 size = m.hi - m.lo;
        const float diag = jmax (1e-6f, std::sqrt (size.x * size.x + size.y * size.y + size.z * size.z));
        const float scale = (float) jmin (w, h) * 0.9f / diag * vp.zoom;

        // Z hore (ako pri 3D tlači): otočenie okolo Z (yaw), potom náklon (pitch)
        const float cy = std::cos (vp.yaw), sy = std::sin (vp.yaw), cp = std::cos (vp.pitch), sp = std::sin (vp.pitch);
        auto xf = [&] (V3 p) -> V3
        {
            p = p - c;
            const float x1 = p.x * cy - p.y * sy, y1 = p.x * sy + p.y * cy, z1 = p.z;
            // obrazovka: x vpravo, y hore, z k divákovi
            const float sx = x1, syy = z1 * cp - y1 * sp, sz = z1 * sp + y1 * cp;
            return { (float) w * 0.5f + vp.panX + sx * scale, (float) h * 0.5f + vp.panY - syy * scale, -sz * scale };
        };

        std::vector<float> zb ((size_t) w * (size_t) h, -1e30f);
        Image::BitmapData bd (im, Image::BitmapData::readWrite);
        const V3 lightDir { -0.35f, 0.55f, 0.76f };
        const Colour base = light ? Colour (0xff5d86b0) : Colour (0xff8fb1d6);

        const size_t tris = m.triangles();
        for (size_t t = 0; t < tris; ++t)
        {
            const V3 a = xf (m.v[t * 3]), b = xf (m.v[t * 3 + 1]), d = xf (m.v[t * 3 + 2]);
            // normála v priestore obrazovky (y je otočené)
            V3 n = cross ({ b.x - a.x, -(b.y - a.y), b.z - a.z }, { d.x - a.x, -(d.y - a.y), d.z - a.z });
            const float len = std::sqrt (n.x * n.x + n.y * n.y + n.z * n.z);
            if (len < 1e-12f) continue;
            n = { n.x / len, n.y / len, n.z / len };
            const float diff = std::abs (n.x * lightDir.x + n.y * lightDir.y + n.z * lightDir.z);   // obojstranné
            const float shade = 0.28f + 0.72f * diff;
            const auto colr = base.withMultipliedBrightness (shade);
            const PixelRGB px = [&] { PixelRGB p; p.setARGB (255, colr.getRed(), colr.getGreen(), colr.getBlue()); return p; }();

            const int x0 = jmax (0, (int) std::floor (jmin (a.x, b.x, d.x))), x1 = jmin (w - 1, (int) std::ceil (jmax (a.x, b.x, d.x)));
            const int y0 = jmax (0, (int) std::floor (jmin (a.y, b.y, d.y))), y1 = jmin (h - 1, (int) std::ceil (jmax (a.y, b.y, d.y)));
            if (x0 > x1 || y0 > y1) continue;
            const float area = (b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x);
            if (std::abs (area) < 1e-9f)
            {
                continue;
            }
            const float inv = 1.0f / area;
            for (int y = y0; y <= y1; ++y)
            {
                const float py = (float) y + 0.5f;
                auto* line = reinterpret_cast<PixelRGB*> (bd.getLinePointer (y));
                for (int x = x0; x <= x1; ++x)
                {
                    const float px2 = (float) x + 0.5f;
                    const float w0 = ((b.x - px2) * (d.y - py) - (b.y - py) * (d.x - px2)) * inv;
                    const float w1 = ((d.x - px2) * (a.y - py) - (d.y - py) * (a.x - px2)) * inv;
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    const float z = w0 * a.z + w1 * b.z + w2 * d.z;
                    float& zz = zb[(size_t) y * (size_t) w + (size_t) x];
                    if (z <= zz) continue;
                    zz = z;
                    line[x] = px;
                }
            }
        }
        return im;
    }

    String sizeMm (const Mesh& m)
    {
        const V3 s = m.hi - m.lo;
        auto f = [] (float v) { return String (v, v < 10 ? 2 : 1); };
        return f (s.x) + String::fromUTF8 (" × ") + f (s.y) + String::fromUTF8 (" × ") + f (s.z) + " mm";
    }

    class ModelView : public DocViewer
    {
    public:
        ~ModelView() override
        {
            *alive = false;
            loader.removeAllJobs (true, 5000);
        }

        void load (const File& f) override
        {
            file = f;
            mesh.reset();
            vp = {};
            cached = {};
            const int gen = ++generation;
            auto aliveFlag = alive;
            loader.removeAllJobs (false, 0);
            loader.addJob ([this, aliveFlag, gen, f]
            {
                auto m = loadMesh (f);
                MessageManager::callAsync ([this, aliveFlag, gen, m]
                {
                    if (! *aliveFlag || gen != generation) return;
                    mesh = m;
                    cached = {};
                    repaint();
                    infoChanged();
                });
            });
            repaint();
            infoChanged();
        }

        String infoText() const override
        {
            if (mesh == nullptr) return String::fromUTF8 ("Načítavam model… · ") + sizeText (file.getSize());
            if (mesh->error.isNotEmpty()) return mesh->error + String::fromUTF8 (" · ") + sizeText (file.getSize());
            return sizeMm (*mesh) + String::fromUTF8 (" · ") + String ((int64) mesh->triangles()) + String::fromUTF8 (" trojuholníkov · ")
                 + sizeText (file.getSize());
        }

        String hintText() const override { return String::fromUTF8 ("ťahanie otáča · pravé tlačidlo posúva · koliesko priblíženie · dvojklik pôvodný pohľad · Esc koniec"); }

        void paint (Graphics& g) override
        {
            const float scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
            const bool fast = dragging;
            const int div = fast && mesh != nullptr && mesh->triangles() > 200000 ? 2 : 1;
            const int w = jmax (1, (int) ((float) getWidth() * scale) / div), h = jmax (1, (int) ((float) getHeight() * scale) / div);
            if (mesh == nullptr)
            {
                g.fillAll (col::bg);
                g.setColour (col::textDim);
                g.drawText (String::fromUTF8 ("Načítavam…"), getLocalBounds(), Justification::centred);
                return;
            }
            if (! cached.isValid() || cached.getWidth() != w || cached.getHeight() != h || cachedParams)
            {
                ViewParams p = vp;
                p.panX *= scale / (float) div;
                p.panY *= scale / (float) div;
                cached = renderMesh (*mesh, w, h, p);
                cachedParams = false;
            }
            g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
            g.drawImage (cached, getLocalBounds().toFloat());
            if (mesh->error.isNotEmpty())
            {
                g.setColour (col::textDim);
                g.drawText (mesh->error, getLocalBounds(), Justification::centred);
            }
        }

        void mouseDown (const MouseEvent& e) override { last = e.position; dragging = true; }
        void mouseUp (const MouseEvent&) override { dragging = false; cachedParams = true; repaint(); }

        void mouseDrag (const MouseEvent& e) override
        {
            const auto d = e.position - last;
            last = e.position;
            if (e.mods.isPopupMenu() || e.mods.isShiftDown()) { vp.panX += d.x; vp.panY += d.y; }
            else
            {
                vp.yaw += d.x * 0.01f;
                vp.pitch = jlimit (-1.55f, 1.55f, vp.pitch + d.y * 0.01f);
            }
            cachedParams = true;
            repaint();
        }

        void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
        {
            vp.zoom = jlimit (0.1f, 50.0f, vp.zoom * std::pow (1.25f, jlimit (-3.0f, 3.0f, w.deltaY * 4.0f)));
            cachedParams = true;
            repaint();
        }

        void mouseDoubleClick (const MouseEvent&) override { vp = {}; cachedParams = true; repaint(); }

        bool handleKey (const KeyPress& key) override
        {
            const int k = key.getKeyCode();
            if (k == '0') { vp = {}; cachedParams = true; repaint(); return true; }
            return false;
        }

    private:
        File file;
        std::shared_ptr<Mesh> mesh;
        ViewParams vp;
        Image cached;
        bool cachedParams = false, dragging = false;
        Point<float> last;
        int generation = 0;
        ThreadPool loader { ThreadPoolOptions{}.withThreadName ("Model").withNumberOfThreads (1) };
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };
}

std::unique_ptr<DocViewer> createModelViewer() { return std::make_unique<ModelView>(); }

Image modelThumb (const File& f, int side)
{
    auto m = loadMesh (f);
    if (m->v.empty()) return {};
    return renderMesh (*m, side, side, {}, true);
}
}
