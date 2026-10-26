# Vendored firmware documents

Baseline copied from [TWildberger/lxr-02-firmware](https://github.com/TWildberger/lxr-02-firmware),
branch `twildberger-lxr-02-firmware-sysex-protocol`, commit `706e062` ([PR #1](https://github.com/TWildberger/lxr-02-firmware/pull/1)):

| File | Purpose |
|------|---------|
| `sysex.md` | Extended SysEx protocol v1 (implemented in `core/src/SysEx.cpp`) |
| `params.json` | Authoritative parameter table (ids, scopes, ranges, CC/NRPN, mod targets) |
| `file-formats.md` | SD card file layouts |

File-format/SysEx compatibility notes additionally track firmware
[freeze PR](https://github.com/TWildberger/lxr-02-firmware/pull/5) and
[feedback-FM PR](https://github.com/TWildberger/lxr-02-firmware/pull/6),
including bit 8 for freeze and bit 7 for the engine trailer. `params.json` and
the generated automation table retain the baseline layout. The editor exposes
the six opt-in engine fields as static container controls using the documented
IDs 221-226, without exposing formerly reserved bytes on legacy kits.

`tools/gen_params.py` takes ids, symbols, scopes, ranges, MIDI numbers and the
modulation-target table from `params.json` and only adds editor presentation
(labels, sections, ordering, init-kit defaults) derived from the firmware
source. Deliberate deviations are listed in `VENDORED_OVERRIDES` in the
generator and checked by `tests/test_param_table.cpp`.

To update: copy the new files here, run
`python3 tools/gen_params.py <firmware checkout>`, and run the tests.
