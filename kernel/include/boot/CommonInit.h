#pragma once

namespace Npk
{
    void InitEarlyPlatform();
    void InitMemory();
    void InitPlatform();

    void InitThread(void*);

    [[noreturn]]
    void ExitBspInit();
    [[noreturn]]
    void ExitApInit();
}
