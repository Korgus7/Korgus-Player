#pragma once
#include "Common.h"

namespace kp
{
// Čo je to za súbor a čím ho otvoriť / upraviť (zabudované, funguje bez internetu)
struct FormatInfo
{
    String name;       // napr. "Výkres AutoCAD"
    String freeApps;   // programy zadarmo
    String paidApps;   // platené / pôvodný program
    String note;       // tip
};

const FormatInfo* formatInfo (const File& f);
}
