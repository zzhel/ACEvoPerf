#include "acevo/engine/active_reset_input.h"
#include "acevo/core/log.h"
#include <shlobj.h>
#include <Xinput.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

namespace {
constexpr unsigned saveAction = 551;
constexpr unsigned restoreAction = 552;
constexpr unsigned maxConfigSize = 1024 * 1024;

// The binding files are protobuf messages. Read only their binding fields and skip unrelated settings.
struct Wire {
    const BYTE* begin = nullptr;
    const BYTE* end = nullptr;

    bool Varint(uint64_t& value)
    {
        value = 0;
        for (unsigned shift = 0; shift <= 63 && begin != end; shift += 7) {
            const BYTE byte = *begin++;
            if (shift == 63 && byte > 1) return false;
            value |= uint64_t(byte & 0x7f) << shift;
            if (byte < 0x80) return true;
        }
        return false;
    }

    template<class Visitor> bool Read(Visitor visit)
    {
        while (begin != end) {
            uint64_t tag = 0;
            uint64_t value = 0;
            Wire bytes;
            if (!Varint(tag) || !(tag >> 3) || tag > UINT32_MAX) return false;
            switch (tag & 7) {
            case 0:
                if (!Varint(value)) return false;
                break;
            case 2:
                if (!Varint(value) || value > uint64_t(end - begin)) return false;
                bytes = {begin, begin + value};
                begin += value;
                break;
            case 1:
            case 5:
                value = (tag & 7) == 1 ? 8 : 4;
                if (value > uint64_t(end - begin)) return false;
                begin += value;
                break;
            default:
                return false;
            }
            if (!visit(unsigned(tag >> 3), unsigned(tag & 7), value, bytes)) return false;
        }
        return true;
    }
};

struct Binding {
    unsigned action = 0;
    unsigned button = 0;
    unsigned holdMs = 0;
    std::vector<unsigned> modifiers;
    bool requireShift = false;
    bool armed = false;
    ULONGLONG pressedAt = 0;
};

bool ReadRepeated(unsigned type, uint64_t value, Wire bytes, std::vector<unsigned>& result)
{
    if (type == 0) {
        if (value > UINT32_MAX) return false;
        result.push_back(unsigned(value));
    } else if (type == 2) {
        while (bytes.begin != bytes.end) {
            if (!bytes.Varint(value) || value > UINT32_MAX) return false;
            result.push_back(unsigned(value));
        }
    }
    return true;
}

bool ReadBinding(Wire bytes, bool keyboard, std::vector<Binding>& bindings)
{
    Binding binding;
    bool valid = bytes.Read([&](unsigned field, unsigned type, uint64_t value, Wire nested) {
        if (field == 1 && type == 2) {
            return nested.Read([&](unsigned id, unsigned kind, uint64_t number, Wire) {
                if (kind == 0 && id == 1) binding.action = unsigned(number);
                if (kind == 0 && id == 2) binding.holdMs = unsigned(number);
                return true;
            });
        }
        if (field == 2 && type == 0) binding.button = unsigned(value);
        if (field == 3 && keyboard) return ReadRepeated(type, value, nested, binding.modifiers);
        if (field == 3 && type == 0) binding.requireShift = value != 0;
        return true;
    });
    if (valid && (binding.action == saveAction || binding.action == restoreAction))
        bindings.push_back(std::move(binding));
    return valid;
}

struct Device {
    std::wstring instance;
    bool xinput = false;
    std::vector<Binding> bindings;
    std::vector<unsigned> shifts;
    IDirectInputDevice8W* joystick = nullptr;
    ULONGLONG nextConnect = 0;
};

std::wstring configDir;
bool initialized = false;
IDirectInput8W* directInput = nullptr;
std::vector<Binding> keyboard;
std::vector<Device> devices;
std::vector<unsigned> allowedModifiers;

struct ConfigFile {
    FILETIME modified = {};
    DWORD size = 0;
    bool loaded = false;
};

template<class Parser> void RefreshConfig(const wchar_t* filename, ConfigFile& state, Parser parse)
{
    const std::wstring path = configDir + filename;
    WIN32_FILE_ATTRIBUTE_DATA attrs = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attrs)) {
        const DWORD error = GetLastError();
        if (state.loaded && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) {
            parse(Wire{});
            state.loaded = false;
        }
        return;
    }
    if (attrs.nFileSizeHigh || attrs.nFileSizeLow > maxConfigSize) return;
    if (state.loaded && attrs.nFileSizeLow == state.size && !CompareFileTime(&attrs.ftLastWriteTime, &state.modified)) return;

    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    std::vector<BYTE> data(size_t(attrs.nFileSizeLow) + 1);
    DWORD read = 0;
    const BOOL ok = ReadFile(file, data.data(), DWORD(data.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok || read != attrs.nFileSizeLow || !parse(Wire{data.data(), data.data() + read})) return;
    state = {attrs.ftLastWriteTime, read, true};
}

