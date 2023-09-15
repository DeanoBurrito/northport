#pragma once

#include <stdint.h>
#include <memory/VmObject.h>
#include <Span.h>
#include <String.h>
#include <containers/Vector.h>
#include <formats/Elf.h>

namespace Npk::Drivers
{
    struct LoadableModule
    {
        sl::String filepath;
        VmObject image;
    };

    bool LoadModule(LoadableModule& module);
    bool UnloadModule(LoadableModule& module);

    bool ScanForDrivers(sl::StringSpan filepath);
    void ScanForModules(sl::StringSpan directory);
}
