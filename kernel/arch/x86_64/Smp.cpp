#include <arch/Smp.h>
#include <boot/LimineTags.h>
#include <debug/Log.h>

namespace Npk
{
    void InitSmp()
    {} //on x86_64 we'll use limine's smp features, nothing for us to do here.

    void BootAllProcessors(uintptr_t entry)
    {
        if (Boot::smpRequest.response == nullptr)
            return;
        
        for (size_t i = 0; i < Boot::smpRequest.response->cpu_count; i++)
        {
            limine_smp_info* procInfo = Boot::smpRequest.response->cpus[i];
            if (procInfo->lapic_id == Boot::smpRequest.response->bsp_lapic_id)
                continue;
            
            procInfo->goto_address = reinterpret_cast<limine_goto_address>(entry);
            Log("Sending bring-up request to core %u.", LogLevel::Verbose, procInfo->lapic_id);
        }
    }
}
