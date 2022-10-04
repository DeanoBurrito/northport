#pragma once

namespace Npk
{
    void InitEarlyPlatform();
    void InitMemory();
    void InitPlatform();

    [[noreturn]]
    void ExitBspInit();
    [[noreturn]]
    void ExitApInit();
}
