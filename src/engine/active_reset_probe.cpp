#include "acevo/engine/active_reset_probe.h"
#include "acevo/core/code_patch.h"
#include "acevo/core/config.h"
#include "acevo/core/log.h"

namespace {
using StepFn = void (*)(BYTE*, float, double);
StepFn originalStep = nullptr;
BYTE* expectedVtable = nullptr;
BYTE* lastController = nullptr;
bool wasSaveDown = false;
bool wasLoadDown = false;
bool reportedMismatch = false;

void StepProbe(BYTE* controller, float delta, double time)
{
    originalStep(controller, delta, time);
    if (*reinterpret_cast<BYTE**>(controller) != expectedVtable) {
        if (!reportedMismatch) {
            Log("[active-reset-probe] unexpected controller vtable; diagnostics skipped");
            reportedMismatch = true;
        }
        return;
    }
    if (controller != lastController) {
        Log("[active-reset-probe] physics callback reached, thread=%lu controller=%p dt=%.6f",
            GetCurrentThreadId(), controller, delta);
        lastController = controller;
        wasSaveDown = false;
        wasLoadDown = false;
    }
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    const bool focused = foregroundPid == GetCurrentProcessId();
    const bool saveDown = (GetAsyncKeyState('I') & 0x8000) != 0;
    const bool loadDown = (GetAsyncKeyState('U') & 0x8000) != 0;
    if (focused && ((saveDown && !wasSaveDown) || (loadDown && !wasLoadDown))) {
        const auto world = *reinterpret_cast<BYTE**>(controller + 0x2dc0);
        size_t count = 0;
        if (world) {
            const auto first = *reinterpret_cast<uintptr_t*>(world + 0x10);
            const auto last = *reinterpret_cast<uintptr_t*>(world + 0x18);
            if (last >= first && (last - first) % sizeof(void*) == 0)
                count = (last - first) / sizeof(void*);
        }
        Log("[active-reset-probe] key=%s physics_cars=%zu thread=%lu; diagnostic only, no state changed",
            saveDown && !wasSaveDown ? "I" : "U", count, GetCurrentThreadId());
    }
    wasSaveDown = saveDown;
    wasLoadDown = loadDown;
}
}

void InstallActiveResetProbe()
{
    const std::wstring ini = g_dir + L"acevo_active_reset.ini";
    if (!GetPrivateProfileIntW(L"probe", L"enabled", 0, ini.c_str())) return;
    auto exe = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(exe);
    const auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(exe + dos->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != 0x6a9ec72a || nt->OptionalHeader.SizeOfImage != 0x06cdd000 ||
        Fnv1a64(exe + 0x14d6a2c, 17) != 0xa28f80be32120031ull ||
        Fnv1a64(exe + 0x4d310, 5) != 0xc986d1de468c8117ull ||
        Fnv1a64(exe + 0x14d8c40, 64) != 0x38af18d0a8cb95ffull) {
        Log("[active-reset-probe] unsupported executable; no hook installed");
        return;
    }
    // Replace one call with a nearby absolute jump, preserving the original ABI and return address.
    BYTE* call = exe + 0x14d6a38;
    BYTE* relay = AllocNear(call, 14);
    if (!relay) {
        Log("[active-reset-probe] relay allocation failed");
        return;
    }
    BYTE code[14] = { 0xff, 0x25, 0, 0, 0, 0 };
    const auto target = reinterpret_cast<uintptr_t>(&StepProbe);
    memcpy(code + 6, &target, sizeof target);
    memcpy(relay, code, sizeof code);
    DWORD old = 0;
    BYTE patch[5];
    if (!VirtualProtect(relay, sizeof code, PAGE_EXECUTE_READ, &old) ||
        !EncodeRel32(0xe8, call, relay, patch)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        Log("[active-reset-probe] relay preparation failed");
        return;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, sizeof code);
    originalStep = reinterpret_cast<StepFn>(exe + 0x14d8c40);
    expectedVtable = exe + 0x3231a70;
    if (!WriteCode(call, patch, sizeof patch)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        Log("[active-reset-probe] call patch failed");
        return;
    }
    Log("[active-reset-probe] installed for EVO 0.9.1; I/U diagnostics only, no snapshot restoration");
}
