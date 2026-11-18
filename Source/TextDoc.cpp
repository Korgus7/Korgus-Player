#include "DocViewers.h"
#include "WinImaging.h"
#include "ImageLoader.h"

namespace kp
{
namespace
{
    constexpr int64 maxTextBytes = 4 * 1024 * 1024;
    constexpr int maxRows = 3000, maxCols = 120;

    // ---------------------------------------------------------------- dekódovanie textu
    String decodeBytes (const MemoryBlock& mb)
    {
        auto* d = static_cast<const uint8*> (mb.getData());
        const size_t n = mb.getSize();
        if (n >= 2 && ((d[0] == 0xff && d[1] == 0xfe) || (d[0] == 0xfe && d[1] == 0xff)))
            return String::createStringFromData (d, (int) n);                         // UTF-16 s BOM
        if (n >= 3 && d[0] == 0xef && d[1] == 0xbb && d[2] == 0xbf)
            return String (CharPointer_UTF8 ((const char*) d + 3), n - 3);
        if (CharPointer_UTF8::isValidString ((const char*) d, (int) n))
            return String (CharPointer_UTF8 ((const char*) d), n);
        return win::decodeAnsi ((const char*) d, n);                                  // napr. Windows-1250
    }

    String readText (const File& f)
    {
        FileInputStream in (f);
        if (! in.openedOk()) return {};
        MemoryBlock mb;
        in.readIntoMemoryBlock (mb, maxTextBytes);
        return decodeBytes (mb);
    }

    // ---------------------------------------------------------------- RTF → text
    String rtfToText (const String& rtf)
    {
        String out;
        auto p = rtf.getCharPointer();
        int depth = 0, skipDepth = -1, ucSkip = 1;
        MemoryBlock pending;   // \'hh bajty sa dekódujú spolu
        auto flush = [&]
        {
            if (pending.getSize() > 0) { out << win::decodeAnsi ((const char*) pending.getData(), pending.getSize()); pending.reset(); }
        };
        while (! p.isEmpty())
        {
            const auto c = p.getAndAdvance();
            if (c == '{') { ++depth; continue; }
            if (c == '}') { if (depth == skipDepth) skipDepth = -1; --depth; continue; }
            const bool skipping = skipDepth >= 0;
            if (c == '\\')
            {
                if (p.isEmpty()) break;
                const auto n = *p;
                if (n == '\'')
                {
                    ++p;
                    String hex;
                    for (int i = 0; i < 2 && ! p.isEmpty(); ++i) hex << String::charToString (p.getAndAdvance());
                    if (! skipping) { const uint8 b = (uint8) hex.getHexValue32(); pending.append (&b, 1); }
                    continue;
                }
                flush();
                if (n == '\\' || n == '{' || n == '}') { ++p; if (! skipping) out << String::charToString (n); continue; }
                if (n == '*') { ++p; skipDepth = depth; continue; }
                if (! CharacterFunctions::isLetter (n)) { ++p; if (n == '~' && ! skipping) out << " "; continue; }
                String word;
                while (! p.isEmpty() && CharacterFunctions::isLetter (*p)) word << String::charToString (p.getAndAdvance());
                String num;
                if (! p.isEmpty() && (*p == '-' || CharacterFunctions::isDigit (*p)))
                    while (! p.isEmpty() && (*p == '-' || CharacterFunctions::isDigit (*p))) num << String::charToString (p.getAndAdvance());
                if (! p.isEmpty() && *p == ' ') ++p;
                if (word == "fonttbl" || word == "colortbl" || word == "stylesheet" || word == "info" || word == "pict"
                    || word == "header" || word == "footer" || word == "listtable" || word == "listoverridetable")
                    skipDepth = depth;
                if (skipping) continue;
                if (word == "par" || word == "line" || word == "row") out << "\n";
                else if (word == "tab" || word == "cell") out << "\t";
                else if (word == "uc") ucSkip = num.getIntValue();
                else if (word == "u")
                {
                    int v = num.getIntValue();
                    if (v < 0) v += 65536;
                    out << String::charToString ((juce_wchar) v);
                    for (int i = 0; i < ucSkip && ! p.isEmpty(); ++i)
                    {
                        if (*p == '\\') { ++p; if (! p.isEmpty() && *p == '\'') { ++p; ++p; if (! p.isEmpty()) ++p; } else ++p; }
                        else ++p;
                    }
                }
                continue;
            }
            flush();
            if (skipping || c == '\r' || c == '\n') continue;
            out << String::charToString (c);
        }
        flush();
        return out;
    }

