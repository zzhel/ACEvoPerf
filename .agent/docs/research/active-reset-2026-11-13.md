---
name: active-reset-2026-11-13
kind: doc
description: native physics snapshots and input actions found in the release executable, live reset test pending
updated: 2026-11-13
links: [moddability, TODO-029-active-reset]
---

# Active reset

Static analysis found a native snapshot route worth testing before adding DLL hooks. No runtime success is claimed.

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

## Verification still needed

Use solo practice without AI because the save handler appears to capture every physics car. Save while moving, drive further and restore repeatedly. Confirm position, heading, speed and gear, then check logs for both handler names. Check whether the input actions are accepted in the release UI. The existence of command code does not prove that input dispatch reaches it.

Before making a one button feature, verify session changes invalidate the checkpoint, and determine effects on timing, replay, force feedback and collisions. Do not expose these actions in multiplayer. No executable or DLL was modified during this investigation.
