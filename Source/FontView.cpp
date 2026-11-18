#include "DocViewers.h"

namespace kp
{
namespace
{
    Typeface::Ptr loadTypeface (const File& f)
    {
        MemoryBlock mb;
        if (! f.loadFileAsData (mb) || mb.getSize() < 12) return nullptr;
        return Typeface::createSystemTypefaceFor (mb.getData(), mb.getSize());
    }

    const char* const pangram = "Kŕdeľ šťastných ďatľov učí pri ústí Váhu mĺkveho koňa obhrýzať kôru a žrať čerstvé mäso.";

    class FontView : public DocViewer
    {
    public:
        void load (const File& f) override
        {
            file = f;
            face = loadTypeface (f);
            repaint();
            infoChanged();
        }

        String infoText() const override
        {
            if (face == nullptr) return String::fromUTF8 ("Font sa nedá načítať · ") + sizeText (file.getSize());
            return face->getName() + String::fromUTF8 (" · ") + face->getStyle() + String::fromUTF8 (" · ") + sizeText (file.getSize());
        }

        void paint (Graphics& g) override
        {
            g.fillAll (col::bg);
            if (face == nullptr) return;
            auto r = getLocalBounds().reduced (16, 12);
            auto font = [this] (float h) { return Font (FontOptions (face).withHeight (h)); };

            g.setColour (col::text);
            const float big = jlimit (40.0f, 140.0f, (float) r.getHeight() * 0.22f);
            g.setFont (font (big));
            g.drawFittedText ("Aa Bb Cc", r.removeFromTop ((int) (big * 1.25f)), Justification::centredLeft, 1, 0.4f);

            g.setColour (col::textDim);
            g.setFont (font (18.0f));
            g.drawText (String::fromUTF8 ("ABCDEFGHIJKLMNOPQRSTUVWXYZ ÁČĎÉÍĽĹŇÓÔŔŠŤÚÝŽ"), r.removeFromTop (26), Justification::centredLeft, true);
            g.drawText (String::fromUTF8 ("abcdefghijklmnopqrstuvwxyz áäčďéíľĺňóôŕšťúýž"), r.removeFromTop (26), Justification::centredLeft, true);
            g.drawText (String::fromUTF8 ("0123456789 .,:;!?&@€$%()[]{}/*+-=\"'"), r.removeFromTop (30), Justification::centredLeft, true);

            g.setColour (col::text);
            for (float h : { 12.0f, 16.0f, 22.0f, 30.0f, 42.0f })
            {
                if (r.getHeight() < (int) h) break;
                g.setFont (font (h));
                const auto line = r.removeFromTop ((int) (h * 1.5f));
                g.drawText (String::fromUTF8 (pangram), line, Justification::centredLeft, true);
            }
        }

    private:
        File file;
        Typeface::Ptr face;
    };
}

std::unique_ptr<DocViewer> createFontViewer() { return std::make_unique<FontView>(); }

Image fontThumb (const File& f, int side)
{
    // písma kreslíme na hlavnom vlákne (načítanie fontu nemusí byť bezpečné v iných vláknach)
    struct State { Image result; WaitableEvent done; };
    auto st = std::make_shared<State>();
    auto job = [st, f, side]
    {
        if (auto face = loadTypeface (f))
        {
            Image im (Image::RGB, side, side * 3 / 4, true, SoftwareImageType());
            Graphics g (im);
            g.fillAll (Colour (0xfff4f1ec));
            g.setColour (Colour (0xff1d242e));
            g.setFont (Font (FontOptions (face).withHeight ((float) side * 0.42f)));
            g.drawText ("Aa", im.getBounds().withTrimmedBottom (side / 7), Justification::centred, false);
            g.setFont (Font (FontOptions (face).withHeight ((float) side * 0.09f)));
            g.drawText (face->getName(), im.getBounds().removeFromBottom (side / 5), Justification::centred, true);
            st->result = im;
        }
        st->done.signal();
    };
    if (MessageManager::getInstance()->isThisTheMessageThread()) job();
    else
    {
        MessageManager::callAsync (job);
        if (! st->done.wait (3000)) return {};
    }
    return st->result;
}
}
