#pragma once
#include "Common.h"

namespace kp
{
// Aktualizácie z verejného repozitára Korgus-Download (latest-player.json)
namespace updater
{
    struct Info { String version, url, mpvUrl, notes; };

    void cleanupOld();                                  // zmaže "*.old" po predošlej aktualizácii
    bool isNewer (const String& remote, const String& local);
    // na pozadí stiahne latest-player.json; done sa volá na hlavnom vlákne (prázdna verzia = chyba)
    void check (std::function<void (Info)> done);
    // stiahne nové exe, nahradí bežiace a spustí ho; vráti chybu alebo prázdny text
    void install (const Info& info, std::function<void (String error)> done);
    // stiahne libmpv (prehrávanie videa) vedľa programu
    void installVideo (const Info& info, std::function<void (String error)> done);
}
}
