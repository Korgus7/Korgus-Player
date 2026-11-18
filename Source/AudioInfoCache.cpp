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

namespace
{
    // tempo zo súboru, napr. „Loop_128bpm.wav“ alebo „120 BPM“
    double bpmFromName (const String& name)
    {
        const auto n = name.toLowerCase();
        const int i = n.indexOf ("bpm");
        if (i < 0) return 0;
        int j = i - 1;
        while (j >= 0 && (n[j] == ' ' || n[j] == '_' || n[j] == '-')) --j;
        int k = j;
        while (k >= 0 && CharacterFunctions::isDigit (n[k])) --k;
        double v = n.substring (k + 1, j + 1).getDoubleValue();
        if (v < 40 || v > 300)
        {
            // „bpm128“
            v = n.substring (i + 3).trimCharactersAtStart (" _-").getIntValue();
        }
        return (v >= 40 && v <= 300) ? v : 0;
    }

    // tempo: obálka energie → autokorelácia
    double detectBpm (const std::vector<float>& mono, double sr)
    {
        const int hop = 512;
        const int frames = (int) mono.size() / hop;
        if (frames < 64) return 0;
        std::vector<float> env ((size_t) frames, 0.0f);
        float prev = 0;
        for (int i = 0; i < frames; ++i)
        {
            float e = 0;
            for (int j = 0; j < hop; ++j) { const float v = mono[(size_t) (i * hop + j)]; e += v * v; }
            e = std::sqrt (e / hop);
            env[(size_t) i] = jmax (0.0f, e - prev);   // nárast energie = úder
            prev = e;
        }
        const double fps = sr / hop;
        const int minLag = (int) (fps * 60.0 / 200.0), maxLag = (int) (fps * 60.0 / 60.0);
        double best = 0, bestLag = 0;
        for (int lag = minLag; lag <= maxLag && lag < frames / 2; ++lag)
        {
            double c = 0;
            for (int i = 0; i + lag < frames; ++i) c += env[(size_t) i] * env[(size_t) (i + lag)];
            c /= (frames - lag);
            const double bpm = 60.0 * fps / lag;
            const double w = (bpm >= 80 && bpm <= 165) ? 1.0 : 0.85;   // bežné tempá majú prednosť
            if (c * w > best) { best = c * w; bestLag = lag; }
        }
        if (bestLag <= 0 || best <= 0) return 0;
        double bpm = 60.0 * fps / bestLag;
        while (bpm < 75) bpm *= 2;
        while (bpm > 180) bpm /= 2;
        return bpm;
    }

    // tónina: chromagram (Goertzel na tónoch 55 Hz – 1,8 kHz po zmenšení vzorkovania) + profily Krumhansl
    String detectKey (const std::vector<float>& monoIn, double srIn)
    {
        const int dec = jmax (1, (int) (srIn / 11025.0));
        const double sr = srIn / dec;
        std::vector<float> mono (monoIn.size() / (size_t) dec);
        for (size_t i = 0; i < mono.size(); ++i)
        {
            float acc = 0;
            for (int j = 0; j < dec; ++j) acc += monoIn[i * (size_t) dec + (size_t) j];
            mono[i] = acc / dec;
        }
        const int size = 2048;
        if ((int) mono.size() < size * 2) return {};
        double chroma[12] = {};
        std::vector<float> win ((size_t) size);
        for (int i = 0; i < size; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (size - 1));
        for (size_t start = 0; start + (size_t) size <= mono.size(); start += (size_t) size)
            for (int note = 33; note < 33 + 60; ++note)   // A1 … G#6
            {
                const double fr = 440.0 * std::pow (2.0, (note - 69) / 12.0);
                const double w = MathConstants<double>::twoPi * fr / sr;
                const double coeff = 2.0 * std::cos (w);
                double s1 = 0, s2 = 0;
                for (int i = 0; i < size; ++i)
                {
                    const double s0 = mono[start + (size_t) i] * win[(size_t) i] + coeff * s1 - s2;
                    s2 = s1; s1 = s0;
                }
                chroma[note % 12] += std::sqrt (jmax (0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2));
            }
        static const double major[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double minor[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "H" };
        auto corr = [&] (const double* prof, int tonic)
        {
            double mx = 0, my = 0;
            for (int i = 0; i < 12; ++i) { mx += chroma[(i + tonic) % 12]; my += prof[i]; }
            mx /= 12; my /= 12;
            double num = 0, dx = 0, dy = 0;
            for (int i = 0; i < 12; ++i)
            {
                const double x = chroma[(i + tonic) % 12] - mx, y = prof[i] - my;
                num += x * y; dx += x * x; dy += y * y;
            }
            return dx > 0 && dy > 0 ? num / std::sqrt (dx * dy) : 0.0;
        };
        double best = -1;
        String key;
        for (int t = 0; t < 12; ++t)
        {
            const double a = corr (major, t), b = corr (minor, t);
            const String n = english && t == 11 ? String ("B") : String (names[t]);
            if (a > best) { best = a; key = english ? n + " major" : n + " dur"; }
            if (b > best) { best = b; key = english ? n + " minor" : n.toLowerCase() + " mol"; }
        }
        return best > 0.55 ? key : String();   // slabá zhoda = bez tóniny (napr. bicie)
    }
}

AudioInfoCache::Info AudioInfoCache::compute (AudioFormatManager& fm, const File& f)
{
    Info info;
    const auto dot = tr (" · ");

    if (kindOf (f) == Kind::Midi)
    {
        MidiMessageSequence seq;
        MidiPlayer::Summary sm;
        if (! MidiPlayer::parse (f, seq, sm)) return info;
        info.ok = true;
        info.length = sm.length;
        info.format = "MIDI" + dot + String (sm.tracks) + tr (" stôp") + dot + String (sm.notes) + tr (" nôt");
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
    const bool isRex = r->metadataValues.containsKey ("bpm");
    if (isRex)
    {
        info.bpm = r->metadataValues["bpm"].getDoubleValue();
        info.format = "REX" + dot + String (info.bpm, 1) + " BPM" + dot + info.format;
    }

    // tempo a tónina (z prvých 60 s, len pre dlhšie ako 1,5 s)
    if (info.length > 1.5)
    {
        const int64 n = jmin<int64> (r->lengthInSamples, (int64) (r->sampleRate * 60.0));
        AudioBuffer<float> b ((int) jmin<int64> (2, r->numChannels), (int) n);
        r->read (&b, 0, (int) n, 0, true, b.getNumChannels() > 1);
        std::vector<float> mono ((size_t) n);
        for (int i = 0; i < (int) n; ++i)
            mono[(size_t) i] = b.getNumChannels() > 1 ? 0.5f * (b.getSample (0, i) + b.getSample (1, i)) : b.getSample (0, i);
        String extra;
        if (! isRex)
        {
            double bpm = bpmFromName (f.getFileNameWithoutExtension());
            if (bpm <= 0 && info.length >= 3.0) bpm = detectBpm (mono, r->sampleRate);
            if (bpm > 0) extra << String (roundToInt (bpm)) << " BPM";
            info.bpm = bpm;
        }
        const auto key = detectKey (mono, r->sampleRate);
        info.key = key;
        if (key.isNotEmpty()) extra << (extra.isEmpty() ? String() : dot) << key;
        if (extra.isNotEmpty()) info.format = extra + dot + info.format;
    }

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