    // ---------------------------------------------------------------- zip pomocníci
    std::unique_ptr<XmlElement> zipXml (ZipFile& zip, const String& name)
    {
        const int i = zip.getIndexOfFileName (name, true);
        if (i < 0) return nullptr;
        std::unique_ptr<InputStream> in (zip.createStreamForEntry (i));
        if (in == nullptr) return nullptr;
        return parseXML (in->readEntireStreamAsString());
    }

    void collectText (const XmlElement& el, String& out, bool odf)
    {
        const auto tag = el.getTagNameWithoutNamespace();
        if (el.isTextElement()) { out << el.getText(); return; }
        if (! odf)
        {
            if (tag == "t") { out << el.getAllSubText(); return; }
            if (tag == "tab") { out << "\t"; return; }
            if (tag == "br" || tag == "cr") { out << "\n"; return; }
        }
        else
        {
            if (tag == "s") { out << String::repeatedString (" ", el.getIntAttribute ("text:c", 1)); return; }
            if (tag == "tab") { out << "\t"; return; }
            if (tag == "line-break") { out << "\n"; return; }
        }
        for (auto* c : el.getChildIterator()) collectText (*c, out, odf);
        if (tag == "p" || tag == "h") out << "\n";
    }

    String docxText (const File& f)
    {
        ZipFile zip (f);
        if (auto xml = zipXml (zip, "word/document.xml")) { String s; collectText (*xml, s, false); return s; }
        return {};
    }

    String odtText (const File& f)
    {
        ZipFile zip (f);
        if (auto xml = zipXml (zip, "content.xml"))
        {
            String s;
            // len telo dokumentu
            std::function<XmlElement* (XmlElement&)> findBody = [&] (XmlElement& e) -> XmlElement*
            {
                if (e.getTagNameWithoutNamespace() == "text" && e.getNamespace() == "office") return &e;
                for (auto* c : e.getChildIterator()) if (auto* r = findBody (*c)) return r;
                return nullptr;
            };
            if (auto* body = findBody (*xml)) collectText (*body, s, true);
            else collectText (*xml, s, true);
            return s;
        }
        return {};
    }

    String loadAnyText (const File& f)
    {
        const auto e = f.getFileExtension().toLowerCase();
        if (e == ".docx") return docxText (f);
        if (e == ".odt") return odtText (f);
        if (e == ".rtf") return rtfToText (readText (f));
        return readText (f);
    }

    bool isProse (const File& f)
    {
        const auto e = f.getFileExtension().toLowerCase();
        return e == ".txt" || e == ".md" || e == ".markdown" || e == ".rtf" || e == ".docx" || e == ".odt" || e == ".nfo" || e == ".tex" || e == ".srt";
    }

    // ---------------------------------------------------------------- tabuľky
    using Table = std::vector<StringArray>;

    Table parseCsv (const String& text, const File& f)
    {
        Table t;
        juce_wchar delim = f.hasFileExtension ("tsv") ? '\t' : ',';
        if (! f.hasFileExtension ("tsv"))
        {
            const auto first = text.upToFirstOccurrenceOf ("\n", false, false);
            int best = 0;
            for (juce_wchar d : { (juce_wchar) ',', (juce_wchar) ';', (juce_wchar) '\t', (juce_wchar) '|' })
            {
                int n = 0;
                for (auto c : first) if (c == d) ++n;
                if (n > best) { best = n; delim = d; }
            }
        }
        StringArray row;
        String cell;
        bool quoted = false;
        auto p = text.getCharPointer();
        while (! p.isEmpty() && (int) t.size() < maxRows)
        {
            const auto c = p.getAndAdvance();
            if (quoted)
            {
                if (c == '"') { if (*p == '"') { cell << "\""; ++p; } else quoted = false; }
                else cell << String::charToString (c);
            }
            else if (c == '"') quoted = true;
            else if (c == delim) { row.add (cell); cell.clear(); }
            else if (c == '\n') { row.add (cell); cell.clear(); t.push_back (row); row.clear(); }
            else if (c != '\r') cell << String::charToString (c);
        }
        if (cell.isNotEmpty() || ! row.isEmpty()) { row.add (cell); t.push_back (row); }
        return t;
    }

