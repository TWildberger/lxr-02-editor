#!/usr/bin/env python3
"""Generate the parameter table for the LXR-02 editor from the firmware source.

Usage:
    tools/gen_params.py <firmware-repo-root> [--vendored docs/firmware/params.json] [--out-json docs/editor-params.json]

The vendored params.json (from the TWildberger/lxr-02-firmware fork, see docs/firmware/README.md) is
authoritative for ids, symbols, ranges, MIDI numbers and modulation targets. The firmware source only
supplies editor presentation (labels, sections, ordering) and init-kit defaults. Any disagreement that
is not listed in VENDORED_OVERRIDES aborts generation.
                        [--out-cpp core/src/generated/ParamTable.gen.cpp]
                        [--out-h core/include/kitforge/ParamIds.gen.h]

The firmware (GPL-3.0) is the single source of truth for:
  * parameter indices        Core/Inc/Parameters.h          (enum ParamEnums)
  * data types / ranges      Core/Src/menu/menu.c           (parameter_dtypes[])
  * value name tables        Core/Inc/menu/MenuText.h       (fxNames, filterTypes, ...)
  * on-device page layout    Core/Inc/menu/menuPages.h      (menuPages[][])
  * modulation targets       Core/Src/menu/Cc2Text.c        (modTargets[], modTargetVoiceOffsets[])
  * init kit defaults        Core/Src/Preset/initKit.c      (initKit[])

The generated files are checked in so that building the editor does not
require the firmware source. Re-run this script when the firmware changes.
"""
import argparse
import json
import os
import re
import sys

# --------------------------------------------------------------------------
# small C parsing helpers


def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    return re.sub(r"//[^\n]*", "", s)


def strip_disabled_ifs(s, defined=()):
    """Remove `#if X ... #endif` blocks whose condition is a plain identifier
    that is not in `defined` (undefined macros evaluate to 0 in C)."""
    out, stack = [], []
    for line in s.split("\n"):
        m = re.match(r"\s*#\s*if\s+(\w+)\s*$", line)
        if m:
            stack.append(m.group(1) in defined)
            continue
        if re.match(r"\s*#\s*else\b", line) and stack:
            stack[-1] = not stack[-1]
            continue
        if re.match(r"\s*#\s*endif\b", line) and stack:
            stack.pop()
            continue
        if all(stack):
            out.append(line)
    return "\n".join(out)


def block_after(s, marker, open_ch="{", close_ch="}"):
    i = s.index(marker)
    i = s.index(open_ch, i)
    depth = 0
    for j in range(i, len(s)):
        if s[j] == open_ch:
            depth += 1
        elif s[j] == close_ch:
            depth -= 1
            if depth == 0:
                return s[i + 1 : j]
    raise ValueError("unbalanced block after " + marker)


def parse_enum(src, marker):
    body = strip_comments(strip_disabled_ifs(block_after(src, marker)))
    names, order, val = {}, [], -1
    for tok in [t.strip() for t in body.split(",")]:
        if not tok:
            continue
        if "=" in tok:
            n, v = [x.strip() for x in tok.split("=", 1)]
            val = names[v] if v in names else int(v, 0)
        else:
            n = tok
            val += 1
        names[n] = val
        order.append(n)
    return names, order


def split_top_level(body):
    items, depth, cur = [], 0, ""
    for ch in body:
        if ch in "{(":
            depth += 1
        elif ch in "})":
            depth -= 1
        if ch == "," and depth == 0:
            items.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        items.append(cur.strip())
    return items


def parse_string_table(src, name):
    body = strip_comments(block_after(src, "const char " + name + "[]"))
    entries = split_top_level(body)
    count = int(entries[0].strip("{} "), 0)
    strs = [re.search(r'"(.*)"', e).group(1) for e in entries[1:]]
    return strs[:count]


# --------------------------------------------------------------------------
# curated presentation data (labels, sections) - everything else is derived

VOICES = ["Kit", "Drum 1", "Drum 2", "Drum 3", "Snare/Clap", "Cymbal", "Hi-Hat"]
VOICE_SHORT = ["Kit", "D1", "D2", "D3", "SN", "CY", "HH"]
VOICE_SECTIONS = ["Osc", "Amp EG", "Mod", "FM", "Transient", "Filter", "LFO", "Mix"]

