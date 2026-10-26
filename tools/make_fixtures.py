#!/usr/bin/env python3
"""Writes synthetic test fixtures independently of the C++ code, following the
firmware's on-card layouts (Core/Src/Preset/presetManager.c). Deterministic output."""
import json, os, random, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "tests", "fixtures")
TRACKS, STEPS, BARS = 7, 64, 4


def name8(s):
    b = s.encode("ascii")[:8]
    return b + b"\0" * (8 - len(b))


def pattern(rng, version, name, with_lengths=True):
    out = bytearray(name8(name))
    if version > 1:
        out += b"PatV%02d" % version
    for _ in range(TRACKS):
        for _ in range(STEPS):
            active = rng.random() < 0.3
            vol = rng.randrange(128) | (0x80 if active else 0)
            out += bytes([vol, rng.randrange(101), rng.randrange(128),
                          rng.randrange(247), rng.randrange(128), rng.randrange(247), rng.randrange(128)])
        if version > 2:
            out += bytes((rng.randrange(6) | (rng.randrange(16) << 4)) for _ in range(STEPS))
    for _ in range(TRACKS):
        for _ in range(BARS):
            out += struct.pack("<H", rng.randrange(65536))
    out.append(rng.randrange(128))  # shuffle
    if with_lengths:
        out += bytes(rng.randrange(1, 65) for _ in range(TRACKS))
    if version > 1:
        out.append(rng.randrange(64))  # kit nr
    return bytes(out)


def main():
    os.makedirs(OUT, exist_ok=True)
    params = json.load(open(os.path.join(ROOT, "docs", "params.json")))
    init = bytes(params["initKit"]["bytes"])
    init = init + b"\0" * (255 - len(init))
    rng = random.Random(0x4B46)

    files = {}
    files["initkit.snd"] = init
    kit = bytearray(init)
    kit[0:8] = name8("Synth 1")
    for p in params["params"]:
        if p["scope"] == "sound":
            kit[8 + p["index"]] = rng.randint(p["min"], min(p["max"], 255))
    files["synthetic.snd"] = bytes(kit)
    files["short.snd"] = init[:200]
    files["pattern_v3.pat"] = pattern(rng, 3, "Pat V3")
    files["pattern_v2.pat"] = pattern(rng, 2, "Pat V2")
    files["pattern_v1.pat"] = pattern(rng, 1, "Pat V1", with_lengths=False)
    song = bytearray(name8("Song 1")) + bytes([i % 8 for i in range(16)] + [0x40] * 48)
    files["song.sng"] = bytes(song)
    glo = bytes(p["default"] for p in params["params"] if p["scope"] == "global")
    files["glo.cfg"] = glo

    # a miniature SD card: two project folders, firmware file naming, plus junk to skip
    card = {
        "card/proj00/glo.cfg": glo,
        "card/proj00/prj.nfo": name8("Demo"),
        "card/proj00/00-Initk.snd": init,
        "card/proj00/01-Synth.snd": bytes(kit),
        "card/proj00/00-Pat_V.pat": files["pattern_v3.pat"],
        "card/proj00/00-Song_.sng": bytes(song),
        "card/proj01/p002.snd": bytes(kit),  # legacy naming, duplicate content
        "card/proj01/._03-Junk.snd": b"\0\5\x16\7",  # macOS resource fork
        "card/proj01/notes.txt": b"ignore me",
    }
    files.update(card)
    for rel, data in files.items():
        path = os.path.join(OUT, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
    print(f"wrote {len(files)} fixtures to {OUT}")


if __name__ == "__main__":
    sys.exit(main())
