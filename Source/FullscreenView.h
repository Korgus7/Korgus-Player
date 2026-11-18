#pragma once
#include "Common.h"
#include "ImageCache.h"
#include "ThumbCache.h"
#include "Icons.h"

namespace kp
{
// Obrázok na celú obrazovku. Šípky listujú, Esc zavrie, 1–9 a Ctrl+Z rieši MainComponent.
class FullscreenView : public Component, private Timer
{
public:
    FullscreenView (ImageCache& images, ThumbCache& thumbs);

    void setFile (const File& f, int position, int total);
    void imageLoaded (const String& path);
    void refresh();   // po úprave súboru (otočenie)
    void showMessage (const String& m, bool error = false);

    std::function<void (int)> onStep;            // -1 / +1, alebo ±100000 = prvý / posledný
    std::function<void()> onClose;
    std::function<bool (const KeyPress&)> onOtherKey;
    std::function<void (bool clockwise)> onRotate;
    std::function<void()> onPickMusic;

    // prezentácia
    void setSlideshow (bool on);
    bool isSlideshow() const { return slideshow; }
    bool isShuffle() const { return shuffle; }

    void paint (Graphics& g) override;
    void resized() override;
    bool keyPressed (const KeyPress& key) override;
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override;
    void mouseDoubleClick (const MouseEvent& e) override;
    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;

private:
    void timerCallback() override { message = {}; stopTimer(); repaint(); }

    struct Ticker : Timer { std::function<void()> fn; void timerCallback() override { if (fn) fn(); } };
    Ticker slideTimer, fadeTimer;
    bool slideshow = false, shuffle = false;
    static inline int intervalSec = 4;
    Image fadeFrom;
    double fadeStart = 0;
    void changeInterval (int dir);
    void updateButtons();

    IconButton rotL { Icon::RotateLeft, String::fromUTF8 ("Otočiť doľava (L)") },
               rotR { Icon::RotateRight, String::fromUTF8 ("Otočiť doprava (R)") },
               showB { Icon::Slideshow, String::fromUTF8 ("Prezentácia (F5)"), String::fromUTF8 ("Prezentácia") },
               shufB { Icon::Shuffle, String::fromUTF8 ("Náhodné poradie") },
               slowerB { Icon::Left, String::fromUTF8 ("Kratší interval (↓)") },
               fasterB { Icon::Right, String::fromUTF8 ("Dlhší interval (↑)") },
               fitB { Icon::Fit, String::fromUTF8 ("Veľkosť obrázka: celý / na celú obrazovku / pôvodná (F)"), String::fromUTF8 ("Celý") };
    TextButton intervalB;
    IconButton musicB { Icon::Volume, String::fromUTF8 ("Hudba pod prezentáciu – vybrať priečinok"), String::fromUTF8 ("Hudba") };
    static inline int fitMode = 0;   // 0 = celý obrázok (aj zväčšiť), 1 = vyplniť obrazovku, 2 = pôvodná veľkosť
    void cycleFit();

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
