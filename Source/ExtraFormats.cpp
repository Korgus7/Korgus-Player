#include "ExtraFormats.h"
#include "velociloops.h"
#include <cmath>
#include <cstring>

namespace kp
{
namespace
{
    // Čítačka nad dekódovaným zvukom v pamäti
    class BufferReader : public AudioFormatReader
    {
    public:
        BufferReader (InputStream* in, const String& name, AudioBuffer<float>&& b, double sr, int bits)
            : AudioFormatReader (in, name), buffer (std::move (b))
        {
            sampleRate = sr;
            bitsPerSample = (unsigned int) jmax (8, bits);
            lengthInSamples = buffer.getNumSamples();
            numChannels = (unsigned int) buffer.getNumChannels();
            usesFloatingPointData = true;
        }

        bool readSamples (int* const* dest, int numDest, int destOffset, int64 start, int num) override
        {
            for (int ch = 0; ch < numDest; ++ch)
            {
                if (dest[ch] == nullptr) continue;
                auto* d = reinterpret_cast<float*> (dest[ch]) + destOffset;
                const int srcCh = jmin (ch, buffer.getNumChannels() - 1);
                const int64 avail = jlimit<int64> (0, num, (int64) buffer.getNumSamples() - start);
                if (avail > 0) FloatVectorOperations::copy (d, buffer.getReadPointer (srcCh, (int) start), (int) avail);
                if (avail < num) FloatVectorOperations::clear (d + avail, (int) (num - avail));
            }
            return true;
        }

    private:
        AudioBuffer<float> buffer;
    };

    bool readAll (InputStream& in, MemoryBlock& mb)
    {
        in.setPosition (0);
        mb.reset();
        in.readIntoMemoryBlock (mb);
        return mb.getSize() > 12;
    }

    class RexAudioFormat : public AudioFormat
    {
    public:
        RexAudioFormat() : AudioFormat ("REX loop", ".rx2 .rex .rcy") {}
        Array<int> getPossibleSampleRates() override { return {}; }
        Array<int> getPossibleBitDepths() override { return {}; }
        bool canDoStereo() override { return true; }
        bool canDoMono() override { return true; }

        AudioFormatReader* createReaderFor (InputStream* in, bool deleteIfFails) override
        {
            MemoryBlock mb;
            AudioBuffer<float> buf;
            double sr = 0, bpm = 0;
            int bits = 16;
            if (in != nullptr && readAll (*in, mb) && decodeRex (mb.getData(), mb.getSize(), buf, sr, bits, bpm))
            {
                auto* r = new BufferReader (in, getFormatName(), std::move (buf), sr, bits);
                r->metadataValues.set ("bpm", String (bpm, 2));
                return r;
            }
            if (deleteIfFails) delete in;
            return nullptr;
        }

        std::unique_ptr<AudioFormatWriter> createWriterFor (std::unique_ptr<OutputStream>&, const AudioFormatWriterOptions&) override
        {
            return nullptr;
        }
    };

    class AifcAudioFormat : public AudioFormat
    {
    public:
        AifcAudioFormat() : AudioFormat ("AIFC", ".aifc .aif .aiff .afc") {}
        Array<int> getPossibleSampleRates() override { return {}; }
        Array<int> getPossibleBitDepths() override { return {}; }
        bool canDoStereo() override { return true; }
        bool canDoMono() override { return true; }

        AudioFormatReader* createReaderFor (InputStream* in, bool deleteIfFails) override
        {
            MemoryBlock mb;
            AudioBuffer<float> buf;
            double sr = 0;
            int bits = 16;
            if (in != nullptr && readAll (*in, mb) && decodeAifc (mb.getData(), mb.getSize(), buf, sr, bits))
                return new BufferReader (in, getFormatName(), std::move (buf), sr, bits);
            if (deleteIfFails) delete in;
            return nullptr;
        }

        std::unique_ptr<AudioFormatWriter> createWriterFor (std::unique_ptr<OutputStream>&, const AudioFormatWriterOptions&) override
        {
            return nullptr;
        }
    };


    // ---------------------------------------------------------------- Sony Wave64 (.w64)
    class W64Reader : public AudioFormatReader
    {
    public:
        explicit W64Reader (InputStream* in) : AudioFormatReader (in, "Wave64") {}

