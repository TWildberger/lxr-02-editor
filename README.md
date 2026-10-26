# KitForge: editor & librarian for LXR-02

KitForge is a sound editor and preset librarian for the Erica Synths LXR-02 drum
machine. It is a JUCE plugin (VST3, AU, Standalone) and works with the **stock
firmware** over CC/NRPN and SD-card files. When it detects the
[extended firmware](https://github.com/TWildberger/lxr-02-firmware), it switches to
a full SysEx mode with kit dump/load, readback and slot management.

> KitForge is an independent project and is not affiliated with or endorsed by
> Erica Synths. "LXR-02" is used only to describe compatibility.

## Features

**Editor**
- Every sound parameter of all six voices (Drum 1–3, Snare/Clap, Cymbal, Hi-Hat) is
  on screen, grouped as on the device (Osc, Amp EG, Mod, FM, Transient, Filter, LFO,
  Mix). The "FX & Kit" page holds the FX, the global sample rate and morph.
- The Mod Matrix page shows all LFO and velocity routings at once. Targets are
  picked from a menu grouped by voice, and choosing a target on another voice also
  sets that LFO's target voice.
- Undo/redo (Cmd+Z / Shift+Cmd+Z), A/B compare, copy voice, and reset voice/FX.
- Randomize, with an amount setting, for a voice or the whole kit. Right-click any
  control to lock it against randomizing.
- Morph the kit halfway towards the other A/B kit.
- Every sound parameter is exposed to the host for automation. The plugin state
  stores the whole kit byte-exactly and sends it to the device on session recall.
- Globals page for all global settings (MIDI channels and notes, clock, filters,
  and so on), with glo.cfg open/save and send/fetch in extended mode.

**Librarian**
- A SQLite library (`~/Library/Application Support/KitForge/library.sqlite`, or set
  `KITFORGE_LIBRARY_PATH`) for kits (.snd), patterns (.pat), globals (glo.cfg),
  songs and project files.
- Import single files or a whole SD-card folder. Duplicates are detected by content.
- Search, filter by type or tag, favorites, notes, rename and export.
- Kit diff between two library kits, or between a library kit and the current
  kit.
- Drag .snd files or folders onto the window.
- Batch send to device slots and fetch from slots (extended firmware).

**File formats**
- .snd (8-byte name plus 247 parameter bytes), .pat (V1–V3, written as V3) and
  glo.cfg (30 bytes stock, 33 bytes extended).
- The parsers keep every byte, including out-of-range values found in factory kits.
  Round-trip tests check that all 216 factory kits come back byte-identical.

## Device connection

Set this up on the **Device** tab.

| Route | Use |
|-------|-----|
| Plugin MIDI output | Default in a DAW. Route the plugin's MIDI output track to the device. |
| Direct MIDI port | Default in the standalone app. KitForge opens the MIDI ports itself. Use this when the DAW filters SysEx; Logic/AU in particular is unreliable. |

On connect, KitForge sends an identity request (`F0 7D 4C 7F 01 01 7F F7`):
- **No reply within 500 ms**: stock firmware. Edits go out as CC/NRPN, and the
  rate is limited to stay DIN-safe.
- **Reply**: extended firmware. Edits are batched into `PARAM_SET`. Kits, globals
  and patterns can be read and written, kits stored to slots, and with Param Echo
  on, knob turns on the device show up in the editor.

With a direct port, also select the device's MIDI **input**. Over USB the device
drops its output if nobody reads it for more than 100 ms.

### Stock-firmware limitations

The stock firmware cannot receive some things over MIDI. KitForge reports them in
the Device log and keeps them in the kit, so save the kit as .snd and load it from
the SD card to transfer them:
- LFO target voice (the stock MIDI parser ignores it), and LFO/velocity targets that
  have no CC/NRPN number of their own.
- Morph destination kit.
- Parameter 127, because CC0 is bank select.
- Global settings: use glo.cfg on the SD card.

## Building

Requirements: CMake 3.22+, a C++17 compiler (Xcode or Command Line Tools on macOS),
and an internet connection the first time, to fetch JUCE 8.0.15, Catch2 and
nlohmann/json.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
(cd build && ctest --output-on-failure)
```

The plugins are written to `build/plugin/KitForge_artefacts/Release/{VST3,AU,Standalone}`.
To install them, copy the bundles to `~/Library/Audio/Plug-Ins/{VST3,Components}`.

Options:
- `-DKITFORGE_BUILD_PLUGIN=OFF` builds only the core library and tests, without
  JUCE.
- `-DKITFORGE_BUILD_TESTS=OFF` skips the tests.
- `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE-8.0.15` builds offline with a local
  JUCE.

To run the factory-kit round-trip test, point it at a folder of factory .snd
files:

```sh
KITFORGE_FACTORY_KITS_DIR=/path/to/kits ./build/tests/kitforge_tests "[factory]"
```

`plugin_smoke` is a headless test of the real processor and editor. It covers
state recall, undo, A/B, automation, firmware detection and every page rendering.
Pass it a directory to also write a PNG of each page:

```sh
./build/plugin/KitForgeSmokeTest_artefacts/Release/KitForgeSmokeTest /tmp/kitforge-shots
```

## Layout

| Path | Contents |
|------|----------|
| `core/` | JUCE-free library: param table, file formats, stock MIDI codec, SysEx protocol v1, ParamSync, librarian (SQLite) |
| `plugin/` | JUCE processor, `DeviceSession` (transport and protocol), UI pages, smoke test |
| `tests/` | Catch2 unit tests and fixtures |
| `tools/gen_params.py` | Generates the param table from the vendored firmware table plus editor presentation (labels, sections) |
| `docs/firmware/` | Vendored firmware spec: SysEx protocol, params.json, file formats |
| `docs/firmware-plan.md` | Planned firmware improvements |

## License

GPL-3.0-or-later (see `LICENSE`), matching the firmware it is derived from. JUCE is
used under its AGPLv3 option, which is compatible with GPLv3.
