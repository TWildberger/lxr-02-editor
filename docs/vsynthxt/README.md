# V-Synth XT Companion

An independent hardware editor and patch librarian, available as
**V-Synth XT Companion** (VST3, AU and standalone). It does not emulate the XT.
It needs V-Synth XT hardware, **not an LXR-02 or KitForge installation**.
Roland and V-Synth are trademarks of their respective owner; this project is not
affiliated with or endorsed by Roland.

## Initial native-mode milestone

The Patch page exposes name, coarse/fine tuning and chorus type. The Native page
adds zone-selectable oscillator/structure/mix, all 13 envelopes, all five LFOs,
both COSM blocks and global MFX/chorus/reverb sections.
Numeric controls are host-automatable, but automation edits the **local patch**:
it does not stream to the hardware. Complete snapshots preserve all 103 documented
part-1 patch blocks, including every zone, FX, controllers, arpeggiator and step
modulator. Unexposed and reserved bytes are retained without clamping.

Available workflows:

- Fetch a complete temporary part-1 patch without writing to the device.
- Edit locally, undo/redo and compare A/B (copy to the other buffer first).
- Save/search patch snapshots in a separate SQLite library, with tags, favorites
  and project/sample notes. Saving identical patch bytes updates their metadata.
- Import/export native part-1 DT1 snapshots as `.syx`. This is not a `.prj` or
  sample-file importer; incomplete, mixed-device, corrupt and unsupported-address
  files are rejected.
- Explicitly send a whole snapshot to the temporary part-1 buffer, then fetch all
  blocks again and compare bytes. Transmission alone is **not** reported as
  verification. This replaces unsaved part-1 edits; archive them first.
- Restore complete local patch and A/B state from a DAW session, without opening
  MIDI ports, changing programs or sending patch data. The selected editor tab is
  also restored. MIDI routing and native-mode confirmation are not serialized
  into DAW state.

There are no user-slot writes, system writes, program changes or automatic
connect/recall sends. Continuous knob-to-hardware automation, other parts,
user-slot browsing, algorithm-specific numbered effect/COSM slot bindings, VC-1/VC-2, sample/project
management and audio streaming are not implemented in this milestone.

## Native parameter pages

Select **Native**, a group, a zone (1-16) and a section. Effects are patch-global;
the zone selector is disabled there. Each envelope/LFO section identifies its
destination. Long sections scroll. The view selection is stored in DAW state.

The checked-in `native-params.json` expands into 4,972 native parameters, in
addition to the original three automation IDs (which remain unchanged). Each
zone has its own stable IDs; changing the viewed zone does not change automation
bindings. The generated registry exposes documented scalar oscillator fields
(including PCM wave number, but not wave data/hash), envelopes, LFOs, COSM and
effects. Two-/four-byte fields use high-nibble-first codecs, with signed display
offsets where documented. Generate/check with:

```sh
python3 tools/gen_vsynthxt_params.py
python3 tools/gen_vsynthxt_params.py --check
```

This is a protocol-level native editor, not a finished recreation of the XT's
screen. Fields labeled **wire** expose numeric protocol codes; envelope/LFO sync
values 128-144 remain numeric codes. The Native page provides named algorithm
selectors for both COSMs in every zone (codes 0-16), MFX (1-41), chorus (1-8)
and reverb (1-10). Code 0 for effects remains explicitly unnamed: the numbered
owner-manual lists do not establish an algorithm name for it. Unknown raw codes
are displayed explicitly and disabled, never silently replaced.

