#pragma once
#include "acevo/common.h"

// Called only by the physics thread. Returns a configured action on the press edge.
unsigned PollActiveResetInput(HWND foreground, bool reset);