        bool parse()
        {
            uint8 h[40];
            if (input->read (h, 40) != 40) return false;
            if (std::memcmp (h, "riff", 4) != 0 || std::memcmp (h + 24, "wave", 4) != 0) return false;

            int64 pos = 40;
            const int64 total = input->getTotalLength();
            bool haveFmt = false;
            while (pos + 24 <= total)
            {
                input->setPosition (pos);
                uint8 c[24];
                if (input->read (c, 24) != 24) break;
                uint64 size = 0;
                for (int i = 7; i >= 0; --i) size = (size << 8) | c[16 + i];
                if (size < 24) break;
                if (std::memcmp (c, "fmt ", 4) == 0)
                {
                    uint8 f[40] = {};
                    input->read (f, (int) jmin<uint64> (40, size - 24));
                    tag = f[0] | (f[1] << 8);
                    numChannels = (unsigned int) (f[2] | (f[3] << 8));
                    sampleRate = (double) (f[4] | (f[5] << 8) | (f[6] << 16) | ((uint32) f[7] << 24));
                    blockAlign = f[12] | (f[13] << 8);
                    bitsPerSample = (unsigned int) (f[14] | (f[15] << 8));
                    if (tag == 0xfffe && size - 24 >= 40) tag = f[24] | (f[25] << 8);   // WAVE_FORMAT_EXTENSIBLE
                    haveFmt = true;
                }
                else if (std::memcmp (c, "data", 4) == 0)
                {
                    dataStart = pos + 24;
                    dataBytes = jmin<int64> ((int64) size - 24, total - dataStart);
                    break;
                }
                pos += (int64) ((size + 7) & ~(uint64) 7);
            }
            if (! haveFmt || dataStart < 0 || numChannels == 0 || numChannels > 32 || sampleRate <= 0) return false;
            if (tag != 1 && tag != 3) return false;
            bytesPer = (int) bitsPerSample / 8;
            if (blockAlign <= 0) blockAlign = bytesPer * (int) numChannels;
            if (bytesPer < 1 || bytesPer > 8 || blockAlign < bytesPer * (int) numChannels) return false;
            if (tag == 3 && bytesPer != 4 && bytesPer != 8) return false;
            lengthInSamples = dataBytes / blockAlign;
            usesFloatingPointData = true;
            return true;
        }

        bool readSamples (int* const* dest, int numDest, int destOffset, int64 start, int num) override
        {
            const int64 avail = jlimit<int64> (0, num, lengthInSamples - start);
            if (avail > 0)
            {
                temp.ensureSize ((size_t) (avail * blockAlign));
                input->setPosition (dataStart + start * blockAlign);
                const int got = input->read (temp.getData(), (int) (avail * blockAlign));
                const int frames = got / blockAlign;
                auto* b = static_cast<const uint8*> (temp.getData());
                for (int ch = 0; ch < numDest; ++ch)
                {
                    if (dest[ch] == nullptr) continue;
                    auto* d = reinterpret_cast<float*> (dest[ch]) + destOffset;
                    const int srcCh = jmin (ch, (int) numChannels - 1);
                    for (int i = 0; i < frames; ++i)
                        d[i] = sampleAt (b + (size_t) i * (size_t) blockAlign + (size_t) srcCh * (size_t) bytesPer);
                    if (frames < num) FloatVectorOperations::clear (d + frames, num - frames);
                }
                return true;
            }
            for (int ch = 0; ch < numDest; ++ch)
                if (dest[ch] != nullptr) FloatVectorOperations::clear (reinterpret_cast<float*> (dest[ch]) + destOffset, num);
            return true;
        }

    private:
        float sampleAt (const uint8* p) const
        {
            if (tag == 3)
            {
                if (bytesPer == 4) { float f; std::memcpy (&f, p, 4); return f; }
                double v; std::memcpy (&v, p, 8); return (float) v;
            }
            if (bytesPer == 1) return ((int) p[0] - 128) / 128.0f;
            int64 x = 0;
            for (int i = bytesPer - 1; i >= 0; --i) x = (x << 8) | p[i];
            const int shift = 64 - bytesPer * 8;
            x = (int64) ((uint64) x << shift) >> shift;
            return (float) ((double) x / (double) ((int64) 1 << (bytesPer * 8 - 1)));
        }

