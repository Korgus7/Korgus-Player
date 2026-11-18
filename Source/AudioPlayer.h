#pragma once
#include "Common.h"
#include "AudioTags.h"
#include <random>

namespace kp
{
// Prehrávač zvuku pre celý program: hrá ďalej aj pri prezeraní iných súborov,
// má zoznam skladieb, opakovanie a náhodné poradie.
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
    double getPosition() const { return transport.getCurrentPosition(); }
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
