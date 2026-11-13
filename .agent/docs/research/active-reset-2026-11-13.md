---
name: active-reset-2026-11-13
kind: doc
description: per-car save and restore, configurable controls, notifications and local session guard
updated: 2026-11-14
links: [moddability, TODO-029-active-reset]
---

# Active reset

Static analysis found a native snapshot route. The keyboard binding test produced no reset and neither snapshot handler appeared in the two session logs. Both bindings persisted in the keyboard configuration. The DLL prototype subsequently restored position, speed and gear correctly in the user's solo practice test.

## Build identity

The inspected executable is 101068288 bytes, PE timestamp `0x6a9ec72a`, SHA256 `ab11fc2a35ef9c2c61c6e0bcbdeb755b0d7787d101c3fdebfee12df3eadedbcb`. All addresses below are RVAs for this exact executable, not portable addresses.

## Native input route

The embedded `InputEnum.proto` defines `InputAction_AIDev_SaveSnapshot` as 551 and `InputAction_AIDev_LoadSnapshot` as 552. The function at `0x132cff0` branches on these values, constructs snapshot commands and sends them through a command interface. It references `snapshot.bin`, but the realtime handlers below retain snapshots in memory. A filename in the command is not evidence that a file is written.

For a first experiment, bind these actions through `KeyboardInputConfiguration.keyboard_commands` using the existing `tools/acevo_settings.py`. Use action input and number 551 with key 73 for I, and 552 with key 85 for U. Set `trigger_time_ms` to 22 and `repeat_time_ms` to -1. Preserve existing commands, check for key collisions and back up the file with the game closed.

## Realtime implementation

- Save handler `0x14d1760` clears the snapshot map at controller offset `0x2ea8`, iterates cars from the object at `0x2dc0` and stores byte vectors keyed by each car GUID at `0x7700`. It appears to capture all physics cars rather than only the command's car ID.
- Load handler `0x14d1480` iterates that map, finds current cars with matching GUIDs and passes their stored bytes to the restore routine.
- Save wrapper `0x22259c0` calls `0x24370a0`, which builds a protobuf snapshot through `0x24386c0` and serializes it into a byte vector.
- Load wrapper `0x22311e0` calls `0x2437030`, which parses a protobuf snapshot and calls `0x24372d0` through thunk `0x808d7`. It then updates additional car components through virtual calls.

The embedded `PhysicsSnapshot.proto` defines `ACCCarSnap` with chassis and suspension rigid bodies, drivetrain, fuel, tyre state, damage and brake state. `ACCRigidBodySnap` includes a matrix, velocity and angular velocity. `ACCDrivetrainSnap` includes engine and shaft speeds and current gear. The serializer and deserializer contain substantial implementations, not just empty command stubs.

## Failed input test and diagnostic

The tested game identifies itself as `0.9.1+release.6`. Searching direct call, jump, RIP relative LEA and absolute pointer references found only the thunk at `0x629d6` for the input handler `0x132cff0`, and no references to that thunk. This supports an unregistered developer input route, but is not a complete proof of unreachable code.

The realtime controller vtable is at `0x3231a70`. Slots 9 and 10 resolve to `0x14d6dc0` and `0x14d6600`. Comparison with the base interface's fallback messages identifies them as saveCarSnapshot and restoreCarSnapshot. Both take a car GUID and a byte vector and return a boolean. Their bodies access physics cars directly without a visible thread handoff, so do not call them from an input polling thread.

`src/engine/active_reset_probe.cpp` hooks the call at `0x14d6a38` to `stepOnce`, preserving the call's controller pointer, float timestep and double time arguments. It runs the original step before processing keys. The hook checks the PE timestamp, image size and six code hashes before patching. The callback checks the controller vtable before reading the car vector and only acts on key presses while the game has focus.

Enable the diagnostic with `[probe] enabled=1` in `acevo_active_reset.ini` next to the game. The distributed setting defaults to zero. Log entries have prefix `[active-reset-probe]`, report installation, the first physics callback and I/U key presses with physics car count. The live solo practice test on 2026-11-13 logged installation, the physics callback at a 0.003 second timestep and three I/U pairs on the same thread with exactly one physics car. This verifies the hook and input path, not state restoration. The C++ build completed with zero errors and an existing conversion warning in `src/engine/flags.cpp`.

## Snapshot prototype

The additional opt-in `[probe] solo_snapshot_experiment=1` enables I to capture and U to restore through the per-car methods. Both settings default to zero in the distributed file. In the live test on 2026-11-13, the log recorded a 1560-byte snapshot and two successful restore returns. Asked whether position, speed and gear returned correctly, the user answered "Yes, it restores correctly". This verifies the core mechanism in that session, not all cars or session transitions.

