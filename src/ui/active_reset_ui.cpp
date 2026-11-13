#include "acevo/ui/active_reset_ui.h"
#include "acevo/engine/active_reset_session.h"
#include "acevo/engine/active_reset_probe.h"
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
    window.__acevoGateMarker = function (visible, x, y) {
        var marker = document.getElementById('acevo-gate-marker');
        var season = window.SeasonInfo;
        visible = visible && !document.body.classList.contains('multiplayer') &&
            !(season && season.mode === 'ClientCommandsMode_MULTIPLAYER') && !!document.querySelector('ks-hud#mainHUD');
        if (!marker && visible) {
            marker = document.createElement('div');
            marker.id = 'acevo-gate-marker';
            marker.style.cssText = 'position:fixed;transform:translate(-50%,-100%);pointer-events:none;z-index:999;color:#9dffb3;text-align:center;font:700 1.2rem rajdhani-mono,sans-serif;text-shadow:0 1px 3px black';
            marker.innerHTML = '<div>GATE</div><div style="width:0;height:0;margin:auto;border-left:.45rem solid transparent;border-right:.45rem solid transparent;border-top:.65rem solid #9dffb3"></div>';
            document.body.appendChild(marker);
        }
        if (!marker) return;
        marker.style.display = visible ? 'block' : 'none';
        if (visible) {
            marker.style.left = (x * 100) + 'vw';
            marker.style.top = (y * 100) + 'vh';
        }
    };
    var countdown = null;
    function clearCountdown() {
        if (!countdown) return;
        clearInterval(countdown.interval);
        clearTimeout(countdown.fadeTimer);
        cancelAnimationFrame(countdown.fadeFrame);
        if (countdown.fade) countdown.fade.remove();
        if (countdown.element) countdown.element.remove();
        countdown = null;
    }
    function resumeWithoutHudReload(hud) {
        var ui = window.ksUI;
        var goTo = ui.goTo;
        var timeout;
        function restoreGoTo() {
            clearTimeout(timeout);
            if (ui.goTo === intercept) ui.goTo = goTo;
        }
        function intercept(page) {
            if (page === 'hud.html' && hud.isConnected && document.querySelector('ks-hud#mainHUD') === hud) {
                restoreGoTo();
                return;
            }
            return goTo.apply(this, arguments);
        }
        ui.goTo = intercept;
        timeout = setTimeout(restoreGoTo, 1500);
        ui.request('GameMode', 'Resume');
    }
    function pauseForRestore() {
        var hud = document.querySelector('ks-hud#mainHUD');
        if (!hud || !hud.GameModeClient || countdown) return;
        var pending = {hud: hud};
        countdown = pending;
        hud.GameModeClient.request('Pause', function (response) {
            if (countdown !== pending) return;
            if (!response || response.response !== 'GameModePauseOK' || !hud.isConnected ||
                document.body.classList.contains('multiplayer')) {
                clearCountdown();
                return;
            }
            var fade = document.createElement('div');
            fade.style.cssText = 'position:fixed;top:0;left:0;width:100vw;height:100vh;background-color:#000;opacity:1;transition:opacity 500ms linear;pointer-events:none;z-index:10000';
            document.body.appendChild(fade);
            pending.fade = fade;
            var element = document.createElement('div');
            element.style.cssText = 'position:fixed;top:38%;left:0;width:100%;text-align:center;color:white;font:700 8rem rajdhani-mono,sans-serif;text-shadow:0 2px 4px black;pointer-events:none;z-index:10001';
            document.body.appendChild(element);
            pending.element = element;
            var end = Date.now() + 3000;
            element.textContent = '3';
            pending.interval = setInterval(function () {
                if (document.querySelector('ks-hud#mainHUD') !== hud || document.body.classList.contains('multiplayer')) {
                    clearCountdown();
                    return;
                }
                var remaining = end - Date.now();
                if (remaining <= 0) {
                    clearCountdown();
                    resumeWithoutHudReload(hud);
                } else {
                    element.textContent = String(Math.ceil(remaining / 1000));
                }
            }, 50);
            pending.fadeFrame = requestAnimationFrame(function () {
                pending.fadeFrame = requestAnimationFrame(function () {
                    if (countdown !== pending) return;
                    fade.style.opacity = '0';
                    pending.fadeTimer = setTimeout(function () {
                        if (countdown !== pending) return;
                        fade.remove();
                        pending.fade = null;
                    }, 500);
                });
            });
        });
    }
    window.__acevoActiveResetTimer = function (elapsed, best, delta, finished, improved) {
        if (elapsed < 0) clearCountdown();
        var timer = document.querySelector('acevo-snapshot');
        var hud = document.querySelector('ks-hud#mainHUD');
        if (!timer && hud && customElements.get('acevo-snapshot') && elapsed >= 0) {
            timer = document.createElement('acevo-snapshot');
            timer.id = 'acevo-reset';
            timer.style.cssText = 'position:absolute;left:2rem;top:12rem;width:16rem;color:white;filter:drop-shadow(.1rem .05rem .1rem #000);';
            var body = hud.querySelector('.component-body');
            (body || hud).appendChild(timer);
        }
        if (!timer) return;
        var season = window.SeasonInfo;
        var hidden = elapsed < 0 || document.body.classList.contains('multiplayer') ||
            (season && season.mode === 'ClientCommandsMode_MULTIPLAYER');
        timer.style.display = hidden ? 'none' : 'block';
        if (hidden) {
            clearInterval(timer.__acevoFlash);
            timer.__acevoFlash = null;
            timer.__acevoWasFinished = false;
            var hiddenBest = timer.querySelector('.acevo-reset-best');
            if (hiddenBest) hiddenBest.style.opacity = '';
            return;
        }
        if (!timer.querySelector('.acevo-reset-value')) return;
        function format(value) {
            return String(Math.floor(value / 60000)).padStart(2, '0') + ':' +
                String(Math.floor(value / 1000) % 60).padStart(2, '0') + '.' + String(value % 1000).padStart(3, '0');
        }
        timer.querySelector('.acevo-reset-value').textContent = format(elapsed);
        var bestLabel = timer.querySelector('.acevo-reset-best');
        bestLabel.textContent = best >= 0 ? 'BEST ' + format(best) : '';
        if (finished && !timer.__acevoWasFinished && improved && best >= 0) {
            clearInterval(timer.__acevoFlash);
            var flashes = 0;
            bestLabel.style.opacity = '0';
            timer.__acevoFlash = setInterval(function () {
                flashes++;
                bestLabel.style.opacity = flashes % 2 ? '1' : '0';
                if (flashes === 6) {
                    clearInterval(timer.__acevoFlash);
                    timer.__acevoFlash = null;
                    bestLabel.style.opacity = '';
                }
            }, 500);
        } else if (!finished && timer.__acevoWasFinished) {
            clearInterval(timer.__acevoFlash);
            timer.__acevoFlash = null;
            bestLabel.style.opacity = '';
        }
        timer.__acevoWasFinished = finished;
        var label = timer.querySelector('.acevo-reset-delta');
        var track = timer.querySelector('.acevo-delta-track');
        var bar = timer.querySelector('.acevo-delta-bar');
        track.style.display = delta === null || finished ? 'none' : 'block';
        label.textContent = delta === null || finished ? '' : (delta > 0 ? '+' : delta < 0 ? '-' : '') + (Math.abs(delta) / 1000).toFixed(3);
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
        if (message === 'Snapshot restored') pauseForRestore();
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
                        '<span class="acevo-reset-delta"></span><div class="acevo-delta-track" style="position:relative;width:15rem;height:.4rem;background:rgba(255,255,255,.2)">' +
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
        const bool sameSession = (finished >> 2) == epoch;
        sprintf_s(script, "window.__acevoActiveResetTimer && window.__acevoActiveResetTimer(%lld,%lld,%s,%s,%s);", elapsed, bestMs, deltaText,
            sameSession && (finished & 1) ? "true" : "false", sameSession && (finished & 2) ? "true" : "false");
        using ScriptFn = void (*)(void*, const char*);
        reinterpret_cast<ScriptFn>((*static_cast<void***>(mainView))[cohtml_slot::kViewExecuteScript])(mainView, script);
        float markerX = 0, markerY = 0;
        const bool markerVisible = (epoch & 1) && ReadActiveResetMarker(epoch, markerX, markerY);
        char markerScript[192];
        sprintf_s(markerScript, "window.__acevoGateMarker && window.__acevoGateMarker(%s,%.6f,%.6f);",
            markerVisible ? "true" : "false", markerX, markerY);
        reinterpret_cast<ScriptFn>((*static_cast<void***>(mainView))[cohtml_slot::kViewExecuteScript])(mainView, markerScript);
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

void SetActiveResetFinished(uint64_t sessionEpoch, bool finished, bool improved)
{
    finishedState.store((sessionEpoch << 2) | (finished ? 1 : 0) | (improved ? 2 : 0), std::memory_order_release);
}

void InstallActiveResetUi()
{
    AddCohtmlViewListener(&OnView);
    AddUiFrameEndListener(&OnFrameEnd);
}