    int colIndex (const String& ref)
    {
        int c = 0;
        for (auto ch : ref)
        {
            if (ch >= 'A' && ch <= 'Z') c = c * 26 + (int) (ch - 'A' + 1);
            else break;
        }
        return c - 1;
    }

    Table xlsxTable (const File& f)
    {
        Table t;
        ZipFile zip (f);
        StringArray shared;
        if (auto ss = zipXml (zip, "xl/sharedStrings.xml"))
            for (auto* si : ss->getChildIterator())
            {
                String s;
                std::function<void (XmlElement&)> walk = [&] (XmlElement& e)
                {
                    if (e.getTagNameWithoutNamespace() == "t") { s << e.getAllSubText(); return; }
                    if (e.getTagNameWithoutNamespace() == "rPh") return;   // fonetika
                    for (auto* c : e.getChildIterator()) walk (*c);
                };
                walk (*si);
                shared.add (s);
            }
        auto sheet = zipXml (zip, "xl/worksheets/sheet1.xml");
        if (sheet == nullptr)
            for (int i = 0; i < zip.getNumEntries() && sheet == nullptr; ++i)
                if (zip.getEntry (i)->filename.startsWith ("xl/worksheets/sheet")) sheet = zipXml (zip, zip.getEntry (i)->filename);
        if (sheet == nullptr) return t;
        for (auto* part : sheet->getChildIterator())
        {
            if (part->getTagNameWithoutNamespace() != "sheetData") continue;
            for (auto* row : part->getChildIterator())
            {
                if ((int) t.size() >= maxRows) break;
                const int r = row->getIntAttribute ("r", (int) t.size() + 1) - 1;
                if (r < 0 || r >= maxRows) continue;
                while ((int) t.size() <= r) t.emplace_back();
                auto& out = t[(size_t) r];
                int nextCol = 0;
                for (auto* c : row->getChildIterator())
                {
                    int col = c->hasAttribute ("r") ? colIndex (c->getStringAttribute ("r")) : nextCol;
                    nextCol = col + 1;
                    if (col < 0 || col >= maxCols) continue;
                    String v;
                    const auto type = c->getStringAttribute ("t");
                    for (auto* ch : c->getChildIterator())
                    {
                        const auto tag = ch->getTagNameWithoutNamespace();
                        if (tag == "v") v = ch->getAllSubText();
                        else if (tag == "is") v = ch->getAllSubText();
                    }
                    if (type == "s") v = shared[v.getIntValue()];
                    else if (type == "b") v = v == "1" ? "TRUE" : "FALSE";
                    while (out.size() <= col) out.add ({});
                    out.set (col, v);
                }
            }
        }
        return t;
    }

    Table odsTable (const File& f)
    {
        Table t;
        ZipFile zip (f);
        auto xml = zipXml (zip, "content.xml");
        if (xml == nullptr) return t;
        XmlElement* table = nullptr;
        std::function<void (XmlElement&)> find = [&] (XmlElement& e)
        {
            if (table) return;
            if (e.getTagName() == "table:table") { table = &e; return; }
            for (auto* c : e.getChildIterator()) find (*c);
        };
        find (*xml);
        if (table == nullptr) return t;
        std::function<void (XmlElement&)> rows = [&] (XmlElement& e)
        {
            for (auto* r : e.getChildIterator())
            {
                if ((int) t.size() >= maxRows) return;
                if (r->getTagName() == "table:table-row-group" || r->getTagName() == "table:table-header-rows" || r->getTagName() == "table:table-rows")
                {
                    rows (*r);
                    continue;
                }
                if (r->getTagName() != "table:table-row") continue;
                StringArray row;
                for (auto* c : r->getChildIterator())
                {
                    const int rep = jlimit (1, maxCols, c->getIntAttribute ("table:number-columns-repeated", 1));
                    String v;
                    for (auto* p : c->getChildIterator())
                        if (p->getTagName() == "text:p") { if (v.isNotEmpty()) v << " "; v << p->getAllSubText(); }
                    for (int k = 0; k < rep && row.size() < maxCols; ++k) row.add (v);
                }
                while (! row.isEmpty() && row[row.size() - 1].isEmpty()) row.remove (row.size() - 1);
                const int rep = jlimit (1, 50, r->getIntAttribute ("table:number-rows-repeated", 1));
                for (int k = 0; k < rep && (int) t.size() < maxRows; ++k) t.push_back (row);
            }
        };
        rows (*table);
        while (! t.empty() && t.back().isEmpty()) t.pop_back();
        return t;
    }

