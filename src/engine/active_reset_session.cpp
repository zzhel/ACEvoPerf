#include "acevo/engine/active_reset_session.h"
#include "acevo/core/code_patch.h"
#include "acevo/core/iat.h"
#include "acevo/core/log.h"

namespace {
using CommandFn = void (*)(void*, const void*);
CommandFn startSession = nullptr;
CommandFn joinServer = nullptr;
CommandFn connectToServer = nullptr;
CommandFn exitSession = nullptr;
SRWLOCK transitionLock = SRWLOCK_INIT;
std::atomic<uint64_t> sessionEpoch{0};

uint64_t DisableReset(const char* reason)
{
    AcquireSRWLockExclusive(&transitionLock);
    const uint64_t previous = sessionEpoch.load(std::memory_order_relaxed);
    const uint64_t epoch = (previous & ~uint64_t{1}) + 2;
    sessionEpoch.store(epoch, std::memory_order_release);
    ReleaseSRWLockExclusive(&transitionLock);
    Log("[active-reset] session guard closed: %s", reason);
    return epoch;
}

void StartSession(void* client, const void* request)
{
    const uint64_t epoch = DisableReset("session start");
    const auto* args = static_cast<const BYTE*>(request);
    const auto* platform = static_cast<const BYTE*>(client);
    // The game uses separate variants for a server connection and a local season.
    const bool localRequest = !args[0x68] && args[0x98] == 1;
    const auto* localServer = reinterpret_cast<const void* const*>(platform + 0x4538);
    const bool hadLocalServer = *localServer != nullptr;
    startSession(client, request);
    if (!localRequest || hadLocalServer || !*localServer) return;

    AcquireSRWLockExclusive(&transitionLock);
    const bool current = sessionEpoch.load(std::memory_order_relaxed) == epoch;
    if (current) sessionEpoch.store(epoch | 1, std::memory_order_release);
    ReleaseSRWLockExclusive(&transitionLock);
    if (current) Log("[active-reset] session guard opened: local server created");
}

void JoinServer(void* callback, const void* request)
{
    DisableReset("multiplayer join");
    joinServer(callback, request);
}

void ConnectToServer(void* callback, const void* request)
{
    DisableReset("multiplayer connection");
    connectToServer(callback, request);
}

void ExitSession(void* callback, const void* request)
{
    DisableReset("session exit");
    exitSession(callback, request);
}

struct CommandHook {
    unsigned vtable;
    unsigned invoke;
    CommandFn replacement;
    CommandFn* original;
};

constexpr CommandHook commandHooks[] = {
    {0x316a160, 0x117408, &JoinServer, &joinServer},
    {0x31695d8, 0x1113af, &ConnectToServer, &connectToServer},
    {0x321d6a8, 0xb8cd7, &ExitSession, &exitSession},
};
}

uint64_t ActiveResetSessionEpoch()
{
    return sessionEpoch.load(std::memory_order_acquire);
}

ActiveResetSessionRead::ActiveResetSessionRead()
{
    AcquireSRWLockShared(&transitionLock);
}

ActiveResetSessionRead::~ActiveResetSessionRead()
{
    ReleaseSRWLockShared(&transitionLock);
}

uint64_t ActiveResetSessionRead::Epoch() const
{
    return sessionEpoch.load(std::memory_order_relaxed);
}

bool InstallActiveResetSessionGuard(BYTE* exe)
{
    BYTE* thunk = exe + 0x18e3f5;
    if (Fnv1a64(thunk, 5) != 0xf00918ce98e999aeull ||
        Fnv1a64(exe + 0x78b1b0, 0x74) != 0x27754b3f61e58406ull ||
        Fnv1a64(exe + 0x1306010, 0x43) != 0xf4fa14afa42e66abull) return false;
    constexpr int invokeSlot = 2;
    for (const CommandHook& hook : commandHooks) {
        auto vtable = reinterpret_cast<void**>(exe + hook.vtable);
        if (vtable[invokeSlot] != exe + hook.invoke) return false;
    }
    for (const CommandHook& hook : commandHooks) {
        auto vtable = reinterpret_cast<void**>(exe + hook.vtable);
        HookVtableSlot(vtable, invokeSlot, reinterpret_cast<void*>(hook.replacement),
            reinterpret_cast<void**>(hook.original), "active reset session transition");
        if (!*hook.original) return false;
    }

    BYTE* relay = AllocNear(thunk, 14);
    if (!relay) return false;
    BYTE code[14] = {0xff, 0x25, 0, 0, 0, 0};
    const auto target = reinterpret_cast<uintptr_t>(&StartSession);
    memcpy(code + 6, &target, sizeof target);
    memcpy(relay, code, sizeof code);
    DWORD old = 0;
    BYTE patch[5];
    if (!VirtualProtect(relay, sizeof code, PAGE_EXECUTE_READ, &old) ||
        !EncodeRel32(0xe9, thunk, relay, patch)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, sizeof code);
    startSession = reinterpret_cast<CommandFn>(exe + 0x78b1b0);
    if (!WriteCode(thunk, patch, sizeof patch)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    return true;
}
