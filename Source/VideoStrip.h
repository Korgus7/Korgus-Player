#pragma once
#include "Common.h"
#include <map>
#include <atomic>
#include <mutex>

namespace kp
{
// Filmový pás pod videom (ako vlna pri zvuku): náhľady snímok v pozadí cez samostatný libmpv bez okna,
// výber úseku s úchytmi, priblíženie kolieskom a prehľad celého videa pri priblížení.
class VideoThumbs : private Thread
{
public:
    VideoThumbs();
    ~VideoThumbs() override;
    void setFile (const File& f);
    // požadované časy (sekundy); exact = presná snímka (pomalšie), inak najbližšia kľúčová
    void request (const std::vector<double>& times, bool exact);
    // náhľad blízko času t (do tol sekúnd), alebo prázdny obrázok
    Image get (double t, double tol) const;
    std::function<void()> onNew;   // volá sa na hlavnom vlákne
    static bool available();

private:
    void run() override;
    mutable std::mutex lock;
    File file, loadedFile;
    std::vector<double> pending;
    bool pendingExact = false;
    std::atomic<int> generation { 0 };
    std::map<int64, Image> cache;      // kľúč = čas v ms
    std::vector<int64> order;          // poradie pridania (na vyhadzovanie)
    WaitableEvent wake;
    bool notifyQueued = false;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

class VideoStrip : public Component, private Timer
{
public:
    VideoStrip();
    void setFile (const File& f);
    void setDuration (double d);
    void setPosition (double t);
    bool hasSelection() const { return selA >= 0 && selB > selA; }
    double getSelStart() const { return selA; }
    double getSelEnd() const { return selB; }
    void clearSelection();

    std::function<void (double)> onSeek;
    std::function<void()> onSelectionChanged;

    void paint (Graphics& g) override;
    void resized() override { requestThumbs(); }
    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;
    void mouseUp (const MouseEvent& e) override;
    void mouseMove (const MouseEvent& e) override;
    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override;
    void mouseDoubleClick (const MouseEvent& e) override;
    // pre test: počet hotových náhľadov vo viditeľnom úseku
    int readyThumbs() const;
    void setViewForTest (double a, double b) { setView (a, b); }

    static constexpr int stripH = 76;

private:
    void timerCallback() override;
    Rectangle<int> tilesArea() const { return getLocalBounds().reduced (6, 4).withTrimmedBottom (14); }
    Rectangle<int> overviewArea() const { return getLocalBounds().reduced (6, 2).removeFromBottom (12); }
    double xToTime (int x) const;
    int timeToX (double t) const;
    void setView (double a, double b);
    int edgeAt (Point<int> p) const;
    void requestThumbs();
    int tileCount() const;

    VideoThumbs thumbs;
    File file;
    double duration = 0, position = 0;
    double viewA = 0, viewB = 0;
    double selA = -1, selB = -1;
    bool selecting = false, draggingView = false;
    int dragEdge = -1;
    double dragT0 = 0;
    bool wasSeekClick = false;
};

// dĺžka videa v sekundách (0 = nedá sa zistiť) – na test z príkazového riadku
double probeVideoDuration (const File& f);

// Uloženie výrezu videa (v pozadí, samostatný libmpv v režime kódovania): H.264 + AAC do MP4, presne na snímku.
namespace videocut
{
    // progress 0..1 na hlavnom vlákne; done (výsledný súbor alebo prázdny pri chybe, správa)
    void start (const File& in, double a, double b,
                std::function<void (float)> progress, std::function<void (const File&, const String&)> done);
    bool busy();
    // synchronne (na test z príkazového riadku)
    File run (const File& in, double a, double b, std::function<void (float)> progress, String& message);
}
}
