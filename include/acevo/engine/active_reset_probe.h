#pragma once
#include <cstdint>

void InstallActiveResetProbe();
bool ReadActiveResetMarker(uint64_t epoch, float& x, float& y);
