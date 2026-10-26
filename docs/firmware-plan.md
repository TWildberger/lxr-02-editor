# Firmware improvement plan

KitForge works with the stock LXR-02 firmware (v1.7) and gets more capable with our
fork, [TWildberger/lxr-02-firmware](https://github.com/TWildberger/lxr-02-firmware).
This document tracks what the fork already provides, what the editor still has to
work around, and the proposed next steps. The protocol specification itself is
`docs/firmware/sysex.md`, vendored from the fork.

## Done: SysEx protocol v1 (fork PR #1)

- Identity handshake. The editor sends `F0 7D 4C 7F 01 01 7F F7`; no reply within
  500 ms means stock firmware.
- Kits:
  - dump and load for the edit buffer and slots 0–63;
  - store to a slot and load a slot;
  - rename.
- Patterns: chunked transfer, plus pattern rename.
- Globals: dump and load.
- Single-parameter get and set.
- Parameter echo: knob turns on the device notify the host. Kit-changed events
  are also sent.
- New globals:
  - 302 SysEx enable;
  - 303 Device ID (1–16);
  - 304 Param Echo.

  These make glo.cfg 33 bytes; stock is 30.
- Capability bits in the identity reply, so the editor can enable features one by one.

KitForge implements all of v1: `core/src/SysEx.cpp`, `plugin/Source/DeviceSession.cpp`.

## Workarounds the editor still needs on stock firmware

| Gap | Effect today | Fix in the fork |
|-----|--------------|-----------------|
| CC0 is bank select | Param 127 cannot be set via CC | Add an NRPN for it (or use `PARAM_SET`) |
| LFO target voice ignored by the MIDI parser | LFO routing to another voice only arrives via .snd | Accept it via NRPN (already possible over SysEx) |
| Morph destination not applied live | Same | Apply it on receive |
| Mod targets ≥128 or without their own CC/NRPN | Cannot be selected over MIDI | NRPN value = target index (SysEx already uses the index) |
| Globals have no CC/NRPN | Only via glo.cfg on the SD card | Covered by `GLOBALS_DUMP` in the fork |

## Open issue: out-of-range bytes in factory kits

Some factory kits contain values outside the front-panel ranges:
- hi-hat transient wave 24 (one kit: 89);
- FX type 5.

The stock firmware loads them from .snd unchanged. `KIT_DUMP` clamps every value
(sysex.md §5.6), so sending such a kit over SysEx can change how it sounds.

KitForge keeps the original bytes in its files and library. To decide in the
fork:
- **(a)** `KIT_DUMP` stores bytes exactly as a .snd load does, and only validates
  mod-target indices. This is the preferred option because it makes .snd and SysEx
  equivalent.
- **(b)** Document the clamping. The editor would then warn before sending
  affected kits.

## Proposed next steps

Ordered by how much they help with "the device is hard to program".

1. **Step/pattern editing over SysEx (v1.1, new capability bit).** Today ids
   247–271 are read-only and never echoed. Add step get/set and mute/transport
   events, so the editor can offer a step grid and pattern copy between slots
   without whole-pattern transfers.
2. **Stock-MIDI parity.** Implement the table above. Hosts without SysEx support,
   such as DAW controller mappings, can then reach every sound parameter.
3. **Byte-exact `KIT_DUMP`.** Option (a) above.
4. **SD-card file access over SysEx.** List, read and write files on the card, so
   the librarian can back up and restore the whole card (kits, patterns, songs,
   samples) without removing it.
5. **On-device usability.** These need hardware testing before scoping:
   - copy/paste a voice;
   - compare the edited kit with the saved one;
   - init voice;
   - randomize with locks (mirroring the editor);
   - show long parameter names and units on the display.
6. **Velocity modulation of other voices.** Velocity targets are currently limited
   to the voice's own parameters (`modTargetRange(voice)`). Opening this up needs a
   kit-format change, so pair it with a .snd version tag.

Each item is developed in the fork with a capability bit or a protocol-version
bump (sysex.md §9). KitForge then enables the matching UI only when the device
reports that capability.