TEXT_LABELS = {
    "TEXT_ATTACK": "Attack", "TEXT_AUDIO_OUT": "Output", "TEXT_BPM": "Tempo",
    "TEXT_COARSE": "Coarse", "TEXT_DECAY": "Decay", "TEXT_DECAY_CLOSED": "Decay Closed",
    "TEXT_DECAY_OPEN": "Decay Open", "TEXT_DRIVE": "Drive", "TEXT_EG_SLOPE": "Slope",
    "TEXT_FILTER_DRIVE": "Filter Drive", "TEXT_FILTER_F": "Cutoff", "TEXT_FILTER_RESO": "Resonance",
    "TEXT_FILTER_TYPE": "Filter Type", "TEXT_FINE": "Fine", "TEXT_FM_AMOUNT": "FM Amount",
    "TEXT_FM_FREQ": "FM Freq", "TEXT_FOLLOW": "Follow", "TEXT_FREQ_LFO": "LFO Rate",
    "TEXT_FX_COLOR": "Color", "TEXT_FX_COMP_ATTACK": "Attack", "TEXT_FX_COMP_DECAY": "Decay",
    "TEXT_FX_COMP_THRESHOLD": "Threshold", "TEXT_FX_DIST_DRIVE": "Drive", "TEXT_FX_DRY_WET": "Dry/Wet",
    "TEXT_FX_FEEDBACK": "Feedback", "TEXT_FX_FREQ": "Frequency", "TEXT_FX_GAIN": "Gain",
    "TEXT_FX_PREDELAY": "Pre-delay", "TEXT_FX_RANGE": "Range", "TEXT_FX_RATIO": "Ratio",
    "TEXT_FX_REFLECTION": "Reflection", "TEXT_FX_ROUTING": "FX Output", "TEXT_FX_TIME": "Time",
    "TEXT_FX_TONE": "Tone", "TEXT_FX_TYPE": "FX Type", "TEXT_FX_VOLUME": "Volume", "TEXT_FX_WAVE": "Wave",
    "TEXT_MIDI_CHANNEL": "MIDI Channel", "TEXT_MIDI_CHAN_GLOBAL": "Global MIDI Channel",
    "TEXT_MIDI_FILT_RX": "MIDI RX Filter", "TEXT_MIDI_FILT_TX": "MIDI TX Filter",
    "TEXT_MIDI_ROUTING": "MIDI Routing", "TEXT_MIX": "Noise/Osc Mix", "TEXT_MODE": "FM/Mix Mode",
    "TEXT_MOD_AMOUNT": "Pitch Mod Amount", "TEXT_MOD_LFO": "LFO Amount",
    "TEXT_MOD_OSC1_FREQ": "Mod Osc 1 Freq", "TEXT_MOD_OSC1_GAIN": "Mod Osc 1 Gain",
    "TEXT_MOD_OSC2_FREQ": "Mod Osc 2 Freq", "TEXT_MOD_OSC2_GAIN": "Mod Osc 2 Gain",
    "TEXT_NOISE": "Noise Freq", "TEXT_NOTE": "MIDI Note", "TEXT_OFFSET_LFO": "LFO Phase",
    "TEXT_PAN": "Pan", "TEXT_PATTERN_MODE": "Kit Change Mode", "TEXT_PITCH_DECAY": "Pitch Decay",
    "TEXT_PWM": "Pulse Width", "TEXT_REPEAT": "Repeat", "TEXT_RETRIGGER_LFO": "LFO Retrigger",
    "TEXT_ROLL_SPEED": "Roll Rate", "TEXT_SAMPLE_RATE": "Sample Rate",
    "TEXT_SCREENSAVER_ON_OFF": "Screensaver", "TEXT_SHUFFLE": "Shuffle", "TEXT_SYNC_LFO": "LFO Sync",
    "TEXT_TARGET_LFO": "LFO Target", "TEXT_TARGET_VOICE_LFO": "LFO Target Voice",
    "TEXT_TRANSIENT_FREQ": "Transient Freq", "TEXT_TRANSIENT_VOLUME": "Transient Volume",
    "TEXT_TRANSIENT_WAVE": "Transient Wave", "TEXT_TRIGGER_IN_PPQ": "Clock In PPQ",
    "TEXT_TRIGGER_OUT1_PPQ": "Clock Out PPQ", "TEXT_VEL_AMT": "Velocity Amount",
    "TEXT_VEL_DEST": "Velocity Target", "TEXT_VEL_MOD_VOL": "Velocity -> Volume",
    "TEXT_VOLUME": "Volume", "TEXT_WAVE_LFO": "LFO Wave", "TEXT_WAVEFORM": "Waveform",
    "TEXT_X_FADE": "Morph", "TEXT_SAMPLE_RATE_ALL": "Sample Rate (all)",
}

# Mod-osc waveforms on cymbal/hat pages both use TEXT_WAVEFORM; disambiguate.
PARAM_LABEL_OVERRIDES = {
    "PAR_MOD_WAVE_DRUM1": "FM Waveform", "PAR_MOD_WAVE_DRUM2": "FM Waveform",
    "PAR_MOD_WAVE_DRUM3": "FM Waveform",
    "PAR_WAVE2_CYM": "Mod Osc 1 Wave", "PAR_WAVE3_CYM": "Mod Osc 2 Wave",
    "PAR_WAVE2_HH": "Mod Osc 1 Wave", "PAR_WAVE3_HH": "Mod Osc 2 Wave",
    "PAR_FX_P1": "FX Param 1", "PAR_FX_P2": "FX Param 2", "PAR_FX_P3": "FX Param 3",
    "PAR_FX_P4": "FX Param 4", "PAR_FX_DIST_TYPE": "Distortion Type",
    "PAR_FX_DELAY_TYPE": "Delay Type", "PAR_FX_REVERB_TYPE": "Reverb Type",
    "PAR_VELOD6_CLOSED": "Decay Closed", "PAR_VELOD6_OPEN": "Decay Open",
    "PAR_VOICE_DECIMATION_ALL": "Sample Rate (all)", "PAR_MORPH_KIT_DEST": "Morph Kit",
    "PAR_MIDI_CHAN_7": "MIDI Channel (Open HH)", "PAR_MIDI_NOTE7": "MIDI Note (Open HH)",
    "PAR_PRESCALER_CLOCK_OUT2": "Clock Out 2 PPQ", "PAR_TRIG_GATE_MODE": "Trigger Gate Mode",
    "PAR_BAR_RESET_MODE": "Bar Reset on PC", "PAR_FETCH": "Fetch", "PAR_QUANTISATION": "Quantisation",
    "PAR_MIDI_MODE": "MIDI Mode",
}

