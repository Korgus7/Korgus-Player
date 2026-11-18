#pragma once
#include "Common.h"

namespace kp
{
// Kopírovanie / presun (medzi diskami) na pozadí s priebehom a možnosťou zrušiť.
class Transfer : private Thread
{
public:
    struct Item { File from, to; };
    Transfer (std::vector<Item> items, bool move);
    ~Transfer() override;

    void start() { startThread(); }
    void cancel() { cancelled = true; }
    bool isDone() const { return finished; }
    bool isMove() const { return move; }

    // priebeh (číta hlavné vlákno)
    double progress() const { return total > 0 ? (double) done.load() / (double) total.load() : 0.0; }
    int currentIndex() const { return current; }
    int count() const { return (int) items.size(); }

    // výsledok (po isDone)
    std::vector<Item> completed;   // podarilo sa (pri presune je zdroj už zmazaný)
    String error;
    bool wasCancelled() const { return cancelled; }

    static File rootOf (File f);

private:
    void run() override;
    bool copyFile (const File& from, const File& to);
    bool copyTree (const File& from, const File& to);

    std::vector<Item> items;
    const bool move;
    std::atomic<int64> total { 0 }, done { 0 };
    std::atomic<int> current { 0 };
    std::atomic<bool> cancelled { false }, finished { false };
};
}
