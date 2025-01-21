#pragma once

#include <services/VmPagers.h>

namespace Npk::Services
{
    SwapBackend* InitBadSwap(uintptr_t physBase, size_t length);
}
