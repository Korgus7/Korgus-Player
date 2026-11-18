#pragma once
#include "Common.h"
#include "ThumbCache.h"
#include "AudioInfoCache.h"

namespace kp
{
// Stred okna: súbory aktuálneho priečinka ako mriežka náhľadov alebo zoznam so stĺpcami.
// Kreslí len viditeľné položky.
class FileGrid : public Component
{
public:
    enum class Mode { Grid, List };
    enum class SortBy { Name, Length, Size, Date };

    FileGrid (ThumbCache& thumbs, AudioInfoCache& audio);

    void setFolder (const File& folder, const File& toSelect = {});
    void reload();
    const File& getFolder() const { return folder; }

    File getSelectedFile() const;
    File getFileAt (int index) const;
    Kind getKindAt (int index) const;
    int getSelectedIndex() const { return selected; }
    int getNumFiles() const { return (int) entries.size(); }
    void selectIndex (int index);
    void selectFile (const File& f);

    // Po presune: odstráni súbor zo zoznamu, výber ostane na rovnakom mieste
    void removeFile (const File& f);

    // Označené súbory (Ctrl/Shift). Ak nie je označené nič, vráti aktuálny súbor.
    Array<File> getMarkedFiles() const;
    int getNumMarked() const;
    void clearMarks();
    void markAll();
    std::function<void()> onMarksChanged;

    // vyhľadávanie a filter podľa typu (0 = všetko)
    void setFilter (const String& text, int typeFilter);
    bool isFiltered() const { return filterText.isNotEmpty() || filterType != 0; }
    int getNumAll() const { return (int) allEntries.size(); }

    Mode getMode() const { return mode; }
    void setMode (Mode m);
    void toggleMode() { setMode (mode == Mode::Grid ? Mode::List : Mode::Grid); }

    std::function<void (const File&)> onSelectionChanged;
    std::function<void (const File&)> onOpenFolder;
    std::function<void()> onGoUp;
    std::function<void (const File&)> onActivateFile;   // Enter / dvojklik na súbor
    std::function<void()> onModeChanged;
    std::function<void (Point<int>)> onContextMenu;   // pravé tlačidlo (súradnice obrazovky)

    bool keyPressed (const KeyPress& key) override;
    void resized() override;
    void paint (Graphics& g) override;
    void focusGained (FocusChangeType) override { content.repaint(); }
    void focusLost (FocusChangeType) override { content.repaint(); }

private:
    struct Entry
    {
        File file;
        Kind kind;
        String name;
        int64 size = 0;
        Time modified;
    };

    class Content : public Component
    {
    public:
        explicit Content (FileGrid& o) : owner (o) {}
        void paint (Graphics& g) override;
        void mouseDown (const MouseEvent& e) override;
        void mouseDrag (const MouseEvent& e) override;
        void mouseUp (const MouseEvent& e) override;
        void mouseDoubleClick (const MouseEvent& e) override;
        FileGrid& owner;
        bool dragging = false, clearOnUp = false;
    };

    class Header : public Component
    {
    public:
        explicit Header (FileGrid& o) : owner (o) {}
        void paint (Graphics& g) override;
        void mouseDown (const MouseEvent& e) override;
        FileGrid& owner;
    };

    struct Columns { Rectangle<int> name, wave, length, format, size, date; };
    Columns listColumns (Rectangle<int> row) const;

    int columns() const;
    int itemHeight() const { return mode == Mode::Grid ? cellH + gap : rowH; }
    int indexAt (Point<int> p) const;
    Rectangle<int> cellBounds (int index) const;
    void updateSize();
    void ensureVisible (int index);
    void activate (int index);
    void sortEntries();
    void setSort (SortBy s);
    void paintCell (Graphics& g, int index, Rectangle<int> r);
    void paintRow (Graphics& g, int index, Rectangle<int> r);
    void paintIcon (Graphics& g, const Entry& e, Rectangle<float> area, bool small);

    ThumbCache& thumbs;
    AudioInfoCache& audio;
    File folder;
    std::vector<Entry> entries, allEntries;
    String filterText;
    int filterType = 0;
    void applyFilter();
    bool passes (const Entry& e) const;
    std::vector<char> marked;
    int selected = -1, anchor = -1;
    void markRange (int a, int b);
    void marksChanged();
    Mode mode = Mode::Grid;
    SortBy sortBy = SortBy::Name;
    bool ascending = true;

    Viewport viewport;
    Content content { *this };
    Header header { *this };

    static constexpr int cellW = 152, cellH = 172, gap = 6, rowH = 26, headerH = 26;
};
}
