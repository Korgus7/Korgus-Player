#include "Similar.h"
#include "ImageLoader.h"

namespace kp::similar
{
bool imageHash (const File& f, ImageSig& sig)
{
    uint64& hash = sig.hash;
    auto im = img::loadFull (f, 256).image;
    if (! im.isValid()) return false;
    // 9×8 v odtieňoch sivej, porovnanie susedných bodov
    Image small (Image::RGB, 9, 8, true, SoftwareImageType());
    {
        Graphics g (small);
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (im, 0, 0, 9, 8, 0, 0, im.getWidth(), im.getHeight());
    }
    hash = 0;
    int bit = 0;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x, ++bit)
        {
            const float a = small.getPixelAt (x, y).getPerceivedBrightness();
            const float b = small.getPixelAt (x + 1, y).getPerceivedBrightness();
            if (a > b) hash |= (uint64) 1 << bit;
        }
    Image c4 (Image::RGB, 4, 4, true, SoftwareImageType());
    {
        Graphics g (c4);
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (im, 0, 0, 4, 4, 0, 0, im.getWidth(), im.getHeight());
    }
    for (int i = 0; i < 16; ++i)
    {
        const auto c = c4.getPixelAt (i % 4, i / 4);
        sig.colours[i * 3] = c.getRed(); sig.colours[i * 3 + 1] = c.getGreen(); sig.colours[i * 3 + 2] = c.getBlue();
    }
    return true;
}

bool similarImages (const ImageSig& a, const ImageSig& b)
{
    if (distance (a.hash, b.hash) > 8) return false;
    int diff = 0;
    for (int i = 0; i < 48; ++i) diff += std::abs ((int) a.colours[i] - (int) b.colours[i]);
    return diff / 48 <= 22;
}

SoundFeatures soundFeatures (AudioFormatManager& fm, const File& f)
{
    SoundFeatures out;
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->sampleRate <= 0 || r->lengthInSamples <= 0) return out;
    const double sr = r->sampleRate;
    const double len = (double) r->lengthInSamples / sr;
    // prvé 3 sekundy, mono, zmenšené na ~22 kHz
    const int n = (int) jmin<int64> (r->lengthInSamples, (int64) (sr * 3.0));
    AudioBuffer<float> b ((int) jmin (2u, r->numChannels), n);
    r->read (&b, 0, n, 0, true, b.getNumChannels() > 1);
    const int step = jmax (1, (int) (sr / 22050.0));
    std::vector<float> x;
    x.reserve ((size_t) (n / step + 1));
    for (int i = 0; i < n; i += step)
        x.push_back (b.getNumChannels() > 1 ? 0.5f * (b.getSample (0, i) + b.getSample (1, i)) : b.getSample (0, i));
    const double fs = sr / step;
    if (x.size() < 64) return out;

    // obálka v 10 ms oknách
    const int win = jmax (1, (int) (fs * 0.01));
    std::vector<float> env;
    for (size_t i = 0; i + (size_t) win <= x.size(); i += (size_t) win)
    {
        double e = 0;
        for (int k = 0; k < win; ++k) e += x[i + (size_t) k] * x[i + (size_t) k];
        env.push_back ((float) std::sqrt (e / win));
    }
    if (env.empty()) return out;
    const auto peakIt = std::max_element (env.begin(), env.end());
    const float peak = jmax (1.0e-6f, *peakIt);
    const float attack = (float) std::distance (env.begin(), peakIt) * 0.01f;
    size_t decayIdx = (size_t) std::distance (env.begin(), peakIt);
    while (decayIdx < env.size() && env[decayIdx] > peak * 0.1f) ++decayIdx;
    const float decay = (float) (decayIdx - (size_t) std::distance (env.begin(), peakIt)) * 0.01f;
    double rms = 0; for (auto v : env) rms += v * v; rms = std::sqrt (rms / env.size());

    // jas: prechody nulou a „rozdielová“ energia (výšky) voči celkovej
    int zc = 0; double diffE = 0, totE = 0;
    for (size_t i = 1; i < x.size(); ++i)
    {
        if ((x[i - 1] < 0) != (x[i] < 0)) ++zc;
        const double d = x[i] - x[i - 1];
        diffE += d * d; totE += (double) x[i] * x[i];
    }
    const float zcr = (float) zc / (float) x.size() * (float) fs;      // Hz
    const float bright = (float) (totE > 0 ? diffE / totE : 0);

    // basy: energia po jednoduchom dolnom priepuste (~150 Hz)
    double lowE = 0; float lp = 0; const float a = (float) std::exp (-2.0 * MathConstants<double>::pi * 150.0 / fs);
    for (auto v : x) { lp = a * lp + (1 - a) * v; lowE += lp * lp; }
    const float lowRatio = (float) (totE > 0 ? lowE / totE : 0);

    out.v = { (float) std::log (len + 0.05), std::log (attack + 0.005f), std::log (decay + 0.01f),
              20.0f * std::log10 ((float) rms + 1.0e-6f) / 20.0f, std::log (zcr + 20.0f), std::log (bright + 1.0e-4f), lowRatio * 4.0f };
    out.ok = true;
    return out;
}
}
