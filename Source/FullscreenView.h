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
    void mouseDoubleClick (const MouseEvent& e) override;
    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;

private:
    void timerCallback() override { message = {}; stopTimer(); repaint(); }

    ImageCache& images;
    ThumbCache& thumbs;
    File file;
    std::shared_ptr<ImageCache::Entry> shown;
    int position = 0, total = 0;
    String message;
    bool messageIsError = false;
    // priblíženie (0 = celý obrázok na obrazovku)
    float zoom = 0.0f;
    Point<float> centre;          // stred pohľadu v pixeloch originálu
    Point<float> dragStartCentre;
    Image full;                   // plné rozlíšenie pri veľkom priblížení
    String fullPath;
    bool fullLoading = false;
    ThreadPool fullLoader { ThreadPoolOptions{}.withThreadName ("Plne rozlisenie").withNumberOfThreads (1) };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    Point<float> originalSize() const;
    float fitScale() const;
    float currentScale() const { return zoom > 0.0f ? zoom : fitScale(); }
    void zoomAt (float newScale, Point<float> screenPos);
    void clampCentre();
    void requestFull();

public:
    ~FullscreenView() override;
};
}
