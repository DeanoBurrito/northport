#pragma once

namespace Npk
{
    void InitEarlyPlatform();
    void InitMemory();
    void InitPlatform();

    void InitThread(void*);
    bool CoresInEarlyInit();

    [[noreturn]]
    void ExitBspInit();
    [[noreturn]]
    void ExitApInit();
}
