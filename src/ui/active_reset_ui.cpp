#include "acevo/ui/active_reset_ui.h"
#include "acevo/ui/cohtml_hooks.h"
#include "acevo/core/code_patch.h"
#include "acevo/core/log.h"

namespace {
using InputFn = void (*)(void*, void*, unsigned, bool, bool*);
InputFn originalInput = nullptr;
std::atomic<uint64_t> pending{0};
std::atomic<bool> snapshotActive{false};
void* mainView = nullptr;

void OnInput(void* state, void* devices, unsigned action, bool switchDevice, bool* switched)
{
    originalInput(state, devices, action, switchDevice, switched);
    if (action != 551 && action != 552) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) return;
    pending.store((GetTickCount64() << 16) | action, std::memory_order_release);
    Log("[active-reset] native action=%u", action);
}

const char pageScript[] = R"js(
(function () {
    'use strict';
    if (window.__acevoActiveReset) return;
    var active = false;
    function paint(icon) {
        icon.style.color = active ? '#56d984' : '#888888';
        icon.setAttribute('aria-label', active ? 'Snapshot saved' : 'No snapshot');
        icon.setAttribute('title', active ? 'Snapshot saved' : 'No snapshot');
    }
    window.__acevoActiveReset = function (value) {
        active = value;
        document.querySelectorAll('.acevo-snapshot').forEach(paint);
    };
    var definitions = [
        {number: 551, action: 'InputAction_AIDev_SaveSnapshot', name: 'AIDev SaveSnapshot', label: 'Active reset: Save snapshot'},
        {number: 552, action: 'InputAction_AIDev_LoadSnapshot', name: 'AIDev LoadSnapshot', label: 'Active reset: Restore snapshot'}
    ];
    function matches(action, definition) {
        return action && (action.input === definition.action || action.input === definition.number || action.number === definition.number);
    }
    function addRows(data) {
        if (!data.wrapper || !data.wrapper.action_items) return;
        definitions.forEach(function (definition) {
            var row = data.wrapper.action_items.find(function (item) { return item.number === definition.number && !item.payload; });
            if (!row) {
                row = {__Type: 'InputWrapper_ActionItem', number: definition.number, action: definition.action,
                    name: definition.name, payload: 0, is_set: false, is_mandatory: false,
                    is_forced_const: false, is_forced_repeated: false, is_forced_on_release: false,
                    keyboard: {__Type: 'InputWrapper_ActionItem_Keyboard', is_set: false, button_code: 0, required_modifiers: [], trigger_time_ms: 0, repeat_time_ms: 0},
                    x_input: {__Type: 'InputWrapper_ActionItem_XInput', is_set: false, instance_guid: '', product_guid: '', product_name: '', button_index: 0, require_shift: false},
                    direct_input: {__Type: 'InputWrapper_ActionItem_DirectInput', is_set: false, instance_guid: '', product_guid: '', product_name: '', button_index: 0, require_shift: false}};
                var settings = data.settings || {};
                ((settings.keyboard || {}).keyboard_commands || []).forEach(function (command) {
                    if (!matches(command.action, definition)) return;
                    Object.assign(row.keyboard, {is_set: true, button_code: command.key,
                        required_modifiers: command.required_modifiers || [], trigger_time_ms: command.action.trigger_time_ms || 0,
                        repeat_time_ms: command.action.repeat_time_ms || 0});
                    row.is_set = true;
                });
                ((settings.input_devices || {}).input_controllers || []).forEach(function (device) {
                    (device.commands || []).forEach(function (command) {
                        if (!matches(command.action, definition)) return;
                        var controller = device.controller || {};
                        var target = controller.xinput ? row.x_input : row.direct_input;
                        Object.assign(target, controller, {is_set: true, button_index: command.index,
                            require_shift: !!command.require_shift, trigger_time_ms: command.action.trigger_time_ms || 0,
                            repeat_time_ms: command.action.repeat_time_ms || 0});
                        row.is_set = true;
                    });
                });
                data.wrapper.action_items.push(row);
            }
            row.group = 'Car';
        });
    }
    function patch(name, elementClass) {
        var proto = elementClass.prototype;
        if (name === 'ks-page-settings-controls' && !proto.__acevoResetBindings) {
            proto.__acevoResetBindings = true;
            var refresh = proto.onDevicesChanged;
            proto.onDevicesChanged = function (data) { addRows(data); return refresh.apply(this, arguments); };
            var metadata = proto.setBindingMetadata;
            proto.setBindingMetadata = function (binding) {
                metadata.apply(this, arguments);
                definitions.forEach(function (definition) {
                    if (binding.number === definition.number) binding.__uimetadata.name = definition.label;
                });
            };
            console.log('[ACEvoPerf] active reset controls registered');
        }
        if (name === 'ks-sessionbanner' && !proto.__acevoResetIndicator) {
            proto.__acevoResetIndicator = true;
            var loaded = proto.onTemplateLoaded;
            proto.onTemplateLoaded = function () {
                loaded.apply(this, arguments);
                if (!this.closest('ks-hud') || this.querySelector('.acevo-snapshot')) return;
                var container = this.querySelector('.session-container');
                if (!container) return;
                var icon = document.createElement('span');
                icon.className = 'acevo-snapshot';
                icon.setAttribute('role', 'img');
                icon.style.cssText = 'position:absolute;left:1rem;bottom:0.4rem;width:4rem;height:1.5rem;display:flex;align-items:center;pointer-events:none';
                var car = 'M2 15 L3 10 L9 9 L13 4 L25 4 L31 10 L37 12 L38 18 L33 18 A4 4 0 0 1 25 18 L14 18 A4 4 0 0 1 6 18 L2 18 Z';
                icon.innerHTML = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 84 26" width="100%" height="100%"><path d="' + car + '" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="1 4" stroke-linecap="round"/><path transform="translate(44 0)" d="' + car + '" fill="currentColor"/></svg>';
                container.insertBefore(icon, container.firstChild);
                paint(icon);
                console.log('[ACEvoPerf] snapshot indicator attached');
            };
        }
    }
    var define = customElements.define;
    customElements.define = function (name, elementClass, options) {
        patch(name, elementClass);
        return define.call(customElements, name, elementClass, options);
    };
    ['ks-page-settings-controls', 'ks-sessionbanner'].forEach(function (name) {
        var elementClass = customElements.get(name);
        if (elementClass) patch(name, elementClass);
    });
})();
)js";

