#include "acevo/engine/active_reset_probe.h"
#include "acevo/engine/active_reset_input.h"
#include "acevo/engine/active_reset_session.h"
#include "acevo/core/code_patch.h"
#include "acevo/core/config.h"
#include "acevo/core/log.h"
#include "acevo/ui/active_reset_ui.h"

namespace {
using StepFn = void (*)(BYTE*, float, double);
StepFn originalStep = nullptr;
BYTE* expectedVtable = nullptr;
BYTE* lastController = nullptr;
uint64_t lastSessionEpoch = 0;
bool reportedMismatch = false;
struct Blob { BYTE* first; BYTE* last; BYTE* capacity; };
struct CarId { uint64_t low; uint64_t high; };
using SnapshotFn = bool (*)(BYTE*, const CarId*, Blob*);
using MoveBlobFn = Blob* (*)(Blob*, Blob*);
SnapshotFn saveSnapshot = nullptr;
SnapshotFn restoreSnapshot = nullptr;
MoveBlobFn moveBlob = nullptr;
Blob checkpoint = {};
CarId checkpointId = {};
BYTE* checkpointCar = nullptr;
BYTE* checkpointWorld = nullptr;
bool snapshotExperiment = false;

void ClearCheckpoint()
{
    Blob empty = {};
    moveBlob(&checkpoint, &empty);
    checkpointCar = nullptr;
    checkpointWorld = nullptr;
    checkpointId = {};
    SetActiveResetSnapshot(0);
}

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
    const ActiveResetSessionRead session;
    const uint64_t epoch = session.Epoch();
    const bool sessionChanged = epoch != lastSessionEpoch;
    lastSessionEpoch = epoch;
    if (sessionChanged && checkpoint.first) ClearCheckpoint();
    if (!(epoch & 1)) {
        PollActiveResetInput(nullptr, true);
        return;
    }
    const bool controllerChanged = controller != lastController;
    if (controllerChanged) {
        if (snapshotExperiment) ClearCheckpoint();
        Log("[active-reset-probe] physics callback reached, thread=%lu controller=%p dt=%.6f",
            GetCurrentThreadId(), controller, delta);
        lastController = controller;
    }
    DWORD foregroundPid = 0;
    const HWND foreground = GetForegroundWindow();
    GetWindowThreadProcessId(foreground, &foregroundPid);
    const bool focused = foregroundPid == GetCurrentProcessId();
    const unsigned action = PollActiveResetInput(focused ? foreground : nullptr, controllerChanged || sessionChanged);
    const bool saveDown = action == 551;
    const bool loadDown = action == 552;
    if (snapshotExperiment) {
        const auto world = *reinterpret_cast<BYTE**>(controller + 0x2dc0);
        const auto first = world ? *reinterpret_cast<uintptr_t*>(world + 0x10) : 0;
        const auto last = world ? *reinterpret_cast<uintptr_t*>(world + 0x18) : 0;
        BYTE* car = first && last >= first && last - first == sizeof(void*)
            ? *reinterpret_cast<BYTE**>(first) : nullptr;
        CarId id = {};
        if (car) memcpy(&id, car + 0x7700, sizeof id);
        if (checkpoint.first && (car != checkpointCar || world != checkpointWorld ||
            id.low != checkpointId.low || id.high != checkpointId.high)) {
            ClearCheckpoint();
            Log("[active-reset-probe] checkpoint cleared after car/world change");
        }
        if (focused && saveDown) {
            if (car && (id.low || id.high)) {
                ClearCheckpoint();
                const bool saved = saveSnapshot(controller, &id, &checkpoint);
                if (saved && checkpoint.first && checkpoint.last > checkpoint.first) {
                    checkpointCar = car;
                    checkpointWorld = world;
                    checkpointId = id;
                    SetActiveResetSnapshot(epoch);
                    Log("[active-reset-probe] saved checkpoint, bytes=%zu", size_t(checkpoint.last - checkpoint.first));
                } else {
                    ClearCheckpoint();
                    Log("[active-reset-probe] save failed");
                }
            } else {
                Log("[active-reset-probe] save refused; requires exactly one physics car");
            }
        } else if (focused && loadDown) {
            if (car && checkpoint.first) {
                const bool restored = restoreSnapshot(controller, &id, &checkpoint);
                Log("[active-reset-probe] restore returned %d; driving result requires verification", restored ? 1 : 0);
            } else {
                Log("[active-reset-probe] no checkpoint for this car");
            }
        }
        return;
    }
    if (focused && (saveDown || loadDown)) {
        const auto world = *reinterpret_cast<BYTE**>(controller + 0x2dc0);
        size_t count = 0;
        if (world) {
            const auto first = *reinterpret_cast<uintptr_t*>(world + 0x10);
            const auto last = *reinterpret_cast<uintptr_t*>(world + 0x18);
            if (last >= first && (last - first) % sizeof(void*) == 0)
                count = (last - first) / sizeof(void*);
        }
        Log("[active-reset-probe] key=%s physics_cars=%zu thread=%lu; diagnostic only, no state changed",
            saveDown ? "Save" : "Restore", count, GetCurrentThreadId());
    }
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
        Fnv1a64(exe + 0x14d8c40, 64) != 0x38af18d0a8cb95ffull ||
        Fnv1a64(exe + 0x14d6dc0, 0xbc) != 0xd6ad4e52becf61a1ull ||
        Fnv1a64(exe + 0x14d6600, 0x5b) != 0x4ddfb5257ea5d513ull ||
        Fnv1a64(exe + 0x1117800, 0xa6) != 0x8991fe209197f409ull) {
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
    saveSnapshot = reinterpret_cast<SnapshotFn>(exe + 0x14d6dc0);
    restoreSnapshot = reinterpret_cast<SnapshotFn>(exe + 0x14d6600);
    moveBlob = reinterpret_cast<MoveBlobFn>(exe + 0x1117800);
    snapshotExperiment = GetPrivateProfileIntW(L"probe", L"solo_snapshot_experiment", 0, ini.c_str()) != 0;
    expectedVtable = exe + 0x3231a70;
    if (!InstallActiveResetSessionGuard(exe)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        Log("[active-reset-probe] session guard unavailable; reset disabled");
        return;
    }
    if (!WriteCode(call, patch, sizeof patch)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        Log("[active-reset-probe] call patch failed");
        return;
    }
    InstallActiveResetUi();
    Log("[active-reset-probe] installed for EVO 0.9.1; snapshot_experiment=%d", snapshotExperiment ? 1 : 0);
    if (snapshotExperiment)
        Log("[active-reset-probe] local sessions only: bind Save/Restore snapshot in Controls");
}
