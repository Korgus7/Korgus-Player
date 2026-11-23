#pragma once
#include "Common.h"

namespace kp
{
// Skener siete: vľavo zariadenia v rozsahu adries, vpravo podrobnosti a živý obraz z kamery
class NetScanner : public Component, private TableListBoxModel, private Timer
{
public:
    explicit NetScanner (PropertiesFile& settings);
    ~NetScanner() override;

    std::function<void (const File&)> openFolder;     // \\ip\priečinok → Korgus Commander
    std::function<void (const String&)> openFtp;      // FTP v Korgus Commanderi
    // prehrávač pre kameru (dodá Player – VideoView)
    std::function<Component*()> makeVideo;
    std::function<void (Component*, const String&)> playUrl;
    std::function<void (Component*)> stopVideo;
    bool videoAvailable = false;

    void paint (Graphics& g) override;
    void resized() override;

private:
    struct Host
    {
        uint32 ip = 0;
        String ipText, name, mac, vendor;
        Array<int> ports;
        StringArray shares;
        bool isNew = false;
        String key() const { return mac.isNotEmpty() ? mac : "ip:" + ipText; }
    };
    PropertiesFile& settings;
    std::vector<Host> hosts;
    std::vector<int> view;                 // vyfiltrované a zoradené
    TextEditor range, filter;
    TextButton scanB;
    TableListBox table { {}, this };
    std::unique_ptr<ThreadPool> pool;
    std::shared_ptr<std::atomic<bool>> stopFlag;
    std::atomic<int> done { 0 };
    int total = 0, newCount = 0;
    int sortCol = 3;
    bool sortUp = true;
    std::map<String, String> oui;          // výrobca podľa prvých 3 bajtov MAC
    bool ouiLoading = false;
    std::map<String, String> names;        // vlastné názvy zariadení (podľa MAC)

    // pravá časť: podrobnosti a kamera
    int selected = -1;                     // index v hosts
    OwnedArray<TextButton> actions;
    TextEditor camUser, camPass, camPath;
    TextButton camB, nameB;
    std::unique_ptr<Component> video;
    bool camOn = false;
    Rectangle<int> detailArea() const;
    void showDetails();
    void layoutDetails();
    void startCamera();
    void stopCamera();
    String camKey() const;
    static String defaultCamPath (const String& vendor);
    String displayName (const Host& h) const;
    void renameHost();
    std::unique_ptr<AlertWindow> ask;

    void startScan();
    void stopScan();
    void finishScan();
    void addHost (Host h);
    void rebuildView();
    void loadOui();
    void loadNames();
    void saveNames();
    String vendorOf (const String& mac) const;
    static String serviceName (int port);
    String servicesText (const Host& h) const;
    void hostMenu (int row);
    void runAction (const Host& h, int id);
    void timerCallback() override;

    int getNumRows() override { return (int) view.size(); }
    void paintRowBackground (Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (Graphics&, int row, int col, int w, int h, bool selected) override;
    void sortOrderChanged (int col, bool forwards) override;
    void selectedRowsChanged (int row) override;
    void cellDoubleClicked (int row, int, const MouseEvent&) override { hostMenu (row); }
    void cellClicked (int row, int, const MouseEvent& e) override { if (e.mods.isPopupMenu()) hostMenu (row); }
};
}
