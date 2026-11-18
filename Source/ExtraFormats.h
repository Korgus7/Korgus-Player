#pragma once
#include "Common.h"

namespace kp
{
// Zaregistruje všetky zvukové formáty: základné z JUCE + REX (.rx2/.rex/.rcy) + AIFC s kompresiou.
void registerAllFormats (AudioFormatManager& fm);

// Celý REX loop vyskladaný zo slice-ov v pôvodnom tempe. Pre testy aj pre čítačku.
bool decodeRex (const void* data, size_t size, AudioBuffer<float>& out, double& sampleRate, int& bits, double& bpm);

// AIFF/AIFC formáty, ktoré JUCE nevie (ima4, ulaw, alaw, fl64, in24, in32, .aifc)
bool decodeAifc (const void* data, size_t size, AudioBuffer<float>& out, double& sampleRate, int& bits);
}
