#pragma once
#include "Common.h"

namespace kp
{
// MIDI súbor: načítanie, noty na kreslenie a prehrávanie cez MIDI výstup
// (vo Windows je predvolený zabudovaný „Microsoft GS Wavetable Synth“).
class MidiPlayer
{
public:
    struct Note { double start, end; int pitch, channel; };

    struct Summary
    {
        bool ok = false;
        double length = 0;
        int tracks = 0, notes = 0;
    };

    ~MidiPlayer();

    // Načítanie bez prehrávania (aj z vlákna na pozadí)
    static bool parse (const File& f, MidiMessageSequence& merged, Summary& summary);

    bool load (const File& f);
    void unload();

    bool play (double fromSeconds);   // false = nie je MIDI výstup
    void stop();
    bool isPlaying();
    double getPosition();
    double getLength() const { return summary.length; }
    const Summary& getSummary() const { return summary; }
    const std::vector<Note>& getNotes() const { return notes; }
    int getLowest() const { return lowest; }
    int getHighest() const { return highest; }
    String getDeviceName() const { return deviceName; }

private:
    bool ensureOutput();
    void silence();

    MidiMessageSequence seq;
    Summary summary;
    std::vector<Note> notes;
    int lowest = 60, highest = 72;

    std::unique_ptr<MidiOutput> out;
    String deviceName;
    bool outputFailed = false;
    bool playing = false;
    double startMs = 0, startPos = 0, pausedPos = 0;
};
}
