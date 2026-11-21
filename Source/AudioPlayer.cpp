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
    if (warm.joinable()) warm.join();
    stopTimer();
    transport.stop();
    transport.setSource (nullptr);
    loopSource.reset();
    readerSource.reset();
    if (deviceReady)
    {
        deviceManager.removeAudioCallback (&sourcePlayer);
        deviceManager.closeAudioDevice();
    }
    readThread.stopThread (2000);
}

#if JUCE_WINDOWS
extern "C" __declspec(dllimport) long __stdcall CoInitializeEx (void*, unsigned long);
#endif

void AudioPlayer::warmUp()
{
    if (deviceReady || deviceFailed || warm.joinable()) return;
    warm = std::thread ([this]
    {
       #if JUCE_WINDOWS
        CoInitializeEx (nullptr, 0 /* COINIT_MULTITHREADED */);
       #endif
        deviceError = deviceManager.initialise (0, 2, savedDevice.get(), true);
        if (deviceManager.getCurrentAudioDevice() == nullptr && savedDevice != nullptr)
            deviceError = deviceManager.initialiseWithDefaultDevices (0, 2);
        warmed = true;
    });
}

bool AudioPlayer::ensureDevice()
{
    if (warm.joinable()) warm.join();
    if (deviceReady) return true;
    if (deviceFailed) return false;
    if (! warmed)
    {
        deviceError = deviceManager.initialise (0, 2, savedDevice.get(), true);
        if (deviceManager.getCurrentAudioDevice() == nullptr && savedDevice != nullptr)
            deviceError = deviceManager.initialiseWithDefaultDevices (0, 2);   // uložené zariadenie chýba → predvolené
    }
    if (deviceError.isNotEmpty() || deviceManager.getCurrentAudioDevice() == nullptr)
    {
        deviceFailed = true;
        if (deviceError.isEmpty()) deviceError = tr ("zvukové zariadenie sa nedá otvoriť");
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
    loopSource.reset();
    readerSource.reset();
    current = f;
    length = 0;
    formatText = {};
    tags = readAudioTags (f);

    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr) { changed(); return false; }
    const double sr = reader->sampleRate;
    length = sr > 0 ? (double) reader->lengthInSamples / sr : 0.0;
    const auto dot = tr (" · ");
    formatText = timeTextShort (length) + dot + String (sr / 1000.0, 1) + " kHz" + dot
               + (reader->numChannels == 1 ? String ("mono") : reader->numChannels == 2 ? String ("stereo") : String ((int) reader->numChannels) + " k");
    // synchronizácia tempa (ako „re-pitch“ v DAW – mení aj výšku)
    double ratio = 1.0;
    if (syncTempo > 0 && length > 0.3 && length <= 60.0)
    {
        double bpm = reader->metadataValues.containsKey ("bpm") ? reader->metadataValues["bpm"].getDoubleValue() : 0.0;
        if (bpm <= 0 && bpmOf) bpm = bpmOf (f);
        if (bpm > 0)
        {
            ratio = syncTempo / bpm;
            while (ratio > 1.5) ratio *= 0.5;
            while (ratio < 0.75) ratio *= 2.0;
            formatText << dot << String (roundToInt (bpm)) << tr (" → ") << String (roundToInt (syncTempo)) << " BPM";
            length /= ratio;
        }
    }
    readerSource = std::make_unique<AudioFormatReaderSource> (reader.release(), true);
    loopSource = std::make_unique<LoopRangeSource> (*readerSource);
    if (! ensureDevice()) { changed(); return false; }
    transport.setSource (loopSource.get(), 32768, &readThread, sr * ratio, 2);
    return true;
}

void LoopRangeSource::getNextAudioBlock (const AudioSourceChannelInfo& info)
{
    const int64 start = a.load(), end = b.load();
    if (start < 0 || end <= start) { src.getNextAudioBlock (info); virt += info.numSamples; return; }
    int done = 0;
    while (done < info.numSamples)
    {
        const int64 real = realOf (virt);
        if (src.getNextReadPosition() != real) src.setNextReadPosition (real);
        const int64 untilEnd = real < start ? start - real + (end - start) : end - real;
        const int n = (int) jmax<int64> (1, jmin<int64> (info.numSamples - done, untilEnd));
        AudioSourceChannelInfo sub (info.buffer, info.startSample + done, n);
        src.getNextAudioBlock (sub);
        done += n;
        virt += n;
    }
}

void AudioPlayer::setLoopRange (double f0, double f1)
{
    if (loopSource == nullptr || readerSource == nullptr) return;
    const int64 total = readerSource->getTotalLength();
    const int64 s0 = (int64) (jlimit (0.0, 1.0, jmin (f0, f1)) * (double) total);
    const int64 s1 = (int64) (jlimit (0.0, 1.0, jmax (f0, f1)) * (double) total);
    if (s1 - s0 < 256) { clearLoop(); return; }
    loopSource->setRange (s0, s1);
    transport.setPosition (jmin (f0, f1) * length);   // vyprázdni medzipamäť, hrá sa hneď od začiatku výrezu
    if (! transport.isPlaying()) { transport.start(); wasPlaying = true; }
    changed();
}

void AudioPlayer::clearLoop()
{
    if (loopSource == nullptr || ! loopSource->hasRange()) return;
    const double here = getPosition();   // skutočné miesto vo výreze
    const bool playing = transport.isPlaying();
    loopSource->clearRange();
    transport.setPosition (here);
    if (playing) transport.start();
    changed();
}

bool AudioPlayer::tempoPossible (const File& f)
{
    std::unique_ptr<AudioFormatReader> r (formats.createReaderFor (f));
    if (r == nullptr || r->sampleRate <= 0) return false;
    const double len = (double) r->lengthInSamples / r->sampleRate;
    if (len <= 0.3 || len > 60.0) return false;
    double bpm = r->metadataValues.containsKey ("bpm") ? r->metadataValues["bpm"].getDoubleValue() : 0.0;
    if (bpm <= 0 && bpmOf) bpm = bpmOf (f);
    return bpm > 0;
}

void AudioPlayer::setSyncTempo (double bpm)
{
    syncTempo = bpm;
    if (current == File()) return;
    // znovu načítať aktuálnu skladbu s novým tempom
    const bool playing = transport.isPlaying();
    const double rel = length > 0 ? transport.getCurrentPosition() / length : 0.0;
    if (load (current))
    {
        transport.setPosition (rel * length);
        if (playing) transport.start();
    }
    changed();
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

bool AudioPlayer::playQueued (const File& f)
{
    const auto prev = current;
    const bool ok = load (f);
    if (ok) transport.start();
    wasPlaying = ok;
    if (onTrackChanged && prev != current) onTrackChanged (prev, current);
    changed();
    return ok;
}

void AudioPlayer::next (bool automatic)
{
    if (! queue.isEmpty())
    {
        const auto f = queue.removeAndReturn (0);
        playQueued (f);
        return;
    }
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
    loopSource.reset();
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
        // sample (do 60 s) sa zahrá len raz – nespúšťať ďalšie samply za sebou (to je len pri skladbách)
        else if (length <= 60.0 && queue.isEmpty()) { transport.setPosition (0.0); changed(); }
        else next (true);
    }
}
}
