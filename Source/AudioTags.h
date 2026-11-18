#pragma once
#include "Common.h"

namespace kp
{
// Názov, interpret, album, rok a obal z MP3 (ID3), FLAC, M4A; obal aj z cover.jpg v priečinku
struct AudioTags
{
    String title, artist, album, year;
    Image cover;
    bool empty() const { return title.isEmpty() && artist.isEmpty() && album.isEmpty(); }
    String displayName (const File& f) const
    {
        if (title.isEmpty()) return f.getFileNameWithoutExtension();
        return artist.isNotEmpty() ? artist + tr (" – ") + title : title;
    }
};

AudioTags readAudioTags (const File& f);
}