    Table loadTable (const File& f)
    {
        const auto e = f.getFileExtension().toLowerCase();
        if (e == ".xlsx" || e == ".xlsm") return xlsxTable (f);
        if (e == ".ods") return odsTable (f);
        return parseCsv (readText (f), f);
    }

    // ---------------------------------------------------------------- vložený náhľad dokumentu (Office, ODF, iWork)
    Image embeddedDocThumb (const File& f)
    {
        ZipFile zip (f);
        if (zip.getNumEntries() == 0) return {};
        const ZipFile::ZipEntry* best = nullptr;
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            auto* e = zip.getEntry (i);
            const auto n = e->filename.toLowerCase();
            const bool img = n.endsWith (".png") || n.endsWith (".jpg") || n.endsWith (".jpeg");
            if (! img) continue;
            const bool candidate = n.startsWith ("docprops/thumbnail") || n.startsWith ("thumbnails/thumbnail")
                                || n.contains ("quicklook/thumbnail") || n == "preview.jpg" || n.startsWith ("preview")
                                || n.contains ("/preview") || n.contains ("thumbnail");
            if (candidate && (best == nullptr || e->uncompressedSize > best->uncompressedSize)) best = e;
        }
        if (best == nullptr) return {};
        std::unique_ptr<InputStream> in (zip.createStreamForEntry (*best));
        if (in == nullptr) return {};
        auto im = ImageFileFormat::loadFrom (*in);
        return im.isValid() ? SoftwareImageType().convert (im) : im;
    }

    int slideCount (const File& f)
    {
        ZipFile zip (f);
        int n = 0;
        if (f.hasFileExtension ("pptx"))
        {
            for (int i = 0; i < zip.getNumEntries(); ++i)
            {
                const auto& name = zip.getEntry (i)->filename;
                if (name.startsWith ("ppt/slides/slide") && name.endsWith (".xml")) ++n;
            }
        }
        else if (f.hasFileExtension ("odp"))
        {
            if (auto xml = zipXml (zip, "content.xml"))
            {
                std::function<void (XmlElement&)> walk = [&] (XmlElement& e)
                {
                    if (e.getTagName() == "draw:page") { ++n; return; }
                    for (auto* c : e.getChildIterator()) walk (*c);
                };
                walk (*xml);
            }
        }
        return n;
    }

    Font mono (float h) { return Font (FontOptions (Font::getDefaultMonospacedFontName(), h, Font::plain)); }

    // ---------------------------------------------------------------- prehliadač textu
    class TextView : public DocViewer
    {
    public:
        ~TextView() override { *alive = false; loader.removeAllJobs (true, 3000); }

        void load (const File& f) override
        {
            file = f;
            raw.clear();
            lines.clear();
            scroll = 0;
            loaded = false;
            const int gen = ++generation;
            auto aliveFlag = alive;
            loader.removeAllJobs (false, 0);
            loader.addJob ([this, aliveFlag, gen, f]
            {
                auto text = loadAnyText (f);
                MessageManager::callAsync ([this, aliveFlag, gen, text]
                {
                    if (! *aliveFlag || gen != generation) return;
                    raw = StringArray::fromLines (text);
                    if (raw.size() > 50000) raw.removeRange (50000, raw.size());
                    loaded = true;
                    relayout();
                    repaint();
                    infoChanged();
                });
            });
            repaint();
            infoChanged();
        }

        String infoText() const override
        {
            if (! loaded) return String::fromUTF8 ("Načítavam… · ") + sizeText (file.getSize());
            int chars = 0;
            for (auto& l : raw) chars += l.length();
            return String (raw.size()) + String::fromUTF8 (" riadkov · ") + String (chars) + String::fromUTF8 (" znakov · ") + sizeText (file.getSize());
        }

        String hintText() const override { return String::fromUTF8 ("↑ ↓ PageUp PageDown posun · ← → ďalší súbor · Esc koniec"); }

        void resized() override { relayout(); }