for _v in range(1, 5):
    PARAM_LABEL_OVERRIDES[f"PAR_PITCH_SLOPE{_v}"] = "Pitch Slope"
    PARAM_LABEL_OVERRIDES[f"PAR_MODAMNT{_v}"] = "Pitch Amount"

# Pseudo-parameters that occupy an index but are not sound parameters.
PSEUDO = {
    "PAR_NONE": "unused slot (CC1 mod wheel)",
    "PAR_NRPN_DATA_ENTRY_COARSE": "MIDI CC6 NRPN data entry",
    "PAR_NRPN_FINE": "MIDI CC98 NRPN LSB",
    "PAR_NRPN_COARSE": "MIDI CC99 NRPN MSB",
    "PAR_BANK_CHANGE": "bank change (kit select)",
}

GLOBAL_DEFAULTS = {
    "PAR_BPM": 120, "PAR_MIDI_NOTE1": 48, "PAR_MIDI_NOTE2": 48, "PAR_MIDI_NOTE3": 48,
    "PAR_MIDI_NOTE4": 48, "PAR_MIDI_NOTE5": 48, "PAR_MIDI_NOTE6": 48, "PAR_MIDI_NOTE7": 48,
    "PAR_MIDI_CHAN_1": 1, "PAR_MIDI_CHAN_2": 1, "PAR_MIDI_CHAN_3": 1, "PAR_MIDI_CHAN_4": 1,
    "PAR_MIDI_CHAN_5": 1, "PAR_MIDI_CHAN_6": 1, "PAR_MIDI_CHAN_7": 1,
    "PAR_SCREENSAVER_ON_OFF": 1, "PAR_MIDI_FILT_TX": 15, "PAR_MIDI_FILT_RX": 15,
    "PAR_BAR_RESET_MODE": 1, "PAR_MIDI_CHAN_GLOBAL": 0, "PAR_PATTERN_MODE": 1,
}

DTYPE_MAP = {
    "DTYPE_0B255": ("u0_255", 0, 255), "DTYPE_0B127": ("u0_127", 0, 127),
    "DTYPE_PM100": ("pm100", 0, 200), "DTYPE_MENU": ("menu", 0, 0),
    "DTYPE_PM63": ("pm63", 0, 127), "DTYPE_1B16": ("u1_16", 1, 16),
    "DTYPE_ON_OFF": ("on_off", 0, 1), "DTYPE_0b1": ("on_off", 0, 1),
    "DTYPE_TARGET_SELECTION_LFO": ("lfo_target", 0, 228),
    "DTYPE_TARGET_SELECTION_VELO": ("velo_target", 0, 228),
    "DTYPE_VOICE_LFO": ("voice_lfo", 1, 6), "DTYPE_AUTOM_TARGET": ("autom_target", 0, 228),
    "DTYPE_NOTE_NAME": ("note", 0, 127), "DTYPE_0B15": ("u0_15", 0, 15),
    "DTYPE_1B64": ("u1_64", 1, 64), "DTYPE_0B100": ("u0_100", 0, 100),
}

MENU_TABLES = {
    "MENU_FILTER": "filterTypes", "MENU_WAVEFORM": "waveformNames", "MENU_AUDIO_OUT": "outputNames",
    "MENU_ROLL_RATES": "rollRateNames", "MENU_SYNC_RATES": "syncRateNames",
    "MENU_LFO_WAVES": "lfoWaveNames", "MENU_RETRIGGER": "retriggerNames",
    "MENU_NEXT_PATTERN": "nextPatternNames", "MENU_SEQ_QUANT": "quantisationNames",
    "MENU_TRANS": "transientNames", "MENU_MIDI": "midiModes", "MENU_MIDI_ROUTING": "midiRoutingNames",
    "MENU_MIDI_FILTERING": "midiFilterNames", "MENU_PPQ_IN": "ppqInNames", "MENU_MIX_FM": "mixFmNames",
    "MENU_PPQ_OUT": "ppqOutNames", "MENU_FOLLOW": "followNames", "MENU_FX": "fxNames",
    "MENU_DIST": "distNames", "MENU_RATIO": "ratioNames", "MENU_DELAY": "delayNames",
    "MENU_REVERB": "reverbNames", "MENU_PAT_MODE": "patModeNames", "MENU_STEP_FLAM": "stepFlamNames",
    "MENU_STEP_SHIFT": "stepShiftNames", "MENU_AUDIO_OUT_FX": "outputNamesFx",
}

