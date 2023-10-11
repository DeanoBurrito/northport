#pragma once

#include <devices/GenericDevices.h>

namespace Npk::Debug
{
    void InitEarlyTerminals();

    //TODO: post early-init, adaptor class for having graphical terminals be used as serial devices (and therefore log sinks).
}
