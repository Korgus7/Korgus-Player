#include "VideoExport.h"
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

namespace kp
{
VideoExport::VideoExport (Array<File> i, File m, File o, double s)
    : Thread ("Video z fotiek"), output (std::move (o)), images (std::move (i)), music (std::move (m)), perImage (jlimit (1.5, 30.0, s)) {}

VideoExport::~VideoExport()
{
    cancelled = true;
    stopThread (20000);
}

static void drawKenBurns (Graphics& g, int w, int h, const Image& im, float zoom, int index, float alpha)
{
    if (! im.isValid() || alpha <= 0.0f) return;
    // vyplniť obraz (orezať okraje), striedať smer posunu
    const float sc = jmax ((float) w / (float) im.getWidth(), (float) h / (float) im.getHeight()) * zoom;
    const float dw = (float) im.getWidth() * sc, dh = (float) im.getHeight() * sc;
    const float ex = dw - (float) w, ey = dh - (float) h;
    const float p = (zoom - 1.0f) / 0.12f;   // 0..1 počas zobrazenia
    float x = -ex / 2, y = -ey / 2;
    switch (index % 4)
    {
        case 0: x = -ex * p; break;
        case 1: x = -ex * (1.0f - p); break;
        case 2: y = -ey * p; break;
        default: break;
    }
    g.setOpacity (alpha);
    g.drawImageTransformed (im, AffineTransform::scale (sc).translated (x, y));
    g.setOpacity (1.0f);
}

void VideoExport::renderFrame (Graphics& g, int w, int h, const Image& a, const Image& b, float mix, float zoomA, float zoomB, int indexA, int indexB)
{
    g.fillAll (Colours::black);
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    drawKenBurns (g, w, h, a, zoomA, indexA, 1.0f);
    if (mix > 0.0f) drawKenBurns (g, w, h, b, zoomB, indexB, mix);
}

#if JUCE_WINDOWS
void VideoExport::run()
{
    const int W = 1920, H = 1080, FPS = 30;
    const double fade = jmin (1.0, perImage * 0.3);
    const int framesPer = (int) std::round (perImage * FPS);
    const int total = framesPer * images.size();
    const LONGLONG frameDur = 10000000LL / FPS;

    CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    MFStartup (MF_VERSION, MFSTARTUP_LITE);
    output.deleteFile();

    IMFAttributes* attr = nullptr;
    MFCreateAttributes (&attr, 1);
    attr->SetUINT32 (MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    IMFSinkWriter* w = nullptr;
    HRESULT hr = MFCreateSinkWriterFromURL (output.getFullPathName().toWideCharPointer(), nullptr, attr, &w);
    attr->Release();
    if (FAILED (hr)) { error = tr ("Video sa nedá vytvoriť (Media Foundation)."); finished = true; return; }

    // video
    DWORD vs = 0;
    {
        IMFMediaType* t = nullptr;
        MFCreateMediaType (&t);
        t->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
        t->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_H264);
        t->SetUINT32 (MF_MT_AVG_BITRATE, 12000000);
        t->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize (t, MF_MT_FRAME_SIZE, W, H);
        MFSetAttributeRatio (t, MF_MT_FRAME_RATE, FPS, 1);
        MFSetAttributeRatio (t, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = w->AddStream (t, &vs);
        t->Release();
        if (SUCCEEDED (hr))
        {
            MFCreateMediaType (&t);
            t->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
            t->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            t->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            MFSetAttributeSize (t, MF_MT_FRAME_SIZE, W, H);
            MFSetAttributeRatio (t, MF_MT_FRAME_RATE, FPS, 1);
            MFSetAttributeRatio (t, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
            hr = w->SetInputMediaType (vs, t, nullptr);
            t->Release();
        }
    }

    // zvuk (ak je hudba): 48 kHz stereo AAC
    DWORD as = 0;
    bool withAudio = false;
    std::unique_ptr<AudioFormatReader> reader;
    std::unique_ptr<AudioFormatReaderSource> src;
    std::unique_ptr<ResamplingAudioSource> rs;
    AudioFormatManager fm;
    if (SUCCEEDED (hr) && music.existsAsFile())
    {
        registerAllFormats (fm);
        reader.reset (fm.createReaderFor (music));
        if (reader != nullptr)
        {
            IMFMediaType* t = nullptr;
            MFCreateMediaType (&t);
            t->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            t->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_AAC);
            t->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
            t->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, 2);
            t->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            t->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
            HRESULT ha = w->AddStream (t, &as);
            t->Release();
            if (SUCCEEDED (ha))
            {
                MFCreateMediaType (&t);
                t->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                t->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_PCM);
                t->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
                t->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
                t->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, 2);
                t->SetUINT32 (MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
                t->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 48000 * 4);
                ha = w->SetInputMediaType (as, t, nullptr);
                t->Release();
            }
            if (SUCCEEDED (ha))
            {
                withAudio = true;
                src = std::make_unique<AudioFormatReaderSource> (reader.get(), false);
                src->setLooping (true);   // krátka hudba sa opakuje
                rs = std::make_unique<ResamplingAudioSource> (src.get(), false, 2);
                rs->setResamplingRatio (reader->sampleRate / 48000.0);
                rs->prepareToPlay (1600, 48000.0);
            }
        }
    }
    if (SUCCEEDED (hr)) hr = w->BeginWriting();
    if (FAILED (hr)) { w->Release(); output.deleteFile(); error = tr ("Kodér videa sa nedá spustiť."); finished = true; return; }

    Image frame (Image::ARGB, W, H, true, SoftwareImageType());
    Image cur, next;
    int loadedCur = -1, loadedNext = -1;
    const double totalSec = (double) total / FPS;
    AudioBuffer<float> ab (2, 1600);   // 1600 vzoriek = 1 snímok pri 48 kHz/30 fps
    for (int fi = 0; fi < total && SUCCEEDED (hr); ++fi)
    {
        if (cancelled) break;
        const int idx = fi / framesPer;
        const double tIn = (double) (fi % framesPer) / FPS;   // čas v rámci fotky
        if (loadedCur != idx) { cur = (idx == loadedNext) ? next : img::loadFull (images[idx], 2400).image; loadedCur = idx; }
        float mix = 0.0f;
        const bool hasNext = idx + 1 < images.size();
        if (hasNext && tIn > perImage - fade)
        {
            if (loadedNext != idx + 1) { next = img::loadFull (images[idx + 1], 2400).image; loadedNext = idx + 1; }
            mix = (float) ((tIn - (perImage - fade)) / fade);
        }
        const float zA = 1.0f + 0.12f * (float) (tIn / perImage);
        const float zB = 1.0f;   // nasledujúca fotka sa začne hýbať až po prelínaní
        {
            Graphics g (frame);
            renderFrame (g, W, H, cur, next, mix, zA, zB, idx, idx + 1);
            // stmavnutie na začiatku a konci videa
            const double t = (double) fi / FPS;
            const float edge = (float) jmin (1.0, jmin (t / 0.8, (totalSec - t) / 1.2));
            if (edge < 1.0f) g.fillAll (Colours::black.withAlpha (1.0f - jmax (0.0f, edge)));
        }
        IMFMediaBuffer* mb = nullptr;
        hr = MFCreateMemoryBuffer ((DWORD) (W * H * 4), &mb);
        if (FAILED (hr)) break;
        BYTE* p = nullptr;
        mb->Lock (&p, nullptr, nullptr);
        {
            Image::BitmapData bd (frame, Image::BitmapData::readOnly);
            for (int y = 0; y < H; ++y)   // RGB32 je zdola nahor
                memcpy (p + (size_t) (H - 1 - y) * (size_t) W * 4, bd.getLinePointer (y), (size_t) W * 4);
        }
        mb->Unlock();
        mb->SetCurrentLength ((DWORD) (W * H * 4));
        IMFSample* smp = nullptr;
        MFCreateSample (&smp);
        smp->AddBuffer (mb);
        smp->SetSampleTime (fi * frameDur);
        smp->SetSampleDuration (frameDur);
        hr = w->WriteSample (vs, smp);
        smp->Release();
        mb->Release();

        if (withAudio && SUCCEEDED (hr))
        {
            AudioSourceChannelInfo info (&ab, 0, 1600);
            ab.clear();
            rs->getNextAudioBlock (info);
            const double t = (double) fi / FPS;
            const float gain = (float) jlimit (0.0, 1.0, jmin (t / 0.5, (totalSec - t) / 2.5));
            IMFMediaBuffer* amb = nullptr;
            MFCreateMemoryBuffer (1600 * 4, &amb);
            BYTE* ap = nullptr;
            amb->Lock (&ap, nullptr, nullptr);
            auto* s16 = reinterpret_cast<int16*> (ap);
            const int srcCh = jmin (2, (int) reader->numChannels);
            for (int i = 0; i < 1600; ++i)
                for (int c = 0; c < 2; ++c)
                    *s16++ = (int16) jlimit (-32768, 32767, roundToInt (ab.getSample (srcCh > 1 ? c : 0, i) * gain * 32767.0f));
            amb->Unlock();
            amb->SetCurrentLength (1600 * 4);
            IMFSample* as2 = nullptr;
            MFCreateSample (&as2);
            as2->AddBuffer (amb);
            as2->SetSampleTime (fi * frameDur);
            as2->SetSampleDuration (frameDur);
            hr = w->WriteSample (as, as2);
            as2->Release();
            amb->Release();
        }
        prog = (double) (fi + 1) / (double) total;
    }
    if (SUCCEEDED (hr) && ! cancelled) hr = w->Finalize();
    w->Release();
    if (rs != nullptr) rs->releaseResources();
    if (cancelled || FAILED (hr)) { output.deleteFile(); if (! cancelled) error = tr ("Kódovanie videa sa nepodarilo."); }
    finished = true;
}
#else
void VideoExport::run() { error = tr ("Video sa dá vytvoriť len vo Windows."); finished = true; }
#endif
}