# Waveform menus append user samples ("s0", "s1", ...) after the built-in waves.
MAX_USER_SAMPLES = 64

# menu.c displays these PM63 params as (value - 60); all other PM63 params as (value - 63)
PITCH_PM63 = {
    "PAR_COARSE1", "PAR_COARSE2", "PAR_COARSE3", "PAR_COARSE4", "PAR_COARSE5", "PAR_COARSE6",
    "PAR_FM_FREQ1", "PAR_FM_FREQ2", "PAR_FM_FREQ3", "PAR_MOD_OSC_F1_CYM", "PAR_MOD_OSC_F2_CYM",
    "PAR_MOD_OSC_F1", "PAR_MOD_OSC_F2",
}

FX_TYPE_PAGES = ["off", "drv", "rng", "cmp", "del", "rev"]


# Deliberate, documented deviations from docs/firmware/params.json.
VENDORED_OVERRIDES = {
    "max": {
        # the editor exposes the 64 user-sample slots (s0..s63); the device clamps to the samples it has
        **{n: "user samples s0..s63" for n in (
            "PAR_OSC_WAVE_DRUM1", "PAR_OSC_WAVE_DRUM2", "PAR_OSC_WAVE_DRUM3", "PAR_OSC_WAVE_SNARE",
            "PAR_WAVE1_CYM", "PAR_WAVE1_HH", "PAR_MOD_WAVE_DRUM1", "PAR_MOD_WAVE_DRUM2", "PAR_MOD_WAVE_DRUM3",
            "PAR_WAVE2_CYM", "PAR_WAVE3_CYM", "PAR_WAVE2_HH", "PAR_WAVE3_HH", "PAR_FX_WAVE")},
    },
    "min": {
        "PAR_MIDI_CHAN_GLOBAL": "0 = unset/all, the value presetManager initialises",
    },
    # the stock enum aliases id 1 as PAR_MOD_WHEEL; the editor uses the descriptive name
    "symbol": {"PAR_OSC_WAVE_DRUM1": "PAR_MOD_WHEEL"},
}

# Globals added by the fork (ids 302..304); defaults per docs/firmware/file-formats.md
FORK_GLOBALS = {
    "PAR_SYSEX_ENABLE": ("SysEx", 1),
    "PAR_SYSEX_DEVICE_ID": ("Device ID", 1),
    "PAR_SYSEX_ECHO": ("Param Echo", 0),
}


