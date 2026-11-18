#include "MidiPlayer.h"
#include <cstring>

namespace kp
{
MidiPlayer::~MidiPlayer()
{
    stop();
    if (out) out->stopBackgroundThread();
}

bool MidiPlayer::parse (const File& f, MidiMessageSequence& merged, Summary& s)
{
    s = {};
    merged.clear();
    FileInputStream in (f);
    if (! in.openedOk()) return false;

    MidiFile mf;
    // .rmi = RIFF obal okolo MIDI
    if (f.hasFileExtension ("rmi"))
    {
        MemoryBlock mb;
        in.readIntoMemoryBlock (mb);
        const auto* d = static_cast<const char*> (mb.getData());
        const auto n = mb.getSize();
        for (size_t i = 0; i + 4 < n; ++i)
            if (std::memcmp (d + i, "MThd", 4) == 0)
            {
                MemoryInputStream mis (d + i, n - i, false);
                if (! mf.readFrom (mis)) return false;
                break;
            }
        if (mf.getNumTracks() == 0) return false;
    }
    else if (! mf.readFrom (in))
        return false;

    mf.convertTimestampTicksToSeconds();
    for (int t = 0; t < mf.getNumTracks(); ++t)
        merged.addSequence (*mf.getTrack (t), 0.0);
    merged.updateMatchedPairs();

    s.ok = true;
    s.tracks = mf.getNumTracks();
    s.length = merged.getEndTime();
    for (auto* e : merged)
        if (e->message.isNoteOn()) ++s.notes;
    return true;
}

bool MidiPlayer::load (const File& f)
{
    stop();
    notes.clear();
    if (! parse (f, seq, summary)) return false;

    lowest = 127; highest = 0;
    for (int i = 0; i < seq.getNumEvents(); ++i)
    {
        const auto& m = seq.getEventPointer (i)->message;
        if (! m.isNoteOn()) continue;
        const int off = seq.getIndexOfMatchingKeyUp (i);
        const double end = off >= 0 ? seq.getEventTime (off) : m.getTimeStamp() + 0.25;
        notes.push_back ({ m.getTimeStamp(), end, m.getNoteNumber(), m.getChannel() });
        lowest = jmin (lowest, m.getNoteNumber());
        highest = jmax (highest, m.getNoteNumber());
    }
    if (lowest > highest) { lowest = 60; highest = 72; }
    pausedPos = 0;
    return true;
}

void MidiPlayer::unload()
{
    stop();
    seq.clear();
    notes.clear();
    summary = {};
}

bool MidiPlayer::ensureOutput()
{
    if (out) return true;
    if (outputFailed) return false;
    auto dev = MidiOutput::getDefaultDevice();
    if (dev.identifier.isEmpty())
    {
        auto all = MidiOutput::getAvailableDevices();
        if (all.isEmpty()) { outputFailed = true; return false; }
        dev = all[0];
    }
    out = MidiOutput::openDevice (dev.identifier);
    if (out == nullptr) { outputFailed = true; return false; }
    deviceName = dev.name;
    out->startBackgroundThread();
    return true;
}

void MidiPlayer::silence()
{
    if (! out) return;
    out->clearAllPendingMessages();
    for (int ch = 1; ch <= 16; ++ch)
    {
        out->sendMessageNow (MidiMessage::allNotesOff (ch));
        out->sendMessageNow (MidiMessage::allSoundOff (ch));
    }
}

bool MidiPlayer::play (double from)
{
    if (! summary.ok) return false;
    if (! ensureOutput()) return false;
    silence();
    from = jlimit (0.0, summary.length, from);

    // nastavenia nástrojov a ovládačov pred začiatkom
    for (auto* e : seq)
    {
        const auto& m = e->message;
        if (m.getTimeStamp() >= from) break;
        if (m.isProgramChange() || m.isController() || m.isPitchWheel())
            out->sendMessageNow (m);
    }

    MidiBuffer buf;
    for (auto* e : seq)
    {
        const auto& m = e->message;
        if (m.getTimeStamp() < from || m.isMetaEvent()) continue;
        buf.addEvent (m, (int) std::llround ((m.getTimeStamp() - from) * 1000.0));
    }
    startMs = Time::getMillisecondCounterHiRes() + 30.0;
    startPos = from;
    out->sendBlockOfMessages (buf, startMs, 1000.0);
    playing = true;
    return true;
}

void MidiPlayer::stop()
{
    if (playing) pausedPos = getPosition();
    playing = false;
    silence();
}

bool MidiPlayer::isPlaying()
{
    if (playing && getPosition() >= summary.length) { playing = false; pausedPos = summary.length; }
    return playing;
}

double MidiPlayer::getPosition()
{
    if (! playing) return pausedPos;
    return jlimit (0.0, summary.length, startPos + (Time::getMillisecondCounterHiRes() - startMs) / 1000.0);
}
}
