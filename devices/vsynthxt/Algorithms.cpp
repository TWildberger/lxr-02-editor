#include "Algorithms.h"

namespace vsynthxt
{
const std::vector<Algorithm>& algorithms(const std::string& family)
{
    // Owner's Manual printed pp.153,159-191; references are not SysEx slot mappings.
    static const std::vector<Algorithm> cosm {
        {"THRU",153,"No COSM algorithm."},
        {"Overdrive / Distortion",159,"Drive: 0-127; Tone: 0-127. Drive/Tone LFO depth: -63..+63."},
        {"Wave Shape",159,"Drive: 0-127; Tone: 0-127. Drive/Tone LFO depth: -63..+63."},
        {"Amp Simulator",160,"Amp Type: 1-3; Bass/Middle/Treble: 0-127; Gain: 0-127; Output Level: 0-127."},
        {"Speaker Simulator",160,"Speaker type and microphone settings: see the source table; wire encodings unresolved."},
        {"Resonator",160,"Size: 0-127; Balance: 0-127; Size keyfollow: -200..+200."},
        {"1st-order SideBandFilter",160,"Width: 0-127; Detune: 0-127; LPF: 0-127; Octave: -2,-1,0."},
        {"2nd-order SideBandFilter",161,"Width: 0-127; Detune: 0-127; Q: 0-127; LPF: 0-127; Octave: -2,-1,0."},
        {"Comb Filter",161,"Tone: 0-127; Detune: 0-127; Octave: -2,-1,0,+1,+2."},
        {"Dual Filter",161,"Filter 1/2 frequency: 0-127; resonance: 0-127; frequency keyfollow: -200..+200."},
        {"TVF",162,"Cutoff frequency: 0-127; resonance: 0-127; filter: LPF/BPF/HPF/NOTCH/PEAK; slope: -24/-12/-6 dB/oct."},
        {"Dynamic TVF",162,"Cutoff frequency: 0-127; resonance: 0-127; dynamic envelope: -63..+63; slope: -24/-12/-6 dB/oct."},
        {"Polyphonic Compressor",162,"Attack: 0-127; Sustain: 0-127; Output Level: 0-127."},
        {"Polyphonic Limiter",162,"Attack: 0-127; Release: 0-127; ratio: 2:1/4:1/16:1/100:1; Output Level: 0-127."},
        {"Frequency Shifter",163,"Effect depth: 0-127; Balance: 0-127; effect keyfollow: -200..+200."},
        {"Lo-Fi Processor",163,"Filter cutoff: 0-127; Balance: 0-127; filter keyfollow: -200..+200."},
        {"TB Filter",163,"LPF frequency: 0-127; resonance: 0-127; resonance follow: -63..+63; Gain: 0-127."}
    };
    static const std::vector<Algorithm> mfx {
        {"Unresolved zero code",164,"The numbered owner-manual MFX list does not name code 0."},
        {"Parametric EQ",165,"Frequency, Q and gain bands: see the algorithm table. Gain is expressed in dB."},
        {"Graphic EQ",165,"Band gains: see the algorithm table (dB)."},
        {"Resonant Filter",165,"Filter frequency and resonance: see the algorithm table."},
        {"Isolator and Filter",166,"Low/mid/high band levels: -60..+4 dB."},
        {"Distortion / Overdrive",166,"Drive mode OD/DS; distortion settings: see the algorithm table."},
        {"Guitar Amp Simulator",167,"Noise suppressor threshold/release: 0-127; preamp and speaker models: see table."},
        {"Auto Wah",168,"Filter LPF/BPF; polarity DOWN/UP; frequency: 0-127."},
        {"Humanizer",168,"Vowel settings and modulation: see the algorithm table."},
        {"Dynamic Processor",169,"Compression and dynamics: see the algorithm table."},
        {"Tape Echo Simulator",169,"Tape echo delay and feedback: see the algorithm table."},
        {"Stereo Delay",170,"Delay time and feedback: see the algorithm table."},
        {"Multi Tap Delay",171,"Tap delay times and levels: see the algorithm table."},
        {"Reverse Delay",171,"Reverse delay timing: see the algorithm table."},
        {"Vocal Echo",172,"Echo parameters: see the algorithm table."},
        {"Band Pass Delay",172,"Band-pass delay parameters: see the algorithm table."},
        {"Analog Delay -> Chorus",173,"Delay and chorus parameters: see the algorithm table."},
        {"Digital Chorus",173,"Chorus parameters: see the algorithm table."},
        {"Space Chorus",174,"Chorus parameters: see the algorithm table."},
        {"Hexa Chorus",174,"Chorus parameters: see the algorithm table."},
        {"Analog Flanger",174,"Flanger parameters: see the algorithm table."},
        {"BOSS Flanger",175,"Flanger parameters: see the algorithm table."},
        {"Step Flanger",175,"Step flanger parameters: see the algorithm table."},
        {"Analog Phaser",176,"Phaser parameters: see the algorithm table."},
        {"Digital Phaser",176,"Phaser parameters: see the algorithm table."},
        {"Rotary",177,"Rotary parameters: see the algorithm table."},
        {"Tremolo / Auto Pan",177,"Tremolo/pan parameters: see the algorithm table."},
        {"Stereo Pitch Shifter",178,"Coarse pitch A/B: -12..+12 semitones; grade: 1-5."},
        {"Overdrive/Distortion -> Chorus/Flanger",178,"Drive and modulation parameters: see the algorithm table."},
        {"Overdrive/Distortion -> Delay",179,"Drive: 0-127; delay time: 0-1300 ms or note values."},
        {"Chorus/Flanger -> Delay",179,"Modulation and delay parameters: see the algorithm table."},
        {"Enhancer -> Chorus/Flanger",180,"Enhancer and modulation parameters: see the algorithm table."},
        {"Enhancer -> Delay",180,"Enhancer and delay parameters: see the algorithm table."},
        {"Vocal Multi",181,"Limiter threshold: -60..0 dB; limiter/de-esser, EQ and delay: see table."},
        {"Guitar Multi",181,"Dynamics, drive, modulation and delay: see the algorithm table."},
        {"Bass Multi",182,"Dynamics, drive, EQ and modulation: see the algorithm table."},
        {"EP Multi",183,"EP effects: see the algorithm table."},
        {"Keyboard Multi",183,"Keyboard effects: see the algorithm table."},
        {"Phonograph",184,"Phonograph parameters: see the algorithm table."},
        {"Radio Tuning",185,"Radio parameters: see the algorithm table."},
        {"Bit Rate Converter",185,"Bit conversion parameters: see the algorithm table."},
        {"Pseudo Stereo",185,"Stereo parameters: see the algorithm table."}
    };
    static const std::vector<Algorithm> chorus {
        {"Unresolved zero code",186,"The numbered owner-manual chorus list does not name code 0."},
        {"Chorus 1",186,"Pre-delay: 0-50 ms; Rate: 0.05-10 Hz or note values; Depth: 0-127; Feedback: -98..+98%."},
        {"Chorus 2",186,"Pre-delay: 0-50 ms; Rate: 0.05-10 Hz or note values; Depth: 0-127; Feedback: -98..+98%."},
        {"Chorus 3",186,"Pre-delay: 0-50 ms; Rate: 0.05-10 Hz or note values; Depth: 0-127; Feedback: -98..+98%."},
        {"Chorus 4",186,"Pre-delay: 0-50 ms; Rate: 0.05-10 Hz or note values; Depth: 0-127; Feedback: -98..+98%."},
        {"Feedback Chorus",186,"Pre-delay: 0-50 ms; Rate: 0.05-10 Hz or note values; Depth: 0-127; Feedback: -98..+98%."},
        {"Flanger",186,"Pre-delay: 0-50 ms; Rate: 0.05-10 Hz or note values; Depth: 0-127; Feedback: -98..+98%."},
        {"Short Delay",186,"Chorus parameter table: pre-delay 0-50 ms; feedback -98..+98%."},
        {"Feedback Short Delay",186,"Chorus parameter table: pre-delay 0-50 ms; feedback -98..+98%."}
    };
    static const std::vector<Algorithm> reverb {
        {"Unresolved zero code",186,"The numbered owner-manual reverb list does not name code 0."},
        {"Room 1",187,"Low/high reverb time: 0.06-32 s; pre-delay: 0-200 ms; room size: 5.6-32.6 m."},
        {"Room 2",187,"Reverb time: 0.06-32 s; pre-delay: 0-200 ms; room size: 1-10 (index)."},
        {"Room 3",188,"Reverb time: 0.06-32 s; pre-delay: 0-200 ms; room size: 1-8 (index)."},
        {"Hall 1",188,"Reverb parameters: see the Hall 1 table; physical values are not wire-slot mappings."},
        {"Hall 2",189,"Reverb time: 0.06-64 s; pre-delay: 0-200 ms; room size: 1-10 (index)."},
        {"Hall 3",189,"Reverb parameters: see the Hall 3 table; physical values are not wire-slot mappings."},
        {"Garage",190,"Reverb time: 0.06-32 s; pre-delay: 0-200 ms; room size: 1-8 (index)."},
        {"Plate",190,"Reverb parameters: see the Plate table; physical values are not wire-slot mappings."},
        {"Non-Linear",191,"Feedback time: 0.1-1000 ms; feedback level: 0-99%; envelope time ratio: 10-120%."},
        {"Delay",191,"Delay: 0-1300 ms (MONO), 0-650 ms (STEREO/ALTERNATE), or notes; feedback: -98..+98%."}
    };
    static const std::vector<Algorithm> empty;
    if (family == "COSM") return cosm;
    if (family == "MFX") return mfx;
    if (family == "Chorus") return chorus;
    if (family == "Reverb") return reverb;
    return empty;
}
std::string algorithmFamily(const Parameter& p)
{
    const std::string group(p.group);
    if (group == "COSM") return "COSM";
    if (group != "Effects") return {};
    if (p.offset >= 256 && p.offset < 388) return "MFX";
    if (p.offset >= 512 && p.offset < 563) return "Chorus";
    if (p.offset >= 768 && p.offset < 866) return "Reverb";
    return {};
}
Address algorithmTypeOffset(const Parameter& p)
{
    const auto family = algorithmFamily(p);
    if (family == "COSM") return (p.offset / 128) * 128 + 1;
    if (family == "MFX") return 256;
    if (family == "Chorus") return 512;
    if (family == "Reverb") return 768;
    throw std::runtime_error("Parameter has no algorithm type");
}
const Algorithm* selectedAlgorithm(const Patch& patch, const Parameter& p)
{
    const auto& types = algorithms(algorithmFamily(p));
    if (types.empty() || !patch.complete()) return nullptr;
    const int code = patch.value(algorithmTypeOffset(p));
    return code >= 0 && static_cast<size_t>(code) < types.size() ? &types[static_cast<size_t>(code)] : nullptr;
}
}
