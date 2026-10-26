# Vendored firmware documents

Copied verbatim from [TWildberger/lxr-02-firmware](https://github.com/TWildberger/lxr-02-firmware),
branch `twildberger-lxr-02-firmware-sysex-protocol`, commit `4e1c930`:

| File | Purpose |
|------|---------|
| `sysex.md` | Extended SysEx protocol v1 (implemented in `core/src/SysEx.cpp`) |
| `params.json` | Authoritative parameter table (ids, scopes, ranges, CC/NRPN, mod targets) |
| `file-formats.md` | SD card file layouts |

`tools/gen_params.py` takes ids, symbols, scopes, ranges, MIDI numbers and the
modulation-target table from `params.json` and only adds editor presentation
(labels, sections, ordering, init-kit defaults) derived from the firmware
source. Deliberate deviations are listed in `VENDORED_OVERRIDES` in the
generator and checked by `tests/test_param_table.cpp`.

To update: copy the new files here, run
`python3 tools/gen_params.py <firmware checkout>`, and run the tests.