void OnView(void* view, int number, unsigned, unsigned)
{
    if (number != 1) return;
    mainView = view;
    using ScriptFn = void (*)(void*, const char*);
    reinterpret_cast<ScriptFn>((*static_cast<void***>(view))[cohtml_slot::kViewAddInitialScript])(view, pageScript);
    Log("[active-reset] controls and snapshot indicator script installed");
}

void OnFrameEnd(bool after)
{
    if (!after || !mainView) return;
    static ULONGLONG lastUpdate = 0;
    const auto now = GetTickCount64();
    if (now - lastUpdate < 100) return;
    lastUpdate = now;
    using ScriptFn = void (*)(void*, const char*);
    const char* script = snapshotActive.load(std::memory_order_acquire)
        ? "window.__acevoActiveReset && window.__acevoActiveReset(true);"
        : "window.__acevoActiveReset && window.__acevoActiveReset(false);";
    reinterpret_cast<ScriptFn>((*static_cast<void***>(mainView))[60])(mainView, script);
}
}

unsigned TakeActiveResetAction()
{
    const auto value = pending.exchange(0, std::memory_order_acq_rel);
    return value && GetTickCount64() - (value >> 16) < 500 ? unsigned(value & 0xffff) : 0;
}

void SetActiveResetSnapshot(bool active)
{
    snapshotActive.store(active, std::memory_order_release);
}

bool InstallActiveResetUi(BYTE* exe)
{
    BYTE* entry = exe + 0x20e8890;
    if (Fnv1a64(entry, 0x32) != 0x29273598846b8ddbull) return false;
    BYTE* relay = AllocNear(entry, 64);
    if (!relay) return false;
    BYTE jump[14] = {0xff, 0x25, 0, 0, 0, 0};
    auto target = reinterpret_cast<uintptr_t>(&OnInput);
    memcpy(jump + 6, &target, sizeof target);
    memcpy(relay, jump, sizeof jump);
    memcpy(relay + 16, entry, 5);
    target = reinterpret_cast<uintptr_t>(entry + 5);
    memcpy(jump + 6, &target, sizeof target);
    memcpy(relay + 21, jump, sizeof jump);
    BYTE patch[5];
    DWORD old = 0;
    if (!EncodeRel32(0xe9, entry, relay, patch) || !VirtualProtect(relay, 64, PAGE_EXECUTE_READ, &old)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, 64);
    originalInput = reinterpret_cast<InputFn>(relay + 16);
    if (!WriteCode(entry, patch, sizeof patch)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    AddCohtmlViewListener(&OnView);
    AddUiFrameEndListener(&OnFrameEnd);
    Log("[active-reset] native input callback installed");
    return true;
}
