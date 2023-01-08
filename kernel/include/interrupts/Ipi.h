#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Npk::Interrupts
{
    void InitIpiMailbox();
    void ProcessIpiMail();
    void SendIpiMail(size_t core, void (*callback)(void*), void* arg);
    void BroadcastPanicIpi();
}