def apply_vendored(params, vendored_path, mod_targets_doc):
    v = json.load(open(vendored_path))
    vp = {p["id"]: p for p in v["params"]}
    errors = []
    by_id = {p["index"]: p for p in params}
    next_order = max(p.get("order", 0) for p in params) + 1
    for i in range(v["counts"]["NUM_PARAMS"]):
        f = vp[i]
        m = by_id.get(i)
        if m is None:
            sym = f["symbol"]
            if sym not in FORK_GLOBALS or f["scope"] != "global":
                errors.append(f"{i} {sym}: unknown parameter in vendored table")
                continue
            kind, lo, hi = DTYPE_MAP["DTYPE_" + f["dtype"]]
            label, default = FORK_GLOBALS[sym]
            m = {"index": i, "id": sym, "key": sym[4:].lower(), "scope": "global", "dtype": kind,
                 "min": f["min"], "max": f["max"], "default": default, "voice": 0, "section": "MIDI",
                 "label": label, "order": next_order, "midi": {"kind": "none"},
                 "note": "Extended firmware only (TWildberger/lxr-02-firmware)"}
            next_order += 1
            params.append(m)
            by_id[i] = m
            continue
        if f["symbol"] != m["id"] and VENDORED_OVERRIDES["symbol"].get(m["id"]) != f["symbol"]:
            errors.append(f"{i}: symbol {m['id']} != {f['symbol']}")
        for field in ("min", "max"):
            if f[field] != m[field]:
                if m["id"] in VENDORED_OVERRIDES[field]:
                    continue
                if m["scope"] not in ("sound", "global"):
                    m[field] = f[field]  # sequencer/reserved: just adopt
                    continue
                errors.append(f"{i} {m['id']}: {field} {m[field]} != vendored {f[field]}")
        fm = f["midi"]
        fk = (fm["type"], fm["number"]) if fm else ("none", 0)
        mk = (m["midi"]["kind"], m["midi"].get("number", 0))
        if fk != mk:
            errors.append(f"{i} {m['id']}: midi {mk} != vendored {fk}")
        if f.get("display_offset") is not None and m["dtype"] == "pm63" and -f["display_offset"] != m.get("displayOffset"):
            errors.append(f"{i} {m['id']}: display offset {m.get('displayOffset')} != vendored {-f['display_offset']}")
    vt = v["mod_targets"]
    if len(vt) != len(mod_targets_doc):
        errors.append(f"mod target count {len(mod_targets_doc)} != vendored {len(vt)}")
    for a, b in zip(vt, mod_targets_doc):
        if a["param"] != b["param"] or (a["voice"] or 0) != b["voice"]:
            errors.append(f"mod target {a['index']}: ({b['param']},{b['voice']}) != vendored ({a['param']},{a['voice']})")
    vr = [(r["start"], r["end"]) for r in v["mod_target_voice_ranges"]]
    if errors:
        sys.exit("vendored params.json disagrees with the firmware source:\n  " + "\n  ".join(errors))
    params.sort(key=lambda p: p["index"])
    return v, vr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fw")
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap.add_argument("--vendored", default=os.path.join(root, "docs/firmware/params.json"))
    ap.add_argument("--out-json", default=os.path.join(root, "docs/editor-params.json"))
    ap.add_argument("--out-cpp", default=os.path.join(root, "core/src/generated/ParamTable.gen.cpp"))
    ap.add_argument("--out-h", default=os.path.join(root, "core/include/kitforge/ParamIds.gen.h"))
    a = ap.parse_args()
    core = os.path.join(a.fw, "Core")
    rd = lambda p: open(os.path.join(core, p), encoding="latin-1").read()

    params_h = rd("Inc/Parameters.h")
    menu_c = rd("Src/menu/menu.c")
    menu_text = rd("Inc/menu/MenuText.h")
    menu_pages = rd("Inc/menu/menuPages.h")
    menu_h = rd("Inc/menu/menu.h")
    cc2text = rd("Src/menu/Cc2Text.c")
    init_kit = rd("Src/Preset/initKit.c")

    pvals, porder = parse_enum(params_h, "enum ParamEnums")
    num_params = pvals["NUM_PARAMS"]
    end_sound = pvals["END_OF_SOUND_PARAMETERS"]
    first_global = pvals["PAR_BEGINNING_OF_GLOBALS"]
    by_index = {}
    for n in porder:
        if n in ("NUM_PARAMS", "END_OF_SOUND_PARAMETERS", "PAR_BEGINNING_OF_GLOBALS", "PAR_MOD_WHEEL"):
            continue
        by_index.setdefault(pvals[n], n)

    menu_ids = {m.group(1): int(m.group(2)) for m in re.finditer(r"#define\s+(MENU_\w+)\s+(\d+)", menu_text)}
    menus = {}
    for mname, tname in MENU_TABLES.items():
        menus[mname] = parse_string_table(menu_text, tname)

    # parameter_dtypes[] is positional, one entry per ParamEnums value
    dt_body = strip_comments(block_after(menu_c, "parameter_dtypes[NUM_PARAMS]"))
    dtypes = [d for d in split_top_level(dt_body) if d]
    if len(dtypes) != num_params:
        sys.exit(f"parameter_dtypes has {len(dtypes)} entries, expected {num_params}")

    texts, _ = parse_enum(menu_h, "enum NamesEnum")

    # menuPages: list of pages; each page list of subpages of (texts[8], params[8])
    pages_src = strip_comments(menu_pages)
    pages_body = block_after(pages_src, "menuPages[NUM_PAGES][NUM_SUB_PAGES]")
    page_names, _ = parse_enum(menu_h, "enum PageNames")
    page_by_idx = {v: k for k, v in page_names.items()}
    pages = []
    for pi, pg in enumerate(split_top_level(pages_body)):
        pg = pg.strip()
        if not pg.startswith("{"):
            continue
        subs = []
        for sp in split_top_level(pg.strip()[1:-1]):
            toks = [t.strip() for t in sp.strip().strip("{}").split(",") if t.strip()]
            t = [x for x in toks if x.startswith("TEXT_")]
            p = [x for x in toks if x.startswith("PAR_")]
            subs.append(list(zip(t, p)))
        pages.append(subs)

    # mod targets
    mt_body = strip_comments(block_after(cc2text, "const ModTarg modTargets[]"))
    mod_targets = []
    for e in split_top_level(mt_body):
        m = re.match(r"\{\s*(\w+)\s*,\s*(\w+)\s*\}", e.strip())
        if m:
            mod_targets.append((m.group(1), m.group(2)))
    off_body = strip_comments(block_after(cc2text, "modTargetVoiceOffsets[6]"))
    voice_offsets = [tuple(int(x) for x in re.findall(r"\d+", e)) for e in split_top_level(off_body) if e.strip()]

    kit_bytes = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", block_after(init_kit, "initKit[INIT_KIT_SIZE]"))]
    # initKit[] may be shorter than a full kit (the firmware zero-fills the rest, like a short .snd file)
    if len(kit_bytes) > 8 + end_sound:
        sys.exit(f"initKit has {len(kit_bytes)} bytes, expected <= {8 + end_sound}")
    kit_bytes += [0] * (8 + end_sound - len(kit_bytes))

    # ------------------------------------------------------------------
    # build placement (voice, section, label, order) from the on-device pages
    placement = {}

    def place(pname, voice, section, text, order):
        if pname == "PAR_NONE" or pname in placement:
            return
        label = PARAM_LABEL_OVERRIDES.get(pname) or TEXT_LABELS.get(text) or text.replace("TEXT_", "").title()
        placement[pname] = dict(voice=voice, section=section, label=label, order=order)

    order = 0
    for pi in range(0, 7):  # VOICE1_PAGE..VOICE7_PAGE (7 = open hi-hat)
        voice = min(pi + 1, 6)
        for si, sub in enumerate(pages[pi]):
            for text, pname in sub:
                if text in ("TEXT_EMPTY", "TEXT_SKIP"):
                    continue
                order += 1
                if pvals[pname] >= first_global:
                    place(pname, voice, "MIDI", text, order)
                elif pvals[pname] >= end_sound:
                    continue  # sequencer-only (e.g. track length)
                else:
                    place(pname, voice, VOICE_SECTIONS[si], text, order)
    fx_page = pages[page_names["FX_MODE_PAGE"]]
    fx_labels = {}
    for si, sub in enumerate(fx_page):
        if si >= len(FX_TYPE_PAGES):
            break
        labels = {}
        for text, pname in sub:
            if text in ("TEXT_EMPTY", "TEXT_SKIP"):
                continue
            order += 1
            place(pname, 0, "FX", text, order)
            if pname.startswith("PAR_FX_P"):
                labels[pname] = TEXT_LABELS.get(text, text)
            elif pname.startswith("PAR_FX_"):
                labels[pname] = PARAM_LABEL_OVERRIDES.get(pname) or TEXT_LABELS.get(text, text)
        fx_labels[FX_TYPE_PAGES[si]] = labels
    for pidx in (page_names["MENU_MIDI_PAGE"], page_names["PERF_PAGE"]):
        for sub in pages[pidx]:
            for text, pname in sub:
                if text in ("TEXT_EMPTY", "TEXT_SKIP"):
                    continue
                order += 1
                v = pvals[pname]
                if v >= first_global:
                    place(pname, 0, "Global", text, order)
                elif v < end_sound:
                    place(pname, 0, "Mix", "TEXT_SAMPLE_RATE_ALL" if pname == "PAR_VOICE_DECIMATION_ALL" else text, order)

    # remaining (unpaged) sound/global params
    for idx in range(num_params):
        n = by_index.get(idx)
        if not n or n in placement:
            continue
        order += 1
        if n == "PAR_VELOD6_CLOSED":
            placement[n] = dict(voice=6, section="Amp EG", label="Decay Closed", order=placement["PAR_VELOD6_OPEN"]["order"] - 0.5)
        elif n == "PAR_MORPH_KIT_DEST":
            placement[n] = dict(voice=0, section="Mix", label="Morph Kit", order=order)
        elif idx >= first_global:
            placement[n] = dict(voice=0, section="Global", label=PARAM_LABEL_OVERRIDES.get(n, n[4:].replace("_", " ").title()), order=order)

    # ------------------------------------------------------------------
    params = []
    for idx in range(num_params):
        n = by_index.get(idx, f"PAR_UNNAMED_{idx}")
        d = dtypes[idx]
        m = re.search(r"(DTYPE_\w+)(?:\s*\|\s*\(\s*(MENU_\w+)\s*<<\s*8\s*\))?", d)
        dname, menu = m.group(1), m.group(2)
        kind, lo, hi = DTYPE_MAP[dname]
        names = None
        if menu:
            names = list(menus[menu])
            if menu == "MENU_WAVEFORM":
                names += [f"s{i}" for i in range(MAX_USER_SAMPLES)]
            hi = len(names) - 1
        if kind in ("lfo_target", "velo_target", "autom_target"):
            hi = len(mod_targets) - 1
        if idx < end_sound:
            scope = "sound"
        elif idx < first_global:
            scope = "sequencer"
        else:
            scope = "global"
        if n in PSEUDO or n.startswith("PAR_UNUSED") or n.startswith("UNUSED_"):
            scope = "reserved" if idx < end_sound else scope
        pl = placement.get(n)
        if scope == "reserved" or scope == "sequencer":
            pl = None

        # stock-firmware MIDI transport
        midi = {"kind": "none"}
        limited = None
        if scope == "sound":
            if idx < 127:
                midi = {"kind": "cc", "number": idx + 1}
            else:
                midi = {"kind": "nrpn", "number": idx - 128}
            if kind in ("lfo_target", "velo_target"):
                limited = ("Stock firmware: NRPN value is a raw parameter number < 128; "
                           "targets >= 128 (filter drive, transient, FX params...) cannot be set live. "
                           "Use .snd export or extended mode.")
            elif kind == "voice_lfo":
                limited = "Stock firmware ignores this over MIDI; use .snd export or extended mode."
            elif n == "PAR_MORPH_KIT_DEST":
                limited = "Not applied live by the stock firmware."
            if idx in (119, 120, 121, 122, 123, 124, 125, 126):
                note = f"CC{idx + 1} is a MIDI channel-mode number; hosts may send it on transport stop."
                limited = (limited + " " if limited else "") + note
        if scope == "sound":
            default = kit_bytes[8 + idx]
        elif scope == "global":
            default = GLOBAL_DEFAULTS.get(n, 0)
        else:
            default = 0
        if scope == "reserved":
            default = kit_bytes[8 + idx]

        if n == "PAR_MIDI_CHAN_GLOBAL":
            lo = 0  # presetManager initialises it to 0 ("all"/unset)
        entry = {
            "index": idx,
            "id": n,
            "key": n[4:].lower() if n.startswith("PAR_") else n.lower(),
            "scope": scope,
            "dtype": kind,
            "min": lo,
            "max": hi,
            "default": default,
        }
        if kind == "pm63":
            entry["displayOffset"] = 60 if n in PITCH_PM63 else 63
        if menu:
            entry["menu"] = menu
            entry["menuId"] = menu_ids[menu]
            entry["valueNames"] = names
        if pl:
            entry["voice"] = pl["voice"]
            entry["section"] = pl["section"]
            entry["label"] = pl["label"]
            entry["order"] = pl["order"]
        entry["midi"] = midi
        if limited:
            entry["stockLimitation"] = limited
        if n in PSEUDO:
            entry["note"] = PSEUDO[n]
        params.append(entry)

    targets_doc = [{"param": pvals[pname], "voice": next((vi + 1 for vi, (s0, e0) in enumerate(voice_offsets) if s0 <= i <= e0), 0)}
                   for i, (_, pname) in enumerate(mod_targets)]
    vendored, vendored_ranges = apply_vendored(params, a.vendored, targets_doc)
    if vendored_ranges != [tuple(r) for r in voice_offsets]:
        sys.exit(f"mod target voice ranges differ: {voice_offsets} vs {vendored_ranges}")
    num_params = vendored["counts"]["NUM_PARAMS"]
    if (end_sound, first_global) != (vendored["counts"]["END_OF_SOUND_PARAMETERS"], vendored["counts"]["PAR_BEGINNING_OF_GLOBALS"]):
        sys.exit("kit/global boundaries differ from the vendored table")

    # stable integer ordering
    ordered = sorted([p for p in params if "order" in p], key=lambda p: p["order"])
    for i, p in enumerate(ordered):
        p["order"] = i

    label_of = {}
    for p in params:
        lab = p.get("label", p["id"])
        sec = p.get("section")
        if sec and sec not in ("FX", "Global", "MIDI", "Mod") and not lab.lower().startswith(sec.lower().split()[0]):
            lab = f"{sec} {lab}"
        label_of[p["id"]] = lab
    targets = []
    for i, (text, pname) in enumerate(mod_targets):
        voice = 0
        for vi, (s, e) in enumerate(voice_offsets):
            if s <= i <= e:
                voice = vi + 1
        lab = "none" if pname == "PAR_NONE" else label_of.get(pname, pname)
        if pname == "PAR_VOICE_DECIMATION_ALL":
            lab = "Sample Rate (all)"
        targets.append({"index": i, "param": pvals[pname], "id": pname, "voice": voice, "label": lab})

    doc = {
        "source": "Generated by tools/gen_params.py from docs/firmware/params.json (authoritative ids, ranges, MIDI, mod targets) plus editor labels/defaults from the GPL-3.0 firmware source",
        "vendoredFirmware": vendored["firmware"],
        "vendoredOverrides": VENDORED_OVERRIDES,
        "stockNumGlobals": vendored["counts"]["stock_v1_7_globals"],
        "numParams": num_params,
        "numSoundParams": end_sound,
        "firstGlobal": first_global,
        "voices": VOICES,
        "voiceShort": VOICE_SHORT,
        "sections": VOICE_SECTIONS + ["FX", "MIDI", "Global"],
        "files": {
            "snd": {"nameBytes": 8, "paramBytes": end_sound, "size": 8 + end_sound},
            "glo.cfg": {"size": num_params - first_global, "stockSize": vendored["counts"]["stock_v1_7_globals"], "firstParam": first_global},
        },
        "midi": {
            "ccRule": "CC number = param index + 1 for params 1..126 (except pseudo slots 5, 97, 98)",
            "nrpnRule": "NRPN number = param index - 128 for params 128..(numSoundParams-1); CC99 MSB, CC98 LSB, CC6 value",
            "muteNrpnBase": 1000,
            "kitSelect": "CC0 (bank select MSB) value = kit RAM slot",
            "patternSelect": "Program change on the global channel",
        },
        "fxParamLabels": fx_labels,
        "modTargets": targets,
        "modTargetVoiceRanges": [{"voice": i + 1, "first": s, "last": e} for i, (s, e) in enumerate(voice_offsets)],
        "initKit": {"name": bytes(kit_bytes[:8]).split(b"\0")[0].decode(), "bytes": kit_bytes},
        "params": params,
    }
    os.makedirs(os.path.dirname(a.out_json), exist_ok=True)
    with open(a.out_json, "w") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")

    write_header(a.out_h, params, doc)
    write_cpp(a.out_cpp, params, doc, menus, menu_ids)
    print(f"{num_params} params ({end_sound} sound, globals from {first_global}), {len(targets)} mod targets")