bool ReadKeyboard(Wire file)
{
    std::vector<Binding> bindings;
    std::vector<unsigned> modifiers;
    if (!file.Read([&](unsigned field, unsigned type, uint64_t value, Wire bytes) {
        if (field == 1) return ReadRepeated(type, value, bytes, modifiers);
        return field != 2 || type != 2 || ReadBinding(bytes, true, bindings);
    })) return false;
    keyboard = std::move(bindings);
    allowedModifiers = std::move(modifiers);
    for (const Binding& binding : keyboard)
        Log("[active-reset] loaded keyboard action=%u key=%u", binding.action, binding.button);
    return true;
}

bool ReadDevices(Wire file)
{
    std::vector<Device> configured;
    if (!file.Read([&](unsigned field, unsigned type, uint64_t, Wire entry) {
        if (field != 1 || type != 2) return true;
        Device device;
        const bool valid = entry.Read([&](unsigned id, unsigned kind, uint64_t value, Wire bytes) {
            if (id == 1 && kind == 2) {
                return bytes.Read([&](unsigned item, unsigned wireType, uint64_t flag, Wire str) {
                    if (item == 2 && wireType == 2) device.instance.assign(str.begin, str.end);
                    if (item == 4 && wireType == 0) device.xinput = flag != 0;
                    return true;
                });
            }
            if (id == 2 && kind == 2) return ReadBinding(bytes, false, device.bindings);
            if (id == 4) return ReadRepeated(kind, value, bytes, device.shifts);
            return true;
        });
        if (valid && !device.instance.empty() && !device.bindings.empty()) configured.push_back(std::move(device));
        return valid;
    })) return false;
    for (Device& device : devices) {
        if (device.joystick) device.joystick->Release();
    }
    devices = std::move(configured);
    for (const Device& device : devices) {
        for (const Binding& binding : device.bindings)
            Log("[active-reset] loaded %s action=%u button=%u", device.xinput ? "XInput" : "DirectInput",
                binding.action, binding.button);
    }
    return true;
}

bool KeyDown(unsigned key)
{
    return key > 0 && key <= 255 && (GetAsyncKeyState(int(key)) & 0x8000);
}

bool KeyboardDown(const Binding& binding)
{
    if (!KeyDown(binding.button)) return false;
    for (unsigned required : binding.modifiers) {
        if (!KeyDown(required)) return false;
    }
    for (unsigned modifier : allowedModifiers) {
        const auto& required = binding.modifiers;
        if (modifier != binding.button && KeyDown(modifier) &&
            std::find(required.begin(), required.end(), modifier) == required.end()) return false;
    }
    return true;
}

bool Connect(Device& device, HWND window, ULONGLONG now)
{
    if (now < device.nextConnect) return false;
    device.nextConnect = now + 2000;
    GUID id;
    if (FAILED(CLSIDFromString(device.instance.c_str(), &id))) return false;
    if (!directInput && FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
        IID_IDirectInput8W, reinterpret_cast<void**>(&directInput), nullptr))) return false;
    IDirectInputDevice8W* joystick = nullptr;
    if (FAILED(directInput->CreateDevice(id, &joystick, nullptr))) return false;
    if (FAILED(joystick->SetDataFormat(&c_dfDIJoystick2)) ||
        FAILED(joystick->SetCooperativeLevel(window, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND))) {
        joystick->Release();
        return false;
    }
    device.joystick = joystick;
    return true;
}

