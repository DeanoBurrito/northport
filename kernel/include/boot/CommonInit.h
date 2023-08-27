#pragma once

namespace Npk
{
    void InitEarlyPlatform();
    void InitMemory();
    void InitPlatform();

    void InitThread(void*);
    bool CoresInEarlyInit();

    void PerCoreCommonInit();
    [[noreturn]]
    void ExitCoreInit();
}