def cstr(s):
    return "nullptr" if s is None else json.dumps(s)


HEADER = "// GENERATED by tools/gen_params.py from the GPL-3.0 firmware source. Do not edit.\n"


def write_header(path, params, doc):
    lines = [HEADER, "#pragma once", "", "namespace kitforge::pid {", ""]
    for p in params:
        if p["id"].startswith("PAR_UNNAMED"):
            continue
        lines.append(f"constexpr int {p['id']} = {p['index']};")
    lines += ["", f"constexpr int NUM_PARAMS = {doc['numParams']};",
              f"constexpr int END_OF_SOUND_PARAMETERS = {doc['numSoundParams']};",
              f"constexpr int PAR_BEGINNING_OF_GLOBALS = {doc['firstGlobal']};",
              f"constexpr int STOCK_NUM_GLOBALS = {doc['stockNumGlobals']};",
              "", "} // namespace kitforge::pid", ""]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w").write("\n".join(lines))


DTYPE_ENUM = {
    "u0_255": "U0_255", "u0_127": "U0_127", "pm100": "PM100", "menu": "Menu", "pm63": "PM63",
    "u1_16": "U1_16", "on_off": "OnOff", "lfo_target": "LfoTarget", "velo_target": "VeloTarget",
    "voice_lfo": "VoiceLfo", "autom_target": "AutomTarget", "note": "Note", "u0_15": "U0_15",
    "u1_64": "U1_64", "u0_100": "U0_100",
}
SCOPE_ENUM = {"sound": "Sound", "sequencer": "Sequencer", "global": "Global", "reserved": "Reserved"}
MIDI_ENUM = {"none": "None", "cc": "CC", "nrpn": "NRPN"}


