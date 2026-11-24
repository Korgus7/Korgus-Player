#pragma once
#include "Common.h"

namespace kp
{
// Zariadenie nájdené v sieti
struct NetHost
{
    uint32 ip = 0;
    String ipText, name, mac, vendor;
    Array<int> ports;
    StringArray shares;          // „*“ na začiatku = tlačiareň
    bool isNew = false;
    String key() const { return mac.isNotEmpty() ? mac : "ip:" + ipText; }
};

// Prehľadávanie siete (bez okna): ping, MAC, názov, služby, zdieľané priečinky, výrobca
class NetEngine : private Timer
{
public:
    explicit NetEngine (PropertiesFile& settings);
    ~NetEngine() override;

    void start (const String& range);
    void stop();
    bool isScanning() const { return pool != nullptr; }
    int progressDone() const { return done.load(); }
    int progressTotal() const { return total; }
    int newCount() const { return numNew; }
    String range() const;
    void setRange (const String& r);

    std::vector<NetHost> hosts;
    std::function<void()> onChange;             // nové zariadenie / koniec / výrobcovia

    String displayName (const NetHost& h) const;
    void setName (const NetHost& h, const String& name);
    String servicesText (const NetHost& h) const;
    static String serviceName (int port);
    static String defaultCamPath (const String& vendor);
    PropertiesFile& settings;

private:
    std::unique_ptr<ThreadPool> pool;
    std::shared_ptr<std::atomic<bool>> stopFlag;
    std::atomic<int> done { 0 };
    int total = 0, numNew = 0;
    std::map<String, String> oui, names;
    bool ouiLoading = false;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    void addHost (NetHost h);
    void finish();
    void loadOui();
    void loadNames();
    String vendorOf (const String& mac) const;
    void timerCallback() override;
};

// Podrobnosti zariadenia a živý obraz z kamery (v druhom paneli Korgus Commandera)
class NetDetail : public Component
{
public:
    explicit NetDetail (NetEngine& engine);
    ~NetDetail() override;
    void setHost (int index);                    // index v engine.hosts, -1 = nič
    int getHost() const { return host; }

    std::function<void (const File&)> openFolder;
    std::function<void (const String&)> openFtp;
    std::function<Component*()> makeVideo;
    std::function<void (Component*, const String&)> playUrl;
    std::function<void (Component*)> stopVideo;
    bool videoAvailable = false;

    void runAction (int id);                     // 1 web, 2 https, 3 web 8080, 4 RDP, 5 FTP, 6 rtsp, 7 Prieskumník, 8 IP, 9 MAC, 100+ priečinok
    void renameHost();
    void stopCamera();

    void paint (Graphics& g) override;
    void resized() override;

private:
    NetEngine& net;
    int host = -1;
    OwnedArray<TextButton> actions;
    TextEditor camUser, camPass, camPath;
    TextButton camB, nameB;
    std::unique_ptr<Component> video;
    std::unique_ptr<AlertWindow> ask;
    bool camOn = false;
    void rebuild();
    void startCamera();
    String camKey() const;
    const NetHost* cur() const { return host >= 0 && host < (int) net.hosts.size() ? &net.hosts[(size_t) host] : nullptr; }
};
}
