#include "acevo/ui/active_reset_ui.h"
#include "acevo/engine/active_reset_session.h"
#include "acevo/ui/cohtml_hooks.h"
#include "acevo/core/log.h"

namespace {
std::atomic<uint64_t> pendingNotice{0};
void* mainView = nullptr;

const char pageScript[] = R"js(
(function () {
    'use strict';
    if (window.__acevoActiveResetNotify) return;
    window.__acevoActiveResetNotify = function (message) {
        var panel = document.querySelector('ks-notificationpanel');
        if (!panel || !panel.listNotifications || typeof engine === 'undefined') return;
        var season = window.SeasonInfo;
        if (document.body.classList.contains('multiplayer') ||
            (season && season.mode === 'ClientCommandsMode_MULTIPLAYER')) return;
        engine.trigger('UINotification', {
            type: 'UIInternalNotification_Simple',
            priority: 'UINotificationPriorityType_Normal',
            extra: 'Active reset',
            value: message,
            time_to_hide: 2200
        });
        console.log('[ACEvoPerf] active reset notification: ' + message);
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
    }
    var define = customElements.define;
    customElements.define = function (name, elementClass, options) {
        patch(name, elementClass);
        return define.call(customElements, name, elementClass, options);
    };
    var controls = customElements.get('ks-page-settings-controls');
    if (controls) patch('ks-page-settings-controls', controls);
})();
)js";

void OnView(void* view, int number, unsigned, unsigned)
{
    if (number != 1) return;
    mainView = view;
    using ScriptFn = void (*)(void*, const char*);
    reinterpret_cast<ScriptFn>((*static_cast<void***>(view))[cohtml_slot::kViewAddInitialScript])(view, pageScript);
    Log("[active-reset] controls and notification script installed");
}

void OnFrameEnd(bool after)
{
    if (!after || !mainView) return;
    if (!pendingNotice.load(std::memory_order_relaxed)) return;
    const uint64_t pending = pendingNotice.exchange(0, std::memory_order_acquire);
    const uint64_t epoch = ActiveResetSessionEpoch();
    if (!(epoch & 1) || (pending >> 3) != epoch) return;

    const char* message = nullptr;
    switch (static_cast<ActiveResetNotice>(pending & 7)) {
        case ActiveResetNotice::Saved: message = "Snapshot saved"; break;
        case ActiveResetNotice::Restored: message = "Snapshot restored"; break;
        case ActiveResetNotice::NoSnapshot: message = "No snapshot saved"; break;
        case ActiveResetNotice::SaveFailed: message = "Could not save snapshot"; break;
        case ActiveResetNotice::RestoreFailed: message = "Could not restore snapshot"; break;
    }
    if (!message) return;

    char script[160];
    sprintf_s(script, "window.__acevoActiveResetNotify && window.__acevoActiveResetNotify('%s');", message);
    using ScriptFn = void (*)(void*, const char*);
    reinterpret_cast<ScriptFn>((*static_cast<void***>(mainView))[cohtml_slot::kViewExecuteScript])(mainView, script);
}
}

void NotifyActiveReset(uint64_t sessionEpoch, ActiveResetNotice notice)
{
    pendingNotice.store((sessionEpoch << 3) | static_cast<uint64_t>(notice), std::memory_order_release);
}

void InstallActiveResetUi()
{
    AddCohtmlViewListener(&OnView);
    AddUiFrameEndListener(&OnFrameEnd);
}
