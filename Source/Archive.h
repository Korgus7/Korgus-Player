#pragma once
#include "Common.h"

namespace kp
{
// Práca na pozadí s priebehom (archívy, hľadanie, synchronizácia …)
class TaskJob : private Thread
{
public:
    explicit TaskJob (const String& label, std::function<void (TaskJob&)> work)
        : Thread ("Korgus uloha"), text (label), fn (std::move (work)) {}
    ~TaskJob() override { cancelled = true; stopThread (20000); }
    void start() { startThread(); }
    void cancel() { cancelled = true; }
    bool isDone() const { return finished; }
    bool isCancelled() const { return cancelled; }
    const String& label() const { return text; }

    std::atomic<double> progress { 0.0 };
    std::atomic<bool> cancelled { false };
    String error;
    var result;                 // voľné údaje pre výsledok
    std::vector<String> lines;  // výsledky (hľadanie, porovnanie …)

private:
    void run() override { fn (*this); finished = true; }
    String text;
    std::function<void (TaskJob&)> fn;
    std::atomic<bool> finished { false };
};

// Archívy: ZIP cez JUCE, RAR / 7z / TAR cez tar.exe vo Windows (len čítanie)
struct ArcItem { String path; bool dir = false; int64 size = 0; Time mod; };

namespace winshell
{
    void properties (const File& f);                       // Alt+Enter
    void run (const String& commandLine, const File& dir); // príkazový riadok
}

namespace archive
{
    bool isArchive (const File& f);
    bool isZip (const File& f);
    bool list (const File& arc, std::vector<ArcItem>& out, String& error);
    // vybrané cesty (súbory aj priečinky) z priečinka baseDir v archíve do dest
    bool extract (const File& arc, const StringArray& paths, const String& baseDir, const File& dest, TaskJob* job, String& error);
    bool extractAll (const File& arc, const File& dest, TaskJob* job, String& error);
    bool createZip (const Array<File>& files, const File& baseFolder, const File& zip, TaskJob* job, String& error);
    bool addToZip (const File& zip, const String& arcDir, const Array<File>& files, TaskJob* job, String& error);
    bool deleteFromZip (const File& zip, const StringArray& paths, String& error);
}
}
