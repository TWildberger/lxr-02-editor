#pragma once
#include "VSynthXT.h"

namespace vsynthxt
{
struct Parameter
{
    const char* key;
    const char* label;
    const char* group;
    const char* section;
    int zone;
    Address offset;
    int width;
    int minimum, maximum, bias;
    int page;
};
const std::vector<Parameter>& parameters();
const Parameter& parameter(const std::string& key);
int parameterValue(const Patch&, const Parameter&);
void setParameter(Patch&, const Parameter&, int displayValue);
}
