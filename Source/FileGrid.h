#pragma once
#include "Common.h"
#include "ThumbCache.h"

namespace kp
{
// Mriežka súborov aktuálneho priečinka s náhľadmi. Kreslí len viditeľné dlaždice.
class FileGrid : public Component
{
public:
    explicit FileGrid (ThumbCache& cache);

    void setFolder (const File& folder, const File& toSelect = {});
    void reload();
    const File& getFolder() const { return folder; }

    File getSelectedFile() const;
    File getFileAt (int index) const;
    int getSelectedIndex() const { return selected; }
    int getNumFiles() const { return (int) entries.size(); }
    void selectIndex (int index);
    void selectFile (const File& f);

    // Po presune: odstráni súbor zo zoznamu, výber ostane na rovnakom mieste
    void removeFile (const File& f);

    std::function<void (const File&)> onSelectionChanged;
    std::function<void (const File&)> onOpenFolder;
    std::function<void()> onGoUp;
    std::function<void (const File&)> onOpenExternally;

    bool keyPressed (const KeyPress& key) override;
    void resized() override;
    void paint (Graphics& g) override;
    void focusGained (FocusChangeType) override { content.repaint(); }
    void focusLost (FocusChangeType) override { content.repaint(); }

private:
    struct Entry { File file; Kind kind; String name; };

    class Content : public Component
    {
    public:
        explicit Content (FileGrid& o) : owner (o) {}
        void paint (Graphics& g) override;
        void mouseDown (const MouseEvent& e) override;
        void mouseDoubleClick (const MouseEvent& e) override;
        FileGrid& owner;
    };

    int columns() const;
    int indexAt (Point<int> p) const;
    Rectangle<int> cellBounds (int index) const;
    void updateSize();
    void ensureVisible (int index);
    void activate (int index);
    void paintCell (Graphics& g, int index, Rectangle<int> r);

    ThumbCache& thumbs;
    File folder;
    std::vector<Entry> entries;
    int selected = -1;

    Viewport viewport;
    Content content { *this };

    static constexpr int cellW = 152, cellH = 172, gap = 6;
};
}
