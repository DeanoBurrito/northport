#pragma once

#include <Io.hpp>
#include <Vm.hpp>

namespace Npk::Private
{
    constexpr HeapTag IoHeapTag = NPK_MAKE_HEAP_TAG("IOIO");

    bool RefIoInterface(IoInterface* ioi);
    bool UnrefIoInterface(IoInterface* ioi);
    void QueueContinuation(Iop* packet);
    void RunPendingIopContinuations();
}
