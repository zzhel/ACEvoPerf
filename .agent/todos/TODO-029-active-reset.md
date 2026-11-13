---
name: TODO-029-active-reset
kind: todo
description: test native physics snapshots and build a single checkpoint active reset for solo practice
updated: 2026-11-13
links: [active-reset-2026-11-13]
status: open
by: owner
area: foundation
born: 2026-11-13
---

# Active reset

## What

You activate it on a part of the map, drive past that part of the map, then activate it again and you are taken back to drive the same part again.

## Done when

A solo practice test repeatedly restores a moving car's saved position, heading, speed and gear through one checkpoint button. A separate command replaces the checkpoint. Session changes clear it, and multiplayer cannot activate it.

The native binding experiment did nothing. The DLL prototype saves a checkpoint and restores position, speed and gear correctly in the user's solo practice test, with two successful restore calls logged. The local session guard and its test cases are in the linked research. The final one button interaction remains.

The owner requested configurable entries in the game controls and a larger dotted car beside the session timer, grey without a snapshot and green with one. The icon and actions are gated to local sessions. Live logs show captures, successful restore returns and XInput remapping after Apply. DirectInput wheel hardware still needs a live test.
