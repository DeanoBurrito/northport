#include <arch/Platform.h>
#include <boot/CommonInit.h>
#include <boot/LimineTags.h>
#include <boot/LimineBootstrap.h>

namespace Npk
{
    void InitCore(size_t id)
    {

    }

    void ApEntry()
    {

    }
}

extern "C"
{
    struct EntryData
    {
        uintptr_t hartId;
        uintptr_t dtb;
        uintptr_t physBase;
        uintptr_t virtBase;
    };

    void KernelEntry(EntryData* data)
    {
        using namespace Npk;
        Boot::PerformLimineBootstrap(data->physBase, data->virtBase, data->hartId, data->dtb);
        (void)data;

        Halt();
    }
}
