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

Native snapshot commands and per-car methods have been found in the release executable. The separate save and load binding experiment did nothing. The guarded physics callback diagnostic is built and verified in solo practice with three I/U pairs on the physics thread. It does not save or restore yet. See the linked research for addresses and limitations.
