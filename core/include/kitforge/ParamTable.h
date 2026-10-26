#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "kitforge/ParamIds.gen.h"

namespace kitforge
{

constexpr int kNumParams = pid::NUM_PARAMS;
constexpr int kNumSoundParams = pid::END_OF_SOUND_PARAMETERS;
constexpr int kFirstGlobal = pid::PAR_BEGINNING_OF_GLOBALS;
constexpr int kNumGlobals = kNumParams - kFirstGlobal;
constexpr int kNumVoices = 6;

enum class Scope : uint8_t
{
    Sound,      // stored in .snd kits, editable
    Sequencer,  // menu-only sequencer parameters, never stored in a kit
    Global,     // stored in glo.cfg
    Reserved,   // occupies a kit byte but is not a sound parameter (pseudo/unused)
};

enum class DType : uint8_t
{
    U0_255,
    U0_127,
    PM100,
    Menu,
    PM63,
    U1_16,
    OnOff,
    LfoTarget,
    VeloTarget,
    VoiceLfo,
    AutomTarget,
    Note,
    U0_15,
    U1_64,
    U0_100,
};

enum class MidiKind : uint8_t
{
    None,
    CC,
    NRPN,
};

struct ParamInfo
{
    int index;
    const char* id;      // firmware enum name, e.g. "PAR_COARSE1"
    const char* key;     // stable lowercase key, e.g. "coarse1"
    const char* label;   // short UI label, e.g. "Coarse" (nullptr if not user-facing)
    const char* section; // "Osc", "Amp EG", ... (nullptr if not user-facing)
    int voice;           // 0 = kit-wide, 1..6 = voice, -1 = n/a
    Scope scope;
    DType dtype;
    int minValue;
    int maxValue;
    int defaultValue;
    int menuId;
    const char* const* valueNames;
    int numValueNames;
    MidiKind midiKind;
    int midiNumber; // CC number or NRPN number
    const char* stockLimitation;
    int order;         // UI ordering, -1 if not user-facing
    int displayOffset; // PM63 display offset (value - offset)

    bool isUserFacing() const { return label != nullptr && (scope == Scope::Sound || scope == Scope::Global); }
    bool isSound() const { return scope == Scope::Sound; }
};

struct ModTargetInfo
{
    int index; // value stored in PAR_TARGET_LFOx / PAR_VEL_DEST_x
    int param; // parameter being modulated
    int voice; // 0 = global, 1..6
    const char* label;
};

struct ModTargetRange
{
    int first;
    int last;
};

class ParamTable
{
public:
    static const ParamInfo* all();
    static int size();
    static const ParamInfo& get(int index);
    static const ParamInfo* findByKey(const std::string& key);
    static const ParamInfo* findById(const std::string& id);

    static const ModTargetInfo* modTargets();
    static int numModTargets();
    static ModTargetRange modTargetRange(int voice);

    static const unsigned char* initKitBytes();
    static int initKitSize();

    /** FX P1..P4 (and type-specific selectors) are relabelled by the FX type. */
    static const char* fxParamLabel(int fxType, int param);

    /** Voice names: 0 = "Kit", 1..6 = "Drum 1".."Hi-Hat". */
    static const char* voiceName(int voice);
    static const char* voiceShortName(int voice);

    /** User-facing sound params sorted by on-device page order. */
    static std::vector<int> soundParamsInOrder();
    static std::vector<int> paramsForVoice(int voice);

    /** Clamp a value into the param's legal range (mod targets respect per-voice ranges). */
    static int clamp(int index, int value);

    /** Human-readable value as the device would display it. */
    static std::string formatValue(int index, int value);

    /** Full display name, e.g. "Drum 1 Osc Coarse". */
    static std::string fullName(int index);
};

} // namespace kitforge
