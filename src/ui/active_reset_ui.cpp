#include "acevo/ui/active_reset_ui.h"
#include "acevo/ui/cohtml_hooks.h"
#include "acevo/core/log.h"

namespace {
std::atomic<bool> snapshotActive{false};
void* mainView = nullptr;

const char pageScript[] = R"js(
(function () {
    'use strict';
    if (window.__acevoActiveReset) return;
    var active = false;
    var indicator;
    // Transparent PNGs with the same R8-style outline mask, one for each state.
    var inactivePng = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAJAAAAAmBAMAAADZ+w/jAAAAMFBMVEXT19vT19vT19vT19vT19vT19vT19vT19vT19vT19vT19vT19vT19vT19vT19vT19smuEYJAAAAEHRSTlMAESIzRFVmd4iZqrvM3e7/dpUBFQAAAzFJREFUeNrtVUtsTFEY/s6dzkzbGXpJiYrH9U4XlalE6IZ6xyNRlXgkwkVIbLRdSEMkLSKSClM2xKsNCySojQ2hukFETOOVIM2gYtNyPDrT19z7WUw77tzOvRI74V/dc/L9539933+Bf8d2AOqf+GVZD+LsizNnAneaR7t5TF2ySAN4++w7Z4yfXXlsbWBZvnNScyWT1rnVCVLwIJuxAGtrWBxtd46VMkN3KG1nScGN5/HqOv+clxP7nTrxCLx99x2grCtXzrW2ZgSFqQ18KTLmKc2IqaFR9avG7kwQnz72Veowp7QiY07Z5IHUYQzZkgHT0Jt2jMhQBlATrc1bRcOet+dJiEybVI2hLrSBRs7eR8P6vGhgT5rTisphbLVl5K0KmCHfQUus85L2Wvzk29NrU28pMj6c7326LYH5sj5spEDTm0kyYePXKpLs2DYYy+z1fhk6pHEsjcjQUX0gGPn58Sk7B8VxkjRvAQBGaU3tGDG0sWI7Nvf52QUACNI4lIntytK6eyQvAhDRdo+TIJRQNmPJmVJ34rrYGDUNDfCZfS4KFffqAcDLHhfQqDCfAR4Zc9O6UA4DCPKmGyiHPQBKylz3zHjqQNC5smTlBhQ8dA2GyQj9fqs1KhqUp5WumBxDA4J0T3sCyxCga48waxKAIBtdQUWsFbkxFI8dncT5Lmwq3FCzvLvldPVXAEDJjCvdVfVAVkfuSjz5CsCzwOKfuA8Ai6FcVddAibA8mdT+yi0MRXo9/BHgzXwVgJDxPH4DIKIkPwIQzbSYoQEoIsleAIH+JJWy+KNB6jR8ZjyPH2VLUjyB5ODHDYC9TDMdQJikqQOAJiIfAHjZNTOhhuNK9Jmfu8w4AK25XVSrACCut0kDQC6NtpRF2Qggara1XRqk5pTLEOqxWmUNskLIV7HYyw9ATn++9qshq3kCqGBlSk/CzzhQMKDGwfHpuz+lzWFZ8TW9gvWWm4BpXrluGhZdiqh8UyellYfzWStN1baev6+mlWJKhCSfWzHzSDKhWb06tUjC/gP77n+ddjNGkn1a2oZuIs0DaSAVhSdtaj03hMzT9u6xycWz/kg5/tvfaz8Bmo+UdiaINMwAAAAASUVORK5CYII=';
    var activePng = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAJAAAAAmBAMAAADZ+w/jAAAAMFBMVEWO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+KuO+Ks6mUMGAAAAEHRSTlMAESIzRFVmd4iZqrvM3e7/dpUBFQAAAzFJREFUeNrtVUtsTFEY/s6dzkzbGXpJiYrH9U4XlalE6IZ6xyNRlXgkwkVIbLRdSEMkLSKSClM2xKsNCySojQ2hukFETOOVIM2gYtNyPDrT19z7WUw77tzOvRI74V/dc/L9539933+Bf8d2AOqf+GVZD+LsizNnAneaR7t5TF2ySAN4++w7Z4yfXXlsbWBZvnNScyWT1rnVCVLwIJuxAGtrWBxtd46VMkN3KG1nScGN5/HqOv+clxP7nTrxCLx99x2grCtXzrW2ZgSFqQ18KTLmKc2IqaFR9avG7kwQnz72Veowp7QiY07Z5IHUYQzZkgHT0Jt2jMhQBlATrc1bRcOet+dJiEybVI2hLrSBRs7eR8P6vGhgT5rTisphbLVl5K0KmCHfQUus85L2Wvzk29NrU28pMj6c7326LYH5sj5spEDTm0kyYePXKpLs2DYYy+z1fhk6pHEsjcjQUX0gGPn58Sk7B8VxkjRvAQBGaU3tGDG0sWI7Nvf52QUACNI4lIntytK6eyQvAhDRdo+TIJRQNmPJmVJ34rrYGDUNDfCZfS4KFffqAcDLHhfQqDCfAR4Zc9O6UA4DCPKmGyiHPQBKylz3zHjqQNC5smTlBhQ8dA2GyQj9fqs1KhqUp5WumBxDA4J0T3sCyxCga48waxKAIBtdQUWsFbkxFI8dncT5Lmwq3FCzvLvldPVXAEDJjCvdVfVAVkfuSjz5CsCzwOKfuA8Ai6FcVddAibA8mdT+yi0MRXo9/BHgzXwVgJDxPH4DIKIkPwIQzbSYoQEoIsleAIH+JJWy+KNB6jR8ZjyPH2VLUjyB5ODHDYC9TDMdQJikqQOAJiIfAHjZNTOhhuNK9Jmfu8w4AK25XVSrACCut0kDQC6NtpRF2Qggara1XRqk5pTLEOqxWmUNskLIV7HYyw9ATn++9qshq3kCqGBlSk/CzzhQMKDGwfHpuz+lzWFZ8TW9gvWWm4BpXrluGhZdiqh8UyellYfzWStN1baev6+mlWJKhCSfWzHzSDKhWb06tUjC/gP77n+ddjNGkn1a2oZuIs0DaSAVhSdtaj03hMzT9u6xycWz/kg5/tvfaz8Bmo+UdiaINMwAAAAASUVORK5CYII=';
    function paint(icon) {
        var label = active ? 'Snapshot saved' : 'No snapshot';
        icon.setAttribute('src', active ? activePng : inactivePng);
        icon.setAttribute('alt', label);
        icon.setAttribute('title', label);
    }
    window.__acevoActiveReset = function (value) {
        if (active === value) return;
        active = value;
        if (!indicator) return;
        paint(indicator);
        console.log('[ACEvoPerf] snapshot indicator ' + (active ? 'active' : 'inactive'));
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
                icon.style.cssText = 'position:absolute;left:2.3rem;bottom:0.72rem;width:3.25rem;height:0.86rem;pointer-events:none';
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
    const char* script = snapshotActive.load(std::memory_order_acquire)
        ? "window.__acevoActiveReset && window.__acevoActiveReset(true);"
        : "window.__acevoActiveReset && window.__acevoActiveReset(false);";
    reinterpret_cast<ScriptFn>((*static_cast<void***>(mainView))[60])(mainView, script);
}
}

void SetActiveResetSnapshot(bool active)
{
    snapshotActive.store(active, std::memory_order_release);
}

void InstallActiveResetUi()
{
    AddCohtmlViewListener(&OnView);
    AddUiFrameEndListener(&OnFrameEnd);
}
