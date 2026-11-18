#pragma once
#include "Common.h"
#include "ThumbCache.h"
#include "ImageCache.h"
#include "MidiPlayer.h"
#include "VideoView.h"
#include "DocViewers.h"
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
    PreviewPane (ThumbCache& thumbs, ImageCache& images);
    ~PreviewPane() override;

    void show (const File& f);
    void reload() { const auto f = current; show ({}); show (f); }
    void imageLoaded (const String& path);      // volá MainComponent z ImageCache::onLoaded
    void release (const File& f);               // pustí súbor (pred presunom)
    void togglePlay();
    bool isAudio() const { return mode == Mode::Audio || mode == Mode::Midi; }
    void stopPlayback() { stopAudio(); midi.stop(); repaint(); }

    void paint (Graphics& g) override;
    void resized() override;
    std::function<void()> onFocusBack;
    static constexpr int infoH = 128;
    void mouseDown (const MouseEvent& e) override;
    void mouseMove (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;

private:
    enum class Mode { None, Image, Audio, Midi, Video, Doc, Other };

    void changeListenerCallback (ChangeBroadcaster*) override;
    void timerCallback() override;
    void updateImageInfo();
    void startAudio (const File& f);
    void stopAudio();
    bool ensureAudioDevice();
    Rectangle<int> waveArea() const;
    Rectangle<int> infoArea() const;
    void showOpenWithMenu();

    // názvy programov ako „čipy“: klik skopíruje, pravý klik ponúkne aj hľadanie na webe
    struct Chip { Rectangle<int> r; String label, name; };
    std::vector<Chip> chips;
    String copiedName;
    int chipAt (Point<int> p) const;
    TextButton openWithButton;
    void seekFromMouse (int x);

    ThumbCache& thumbs;
    File current;
    Mode mode = Mode::None;
    String infoLine;

    // obrázky
    ImageCache& images;
    std::shared_ptr<ImageCache::Entry> shown;

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

    // MIDI
    MidiPlayer midi;

    // video (len ak je libmpv-2.dll vedľa programu)
    std::unique_ptr<VideoView> video;

    // fonty, PDF, 3D, DXF
    std::unique_ptr<DocViewer> doc;
    Kind docKind = Kind::Other;
    void paintMidi (Graphics& g);
};
}