        void paint (Graphics& g) override
        {
            g.fillAll (Colour (0xff14191f));
            const auto font = mono (fontH);
            g.setFont (font);
            const int lh = lineH();
            const int gutter = wrap ? 0 : gutterW();
            if (! wrap)
            {
                g.setColour (col::panel);
                g.fillRect (0, 0, gutter, getHeight());
            }
            const int first = scroll, visible = getHeight() / lh + 1;
            for (int i = first; i < jmin ((int) lines.size(), first + visible); ++i)
            {
                const int y = (i - first) * lh + 4;
                const auto& ln = lines[(size_t) i];
                if (! wrap)
                {
                    g.setColour (col::textDim.withAlpha (0.7f));
                    g.drawText (String (ln.source + 1), 0, y, gutter - 6, lh, Justification::centredRight, false);
                }
                g.setColour (col::text);
                g.drawText (ln.text, gutter + 8, y, getWidth() - gutter - 12, lh, Justification::centredLeft, false);
            }
        }

        void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
        {
            scrollBy (-(int) std::round (w.deltaY * 12.0f));
        }

        bool handleKey (const KeyPress& key) override
        {
            const int k = key.getKeyCode(), page = jmax (1, getHeight() / lineH() - 2);
            if (k == KeyPress::upKey) { scrollBy (-3); return true; }
            if (k == KeyPress::downKey) { scrollBy (3); return true; }
            if (k == KeyPress::pageUpKey) { scrollBy (-page); return true; }
            if (k == KeyPress::pageDownKey || k == KeyPress::spaceKey) { scrollBy (page); return true; }
            if (k == KeyPress::homeKey) { scroll = 0; repaint(); return true; }
            if (k == KeyPress::endKey) { scrollBy (1 << 20); return true; }
            return false;
        }

    private:
        struct Line { String text; int source; };

        int lineH() const { return (int) std::ceil (fontH * 1.35f); }
        int gutterW() const { return 12 + 8 * String (raw.size()).length(); }

        void scrollBy (int d)
        {
            const int maxScroll = jmax (0, (int) lines.size() - getHeight() / lineH() + 1);
            scroll = jlimit (0, maxScroll, scroll + d);
            repaint();
        }

        void relayout()
        {
            lines.clear();
            wrap = isProse (file);
            const float cw = GlyphArrangement::getStringWidth (mono (fontH), "M");
            const int cols = jmax (20, (int) ((float) (getWidth() - (wrap ? 20 : gutterW() + 20)) / jmax (1.0f, cw)));
            for (int i = 0; i < raw.size(); ++i)
            {
                auto s = raw[i].replace ("\t", "    ");
                if (! wrap || s.length() <= cols) { lines.push_back ({ s, i }); continue; }
                while (s.length() > cols)
                {
                    int cut = s.substring (0, cols).lastIndexOfChar (' ');
                    if (cut < cols / 2) cut = cols;
                    lines.push_back ({ s.substring (0, cut), i });
                    s = s.substring (cut).trimStart();
                }
                lines.push_back ({ s, i });
            }
            scroll = jmin (scroll, jmax (0, (int) lines.size() - 1));
        }

        File file;
        StringArray raw;
        std::vector<Line> lines;
        int scroll = 0, generation = 0;
        bool loaded = false, wrap = false;
        float fontH = 13.5f;
        ThreadPool loader { ThreadPoolOptions{}.withThreadName ("Text").withNumberOfThreads (1) };
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };

    // ---------------------------------------------------------------- prehliadač tabuliek
    class TableView : public DocViewer
    {
    public:
        ~TableView() override { *alive = false; loader.removeAllJobs (true, 3000); }

        void load (const File& f) override
        {
            file = f;
            table.clear();
            widths.clear();
            sx = sy = 0;
            loaded = false;
            const int gen = ++generation;
            auto aliveFlag = alive;
            loader.removeAllJobs (false, 0);
            loader.addJob ([this, aliveFlag, gen, f]
            {
                auto t = std::make_shared<Table> (loadTable (f));
                MessageManager::callAsync ([this, aliveFlag, gen, t]
                {
                    if (! *aliveFlag || gen != generation) return;
                    table = std::move (*t);
                    loaded = true;
                    measure();
                    repaint();
                    infoChanged();
                });
            });
            repaint();
            infoChanged();
        }

