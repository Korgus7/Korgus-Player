#pragma once
#include "Common.h"
#include "ThumbCache.h"
#include "ImageCache.h"
#include "MidiPlayer.h"
#include "VideoView.h"
#include "DocViewers.h"
#include "AudioPlayer.h"
#include "Icons.h"
#include "PhotoInfo.h"
#include "MetaStore.h"
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
    PreviewPane (ThumbCache& thumbs, ImageCache& images, AudioPlayer& player);
    ~PreviewPane() override;

    void show (const File& f);
    void reload() { const auto f = current; show ({}); show (f); }
    void imageLoaded (const String& path);      // volá MainComponent z ImageCache::onLoaded
    void release (const File& f);               // pustí súbor (pred presunom)
    void togglePlay();
    bool isAudio() const { return mode == Mode::Audio || mode == Mode::Midi; }
    void stopPlayback() { midi.stop(); repaint(); }
    void playerChanged();   // volá MainComponent pri zmene prehrávača

    void paint (Graphics& g) override;
    void resized() override;
    std::function<void()> onFocusBack;
    std::function<void (bool clockwise)> onRotate;
    std::function<void()> onResize, onEdit, onVideoFullscreen;
    std::function<void (const File&)> onSnapshot;
    std::function<void (const File&, const String&)> onFileCreated;   // nový súbor (výrez) + správa
    double getVideoPosition() const;
    std::function<void (int)> onRating, onLabel;
    void setMetaStore (MetaStore* m) { meta = m; }
    static constexpr int infoH = 128;
    void mouseDown (const MouseEvent& e) override;
    void mouseMove (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;
    void mouseUp (const MouseEvent& e) override;

private:
    enum class Mode { None, Image, Audio, Midi, Video, Doc, Other };
    // výrez zo zvuku (0..1 z dĺžky), -1 = nič
    double selA = -1, selB = -1;
    bool selecting = false;
    TextButton loopB, saveSelB, clearSelB;
    void updateSelectionButtons();
    void saveSelection();
    void clearSelection();

    void changeListenerCallback (ChangeBroadcaster*) override;
    void timerCallback() override;
    void updateImageInfo();
    void startAudio (const File& f);
    void stopAudio() {}
    bool playingThis() const { return player.getCurrent() == current && player.hasTrack(); }
    Rectangle<int> waveArea() const;
    Rectangle<int> infoArea() const;
    void showOpenWithMenu();

    // názvy programov ako „čipy“: klik skopíruje, pravý klik ponúkne aj hľadanie na webe
    struct Chip { Rectangle<int> r; String label, name; };
    std::vector<Chip> chips;
    AudioTags previewTags;
    PhotoInfo photo;
    MetaStore* meta = nullptr;
    String copiedName;
    int chipAt (Point<int> p) const;
    TextButton openWithButton;
    IconButton rotL { Icon::RotateLeft, tr ("Otočiť doľava (L)") },
               rotR { Icon::RotateRight, tr ("Otočiť doprava (R)") },
               resizeB { Icon::Resize, tr ("Zmeniť veľkosť (Ctrl+R)"), tr ("Veľkosť") },
               editB { Icon::Crop, tr ("Orezať, jas, kontrast (E)"), tr ("Upraviť") };
    void updateEditButtons();
    void seekFromMouse (int x);

    ThumbCache& thumbs;
    File current;
    Mode mode = Mode::None;
    String infoLine;

    // obrázky
    ImageCache& images;
    std::shared_ptr<ImageCache::Entry> shown;

    // zvuk
    AudioPlayer& player;
    AudioFormatManager formats;
    AudioThumbnailCache waveCache { 8 };
    AudioThumbnail wave { 512, formats, waveCache };

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
