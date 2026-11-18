#include "AudioInfoCache.h"
#include "ExtraFormats.h"
#include "MidiPlayer.h"

namespace kp
{
AudioInfoCache::AudioInfoCache()
    : pool (ThreadPoolOptions{}.withThreadName ("Zvuky").withNumberOfThreads (jlimit (2, 4, SystemStats::getNumCpus() / 2)))
{
    registerAllFormats (formats);
}

AudioInfoCache::~AudioInfoCache()
{
    *alive = false;
    pool.removeAllJobs (true, 3000);
}

AudioInfoCache::Info AudioInfoCache::compute (AudioFormatManager& fm, const File& f)
{
    Info info;
    const auto dot = String::fromUTF8 (" · ");

    if (kindOf (f) == Kind::Midi)
    {
        MidiMessageSequence seq;
        MidiPlayer::Summary sm;
        if (! MidiPlayer::parse (f, seq, sm)) return info;
        info.ok = true;
        info.length = sm.length;
        info.format = "MIDI" + dot + String (sm.tracks) + String::fromUTF8 (" stôp") + dot + String (sm.notes) + String::fromUTF8 (" nôt");
        // hustota nôt namiesto vlny
        info.peaks.assign (numPeaks, 0.0f);
        if (sm.length > 0)
            for (auto* e : seq)
                if (e->message.isNoteOn())
                {
                    const int b = jlimit (0, numPeaks - 1, (int) (e->message.getTimeStamp() / sm.length * numPeaks));
                    info.peaks[(size_t) b] += 1.0f;
                }
        float mx = 0;
        for (auto p : info.peaks) mx = jmax (mx, p);
        if (mx > 0) for (auto& p : info.peaks) p = 0.15f + 0.85f * p / mx;
        return info;
    }

    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->sampleRate <= 0) return info;

    info.ok = true;
    info.sampleRate = r->sampleRate;
    info.bits = (int) r->bitsPerSample;
    info.channels = (int) r->numChannels;
    info.length = (double) r->lengthInSamples / r->sampleRate;
    info.format = String (r->sampleRate / 1000.0, 1) + " kHz";
    if (r->bitsPerSample > 0) info.format << dot << (int) r->bitsPerSample << " bit";
    info.format << dot << (r->numChannels == 1 ? String ("mono") : r->numChannels == 2 ? String ("stereo") : String ((int) r->numChannels) + " k");
    if (r->metadataValues.containsKey ("bpm"))
        info.format = "REX" + dot + String (r->metadataValues["bpm"].getDoubleValue(), 1) + " BPM" + dot + info.format;

    const int64 total = r->lengthInSamples;
    if (total <= 0) return info;
    const int chans = jlimit (1, 2, (int) r->numChannels);
    const int64 perBin = jmax<int64> (1, total / numPeaks);
    // dlhé súbory: z každého úseku len kúsok, aby to bolo rýchle
    const int64 readLen = jmin<int64> (perBin, 16384);

    info.peaks.resize (numPeaks, 0.0f);
    float maxPeak = 0.0f;
    for (int i = 0; i < numPeaks; ++i)
    {
        const int64 start = i * perBin;
        if (start >= total) break;
        Range<float> lv[2];
        r->readMaxLevels (start, jmin (readLen, total - start), lv, chans);
        float p = 0.0f;
        for (int c = 0; c < chans; ++c)
            p = jmax (p, std::abs (lv[c].getStart()), std::abs (lv[c].getEnd()));
        info.peaks[(size_t) i] = p;
        maxPeak = jmax (maxPeak, p);
    }
    // vlna vždy na celú výšku, aby bol tvar vidieť aj pri tichých samploch
    if (maxPeak > 0.0001f)
        for (auto& p : info.peaks) p = p / maxPeak * 0.95f;
    for (auto& p : info.peaks) p = jlimit (0.0f, 1.0f, p);
    return info;
}

const AudioInfoCache::Info* AudioInfoCache::get (const File& f)
{
    const auto path = f.getFullPathName();
    if (infos.contains (path)) return infos[path].get();
    if (kindOf (f) != Kind::Audio && kindOf (f) != Kind::Midi) return nullptr;
    // WMA potrebuje COM, ktorý vlákna na pozadí nemajú
    if (f.hasFileExtension ("wma")) return nullptr;

    const int gen = generation.load();
    if (pending.contains (path) && pending[path] == gen) return nullptr;
    pending.set (path, gen);

    auto aliveFlag = alive;
    pool.addJob ([this, f, path, gen, aliveFlag]
    {
        if (gen != generation.load()) return;
        auto info = std::make_shared<Info> (compute (formats, f));
        MessageManager::callAsync ([this, aliveFlag, path, info]
        {
            if (! *aliveFlag) return;
            pending.remove (path);
            infos.set (path, info);
            if (onReady) onReady();
        });
    });
    return nullptr;
}

void AudioInfoCache::cancelPending()
{
    ++generation;
    pool.removeAllJobs (false, 0);
    pending.clear();
}
}
