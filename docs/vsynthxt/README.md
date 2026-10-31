# V-Synth XT Companion

An independent hardware editor and patch librarian, built alongside KitForge as
**V-Synth XT Companion** (VST3, AU and standalone). It does not emulate the XT.
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
user-slot browsing, algorithm-specific effect/COSM parameter labeling, VC-1/VC-2, sample/project
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
values 128-144 remain numeric codes. COSM/effect numbered parameters use the
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

Use the repository's normal CMake build. JUCE is fetched once for both products.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target VSynthXT_VST3 VSynthXT_AU VSynthXT_Standalone vsynthxt_tests VSynthXTSmokeTest -j6
ctest --test-dir build -R 'XT|vsynthxt' --output-on-failure
```

Artifacts are in
`build/plugin/vsynthxt/VSynthXT_artefacts/Release/{VST3,AU,Standalone}`.
macOS bundles receive an ad-hoc development signature after their resources have
been generated; they are not notarized distribution packages. Building does not
install plug-ins or launch the standalone app.

`VSYNTHXT_BUILD_PLUGIN` enables this product independently.
Its default follows `KITFORGE_BUILD_PLUGIN`, preserving the existing core-only
build command. To build only the XT plug-in, configure with
`-DKITFORGE_BUILD_PLUGIN=OFF -DVSYNTHXT_BUILD_PLUGIN=ON`.

The tests cover independent Roland message vectors, block-schema consistency,
address carries, corruption, fragments/realtime bytes, raw snapshot/library
round trips, DAW recall, initial editor rendering, cancellation, stopped-host
expiry and a fake XT's full fetch/send/readback. These are **not hardware tests**.
The smoke test uses a build-directory database rather than your personal library.
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