bool DirectButton(const DIJOYSTATE2& state, unsigned button)
{
    if (button < sizeof state.rgbButtons) return (state.rgbButtons[button] & 0x80) != 0;
    const unsigned hat = button / 100000;
    return hat >= 1 && hat <= 4 && state.rgdwPOV[hat - 1] == button % 100000;
}

bool XboxButton(const XINPUT_STATE& state, unsigned button)
{
    constexpr WORD masks[] = {XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN,
        XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT, XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_BACK,
        XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB, XINPUT_GAMEPAD_LEFT_SHOULDER,
        XINPUT_GAMEPAD_RIGHT_SHOULDER, XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y};
    return button < sizeof masks / sizeof masks[0] && (state.Gamepad.wButtons & masks[button]);
}

bool Trigger(Binding& binding, bool down, bool reset, ULONGLONG now)
{
    if (reset || !down) {
        binding.armed = !down;
        binding.pressedAt = 0;
        return false;
    }
    if (!binding.armed) return false;
    if (!binding.pressedAt) binding.pressedAt = now;
    if (now - binding.pressedAt < binding.holdMs) return false;
    binding.armed = false;
    return true;
}

unsigned PollDevice(Device& device, HWND window, bool reset, ULONGLONG now)
{
    DIJOYSTATE2 direct = {};
    XINPUT_STATE xbox = {};
    bool connected = false;
    if (device.xinput) {
        if (device.instance.size() == 1 && device.instance[0] >= L'0' && device.instance[0] <= L'3')
            connected = XInputGetState(DWORD(device.instance[0] - L'0'), &xbox) == ERROR_SUCCESS;
    } else if (device.joystick || Connect(device, window, now)) {
        device.joystick->Poll();
        HRESULT status = device.joystick->GetDeviceState(sizeof direct, &direct);
        if (status == DIERR_INPUTLOST || status == DIERR_NOTACQUIRED) {
            if (SUCCEEDED(device.joystick->Acquire())) {
                device.joystick->Poll();
                status = device.joystick->GetDeviceState(sizeof direct, &direct);
            }
        }
        connected = SUCCEEDED(status);
        if (!connected && status != DIERR_NOTACQUIRED && status != DIERR_INPUTLOST) {
            device.joystick->Release();
            device.joystick = nullptr;
        }
    }
    const auto down = [&](unsigned button) {
        return connected && (device.xinput ? XboxButton(xbox, button) : DirectButton(direct, button));
    };
    const bool shifted = std::any_of(device.shifts.begin(), device.shifts.end(), down);
    unsigned action = 0;
    for (Binding& binding : device.bindings) {
        if (!connected) {
            binding.armed = false;
            binding.pressedAt = 0;
        } else if (Trigger(binding, down(binding.button) && binding.requireShift == shifted, reset, now)) {
            action = binding.action;
        }
    }
    return action;
}
}

unsigned PollActiveResetInput(HWND foreground, bool reset)
{
    static HWND lastForeground = nullptr;
    reset = reset || foreground != lastForeground;
    lastForeground = foreground;
    if (!foreground) return 0;

    if (!initialized) {
        initialized = true;
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, KF_FLAG_DONT_VERIFY, nullptr, &path)))
            configDir = std::wstring(path) + L"\\ACE\\";
        CoTaskMemFree(path);
        if (configDir.empty()) Log("[active-reset] could not locate the Saved Games folder");
    }
    if (configDir.empty()) return 0;

    const auto now = GetTickCount64();
    static ULONGLONG nextRefresh = 0;
    static ConfigFile keyboardFile;
    static ConfigFile devicesFile;
    if (now >= nextRefresh) {
        nextRefresh = now + 500;
        RefreshConfig(L"input_keyboard.keyboardinputconfiguration", keyboardFile, ReadKeyboard);
        RefreshConfig(L"input_devices.inputdeviceconfiguration", devicesFile, ReadDevices);
    }
    unsigned action = 0;
    for (Binding& binding : keyboard) {
        if (Trigger(binding, KeyboardDown(binding), reset, now)) action = binding.action;
    }
    for (Device& device : devices) {
        const unsigned deviceAction = PollDevice(device, foreground, reset, now);
        if (deviceAction) action = deviceAction;
    }
    return action;
}
