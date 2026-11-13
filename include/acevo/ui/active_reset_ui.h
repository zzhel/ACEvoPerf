#pragma once
#include "acevo/common.h"

void InstallActiveResetUi();

enum class ActiveResetNotice : uint64_t {
    Saved = 1,
    Restored,
    NoSnapshot,
    SaveFailed,
    RestoreFailed,
    GateSaved,
    GateFailed,
};

void NotifyActiveReset(uint64_t sessionEpoch, ActiveResetNotice notice);
void SetActiveResetTimer(uint64_t sessionEpoch, uint32_t elapsedMs);
void SetActiveResetBest(uint64_t sessionEpoch, uint32_t elapsedMs);
void SetActiveResetDelta(uint64_t sessionEpoch, int32_t deltaMs);
void SetActiveResetFinished(uint64_t sessionEpoch, bool finished, bool improved);