        String infoText() const override
        {
            if (! loaded) return String::fromUTF8 ("Načítavam… · ") + sizeText (file.getSize());
            int cols = 0;
            for (auto& r : table) cols = jmax (cols, r.size());
            return String ((int) table.size()) + String::fromUTF8 (" riadkov · ") + String (cols) + String::fromUTF8 (" stĺpcov")
                 + ((int) table.size() >= maxRows ? String::fromUTF8 (" (zobrazené prvé)") : String())
                 + String::fromUTF8 (" · ") + sizeText (file.getSize());
        }

        String hintText() const override { return String::fromUTF8 ("koliesko posun · Shift+koliesko do strán · ← → ďalší súbor · Esc koniec"); }

        void paint (Graphics& g) override
        {
            g.fillAll (Colour (0xff14191f));
            if (table.empty())
            {
                g.setColour (col::textDim);
                g.drawText (loaded ? String::fromUTF8 ("Prázdna tabuľka") : String::fromUTF8 ("Načítavam…"), getLocalBounds(), Justification::centred);
                return;
            }
            const int rh = 22, numW = 46;
            g.setFont (FontOptions (12.5f));
            int y = 0;
            for (int r = sy; r < (int) table.size() && y < getHeight(); ++r, y += rh)
            {
                const bool head = r == 0;
                g.setColour (head ? col::panelHi : (r % 2 ? Colour (0xff181e25) : Colour (0xff14191f)));
                g.fillRect (0, y, getWidth(), rh);
                g.setColour (col::textDim);
                g.drawText (String (r + 1), 0, y, numW - 6, rh, Justification::centredRight, false);
                int x = numW - xOffset();
                const auto& row = table[(size_t) r];
                for (int c = 0; c < (int) widths.size(); ++c)
                {
                    const int w = widths[(size_t) c];
                    if (x + w > numW && x < getWidth())
                    {
                        g.setColour (col::line);
                        g.fillRect (x + w - 1, y, 1, rh);
                        if (c < row.size())
                        {
                            g.setColour (head ? Colours::white : col::text);
                            Graphics::ScopedSaveState ss (g);
                            g.reduceClipRegion (numW, y, getWidth() - numW, rh);
                            g.drawText (row[c], x + 5, y, w - 10, rh, Justification::centredLeft, true);
                        }
                    }
                    x += w;
                }
            }
            g.setColour (col::line);
            g.fillRect (numW - 1, 0, 1, getHeight());
        }

        void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
        {
            if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
                sx = jmax (0, sx - (int) std::round ((w.deltaX != 0 ? w.deltaX : w.deltaY) * 600.0f));
            else
                sy = jlimit (0, jmax (0, (int) table.size() - 3), sy - (int) std::round (w.deltaY * 12.0f));
            repaint();
        }

        bool handleKey (const KeyPress& key) override
        {
            const int k = key.getKeyCode(), page = jmax (1, getHeight() / 22 - 2);
            const int maxY = jmax (0, (int) table.size() - 3);
            if (k == KeyPress::upKey) { sy = jmax (0, sy - 1); repaint(); return true; }
            if (k == KeyPress::downKey) { sy = jmin (maxY, sy + 1); repaint(); return true; }
            if (k == KeyPress::pageUpKey) { sy = jmax (0, sy - page); repaint(); return true; }
            if (k == KeyPress::pageDownKey) { sy = jmin (maxY, sy + page); repaint(); return true; }
            return false;
        }

    private:
        int xOffset() const { return sx; }

        void measure()
        {
            widths.clear();
            const Font f (FontOptions (12.5f));
            for (size_t r = 0; r < jmin<size_t> (table.size(), 400); ++r)
                for (int c = 0; c < table[r].size(); ++c)
                {
                    if ((int) widths.size() <= c) widths.push_back (50);
                    widths[(size_t) c] = jlimit (50, 360, jmax (widths[(size_t) c], (int) GlyphArrangement::getStringWidth (f, table[r][c]) + 16));
                }
        }

        File file;
        Table table;
        std::vector<int> widths;
        int sx = 0, sy = 0, generation = 0;
        bool loaded = false;
        ThreadPool loader { ThreadPoolOptions{}.withThreadName ("Tabulka").withNumberOfThreads (1) };
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };

