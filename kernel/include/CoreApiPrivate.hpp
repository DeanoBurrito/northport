#pragma once

#include <CoreTypes.hpp>

/* This file (and the ::Private namespace) contains declarations only intended
 * for use by other components of the kernel core. No promises are made about
 * the effects of anything here remaining stable.
 */
namespace Npk::Private
{
    void InitLocalScheduler(ThreadContext* idle);
    void OnPassiveRunLevel();
}
