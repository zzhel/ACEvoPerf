#include "acevo/engine/active_reset_probe.h"
#include "acevo/engine/active_reset_input.h"
#include "acevo/engine/active_reset_session.h"
#include "acevo/core/code_patch.h"
#include "acevo/core/config.h"
#include "acevo/core/log.h"
#include "acevo/ui/active_reset_ui.h"
#include <cmath>

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
double timerStart = 0;
struct Vec3 { float x = 0, y = 0, z = 0; };
struct Gate { Vec3 position, direction; bool set = false, armed = false, previousValid = false; double previousDistance = 0, previousTime = 0; } gate;
double nextPositionSample = 0;
uint32_t finishedTime = 0;
uint32_t bestTime = 0;
bool finished = false;

struct Proto {
    const BYTE* p;
    const BYTE* end;
    bool Varint(uint64_t& value) {
        value = 0;
        for (unsigned shift = 0; shift < 64 && p < end; shift += 7) {
            const BYTE b = *p++;
            if (shift == 63 && b > 1) return false;
            value |= uint64_t(b & 127) << shift;
            if (!(b & 128)) return true;
        }
        return false;
    }
    bool Field(unsigned& field, unsigned& wire, uint64_t& value, Proto& bytes) {
        uint64_t tag = 0;
        if (!Varint(tag) || !tag || (tag >> 3) > UINT32_MAX) return false;
        field = unsigned(tag >> 3); wire = unsigned(tag & 7); value = 0;
        if (wire == 0) return Varint(value);
        if (wire == 2) {
            if (!Varint(value) || value > uint64_t(end - p)) return false;
            bytes = {p, p + size_t(value)}; p += size_t(value); return true;
        }
        const size_t size = wire == 5 ? 4 : wire == 1 ? 8 : 0;
        if (!size || size_t(end - p) < size) return false;
        if (size == 4) { uint32_t bits; memcpy(&bits, p, 4); value = bits; }
        p += size; return true;
    }
};

bool Submessage(Proto input, unsigned wanted, Proto& result)
{
    while (input.p < input.end) {
        unsigned field, wire; uint64_t value; Proto bytes{};
        if (!input.Field(field, wire, value, bytes)) return false;
        if (field == wanted && wire == 2) { result = bytes; return true; }
    }
    return false;
}

bool ReadVector(Proto input, unsigned firstField, Vec3& result)
{
    float values[3] = {};
    while (input.p < input.end) {
        unsigned field, wire; uint64_t value; Proto bytes{};
        if (!input.Field(field, wire, value, bytes)) return false;
        if (wire == 5 && field >= firstField && field < firstField + 3) {
            const uint32_t bits = uint32_t(value);
            memcpy(&values[field - firstField], &bits, sizeof bits);
        }
    }
    result = {values[0], values[1], values[2]};
    return std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z);
}

bool ReadPosition(BYTE* controller, const CarId& id, Vec3& position, Vec3& velocity)
{
    Blob sample = {};
    const bool saved = saveSnapshot(controller, &id, &sample);
    bool valid = false;
    if (saved && sample.first && sample.last > sample.first) {
        Proto root{sample.first, sample.last}, body{}, matrix{}, speed{};
        valid = Submessage(root, 1, body) && Submessage(body, 1, matrix) &&
            Submessage(body, 2, speed) && ReadVector(matrix, 13, position) && ReadVector(speed, 1, velocity);
    }
    Blob empty = {};
    moveBlob(&sample, &empty);
    return valid;
}

void ClearCheckpoint()
{
    Blob empty = {};
    moveBlob(&checkpoint, &empty);
    checkpointCar = nullptr;
    checkpointWorld = nullptr;
    checkpointId = {};
    gate = {};
    finished = false;
    bestTime = 0;
    SetActiveResetTimer(0, 0);
    SetActiveResetBest(0, 0);
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
    const bool gateDown = action == 553;
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
                    timerStart = time;
                    NotifyActiveReset(epoch, ActiveResetNotice::Saved);
                    Log("[active-reset-probe] saved checkpoint, bytes=%zu", size_t(checkpoint.last - checkpoint.first));
                } else {
                    ClearCheckpoint();
                    NotifyActiveReset(epoch, ActiveResetNotice::SaveFailed);
                    Log("[active-reset-probe] save failed");
                }
            } else {
                Log("[active-reset-probe] save refused; requires exactly one physics car");
                NotifyActiveReset(epoch, ActiveResetNotice::SaveFailed);
            }
        } else if (focused && loadDown) {
            if (car && checkpoint.first) {
                const bool restored = restoreSnapshot(controller, &id, &checkpoint);
                if (restored) {
                    timerStart = time;
                    finished = false;
                    gate.armed = gate.set;
                    gate.previousValid = false;
                    nextPositionSample = time;
                }
                NotifyActiveReset(epoch, restored ? ActiveResetNotice::Restored : ActiveResetNotice::RestoreFailed);
                Log("[active-reset-probe] restore returned %d; driving result requires verification", restored ? 1 : 0);
            } else {
                Log("[active-reset-probe] no checkpoint for this car");
                NotifyActiveReset(epoch, ActiveResetNotice::NoSnapshot);
            }
        } else if (focused && gateDown) {
            Vec3 position, velocity;
            const bool valid = car && checkpoint.first && ReadPosition(controller, id, position, velocity);
            const float speed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
            if (valid && speed >= 2.0f) {
                gate = {};
                gate.position = position;
                gate.direction = {velocity.x / speed, 0, velocity.z / speed};
                gate.set = true;
                bestTime = 0;
                finished = false;
                SetActiveResetBest(0, 0);
                NotifyActiveReset(epoch, ActiveResetNotice::GateSaved);
                Log("[active-reset-probe] gate set at %.2f %.2f %.2f", position.x, position.y, position.z);
            } else {
                NotifyActiveReset(epoch, ActiveResetNotice::GateFailed);
            }
        }
        if (gate.armed && !finished && car && time >= nextPositionSample) {
            nextPositionSample = time + 50;
            Vec3 position, velocity;
            if (ReadPosition(controller, id, position, velocity)) {
                const double x = position.x - gate.position.x, z = position.z - gate.position.z;
                const double distance = x * gate.direction.x + z * gate.direction.z;
                const double lateral = x * -gate.direction.z + z * gate.direction.x;
                if (gate.previousValid && gate.previousDistance < 0 && distance >= 0 &&
                    std::abs(lateral) <= 15 && std::abs(position.y - gate.position.y) <= 5) {
                    const double fraction = -gate.previousDistance / (distance - gate.previousDistance);
                    const double elapsed = gate.previousTime + fraction * (time - gate.previousTime) - timerStart;
                    if (elapsed >= 0 && elapsed < 4294967295.0) {
                        finishedTime = uint32_t(elapsed);
                        finished = true;
                        gate.armed = false;
                        if (!bestTime || finishedTime < bestTime) bestTime = finishedTime;
                        SetActiveResetBest(epoch, bestTime);
                        Log("[active-reset-probe] gate crossed: time=%u best=%u ms", finishedTime, bestTime);
                    }
                }
                gate.previousDistance = distance;
                gate.previousTime = time;
                gate.previousValid = true;
            } else {
                gate.previousValid = false;
            }
        }
        if (checkpoint.first) {
            const double elapsed = time - timerStart;
            SetActiveResetTimer(epoch, finished ? finishedTime : elapsed > 0 && elapsed < 4294967295.0 ? static_cast<uint32_t>(elapsed) : 0);
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
