#include "AudioTags.h"
#include <cstring>

namespace kp
{
namespace
{
    uint32 be32 (const uint8* p) { return ((uint32) p[0] << 24) | ((uint32) p[1] << 16) | ((uint32) p[2] << 8) | p[3]; }
    uint32 le32 (const uint8* p) { return (uint32) p[0] | ((uint32) p[1] << 8) | ((uint32) p[2] << 16) | ((uint32) p[3] << 24); }
    uint32 syncsafe (const uint8* p) { return ((uint32) (p[0] & 0x7f) << 21) | ((uint32) (p[1] & 0x7f) << 14) | ((uint32) (p[2] & 0x7f) << 7) | (p[3] & 0x7f); }

    Image decodeImage (const uint8* d, size_t n)
    {
        if (n < 8) return {};
        MemoryInputStream mis (d, n, false);
        auto im = ImageFileFormat::loadFrom (mis);
        return im.isValid() ? SoftwareImageType().convert (im) : im;
    }

    // ID3 text: prvý bajt = kódovanie
    String id3Text (const uint8* d, size_t n)
    {
        if (n < 1) return {};
        const uint8 enc = d[0];
        ++d; --n;
        if (enc == 0) { String s; for (size_t i = 0; i < n && d[i]; ++i) s << String::charToString ((juce_wchar) d[i]); return s; }
        if (enc == 3) return String (CharPointer_UTF8 ((const char*) d), n).upToFirstOccurrenceOf (String::charToString (0), false, false).trim();
        // UTF-16 (s BOM alebo BE)
        bool little = enc == 1;
        if (n >= 2 && enc == 1) { little = d[0] == 0xff; d += 2; n -= 2; }
        String s;
        for (size_t i = 0; i + 1 < n; i += 2)
        {
            const juce_wchar c = little ? (juce_wchar) (d[i] | (d[i + 1] << 8)) : (juce_wchar) ((d[i] << 8) | d[i + 1]);
            if (c == 0) break;
            s << String::charToString (c);
        }
        return s.trim();
    }

    // dĺžka ukončeného textu v danom kódovaní (vrátane nuly)
    size_t skipString (const uint8* d, size_t n, uint8 enc)
    {
        if (enc == 1 || enc == 2)
        {
            for (size_t i = 0; i + 1 < n; i += 2) if (d[i] == 0 && d[i + 1] == 0) return i + 2;
            return n;
        }
        for (size_t i = 0; i < n; ++i) if (d[i] == 0) return i + 1;
        return n;
    }

    void readId3 (const MemoryBlock& mb, AudioTags& t)
    {
        auto* d = static_cast<const uint8*> (mb.getData());
        const size_t n = mb.getSize();
        if (n < 10 || std::memcmp (d, "ID3", 3) != 0) return;
        const int ver = d[3];
        const size_t size = jmin<size_t> (syncsafe (d + 6) + 10, n);
        size_t pos = 10;
        if (d[5] & 0x40) pos += ver == 4 ? syncsafe (d + 10) : be32 (d + 10) + 4;   // rozšírená hlavička
        const bool v22 = ver == 2;
        const size_t hdr = v22 ? 6 : 10;
        while (pos + hdr < size)
        {
            const uint8* f = d + pos;
            if (f[0] == 0) break;
            const String id = String::createStringFromData (f, v22 ? 3 : 4);
            size_t len = v22 ? (size_t) ((f[3] << 16) | (f[4] << 8) | f[5]) : (ver == 4 ? syncsafe (f + 4) : be32 (f + 4));
            if (len == 0 || pos + hdr + len > size) break;
            const uint8* body = f + hdr;
            if (id == "TIT2" || id == "TT2") t.title = id3Text (body, len);
            else if (id == "TPE1" || id == "TP1") t.artist = id3Text (body, len);
            else if (id == "TALB" || id == "TAL") t.album = id3Text (body, len);
            else if ((id == "TYER" || id == "TDRC" || id == "TYE") && t.year.isEmpty()) t.year = id3Text (body, len).substring (0, 4);
            else if ((id == "APIC" || id == "PIC") && ! t.cover.isValid() && len > 4)
            {
                const uint8 enc = body[0];
                size_t p = 1;
                if (v22) p += 3;                               // formát obrázka (3 znaky)
                else p += skipString (body + p, len - p, 0);  // MIME
                p += 1;                                         // typ obrázka
                p += skipString (body + p, len - p, enc);      // popis
                if (p < len) t.cover = decodeImage (body + p, len - p);
            }
            pos += hdr + len;
        }
    }

