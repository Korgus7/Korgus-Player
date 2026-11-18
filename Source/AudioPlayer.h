#pragma once
#include "Common.h"
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

    std::function<void (const File& previous, const File& now)> onTrackChanged;
    std::function<void()> onStateChanged;

private:
    void timerCallback() override;
    bool load (const File& f);
    bool ensureDevice();
    void changed() { if (onStateChanged) onStateChanged(); }

    AudioFormatManager formats;
    AudioDeviceManager deviceManager;
    bool deviceReady = false, deviceFailed = false;
    String deviceError;
    AudioSourcePlayer sourcePlayer;
    AudioTransportSource transport;
    std::unique_ptr<AudioFormatReaderSource> readerSource;
    TimeSliceThread readThread { "Citanie zvuku" };

    File current;
    String formatText;
    double length = 0;
    float volume = 0.85f;
    Array<File> playlist;
    int index = -1;
    std::vector<int> history;
    std::mt19937 rng { (unsigned) Time::currentTimeMillis() };
    bool wasPlaying = false;
};
}
