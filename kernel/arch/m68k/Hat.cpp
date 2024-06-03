#include <arch/Hat.h>
#include <debug/Log.h>

namespace Npk
{
    void HatInit()
    {
        ASSERT_UNREACHABLE();
    }

    const HatLimits& GetHatLimits()
    {
        ASSERT_UNREACHABLE();
    }

    HatMap* InitNewMap()
    {
        ASSERT_UNREACHABLE();
    }

    void CleanupMap(HatMap* map)
    {
        ASSERT_UNREACHABLE();
    }

    HatMap* KernelMap()
    {
        ASSERT_UNREACHABLE();
    }

    bool Map(HatMap* map, uintptr_t vaddr, uintptr_t paddr, size_t mode, HatFlags flags, bool flush)
    {
        ASSERT_UNREACHABLE();
    }

    bool Unmap(HatMap* map, uintptr_t vaddr, uintptr_t& paddr, size_t& mode, bool flush)
    {
        ASSERT_UNREACHABLE();
    }

    sl::Opt<uintptr_t> GetMap(HatMap* map, uintptr_t vaddr, size_t& mode)
    {
        ASSERT_UNREACHABLE();
    }

    bool SyncMap(HatMap* map, uintptr_t vaddr, sl::Opt<uintptr_t> paddr, sl::Opt<HatFlags> flags, bool flush)
    {
        ASSERT_UNREACHABLE();
    }

    void SyncWithMasterMap(HatMap* map)
    {
        ASSERT_UNREACHABLE();
    }

    void MakeActiveMap(HatMap* map)
    {
        ASSERT_UNREACHABLE();
    }
}
