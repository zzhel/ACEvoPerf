#pragma once
#include "acevo/common.h"

bool InstallActiveResetSessionGuard(BYTE* exe);
uint64_t ActiveResetSessionEpoch();

// Keep transitions out of the interval in which physics reads or restores a snapshot.
class ActiveResetSessionRead {
public:
    ActiveResetSessionRead();
    ~ActiveResetSessionRead();
    ActiveResetSessionRead(const ActiveResetSessionRead&) = delete;
    ActiveResetSessionRead& operator=(const ActiveResetSessionRead&) = delete;

    uint64_t Epoch() const;
};