Snapshots remain in a game-allocated byte vector. The native vector move assignment at `0x1117800` releases and replaces that vector, avoiding a free through the DLL's separate C runtime. The checkpoint is invalidated on a changed controller, world, car pointer or GUID, or if there is no longer exactly one physics car. Only one checkpoint is retained. The current allocation is reclaimed at process exit if no further physics step clears it.

Exactly one car remains a constraint for taking a snapshot. The separate session guard below determines whether reset is available.

## Controls and HUD experiment

The controls page now exposes actions 551 and 552 as Active reset: Save snapshot and Active reset: Restore snapshot in the Car group. An initial script wraps the stock controls component and supplies missing rows from the current input configuration; the stock binding editor retains capture, conflicts, Apply and persistence. The user confirmed the rows appear. The settings files also confirmed remapped N/M keyboard bindings and XInput buttons 7/13 persisted.

Neither the platform action detour at `0x7414a0` nor the input helper at `0x20e8890` received actions 551 and 552 in live practice. Both detours have been removed. The controls page still uses the stock editor. The physics callback reads the saved keyboard and device bindings and polls the configured inputs when EVO has focus. It checks for file changes twice a second so Apply takes effect in the same game run. Keyboard modifiers, controller shift buttons and hold times come from the saved bindings. Each press triggers once.

The state is published atomically from successful snapshot saves and checkpoint clearing, then sent to the main Cohtml view after the UI frame completes. View slot 60 accepts a C string, confirmed from the installed DLL's `0x479ab0` implementation. The user reported the original SVG stayed dark when a snapshot was saved and overlapped the sloped edge of the timer panel. The updated script assigns the current state's transparent PNG directly to the image element. Both images use a dotted trailing car and a solid-outline leading car with an R8-style profile. Light grey means no checkpoint and light green means a saved checkpoint. The icon sits within the left margin before the timer, inset from the sloped edge. A live screenshot confirmed the green icon is fully inside the panel after saving. The UI script check also confirmed it selects grey when the checkpoint state clears.

In solo practice the input build loaded the saved N/M bindings, recorded two 1560-byte saves and two successful restore returns. It then loaded XInput buttons 13 and 7 after Apply in the same run, and logged more saves and successful restores. The user confirmed controller inputs work. A standalone check covered invalid and cleared settings, press edges and controller button mapping. DirectInput wheel hardware still requires live testing.

## Local session guard

The guard starts closed. The session start routine at `0x78b1b0` accepts a tagged local season or a server connection. It enables reset only after a local start creates a new local server in the platform client at `+0x4538`. Remote variants and rejected starts leave reset disabled. The multiplayer join and connect callbacks close the guard before forwarding to the game. Exit also closes it. The version and instruction checks disable reset if the expected hooks cannot be installed.

Physics checks the guard before polling the bindings. A new session generation invalidates the previous snapshot even if the same physics objects are reused. A local test covered remote and failed starts, a successful local start, joining during startup, connecting and exiting. Multiplayer server testing has not been performed.

The owner replaced the persistent snapshot icon with transient in-game notifications. After physics completes a save or restore, the UI posts the outcome to EVO's existing `UINotification` event and `UIInternalNotification_Simple` renderer. A restore without a checkpoint and failed operations also report their outcome. The pending notice carries its session generation so a queued notice is discarded if the session changes before the UI receives it. The UI also checks the game's multiplayer mode. The build no longer embeds the icon assets.

This guard only controls active reset. It does not remove the proxy DLL or other in-memory hooks. Use the original game files for a multiplayer launch.

## Gate marker experiment

The game schema defines `AddCustomPoiCommand` with `ZoneData` points, and the executable's handler type identifies `FreeRoamRemote`. The free-roam HUD renders selected points using game-projected screen coordinates. Circuit practice does not expose that HUD or a verified arbitrary-point projection interface.

On 2026-11-13 a local circuit probe sent `AddCustomPoiCommand` through `OnUICommand` when setting a gate. Its first payload had an invalid nested type, and the game logged that the circuit game mode had no message handler for the command. A second payload changed the type names and crashed the game in argument conversion at executable RVA `0x2b8ce5c`; it did not establish that the command works. The probe was removed and the DLL rebuilt. Do not repeat this command in circuit mode. The user confirmed the revised black-screen restore fade works.

On 2026-11-14 an empty `AddCustomPoiCommand` with `clear_before=false` returned without a conversion error or crash, and circuit mode logged twice that it had no handler for the command. This confirms that varying the nested marker payload cannot make this UI command work in the tested circuit session. The probe was removed and the normal DLL rebuilt.

## Remaining verification

Add the final one button interaction. Validate more cars and repeated checkpoint replacement. Position, speed and gear are confirmed by the user in the initial solo practice test.

Before making a one button feature, verify session changes invalidate the checkpoint, and determine effects on timing, replay, force feedback and collisions. Do not expose these actions in multiplayer. The diagnostic patches one call in memory through the proxy, and does not modify the executable on disk.