    void readFlac (const MemoryBlock& mb, AudioTags& t)
    {
        auto* d = static_cast<const uint8*> (mb.getData());
        const size_t n = mb.getSize();
        if (n < 8 || std::memcmp (d, "fLaC", 4) != 0) return;
        size_t pos = 4;
        while (pos + 4 <= n)
        {
            const bool last = (d[pos] & 0x80) != 0;
            const int type = d[pos] & 0x7f;
            const size_t len = (size_t) ((d[pos + 1] << 16) | (d[pos + 2] << 8) | d[pos + 3]);
            const uint8* b = d + pos + 4;
            if (pos + 4 + len > n) break;
            if (type == 4 && len >= 8)
            {
                size_t p = 4 + le32 (b);
                const uint32 count = p + 4 <= len ? le32 (b + p) : 0;
                p += 4;
                for (uint32 i = 0; i < count && p + 4 <= len; ++i)
                {
                    const uint32 l = le32 (b + p);
                    p += 4;
                    if (p + l > len) break;
                    const String c (CharPointer_UTF8 ((const char*) b + p), l);
                    const auto key = c.upToFirstOccurrenceOf ("=", false, false).toUpperCase();
                    const auto val = c.fromFirstOccurrenceOf ("=", false, false);
                    if (key == "TITLE") t.title = val;
                    else if (key == "ARTIST" && t.artist.isEmpty()) t.artist = val;
                    else if (key == "ALBUM") t.album = val;
                    else if (key == "DATE" && t.year.isEmpty()) t.year = val.substring (0, 4);
                    p += l;
                }
            }
            else if (type == 6 && len > 32 && ! t.cover.isValid())
            {
                size_t p = 4;
                p += 4 + be32 (b + p);         // MIME
                if (p + 4 > len) break;
                p += 4 + be32 (b + p);         // popis
                p += 16;                       // rozmery, farby
                if (p + 4 > len) break;
                const uint32 dl = be32 (b + p);
                p += 4;
                if (p + dl <= len) t.cover = decodeImage (b + p, dl);
            }
            if (last) break;
            pos += 4 + len;
        }
    }

    // MP4 / M4A: moov/udta/meta/ilst (typy atómov ako 4 bajty)
    constexpr uint32 fourcc (uint8 a, uint8 b, uint8 c, uint8 d) { return ((uint32) a << 24) | ((uint32) b << 16) | ((uint32) c << 8) | d; }

    void readMp4Atoms (const uint8* d, size_t n, AudioTags& t, int depth, uint32 parent)
    {
        size_t pos = 0;
        while (pos + 8 <= n && depth < 8)
        {
            size_t size = be32 (d + pos);
            const uint32 type = be32 (d + pos + 4);
            size_t hdr = 8;
            if (size == 1 && pos + 16 <= n) { size = (size_t) (((uint64) be32 (d + pos + 8) << 32) | be32 (d + pos + 12)); hdr = 16; }
            if (size == 0) size = n - pos;
            if (size < hdr || pos + size > n) break;
            const uint8* body = d + pos + hdr;
            const size_t blen = size - hdr;
            if (type == fourcc ('m','o','o','v') || type == fourcc ('u','d','t','a') || type == fourcc ('i','l','s','t'))
                readMp4Atoms (body, blen, t, depth + 1, type);
            else if (type == fourcc ('m','e','t','a') && blen > 4)
                readMp4Atoms (body + 4, blen - 4, t, depth + 1, type);
            else if (parent == fourcc ('i','l','s','t'))
                readMp4Atoms (body, blen, t, depth + 1, type);    // položka: obsahuje atóm data
            else if (type == fourcc ('d','a','t','a') && blen > 8)
            {
                const uint8* v = body + 8;
                const size_t vl = blen - 8;
                const String txt (CharPointer_UTF8 ((const char*) v), vl);
                if (parent == fourcc (0xa9,'n','a','m')) t.title = txt;
                else if (parent == fourcc (0xa9,'A','R','T')) t.artist = txt;
                else if (parent == fourcc ('a','A','R','T') && t.artist.isEmpty()) t.artist = txt;
                else if (parent == fourcc (0xa9,'a','l','b')) t.album = txt;
                else if (parent == fourcc (0xa9,'d','a','y')) t.year = txt.substring (0, 4);
                else if (parent == fourcc ('c','o','v','r') && ! t.cover.isValid()) t.cover = decodeImage (v, vl);
            }
            pos += size;
        }
    }

    Image folderCover (const File& f)
    {
        for (auto name : { "cover", "folder", "front", "album", "albumart", "albumartsmall" })
            for (auto ext : { ".jpg", ".jpeg", ".png" })
            {
                // Windows nerozlišuje veľkosť písmen; inde skúsime aj veľké začiatočné písmeno
                for (auto n : { String (name), String (name).substring (0, 1).toUpperCase() + String (name).substring (1) })
                {
                    auto c = f.getSiblingFile (n + ext);
                    if (c.existsAsFile())
                    {
                        auto im = ImageFileFormat::loadFrom (c);
                        if (im.isValid()) return SoftwareImageType().convert (im);
                    }
                }
            }
        return {};
    }
}

AudioTags readAudioTags (const File& f)
{
    AudioTags t;
    FileInputStream in (f);
    if (in.openedOk())
    {
        MemoryBlock head;
        const auto e = f.getFileExtension().toLowerCase();
        if (e == ".m4a" || e == ".m4b" || e == ".mp4" || e == ".aac" || e == ".alac")
        {
            // atóm moov môže byť na začiatku alebo na konci
            in.readIntoMemoryBlock (head, jmin<int64> (in.getTotalLength(), 64 * 1024 * 1024));
            readMp4Atoms (static_cast<const uint8*> (head.getData()), head.getSize(), t, 0, 0);
        }
        else
        {
            in.readIntoMemoryBlock (head, 16 * 1024 * 1024);
            readId3 (head, t);
            readFlac (head, t);
        }
    }
    if (! t.cover.isValid()) t.cover = folderCover (f);
    if (t.cover.isValid() && jmax (t.cover.getWidth(), t.cover.getHeight()) > 800)
        t.cover = t.cover.rescaled (t.cover.getWidth() * 800 / jmax (t.cover.getWidth(), t.cover.getHeight()),
                                    t.cover.getHeight() * 800 / jmax (t.cover.getWidth(), t.cover.getHeight()),
                                    Graphics::highResamplingQuality);
    return t;
}
}
