#include "Updater.h"

namespace kp::updater
{
static const char* manifestUrl = "https://raw.githubusercontent.com/Korgus7/Korgus-Download/main/latest-player.json";

static File exeFile() { return File::getSpecialLocation (File::currentExecutableFile); }

void cleanupOld()
{
    for (auto& f : exeFile().getParentDirectory().findChildFiles (File::findFiles, false, "*.old"))
        f.deleteFile();
}

bool isNewer (const String& remote, const String& local)
{
    auto a = StringArray::fromTokens (remote.trim(), ".", ""), b = StringArray::fromTokens (local.trim(), ".", "");
    for (int i = 0; i < jmax (a.size(), b.size()); ++i)
    {
        const int x = a[i].getIntValue(), y = b[i].getIntValue();
        if (x != y) return x > y;
    }
    return false;
}

static bool download (const String& url, MemoryBlock& out)
{
    auto stream = URL (url).createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress)
                                                   .withConnectionTimeoutMs (15000).withNumRedirectsToFollow (5));
    if (stream == nullptr) return false;
    out.reset();
    MemoryOutputStream mos (out, false);
    mos.writeFromInputStream (*stream, -1);
    mos.flush();
    return out.getSize() > 0;
}

void check (std::function<void (Info)> done)
{
    Thread::launch ([done]
    {
        Info info;
        MemoryBlock mb;
        if (download (String (manifestUrl) + "?t=" + String (Time::currentTimeMillis()), mb))
        {
            const auto json = JSON::parse (mb.toString());
            info.version = json["version"].toString();
            info.url = json["url"].toString();
            info.mpvUrl = json["libmpv"].toString();
            info.notes = json["notes"].toString();
        }
        MessageManager::callAsync ([done, info] { done (info); });
    });
}

void install (const Info& info, std::function<void (String)> done)
{
    Thread::launch ([info, done]
    {
        String err;
        MemoryBlock mb;
        const auto exe = exeFile();
        const auto fresh = exe.getSiblingFile (exe.getFileNameWithoutExtension() + ".new");
        const auto old = exe.getSiblingFile (exe.getFileNameWithoutExtension() + ".old");
        if (! download (info.url, mb) || mb.getSize() < 1000000 || static_cast<const char*> (mb.getData())[0] != 'M')
            err = tr ("Novú verziu sa nepodarilo stiahnuť.");
        else if (! fresh.replaceWithData (mb.getData(), mb.getSize()))
            err = tr ("Do priečinka programu sa nedá zapisovať: ") + exe.getParentDirectory().getFullPathName();
        else
        {
            old.deleteFile();
            // bežiace exe sa vo Windows nedá prepísať, ale dá sa premenovať
            if (! exe.moveFileTo (old)) { fresh.deleteFile(); err = tr ("Program sa nedá nahradiť."); }
            else if (! fresh.moveFileTo (exe)) { old.moveFileTo (exe); err = tr ("Program sa nedá nahradiť."); }
        }
        MessageManager::callAsync ([done, err]
        {
            if (err.isEmpty())
            {
                exeFile().startAsProcess();
                done ({});
                JUCEApplication::getInstance()->systemRequestedQuit();
            }
            else done (err);
        });
    });
}

void installVideo (const Info& info, std::function<void (String)> done)
{
    Thread::launch ([info, done]
    {
        String err;
        MemoryBlock mb;
        if (info.mpvUrl.isEmpty() || ! download (info.mpvUrl, mb))
            err = tr ("Prehrávač videa sa nepodarilo stiahnuť.");
        else
        {
            MemoryInputStream mis (mb, false);
            ZipFile zip (mis);
            const auto dir = exeFile().getParentDirectory();
            if (zip.getNumEntries() == 0 || zip.uncompressTo (dir, true).failed())
                err = tr ("Do priečinka programu sa nedá zapisovať: ") + dir.getFullPathName();
        }
        MessageManager::callAsync ([done, err] { done (err); });
    });
}
}
