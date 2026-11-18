#pragma once
#include "Common.h"

namespace kp::win
{
    struct PrintResult { int sent = 0; bool cancelled = false; String error; };
    // Obrázky: vlastná tlač (výber tlačiarne, každý na celú stranu, otočí sa podľa strany papiera).
    // Ostatné súbory (PDF, text…): tlač cez program, ktorý ich vo Windows otvára.
    PrintResult printFiles (const Array<File>& files, void* ownerWindow);
}
