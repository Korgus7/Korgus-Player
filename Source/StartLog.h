#pragma once
// Meranie štartu: %APPDATA%\Korgus Player\start-log.txt – len posledné spustenie (súbor sa vždy prepíše).
// Zapisuje sa len prvých 15 s po spustení procesu.
#include "Common.h"
#include "NetWin.h"
#include <mutex>

namespace kp::startlog
{
    inline void mark (const String& step)
    {
        static std::mutex m;
        static double t0 = -1;
        static File f;
        std::lock_guard<std::mutex> g (m);
        const double now = Time::getMillisecondCounterHiRes();
        if (t0 < 0)
        {
            t0 = now - netwin::processAgeMs();   // od spustenia procesu (aj načítanie knižníc)
            f = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Korgus Player").getChildFile ("start-log.txt");
            f.getParentDirectory().createDirectory();
            f.replaceWithText ("Korgus Player " + String (KP_VERSION) + "  " + Time::getCurrentTime().toString (true, true, true, true) + "\n");
        }
        if (now - t0 > 15000.0) return;
        f.appendText (String (roundToInt (now - t0)).paddedLeft (' ', 6) + " ms  " + step + "\n");
    }
}
