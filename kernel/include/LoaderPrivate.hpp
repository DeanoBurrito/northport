#pragma once

#include <Loader.hpp>
#include <Namespace.hpp>
#include <Vm.hpp>

namespace Npk::Private
{
    NpkStatus LoadElf(VmSpace& space, uintptr_t loadBase, NsObject& source);
    NpkStatus LoadPe(VmSpace& space, uintptr_t loadBase, NsObject& source);
}
