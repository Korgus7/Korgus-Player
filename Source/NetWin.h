#pragma once
// Sieťové funkcie Windows (ping, MAC cez ARP, názov, zdieľané priečinky) – bez JUCE,
// aby sa hlavičky Windows nebili s JUCE
#include <cstdint>
#include <string>
#include <vector>

namespace kp::netwin
{
    void init();
    bool ping (uint32_t ip, int timeoutMs);                 // ip v poradí 192.168.0.1 = 0xC0A80001
    bool mac (uint32_t ip, std::string& out);               // "AA:BB:CC:DD:EE:FF"
    std::string hostName (uint32_t ip);                     // UTF-8, prázdne ak nie je
    std::vector<std::string> shares (uint32_t ip);          // zdieľané priečinky (UTF-8)
    std::vector<std::string> localIPv4();                   // adresy tohto PC
    bool runElevated (const std::string& exe, const std::string& args, int waitMs);   // so suhlasom spravcu (UAC), true = skoncilo s kodom 0
}
