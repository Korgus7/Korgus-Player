#include "AudioPlayer.h"
#include "ExtraFormats.h"

namespace kp
{
static String timeTextShort (double s)
{
    const int t = (int) s;
    return String (t / 60) + ":" + String (t % 60).paddedLeft ('0', 2);
}

AudioPlayer::AudioPlayer()
{
    registerAllFormats (formats);
    readThread.startThread();
    transport.setGain (volume * volume);
    startTimerHz (8);
}

AudioPlayer::~AudioPlayer()
{
    stopTimer();
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
    if (deviceReady)
    {
        deviceManager.removeAudioCallback (&sourcePlayer);
        deviceManager.closeAudioDevice();
    }
    readThread.stopThread (2000);
}

bool AudioPlayer::ensureDevice()
{
    if (deviceReady) return true;
    if (deviceFailed) return false;
    deviceError = deviceManager.initialiseWithDefaultDevices (0, 2);
    if (deviceError.isNotEmpty() || deviceManager.getCurrentAudioDevice() == nullptr)
    {
        deviceFailed = true;
        if (deviceError.isEmpty()) deviceError = String::fromUTF8 ("zvukové zariadenie sa nedá otvoriť");
        return false;
    }
    sourcePlayer.setSource (&transport);
    deviceManager.addAudioCallback (&sourcePlayer);
    deviceReady = true;
    return true;
}

bool AudioPlayer::load (const File& f)
{
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
    current = f;
    length = 0;
    formatText = {};

    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr) { changed(); return false; }
    const double sr = reader->sampleRate;
    length = sr > 0 ? (double) reader->lengthInSamples / sr : 0.0;
    const auto dot = String::fromUTF8 (" · ");
    formatText = timeTextShort (length) + dot + String (sr / 1000.0, 1) + " kHz" + dot
               + (reader->numChannels == 1 ? String ("mono") : reader->numChannels == 2 ? String ("stereo") : String ((int) reader->numChannels) + " k");
    readerSource = std::make_unique<AudioFormatReaderSource> (reader.release(), true);
    if (! ensureDevice()) { changed(); return false; }
    transport.setSource (readerSource.get(), 32768, &readThread, sr, 2);
    return true;
}

bool AudioPlayer::play (const File& f, const Array<File>& list)
{
    const auto prev = current;
    if (! list.isEmpty())
    {
        playlist = list;
        history.clear();
    }
    index = playlist.indexOf (f);
    if (index < 0) { playlist = { f }; index = 0; }
    const bool ok = load (f);
    if (ok) transport.start();
    wasPlaying = ok;
    if (onTrackChanged && prev != current) onTrackChanged (prev, current);
    changed();
    return ok;
}

void AudioPlayer::stop()
{
    transport.stop();
    transport.setPosition (0.0);
    wasPlaying = false;
    changed();
}

void AudioPlayer::togglePause()
{
    if (readerSource == nullptr)
    {
        if (index >= 0 && index < playlist.size()) play (playlist[index]);
        return;
    }
    if (transport.isPlaying()) { transport.stop(); wasPlaying = false; }
    else
    {
        if (transport.getCurrentPosition() >= length - 0.05) transport.setPosition (0.0);
        transport.start();
        wasPlaying = true;
    }
    changed();
}

void AudioPlayer::seek (double s)
{
    if (readerSource == nullptr) return;
    transport.setPosition (jlimit (0.0, length, s));
    if (! transport.isPlaying()) { transport.start(); wasPlaying = true; }
    changed();
}

void AudioPlayer::next (bool automatic)
{
    if (playlist.isEmpty()) return;
    int n;
    if (shuffle && playlist.size() > 1)
    {
        history.push_back (index);
        std::uniform_int_distribution<int> d (0, playlist.size() - 2);
        n = d (rng);
        if (n >= index) ++n;     // nie tá istá skladba
    }
    else
    {
        n = index + 1;
        if (n >= playlist.size())
        {
            if (automatic && repeat == Repeat::Off) { stop(); return; }
            n = 0;
        }
    }
    play (playlist[n]);
}

void AudioPlayer::previous()
{
    if (playlist.isEmpty()) return;
    // po 3 sekundách skladby „predošlá“ znamená od začiatku
    if (transport.getCurrentPosition() > 3.0) { seek (0.0); return; }
    int n;
    if (shuffle && ! history.empty()) { n = history.back(); history.pop_back(); }
    else n = index - 1 < 0 ? playlist.size() - 1 : index - 1;
    const auto list = playlist;
    play (list[n]);
}

void AudioPlayer::releaseIfCurrent (const File& f)
{
    if (f != current) return;
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
    wasPlaying = false;
    changed();
}

void AudioPlayer::timerCallback()
{
    // koniec skladby → ďalšia podľa opakovania a náhodného poradia
    if (wasPlaying && readerSource != nullptr && ! transport.isPlaying() && transport.hasStreamFinished())
    {
        wasPlaying = false;
        if (repeat == Repeat::One) { transport.setPosition (0.0); transport.start(); wasPlaying = true; changed(); }
        else next (true);
    }
}
}
