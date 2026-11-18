#pragma once
#include "Common.h"
#include "AudioTags.h"
#include <random>

namespace kp
{
// Prehrávač zvuku pre celý program: hrá ďalej aj pri prezeraní iných súborov,
// má zoznam skladieb, opakovanie a náhodné poradie.
// zdroj, ktorý vie hrať dokola len vybraný úsek (výrez zo samplu)
class LoopRangeSource : public PositionableAudioSource
{
public:
    explicit LoopRangeSource (PositionableAudioSource& s) : src (s) {}
    void setRange (int64 start, int64 end) { a = start; b = end; }
    void clearRange() { a = -1; b = -1; }
    bool hasRange() const { return a >= 0 && b > a; }
    void prepareToPlay (int n, double sr) override { src.prepareToPlay (n, sr); }
    void releaseResources() override { src.releaseResources(); }
    void getNextAudioBlock (const AudioSourceChannelInfo& info) override;
    // pozícia navonok rastie stále ďalej (tak to čaká BufferingAudioSource), dovnútra sa zabalí do úseku
    void setNextReadPosition (int64 p) override { virt = p; src.setNextReadPosition (realOf (p)); }
    int64 getNextReadPosition() const override { return virt; }
    int64 realOf (int64 v) const
    {
        const int64 s = a.load(), e = b.load();
        if (s < 0 || e <= s || v < s) return v;
        return s + (v - s) % (e - s);
    }
    int64 getTotalLength() const override { return src.getTotalLength(); }
    bool isLooping() const override { return hasRange() || src.isLooping(); }
    void setLooping (bool l) override { src.setLooping (l); }
private:
    PositionableAudioSource& src;
    std::atomic<int64> a { -1 }, b { -1 };
    int64 virt = 0;
};

class AudioPlayer : private Timer
{
public:
    enum class Repeat { Off, All, One };

    AudioPlayer();
    ~AudioPlayer() override;

    bool play (const File& f, const Array<File>& playlist = {});
    void stop();
    void togglePause();
    void next (bool automatic = false);
    void previous();
    void seek (double seconds);
    void releaseIfCurrent (const File& f);

    // fronta: skladby, ktoré idú na rad pred zvyškom zoznamu
    void enqueue (const File& f) { queue.add (f); changed(); }
    int getQueueSize() const { return queue.size(); }
    void clearQueue() { queue.clear(); changed(); }
    const AudioTags& getTags() const { return tags; }

    bool isPlaying() const { return transport.isPlaying(); }
    bool hasTrack() const { return readerSource != nullptr; }
    double getPosition() const
    {
        const double p = transport.getCurrentPosition();
        if (! hasLoop() || length <= 0) return p;
        // vo výreze zobraziť skutočné miesto v skladbe
        const double total = (double) loopSource->getTotalLength();
        return total > 0 ? (double) loopSource->realOf ((int64) (p / length * total)) / total * length : p;
    }
    double getLength() const { return length; }
    File getCurrent() const { return current; }
    String getFormatText() const { return formatText; }
    String getDeviceError() const { return deviceError; }
    int getIndex() const { return index; }
    int getCount() const { return playlist.size(); }

    Repeat repeat = Repeat::All;
    bool shuffle = false;
    void setVolume (float v) { volume = jlimit (0.0f, 1.0f, v); transport.setGain (volume * volume); }
    float getVolume() const { return volume; }

    AudioFormatManager& getFormats() { return formats; }

    // výrez: hrať dokola úsek (0..1 z dĺžky skladby)
    void setLoopRange (double fromFrac, double toFrac);
    void clearLoop();
    bool hasLoop() const { return loopSource != nullptr && loopSource->hasRange(); }

    // zvukový výstup (aj ASIO): uložený stav z nastavení, dialóg na výber
    void setSavedDeviceState (std::unique_ptr<XmlElement> xml) { savedDevice = std::move (xml); }
    AudioDeviceManager& getDeviceManager() { ensureDevice(); return deviceManager; }
    std::unique_ptr<XmlElement> getDeviceState() { return deviceManager.createStateXml(); }

    // tempo: krátke loopy so známym BPM sa prehrajú zrýchlene/spomalene na toto tempo (0 = vypnuté)
    void setSyncTempo (double bpm);
    double getSyncTempo() const { return syncTempo; }
    std::function<double (const File&)> bpmOf;   // BPM súboru, ak je známe

    std::function<void (const File& previous, const File& now)> onTrackChanged;
    std::function<void()> onStateChanged;

private:
    void timerCallback() override;
    bool load (const File& f);
    bool ensureDevice();
    void changed() { if (onStateChanged) onStateChanged(); }

    AudioFormatManager formats;
    AudioDeviceManager deviceManager;
    std::unique_ptr<XmlElement> savedDevice;
    bool deviceReady = false, deviceFailed = false;
    String deviceError;
    AudioSourcePlayer sourcePlayer;
    AudioTransportSource transport;
    std::unique_ptr<AudioFormatReaderSource> readerSource;
    std::unique_ptr<LoopRangeSource> loopSource;
    TimeSliceThread readThread { "Citanie zvuku" };

    File current;
    String formatText;
    double length = 0, syncTempo = 0;
    float volume = 0.85f;
    Array<File> playlist, queue;
    AudioTags tags;
    bool playQueued (const File& f);
    int index = -1;
    std::vector<int> history;
    std::mt19937 rng { (unsigned) Time::currentTimeMillis() };
    bool wasPlaying = false;
};
}
