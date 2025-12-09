#pragma once

#include <Namespace.hpp>
#include <Vm.hpp>

namespace Npk::Private
{
    constexpr HeapTag NamespaceHeapTag = NPK_MAKE_HEAP_TAG("Name");

    void InitNamespace();
}
