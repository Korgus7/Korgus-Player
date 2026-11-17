#include "AudioInfoCache.h"

namespace kp
{
AudioInfoCache::AudioInfoCache()
    : pool (ThreadPoolOptions{}.withThreadName ("Zvuky").withNumberOfThreads (jlimit (2, 4, SystemStats::getNumCpus() / 2)))
{
    formats.registerBasicFormats();
}

AudioInfoCache::~AudioInfoCache()
{
    *alive = false;
    pool.removeAllJobs (true, 3000);
}

AudioInfoCache::Info AudioInfoCache::compute (AudioFormatManager& fm, const File& f)
{
    Info info;
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->sampleRate <= 0) return info;

    info.ok = true;
    info.sampleRate = r->sampleRate;
    info.bits = (int) r->bitsPerSample;
    info.channels = (int) r->numChannels;
    info.length = (double) r->lengthInSamples / r->sampleRate;

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
    // tiché samply zväčšiť, aby bol tvar vidieť
    if (maxPeak > 0.0001f && maxPeak < 0.5f)
        for (auto& p : info.peaks) p = p / maxPeak * 0.5f;
    for (auto& p : info.peaks) p = jlimit (0.0f, 1.0f, p);
    return info;
}

const AudioInfoCache::Info* AudioInfoCache::get (const File& f)
{
    const auto path = f.getFullPathName();
    if (infos.contains (path)) return infos[path].get();
    if (kindOf (f) != Kind::Audio) return nullptr;
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
