#pragma once

#include <cstdint>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "kitforge/Kit.h"

namespace kitforge
{

struct ParamDiff
{
    int param;
    int a;
    int b;
};

/** Sound parameters that differ between two kits (pseudo/unused bytes are ignored). */
std::vector<ParamDiff> diffKits(const Kit& a, const Kit& b);
/** Human-readable diff, one line per change: "Drum 1 Osc Coarse: +3 -> +7". */
std::vector<std::string> describeDiff(const std::vector<ParamDiff>& diff);

/**
 * Copies one voice's settings onto another. Parameters are matched by (section, label), so
 * Drum 1 -> Drum 2 copies everything, while Drum -> Snare copies only what both voices have.
 * Mod targets are re-mapped onto the destination voice where an equivalent exists.
 * Returns the number of parameters written.
 */
int copyVoice(Kit& kit, int fromVoice, int toVoice, const Kit* sourceKit = nullptr);

/** Equivalent param on another voice, or -1. */
int equivalentParam(int param, int toVoice);

struct RandomizeOptions
{
    std::set<int> voices { 1, 2, 3, 4, 5, 6 }; // 0 includes kit-wide (FX) params
    std::set<std::string> sections;            // empty = all sections
    std::set<int> lockedParams;
    double amount = 1.0; // 0..1: 1 = fully random, smaller = perturb around current value
    bool includeModTargets = false;
    bool includeWaveforms = true;
};

int randomizeKit(Kit& kit, const RandomizeOptions& opts, std::mt19937& rng);

/** Linear interpolation between two kits (menu/discrete params switch at t >= 0.5). */
Kit morphKits(const Kit& a, const Kit& b, double t);

} // namespace kitforge
