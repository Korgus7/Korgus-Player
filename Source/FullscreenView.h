#pragma once
#include "Common.h"
#include "ImageCache.h"
#include "ThumbCache.h"

namespace kp
{
// Obrázok na celú obrazovku. Šípky listujú, Esc zavrie, 1–9 a Ctrl+Z rieši MainComponent.
class FullscreenView : public Component, private Timer
{
public:
    FullscreenView (ImageCache& images, ThumbCache& thumbs);

    void setFile (const File& f, int position, int total);
    void imageLoaded (const String& path);
    void showMessage (const String& m, bool error = false);

    std::function<void (int)> onStep;            // -1 / +1, alebo ±100000 = prvý / posledný
    std::function<void()> onClose;
    std::function<bool (const KeyPress&)> onOtherKey;

    void paint (Graphics& g) override;
    bool keyPressed (const KeyPress& key) override;
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override;
    void mouseDoubleClick (const MouseEvent&) override { if (onClose) onClose(); }

private:
    void timerCallback() override { message = {}; stopTimer(); repaint(); }

    ImageCache& images;
    ThumbCache& thumbs;
    File file;
    std::shared_ptr<ImageCache::Entry> shown;
    int position = 0, total = 0;
    String message;
    bool messageIsError = false;
    double lastWheel = 0;
};
}
