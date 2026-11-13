#pragma once
#include "acevo/common.h"

void InstallActiveResetUi();

enum class ActiveResetNotice : uint64_t {
    Saved = 1,
    Restored,
    NoSnapshot,
    SaveFailed,
    RestoreFailed,
};

void NotifyActiveReset(uint64_t sessionEpoch, ActiveResetNotice notice);
