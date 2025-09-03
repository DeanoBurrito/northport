#pragma once

#include <Io.hpp>

namespace Npk::Private
{
    void QueueContinuation(Iop* packet);
    void RunPendingIopContinuations();
}
