#pragma once
#include "Common.h"

namespace kp
{
// Prezentácia fotiek ako video MP4 (1920×1080, 30 fps, H.264 + AAC cez Windows Media Foundation):
// pomalé približovanie (Ken Burns), prelínanie, voliteľne hudba so stlmením na konci.
class VideoExport : private Thread
{
public:
    VideoExport (Array<File> images, File music, File output, double secondsPerImage);
    ~VideoExport() override;
    void start() { startThread(); }
    void cancel() { cancelled = true; }
    bool isDone() const { return finished; }
    double progress() const { return prog; }
    String error;
    File output;
    bool wasCancelled() const { return cancelled; }

    // jeden snímok (spoločné pre Windows aj test): t = čas v sekundách
    static void renderFrame (Graphics& g, int w, int h, const Image& a, const Image& b, float mix, float zoomA, float zoomB, int indexA, int indexB);

private:
    void run() override;
    Array<File> images;
    File music;
    double perImage;
    std::atomic<bool> cancelled { false }, finished { false };
    std::atomic<double> prog { 0.0 };
};
}
