#include <Log.h>
#include <scheduling/Scheduler.h>
#include <devices/Ps2Controller.h>
#include <devices/PciBridge.h>
#include <drivers/DriverManager.h>
#include <devices/DeviceManager.h>
#include <filesystem/Vfs.h>
#include <Loader.h>

using namespace Kernel::Scheduling;
ThreadGroup* initTaskThreadGroup;

void InitUserspaceTask()
{
    //TODO: we should wait until all the main kernel tasks have finished running before starting this. Look into the main thread group exiting?
    //something of flag will need to be set. Alternatively we could use a callback for when the init thread group gets cleaned up?
    using namespace Kernel::Filesystem;
    auto maybeStartupFile = VFS::Global()->FindNode("/initdisk/apps/startup.elf");
    if (!maybeStartupFile)
    {
        Log("Couldnt find startup.elf, kernel will finish init and then halt.", Kernel::LogSeverity::Warning);
        return;
    }

    auto maybeThreadId = Kernel::LoadElfFromFile("/initdisk/apps/startup.elf", ThreadFlags::None);
    if (!maybeThreadId)
    {
        Log("Couldnt load startup.elf, check error log.", Kernel::LogSeverity::Warning);
        return;
    }

    Thread* startupThread = Scheduler::Global()->GetThread(*maybeThreadId);
    startupThread->Start(nullptr);
    
    auto maybeWindowid = Kernel::LoadElfFromFile("/initdisk/apps/server-window.elf", ThreadFlags::None);
    if (!maybeWindowid)
    {
        Log("Couldnt load server-window.elf, check error log.", Kernel::LogSeverity::Warning);
        return;
    }

    Thread* serverStartupThread = Scheduler::Global()->GetThread(*maybeWindowid);
    serverStartupThread->Start(nullptr);
}

void InitPs2Task()
{
    using namespace Kernel::Devices;
    
    size_t ps2PortCount = Ps2Controller::InitController();
    if (ps2PortCount > 0)
        Ps2Controller::Keyboard()->Init(false);
    if (ps2PortCount > 1)
        Ps2Controller::Mouse()->Init(true);
}

void InitPciTask()
{
    Kernel::Devices::PciBridge::Global()->Init();
}

void InitManagersTask()
{
    using namespace Kernel::Devices;
    using namespace Kernel::Drivers;
    
    DeviceManager::Global()->Init();
    DriverManager::Global()->Init();

    //start initdisk driver
    uint8_t initDiskMachineName[] = { 'i', 'n', 'i', 't', 'd', 'i', 's', 'k' };
    auto maybeDriver = DriverManager::Global()->FindDriver(DriverSubsytem::Filesystem, { 8, initDiskMachineName });
    if (maybeDriver)
        DriverManager::Global()->StartDriver(*maybeDriver, nullptr);
    else //we should never not have this, its baked into the kernel.
        Log("Initdisk filesystem driver is not available!", Kernel::LogSeverity::Error);

    Scheduler::Global()->CreateThread((size_t)InitPciTask, ThreadFlags::KernelMode, initTaskThreadGroup)->Start(nullptr);
    Scheduler::Global()->CreateThread((size_t)InitUserspaceTask, ThreadFlags::KernelMode, initTaskThreadGroup)->Start(nullptr);
}

extern "C" void QueueInitTasks()
{
    initTaskThreadGroup = Scheduler::Global()->CreateThreadGroup();

    Scheduler::Global()->CreateThread((size_t)InitPs2Task, ThreadFlags::KernelMode, initTaskThreadGroup)->Start(nullptr);
    Scheduler::Global()->CreateThread((size_t)InitManagersTask, ThreadFlags::KernelMode, initTaskThreadGroup)->Start(nullptr);
}
