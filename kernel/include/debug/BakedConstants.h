#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Npk::Debug
{
    extern const char* targetArchStr;
    extern const char* gitCommitHash; //hash represented as ascii text
    extern const char* gitCommitShortHash;
    extern size_t versionMajor;
    extern size_t versionMinor;
    extern size_t versionRev;
}
