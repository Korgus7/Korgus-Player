#include "NetWin.h"

#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <winsock2.h>
 #include <ws2tcpip.h>
 #include <windows.h>
 #include <iphlpapi.h>
 #include <icmpapi.h>
 #include <lm.h>
 #include <shellapi.h>
 #include <mutex>
#else
 #include <arpa/inet.h>
 #include <netdb.h>
 #include <ifaddrs.h>
 #include <netinet/in.h>
 #include <sys/socket.h>
#endif
#include <cstdio>
#include <cstring>

namespace kp::netwin
{
#ifdef _WIN32
namespace
{
    std::string toUtf8 (const wchar_t* w)
    {
        if (w == nullptr || *w == 0) return {};
        const int n = WideCharToMultiByte (CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string s ((size_t) (n > 0 ? n - 1 : 0), '\0');
        if (n > 1) WideCharToMultiByte (CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
        return s;
    }
    IPAddr toNet (uint32_t ip) { return htonl (ip); }
}

void init()
{
    static std::once_flag once;
    std::call_once (once, [] { WSADATA d; WSAStartup (MAKEWORD (2, 2), &d); });
}

bool ping (uint32_t ip, int timeoutMs)
{
    HANDLE h = IcmpCreateFile();
    if (h == INVALID_HANDLE_VALUE) return false;
    char data[8] = "korgus";
    char reply[sizeof (ICMP_ECHO_REPLY) + 64] = {};
    const DWORD n = IcmpSendEcho (h, toNet (ip), data, (WORD) sizeof (data), nullptr, reply, sizeof (reply), (DWORD) timeoutMs);
    IcmpCloseHandle (h);
    return n > 0 && ((PICMP_ECHO_REPLY) reply)->Status == IP_SUCCESS;
}

bool mac (uint32_t ip, std::string& out)
{
    ULONG m[2] = {};
    ULONG len = 6;
    if (SendARP (toNet (ip), 0, m, &len) != NO_ERROR || len < 6) return false;
    const auto* b = (const unsigned char*) m;
    char buf[32];
    std::snprintf (buf, sizeof (buf), "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
    out = buf;
    return true;
}

std::string hostName (uint32_t ip)
{
    init();
    sockaddr_in sa {};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = toNet (ip);
    wchar_t host[NI_MAXHOST] = {};
    if (GetNameInfoW ((const sockaddr*) &sa, sizeof (sa), host, NI_MAXHOST, nullptr, 0, NI_NAMEREQD) == 0)
        return toUtf8 (host);
    return {};
}

std::vector<std::string> shares (uint32_t ip)
{
    std::vector<std::string> v;
    char a[32];
    std::snprintf (a, sizeof (a), "\\\\%u.%u.%u.%u", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    wchar_t server[32] = {};
    MultiByteToWideChar (CP_ACP, 0, a, -1, server, 32);
    LPBYTE buf = nullptr;
    DWORD read = 0, total = 0, resume = 0;
    if (NetShareEnum (server, 1, &buf, MAX_PREFERRED_LENGTH, &read, &total, &resume) == NERR_Success && buf != nullptr)
    {
        auto* s = (SHARE_INFO_1*) buf;
        for (DWORD i = 0; i < read; ++i)
            if ((s[i].shi1_type & 0xff) == STYPE_DISKTREE || (s[i].shi1_type & 0xff) == STYPE_PRINTQ)
            {
                std::string n = toUtf8 (s[i].shi1_netname);
                if (! n.empty() && n.back() != '$') v.push_back (((s[i].shi1_type & 0xff) == STYPE_PRINTQ ? "*" : "") + n);
            }
        NetApiBufferFree (buf);
    }
    return v;
}

std::vector<std::string> localIPv4()
{
    std::vector<std::string> v;
    ULONG len = 15000;
    std::vector<unsigned char> buf (len);
    if (GetAdaptersAddresses (AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                              (PIP_ADAPTER_ADDRESSES) buf.data(), &len) == ERROR_BUFFER_OVERFLOW)
    {
        buf.resize (len);
        if (GetAdaptersAddresses (AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                  (PIP_ADAPTER_ADDRESSES) buf.data(), &len) != NO_ERROR) return v;
    }
    for (auto* a = (PIP_ADAPTER_ADDRESSES) buf.data(); a != nullptr; a = a->Next)
    {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u != nullptr; u = u->Next)
        {
            const auto* sa = (const sockaddr_in*) u->Address.lpSockaddr;
            const uint32_t ip = ntohl (sa->sin_addr.s_addr);
            char b[32];
            std::snprintf (b, sizeof (b), "%u.%u.%u.%u", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
            v.push_back (b);
        }
    }
    return v;
}
#else
// mimo Windows (len na skúšanie): bez pingu a MAC
void init() {}
bool ping (uint32_t, int) { return false; }
bool mac (uint32_t, std::string&) { return false; }
std::string hostName (uint32_t ip)
{
    sockaddr_in sa {};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl (ip);
    char host[NI_MAXHOST] = {};
    if (getnameinfo ((const sockaddr*) &sa, sizeof (sa), host, NI_MAXHOST, nullptr, 0, NI_NAMEREQD) == 0) return host;
    return {};
}
std::vector<std::string> shares (uint32_t) { return {}; }
std::vector<std::string> localIPv4()
{
    std::vector<std::string> v;
    ifaddrs* ifa = nullptr;
    if (getifaddrs (&ifa) != 0) return v;
    for (auto* i = ifa; i != nullptr; i = i->ifa_next)
        if (i->ifa_addr != nullptr && i->ifa_addr->sa_family == AF_INET)
        {
            char b[INET_ADDRSTRLEN];
            inet_ntop (AF_INET, &((sockaddr_in*) i->ifa_addr)->sin_addr, b, sizeof (b));
            if (std::strcmp (b, "127.0.0.1") != 0) v.push_back (b);
        }
    freeifaddrs (ifa);
    return v;
}
#endif

#ifdef _WIN32
static std::wstring wide (const std::string& u)
{
    if (u.empty()) return {};
    const int n = MultiByteToWideChar (CP_UTF8, 0, u.c_str(), (int) u.size(), nullptr, 0);
    std::wstring w ((size_t) n, L'\0');
    MultiByteToWideChar (CP_UTF8, 0, u.c_str(), (int) u.size(), w.data(), n);
    return w;
}
bool runElevated (const std::string& exe, const std::string& args, int waitMs)
{
    const auto e = wide (exe), a = wide (args);
    SHELLEXECUTEINFOW sei {};
    sei.cbSize = sizeof (sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = e.c_str();
    sei.lpParameters = a.c_str();
    sei.nShow = SW_HIDE;
    if (! ShellExecuteExW (&sei) || sei.hProcess == nullptr) return false;   // zamietnute v UAC
    WaitForSingleObject (sei.hProcess, (DWORD) waitMs);
    DWORD code = 1;
    GetExitCodeProcess (sei.hProcess, &code);
    CloseHandle (sei.hProcess);
    return code == 0;
}
#else
bool runElevated (const std::string&, const std::string&, int) { return false; }
#endif
}
