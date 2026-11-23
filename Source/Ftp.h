#pragma once
#include "Common.h"

namespace kp
{
// Uložené FTP pripojenie (heslo sa ukladá zašifrované cez Windows)
struct FtpConn
{
    String name, host, user, pass, dir;
    int port = 21;
    bool passive = true;
    bool tls = false;           // SSL/TLS (FTPS) – cez curl.exe vo Windows
};

struct FtpEntry { String name; bool dir = false; int64 size = 0; Time mod; };

// Spojenie cez Windows (WinINet). Jedna operácia naraz (zámok), volať z vlákna na pozadí.
class FtpSession
{
public:
    ~FtpSession();
    bool connect (const FtpConn& c, String& error);
    void disconnect();
    bool isConnected() const { return conn != nullptr; }
    const FtpConn& info() const { return cfg; }
    String startDir() const { return home; }

    bool list (const String& dir, std::vector<FtpEntry>& out, String& error);
    bool download (const String& remote, const File& local, std::atomic<bool>& cancel, std::atomic<int64>& done, String& error);
    bool upload (const File& local, const String& remote, std::atomic<bool>& cancel, std::atomic<int64>& done, String& error);
    bool removeFile (const String& remote);
    bool removeDir (const String& remote);
    bool makeDir (const String& remote);
    bool rename (const String& from, const String& to);

    static String join (const String& dir, const String& name) { return (dir.endsWithChar ('/') ? dir : dir + "/") + name; }
    static String parent (const String& dir);

private:
    CriticalSection lock;
    void* inet = nullptr;
    void* conn = nullptr;
    FtpConn cfg;
    String home = "/";
    String lastError() const;
    // FTPS cez curl.exe
    bool curlRun (const StringArray& extra, const String& url, String& out, String& error, std::atomic<bool>* cancel = nullptr,
                  const File& watchFile = {}, std::atomic<int64>* done = nullptr);
    String curlUrl (const String& path, bool dir) const;
    bool curlQuote (const StringArray& cmds);
};

// Kopírovanie / presun / mazanie so serverom na pozadí (priebeh ako pri Transfer)
class FtpJob : private Thread
{
public:
    enum class Kind { Upload, Download, Delete };
    struct Item { File local; String remote; bool dir = false; int64 size = 0; };

    FtpJob (std::shared_ptr<FtpSession> s, Kind k, std::vector<Item> items, bool move);
    ~FtpJob() override;
    void start() { startThread(); }
    void cancel() { cancelled = true; }
    bool isDone() const { return finished; }
    Kind kind() const { return what; }
    bool isMove() const { return move; }
    double progress() const { return total > 0 ? (double) done.load() / (double) total.load() : 0.0; }
    int currentIndex() const { return current; }
    int count() const { return numFiles; }
    int completedCount() const { return completed; }
    bool wasCancelled() const { return cancelled; }
    String error;

private:
    void run() override;
    struct Op { File local; String remote; bool dir; int64 size; };
    void planUpload (const File& local, const String& remote, std::vector<Op>& ops);
    void planDownload (const String& remote, const File& local, std::vector<Op>& ops);
    void planDelete (const String& remote, bool dir, std::vector<Op>& ops);

    std::shared_ptr<FtpSession> session;
    Kind what;
    std::vector<Item> items;
    bool move;
    std::atomic<int64> total { 0 }, done { 0 };
    std::atomic<int> current { 0 }, numFiles { 0 }, completed { 0 };
    std::atomic<bool> cancelled { false }, finished { false };
};

namespace ftpstore
{
    std::vector<FtpConn> load (PropertiesFile& s);
    void save (PropertiesFile& s, const std::vector<FtpConn>& v);
    bool exportTo (const File& f, const std::vector<FtpConn>& v, const String& password);
    bool importFrom (const File& f, const String& password, std::vector<FtpConn>& out);
}

// Okno so zoznamom pripojení (Ctrl+F)
class FtpManager : public Component, private ListBoxModel
{
public:
    FtpManager (PropertiesFile& s);
    std::function<void (const FtpConn&)> onConnect;
    std::function<void()> onClose;
    void paint (Graphics& g) override;
    void resized() override;
    bool keyPressed (const KeyPress& key) override;

private:
    PropertiesFile& settings;
    std::vector<FtpConn> conns;
    ListBox list { {}, this };
    TextButton connectB, newB, editB, delB, exportB, importB, closeB;
    std::unique_ptr<Component> editor;
    std::unique_ptr<AlertWindow> askWin;
    std::unique_ptr<FileChooser> chooser;
    int getNumRows() override { return (int) conns.size(); }
    void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int, const MouseEvent&) override { doConnect(); }
    void returnKeyPressed (int) override { doConnect(); }
    void doConnect();
    void edit (int index);
    void remove();
    void doExport();
    void doImport();
    void askPassword (const String& title, std::function<void (String)> done);
    void store();
};
}
