#pragma once

#include <string>

#include "kitforge/Bytes.h"

inline std::string fixture(const std::string& rel) { return std::string(KITFORGE_FIXTURES_DIR) + "/" + rel; }

inline kitforge::Bytes loadFixture(const std::string& rel)
{
    auto b = kitforge::readFileBytes(fixture(rel));
    return b ? *b : kitforge::Bytes {};
}