def write_cpp(path, params, doc, menus, menu_ids):
    L = [HEADER, '#include "kitforge/ParamTable.h"', "", "namespace kitforge {", "namespace {", ""]
    used = {p.get("menu") for p in params}
    for mname, names in menus.items():
        if mname not in used:
            continue
        full = list(names)
        if mname == "MENU_WAVEFORM":
            full += [f"s{i}" for i in range(MAX_USER_SAMPLES)]
        L.append(f"const char* const k{mname}[] = {{ {', '.join(cstr(n) for n in full)} }};")
    L.append("")
    L.append("const ParamInfo kParams[] = {")
    for p in params:
        menu = p.get("menu")
        names = f"k{menu}" if menu else "nullptr"
        nnames = len(p.get("valueNames", []))
        midi = p["midi"]
        L.append(
            "    { %d, %s, %s, %s, %s, %d, Scope::%s, DType::%s, %d, %d, %d, %d, %s, %d, MidiKind::%s, %d, %s, %d, %d },"
            % (
                p["index"], cstr(p["id"]), cstr(p["key"]), cstr(p.get("label")), cstr(p.get("section")),
                p.get("voice", -1), SCOPE_ENUM[p["scope"]], DTYPE_ENUM[p["dtype"]], p["min"], p["max"],
                p["default"], p.get("menuId", 0), names, nnames, MIDI_ENUM[midi["kind"]], midi.get("number", 0),
                cstr(p.get("stockLimitation")), p.get("order", -1), p.get("displayOffset", 0),
            )
        )
    L.append("};")
    L.append("")
    L.append("const ModTargetInfo kModTargets[] = {")
    for t in doc["modTargets"]:
        L.append(f"    {{ {t['index']}, {t['param']}, {t['voice']}, {cstr(t['label'])} }},")
    L.append("};")
    L.append("")
    L.append("const ModTargetRange kModTargetRanges[] = {")
    for r in doc["modTargetVoiceRanges"]:
        L.append(f"    {{ {r['first']}, {r['last']} }},")
    L.append("};")
    L.append("")
    kb = doc["initKit"]["bytes"]
    L.append("const unsigned char kInitKit[] = {")
    for i in range(0, len(kb), 16):
        L.append("    " + ", ".join(f"0x{b:02x}" for b in kb[i:i + 16]) + ",")
    L.append("};")
    L.append("")
    L.append("struct FxLabel { int fxType; int param; const char* label; };")
    L.append("const FxLabel kFxLabels[] = {")
    for ti, tname in enumerate(FX_TYPE_PAGES):
        for pid, lab in doc["fxParamLabels"].get(tname, {}).items():
            idx = next(p["index"] for p in params if p["id"] == pid)
            L.append(f"    {{ {ti}, {idx}, {cstr(lab)} }},")
    L.append("};")
    L += [
        "",
        "} // namespace",
        "",
        "const ParamInfo* ParamTable::all() { return kParams; }",
        "int ParamTable::size() { return (int) (sizeof(kParams) / sizeof(kParams[0])); }",
        "const ModTargetInfo* ParamTable::modTargets() { return kModTargets; }",
        "int ParamTable::numModTargets() { return (int) (sizeof(kModTargets) / sizeof(kModTargets[0])); }",
        "ModTargetRange ParamTable::modTargetRange(int voice) { return (voice >= 1 && voice <= 6) ? kModTargetRanges[voice - 1] : ModTargetRange{ 0, 0 }; }",
        "const unsigned char* ParamTable::initKitBytes() { return kInitKit; }",
        "int ParamTable::initKitSize() { return (int) sizeof(kInitKit); }",
        "const char* ParamTable::fxParamLabel(int fxType, int param)",
        "{",
        "    for (const auto& l : kFxLabels)",
        "        if (l.fxType == fxType && l.param == param)",
        "            return l.label;",
        "    return nullptr;",
        "}",
        "",
        "} // namespace kitforge",
        "",
    ]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w").write("\n".join(L))


if __name__ == "__main__":
    main()
