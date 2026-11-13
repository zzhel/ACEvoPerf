#include "acevo/ui/active_reset_ui.h"
#include "acevo/engine/active_reset_session.h"
#include "acevo/ui/cohtml_hooks.h"
#include "acevo/core/log.h"

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
    // Transparent PNGs with the same R8-style outline mask, one for each state.
    var inactivePng = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAJAAAABBBAMAAADMCjABAAAAMFBMVEXT19vT19vT19vT19vT19vT19vT19vT19vT19vT19vT19vT19vT19vT19vT19vT19smuEYJAAAAEHRSTlMAESIzRFVmd4iZqrvM3e7/dpUBFQAABChJREFUeNrtmM1rXGUUxn93MpM0bYVrsVQaA5EutJt0FEHQRcaPSrHUBEVX1k6hC12UpmJdFDVNly7SUMGFQhPoH9BqC1IoJC4U/KATQaogdJoqWGlhak0Tk0nuz8VkMpNJZjKT0Z3v6s57X5573vPxnOcM/L+aXnt/SQFbupqAaPn0JLTqDGzL/tEE0imjNJ0ahYzoxLpxYuokvWo6ltPptY5XfdMGbCUEiIeQgEfeadw9n6TYqM7QrVFXXJ1lQ87PGwUaMR/G1bts0FnI6F16dLZRg9Q0YzpM8H40CPuMTjCmUYOx26hOsO3a1yEEjwHBWychq6YaA9qsXoeg8r5GYWNArerwit1dOgMvNGRUVld6I5GJBtlrPlknyFOXung0d3aVN/FnIFt3jif0V9hS49L36gPq0YXqLm23WjItK5FtxyANsb6qQAvA/JpALT9/OEoIVLdo7jp8R+zZ2sXbrXMM1E67p80ngyFPV3XxlashI2ryIZ2r5cPdKTZoHoLjH63y+ogOk1HTQcbRNeLRrSbZt1rSBlm9wTk1ReLQWoHtVfvJ6F8rU02dpkfz9WRIt5qKq3MQhKsAxfWzeoDadZ6EOk9s6FZq5dXY+XFd1R075wWCnE7TuYzMd4QFZ9dPfIeAAZ1goLwOtnu1EP4GuebKzS4yZVkXjGm6RjepfsMQMmqRVtrUG+ttfgfKAr25fmZYuTbpb2XP6wfixS/Ccq6abEavxN4Ni90hSjUDNORPi96+dqEZnIRLneL+ppTY5kLvigG5poBiQBJiEHu8KaAFYByAM9GJZoBaip28Xf9uyqTXvVjMcpsKP88tiQx/Xz9K8ObBklrRFOyuFAe7k0DsZYCtKSB4tYJpgleAwy70LfVoPU9HZSvucBaCM56Gtmw+BW/44/ITHywS5Q+lWnOyIPSKnzp+lGBMh2nXOejVSeIaJSsExQJtlsh2v3q+VZ0qHnnJKNWmTi22sIzO0Kl+yZNvLwF1qul2S0GPqamCGF5cGR0vbBxQ00FO8+xS/9xuKel2qcOJogExiPZzc1zAYhkm4QkAuQPcKT3mjhEsU+138hNweennayEtZazUqt4rbNynCyGndIp2dTBXRq0b1S525irmnQMlVwY5nSxsJHQaOnUUxpxrUaPFU2Es5yywo0Jtxi+fpWVxb0D7iV8+C+z8JgnBexeBtu/7g+ySVjl8u+/Bb6tURGthKgtJXPmqSh4fKSqHtlrz0hE9T3D4dl+lVC8bmXLzfew5WlAk1XRykNUpNtUmgwdSdBilGVFPVNGQsS5I8Dy01hhdbo9ziGCwNmfmdJpzan/Ncs/pDL2V5FNmUTQKt0opWBUohBYuwex4tSMdLvTRs9YMFGT0HgxFNezecxDa15yDe3UcgofrGWpqd8RMM38CLOsb/xLOf7z+AXsbcdFXsbSXAAAAAElFTkSuQmCC';
    var activePng = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAJAAAABBBAMAAADMCjABAAAAMFBMVEWO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+Ks6mUMGAAAAEHRSTlMAESIzRFVmd4iZqrvM3e7/dpUBFQAABChJREFUeNrtmM1rXGUUxn93MpM0bYVrsVQaA5EutJt0FEHQRcaPSrHUBEVX1k6hC12UpmJdFDVNly7SUMGFQhPoH9BqC1IoJC4U/KATQaogdJoqWGlhak0Tk0nuz8VkMpNJZjKT0Z3v6s57X5573vPxnOcM/L+aXnt/SQFbupqAaPn0JLTqDGzL/tEE0imjNJ0ahYzoxLpxYuokvWo6ltPptY5XfdMGbCUEiIeQgEfeadw9n6TYqM7QrVFXXJ1lQ87PGwUaMR/G1bts0FnI6F16dLZRg9Q0YzpM8H40CPuMTjCmUYOx26hOsO3a1yEEjwHBWychq6YaA9qsXoeg8r5GYWNArerwit1dOgMvNGRUVld6I5GJBtlrPlknyFOXung0d3aVN/FnIFt3jif0V9hS49L36gPq0YXqLm23WjItK5FtxyANsb6qQAvA/JpALT9/OEoIVLdo7jp8R+zZ2sXbrXMM1E67p80ngyFPV3XxlashI2ryIZ2r5cPdKTZoHoLjH63y+ogOk1HTQcbRNeLRrSbZt1rSBlm9wTk1ReLQWoHtVfvJ6F8rU02dpkfz9WRIt5qKq3MQhKsAxfWzeoDadZ6EOk9s6FZq5dXY+XFd1R075wWCnE7TuYzMd4QFZ9dPfIeAAZ1goLwOtnu1EP4GuebKzS4yZVkXjGm6RjepfsMQMmqRVtrUG+ttfgfKAr25fmZYuTbpb2XP6wfixS/Ccq6abEavxN4Ni90hSjUDNORPi96+dqEZnIRLneL+ppTY5kLvigG5poBiQBJiEHu8KaAFYByAM9GJZoBaip28Xf9uyqTXvVjMcpsKP88tiQx/Xz9K8ObBklrRFOyuFAe7k0DsZYCtKSB4tYJpgleAwy70LfVoPU9HZSvucBaCM56Gtmw+BW/44/ITHywS5Q+lWnOyIPSKnzp+lGBMh2nXOejVSeIaJSsExQJtlsh2v3q+VZ0qHnnJKNWmTi22sIzO0Kl+yZNvLwF1qul2S0GPqamCGF5cGR0vbBxQ00FO8+xS/9xuKel2qcOJogExiPZzc1zAYhkm4QkAuQPcKT3mjhEsU+138hNweennayEtZazUqt4rbNynCyGndIp2dTBXRq0b1S525irmnQMlVwY5nSxsJHQaOnUUxpxrUaPFU2Es5yywo0Jtxi+fpWVxb0D7iV8+C+z8JgnBexeBtu/7g+ySVjl8u+/Bb6tURGthKgtJXPmqSh4fKSqHtlrz0hE9T3D4dl+lVC8bmXLzfew5WlAk1XRykNUpNtUmgwdSdBilGVFPVNGQsS5I8Dy01hhdbo9ziGCwNmfmdJpzan/Ncs/pDL2V5FNmUTQKt0opWBUohBYuwex4tSMdLvTRs9YMFGT0HgxFNezecxDa15yDe3UcgofrGWpqd8RMM38CLOsb/xLOf7z+AXsbcdFXsbSXAAAAAElFTkSuQmCC';
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
                icon.style.cssText = 'display:none;position:absolute;left:2.3rem;bottom:0.41rem;width:3.25rem;height:1.47rem;pointer-events:none';
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
