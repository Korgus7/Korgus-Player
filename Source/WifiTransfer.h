#pragma once
#include "Common.h"

namespace kp
{
// Prenos z mobilu cez Wi-Fi: malý webový server v PC, mobil načíta QR kód a v prehliadači
// posiela súbory do priečinka (alebo si ich z neho stiahne). Funguje len kým je okno otvorené.
class WifiTransfer : public Component, private Thread
{
public:
    explicit WifiTransfer (const File& targetFolder);
    ~WifiTransfer() override;

    std::function<void (const File&)> onReceived;   // na hlavnom vlákne
    const String& getAddress() const { return address; }
    void paint (Graphics& g) override;
    void resized() override;

private:
    File target;
    StreamingSocket listener;
    int port = 0;
    String token, address, error;
    Image qr;
    StringArray log;
    std::unique_ptr<ThreadPool> clients;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    TextButton openB;

    void run() override;
    void handle (StreamingSocket* s);
    void addLog (const String& line);
    void makeQr();
};
}
