#pragma once
#include "Parameters.h"

namespace vsynthxt
{
struct Algorithm
{
    const char* name;
    int page;
    const char* reference;
};
const std::vector<Algorithm>& algorithms(const std::string& family);
std::string algorithmFamily(const Parameter&);
Address algorithmTypeOffset(const Parameter&);
const Algorithm* selectedAlgorithm(const Patch&, const Parameter&);
}