    // ---------------------------------------------------------------- prezentácie a iWork: vložený náhľad
    class SlidesView : public DocViewer
    {
    public:
        ~SlidesView() override { *alive = false; loader.removeAllJobs (true, 3000); }

        void load (const File& f) override
        {
            file = f;
            image = {};
            slides = 0;
            loaded = false;
            const int gen = ++generation;
            auto aliveFlag = alive;
            loader.removeAllJobs (false, 0);
            loader.addJob ([this, aliveFlag, gen, f]
            {
                auto im = embeddedDocThumb (f);
                const int n = slideCount (f);
                MessageManager::callAsync ([this, aliveFlag, gen, im, n]
                {
                    if (! *aliveFlag || gen != generation) return;
                    image = im;
                    slides = n;
                    loaded = true;
                    repaint();
                    infoChanged();
                });
            });
            repaint();
            infoChanged();
        }

        String infoText() const override
        {
            String s;
            if (slides > 0) s << slides << String::fromUTF8 (" snímok · ");
            if (loaded && ! image.isValid()) s << String::fromUTF8 ("bez uloženého náhľadu · ");
            return s + sizeText (file.getSize());
        }

        void paint (Graphics& g) override
        {
            g.fillAll (col::bg);
            auto area = getLocalBounds().reduced (8).toFloat();
            if (image.isValid())
            {
                g.setImageResamplingQuality (Graphics::highResamplingQuality);
                g.drawImage (image, RectanglePlacement (RectanglePlacement::centred).appliedTo (image.getBounds().toFloat(), area));
                return;
            }
            g.setColour (col::textDim);
            g.drawText (loaded ? String::fromUTF8 ("Súbor nemá uložený náhľad – Enter ho otvorí v predvolenom programe")
                               : String::fromUTF8 ("Načítavam…"), getLocalBounds(), Justification::centred);
        }

    private:
        File file;
        Image image;
        int slides = 0, generation = 0;
        bool loaded = false;
        ThreadPool loader { ThreadPoolOptions{}.withThreadName ("Nahlad dokumentu").withNumberOfThreads (1) };
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };

    Image pageThumb (const StringArray& lines, int side, bool table)
    {
        const int w = side * 3 / 4, h = side;
        Image im (Image::RGB, w, h, true, SoftwareImageType());
        Graphics g (im);
        g.fillAll (Colour (0xfff7f7f5));
        g.setColour (Colour (0xff2a2f36));
        const float fh = (float) side / 22.0f;
        g.setFont (mono (fh));
        int y = (int) (fh * 0.8f);
        for (auto& l : lines)
        {
            if (y > h - (int) fh) break;
            g.drawText (l.replace ("\t", "  "), (int) (fh * 0.6f), y, w - (int) fh, (int) (fh * 1.3f), Justification::centredLeft, false);
            if (table)
            {
                g.setColour (Colour (0xffc8ccd2));
                g.fillRect (0, y + (int) (fh * 1.3f), w, 1);
                g.setColour (Colour (0xff2a2f36));
            }
            y += (int) (fh * 1.3f);
        }
        return im;
    }
}

std::unique_ptr<DocViewer> createTextViewer() { return std::make_unique<TextView>(); }
std::unique_ptr<DocViewer> createTableViewer() { return std::make_unique<TableView>(); }
std::unique_ptr<DocViewer> createSlidesViewer() { return std::make_unique<SlidesView>(); }

Image textThumb (const File& f, int side)
{
    const auto e = f.getFileExtension().toLowerCase();
    if (e == ".docx" || e == ".odt")
        if (auto im = embeddedDocThumb (f); im.isValid()) return img::downscale (im, side);
    auto lines = StringArray::fromLines (loadAnyText (f).substring (0, 6000));
    return pageThumb (lines, side, false);
}

Image tableThumb (const File& f, int side)
{
    const auto e = f.getFileExtension().toLowerCase();
    if (e == ".xlsx" || e == ".xlsm" || e == ".ods")
        if (auto im = embeddedDocThumb (f); im.isValid()) return img::downscale (im, side);
    auto t = loadTable (f);
    StringArray lines;
    for (size_t r = 0; r < jmin<size_t> (t.size(), 30); ++r) lines.add (t[r].joinIntoString ("  "));
    return pageThumb (lines, side, true);
}

Image slidesThumb (const File& f, int side)
{
    return img::downscale (embeddedDocThumb (f), side);
}
}
