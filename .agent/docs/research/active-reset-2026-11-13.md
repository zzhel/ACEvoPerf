---
name: active-reset-2026-11-13
kind: doc
description: per-car save and restore verified in solo practice, one button control and multiplayer lockout remain
updated: 2026-11-13
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

Exactly one car is a prototype constraint, not proof of a single-player session. Automatic multiplayer exclusion is not implemented, so this build is only for an explicitly requested solo test. The user has been told this limitation. A release needs a verified session guard and the final one button interaction.

## Controls and HUD experiment

The controls page now exposes actions 551 and 552 as Active reset: Save snapshot and Active reset: Restore snapshot in the Car group. An initial script wraps the stock controls component and supplies missing rows from the current input configuration; the stock binding editor retains capture, conflicts, Apply and persistence. The user confirmed the rows appear. The settings files also confirmed remapped N/M keyboard bindings and XInput buttons 7/13 persisted.

The first input detour at `0x7414a0` did not receive those actions during the live test. Its UI rows worked, but save and restore regressed. The revised detour observes the shared resolved-input helper at `0x20e8890`, called after keyboard and device button press/repeat handling. It forwards all five original arguments and queues only 551/552 for the physics thread, with a 500 ms expiry. Hardcoded I/U polling is removed. The native callback is guarded by an instruction hash.

The session banner script inserts an SVG with a dotted trailing car and solid leading car. The state is published atomically from successful snapshot saves and checkpoint clearing, then sent to the main Cohtml view after the UI frame completes. View slot 60 accepts a C string, confirmed from the installed DLL's `0x479ab0` implementation. The original icon placement outside the banner was clipped by `.session-info { overflow: hidden; }`; the revised icon occupies the existing left margin before the timer. Grey means no checkpoint and green means a saved checkpoint.

The revised DLL builds with zero errors. A standalone script harness checks keyboard remapping, wheel mapping, duplicate row prevention, labels and grey/green transitions. Live input and indicator verification is pending; the first combined build failed the live test. Do not treat the harness as proof of native game behavior.

## Remaining verification

Add a verified session guard and the final one button interaction. Validate car and session changes, other car types and repeated checkpoint replacement. Position, speed and gear are confirmed by the user in the initial solo practice test.

Before making a one button feature, verify session changes invalidate the checkpoint, and determine effects on timing, replay, force feedback and collisions. Do not expose these actions in multiplayer. The diagnostic patches one call in memory through the proxy, and does not modify the executable on disk.
