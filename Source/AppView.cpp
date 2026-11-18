#include "DocViewers.h"
#include "WinImaging.h"

namespace kp
{
namespace
{
    class AppView : public DocViewer
    {
    public:
        void load (const File& f) override
        {
            file = f;
            icon = win::shellIcon (f, 256);
            info = win::versionInfo (f);
            repaint();
            infoChanged();
        }

        String infoText() const override
        {
            String s;
            if (info["FileVersion"].isNotEmpty()) s << "Verzia " << info["FileVersion"] << tr (" · ");
            return s + sizeText (file.getSize());
        }

        void paint (Graphics& g) override
        {
            g.fillAll (col::bg);
            auto r = getLocalBounds().reduced (20);
            auto top = r.removeFromTop (jmin (160, r.getHeight() / 2));
            if (icon.isValid())
                g.drawImage (icon, RectanglePlacement (RectanglePlacement::centred | RectanglePlacement::onlyReduceInSize)
                                       .appliedTo (icon.getBounds().toFloat(), top.toFloat()));
            r.removeFromTop (14);
            struct Row { const char* label; String value; };
            const Row rows[] = {
                { "Názov", info["ProductName"] },
                { "Popis", info["FileDescription"] },
                { "Výrobca", info["CompanyName"] },
                { "Verzia", info["ProductVersion"].isNotEmpty() ? info["ProductVersion"] : info["FileVersion"] },
                { "Autorské práva", info["LegalCopyright"] },
                { "Zmenené", file.getLastModificationTime().formatted ("%d.%m.%Y %H:%M") },
                { "Veľkosť", sizeText (file.getSize()) },
            };
            for (auto& row : rows)
            {
                if (row.value.isEmpty()) continue;
                auto line = r.removeFromTop (24);
                g.setColour (col::textDim);
                g.setFont (FontOptions (13.0f));
                g.drawText (tr (row.label), line.removeFromLeft (130), Justification::centredLeft, false);
                g.setColour (col::text);
                g.drawText (row.value, line, Justification::centredLeft, true);
            }
            r.removeFromTop (10);
            g.setColour (col::textDim);
            g.setFont (FontOptions (12.0f));
            if (file.hasFileExtension ("exe;msi"))
                g.drawFittedText (tr ("Enter program spustí. Spúšťaj len programy z dôveryhodných zdrojov."),
                                  r.removeFromTop (36), Justification::topLeft, 2);
        }

    private:
        File file;
        Image icon;
        StringPairArray info;
    };
}

std::unique_ptr<DocViewer> createAppViewer() { return std::make_unique<AppView>(); }

Image appThumb (const File& f, int side)
{
    return win::shellIcon (f, side);
}
}
