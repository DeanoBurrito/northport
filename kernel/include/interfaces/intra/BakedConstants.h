
#pragma once

#include <Types.h>

namespace Npk
{
    extern const char* targetArchStr;
    extern const char* targetPlatformStr;
    extern const char* gitCommitHash; //hash represented as ascii text
    extern const char* gitCommitShortHash;
    extern const bool gitCommitDirty;
    extern const size_t versionMajor;
    extern const size_t versionMinor;
    extern const size_t versionRev;
    extern const char* toolchainUsed;
}
