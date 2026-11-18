#include "Convert.h"
#include "ImageLoader.h"
#include "ExtraFormats.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <mfapi.h>
 #include <mfidl.h>
 #include <mfreadwrite.h>
#endif

namespace kp::convert
{
std::vector<Option> optionsFor (bool images, bool audio)
{
    std::vector<Option> o;
    if (images)
    {
        o.push_back ({ Target::Jpg, tr ("JPG (fotky, menší súbor)"), false });
        o.push_back ({ Target::Png, tr ("PNG (bez straty kvality)"), false });
    }
    if (audio)
    {
        o.push_back ({ Target::Wav16, "WAV 16 bit", true });
        o.push_back ({ Target::Wav24, "WAV 24 bit", true });
        o.push_back ({ Target::Flac, tr ("FLAC (bezstratový, menší)"), true });
        o.push_back ({ Target::Ogg, "OGG Vorbis", true });
       #if JUCE_WINDOWS
        o.push_back ({ Target::Mp3, "MP3 320 kbps", true });
        o.push_back ({ Target::M4a, "M4A / AAC 256 kbps", true });
       #endif
    }
    return o;
}

String extensionOf (Target t)
{
    switch (t)
    {
        case Target::Jpg: return ".jpg";
        case Target::Png: return ".png";
        case Target::Wav16: case Target::Wav24: return ".wav";
        case Target::Flac: return ".flac";
        case Target::Ogg: return ".ogg";
        case Target::Mp3: return ".mp3";
        case Target::M4a: return ".m4a";
    }
    return {};
}

static File outputFor (const File& src, Target t)
{
    const auto ext = extensionOf (t);
    auto f = src.withFileExtension (ext);
    if (f.exists()) f = src.getParentDirectory().getNonexistentChildFile (src.getFileNameWithoutExtension(), ext, false);
    return f;
}

#if JUCE_WINDOWS
// MP3 / AAC cez kodéry Windows (Media Foundation)
static bool writeMf (AudioFormatReader& r, const File& out, bool mp3, String& error)
{
    CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    MFStartup (MF_VERSION, MFSTARTUP_LITE);
    const UINT32 rate = (UINT32) r.sampleRate, ch = (UINT32) jlimit (1u, 2u, r.numChannels);
    if (rate != 44100 && rate != 48000) { error = tr ("MP3/M4A vie Windows zakódovať len pri 44,1 alebo 48 kHz."); return false; }

    IMFSinkWriter* w = nullptr;
    if (FAILED (MFCreateSinkWriterFromURL (out.getFullPathName().toWideCharPointer(), nullptr, nullptr, &w))) { error = tr ("Kodér sa nedá otvoriť."); return false; }
    IMFMediaType* ot = nullptr; IMFMediaType* it = nullptr;
    MFCreateMediaType (&ot);
    ot->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    ot->SetGUID (MF_MT_SUBTYPE, mp3 ? MFAudioFormat_MP3 : MFAudioFormat_AAC);
    ot->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    ot->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, ch);
    ot->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, mp3 ? 40000 : 32000);
    if (! mp3) ot->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    DWORD idx = 0;
    HRESULT hr = w->AddStream (ot, &idx);
    MFCreateMediaType (&it);
    it->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    it->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_PCM);
    it->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    it->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    it->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, ch);
    it->SetUINT32 (MF_MT_AUDIO_BLOCK_ALIGNMENT, ch * 2);
    it->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * ch * 2);
    if (SUCCEEDED (hr)) hr = w->SetInputMediaType (idx, it, nullptr);
    if (SUCCEEDED (hr)) hr = w->BeginWriting();
    const int block = 4096;
    AudioBuffer<float> buf ((int) ch, block);
    LONGLONG time = 0;
    for (int64 pos = 0; SUCCEEDED (hr) && pos < r.lengthInSamples; pos += block)
    {
        const int n = (int) jmin<int64> (block, r.lengthInSamples - pos);
        r.read (&buf, 0, n, pos, true, ch > 1);
        IMFMediaBuffer* mb = nullptr;
        hr = MFCreateMemoryBuffer ((DWORD) (n * ch * 2), &mb);
        if (FAILED (hr)) break;
        BYTE* p = nullptr;
        mb->Lock (&p, nullptr, nullptr);
        auto* s16 = reinterpret_cast<int16*> (p);
        for (int i = 0; i < n; ++i)
            for (UINT32 c = 0; c < ch; ++c)
                *s16++ = (int16) jlimit (-32768, 32767, roundToInt (buf.getSample ((int) c, i) * 32767.0f));
        mb->Unlock();
        mb->SetCurrentLength ((DWORD) (n * ch * 2));
        IMFSample* smp = nullptr;
        MFCreateSample (&smp);
        smp->AddBuffer (mb);
        const LONGLONG dur = (LONGLONG) n * 10000000LL / rate;
        smp->SetSampleTime (time);
        smp->SetSampleDuration (dur);
        time += dur;
        hr = w->WriteSample (idx, smp);
        smp->Release();
        mb->Release();
    }
    if (SUCCEEDED (hr)) hr = w->Finalize();
    w->Release(); ot->Release(); it->Release();
    if (FAILED (hr)) { out.deleteFile(); error = tr ("Kódovanie sa nepodarilo."); return false; }
    return true;
}
#endif

