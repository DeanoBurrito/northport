#include <KernelApi.hpp>

namespace Npk
{
    void InitPageAccessCache(size_t entries, PageAccessCache::Slot* slots, Paddr defaultPaddr)
    {
        //NPK_UNREACHABLE(); //TODO: implement
    }

    PageAccessRef AccessPage(Paddr paddr)
    {
        NPK_UNREACHABLE();
    }

    size_t CopyFromPages(Paddr base, sl::Span<char> buffer)
    {
        NPK_UNREACHABLE();
    }
}
