#pragma once
#include "Common.h"

namespace kp
{
// Skener siete: zariadenia v rozsahu adries, názov, MAC, výrobca, otvorené služby a zdieľané priečinky
class NetScanner : public Component, private TableListBoxModel, private Timer
{
public:
    explicit NetScanner (PropertiesFile& settings);
    ~NetScanner() override;

    std::function<void (const File&)> openFolder;     // \\ip\priečinok → Korgus Commander
    std::function<void (const String&)> openFtp;      // FTP v Korgus Commanderi

    void paint (Graphics& g) override;
    void resized() override;

private:
    struct Host
    {
        uint32 ip = 0;
        String ipText, name, mac, vendor;
        Array<int> ports;
        StringArray shares;
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
    int total = 0;
    int sortCol = 3;
    bool sortUp = true;
    std::map<String, String> oui;          // výrobca podľa prvých 3 bajtov MAC
    bool ouiLoading = false;

    void startScan();
    void stopScan();
    void addHost (Host h);
    void rebuildView();
    void loadOui();
    String vendorOf (const String& mac) const;
    static String serviceName (int port);
    String servicesText (const Host& h) const;
    void hostMenu (int row);
    void timerCallback() override;

    int getNumRows() override { return (int) view.size(); }
    void paintRowBackground (Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (Graphics&, int row, int col, int w, int h, bool selected) override;
    void sortOrderChanged (int col, bool forwards) override;
    void cellDoubleClicked (int row, int, const MouseEvent&) override { hostMenu (row); }
    void cellClicked (int row, int, const MouseEvent& e) override { if (e.mods.isPopupMenu()) hostMenu (row); }
};
}
