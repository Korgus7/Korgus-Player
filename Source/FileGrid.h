#pragma once
#include "Common.h"
#include "ThumbCache.h"
#include "AudioInfoCache.h"
#include "MetaStore.h"

namespace kp
{
// Stred okna: súbory aktuálneho priečinka ako mriežka náhľadov alebo zoznam so stĺpcami.
// Kreslí len viditeľné položky.
class FileGrid : public Component
{
public:
    enum class Mode { Grid, List };
    enum class SortBy { Name, Length, Size, Date, Taken, Type };

    FileGrid (ThumbCache& thumbs, AudioInfoCache& audio);
    ~FileGrid() override { *alive = false; takenPool.removeAllJobs (true, 3000); }
    void setMetaStore (MetaStore* m) { meta = m; }

    void setFolder (const File& folder, const File& toSelect = {});
    void reload();
    // zoznam súborov z viacerých priečinkov (hľadanie, podobné fotky/zvuky); keepOrder = neradiť
    void showList (const File& base, const Array<File>& files, const String& title, bool keepOrder);
    bool isListMode() const { return listMode; }
    const String& getListTitle() const { return listTitle; }
    static String fold (const String& s);   // bez diakritiky a veľkých písmen
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
    void markFiles (const Array<File>& files);
    std::function<void()> onMarksChanged;

    // vyhľadávanie a filter podľa typu (0 = všetko)
    void setFilter (const String& text, int typeFilter);
    bool isFiltered() const { return filterText.isNotEmpty() || filterType != 0; }
    int getNumAll() const { return (int) allEntries.size(); }

    Mode getMode() const { return mode; }
    void setMode (Mode m);
    void toggleMode() { setMode (mode == Mode::Grid ? Mode::List : Mode::Grid); }
    // veľkosť náhľadov v mriežke (šírka bunky) a riadkov v zozname (výška), v pixeloch
    void setThumbWidth (int px);
    void setRowHeight (int px);
    int getThumbWidth() const { return cellW; }
    // písmo názvu pod náhľadom rastie s náhľadom (na veľkých monitoroch)
    float nameFontSize() const { return 11.5f + (float) (cellW - minThumbW) * 0.045f; }
    int nameHeight() const { return roundToInt (nameFontSize() * 2.3f + 2.0f); }
    int getRowHeight() const { return rowH; }
    static constexpr int minThumbW = 104, maxThumbW = 208, minRowH = 20, maxRowH = 40;
    std::function<void()> onSizeChanged;   // Ctrl + koliesko
    // stĺpce zoznamu (bity): vlna, dĺžka, formát, veľkosť, dátum
    enum Column { ColWave = 1, ColLength = 2, ColFormat = 4, ColSize = 8, ColDate = 16 };
    void setColumns (int mask);
    int getColumns() const { return colsShown; }
    // šírky stĺpcov zoznamu (ťahanie v nadpise), -1 = automaticky; poradie: vlna, dĺžka, formát, veľkosť, dátum
    String getColumnWidths() const;
    void setColumnWidths (const String& s);

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
        Time modified, taken;
        bool takenKnown = false;
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
        void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override;
        FileGrid& owner;
        bool dragging = false, clearOnUp = false;
    };

    class Header : public Component
    {
    public:
        explicit Header (FileGrid& o) : owner (o) {}
        void paint (Graphics& g) override;
        void mouseDown (const MouseEvent& e) override;
        void mouseMove (const MouseEvent& e) override;
        void mouseDrag (const MouseEvent& e) override;
        void mouseUp (const MouseEvent& e) override;
        void mouseExit (const MouseEvent& e) override;
        int edgeAt (Point<int> p) const;   // index stĺpca, ktorého ľavý okraj je pod myšou, inak -1
        FileGrid& owner;
        int dragCol = -1, dragStartW = 0, hoverEdge = -1;
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
public:
    void setSortExplicit (SortBy s, bool asc);
    std::function<void()> onSortChanged;
private:
    void loadTakenDates();
    ThreadPool takenPool { ThreadPoolOptions{}.withThreadName ("Datumy").withNumberOfThreads (1) };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    int takenGeneration = 0;
    void paintCell (Graphics& g, int index, Rectangle<int> r);
    void paintRow (Graphics& g, int index, Rectangle<int> r);
    void paintIcon (Graphics& g, const Entry& e, Rectangle<float> area, bool small);

    ThumbCache& thumbs;
    AudioInfoCache& audio;
    File folder;
    bool listMode = false, listKeepOrder = false;
    Array<File> fixedList;
    String listTitle;
    void finishEntries (bool sameFolder, const File& toSelect);
    std::vector<Entry> entries, allEntries;
    MetaStore* meta = nullptr;
    void paintMeta (Graphics& g, const File& f, Rectangle<int> r, bool compact);
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

    int cellW = 152, cellH = 172, rowH = 26;
    static constexpr int gap = 6, headerH = 26;
    int colsShown = ColWave | ColLength | ColFormat | ColSize | ColDate;
    int colW[5] = { -1, -1, -1, -1, -1 };
};
}