A read-only algorithm reference and link to the official owner's manual show
selected semantic names, units and ranges; this is not an editable slot mapping.
References use printed pages 153, 159-163 (COSM), 164-185 (MFX), 186 (chorus)
and 186-191 (reverb) of
[Roland's V-Synth XT Owner's Manual](https://static.roland.com/assets/media/pdf/V-SYNTH_XT_OM.pdf).
For example, TVF cutoff/resonance are 0-127, chorus rate is 0.05-10 Hz or notes,
and reverb Delay time is 0-1300 ms in MONO or 0-650 ms in STEREO/ALTERNATE,
or notes. The source link covers the complete table where only a subset is
summarized. The PDF page index is one greater than the printed page.

**Unresolved mapping:** matrix-control destination markers (`#1`/`#2`, or
MFX1-MFX3) and screen-table ordering do not establish numbered SysEx slot
indices or their physical-value conversion/lookup tables. Those numbered
controls therefore retain generic labels, with no guessed units or narrowed
ranges. COSM/effect numbered parameters use the MIDI implementation's
manual's generic -20000..20000 representation, not algorithm-specific names,
units or ranges. Their meaning depends on the selected type. Changing type does
not initialize its other parameter bytes. Consult the XT manual before sending
such edits. Wave-number edits require the matching waves in the current project.

Every control shows its temporary address, stored wire value and source page.
Out-of-range or malformed-nibble values are preserved verbatim and the affected
UI controls are disabled rather than normalizing the snapshot. The captured
hardware patch contains some out-of-range detune/sub-octave fields relative to
the published table; these remain untouched. Host automation is an intentional
edit and can replace a field with a documented value, but never sends it by itself.
Unexposed controller/step/arpeggio bytes remain part of the complete snapshot.

## Connection and safety

1. Select the XT's **native V-Synth mode**, not VC-1/VC-2. Enable **Receive
   Exclusive** under the hardware's SYSTEM Com MIDI/USB settings. Match Device ID
   (17-32; default 17). The shared V-Synth model ID cannot prove which mode is
   active, so the app requires an explicit native-mode confirmation.
2. On the Device tab select Direct MIDI, both input and output ports, and the
   device ID. Click **Apply route**, then confirm native mode. Applying a route
   sends nothing and does not establish that the device answers.
3. Click **Fetch part 1**. A complete fetch must finish before the fetched patch
   replaces local state. A timeout is an error, not a fallback to another mode.
4. Archive/export the patch before editing or sending. **Send to temporary
   buffer** displays a replacement warning and verifies readback; it never
   commits the sound to a hardware user slot.

USB MIDI relies on the user's existing compatible driver; no driver is installed
or modified. The MOTU DIN connection is an alternative: XT MIDI OUT -> MOTU IN,
MOTU OUT -> XT MIDI IN. Device lists are captured when the editor opens; reopen
the editor after adding a new MIDI interface. Reapply the route after reopening
the editor or changing its settings.

Direct MIDI is the recommended initial route, especially with Logic/AU.
Host MIDI is optional and needs the DAW to pass SysEx in **both directions** and
keep processing the plug-in. Host transfers fail if processing stops; queued
messages expire rather than replaying later. The selected direct output is
leased across companion instances/processes; competing instances show an error.
Do not run unrelated MIDI editors on the same route.

The companion does **not** forward notes, controllers or unrelated SysEx. Route a
separate DAW MIDI track/controller to the XT for playing, and record its audio
through your existing audio interface. Avoid MIDI feedback loops.

Cancel/disconnect stops future writes but cannot undo bytes already transmitted.
A host can also retain an already emitted MIDI event outside the plug-in's
control. Fetch again before retrying after interruption. Exact readback mismatch
is reported explicitly; no undocumented "volatile" fields are silently ignored.
If local edits change during a transfer, completion does not overwrite them with
the fetched snapshot; the status asks you to Fetch again when ready.

## Patches are not sample backups

Snapshots retain wave references, not sample audio. Custom-wave sounds require
the same project and waves already loaded on the hardware. Use the library's
notes field to identify these dependencies. A snapshot is not a portable sound
or complete project backup.

The separate database is
`~/Library/Application Support/VSynthXTCompanion/library.sqlite` on macOS;
`VSYNTHXT_LIBRARY_PATH` can override it. It never imports into or migrates the
KitForge library. Tags/notes/favorite status belong to the library entry; `.syx`
exports contain only the patch.
To change an archived patch's metadata, load it locally, edit its tags/notes/
favorite status, then archive the current patch again.

## Build and checks

### XT-only build and distribution

From this repository root, use the dedicated preset:

```sh
cmake --preset vsynthxt
cmake --build --preset vsynthxt -j6
ctest --preset vsynthxt
cpack --config build-xt/CPackConfig.cmake -B build-xt/packages
```

Artifacts are in
`build-xt/plugin/vsynthxt/VSynthXT_artefacts/Release/{VST3,AU,Standalone}`.
The XT archive in `build-xt/packages` contains only those XT bundles, the XT
readme and license, separated into format folders. No KitForge bundle, LXR
schema/asset, test executable, library database or captured patch is shipped.
Extract/run the standalone without installing KitForge; plug-in installation,
if desired, is a separate user action for the format/host you choose.

For explicit local staging (not system/user plug-in installation):
`cmake --install build-xt --component VSynthXT --prefix "$PWD/build-xt/stage"`.
In a combined build, the `VSynthXT` and `KitForge` components remain independently
selectable, and CPack produces separate product archives. The `kitforge` preset
provides the equivalent LXR-only build in `build-lxr`.

macOS bundles receive an ad-hoc development signature after their resources have
been generated; they are not notarized distribution packages. Building does not
install plug-ins or launch the standalone app.

The preset sets all `KITFORGE_BUILD_{CORE,PLUGIN,TESTS}` options OFF and
`VSYNTHXT_BUILD_{CORE,PLUGIN,TESTS}` ON. These switches can also be passed
individually to CMake. `VSYNTHXT_BUILD_TESTS=OFF` omits test dependencies and
executables for a release-only build. Core targets can be built without JUCE by
disabling each product's plug-in switch. The default unqualified build preserves
both products and existing KitForge behavior.

**Remaining shared build-time sources:** the repository's CMake entry point and
packaging helper, a device-neutral header-only MIDI SysEx assembler, JUCE,
SQLite (system library or amalgamation), and Catch2/nlohmann JSON/Python for
tests/schema checks. XT does not compile/link `kitforge_core`, include LXR
headers, or require its firmware tables, fixtures or runtime assets. SQLite
linkage is generic, but XT and KitForge still use separate database paths and
unchanged product IDs/settings/state/automation. This is independent
build/distribution from one repository, not a newly created repository.

The tests cover independent Roland message vectors, block-schema consistency,
address carries, corruption, fragments/realtime bytes, raw snapshot/library
round trips, DAW recall, initial editor rendering, cancellation, stopped-host
expiry and a fake XT's full fetch/send/readback. These are **not hardware tests**.
`vsynthxt_smoke` instantiates the shared processor/editor directly, not the
VST3 bundle. It also checks malformed XML, unsupported versions and corrupt
patch/A-B encodings: errors retain the prior local state and cancel queued
fake host transfers without MIDI output.

`vsynthxt_vst3_host` **loads the actual built VST3 bundle** using a small JUCE
test host compiled from existing dependencies. It checks the 4,975 XT parameter
names/order (separately from wrapper bypass/MIDI-controller proxies), complete
fixture loading, type-independent coarse automation's exact patch-byte change,
state/raw-byte recall into a second instance, 48/44.1 kHz processing at small
block sizes, silent audio/MIDI on construction and recall, and editor
creation/rendering. Its synthetic state does not contain MIDI routing, and it
does not open an audio device, send notes, configure real MIDI ports, or install
the bundle. Run it with `ctest --test-dir build-xt -R '^vsynthxt_vst3_host$' -V`.
This focused host check is **not** a comprehensive SDK/pluginval conformance
suite, a standalone live UI Fetch test, AU validation, or Logic/Ableton testing.

The smoke and VST3 host tests use build-directory databases rather than your
personal library.
For explicitly authorized hardware diagnostics, the same executable supports
`--list-midi` (enumeration only) and
`--fetch-native inputID outputID deviceID newBackup.syx` (RQ1 reads only).
Quote endpoint identifiers containing spaces. The latter requires the XT to be
in native mode and refuses to replace an existing backup file.
`--verify-temporary inputID outputID deviceID backup.syx newReadback.syx newAudit.syx`
is a **writing diagnostic**, not part of the default test run. Use it only with
explicit hardware authorization: it first verifies the current temporary patch
equals the backup, sends only that snapshot to the temporary buffer, then reads
it back. It records exact submitted direct-route frames in a bounded audit.
`VSYNTHXT_TEST_SNAPSHOT=/path/to/backup.syx` makes the offline smoke test exercise
a real captured snapshot using its fake device; it does not open hardware ports.

## Protocol references and remaining hardware gate

`protocol.json` records decimal linear offsets/sizes, initial parameter ranges
and source pages from Roland's [MIDI Implementation (2005-03-24,
v1.00)](https://static.roland.com/assets/media/pdf/V-SynthXT_MI.pdf).
The implementation uses Roland `41 / 00 53`, RQ1 `11`, DT1 `12`, four base-128
address digits and Roland's address/data checksum, not LXR SysEx.

Each documented block is requested separately and reconstructed by address.
Requests are serialized; outgoing writes include DIN wire time and at least a
25 ms gap. Wrong-device/model traffic is ignored; malformed matching replies,
conflicting duplicates, incomplete transfers and readback mismatches are errors.

Before calling this hardware-verified, complete these checks with a backed-up
patch and user authorization:

- Complete USB fetch, local archive/export/reimport and explicit temporary
  send/readback; confirm the sound with matching project/waves.
- Repeat over the MOTU DIN route; interrupt/reconnect without stale writes.
- Confirm actual native-mode block behavior and whether readback contains any
  device-updated fields. Investigate discrepancies rather than masking them.
- Install the new bundles explicitly, validate the AU with
  `auval -strict -v aumu Vsxt Twld`, and verify Logic/Ableton discovery and recall
  with no automatic patch send.

The broader native editor and persistent slot management remain behind this
round-trip gate. No hardware verification is implied by a successful build.

The first authorized USB hardware check successfully fetched all 103 documented
native part-1 blocks from a Roland V-Synth XT at device ID 17. The resulting
10,302-byte `.syx` snapshot passed checksum/address/completeness validation and
was reopened from disk with exact patch-byte equality. This check sent only
RQ1 requests: no DT1 edits, program changes or slot writes.

The subsequent explicitly authorized **USB temporary-buffer round trip passed**:
preflight fetched all blocks and matched the backup before any writes; 103 DT1
messages then submitted exactly the backup bytes to part-1 temporary memory;
post-fetch received all 103 blocks and matched every byte.

| Submitted messages | Count | Bytes including framing |
|---|---:|---:|
| Preflight RQ1 | 103 | 1,648 |
| Temporary DT1 | 103 | 10,302 (9,066 payload bytes) |
| Verification RQ1 | 103 | 1,648 |
| Total | 309 | 13,598 |

The independent audit verified every checksum, model `00 53`, device wire ID
`10`, and temporary address prefix `10 00`. DT1 block starts were
`00/02/04/06/08/09/0A`, `10..1F`, `20/22/..3E`, `40..4F`, `50..5F`, `60..6F`,
and `70..7F` in the third address digit (fourth digit `00`).
No system/user-slot writes, program changes or unrelated messages were submitted.
The audit records submitted MIDI bytes, not a physical-wire analyzer trace;
successful post-fetch establishes that the patch contents matched.

Audible recall and MOTU DIN verification remain unverified. The user chose to
leave the builds in the worktree without installing them, so AU host registration/
`auval` and Logic/Ableton discovery also remain unverified. No additional hardware
writes are implied or authorized by native page expansion.
