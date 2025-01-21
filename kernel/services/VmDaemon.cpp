#include <services/VmDaemon.h>
#include <core/Event.h>
#include <core/Log.h>
#include <core/Pmm.h>
#include <core/Config.h>
#include <KernelThread.h>

namespace Npk::Services
{
    constexpr size_t DefaultWakeTimeoutMs = 500;

    static void VmDaemonThread(void* arg)
    {
        (void)arg;

        const size_t wakeMs = Core::GetConfigNumber("kernel.vmd.wake_timeout_ms", DefaultWakeTimeoutMs);
        Core::WaitEntry waitEntry {};
        Core::Waitable wakeEvent {};
        wakeEvent.Reset(0, 1);
        Core::Pmm::Global().AttachVmDaemon(wakeEvent);

        //TODO: take in 'vm context' as arg, will contain active/standby lists + Lock, also good for segmenting memory regions.
        while (true)
        {
            Core::WaitOne(&wakeEvent, &waitEntry, sl::TimeCount(sl::Millis, wakeMs));
            Log("VmDaemon is doing things", LogLevel::Debug);
        }
    }

    void StartVmDaemon()
    {
        auto maybeThread = CreateKernelThread(VmDaemonThread, nullptr);
        VALIDATE(maybeThread.HasValue(), , "Failed to start virtual memory daemon thread.");
        Core::SchedEnqueue(*maybeThread, 0);

        Log("VM daemon thread running.", LogLevel::Verbose);
    }
}
