#include "acevo/ui/active_reset_ui.h"
#include "acevo/engine/active_reset_session.h"
#include "acevo/ui/cohtml_hooks.h"
#include "acevo/core/log.h"
#include "snapshot_icons.h"

namespace {
std::atomic<uint64_t> snapshotEpoch{0};
void* mainView = nullptr;

const char pageScript[] = R"js(
(function () {
    'use strict';
    if (window.__acevoActiveReset) return;
    var active = false;
    var available = false;
    var indicator;
    var inactivePng = ')js" ACEVO_SNAPSHOT_INACTIVE_URL R"js(';
    var activePng = ')js" ACEVO_SNAPSHOT_ACTIVE_URL R"js(';
    function paint(icon) {
        var season = window.SeasonInfo;
        var multiplayer = document.body.classList.contains('multiplayer') ||
            (season && season.mode === 'ClientCommandsMode_MULTIPLAYER');
        icon.style.display = available && !multiplayer ? 'block' : 'none';
        var label = active ? 'Snapshot saved' : 'No snapshot';
        var source = active ? activePng : inactivePng;
        if (icon.getAttribute('src') === source) return;
        icon.setAttribute('src', source);
        icon.setAttribute('alt', label);
        icon.setAttribute('title', label);
    }
    window.__acevoActiveReset = function (allowed, value) {
        var changed = active !== value;
        available = allowed;
        active = value;
        if (!indicator) return;
        paint(indicator);
        if (changed) console.log('[ACEvoPerf] snapshot indicator ' + (active ? 'active' : 'inactive'));
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
                var icon = document.createElement('img');
                icon.className = 'acevo-snapshot';
                icon.style.cssText = 'display:none;position:absolute;left:2.1rem;bottom:0.33rem;width:3.6rem;height:2.03rem;pointer-events:none';
                icon.addEventListener('error', function () {
                    console.error('[ACEvoPerf] snapshot indicator PNG failed to load');
                });
                indicator = icon;
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
    const uint64_t epoch = ActiveResetSessionEpoch();
    const char* script;
    if (!(epoch & 1))
        script = "window.__acevoActiveReset && window.__acevoActiveReset(false, false);";
    else if (snapshotEpoch.load(std::memory_order_acquire) == epoch)
        script = "window.__acevoActiveReset && window.__acevoActiveReset(true, true);";
    else
        script = "window.__acevoActiveReset && window.__acevoActiveReset(true, false);";
    reinterpret_cast<ScriptFn>((*static_cast<void***>(mainView))[60])(mainView, script);
}
}

void SetActiveResetSnapshot(uint64_t sessionEpoch)
{
    snapshotEpoch.store(sessionEpoch, std::memory_order_release);
}

void InstallActiveResetUi()
{
    AddCohtmlViewListener(&OnView);
    AddUiFrameEndListener(&OnFrameEnd);
}
