#pragma once
#include "Common.h"
#include "ThumbCache.h"
#include "ImageLoader.h"
#include <memory>
#include <deque>

namespace kp
{
// Pravý panel: veľký obrázok, alebo vlna zvuku s prehrávaním.
class PreviewPane : public Component,
                    private ChangeListener,
                    private Timer
{
public:
    explicit PreviewPane (ThumbCache& thumbs);
    ~PreviewPane() override;

    void show (const File& f);
    void prefetch (const Array<File>& files);   // susedné obrázky dopredu
    void release (const File& f);               // pustí súbor (pred presunom)
    void togglePlay();
    bool isAudio() const { return mode == Mode::Audio; }

    void paint (Graphics& g) override;
    void resized() override {}
    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;

private:
    enum class Mode { None, Image, Audio, Other };

    void changeListenerCallback (ChangeBroadcaster*) override;
    void timerCallback() override;
    void requestImage (const File& f, bool show);
    void imageArrived (const String& path, const img::Loaded& loaded);
    void startAudio (const File& f);
    void stopAudio();
    bool ensureAudioDevice();
    Rectangle<int> waveArea() const;
    Rectangle<int> infoArea() const;
    void seekFromMouse (int x);

    ThumbCache& thumbs;
    File current;
    Mode mode = Mode::None;
    String infoLine;

    // obrázky
    struct Cached { img::Loaded loaded; Image display; };
    ThreadPool loader { ThreadPoolOptions{}.withThreadName ("Obrazky").withNumberOfThreads (2) };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    HashMap<String, std::shared_ptr<Cached>> images;
    std::deque<String> imageOrder;
    HashMap<String, bool> loading;
    std::shared_ptr<Cached> shown;
    int maxSide = 2560;

    // zvuk
    AudioFormatManager formats;
    AudioDeviceManager deviceManager;
    bool deviceReady = false, deviceFailed = false;
    AudioSourcePlayer sourcePlayer;
    AudioTransportSource transport;
    std::unique_ptr<AudioFormatReaderSource> readerSource;
    TimeSliceThread readThread { "Citanie zvuku" };
    AudioThumbnailCache waveCache { 8 };
    AudioThumbnail wave { 512, formats, waveCache };
    double audioLength = 0.0;
};
}