        int tag = 0, blockAlign = 0, bytesPer = 0;
        int64 dataStart = -1, dataBytes = 0;
        MemoryBlock temp;
    };

    class W64AudioFormat : public AudioFormat
    {
    public:
        W64AudioFormat() : AudioFormat ("Wave64", ".w64") {}
        Array<int> getPossibleSampleRates() override { return {}; }
        Array<int> getPossibleBitDepths() override { return {}; }
        bool canDoStereo() override { return true; }
        bool canDoMono() override { return true; }

        AudioFormatReader* createReaderFor (InputStream* in, bool deleteIfFails) override
        {
            if (in != nullptr)
            {
                auto r = std::make_unique<W64Reader> (in);
                if (r->parse()) return r.release();
                r->input = nullptr;   // stream patrí volajúcemu, ak zlyhá
            }
            if (deleteIfFails) delete in;
            return nullptr;
        }

        std::unique_ptr<AudioFormatWriter> createWriterFor (std::unique_ptr<OutputStream>&, const AudioFormatWriterOptions&) override
        {
            return nullptr;
        }
    };

    // ---------------------------------------------------------------- AIFC pomocníci
    uint32 be32 (const uint8* p) { return ((uint32) p[0] << 24) | ((uint32) p[1] << 16) | ((uint32) p[2] << 8) | p[3]; }
    uint16 be16 (const uint8* p) { return (uint16) ((p[0] << 8) | p[1]); }

    double extended80 (const uint8* b)
    {
        const int exp = ((b[0] & 0x7f) << 8) | b[1];
        uint64 mant = 0;
        for (int i = 2; i < 10; ++i) mant = (mant << 8) | b[i];
        if (exp == 0 && mant == 0) return 0.0;
        const double v = std::ldexp ((double) mant, exp - 16383 - 63);
        return (b[0] & 0x80) ? -v : v;
    }

    float ulaw (uint8 u)
    {
        u = (uint8) ~u;
        const int sign = u & 0x80, exponent = (u >> 4) & 7, mantissa = u & 0x0f;
        int s = ((mantissa << 3) + 0x84) << exponent;
        s -= 0x84;
        return (float) (sign ? -s : s) / 32768.0f;
    }

    float alaw (uint8 a)
    {
        a ^= 0x55;
        const int sign = a & 0x80, exponent = (a >> 4) & 7, mantissa = a & 0x0f;
        int s = exponent == 0 ? (mantissa << 4) + 8 : ((mantissa << 4) + 0x108) << (exponent - 1);
        return (float) (sign ? s : -s) / 32768.0f;
    }

