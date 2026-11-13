#include "acevo/ui/active_reset_ui.h"
#include "acevo/engine/active_reset_session.h"
#include "acevo/ui/cohtml_hooks.h"
#include "acevo/core/log.h"

namespace {
std::atomic<uint64_t> pendingNotice{0};
std::atomic<uint64_t> timerState{0};
std::atomic<uint64_t> bestState{0};
std::atomic<uint64_t> deltaState{0};
std::atomic<uint64_t> finishedState{0};
void* mainView = nullptr;

const char pageScript[] = R"js(
(function () {
    'use strict';
    if (window.__acevoActiveResetNotify) return;
    window.__acevoActiveResetTimer = function (elapsed, best, delta, finished) {
        var timer = document.querySelector('acevo-snapshot');
        var hud = document.querySelector('ks-hud#mainHUD');
        if (!timer && hud && customElements.get('acevo-snapshot') && elapsed >= 0) {
            timer = document.createElement('acevo-snapshot');
            timer.id = 'acevo-reset';
            timer.style.cssText = 'position:absolute;left:2rem;top:12rem;width:10rem;color:white;filter:drop-shadow(.1rem .05rem .1rem #000);';
            var body = hud.querySelector('.component-body');
            (body || hud).appendChild(timer);
        }
        if (!timer) return;
        var season = window.SeasonInfo;
        var hidden = elapsed < 0 || document.body.classList.contains('multiplayer') ||
            (season && season.mode === 'ClientCommandsMode_MULTIPLAYER');
        timer.style.display = hidden ? 'none' : 'block';
        if (hidden) return;
        if (!timer.querySelector('.acevo-reset-value')) return;
        function format(value) {
            return String(Math.floor(value / 60000)).padStart(2, '0') + ':' +
                String(Math.floor(value / 1000) % 60).padStart(2, '0') + '.' + String(value % 1000).padStart(3, '0');
        }
        timer.querySelector('.acevo-reset-value').textContent = format(elapsed);
        timer.querySelector('.acevo-reset-best').textContent = best >= 0 ? 'BEST ' + format(best) : '';
        var label = timer.querySelector('.acevo-reset-delta');
        var track = timer.querySelector('.acevo-delta-track');
        var bar = timer.querySelector('.acevo-delta-bar');
        track.style.display = delta === null || finished ? 'none' : 'block';
        label.textContent = delta === null ? '' : (delta > 0 ? '+' : delta < 0 ? '-' : '') + (Math.abs(delta) / 1000).toFixed(3);
        label.style.color = delta === null || delta === 0 ? 'white' : delta < 0 ? '#44EA78' : '#FF1418';
        var width = delta === null ? 0 : Math.min(Math.abs(delta) / 1000, 1) * 50;
        bar.style.width = width + '%';
        bar.style.left = (delta < 0 ? 50 : 50 - width) + '%';
        bar.style.backgroundColor = delta < 0 ? '#44EA78' : '#FF1418';
    };
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
        {number: 552, action: 'InputAction_AIDev_LoadSnapshot', name: 'AIDev LoadSnapshot', label: 'Active reset: Restore snapshot'},
        {number: 553, action: 'InputAction_AIDev_StartRecording', name: 'AIDev StartRecording', label: 'Active reset: Set finish gate'}
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
        if (name === 'ks-laptime' && !customElements.get('acevo-snapshot')) {
            var HudElement = Object.getPrototypeOf(elementClass);
            class SnapshotHud extends HudElement {
                constructor() {
                    super();
                    this.template = '<div class="component-body" style="display:flex;flex-direction:column;pointer-events:none;font-family:rajdhani-mono,sans-serif;font-size:1rem">' +
                        '<span>SNAPSHOT</span><span class="acevo-reset-value" style="font-size:1.4rem;font-weight:600"></span><span class="acevo-reset-best"></span>' +
                        '<span class="acevo-reset-delta"></span><div class="acevo-delta-track" style="position:relative;width:8rem;height:.4rem;background:rgba(255,255,255,.2)">' +
                        '<div style="position:absolute;left:50%;height:100%;width:1px;background:white"></div>' +
                        '<div class="acevo-delta-bar" style="position:absolute;height:100%"></div></div></div>';
                }
            }
            customElements.define('acevo-snapshot', SnapshotHud);
        }
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
    var laptime = customElements.get('ks-laptime');
    if (laptime) patch('ks-laptime', laptime);
})();
)js";

void OnView(void* view, int, unsigned, unsigned, bool isMainView)
{
    if (!isMainView) return;
    mainView = view;
    using ScriptFn = void (*)(void*, const char*);
    reinterpret_cast<ScriptFn>((*static_cast<void***>(view))[cohtml_slot::kViewAddInitialScript])(view, pageScript);
    Log("[active-reset] controls and notification script installed");
}

void OnFrameEnd(bool after)
{
    if (!after || !mainView) return;
    static ULONGLONG lastTimerUpdate = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - lastTimerUpdate >= 100) {
        lastTimerUpdate = now;
        const uint64_t state = timerState.load(std::memory_order_acquire);
        const uint64_t epoch = ActiveResetSessionEpoch();
        const int64_t elapsed = (epoch & 1) && (state >> 32) == epoch
            ? static_cast<int64_t>(static_cast<uint32_t>(state)) : -1;
        const uint64_t best = bestState.load(std::memory_order_acquire);
        const int64_t bestMs = (epoch & 1) && (best >> 32) == epoch
            ? static_cast<int64_t>(static_cast<uint32_t>(best)) : -1;
        const uint64_t delta = deltaState.load(std::memory_order_acquire);
        const uint64_t finished = finishedState.load(std::memory_order_acquire);
        char deltaText[32] = "null";
        if ((epoch & 1) && (delta >> 32) == epoch)
            sprintf_s(deltaText, "%d", static_cast<int32_t>(static_cast<uint32_t>(delta)));
        char script[192];
        sprintf_s(script, "window.__acevoActiveResetTimer && window.__acevoActiveResetTimer(%lld,%lld,%s,%s);", elapsed, bestMs, deltaText,
            (finished >> 1) == epoch && (finished & 1) ? "true" : "false");
        using ScriptFn = void (*)(void*, const char*);
        reinterpret_cast<ScriptFn>((*static_cast<void***>(mainView))[cohtml_slot::kViewExecuteScript])(mainView, script);
    }
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
        case ActiveResetNotice::GateSaved: message = "Finish gate set"; break;
        case ActiveResetNotice::GateFailed: message = "Save a snapshot and drive forward to set a gate"; break;
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

void SetActiveResetTimer(uint64_t sessionEpoch, uint32_t elapsedMs)
{
    timerState.store((sessionEpoch << 32) | elapsedMs, std::memory_order_release);
}

void SetActiveResetBest(uint64_t sessionEpoch, uint32_t elapsedMs)
{
    bestState.store((sessionEpoch << 32) | elapsedMs, std::memory_order_release);
}

void SetActiveResetDelta(uint64_t sessionEpoch, int32_t deltaMs)
{
    deltaState.store((sessionEpoch << 32) | static_cast<uint32_t>(deltaMs), std::memory_order_release);
}

void SetActiveResetFinished(uint64_t sessionEpoch, bool finished)
{
    finishedState.store((sessionEpoch << 1) | (finished ? 1 : 0), std::memory_order_release);
}

void InstallActiveResetUi()
{
    AddCohtmlViewListener(&OnView);
    AddUiFrameEndListener(&OnFrameEnd);
}
