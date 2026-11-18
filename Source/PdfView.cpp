#include "DocViewers.h"
#include "WinPdf.h"

namespace kp
{
namespace
{
    Image decodePng (const std::vector<uint8_t>& png)
    {
        if (png.empty()) return {};
        MemoryInputStream mis (png.data(), png.size(), false);
        auto im = ImageFileFormat::loadFrom (mis);
        return im.isValid() ? SoftwareImageType().convert (im) : im;
    }

    class PdfView : public DocViewer
    {
    public:
        ~PdfView() override
        {
            *alive = false;
            loader.removeAllJobs (true, 5000);
        }

        void load (const File& f) override
        {
            file = f;
            page = 0;
            pageCount = 0;
            failed = false;
            image = {};
            request();
            infoChanged();
        }

        String infoText() const override
        {
            if (failed) return String::fromUTF8 ("PDF sa nedá zobraziť (potrebuje Windows 10 alebo novší) · ") + sizeText (file.getSize());
            String s = String::fromUTF8 ("PDF · ");
            if (pageCount > 0) s << pageCount << String::fromUTF8 (pageCount == 1 ? " strana" : (pageCount < 5 ? " strany" : " strán")) << String::fromUTF8 (" · ");
            if (pageW > 0) s << roundToInt (pageW * 25.4 / 96.0) << String::fromUTF8 (" × ") << roundToInt (pageH * 25.4 / 96.0) << " mm · ";
            return s + sizeText (file.getSize());
        }

        String hintText() const override { return String::fromUTF8 ("↑ ↓ strany · ← → ďalší súbor · Esc koniec"); }

        bool handleKey (const KeyPress& key) override
        {
            const int k = key.getKeyCode();
            if (k == KeyPress::upKey || k == KeyPress::pageUpKey) { setPage (page - 1); return true; }
            if (k == KeyPress::downKey || k == KeyPress::pageDownKey || k == KeyPress::spaceKey) { setPage (page + 1); return true; }
            if (k == KeyPress::homeKey) { setPage (0); return true; }
            if (k == KeyPress::endKey) { setPage (pageCount - 1); return true; }
            return false;
        }

        void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
        {
            const double now = Time::getMillisecondCounterHiRes();
            if (std::abs (w.deltaY) < 0.01f || now - lastWheel < 150.0) return;
            lastWheel = now;
            setPage (page + (w.deltaY < 0 ? 1 : -1));
        }

        void mouseDown (const MouseEvent& e) override
        {
            auto bar = barArea();
            if (! bar.contains (e.getPosition())) return;
            if (e.x < bar.getCentreX()) setPage (page - 1);
            else setPage (page + 1);
        }

        void resized() override
        {
            const int w = renderWidth();
            if (image.isValid() && std::abs (w - image.getWidth()) > image.getWidth() / 3) request();
        }

        void paint (Graphics& g) override
        {
            g.fillAll (col::bg);
            auto area = getLocalBounds().withTrimmedBottom (barH).reduced (8);
            if (image.isValid())
            {
                auto fit = RectanglePlacement (RectanglePlacement::centred).appliedTo (image.getBounds().toFloat(), area.toFloat());
                g.setColour (Colours::black.withAlpha (0.35f));
                g.fillRect (fit.translated (3, 3));
                g.setImageResamplingQuality (Graphics::highResamplingQuality);
                g.drawImage (image, fit);
            }
            else
            {
                g.setColour (col::textDim);
                g.setFont (FontOptions (14.0f));
                g.drawText (failed ? String::fromUTF8 ("PDF sa nedá zobraziť") : String::fromUTF8 ("Načítavam…"), area, Justification::centred);
            }

            auto bar = barArea();
            g.setColour (col::panel);
            g.fillRect (bar);
            g.setColour (col::text);
            g.setFont (FontOptions (13.0f));
            if (pageCount > 0)
            {
                g.drawText (String::fromUTF8 ("◀"), bar.removeFromLeft (bar.getWidth() / 2).withTrimmedRight (60), Justification::centredRight);
                g.drawText (String::fromUTF8 ("▶"), bar.withTrimmedLeft (60), Justification::centredLeft);
                g.drawText (String::fromUTF8 ("Strana ") + String (page + 1) + " / " + String (pageCount), barArea(), Justification::centred);
            }
        }

    private:
        Rectangle<int> barArea() const { return getLocalBounds().removeFromBottom (barH); }

        int renderWidth() const
        {
            const float scale = (float) Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale;
            return jlimit (400, 2600, (int) ((float) getWidth() * scale));
        }

        void setPage (int p)
        {
            if (pageCount <= 0) return;
            p = jlimit (0, pageCount - 1, p);
            if (p == page) return;
            page = p;
            request();
            repaint();
        }

        void request()
        {
            const int gen = ++generation;
            auto aliveFlag = alive;
            const auto path = std::wstring (file.getFullPathName().toWideCharPointer());
            const int pg = page, width = renderWidth();
            loader.removeAllJobs (false, 0);
            loader.addJob ([this, aliveFlag, gen, path, pg, width]
            {
                pdf::Page res;
                const bool ok = pdf::render (path, pg, width, res);
                auto im = ok ? decodePng (res.png) : Image();
                MessageManager::callAsync ([this, aliveFlag, gen, ok, im, res]
                {
                    if (! *aliveFlag || gen != generation) return;
                    failed = ! ok;
                    image = im;
                    pageCount = res.pageCount;
                    pageW = res.width;
                    pageH = res.height;
                    repaint();
                    infoChanged();
                });
            });
        }

        File file;
        int page = 0, pageCount = 0, generation = 0;
        double pageW = 0, pageH = 0, lastWheel = 0;
        bool failed = false;
        Image image;
        ThreadPool loader { ThreadPoolOptions{}.withThreadName ("PDF").withNumberOfThreads (1) };
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
        static constexpr int barH = 28;
    };
}

std::unique_ptr<DocViewer> createPdfViewer() { return std::make_unique<PdfView>(); }

Image pdfThumb (const File& f, int side)
{
    pdf::Page res;
    if (! pdf::render (std::wstring (f.getFullPathName().toWideCharPointer()), 0, side, res)) return {};
    return decodePng (res.png);
}
}
