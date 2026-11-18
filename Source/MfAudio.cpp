#include "Common.h"

// AAC / M4A / Opus… cez Windows Media Foundation (streamované čítanie s posunom)
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <mfapi.h>
 #include <mfidl.h>
 #include <mfreadwrite.h>
 #include <propvarutil.h>
#endif

namespace kp
{
#if JUCE_WINDOWS
namespace
{
    void ensureMf()
    {
        thread_local bool com = false;
        if (! com) { CoInitializeEx (nullptr, COINIT_MULTITHREADED); com = true; }
        static bool started = SUCCEEDED (MFStartup (MF_VERSION, MFSTARTUP_LITE));
        ignoreUnused (started);
    }

    class MfReader : public AudioFormatReader
    {
    public:
        MfReader (InputStream* in, const File& f) : AudioFormatReader (in, "Media Foundation"), file (f) {}

        ~MfReader() override { if (reader) reader->Release(); }

        bool open()
        {
            ensureMf();
            if (FAILED (MFCreateSourceReaderFromURL (file.getFullPathName().toWideCharPointer(), nullptr, &reader))) return false;
            reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE);
            if (FAILED (reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE))) return false;

            IMFMediaType* native = nullptr;
            UINT32 nativeBits = 0;
            if (SUCCEEDED (reader->GetNativeMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &native)))
            {
                native->GetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, &nativeBits);
                native->Release();
            }

            IMFMediaType* type = nullptr;
            if (FAILED (MFCreateMediaType (&type))) return false;
            type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            type->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_Float);
            const HRESULT hr = reader->SetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type);
            type->Release();
            if (FAILED (hr)) return false;

            IMFMediaType* cur = nullptr;
            if (FAILED (reader->GetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, &cur))) return false;
            UINT32 ch = 0, sr = 0;
            cur->GetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, &ch);
            cur->GetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
            cur->Release();
            if (ch == 0 || sr == 0) return false;

            PROPVARIANT v;
            PropVariantInit (&v);
            int64 dur100ns = 0;
            if (SUCCEEDED (reader->GetPresentationAttribute ((DWORD) MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &v)))
                dur100ns = (int64) v.uhVal.QuadPart;
            PropVariantClear (&v);

            numChannels = ch;
            sampleRate = sr;
            bitsPerSample = nativeBits > 0 ? nativeBits : 16;
            lengthInSamples = (int64) ((double) dur100ns * sr / 1.0e7);
            usesFloatingPointData = true;
            return lengthInSamples > 0;
        }

        bool readSamples (int* const* dest, int numDest, int destOffset, int64 start, int num) override
        {
            ensureMf();
            if (! valid || start < bufStart || start > bufEnd())
                seekTo (start);

            int done = 0;
            while (done < num)
            {
                const int64 want = start + done;
                // z vyrovnávacej pamäte
                if (want >= bufStart && want < bufEnd())
                {
                    const int offset = (int) (want - bufStart);
                    const int n = jmin (num - done, (int) (bufEnd() - want));
                    for (int c = 0; c < numDest; ++c)
                    {
                        if (dest[c] == nullptr) continue;
                        auto* d = reinterpret_cast<float*> (dest[c]) + destOffset + done;
                        const int src = jmin (c, (int) numChannels - 1);
                        for (int i = 0; i < n; ++i) d[i] = buf[(size_t) ((offset + i) * (int) numChannels + src)];
                    }
                    done += n;
                    continue;
                }
                if (! decodeNext (want)) break;
            }
            for (int c = 0; c < numDest; ++c)
                if (dest[c] != nullptr && done < num)
                    FloatVectorOperations::clear (reinterpret_cast<float*> (dest[c]) + destOffset + done, num - done);
            return true;
        }

    private:
        int64 bufEnd() const { return bufStart + (int64) (buf.size() / jmax (1u, numChannels)); }

        void seekTo (int64 pos)
        {
            PROPVARIANT v;
            InitPropVariantFromInt64 ((LONGLONG) ((double) pos * 1.0e7 / sampleRate), &v);
            reader->SetCurrentPosition (GUID_NULL, v);
            PropVariantClear (&v);
            buf.clear();
            bufStart = pos;
            needTimestamp = true;
            eof = false;
            valid = true;
        }

        // dekóduje ďalší blok; po posune zahodí vzorky pred 'want'
        bool decodeNext (int64 want)
        {
            if (eof) return false;
            DWORD flags = 0;
            LONGLONG ts = 0;
            IMFSample* sample = nullptr;
            if (FAILED (reader->ReadSample ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, &ts, &sample)))
            {
                eof = true;
                return false;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { eof = true; if (sample) sample->Release(); return false; }
            if (sample == nullptr) return true;

            IMFMediaBuffer* mb = nullptr;
            if (SUCCEEDED (sample->ConvertToContiguousBuffer (&mb)))
            {
                BYTE* data = nullptr;
                DWORD len = 0;
                if (SUCCEEDED (mb->Lock (&data, nullptr, &len)))
                {
                    const size_t floats = len / sizeof (float);
                    const int64 frameStart = needTimestamp ? (int64) std::llround ((double) ts * sampleRate / 1.0e7) : bufEnd();
                    needTimestamp = false;
                    const int64 frames = (int64) (floats / numChannels);
                    // zahodiť, čo je pred požadovanou vzorkou
                    int64 skip = jlimit<int64> (0, frames, want - frameStart);
                    if (frameStart > want) skip = 0;
                    buf.assign (reinterpret_cast<float*> (data) + skip * numChannels, reinterpret_cast<float*> (data) + floats);
                    bufStart = frameStart + skip;
                    if (bufStart > want) bufStart = want;   // malé nepresnosti časovej značky
                    mb->Unlock();
                }
                mb->Release();
            }
            sample->Release();
            return true;
        }

        File file;
        IMFSourceReader* reader = nullptr;
        std::vector<float> buf;
        int64 bufStart = 0;
        bool needTimestamp = true, eof = false, valid = false;
    };

    class MfAudioFormat : public AudioFormat
    {
    public:
        MfAudioFormat() : AudioFormat ("Media Foundation", ".m4a .aac .m4b .opus .alac .mp4a .adts .ac3 .ec3 .amr .3ga") {}
        Array<int> getPossibleSampleRates() override { return {}; }
        Array<int> getPossibleBitDepths() override { return {}; }
        bool canDoStereo() override { return true; }
        bool canDoMono() override { return true; }

        AudioFormatReader* createReaderFor (InputStream* in, bool deleteIfFails) override
        {
            if (auto* fis = dynamic_cast<FileInputStream*> (in))
            {
                auto r = std::make_unique<MfReader> (in, fis->getFile());
                if (r->open()) return r.release();
                r->input = nullptr;
            }
            if (deleteIfFails) delete in;
            return nullptr;
        }

        std::unique_ptr<AudioFormatWriter> createWriterFor (std::unique_ptr<OutputStream>&, const AudioFormatWriterOptions&) override
        {
            return nullptr;
        }
    };
}

void registerMediaFoundation (AudioFormatManager& fm) { fm.registerFormat (new MfAudioFormat(), false); }

#else

void registerMediaFoundation (AudioFormatManager&) {}

#endif
}