    const int imaIndex[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
    const int imaStep[89] = {
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
        107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
        5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
        27086, 29794, 32767 };
}

// ---------------------------------------------------------------- REX

bool decodeRex (const void* data, size_t size, AudioBuffer<float>& out, double& sampleRate, int& bits, double& bpm)
{
    VLError err = VL_OK;
    VLFile f = vl_open_from_memory (data, size, &err);
    if (f == nullptr) return false;

    VLFileInfo info {};
    if (vl_get_info (f, &info) != VL_OK || info.sample_rate <= 0 || info.tempo <= 0)
    {
        vl_close (f);
        return false;
    }

    sampleRate = info.sample_rate;
    bits = info.bit_depth > 0 ? info.bit_depth : 16;

    // Tempo: pôvodné (ako bol loop nahraný) alebo nastavené v ReCycle. Vyberie sa to,
    // pri ktorom dĺžka loopu najlepšie sedí na nahraté audio – loop tak znie prirodzene.
    // 15360 tikov na dobu (štvrťovú notu)
    auto loopFrames = [&] (int tempo) { return info.ppq_length * (info.sample_rate * 60.0 / (tempo / 1000.0) / 15360.0); };
    int tempo = info.tempo;
    if (info.original_tempo > 0 && info.total_frames > 0
        && std::abs (loopFrames (info.original_tempo) - info.total_frames) < std::abs (loopFrames (info.tempo) - info.total_frames))
        tempo = info.original_tempo;
    bpm = tempo / 1000.0;
    const double framesPerTick = info.sample_rate * 60.0 / bpm / 15360.0;
    const int chans = info.channels == 2 ? 2 : 1;
    int64 total = (int64) std::llround (info.ppq_length * framesPerTick);

    struct Piece { int64 start; std::vector<float> l, r; };
    std::vector<Piece> pieces;
    for (int i = 0; i < info.slice_count; ++i)
    {
        VLSliceInfo si {};
        if (vl_get_slice_info (f, i, &si) != VL_OK) continue;
        if (si.flags & VL_SLICE_FLAG_MUTED) continue;
        const int n = vl_get_slice_frame_count (f, i);
        if (n <= 0) continue;
        Piece p;
        p.start = (int64) std::llround (si.ppq_pos * framesPerTick);
        p.l.resize ((size_t) n);
        p.r.resize ((size_t) n);
        int32_t written = 0;
        if (vl_decode_slice (f, i, p.l.data(), chans == 2 ? p.r.data() : nullptr, 0, n, &written) != VL_OK) continue;
        p.l.resize ((size_t) written);
        p.r.resize ((size_t) written);
        total = jmax (total, p.start + (int64) written);
        pieces.push_back (std::move (p));
    }
    vl_close (f);

    if (total <= 0 || total > (int64) info.sample_rate * 60 * 30) return false;
    out.setSize (chans, (int) total);
    out.clear();
    for (auto& p : pieces)
    {
        const int n = (int) p.l.size();
        out.addFrom (0, (int) p.start, p.l.data(), n);
        if (chans == 2) out.addFrom (1, (int) p.start, p.r.data(), n);
    }
    return true;
}

// ---------------------------------------------------------------- AIFC

bool decodeAifc (const void* data, size_t size, AudioBuffer<float>& out, double& sampleRate, int& bits)
{
    auto* d = static_cast<const uint8*> (data);
    if (size < 12 || std::memcmp (d, "FORM", 4) != 0) return false;
    const bool isAifc = std::memcmp (d + 8, "AIFC", 4) == 0;
    if (! isAifc && std::memcmp (d + 8, "AIFF", 4) != 0) return false;

    int channels = 0, sampleSize = 0;
    uint32 frames = 0;
    char comp[5] = "NONE";
    const uint8* ssnd = nullptr;
    size_t ssndLen = 0;

    size_t pos = 12;
    while (pos + 8 <= size)
    {
        const uint8* c = d + pos;
        const size_t len = be32 (c + 4);
        const size_t body = pos + 8;
        if (body + len > size && std::memcmp (c, "SSND", 4) != 0) break;
        if (std::memcmp (c, "COMM", 4) == 0 && len >= 18)
        {
            channels = be16 (c + 8);
            frames = be32 (c + 10);
            sampleSize = be16 (c + 14);
            sampleRate = extended80 (c + 16);
            if (isAifc && len >= 22) { std::memcpy (comp, c + 26, 4); comp[4] = 0; }
        }
        else if (std::memcmp (c, "SSND", 4) == 0 && len >= 8)
        {
            const size_t offset = be32 (c + 8);
            const size_t start = body + 8 + offset;
            if (start <= size)
            {
                ssnd = d + start;
                ssndLen = jmin (len - 8 - offset, size - start);
            }
        }
        pos = body + len + (len & 1);
    }
    if (channels <= 0 || channels > 32 || ssnd == nullptr || sampleRate <= 0 || sampleRate > 768000) return false;

    String c (comp);
    const auto cl = c.toLowerCase();
    bits = sampleSize;

    if (cl == "ima4")
    {
        // 34 bajtov na paket a kanál, 64 vzoriek; COMM udáva počet paketov
        const size_t packetBytes = 34 * (size_t) channels;
        const size_t packets = jmin ((size_t) frames, ssndLen / packetBytes);
        if (packets == 0) return false;
        out.setSize (channels, (int) (packets * 64));
        for (size_t p = 0; p < packets; ++p)
            for (int ch = 0; ch < channels; ++ch)
            {
                const uint8* pk = ssnd + p * packetBytes + (size_t) ch * 34;
                const uint16 h = be16 (pk);
                int pred = (int16) (h & 0xff80);
                int idx = jlimit (0, 88, (int) (h & 0x7f));
                float* o = out.getWritePointer (ch, (int) (p * 64));
                for (int i = 0; i < 64; ++i)
                {
                    const uint8 b = pk[2 + i / 2];
                    const int n = (i & 1) ? (b >> 4) : (b & 0x0f);
                    const int step = imaStep[idx];
                    int diff = step >> 3;
                    if (n & 1) diff += step >> 2;
                    if (n & 2) diff += step >> 1;
                    if (n & 4) diff += step;
                    if (n & 8) diff = -diff;
                    pred = jlimit (-32768, 32767, pred + diff);
                    idx = jlimit (0, 88, idx + imaIndex[n]);
                    o[i] = (float) pred / 32768.0f;
                }
            }
        bits = 16;
        return true;
    }

    int bytesPer = 0;
    enum class Enc { PcmBE, PcmLE, F32, F64, ULaw, ALaw } enc = Enc::PcmBE;
    if (cl == "none" || cl == "twos") { bytesPer = (sampleSize + 7) / 8; enc = Enc::PcmBE; }
    else if (cl == "sowt") { bytesPer = (sampleSize + 7) / 8; enc = Enc::PcmLE; }
    else if (cl == "in24") { bytesPer = 3; enc = Enc::PcmBE; bits = 24; }
    else if (cl == "in32") { bytesPer = 4; enc = Enc::PcmBE; bits = 32; }
    else if (cl == "42ni") { bytesPer = 3; enc = Enc::PcmLE; bits = 24; }
    else if (cl == "23ni") { bytesPer = 4; enc = Enc::PcmLE; bits = 32; }
    else if (cl == "fl32") { bytesPer = 4; enc = Enc::F32; bits = 32; }
    else if (cl == "fl64") { bytesPer = 8; enc = Enc::F64; bits = 64; }
    else if (cl == "ulaw") { bytesPer = 1; enc = Enc::ULaw; bits = 16; }
    else if (cl == "alaw") { bytesPer = 1; enc = Enc::ALaw; bits = 16; }
    else return false;
    if (bytesPer < 1 || bytesPer > 8) return false;

    const size_t frameBytes = (size_t) bytesPer * (size_t) channels;
    const size_t n = jmin ((size_t) frames, ssndLen / frameBytes);
    if (n == 0) return false;
    out.setSize (channels, (int) n);

    for (size_t i = 0; i < n; ++i)
        for (int ch = 0; ch < channels; ++ch)
        {
            const uint8* s = ssnd + i * frameBytes + (size_t) ch * (size_t) bytesPer;
            float v = 0.0f;
            switch (enc)
            {
                case Enc::PcmBE:
                case Enc::PcmLE:
                {
                    int64 x = 0;
                    for (int b = 0; b < bytesPer; ++b)
                        x = (x << 8) | (enc == Enc::PcmBE ? s[b] : s[bytesPer - 1 - b]);
                    const int shift = 64 - bytesPer * 8;
                    x = (int64) ((uint64) x << shift) >> shift;           // znamienko
                    v = (float) ((double) x / (double) ((int64) 1 << (bytesPer * 8 - 1)));
                    break;
                }
                case Enc::F32:
                {
                    const uint32 u = be32 (s);
                    float f;
                    std::memcpy (&f, &u, 4);
                    v = f;
                    break;
                }
                case Enc::F64:
                {
                    uint64 u = 0;
                    for (int b = 0; b < 8; ++b) u = (u << 8) | s[b];
                    double f;
                    std::memcpy (&f, &u, 8);
                    v = (float) f;
                    break;
                }
                case Enc::ULaw: v = ulaw (s[0]); break;
                case Enc::ALaw: v = alaw (s[0]); break;
            }
            out.setSample (ch, (int) i, v);
        }
    return true;
}

void registerAllFormats (AudioFormatManager& fm)
{
    fm.registerBasicFormats();
    fm.registerFormat (new RexAudioFormat(), false);
    // AIFC až za JUCE AIFF: použije sa len keď JUCE súbor nevie otvoriť
    fm.registerFormat (new AifcAudioFormat(), false);
    fm.registerFormat (new W64AudioFormat(), false);
}
}