bool run (const File& src, Target t, File& out, String& error)
{
    out = outputFor (src, t);
    if (t == Target::Jpg || t == Target::Png)
    {
        auto im = img::loadFull (src, 0).image;
        if (! im.isValid()) { error = tr ("Obrázok sa nedá načítať: ") + src.getFileName(); return false; }
        MemoryOutputStream mos;
        bool ok;
        if (t == Target::Png) { PNGImageFormat fmt; ok = fmt.writeImageToStream (im, mos); }
        else
        {
            Image rgb (Image::RGB, im.getWidth(), im.getHeight(), true, SoftwareImageType());
            { Graphics g (rgb); g.fillAll (Colours::white); g.drawImageAt (im, 0, 0); }
            JPEGImageFormat fmt;
            fmt.setQuality (0.92f);
            ok = fmt.writeImageToStream (rgb, mos);
        }
        if (! ok || ! out.replaceWithData (mos.getData(), mos.getDataSize())) { error = tr ("Nedá sa zapisovať do priečinka."); return false; }
        return true;
    }

    AudioFormatManager fm;
    registerAllFormats (fm);
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (src));
    if (r == nullptr) { error = tr ("Tento zvukový formát sa nedá prečítať: ") + src.getFileName(); return false; }

   #if JUCE_WINDOWS
    if (t == Target::Mp3 || t == Target::M4a) return writeMf (*r, out, t == Target::Mp3, error);
   #endif

    std::unique_ptr<AudioFormat> fmt;
    int bits = 16;
    if (t == Target::Wav16 || t == Target::Wav24) { fmt = std::make_unique<WavAudioFormat>(); bits = t == Target::Wav24 ? 24 : 16; }
    else if (t == Target::Flac) { fmt = std::make_unique<FlacAudioFormat>(); bits = r->bitsPerSample >= 24 ? 24 : 16; }
    else if (t == Target::Ogg) fmt = std::make_unique<OggVorbisAudioFormat>();
    else { error = tr ("Tento formát sa tu nedá vytvoriť."); return false; }

    double rate = r->sampleRate;
    if (t == Target::Ogg || t == Target::Flac)
    {
        const auto rates = fmt->getPossibleSampleRates();
        if (! rates.contains ((int) rate)) { error = tr ("Táto vzorkovacia frekvencia sa do formátu nedá uložiť."); return false; }
    }
    std::unique_ptr<OutputStream> stream = std::make_unique<FileOutputStream> (out);
    if (! static_cast<FileOutputStream*> (stream.get())->openedOk()) { error = tr ("Nedá sa zapisovať do priečinka."); return false; }
    const int quality = t == Target::Ogg ? jmax (0, fmt->getQualityOptions().size() - 3) : 0;
    auto w = fmt->createWriterFor (stream, AudioFormatWriterOptions{}.withSampleRate (rate)
                                               .withNumChannels ((int) jmin (2u, r->numChannels))
                                               .withBitsPerSample (bits)
                                               .withQualityOptionIndex (quality));
    if (w == nullptr) { stream.reset(); out.deleteFile(); error = tr ("Tento formát sa tu nedá vytvoriť."); return false; }
    const bool ok = w->writeFromAudioReader (*r, 0, r->lengthInSamples);
    w.reset();
    if (! ok) { out.deleteFile(); error = tr ("Prevod sa nepodaril."); return false; }
    return true;
}
}
